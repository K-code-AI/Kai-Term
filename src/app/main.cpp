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
              << "Usage: kai-term [--version] [--help] [open <session>] [session <command>]\n"
              << "A native terminal emulator and PTY-backed shell.\n"
              << "Ctrl+Shift+S keeps this shell and starts a fresh one, Ctrl+Shift+A\n"
              << "brings back one that was kept. Run `kai-term session --help` for the\n"
              << "same from a terminal, and `kai-term open NAME` for a window onto a\n"
              << "session that is already running.\n"
              << "and `kai-term open NAME` for a window onto one that is already running.\n";
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

    // `kai-term open NAME` is the graphical counterpart of `session attach NAME`:
    // that one takes over the terminal it was run from, this one opens a window. Both
    // are views of the same session, so a shell can be started in a window, picked up
    // in a terminal over ssh, and left running when both go away.
    std::string_view attach_to;
    if (first == "open") {
        if (argc < 3) {
            std::cerr << "kai-term open: which session? try `kai-term session list`\n";
            return 2;
        }
        attach_to = argv[2];
    }

    // Sessions are handled before Qt exists: they are a conversation with a daemon
    // over a socket, and none of it needs a display. `kai-term session list` on a
    // headless machine should work, and does.
    if (first == "session")
        return kai::runSessionCommand(argc, argv);

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
        std::unique_ptr<kai::AppWindow> term =
            attach_to.empty()
                ? std::make_unique<kai::AppWindow>(kai::startWindowBackend(24, 80))
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
