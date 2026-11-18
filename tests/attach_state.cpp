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
#include <QEvent>
#include <QMouseEvent>
#include <QObject>
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

    // Paint events, counted. Everything else in this file reads the model, and a window
    // can be entirely right inside while showing a blank rectangle: Kaida's screenshot of
    // a re-attached session was empty until a key was pressed, and every check here was
    // passing at the time. A model nobody paints is not a terminal.
    struct PaintCounter : QObject {
        int paints = 0;
        bool eventFilter(QObject* watched, QEvent* ev) override {
            if (ev->type() == QEvent::Paint)
                ++paints;
            return QObject::eventFilter(watched, ev);
        }
    } counter;
    second.installEventFilter(&counter);

    second.resize(800, 500);
    second.show();
    pump(800);

    expect("the new view shows what was already on the screen",
           onScreen(second).find("TICK") != std::string::npos);
    const int paints_after_attach = counter.paints;
    expect("and something asked for it to be drawn, without anything being typed",
           paints_after_attach > 0);

    // The report: "when we restore a session it's still frozen until a key is pressed".
    // Nothing is typed here, and nothing else is done to the window; the only thing that
    // happens is time passing, which is the thing the session was printing into.
    const std::string settled = onScreen(second);
    const int paints_before_updates = counter.paints;
    expect("and it keeps updating with nobody touching it",
           waitFor([&second, &settled] { return onScreen(second) != settled; }, 4000));
    // The program prints five times a second, so a second and a half of nothing but the
    // event loop is several more chunks. Without this the check raced the next chunk and
    // passed or failed on luck, which is not a thing a check should do.
    pump(1500);
    expect("and each of those updates asks for the window to be drawn again (" +
               std::to_string(counter.paints - paints_before_updates) + " paints in 1.5s)",
           counter.paints > paints_before_updates);

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

    // A session that says one thing and then says nothing more, which is the state of a
    // terminal somebody is looking at. Everything above runs against a program printing
    // twice a second, and that is exactly why none of it caught Kaida's screenshot: with
    // output arriving all the time, whatever is waiting to be read gets carried in on the
    // next chunk and the window looks fine. A session at a prompt produces nothing, so
    // the snapshot has to arrive on its own — otherwise the window opens on an empty
    // rectangle until a keypress gives the shell a reason to talk.
    SessionSpec quiet_spec;
    quiet_spec.name = "quiet";
    quiet_spec.saved = true;
    quiet_spec.rows = 24;
    quiet_spec.cols = 80;
    expect("a second session starts", daemon.create(quiet_spec, error));
    expect("and it prints one thing and stops",
           daemon.type("quiet", "echo NOTHING-MORE-AFTER-THIS\n"));
    pump(600);

    kai::TerminalWidget silent(24, 80, std::make_unique<SessionBackend>("quiet", 24, 80));
    PaintCounter quiet_painter;
    silent.installEventFilter(&quiet_painter);
    silent.resize(800, 500);
    silent.show();
    pump(900);
    // The screen a view arrives with is the one the session was looking at. What is on it
    // is whatever the session had printed, so the check is that the view is not blank —
    // Kaida's screenshot was a window with nothing in it at all, in a session that had
    // been used, and that is the thing this refuses to accept.
    expect("a view of a silent session arrives with something on its screen",
           onScreen(silent).find("NOTHING-MORE-AFTER-THIS") != std::string::npos);
    // And it goes on drawing without anybody at the keys: the shell is asked to speak, and
    // the only thing the window does in between is sit there with its event loop running.
    const int paints_before = quiet_painter.paints;
    expect("the silent session will still answer",
           daemon.type("quiet", "echo SPEAK-WHEN-SPOKEN-TO\n"));
    pump(1200);
    expect("and the window is drawn again when the session has something new to show",
           quiet_painter.paints > paints_before);
    silent.shutdown();

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
