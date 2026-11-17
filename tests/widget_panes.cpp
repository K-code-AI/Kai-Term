// A window that has been told where its panes are. The requirement is one sentence
// long — a drag must not cross into a neighbouring pane — and it is all about what
// happens at a boundary, so these checks drag across boundaries on purpose and read
// back the text that comes out. Real mouse events, real widget; the offscreen platform,
// so no display is needed and nothing here says what the highlight looked like.

#include "../src/terminal/terminalwidget.hpp"

#include <QApplication>
#include <QEvent>
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

    w.shutdown();

    if (failures == 0) {
        std::cout << "PASS: " << checks << " pane-scoped selection checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " pane checks failed\n";
    return 1;
}
