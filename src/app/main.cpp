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

static void print_help()
{
    std::cout << "Kai-Term " << KAI_TERM_VERSION << "\n"
              << "Usage: kai-term                                    a window\n"
              << "       kai-term list | new [NAME] | attach ID [--window]\n"
              << "                | detach | save [ID] [NAME] | send ID TEXT [--enter]\n"
              << "                | kill ID | start | shutdown | serve\n"
              << "A native terminal emulator and PTY-backed shell.\n"
              << "Inside a window, Ctrl+Shift+S keeps this shell and starts a fresh one;\n"
              << "Ctrl+Shift+A brings back one that was kept. From any terminal,\n"
              << "`kai-term list` shows the shells a daemon is keeping and\n"
              << "`kai-term attach <id>` shows one in this terminal; --window opens\n"
              << "a window onto it instead.\n";
}

// The words that mean sessions. Anything else — no word at all, a file, a flag
// for Qt — is a window being asked for, which is how `kai-term` has always read.
static bool is_session_verb(std::string_view word)
{
    static const std::string_view verbs[] = {"list",   "new",     "attach", "detach",
                                             "save",   "send",    "kill",   "start",
                                             "shutdown", "serve", "session", "open"};
    for (const std::string_view verb : verbs)
        if (word == verb)
            return true;
    return false;
}

int main(int argc, char **argv)
{
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
                      boot.rows.value_or(24), boot.cols.value_or(80)))
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
