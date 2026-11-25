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

// Press, move, release — the gesture a person makes with a mouse to select text.
// The cells are grid cells, so a caller aiming at a place beside another one says
// where that place sits in the grid, which is exactly what a pointer reports.
void drag(kai::TerminalWidget* w, int from_row, int from_col, int to_row, int to_col) {
    sendMouse(w, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton,
              cellPoint(w, from_row, from_col));
    sendMouse(w, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton,
              cellPoint(w, to_row, to_col));
    sendMouse(w, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
              cellPoint(w, to_row, to_col));
}

QImage grabImage(kai::TerminalWidget* w) {
    return w->grab().toImage().convertToFormat(QImage::Format_ARGB32);
}

long long differingPixels(const QImage& a, const QImage& b) {
    if (a.size() != b.size())
        return -1;
    long long n = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y))
                ++n;
        }
    }
    return n;
}

// One place's own rectangle out of a whole-window grab. A check that looked at the
// window would see a cursor in the place being looked at and call it a cursor in the
// place being measured; this asks about one place and nothing beside it.
QImage paneShot(kai::TerminalWidget* w, int pane) {
    const QImage whole = grabImage(w);
    const PaneRect cell = w->paneCell(pane);
    const QSizeF one = w->cellSize();
    const QPointF from = w->cellTopLeft(cell.row, cell.col);
    const QRect box(static_cast<int>(from.x()), static_cast<int>(from.y()),
                    static_cast<int>(one.width() * cell.cols),
                    static_cast<int>(one.height() * cell.rows));
    return whole.copy(box.intersected(whole.rect()));
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const std::string root =
        "/tmp/kai-widget-split-" + std::to_string(static_cast<long>(::getpid()));
    ::mkdir(root.c_str(), 0700);
    ::setenv("XDG_RUNTIME_DIR", root.c_str(), 1);
    if (!::getenv("KAI_SHELL"))
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

    // Count the window's own commands, so the control test can prove a key reaches the
    // right action and not the shell.
    int settings_hits = 0;
    int save_hits = 0;
    int attach_hits = 0;
    window.on_settings_menu = [&settings_hits] { ++settings_hits; };
    window.on_save_session = [&save_hits] { ++save_hits; };
    window.on_session_menu = [&attach_hits] { ++attach_hits; };

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

    // A third place, made by dividing the place being looked at. A new place is put
    // immediately after the one it was divided from, so the place that was second is third
    // now. The window must show all three at once, not have the second and third fight for
    // the one slot beside the first, and each must show its own program: the report was
    // that a new place "gets assigned pane 2 instead of 3, and overwrites the display of
    // pane 1".
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
    expect("with the place that was second now third, still showing its own marker",
           waitFor([&window] {
               return window.neighbourText(2).find("RIGHT-PLACE-MARKER") != std::string::npos;
           }));
    expect("the new place sits beside the one it was made from, and is empty of its work",
           window.neighbourText(1).find("RIGHT-PLACE-MARKER") == std::string::npos);
    expect("the new place is asked to print",
           daemon.type("split", "echo THIRD-PLACE-MARKER\n", 1));
    expect("and it appears in its own neighbour and nowhere else",
           waitFor([&window] {
               return window.neighbourText(1).find("THIRD-PLACE-MARKER") != std::string::npos &&
                      window.neighbourText(2).find("THIRD-PLACE-MARKER") == std::string::npos;
           }));
    const QImage three = window.grab().toImage();
    expect("all three places are painted: ink in the left, middle and right",
           inkInColumns(three, &window, 2, 22) && inkInColumns(three, &window, 26, 44) &&
               inkInColumns(three, &window, 50, 88));

    // The numbers on the bar are the order the places were made in, not the order they
    // happen to sit in: the place made third is called 3 even though it sits second, and
    // the bar lists them 1, 2, 3 rather than 1, 3, 2. (Kaida, 2026-11-24: "it wrongly gets
    // assigned pane 2 instead of 3" — with "the top bar should still display in numerical
    // order".)
    expect("each place keeps the number it was made with, so the newest is 3",
           window.tabNumber(0) == 1 && window.tabNumber(2) == 2 && window.tabNumber(1) == 3);
    expect("and the bar lists the places by that number",
           window.tabOrder() == std::vector<int>({0, 2, 1}));
    expect("so the tab a click lands on is the third place, not the one beside it",
           window.tabSlotAt(window.width() - 60.0, 5.0, window.width() - 10.0) == 1);

    const int looked_at_before = window.activePane();
    // Ctrl+Shift and an arrow walks those numbers rather than the positions: from the place
    // called 1 the next one is the place called 2, which sits at the far end of the bar, and
    // only after it comes the place called 3 that sits underneath.
    // (Kaida, 2026-11-24: "Ctrl+Shift+Arrow should cycle in numerical order".)
    auto walk = [&window](Qt::Key key) {
        QKeyEvent step(QEvent::KeyPress, key, Qt::ControlModifier | Qt::ShiftModifier);
        QApplication::sendEvent(&window, &step);
    };
    window.showPaneAt(0);
    walk(Qt::Key_Right);
    expect("the first arrow lands on the place called 2",
           window.activePane() == 2 && window.tabNumber(window.activePane()) == 2);
    walk(Qt::Key_Right);
    expect("the next lands on the place called 3, which is the one underneath",
           window.activePane() == 1 && window.tabNumber(window.activePane()) == 3);
    walk(Qt::Key_Right);
    expect("and the next wraps to 1 rather than running off the numbers",
           window.activePane() == 0);
    walk(Qt::Key_Left);
    expect("going back runs the numbers down", window.activePane() == 1);
    walk(Qt::Key_Up);
    expect("up is the same walk as left", window.activePane() == 2);
    walk(Qt::Key_Down);
    expect("and down the same as right", window.activePane() == 1);

    // The line between two places. (Kaida, 2026-11-24: "the separator should be thinner and
    // equal in size when comparing the horizontal and vertical one (maybe even an option in
    // the settings to change it size)".) One default for both directions, and a number the
    // settings menu can move without making the two disagree.
    expect("the divider starts as a line rather than the whole gap",
           window.dividerWidth() == 1.0);
    window.setDividerWidth(3.0);
    expect("and it takes the width the settings menu hands it", window.dividerWidth() == 3.0);
    window.setDividerWidth(4000.0);
    expect("a wild number is held to something drawable", window.dividerWidth() == 16.0);
    window.setDividerWidth(0.0);
    expect("and none at all is a real answer: a gap with nothing drawn in it",
           window.dividerWidth() == 0.0);
    window.setDividerWidth(1.0);

    // Keeping a session asks what to call it, in the window's own panel: a field you type
    // into, Enter to hand the name over, Esc to back out.
    // (Kaida, 2026-11-24: "saving a session should now prompt for a name (can be disabled in
    // setting)".)
    std::string given;
    window.askNameInTerminal("What should this session be called?", "", "w7",
                             [&given](std::string name) { given = name; });
    expect("the name is asked for in the window, not on the shell",
           window.overlayActive() && window.overlayEditable() &&
               window.overlayQuestion() == "What should this session be called?");
    expect("offering the name the window would have picked",
           window.overlayField() == "w7");
    const struct { int code; const char* text; } typed[] = {
        {Qt::Key_M, "M"}, {Qt::Key_Y, "y"}, {Qt::Key_Minus, "-"}, {Qt::Key_7, "7"}};
    for (const auto& letter : typed) {
        QKeyEvent key(QEvent::KeyPress, letter.code, Qt::NoModifier, letter.text);
        QApplication::sendEvent(&window, &key);
    }
    expect("the first letter replaces the name that was offered, and the rest follow it",
           window.overlayField() == "My-7");
    QKeyEvent erase(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    QApplication::sendEvent(&window, &erase);
    expect("backspace takes one letter off the end", window.overlayField() == "My-");
    QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, "\r");
    QApplication::sendEvent(&window, &accept);
    expect("enter hands that name over and puts the panel away",
           given == "My-" && !window.overlayActive());

    given = "untouched";
    window.askNameInTerminal("What should this session be called?", "", "w8",
                             [&given](std::string name) { given = name; });
    QKeyEvent away(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&window, &away);
    expect("and escape backs out without keeping anything",
           !window.overlayActive() && given == "untouched");
    window.showPaneAt(looked_at_before); // the checks after this one know where they are

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
    // Scrolled back, the place being looked at must still show its own lines, not a
    // blank half. The reported breakage was that nothing scrolled in side-by-side, and
    // a scroll that grows the offset but paints an empty cell is just as broken.
    expect("and shows its own history when scrolled up, not a blank half",
           !window.displayRowText(0).empty());
    // The wheel belongs to the place being looked at, wherever the pointer is. The
    // reported bug was a wheel over a neighbour moving that neighbour while the place in
    // hand — the one being read — stayed put. So a wheel that lands on a neighbour must
    // scroll the place in hand and leave the neighbour where it was.
    const int was_focused = window.scrollOffset();
    const QPointF middle = cellPoint(&window, 5, 32); // solidly inside the middle place
    sendWheel(&window, middle, +120);
    pump(120);
    expect("a wheel over a neighbour scrolls the place in hand, not the neighbour",
           window.scrollOffset() > was_focused);
    expect("and the neighbour under the pointer does not move",
           window.neighbourScrollAt(1) == 0);

    // A place scrolled back shows no cursor. The row the cursor stands on belongs to the
    // live bottom, and scrolling moves that row down out of the cell; drawing the block
    // anyway — which clamping it into the cell used to do — strands it on a history line,
    // in the column the prompt left empty, carrying the glyph of a row that is not
    // underneath it. It reads as a cursor waiting in a place nobody is typing.
    // (Kaida, 2026-11-24: "we shouldn't see the cursor when scrolling up like that".)
    // The blink is what separates the two grabs here: nothing is typed and no output
    // arrives between them, so any pixel that moved is the cursor and nothing else.
    const int looked_at = window.activePane();
    window.setCursorBlink(120);
    const bool went_out = waitFor([&] { return !window.cursorBlinkPhase(); }, 3000);
    const QImage scrolled_out = paneShot(&window, looked_at);
    const bool came_back = waitFor([&] { return window.cursorBlinkPhase(); }, 3000);
    const QImage scrolled_lit = paneShot(&window, looked_at);
    expect("the blink turns, so the two grabs differ only by the cursor",
           went_out && came_back && window.scrollOffset() > 0);
    expect("a place scrolled back paints no cursor at all",
           differingPixels(scrolled_lit, scrolled_out) == 0);

    // And the same pair back at the live bottom must differ, or the check above would be
    // satisfied by a window that never paints a cursor anywhere.
    for (int guard = 0; guard < 200 && window.scrollOffset() > 0; ++guard) {
        sendWheel(&window, left, -120);
        pump(40);
    }
    expect("the place is back at its live bottom", window.scrollOffset() == 0);
    const bool went_out_again = waitFor([&] { return !window.cursorBlinkPhase(); }, 3000);
    const QImage bottom_out = paneShot(&window, looked_at);
    const bool came_back_again = waitFor([&] { return window.cursorBlinkPhase(); }, 3000);
    const QImage bottom_lit = paneShot(&window, looked_at);
    expect("and the cursor is painted there, on its own row",
           went_out_again && came_back_again && differingPixels(bottom_lit, bottom_out) > 0);
    window.setCursorBlink(0);

    // Scrolling a place is a fact about that place, not about the window looking at it,
    // so it holds from both sides. Coming back finds a place where it was left — and the
    // same fact says that looking at a DIFFERENT place must leave the place you walked
    // away from showing the lines you left it on. It used to snap to its live bottom the
    // moment it became a neighbour, so the half you were not looking at changed under
    // you: scroll a log back to its first line, look at the other half, and the first
    // line is gone. (Kaida, 2026-11-24: "during pane 2 selection we should still see the
    // same in pane 1 as when pane 1 was selected".)
    sendWheel(&window, left, +120);
    pump(150);
    const int left_behind = window.activePane();
    const int left_scroll = window.scrollOffset();
    expect("the place being looked at is scrolled back before it is left behind",
           left_scroll > 0);
    window.showPaneAt(left_behind == 0 ? 1 : 0);
    // The neighbour starts holding the scroll, and — the part that used to break — it
    // keeps holding it after the snapshot that every new connection is handed. That
    // snapshot resets the place's screen, and shifting the view by the difference in
    // history depth drove the offset far below zero, where the clamp brought it back to
    // nothing: the place snapped to its live bottom.
    expect("the place left behind is shown by a neighbour that holds its scroll",
           waitFor([&] { return window.neighbourScrollAt(left_behind) > 0; }));
    expect("and the snapshot that follows does not drop it back to the live bottom",
           waitFor([&] {
               const int held = window.neighbourScrollAt(left_behind);
               return held > 0 && !window.neighbourRowText(left_behind, 0).empty();
           }, 2500));

    // A drag must still select in the place being looked at. Side by side it stopped
    // working altogether: the highlight was never painted and the text came back empty,
    // so once a second place existed nothing on screen could be selected for copying.
    // (Kaida, 2026-11-24: "Selection doesn't work, we can't select any text for copying
    // right now when we create a second pane. before creating a second pane it works".)
    {
        // Run the same gesture in whichever place is being looked at, and then in the
        // other one: a place that starts at column zero and one that starts at column
        // forty-two are different arithmetic, and only one of them was ever exercised.
        auto selects_in_the_place_being_looked_at = [&](int marker) {
            const int here = window.activePane();
            const PaneRect place = window.paneCell(here);
            const std::string needle = "SELECTED-HERE-" + std::to_string(marker);
            sendWheel(&window, cellPoint(&window, place.row + 3, place.col + 3), -1200);
            pump(120);
            runLine(&window, "echo " + needle);
            pump(200);
            int row = -1;
            for (int r = window.screen().rows() - 1; r >= 0; --r) {
                if (window.screen().rowText(r).find(needle) != std::string::npos) {
                    row = r;
                    break;
                }
            }
            expect("the place being looked at has text to select", row >= 0);
            if (row < 0)
                return;
            const QImage before = paneShot(&window, here);
            const int other = here == 0 ? 1 : 0;
            const QImage neighbour_before = paneShot(&window, other);
            drag(&window, place.row + row, place.col, place.row + row, place.col + 15);
            pump(80);
            expect("a drag in the place being looked at selects its text",
                   window.selectedText().find(needle) != std::string::npos);
            expect("and the highlight is painted, not merely remembered",
                   differingPixels(before, paneShot(&window, here)) > 0);
            expect("while the place beside it keeps showing its own text, untouched",
                   differingPixels(neighbour_before, paneShot(&window, other)) == 0);
        };
        selects_in_the_place_being_looked_at(1);
        window.showPaneAt(window.activePane() == 0 ? 1 : 0);
        pump(250);
        selects_in_the_place_being_looked_at(2);
    }

    // Ctrl+K is the control; Ctrl+K then N asks which way to divide before it divides.
    // Reaching a place is already covered above; this is the key that MAKES one. It must
    // pop a Vertical / Horizontal question answerable by a single letter, and lay the new
    // place out that way: Vertical beside (shares the rows), Horizontal below (columns).
    window.showPaneAt(0);
    pump(200);
    // First the control itself: Ctrl+K waits for a key without touching the shell, and
    // Escape spends the wait on nothing.
    sendKey(&window, Qt::Key_K, Qt::ControlModifier);
    pump(60);
    expect("Ctrl+K arms the control and waits for a key", window.prefixArmed());
    sendKey(&window, Qt::Key_Escape, Qt::NoModifier);
    pump(60);
    expect("and Escape spends the control on nothing", !window.prefixArmed());

    // Ctrl+K then K reaches the settings menu, not the shell.
    const int settings_before = settings_hits;
    sendKey(&window, Qt::Key_K, Qt::ControlModifier);
    pump(40);
    sendKey(&window, Qt::Key_K, Qt::NoModifier);
    pump(60);
    expect("Ctrl+K then K opens the settings menu",
           waitFor([&] { return settings_hits > settings_before; }));
    expect("and leaves no control waiting", !window.prefixArmed());

    // Ctrl+K then N opens the split question.
    sendKey(&window, Qt::Key_K, Qt::ControlModifier);
    pump(40);
    sendKey(&window, Qt::Key_N, Qt::NoModifier);
    pump(120);
    expect("Ctrl+K then N asks which way to divide rather than just dividing",
           window.overlayActive());
    expect("and the question offers Vertical then Horizontal",
           window.overlayChoices().size() == 2 && window.overlayChoices()[0] == "Vertical" &&
               window.overlayChoices()[1] == "Horizontal");
    const int panes_before = window.paneCount();
    sendKey(&window, Qt::Key_Escape, Qt::NoModifier);
    pump(120);
    expect("and Escape puts the question away", !window.overlayActive());
    expect("without making a place", window.paneCount() == panes_before);

    // Count pairs of places that share a column span (stacked) or a row span (side by
    // side), straight off the cells the session handed back. A split that stacks adds a
    // stacked pair; one that splits side by side adds a side-by-side pair — whichever place
    // ended up focused, and however the rest of the tree is arranged.
    auto stacked_pairs = [&window] {
        int n = 0;
        for (int a = 0; a < window.paneCount(); ++a)
            for (int b = a + 1; b < window.paneCount(); ++b) {
                const PaneRect x = window.paneCell(a);
                const PaneRect y = window.paneCell(b);
                if (x.col == y.col && x.cols == y.cols && x.row != y.row)
                    ++n;
            }
        return n;
    };
    auto side_pairs = [&window] {
        int n = 0;
        for (int a = 0; a < window.paneCount(); ++a)
            for (int b = a + 1; b < window.paneCount(); ++b) {
                const PaneRect x = window.paneCell(a);
                const PaneRect y = window.paneCell(b);
                if (x.row == y.row && x.rows == y.rows && x.col != y.col)
                    ++n;
            }
        return n;
    };

    // Answer with the H key: the new place stacks under the one looked at, so a fresh
    // stacked pair (same column and width, one above the other) appears.
    const int stacked_before = stacked_pairs();
    sendKey(&window, Qt::Key_K, Qt::ControlModifier);
    pump(40);
    sendKey(&window, Qt::Key_N, Qt::NoModifier);
    pump(120);
    sendKey(&window, Qt::Key_H, Qt::NoModifier);
    pump(300);
    expect("answering H closes the question",
           waitFor([&window] { return !window.overlayActive(); }));
    expect("and makes a place",
           waitFor([&window, panes_before] { return window.paneCount() == panes_before + 1; }));
    expect("stacked: a new pair shares a column and width, one above the other",
           waitFor([&] { return stacked_pairs() > stacked_before; }));

    // Answer with the V key: side by side, so a fresh pair sharing a row and height appears.
    const int side_before = side_pairs();
    const int panes_before_v = window.paneCount();
    sendKey(&window, Qt::Key_K, Qt::ControlModifier);
    pump(40);
    sendKey(&window, Qt::Key_N, Qt::NoModifier);
    pump(120);
    sendKey(&window, Qt::Key_V, Qt::NoModifier);
    pump(300);
    expect("answering V makes a place beside the one looked at",
           waitFor([&window, panes_before_v] {
               return window.paneCount() == panes_before_v + 1;
           }));
    expect("side by side: a new pair shares a row and height, side by side",
           waitFor([&] { return side_pairs() > side_before; }));

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
