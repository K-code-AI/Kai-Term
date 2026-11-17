// A window showing a session that lives in the daemon rather than in this process.
//
// The point of the backend seam is that the window should not be able to tell the
// difference, so this test runs the ordinary widget against a real daemon and a real
// pty, and asks the ordinary questions: is what was already on the screen shown, does
// typing arrive, does the size follow the window, and does closing the window leave the
// session alone. Everything here is checked with the offscreen platform — what the
// pixels actually look like is a thing a person has to look at.

#include "../src/session/daemon.hpp"
#include "../src/session/sessionbackend.hpp"
#include "../src/terminal/terminalwidget.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <chrono>
#include <functional>
#include <iostream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace kai;

namespace {

int checks = 0;
int failures = 0;

void expect(std::string_view what, bool value) {
    ++checks;
    if (value)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << "\n";
}

void pump(int ms, int step_ms = 20) {
    for (int elapsed = 0; elapsed < ms; elapsed += step_ms) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
    }
}

bool waitFor(std::function<bool()> pred, int timeout_ms = 6000) {
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += 20) {
        QCoreApplication::processEvents();
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    QCoreApplication::processEvents();
    return pred();
}

std::string onScreen(const kai::TerminalWidget& w) { return w.screen().text(); }

void sendKey(kai::TerminalWidget* w, int key, Qt::KeyboardModifiers mods = Qt::NoModifier,
             const QString& text = QString()) {
    QKeyEvent ev(QEvent::KeyPress, key, mods, text);
    QCoreApplication::sendEvent(w, &ev);
}


} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const std::string root = "/tmp/kai-widget-session-" + std::to_string(static_cast<long>(::getpid()));
    ::mkdir(root.c_str(), 0700);
    ::setenv("XDG_RUNTIME_DIR", root.c_str(), 1);
    ::setenv("KAI_SHELL", "/bin/sh", 1);

    Daemon daemon;
    std::string error;
    if (!daemon.start(error)) {
        std::cerr << "cannot start the daemon: " << error << "\n";
        return 1;
    }
    std::thread serving([&daemon] { daemon.run(); });
    pump(150);

    SessionSpec spec;
    spec.name = "shown";
    spec.rows = 24;
    spec.cols = 80;
    expect("a session starts for the window to show", daemon.create(spec, error));
    pump(400); // let the shell get as far as a prompt

    // Whatever a session printed before this window existed has to be on the screen
    // when the window appears. A blank window that fills in on the next output would
    // be the obvious way to get this wrong, and would look like a crashed session.
    expect("the session takes a line to print",
           daemon.type("shown", "echo ALREADY-ON-SCREEN\n"));
    pump(700);

    kai::TerminalWidget window(24, 80, std::make_unique<SessionBackend>("shown", 24, 80));
    window.resize(800, 500);
    window.show();
    expect("the window shows what the session had already printed",
           waitFor([&window] { return onScreen(window).find("ALREADY-ON-SCREEN") != std::string::npos; }));

    // Typing goes out over the socket, into the shell, and what the shell says comes
    // back the same way. Both halves have to work for the text to land here.
    window.pasteText("echo FROM-THE-WINDOW\n");
    expect("typing reaches the session and comes back",
           waitFor([&window] { return onScreen(window).find("FROM-THE-WINDOW") != std::string::npos; }));

    {
        // Keys, not just paste: a Return has to reach the shell as a line ending.
        window.pasteText("echo KEY-");
        sendKey(&window, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
        expect("a key press reaches the session too",
               waitFor([&window] {
                   const std::string text = onScreen(window);
                   return text.find("echo KEY-\r\n") != std::string::npos ||
                          text.find("echo KEY-\n") != std::string::npos ||
                          text.find("KEY-\r") != std::string::npos;
               }));
    }

    {
        // The window's size is the session's size, so a program inside that asks
        // sees the same thing the person is looking at.
        const int rows = window.rows();
        const int cols = window.cols();
        bool resized = waitFor([&daemon, rows, cols] {
            for (const SessionInfo& info : daemon.list()) {
                if (info.name == "shown" && info.rows == rows && info.cols == cols)
                    return true;
            }
            return false;
        });
        expect("the session took the window's size", resized);
        expect("and the window agrees with its own model",
               window.rows() == static_cast<int>(window.screen().rows()) &&
                   window.cols() == static_cast<int>(window.screen().cols()));
    }

    {
        // The window is a view, not the owner. Closing it has to leave the shell and
        // whatever it is running exactly where they were.
        window.shutdown();
        bool one_viewer = waitFor([&daemon] {
            for (const SessionInfo& info : daemon.list()) {
                if (info.name == "shown")
                    return info.clients == 0;
            }
            return false;
        });
        expect("closing the window leaves nobody watching", one_viewer);
        expect("and leaves the session running",
               !daemon.list().empty() && daemon.list()[0].alive);
        expect("which still takes typing", daemon.type("shown", "echo AFTER-THE-WINDOW\n"));
        kai::TerminalWidget again(24, 80, std::make_unique<SessionBackend>("shown", 24, 80));
        again.show();
        expect("and a new window finds the same session with the same screen",
               waitFor([&again] {
                   const std::string text = onScreen(again);
                   return text.find("AFTER-THE-WINDOW") != std::string::npos &&
                          text.find("ALREADY-ON-SCREEN") != std::string::npos;
               }));
        again.shutdown();
    }

    expect("the session is still there for whoever comes next",
           !daemon.list().empty() && daemon.list()[0].alive);
    expect("and killing it is the only thing that ends it", daemon.kill("shown", error));
    expect("with nothing left running", waitFor([&daemon] { return daemon.list().empty(); }));

    daemon.stop();
    serving.join();

    ::unlink((root + "/kai-term/daemon.sock").c_str());
    ::rmdir((root + "/kai-term").c_str());
    ::rmdir(root.c_str());

    if (failures == 0) {
        std::cout << "PASS: " << checks << " window-showing-a-session checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " checks failed\n";
    return 1;
}
