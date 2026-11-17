#include "../terminal/terminalwidget.hpp"
#include "../platform/platform.hpp"
#include "../session/cli.hpp"

#include <QApplication>
#include <QMainWindow>
#include <iostream>
#include <string_view>
#include <cstdlib>

static void print_help()
{
    std::cout << "Kai-Term " << KAI_TERM_VERSION << "\n"
              << "Usage: kai-term [--version] [--help] [session <command>]\n"
              << "A native terminal emulator and PTY-backed shell.\n"
              << "Run `kai-term session --help` for sessions that outlive the window.\n";
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
    // over a socket, and none of it needs a display. `kai-term session list` on a
    // headless machine should work, and does.
    if (first == "session")
        return kai::runSessionCommand(argc, argv);

    if (std::getenv("KAI_TERM_TEST_NO_WINDOW") != nullptr)
        return 0;

    try {
        kai::prepareQtPlatform();
        QApplication app(argc, argv);
        QMainWindow window;
        window.setWindowTitle("Kai-Term");

        auto* term = new kai::TerminalWidget();
        term->setFocus();
        window.setCentralWidget(term);
        window.resize(1000, 700);
        window.show();

        app.exec();
        term->shutdown();
    } catch (const std::exception& e) {
        std::cerr << "Kai-Term: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
