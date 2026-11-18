// What a second view of a session arrives knowing.
//
// A snapshot that carries the glyphs and nothing else shows the right picture in a window
// that has no idea what the program inside it asked for: the mouse stays with the window
// manager's selection instead of the application, a full-screen program leaves the lines
// it did not draw still visible, and the keys it asked for in a form the view is not
// sending. Kaida found all three from btop in a re-attached session, in one sitting,
// which is what it is like to be told a screen is a picture rather than a terminal.
//
// Everything here is offscreen, as always: what the pixels look like is for a person.

#include "../src/session/daemon.hpp"
#include "../src/session/sessionbackend.hpp"
#include "../src/terminal/terminalwidget.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
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

}  // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const std::string root = "/tmp/kai-attach-state-" + std::to_string(static_cast<long>(::getpid()));
    ::mkdir(root.c_str(), 0700);
    ::setenv("XDG_RUNTIME_DIR", root.c_str(), 1);
    ::setenv("KAI_SHELL", "/bin/sh", 1);

    Daemon daemon;
    std::string error;
    if (!daemon.start(error)) {
        std::cerr << "cannot start the daemon: " + error + "\n";
        return 1;
    }
    std::thread serving([&daemon] { daemon.run(); });
    pump(150);

    SessionSpec spec;
    spec.name = "live";
    spec.saved = true;
    spec.rows = 24;
    spec.cols = 80;
    expect("a session starts", daemon.create(spec, error));

    // A program that goes on the alternate screen, asks for the mouse in SGR coordinates,
    // and prints whether or not anybody is at the keys — the shape of btop, without
    // needing btop.
    expect("it is told to do those things",
           daemon.type("live",
                       "printf '\\033[?1049h\\033[?1000h\\033[?1006h'\n"
                       "i=0; while :; do i=$((i+1)); printf 'TICK %s\\n' \"$i\"; sleep 0.2; done\n"));

    kai::TerminalWidget first(24, 80, std::make_unique<SessionBackend>("live", 24, 80));
    first.resize(800, 500);
    first.show();
    pump(800);
    expect("the view that was there from the start knows the program took the screen",
           first.screen().inAlternateScreen());
    expect("and asked for the mouse", first.screen().mouseReporting());
    expect("with the ticks arriving", waitFor([&first] { return onScreen(first).find("TICK 3") != std::string::npos; }));
    first.shutdown();
    expect("and the session outlives that window",
           waitFor([&daemon] {
               for (const SessionInfo& info : daemon.list())
                   if (info.name == "live" && info.alive && info.clients == 0)
                       return true;
               return false;
           }));

    // Somebody else opens the same session in a new window.
    kai::TerminalWidget second(24, 80, std::make_unique<SessionBackend>("live", 24, 80));
    second.resize(800, 500);
    second.show();
    pump(800);

    expect("the new view shows what was already on the screen",
           onScreen(second).find("TICK") != std::string::npos);

    // The report: "when we restore a session it's still frozen until a key is pressed".
    // Nothing is typed here, and nothing else is done to the window; the only thing that
    // happens is time passing, which is the thing the session was printing into.
    const std::string settled = onScreen(second);
    expect("and it keeps updating with nobody touching it",
           waitFor([&second, &settled] { return onScreen(second) != settled; }, 4000));

    expect("the program still has the mouse in the new view", second.screen().mouseReporting());
    expect("in the same coordinates as before", second.screen().mouseCoord() == MouseCoord::Sgr);
    expect("and the new view knows it is on the alternate screen",
           second.screen().inAlternateScreen());

    // A click has to reach the program rather than start a selection. Both ends are
    // checked, because the failure was one-sided: the widget's gate and the mode a
    // snapshot carries are the same fact seen from either side of the socket, and leaving
    // the mode out silently turns the mouse off in a window that looks exactly as it did
    // before — which is why this was reported as "I can't click in btop" and not as
    // "the mouse mode is missing".
    const QPoint at(60, 40);
    const QPoint dragged(200, 40);
    QMouseEvent press(QEvent::MouseButtonPress, at, at, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(&second, &press);
    QMouseEvent drag(QEvent::MouseMove, dragged, dragged, Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QCoreApplication::sendEvent(&second, &drag);
    QMouseEvent release(QEvent::MouseButtonRelease, dragged, dragged, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&second, &release);
    pump(200);
    expect("a drag with the mouse taken by the program selects nothing locally",
           second.selectedText().empty());

    // Leaving the alternate screen is the other half of knowing it was entered: an
    // application that quits while this view is looking has to land somewhere. The loop
    // has to be interrupted first, because the shell is sitting inside it and anything
    // typed joins a queue it will not read until the loop ends — which is also what a
    // person pressing Ctrl+C is for.
    //
    // What is deliberately not asserted here is *what it lands on*: the snapshot carries
    // the grid the application is drawing on, not the other buffer, so a view that
    // arrives after an application took the screen has no primary-screen history of that
    // session to go back to. That is a known gap, written in the roadmap, not a
    // behaviour to bless with a passing check.
    expect("the program is interrupted out of its loop", daemon.type("live", "\003"));
    pump(500);
    expect("and is told to leave the alternate screen",
           daemon.type("live", "printf '\\033[?1049l'\n"));
    expect("and the new view comes back with it",
           waitFor([&second] { return !second.screen().inAlternateScreen(); }, 4000));

    second.shutdown();
    daemon.stop();
    serving.join();

    ::unlink((root + "/kai-term/daemon.sock").c_str());
    ::unlink((root + "/kai-term/daemon-v3.sock").c_str());
    ::rmdir((root + "/kai-term").c_str());
    ::rmdir(root.c_str());

    if (failures == 0) {
        std::cout << "PASS: " << checks << " attach state checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " attach state checks failed\n";
    return 1;
}
