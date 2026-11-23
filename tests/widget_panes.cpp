// A window that has been told where its panes are. The requirement is one sentence
// long — a drag must not cross into a neighbouring pane — and it is all about what
// happens at a boundary, so these checks drag across boundaries on purpose and read
// back the text that comes out. Real mouse events, real widget; the offscreen platform,
// so no display is needed and nothing here says what the highlight looked like.

#include "../src/terminal/terminalwidget.hpp"

#include <QApplication>
#include <QEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QString>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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

void expectStr(std::string_view what, const std::string& got, const std::string& want) {
    ++checks;
    if (got == want)
        return;
    ++failures;
    std::cerr << what << ": want [" << want << "] got [" << got << "]\n";
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

void sendMouse(kai::TerminalWidget* w, QEvent::Type type, Qt::MouseButton button,
               Qt::MouseButtons buttons, const QPointF& at) {
    QMouseEvent ev(type, at, at, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

QPointF cellPoint(kai::TerminalWidget* w, int row, int col) {
    const QPointF top_left = w->cellTopLeft(row, col);
    const QSizeF cell = w->cellSize();
    return QPointF(top_left.x() + cell.width() * 0.5, top_left.y() + cell.height() * 0.5);
}

// Press, move, release: a drag, which is what a person does with a mouse.
void drag(kai::TerminalWidget* w, int from_row, int from_col, int to_row, int to_col) {
    sendMouse(w, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton,
              cellPoint(w, from_row, from_col));
    sendMouse(w, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton, cellPoint(w, to_row, to_col));
    sendMouse(w, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
              cellPoint(w, to_row, to_col));
}

// Three presses on one cell, which is how this widget decides a drag means lines.
void tripleClick(kai::TerminalWidget* w, int row, int col) {
    for (int click = 0; click < 3; ++click) {
        sendMouse(w, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton,
                  cellPoint(w, row, col));
        sendMouse(w, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
                  cellPoint(w, row, col));
    }
}

// The last row of the live grid holding `needle`, and where in it. The last, because
// a typed command is echoed as it goes in and printed again by whatever it ran: the
// second one is the output.
CellPos lastRowWith(kai::TerminalWidget& w, std::string_view needle) {
    CellPos found{-1, -1};
    for (int row = 0; row < w.rows(); ++row) {
        const std::string text = w.screen().rowText(row);
        const std::size_t at = text.find(needle);
        if (at != std::string::npos) {
            found.row = row;
            found.col = static_cast<int>(at);
        }
    }
    return found;
}

std::string rowText(kai::TerminalWidget& w, int row) { return w.screen().rowText(row); }

std::string rtrim(std::string value) {
    while (!value.empty() && value.back() == ' ')
        value.pop_back();
    return value;
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    kai::TerminalWidget w(24, 80, "/bin/sh");
    w.resize(900, 520);
    w.show();
    pump(300);

    const int rows = w.rows();
    const int cols = w.cols();

    w.pasteText("printf 'one-two three-four five-six seven\\n'\n");
    expect("the words to select appear on the screen",
           waitFor([&w] { return lastRowWith(w, "one-two three-four five-six").row >= 0; }));
    w.pasteText("printf 'BOTTOM-MARKER\\n'\n");
    expect("and a second line below them, belonging to nobody in particular",
           waitFor([&w] { return lastRowWith(w, "BOTTOM-MARKER").row >= 0; }));

    const CellPos line = lastRowWith(w, "one-two three-four five-six");
    const std::string text = rowText(w, line.row);
    const int at_three = static_cast<int>(text.find("three-four"));
    const int at_five = static_cast<int>(text.find("five-six"));
    expect("the words are far enough apart to split between",
           line.row >= 0 && at_three > 0 && at_five > at_three + 4 && at_five < cols - 4);
    const int bottom_row = lastRowWith(w, "BOTTOM-MARKER").row;
    expect("and the marker line is below the line being selected", bottom_row > line.row);

    // The divider: everything left of it is one pane, everything from it rightwards is
    // another. It falls between "four" and "five", where a drag that strays picks up a
    // word the person was not selecting.
    const std::vector<PaneRect> side_by_side{
        PaneRect{0, 0, rows, at_five}, PaneRect{0, at_five, rows, cols - at_five}};

    {
        // Without panes a drag goes as far as it is dragged, which is what it has
        // always done and what a terminal with one shell needs.
        drag(&w, line.row, at_three, line.row, cols - 2);
        expectStr("with no panes a drag reaches wherever the pointer went",
                  w.selectedText(), rtrim(text.substr(at_three, cols - 2 - at_three)));
        w.setPanes(side_by_side);
        expectStr("and describing the panes pulls an existing selection in",
                  w.selectedText(), rtrim(text.substr(at_three, at_five - at_three)));
        expect("the end of it now sits on the divider", w.selection().last().col <= at_five);
    }

    {
        // Same drag, same pointer path, one pane in the way.
        drag(&w, line.row, at_three, line.row, cols - 2);
        expectStr("a drag started in the left pane stops at its edge", w.selectedText(),
                  rtrim(text.substr(at_three, at_five - at_three)));
        expect("nothing of the neighbour's word comes with it",
               w.selectedText().find("five-six") == std::string::npos);
    }

    {
        // The other side of the same divider, so the clipping is not simply "the left
        // part of the screen".
        drag(&w, line.row, at_five, line.row, cols - 2);
        expect("a drag started in the right pane keeps its own text",
               w.selectedText().find("five-six") != std::string::npos);
        expect("and does not reach back for the word on the other side",
               w.selectedText().find("three-four") == std::string::npos);
    }

    {
        // Three clicks select the line — the line of the pane, which is the sentence
        // this whole feature is about: the neighbour's half of that row belongs to a
        // different shell.
        tripleClick(&w, line.row, at_three + 1);
        expectStr("three clicks select the pane's line, not the screen's", w.selectedText(),
                  rtrim(text.substr(0, at_five)));
        expect("and the selection ends where the pane ends",
               w.selection().last().col == at_five);
    }

    {
        // A drag over several rows: the rows in the middle of it run the width of the
        // pane. Both halves matter — the text that comes out, and what is painted,
        // which is asked about directly because a highlight crossing the divider would
        // look like a working selection and copy the wrong thing.
        const int from_row = std::max(0, line.row - 1);
        const int to_row = std::min(rows - 1, line.row + 1);
        drag(&w, from_row, at_three, to_row, cols - 2);
        expect("the drag covers more than one row", to_row > from_row);
        expect("its text is the left pane's, row by row",
               w.selectedText().find("five-six") == std::string::npos &&
                   w.selectedText().find("three") != std::string::npos);
        const int middle = (from_row + to_row) / 2;
        expect("a middle row is selected as far as the divider and no further",
               w.selection().contains(middle, at_five + 2) &&
                   !w.selectedAt(middle, at_five + 2));
        expect("while the pane's own cells are selected", w.selectedAt(middle, at_three + 1));
    }

    {
        // One split sideways and one down the middle: a drag started in the top-left
        // pane stops at both edges, whichever way the pointer goes.
        const std::vector<PaneRect> four{
            PaneRect{0, 0, line.row + 1, at_five},
            PaneRect{0, at_five, line.row + 1, cols - at_five},
            PaneRect{line.row + 1, 0, rows - line.row - 1, at_five},
            PaneRect{line.row + 1, at_five, rows - line.row - 1, cols - at_five}};
        w.setPanes(four);
        drag(&w, line.row, at_three, rows - 1, cols - 2);
        expect("a drag does not fall into the pane below",
               w.selection().last().row <= line.row);
        expect("and does not pick up what is written there",
               w.selectedText().find("BOTTOM-MARKER") == std::string::npos);
        expect("and it still stops sideways too", w.selection().last().col <= at_five);
        expect("the drag in the top-left pane is its own text",
               w.selectedText() == rtrim(text.substr(at_three, at_five - at_three)));
    }

    {
        // Saying nothing about panes again, which is what a host does when it has just
        // one shell: the ordinary terminal has to come back, not a window that selects
        // nothing at all.
        w.setPanes({});
        drag(&w, line.row, at_three, line.row, cols - 2);
        expect("with the panes taken away a drag reaches the whole line again",
               w.selectedText().find("five-six") != std::string::npos &&
                   w.selectedText().find("seven") != std::string::npos);
    }

    {
        // A layout described badly must leave a working terminal: a press that lands
        // between two panes, on a divider, selects the way it always did.
        std::vector<PaneRect> with_a_gap{PaneRect{0, 0, rows, at_three},
                                         PaneRect{0, at_three + 3, rows, cols - at_three - 3}};
        w.setPanes(with_a_gap);
        drag(&w, line.row, at_three + 1, line.row, cols - 2);
        expect("a drag started on a divider still selects", !w.selection().empty());
        expect("and is not clipped to nothing",
               w.selectedText().find("five-six") != std::string::npos);
        w.setPanes({});
    }

    {
        // A tab bar is drawn where a session has more than one place to look at, and the
        // grid moves down to make room for it instead of being painted over.
        const int rows_before = w.rows();
        const double first_row_before = w.cellTopLeft(0, 0).y();
        const double row_height = w.cellTopLeft(1, 0).y() - w.cellTopLeft(0, 0).y();

        w.setTabBar(1, 0, false);
        pump(60);
        expect("one place to look at is not a bar of tabs",
               w.paneCount() == 1 && w.rows() == rows_before &&
                   qAbs(w.cellTopLeft(0, 0).y() - first_row_before) < 0.5);

        w.setTabBar(3, 0, false);
        pump(200);
        expect("three places take a row off the grid", w.rows() == rows_before - 1);
        expect("and the grid begins a row lower than it did",
               qAbs(w.cellTopLeft(0, 0).y() - first_row_before - row_height) < 0.5);

        // The pointer has to keep landing on the character it is over. A bar that moved
        // what is drawn without moving what is clicked would put the highlight a line
        // above the word a person is looking at, which reads as a broken selection rather
        // than as a missing row.
        drag(&w, line.row, at_three, line.row, at_three + 8);
        expectStr("a drag after the bar appeared still lands on the words under it",
                  w.selectedText(), rtrim(text.substr(at_three, 8)));

        // A press in the strip belongs to the bar and not to the grid: it changes which
        // place is being looked at, and it selects nothing. Where the strip is comes out of
        // the same arithmetic that draws it, so this says the bar is where it is drawn too.
        const double bar_left = w.cellTopLeft(0, 0).x();
        const double bar_width = w.width() - 2.0 * bar_left;
        const QPointF second_tab(bar_left + bar_width * 0.6, // well inside the second tab
                                 first_row_before + row_height * 0.5);
        const std::string before = w.selectedText(); // whatever the drag above left
        sendMouse(&w, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton, second_tab);
        sendMouse(&w, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton, second_tab);
        expect("a press on the second tab looks at the second place", w.shownPane() == 1);
        expect("and does not start a selection over the grid", w.selectedText() == before);

        w.showPaneAt(2);
        expect("asking to be shown another place is remembered", w.shownPane() == 2);
        w.showPaneAt(9);
        expect("a place past the end of the bar is not looked at", w.shownPane() == 2);
        w.showPaneAt(-1);
        expect("nor is a place before the first one", w.shownPane() == 2);

        w.setTabBar(2, 0, false);
        expect("a bar that shrinks leaves the window looking at a place that exists",
               w.paneCount() == 2 && w.shownPane() < 2);

        w.setTabBar(0, 0, false);
        pump(200);
        expect("and a session with no tabs left gives the row back",
               w.rows() == rows_before &&
                   qAbs(w.cellTopLeft(0, 0).y() - first_row_before) < 0.5);

        // Ctrl+Shift+N and Ctrl+Shift+H ask the session for a place and for a way of looking
        // at them. This window is holding a shell started here, with no session behind it,
        // so the honest expectation is that nothing happens: no place appears, no view
        // changes, and nothing crashes on the way to not happening.
        w.setTabBar(2, 0, true);
        expect("a session showing one place at a time says so", w.viewIsTabs());
        w.setTabBar(2, 0, false);
        expect("and a session showing them side by side says that instead",
               !w.viewIsTabs());
        w.newTab();
        w.toggleViewMode();
        pump(120);
        expect("a window with a shell of its own makes no new place", w.paneCount() == 2);
        expect("and changes no view it does not have", !w.viewIsTabs());
    }

    {
        // What Ctrl+Shift+H changes. Side by side, the bar is a map of the places, so a
        // place ten columns wide does not get half of it; in tab view the bar is a list, and
        // every place is as important as any other. The two answer the same press
        // differently, which is the whole point of having two views.
        const double bar_left = w.cellTopLeft(0, 0).x();
        const double bar_width = w.width() - 2.0 * bar_left;
        const std::vector<kai::PaneRect> uneven{{0, 0, 24, 10}, {0, 11, 24, 70}};

        w.setTabBar(2, 0, false, uneven);
        pump(60);
        const std::vector<double> split = w.tabEdges();
        // Two places and the divider between them: an edge at the start and end of each.
        expect("side by side, the bar carries the divider as well as the places",
               split.size() == 4 && split.back() == 1.0);
        expect("so the narrow place gets its tenth of the bar",
               split[1] > 0.05 && split[1] < 0.2);
        expect("and a press a quarter along it is the wide place",
               w.tabSlotAt(bar_left + bar_width * 0.25, bar_left, bar_width) == 1);
        const double in_divider = bar_left + bar_width * ((split[1] + split[2]) / 2.0);
        expect("while a press on the divider is no place at all",
               w.tabSlotAt(in_divider, bar_left, bar_width) == -1);

        w.setTabBar(2, 0, true, uneven);
        pump(60);
        const std::vector<double> listed = w.tabEdges();
        expect("in tab view the bar is an even list", qAbs(listed[1] - 0.5) < 0.001);
        expect("so the same press is the first place instead",
               w.tabSlotAt(bar_left + bar_width * 0.25, bar_left, bar_width) == 0);

        // Sizes the session did not say about a bar of that many places are not invented:
        // the bar falls back to an even list rather than drawing rooms of made-up widths.
        w.setTabBar(3, 0, false, uneven);
        pump(60);
        expect("and a bar whose sizes do not match its places is drawn even",
               qAbs(w.tabEdges()[1] - 1.0 / 3.0) < 0.001);
        w.setTabBar(0, 0, false, {});
        pump(120);
    }

    w.shutdown();

    {
        // A refusal has to be readable. tmux says them in its status line; there is no
        // status line here, and a refusal that only exists in the daemon's log is a refusal
        // nobody learns from — they press the key again, and again.
        kai::TerminalWidget w(24, 80, "/bin/sh");
        w.resize(900, 500);
        w.show();
        pump(200);

        w.showNotice("no room in this window for another pane");
        pump(60);
        expect("a refusal is put where it can be read",
               w.notice() == "no room in this window for another pane");

        // On screen, near the bottom, where the line between the work and the edge already
        // is — and lit enough to see out of the corner of an eye.
        const QImage before = w.grab().toImage();
        w.showNotice("");
        pump(60);
        const QImage blank = w.grab().toImage();
        w.showNotice("no room in this window for another pane");
        pump(60);
        const QImage shown = w.grab().toImage();
        int bottom_changed = 0;
        for (int y = shown.height() - 30; y < shown.height(); ++y)
            for (int x = 0; x < shown.width(); x += 4)
                if (shown.pixelColor(x, y) != blank.pixelColor(x, y))
                    ++bottom_changed;
        expect("it is drawn along the bottom of the window", bottom_changed > 200);
        int top_changed = 0;
        for (int y = 0; y < shown.height() - 60; y += 2)
            for (int x = 0; x < shown.width(); x += 8)
                if (shown.pixelColor(x, y) != blank.pixelColor(x, y))
                    ++top_changed;
        expect("and it covers nothing else on the screen", top_changed < 40);

        // Gone by itself, because a notice that stays is a thing to clean up; and gone the
        // moment they carry on typing, because the next thing they do is worth more screen
        // than the last thing that did not happen.
        w.showNotice("a short-lived refusal", 120);
        pump(400);
        expect("it goes away on its own", w.notice().empty());
        w.showNotice("here until the next key");
        pump(60);
        expect("it is there to be seen", !w.notice().empty());
        QKeyEvent press(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
        QApplication::sendEvent(&w, &press);
        pump(60);
        expect("and the next key typed clears it", w.notice().empty());
    }


    if (failures == 0) {
        std::cout << "PASS: " << checks << " pane-scoped selection checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " pane checks failed\n";
    return 1;
}
