#include "../terminal/terminalwidget.hpp"
#include "../platform/platform.hpp"
#include "../session/cli.hpp"
#include "../session/sessionbackend.hpp"
#include "appwindow.hpp"

#include <QApplication>
#include <iostream>
#include <memory>
#include <string_view>
#include <cstdlib>

// The one help text, in the CLI's hands so that `kai-term help`, `kai-term --help`
// and `kai-term <command> --help` cannot drift apart from it.
static void print_help()
{
    static_cast<void>(kai::printHelp(KAI_TERM_VERSION));
}

// The words that mean sessions. Anything else — no word at all, a file, a flag
// for Qt — is a window being asked for, which is how `kai-term` has always read.
// `help` is one of them because the help is about those words, and a person typing
// `kai-term help` should get a page rather than a window.
static bool is_session_verb(std::string_view word)
{
    static const std::string_view verbs[] = {"list",       "new",     "attach",
                                             "detach",     "save",    "send",
                                             "kill",       "layout",  "set-layout",
                                             "start",      "shutdown", "serve",
                                             "session",    "open",    "help"};
    for (const std::string_view verb : verbs)
        if (word == verb)
            return true;
    return false;
}

int main(int argc, char **argv)
{
    // Where the configuration comes from is settled before anything reads one, and
    // before Qt sees the arguments at all: Qt would reject an option it does not
    // know, so both forms are taken out of argv here rather than parsed later.
    // `-c FILE` names one settings file and takes its colours from the `theme.conf`
    // beside it; `--config-dir DIR` names the directory both live in. Either may sit
    // before or after a command word, and because they are stripped first, the saved
    // theme still supersedes everything when no flag is given at all.
    for (int i = 1; i < argc;) {
        const std::string_view word = argv[i];
        const bool dir_form = word == "--config-dir";
        const bool file_form = word == "-c" || word == "--config";
        if (!dir_form && !file_form) {
            ++i;
            continue;
        }
        if (i + 1 >= argc) {
            std::cerr << "kai-term: " << word << " needs a value\n\n";
            print_help();
            return 2;
        }
        if (dir_form)
            kai::setConfigDir(argv[i + 1]);
        else
            kai::setConfigFile(argv[i + 1]);
        for (int j = i; j + 2 <= argc; ++j)
            argv[j] = argv[j + 2]; // keeps the closing nullptr where it belongs
        argc -= 2;
    }

    std::string_view first;
    if (argc > 1)
        first = argv[1];

    if (first == "--version") {
        std::cout << "Kai-Term " << KAI_TERM_VERSION << '\n';
        return 0;
    }
    if (first == "--help" || first == "-h") {
        print_help();
        return 0;
    }

    // A word that starts with a dash and is none of the above is a mistake, and the
    // list of what can be typed is the answer to a mistake. It used to open a window,
    // which is how `kai-term --confg-dir x` came to look like a working terminal
    // wearing the wrong settings. (Kaida, 2026-11-20: "using an invalid argument
    // should open the --help menu listing all possible argument to use with
    // kai-term".) Qt's own options still reach it the way they always have from a
    // script and from the tests — through the environment, where `QT_QPA_PLATFORM`
    // and its neighbours live.
    if (first.size() > 1 && first[0] == '-' && first != "--attach-to") {
        std::cerr << "kai-term: unknown option " << first << "\n\n";
        print_help();
        return 2;
    }

    // Sessions are handled before Qt exists: they are a conversation with a daemon
    // over a socket, and none of it needs a display. `kai-term list` on a headless
    // machine should work, and does.
    if (is_session_verb(first))
        return kai::runSessionCommand(argc, argv);

    // `kai-term attach <id> --window` is the graphical counterpart of a plain
    // attach: that one takes over the terminal it was run from, this one becomes a
    // window. Both are views of the same session, so a shell can be started in a
    // window, picked up in a terminal over ssh, and left running when both go
    // away. The word is --attach-to rather than a nice one because it is not for
    // typing: the CLI resolves the number to a name and then becomes this program
    // with that word, so it is the seam between the two halves of one command.
    std::string_view attach_to;
    if (first == "--attach-to") {
        if (argc < 3) {
            std::cerr << "kai-term --attach-to: which session? try `kai-term list`\n";
            return 2;
        }
        attach_to = argv[2];
    }

    if (std::getenv("KAI_TERM_TEST_NO_WINDOW") != nullptr)
        return 0;

    try {
        kai::prepareQtPlatform();
        QApplication app(argc, argv);

        // The window does not know where its bytes come from; the backend decides
        // that. An ordinary window gets a shell held by the session daemon, started
        // here without being asked for, so that keeping a shell is a thing a keybind
        // can do rather than a thing that needs planning ahead. Starting a window onto
        // a named session that is not running throws rather than opening an empty
        // frame, because an empty window looks like a working terminal and is not one.
        //
        // This object IS the window, and that is not a style choice: a close is sent to
        // the top-level widget and to nothing below it. Wrapped inside a QMainWindow as
        // its central widget, the terminal's own `closeEvent` — the one that asks before
        // throwing a running program away — was never called by anything, and every
        // window closed silently whatever was happening inside it.
        // The grid a new shell starts with is decided here, before the window
        // exists: the PTY has to open at the size the config asked for,
        // because a shell born at 80 columns has already drawn its first
        // prompt at eighty, and growing the window afterwards cannot unsay
        // lines that were wrapped at the wrong width.
        const kai::Config boot = kai::loadConfig();
        std::unique_ptr<kai::AppWindow> term =
            attach_to.empty()
                ? std::make_unique<kai::AppWindow>(kai::startWindowBackend(
                      boot.rows.value_or(24), boot.cols.value_or(80),
                      boot.default_view_mode))
                : std::make_unique<kai::AppWindow>(std::string(attach_to));
        auto title = [term = term.get()] {
            return term->sessionName().empty()
                       ? std::string("Kai-Term")
                       : "Kai-Term - " + term->sessionName();
        };
        term->setWindowTitle(QString::fromStdString(title()));
        term->on_session_changed = [term = term.get(), title] {
            term->setWindowTitle(QString::fromStdString(title()));
        };
        term->setFocus();
        // The opening size the program has always had, unless the config
        // named a grid — in which case the window is already the size that
        // fits it, measured at the configured font, and resizing over the
        // top would only prove the config was read and then ignored.
        if (!boot.cols && !boot.rows)
            term->resize(1000, 700);
        term->show();

        app.exec();
        term->shutdown();
    } catch (const std::exception& e) {
        std::cerr << "Kai-Term: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
