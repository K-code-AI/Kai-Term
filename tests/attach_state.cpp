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

    // The same counting for the window that gets a session swapped into it.
    struct SwapCounter : QObject {
        int paints = 0;
        bool eventFilter(QObject* watched, QEvent* ev) override {
            if (ev->type() == QEvent::Paint)
                ++paints;
            return QObject::eventFilter(watched, ev);
        }
    };
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
    // What it lands on is now carried too: the snapshot paints the primary screen and
    // its history before entering the alternate screen, so the buffer the application
    // was covering comes back when it quits.
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

    // Scrollback is part of what a session is, not decoration. A session that has
    // printed more than a screenful must bring that history across when a view
    // joins it; otherwise the view starts at the bottom of a screen whose older
    // lines have nowhere to go.
    SessionSpec history_spec;
    history_spec.name = "history";
    history_spec.saved = true;
    history_spec.rows = 24;
    history_spec.cols = 80;
    expect("a session is made with more lines than its screen", daemon.create(history_spec, error));
    expect("and it prints a hundred lines",
           daemon.type("history", "for i in $(seq 1 100); do echo \"row $i\"; done\r"));
    pump(900);

    kai::TerminalWidget scrollback(24, 80,
        std::make_unique<SessionBackend>("history", 24, 80));
    scrollback.resize(800, 500);
    scrollback.show();
    pump(900);
    expect("the new view shows the bottom of that screen",
           onScreen(scrollback).find("row 100") != std::string::npos);
    expect("and it has the history above it",
           scrollback.screen().historySize() > 0);
    bool row_one = false;
    for (int i = 0; i < scrollback.screen().historySize() && !row_one; ++i)
        if (rowPlainText(scrollback.screen().historyRow(i)).find("row 1") !=
            std::string::npos)
            row_one = true;
    expect("with row 1 really in the history", row_one);
    scrollback.shutdown();

    // The way the program actually restores a session. Everything above hands a session
    // to a brand-new window, which is what a tab would do; what Ctrl+Shift+A really does
    // is swap a session into a window that is already on screen, through showSession.
    // That path has never been checked, and it is where Kaida's black screen lives: a
    // window which is already mapped gets no show and no resize, so nothing happens
    // around the swap except the swap itself.
    kai::TerminalWidget swap(24, 80);
    swap.resize(800, 500);
    swap.show();
    pump(600);  // A window of its own, drawn, with a shell sitting in it.

    SwapCounter swapped;
    swap.installEventFilter(&swapped);
    swap.showSession(std::make_unique<SessionBackend>("quiet", 24, 80), true);
    pump(1200);

    // Nobody types. The window is simply told to show a session that already has lines on
    // its screen, and what has to be true afterwards is that it is showing them.
    expect("a window switched to a session shows that session's screen without a keypress",
           onScreen(swap).find("NOTHING-MORE-AFTER-THIS") != std::string::npos);
    expect("and it was asked to draw what arrived", swapped.paints > 0);
    swap.shutdown();

    // A screen is a thing of a size. A view that changes size holds a screen painted for
    // the size it used to be, and until now the snapshot was something the attach
    // handshake did once — so the contents of a screen and the size of a window depended
    // on each other exactly once, at the worst moment: while a window was being made
    // bigger by a compositor that had not finished decorating it.
    SessionSpec grow_spec;
    grow_spec.name = "grow";
    grow_spec.saved = true;
    grow_spec.rows = 24;
    grow_spec.cols = 80;
    expect("a session starts for the resize to act on", daemon.create(grow_spec, error));
    expect("and it is given lines to hold",
           daemon.type("grow", "printf 'THE-FIRST-LINE\\nsecond\\nthird\\n'\r"));
    pump(700);

    kai::TerminalWidget grow(24, 80, std::make_unique<SessionBackend>("grow", 24, 80));
    grow.resize(800, 500);
    grow.show();
    pump(900);
    expect("a view of it arrives with its lines",
           onScreen(grow).find("THE-FIRST-LINE") != std::string::npos);
    const int rows_before = grow.rows();

    // The window is made bigger, which is what a compositor does to a fresh one and what
    // no check had ever done to a view of a session.
    grow.resize(1400, 900);
    pump(900);
    const std::string grown = onScreen(grow);
    expect("and the view really did change size", grow.rows() > rows_before);
    expect("and a view that changed size still shows what was on the screen",
           grown.find("THE-FIRST-LINE") != std::string::npos);
    expect("with the lines not painted twice over",
           grown.find("THE-FIRST-LINETHIS") == std::string::npos
               && grown.find("THE-FIRST-LINE\\nTHE-FIRST-LINE") == std::string::npos);
    grow.shutdown();

    // A scroll region is a request about scrolling, and it also clamps every cursor
    // address the terminal is given. A snapshot that replays a session's region before its
    // glyphs therefore paints the whole screen into the handful of rows the region allows,
    // each row on top of the last, and the person looking at the window sees one character
    // in the corner. Programs leave regions behind when they exit, so the region a snapshot
    // replays need not have anything to do with what is on the screen.
    SessionSpec region_spec;
    region_spec.name = "region";
    region_spec.saved = true;
    region_spec.rows = 24;
    region_spec.cols = 80;
    expect("a session starts for the region to be set in", daemon.create(region_spec, error));
    expect("and it narrows its scroll region and then prints a screen full",
           daemon.type("region",
                       "printf '\\033[3;4rTHE-TOP-LINE\\nsecond\\nthird\\n'\r"));
    pump(700);

    kai::TerminalWidget clamped(24, 80, std::make_unique<SessionBackend>("region", 24, 80));
    clamped.resize(800, 500);
    clamped.show();
    pump(900);
    const std::string seen = onScreen(clamped);
    expect("and a view of it shows the first line and not one character in the corner",
           seen.find("THE-TOP-LINE") != std::string::npos);
    expect("and the lines below it are there too",
           seen.find("second") != std::string::npos && seen.find("third") != std::string::npos);
    clamped.shutdown();

    // The order the reports have in them: a window is made, the session's screen arrives
    // while it is still hidden, and only then is it shown. A paint asked for in that order
    // is worth nothing — there is no surface yet to ask for — and everything the session
    // sent is then sitting in a model nothing draws.
    SessionSpec late_spec;
    late_spec.name = "late";
    late_spec.saved = true;
    late_spec.rows = 24;
    late_spec.cols = 80;
    expect("a session is made with something to show", daemon.create(late_spec, error));
    expect("and it says its line and then goes quiet",
           daemon.type("late", "printf 'THE-LATE-LINE\\n'\r"));
    pump(700);

    kai::TerminalWidget late(24, 80, std::make_unique<SessionBackend>("late", 24, 80));
    late.resize(800, 500);
    pump(900);
    // The check that was parked while the delivery path was broken: a view built while
    // its window is still hidden must take in the session's screen. It is fired now because
    // the delivery path is fixed.
    expect("a view built while hidden takes in its session's screen",
           onScreen(late).find("THE-LATE-LINE") != std::string::npos);
    late.show();
    pump(600);
    // Pixels, not the model: the model holding the screen is exactly what the report says
    // is already true, and the window showing nothing is the part a model check cannot see.
    const QImage drawn = late.grab().toImage();
    int lit = 0;
    for (int y = 0; y < drawn.height(); y += 3)
        for (int x = 0; x < drawn.width(); x += 3)
            if (drawn.pixelColor(x, y).value() > 96)
                ++lit;
    expect("and a window shown after the screen arrived still paints it, without a keypress",
           lit > 200);

    // A window too small for another place in it. The session answers "no" — and a window
    // used to hear every "no" as "the session is over": it closed, and a window that closes
    // because it was refused takes the session down with it when nothing else is looking at
    // it. Kaida pressed Ctrl+Shift+N in a narrow window and called it a crash, which is
    // exactly what it looks like from the outside.
    SessionSpec cramped;
    cramped.name = "cramped";
    cramped.rows = 24;
    cramped.cols = 2;
    expect("a session two columns wide starts", daemon.create(cramped, error));
    kai::TerminalWidget narrow(24, 2, std::make_unique<SessionBackend>("cramped", 24, 2));
    narrow.resize(60, 300);
    narrow.show();
    pump(500);
    narrow.newTab();
    pump(700);
    // Room is made afterwards so that something can be printed without it wrapping into
    // ribbons across a two-column screen.
    narrow.resize(700, 300);
    pump(500);
    static_cast<void>(daemon.type("cramped", "echo STILL-OPEN\n"));
    expect("a window told there is no room is not told the session ended",
           waitFor([&narrow] { return onScreen(narrow).find("STILL-OPEN") != std::string::npos; },
                   6000));

    // A refusal is not the session ending. Ending the one place in a session is refused
    // outright — finishing a session is something a person says on purpose — and a window
    // that heard that "no" as "it is over" closed itself. A window closing because it was
    // refused takes the session down with it when nothing else is looking at it, which is
    // what a vanished window and a disappeared session look like from the outside.
    SessionSpec single;
    single.name = "single";
    single.rows = 24;
    single.cols = 80;
    expect("a session with one place in it starts", daemon.create(single, error));
    kai::TerminalWidget sole(24, 80, std::make_unique<SessionBackend>("single", 24, 80));
    sole.resize(900, 500);
    sole.show();
    pump(500);
    sole.closeTab();
    pump(700);
    // The marker is written with nothing between its halves but empty quotes, so that only
    // the shell can print the whole word: this window is eighty columns wide and repeats
    // back whatever it is typed into, and a check that accepts the echo of what it sent is
    // checking that a terminal can repeat itself.
    // And the words themselves make the whole way: from the daemon, over the wire, into the
    // window. Each end is tested on its own, which is exactly how a seam in the middle gets
    // left untested.
    // The notice travels while the session says nothing at all: the shell is sitting at its
    // prompt, printing nothing, which is the ordinary state of an idle window and was enough
    // to keep a refusal from ever being seen.
    pump(1200);
    expect("and what the session said is said in the window (said: [" + sole.notice() + "])",
           sole.notice().find("last pane") != std::string::npos);
    static_cast<void>(daemon.type("single", "echo REFU\"\"SAL\n"));
    expect("a window told no is not told the session ended",
           waitFor([&sole] { return onScreen(sole).find("REFUSAL") != std::string::npos; },
                   6000));
    static_cast<void>(daemon.type("single", "exit\n"));

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
