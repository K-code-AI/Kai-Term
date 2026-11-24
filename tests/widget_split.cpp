// A window that shows two places of one session at the same time.

// This is the visible half of side-by-side view: the arrangement, the sizes and the
// tab bar were all there already, but the window still drew one place and left the
// rest of the screen blank, so pressing Ctrl+Shift+H changed nothing a person could
// see. Here a real daemon runs a real two-pane session, the window is handed a second
// connection pointed at the other place, and the checks ask the two things that
// matter: is each place painted into the cells the session gave it, and does typing
// go to the place whose half of the screen was clicked.

#include "../src/session/daemon.hpp"
#include "../src/session/sessionbackend.hpp"
#include "../src/terminal/terminalwidget.hpp"

#include <QApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QWheelEvent>
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

void sendKey(kai::TerminalWidget* w, int key, Qt::KeyboardModifiers mods = Qt::NoModifier,
             const QString& text = QString()) {
    QKeyEvent ev(QEvent::KeyPress, key, mods, text);
    QCoreApplication::sendEvent(w, &ev);
}

// Type a line and press Enter, the way a person does rather than by pasting: a shell
// with bracketed paste on takes a pasted newline as text and leaves the command at the
// prompt, which would make a check that looks for output find the command line instead.
void runLine(kai::TerminalWidget* w, const std::string& command) {
    w->pasteText(command);
    sendKey(w, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
}

void sendMouse(kai::TerminalWidget* w, QEvent::Type type, Qt::MouseButton button,
               Qt::MouseButtons buttons, const QPointF& at) {
    QMouseEvent ev(type, at, at, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

// Roll the wheel a notch at a point, the way a hand does: the sign of the angle says
// which way, and the widget maps it to lines the same way it does for a real wheel.
void sendWheel(kai::TerminalWidget* w, const QPointF& at, int angle_y) {
    QWheelEvent ev(at, w->mapToGlobal(at.toPoint()), QPoint(0, 0), QPoint(0, angle_y),
                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(w, &ev);
}

// The middle of a cell, in window pixels — where a click means that cell.
QPointF cellPoint(kai::TerminalWidget* w, int row, int col) {
    const QPointF top_left = w->cellTopLeft(row, col);
    const QSizeF cell = w->cellSize();
    return QPointF(top_left.x() + cell.width() * 0.5, top_left.y() + cell.height() * 0.5);
}

// Whether any ink was drawn in a band of columns of the live grid: the difference
// between a place that is painted there and the blank ground that is not. The ground
// is read from the far right of the same rows, where nothing is expected, so this does
// not have to know the theme's colours.
bool inkInColumns(const QImage& shot, kai::TerminalWidget* w, int from_col, int to_col) {
    if (from_col >= to_col)
        return false;
    const QSizeF cell = w->cellSize();
    const QPointF first = w->cellTopLeft(0, from_col);
    const int top = static_cast<int>(first.y());
    const int left = static_cast<int>(w->cellTopLeft(0, from_col).x());
    const int right = static_cast<int>(w->cellTopLeft(0, to_col).x());
    const int rows = w->rows();
    int inked = 0;
    for (int r = 0; r < rows; ++r) {
        const int y = static_cast<int>(w->cellTopLeft(r, 0).y() + cell.height() * 0.5);
        for (int x = left; x < right && x < shot.width(); ++x) {
            if (shot.pixelColor(x, y) != shot.pixelColor(shot.width() - 2, y))
                ++inked;
        }
    }
    (void)top;
    return inked > 3; // a few stray pixels is anti-aliasing, not text
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const std::string root =
        "/tmp/kai-widget-split-" + std::to_string(static_cast<long>(::getpid()));
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
    spec.name = "split";
    spec.rows = 24;
    spec.cols = 80;
    expect("a session starts to be split", daemon.create(spec, error));
    pump(400);

    kai::TerminalWidget window(24, 80, std::make_unique<SessionBackend>("split", 24, 80));
    window.resize(800, 500);
    window.show();
    pump(300);
    // Give the window the one thing a window onto a session has that a bare widget
    // does not: a way to open a neighbour connection. Everything after is the window's
    // own doing — which is exactly what a real window does, and it is what lets a click
    // move the focus from one place to another.
    window.setPaneConnector([&window](int pane) -> std::unique_ptr<TerminalBackend> {
        auto neighbour = std::make_unique<SessionBackend>("split", window.rows(),
                                                          window.cols());
        neighbour->showPane(pane);
        return neighbour;
    });

    // A second place, so there are two to draw. Asked of the window rather than of the
    // daemon directly, because a place is added the way a person adds one.
    window.newTab();
    expect("the session is running two programs",
           waitFor([&daemon] {
               for (const SessionInfo& info : daemon.list())
                   if (info.name == "split")
                       return info.panes == 2;
               return false;
           }));
    expect("and the window is told the tab bar has two places",
           waitFor([&window] { return window.paneCount() == 2; }));
    expect("shown side by side, which is where two places share the screen",
           waitFor([&window] { return !window.viewIsTabs(); }));
    expect("and the window opened a neighbour for the other place on its own",
           waitFor([&window] { return window.hasExtraPane(); }));

    // A new place takes the focus, so the window is looking at the second one. Look it
    // back at the first, the way clicking that tab would, so this test says which half
    // is which rather than following wherever the session happened to point.
    window.showPaneAt(0);
    pump(250);
    expect("and the window is looking at the first place", window.shownPane() == 0);

    // Each place prints something only it says, so a marker found in the right place
    // came from the right program and not from its neighbour. The place being looked at
    // is the window's own screen; the other is a neighbour's.
    runLine(&window, "echo LEFT-PLACE-MARKER");
    expect("the place being looked at has its marker on the window's screen",
           waitFor([&window] { return onScreen(window).find("LEFT-PLACE-MARKER") != std::string::npos; }));
    expect("the second place is asked to print",
           daemon.type("split", "echo RIGHT-PLACE-MARKER\n", 1));
    expect("and its marker lands in the neighbour, not the window's own screen",
           waitFor([&window] {
               return window.neighbourText(1).find("RIGHT-PLACE-MARKER") != std::string::npos &&
                      onScreen(window).find("RIGHT-PLACE-MARKER") == std::string::npos;
           }));

    // Both places painted at once: ink in the left cell and ink in the right, in the
    // same window. This is the thing that was missing — pressing H used to change
    // nothing a person could see.
    const QImage shot = window.grab().toImage();
    expect("the first place is drawn in the left half", inkInColumns(shot, &window, 0, 39));
    expect("and the second place is drawn in the right half", inkInColumns(shot, &window, 40, 80));

    // Clicking a place looks at it: the focus, the highlight and the typing all move
    // there, and the place this window used to look at becomes the neighbour.
    const int right_col = 50; // well inside the second place's cell
    sendMouse(&window, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton,
              cellPoint(&window, 1, right_col));
    sendMouse(&window, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
              cellPoint(&window, 1, right_col));
    pump(250);
    expect("clicking a place looks at it", window.activePane() == 1);
    expect("and its own screen is now the window's, showing its marker",
           waitFor([&window] { return onScreen(window).find("RIGHT-PLACE-MARKER") != std::string::npos; }));
    expect("while the place left behind becomes a neighbour that still shows its marker",
           waitFor([&window] { return window.neighbourText(0).find("LEFT-PLACE-MARKER") != std::string::npos; }));

    // Typing now goes to the place being looked at — the one just clicked — with no
    // need to first select the other and click back. This was the reported breakage:
    // reaching a place by clicking its tab or Ctrl+Shift+Right used to leave it untypeable.
    runLine(&window, "echo TYPED-AFTER-CLICK");
    expect("the place looked at takes what was typed",
           waitFor([&window] { return onScreen(window).find("TYPED-AFTER-CLICK") != std::string::npos; }));
    expect("and the neighbour is left alone",
           window.neighbourText(0).find("TYPED-AFTER-CLICK") == std::string::npos);

    // Ctrl+Shift+Right moves the look the same way a click does, and typing follows.
    sendKey(&window, Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier);
    pump(250);
    expect("Ctrl+Shift+Right moves the look to the next place", window.activePane() == 0);
    expect("and that place is now the window's own screen",
           waitFor([&window] { return onScreen(window).find("LEFT-PLACE-MARKER") != std::string::npos; }));

    // A third place. The window must show all three at once, not have the second and
    // third fight for the one slot beside the first.
    window.newTab();
    expect("the session is running three programs",
           waitFor([&daemon] {
               for (const SessionInfo& info : daemon.list())
                   if (info.name == "split")
                       return info.panes == 3;
               return false;
           }));
    window.showPaneAt(0);
    pump(400);
    expect("with two neighbours drawn beside the place being looked at",
           waitFor([&window] {
               return window.neighbourText(1).find("RIGHT-PLACE-MARKER") != std::string::npos;
           }));
    expect("the third place is asked to print",
           daemon.type("split", "echo THIRD-PLACE-MARKER\n", 2));
    expect("and it appears in its own neighbour",
           waitFor([&window] {
               return window.neighbourText(2).find("THIRD-PLACE-MARKER") != std::string::npos;
           }));
    const QImage three = window.grab().toImage();
    expect("all three places are painted: ink in the left, middle and right",
           inkInColumns(three, &window, 2, 22) && inkInColumns(three, &window, 26, 44) &&
               inkInColumns(three, &window, 50, 88));

    // Each place scrolls on its own. Fill two of them with lines, then scroll the place
    // being looked at and check only it moved, then scroll a neighbour and check only it
    // moved. A split where one wheel moved both, or neither, would show up here.
    runLine(&window, "seq 1 200");
    pump(500);
    daemon.type("split", "seq 1 200\n", 1);
    pump(500);
    // Scroll the place being looked at (left third) with the wheel.
    const QPointF left = cellPoint(&window, 5, 5);
    sendWheel(&window, left, +120);
    pump(120);
    expect("the place being looked at scrolls back", window.scrollOffset() > 0);
    expect("and its neighbour does not move with it", window.neighbourScrollAt(1) == 0);
    // Scroll a neighbour (the middle place); the place being looked at must stay put.
    const int was_focused = window.scrollOffset();
    const QPointF middle = cellPoint(&window, 5, 32); // solidly inside the middle place
    sendWheel(&window, middle, +120);
    pump(120);
    expect("a neighbour scrolls on its own", window.neighbourScrollAt(1) > 0);
    expect("and the place being looked at does not move with it",
           window.scrollOffset() == was_focused);

    expect("the session still runs after all this",
           waitFor([&daemon] {
               for (const SessionInfo& info : daemon.list())
                   if (info.name == "split")
                       return info.alive;
               return false;
           }));

    // A real window onto a session also drops its neighbours when the view goes back to
    // one place at a time, and brings them back on side-by-side.
    window.toggleViewMode();
    expect("tabs mode leaves no neighbour beside the window",
           waitFor([&window] { return window.viewIsTabs() && !window.hasExtraPane(); }));
    window.toggleViewMode();
    expect("side by side opens the neighbours again",
           waitFor([&window] { return !window.viewIsTabs() && window.hasExtraPane(); }));

    // Put the window down before the daemon it is watching, so its readers are joined
    // while the far end is still there to say goodbye; then stop serving and wait.
    window.shutdown();
    static_cast<void>(daemon.kill("split", error));
    daemon.stop();
    serving.join();

    if (failures == 0) {
        std::cout << "PASS: " << checks << " side-by-side pane checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " side-by-side checks failed\n";
    return 1;
}
