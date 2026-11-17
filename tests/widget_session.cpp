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

// What a session's screen says right now, straight from the daemon. The snapshot is
// escape bytes, but the text in it is still text, which is enough to ask whether
// something was typed into a session nobody is looking at.
std::string writtenInto(const std::string& name) {
    DaemonClient client;
    std::string error;
    if (!client.connect(error))
        return {};
    if (!client.request({{"command", "attach"},
                         {"name", name},
                         {"rows", "24"},
                         {"cols", "80"}}))
        return {};
    Frame welcome;
    if (!client.nextFrame(welcome, error))
        return {};
    Frame snapshot;
    std::string text;
    if (client.nextFrame(snapshot, error) &&
        (snapshot.kind == MsgKind::Snapshot || snapshot.kind == MsgKind::Data))
        text = snapshot.body;
    client.close();
    return text;
}

bool sees(const Daemon& daemon, const std::string& name,
          const std::function<bool(const SessionInfo&)>& about) {
    for (const SessionInfo& info : daemon.list()) {
        if (info.name == name)
            return about(info);
    }
    return false;
}

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

    {
        // One window, two shells. A kept session gets restored into the window that is
        // already open, so the window has to stop looking at one shell and start
        // looking at another — without dying, without showing the wrong screen, and
        // without taking the shell it leaves down with it.
        SessionSpec first_spec;
        first_spec.name = "first";
        first_spec.saved = false;
        first_spec.rows = 24;
        first_spec.cols = 80;
        expect("a shell starts for the window", daemon.create(first_spec, error));
        pump(400);

        kai::TerminalWidget window(24, 80, std::make_unique<SessionBackend>("first", 24, 80));
        window.resize(800, 500);
        window.show();
        window.pasteText("echo FIRST-SHELL\n");
        expect("the window shows the shell it started with",
               waitFor([&window] { return onScreen(window).find("FIRST-SHELL") != std::string::npos; }));

        SessionSpec second_spec;
        second_spec.name = "second";
        second_spec.rows = 24;
        second_spec.cols = 80;
        expect("a kept session exists to be restored", daemon.create(second_spec, error));
        pump(400);
        expect("and it has something on its screen already",
               daemon.type("second", "echo SECOND-SHELL\n"));
        pump(500);

        window.showSession(std::make_unique<SessionBackend>("second", 24, 80), false);
        expect("the window shows the session it was pointed at",
               waitFor([&window] { return onScreen(window).find("SECOND-SHELL") != std::string::npos; }));
        expect("and nothing of the one it left",
               onScreen(window).find("FIRST-SHELL") == std::string::npos);
        expect("typing after the switch reaches the new shell",
               waitFor([&window, &daemon] {
                   static bool typed = false;
                   if (!typed) {
                       typed = true;
                       window.pasteText("echo AFTER-THE-SWITCH\n");
                   }
                   return onScreen(window).find("AFTER-THE-SWITCH") != std::string::npos;
               }));
        expect("and not to the shell behind it",
               writtenInto("first").find("AFTER-THE-SWITCH") == std::string::npos);
        // Left behind, and waiting: the view that had been looking at it has to be
        // noticed as gone by the daemon before the count says so, which is a matter of
        // when that thread wakes up rather than of what it decides.
        expect("the shell it left is left behind rather than lost",
               waitFor([&daemon] {
                   return sees(daemon, "first", [](const SessionInfo& info) {
                       return info.alive && info.clients == 0 && !info.saved;
                   });
               }));

        SessionSpec third_spec;
        third_spec.name = "third";
        third_spec.saved = false;
        third_spec.rows = 24;
        third_spec.cols = 80;
        expect("a third shell starts", daemon.create(third_spec, error));
        pump(300);
        // Moving on from a session the person kept is not a way of destroying it. They
        // asked for that one to be kept, and a window that goes to look at something
        // else has no authority over that decision.
        window.showSession(std::make_unique<SessionBackend>("third", 24, 80), true);
        expect("the window is not left without a shell",
               waitFor([&daemon] {
                   return sees(daemon, "third",
                               [](const SessionInfo& info) { return info.clients == 1; });
               }));
        expect("and the kept session outlives the window moving on",
               sees(daemon, "second",
                    [](const SessionInfo& info) { return info.alive && info.saved; }));

        SessionSpec fourth_spec;
        fourth_spec.name = "fourth";
        fourth_spec.saved = false;
        fourth_spec.rows = 24;
        fourth_spec.cols = 80;
        expect("a fourth shell starts", daemon.create(fourth_spec, error));
        pump(300);
        // The unsaved one is the other story: the window said it was done with it.
        window.showSession(std::make_unique<SessionBackend>("fourth", 24, 80), true);
        expect("switching away on purpose ends an unsaved shell",
               waitFor([&daemon] {
                   return !sees(daemon, "third", [](const SessionInfo&) { return true; });
               }));
        window.shutdown();
        expect("closing the window leaves the shell it was showing alone",
               waitFor([&daemon] {
                   return sees(daemon, "fourth", [](const SessionInfo& info) {
                       return info.alive && info.clients == 0;
                   });
               }));
        static_cast<void>(daemon.kill("first", error));
        static_cast<void>(daemon.kill("second", error));
        static_cast<void>(daemon.kill("third", error));
        static_cast<void>(daemon.kill("fourth", error));
        static_cast<void>(waitFor([&daemon] { return daemon.list().empty(); }));
    }

    {
        // The window's own two keybinds, and the line it says for itself. Both are
        // checked offscreen; what they look like is for a person to look at.
        kai::TerminalWidget window(24, 80);
        window.show();
        pump(300);
        int saved_calls = 0;
        int menu_calls = 0;
        window.on_save_session = [&saved_calls] { ++saved_calls; };
        window.on_session_menu = [&menu_calls] { ++menu_calls; };
        sendKey(&window, Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
        sendKey(&window, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
        expect("Ctrl+Shift+D belongs to the window", saved_calls == 1);
        expect("and Ctrl+Shift+A asks for the list of sessions", menu_calls == 1);
        // Sent on, Ctrl+Shift+D would be an end-of-file at the prompt and the shell
        // would be gone, so the shell being alive afterwards is the proof it never
        // arrived.
        window.pasteText("echo AFTER-THE-BINDS\n");
        expect("neither keybind reaches the shell",
               waitFor([&window] {
                   return onScreen(window).find("AFTER-THE-BINDS") != std::string::npos;
               }));
        window.setStatusText("session 'work' saved");
        expect("the window can say a line for itself",
               window.statusText() == "session 'work' saved");
        window.setStatusText("gone shortly", 0);
        pump(200);
        expect("and stops saying it after a while", window.statusText().empty());
        window.shutdown();
    }

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
