// The shape of a session's screen: the tree of panes, where the dividers land, and whose
// size a session takes when more than one view is looking.
//
// None of this is drawn here. That is the point — the arrangement is arithmetic, and the
// promises worth pinning down are the ones a person feels without being able to name: that
// the screen is tiled with no hole in it, that a divider stops rather than eating a pane,
// that closing a pane gives its room back to the one that shared it, and that a preset
// saved today reads back as the same arrangement tomorrow.
//
// No Qt in the model, so no Qt here: it runs headless in a fraction of a second.

#include "../src/session/layout.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
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

// Every cell of the area, counted: which pane claims it, which a divider holds, and which
// nothing claims. A layout that overlaps two panes, leaves a band of the screen unassigned,
// or plants a divider in the middle of a pane is the failure mode of every tiling scheme
// ever written, and it is much easier to see here than in a screenshot. The dividers are
// part of the count because they are part of the screen: a cell the window paints as a
// divider belongs to no pane, which is not the same as belonging to nothing.
struct Coverage {
    int holes = 0;
    int overlaps = 0;
    int divider_cells = 0;
};

Coverage cover(const Layout& l, int rows, int cols) {
    std::vector<int> claim(static_cast<size_t>(rows * cols), 0); // 1 = pane, 2 = divider
    Coverage out;
    for (const CellRect& r : l.rects(rows, cols)) {
        for (int row = r.top; row < r.top + r.rows; ++row) {
            for (int col = r.left; col < r.left + r.cols; ++col) {
                if (row < 0 || row >= rows || col < 0 || col >= cols) {
                    ++out.overlaps; // a pane drawn off the screen claims nothing real
                    continue;
                }
                int& cell = claim[static_cast<size_t>(row * cols + col)];
                if (cell != 0)
                    ++out.overlaps;
                cell = 1;
            }
        }
    }
    for (const CellRect& r : l.dividerRects(rows, cols)) {
        for (int row = r.top; row < r.top + r.rows; ++row) {
            for (int col = r.left; col < r.left + r.cols; ++col) {
                if (row < 0 || row >= rows || col < 0 || col >= cols) {
                    ++out.overlaps;
                    continue;
                }
                int& cell = claim[static_cast<size_t>(row * cols + col)];
                if (cell != 0)
                    ++out.overlaps; // a divider standing inside a pane
                else {
                    cell = 2;
                    ++out.divider_cells;
                }
            }
        }
    }
    for (const int n : claim) {
        if (n == 0)
            ++out.holes;
    }
    return out;
}

// Every cell of the screen accounted for — exactly one pane, or exactly one divider.
bool tiles(const Layout& l, int rows, int cols) {
    const Coverage c = cover(l, rows, cols);
    return c.holes == 0 && c.overlaps == 0;
}
bool mentions(const std::vector<std::string>& problems, std::string_view needle) {
    for (const std::string& p : problems) {
        if (p.find(std::string(needle)) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

int main() {
    {
        // One pane is not a special case bolted on afterwards; it is what every window
        // starts as, and it gets the whole screen.
        const Layout one;
        expect("a window that has never been split has one pane", one.paneCount() == 1);
        expect("and that pane is the focused one", one.focused() == 0);
        expect("and it has no divider to drag", one.splitCount() == 0);
        const std::vector<CellRect> whole = one.rects(24, 80);
        expect("and it is given all of the screen",
               whole.size() == 1 && whole[0] == CellRect{0, 0, 24, 80});
        expect("with nothing to divide it", one.dividerCells(24, 80) == 0);
        expect("and a pane nobody asked about has no rectangle at all",
               one.rectOf(41, 24, 80).empty());
    }

    {
        // Two panes, the two ways, with the divider costing a cell of its own.
        Layout side_by_side;
        const int right = side_by_side.splitPane(0, Split::LeftRight, 24, 80);
        expect("a split hands back the new pane", right == 1);
        const std::vector<CellRect> pair = side_by_side.rects(24, 80);
        expect("two panes side by side fill the width with a divider between them",
               pair.size() == 2 && pair[0].left == 0 && pair[0].rows == 24 &&
                   pair[1].left == pair[0].cols + 1 && pair[1].rows == 24);
        expect("and 80 columns of that is 39, a line, and 40",
               pair[0].cols == 39 && pair[1].cols == 40);
        expect("so the screen is tiled, not holed", tiles(side_by_side, 24, 80));
        expect("and the divider is counted", side_by_side.dividerCells(24, 80) == 1);

        Layout stacked;
        stacked.splitPane(0, Split::UpDown, 24, 80);
        const std::vector<CellRect> stack = stacked.rects(24, 80);
        expect("two panes stacked split the rows the same way",
               stack.size() == 2 && stack[0].top == 0 && stack[0].cols == 80 &&
                   stack[1].top == stack[0].rows + 1 && stack[1].cols == 80);
        expect("and that is 11 rows, a line, and 12",
               stack[0].rows == 11 && stack[1].rows == 12);
        expect("tiled as well", tiles(stacked, 24, 80));
    }

    {
        // The arrangement from the request, word for word: one pane on the left half and
        // two stacked on the right. A record that could only hold a grid could not hold
        // this, which is the reason it is a tree.
        // (Kaida, 2026-11-21: "1 pane using the half left of the screen and 2 pane
        // horizontally spliting the right side".)
        Layout l;
        const int right = l.splitPane(0, Split::LeftRight, 24, 80);
        const int lower_right = l.splitPane(right, Split::UpDown, 24, 80);
        expect("the right side splits into two", lower_right == 2);
        expect("so there are three panes", l.paneCount() == 3);
        const std::vector<CellRect> r = l.rects(24, 80);
        expect("the left pane is the whole height", r.size() == 3 && r[0].rows == 24);
        expect("and the two on the right share the right-hand width",
               r[1].left == r[2].left && r[1].left > r[0].left && r[1].cols == r[2].cols);
        expect("stacked one above the other", r[1].top == 0 && r[2].top > r[1].top);
        expect("with the left pane still half the window", r[0].cols == 39);
        expect("and no hole anywhere in it", tiles(l, 24, 80));
        expect("counting both dividers", l.dividerCells(24, 80) == 2);
    }

    {
        // Repeated splits in one direction stay even. Filling a wide window means pressing
        // "new place" a few times, each split dividing the place you are looking at; without
        // a rebalance the second and third new place are halved into slivers beside a fat
        // first one, which is the uneven split the report was about.
        Layout l;
        l.splitPane(l.focused(), Split::LeftRight, 24, 80);
        l.splitPane(l.focused(), Split::LeftRight, 24, 80);
        l.splitPane(l.focused(), Split::LeftRight, 24, 80);
        const std::vector<CellRect> before = l.rects(24, 80);
        int widest_before = 0, narrowest_before = 999;
        for (const CellRect& one : before) {
            widest_before = std::max(widest_before, one.cols);
            narrowest_before = std::min(narrowest_before, one.cols);
        }
        expect("three same-direction splits make four places", before.size() == 4);
        expect("and left alone they are uneven — the fat-first-pane the report saw",
               widest_before - narrowest_before > 1);
        l.evenize();
        const std::vector<CellRect> after = l.rects(24, 80);
        int widest = 0, narrowest = 999;
        for (const CellRect& one : after) {
            widest = std::max(widest, one.cols);
            narrowest = std::min(narrowest, one.cols);
        }
        expect("evenize shares the width evenly, within a cell", widest - narrowest <= 1);
        expect("and it does that without putting a hole in the screen", tiles(l, 24, 80));
    }

    {
        // A 4x4 is what happens when the same split is asked for evenly: sixteen panes, and
        // the odd cell from an indivisible width going somewhere sensible rather than to
        // the last pane as a bonus.
        const Layout grid = Layout::grid(4, 4);
        expect("a grid of four by four has sixteen panes", grid.paneCount() == 16);
        const std::vector<CellRect> r = grid.rects(24, 80);
        bool all_reachable = r.size() == 16;
        for (const CellRect& one : r) {
            if (one.rows < 1 || one.cols < 1)
                all_reachable = false;
        }
        expect("every one of them has room to be seen", all_reachable);
        expect("and the screen is still tiled, thirteen lines and all",
               tiles(grid, 24, 80));
        int smallest_rows = 99;
        int smallest_cols = 99;
        for (const CellRect& one : r) {
            smallest_rows = std::min(smallest_rows, one.rows);
            smallest_cols = std::min(smallest_cols, one.cols);
        }
        expect("none of them is a sliver at this size",
               smallest_rows >= 4 && smallest_cols >= 15);
        expect("and they are as even as 21 rows can be", smallest_rows >= 4 &&
                                                            smallest_rows <= 6);
    }

    {
        // Closing gives the room back to whoever shared the split, which is the difference
        // between a collapse and a hole.
        Layout l;
        const int right = l.splitPane(0, Split::LeftRight, 24, 80);
        const int lower_right = l.splitPane(right, Split::UpDown, 24, 80);
        expect("closing the bottom-right pane is allowed", l.closePane(lower_right));
        expect("and two panes are left", l.paneCount() == 2);
        const std::vector<CellRect> r = l.rects(24, 80);
        expect("the one above it takes the whole right side",
               r.size() == 2 && r[1].rows == 24 && r[1].cols == 40);
        expect("so the screen is still tiled", tiles(l, 24, 80));
        // Which two are left has to be asked, not counted: the number of the pane that was
        // just closed is gone for good, so the numbers on screen are not 0 and 1 in order.
        const std::vector<int> two_left = l.panes();
        expect("closing either of the last two leaves one", l.closePane(two_left.back()));
        expect("and the last pane cannot be closed", !l.closePane(two_left.front()));
        expect("which is one pane, not none", l.paneCount() == 1);
    }

    {
        // Numbers are not handed out twice. The daemon has the same rule for sessions, and
        // for the same reason: a number that came back after its pane was closed would
        // point at a pane that is not the one somebody typed the number for.
        Layout l;
        const int a = l.splitPane(0, Split::LeftRight, 24, 80);
        l.closePane(a);
        const int b = l.splitPane(0, Split::LeftRight, 24, 80);
        expect("a closed pane's number is not given to the next one", b != a);
        expect("and the new pane really exists", l.hasPane(b));
        expect("while the closed one does not", !l.hasPane(a));
    }

    {
        // Dragging a divider. It moves by cells because a pointer moves by pixels and a
        // terminal is cells; it stops at both ends; and it never takes a pane to nothing,
        // because a pane at zero cells cannot be dragged back — there is nothing left to
        // point at.
        Layout l;
        const int right = l.splitPane(0, Split::LeftRight, 24, 80);
        expect("a divider moves", l.resizeDivider(right, 10, 24, 80, Split::LeftRight));
        expect("and the panes are 49 and 30 now",
               l.rectOf(0, 24, 80).cols == 49 && l.rectOf(right, 24, 80).cols == 30);
        expect("moving it back returns the width", l.resizeDivider(right, -10, 24, 80, Split::LeftRight));
        expect("and it stops at the left edge", l.resizeDivider(right, -1000, 24, 80, Split::LeftRight));
        expect("leaving the first pane one cell", l.rectOf(0, 24, 80).cols == 1);
        expect("and the other pane everything else", l.rectOf(right, 24, 80).cols == 78);
        expect("pushing further right stops too", l.resizeDivider(right, 5000, 24, 80, Split::LeftRight));
        expect("with the far pane down to one cell", l.rectOf(right, 24, 80).cols == 1);
        expect("tiled the whole way down the range",
               tiles(l, 24, 80));
        Layout solo;
        expect("and a window with one pane has no divider to move",
               !solo.resizeDivider(0, 1, 24, 80, Split::LeftRight));
    }

    {
        // One press, one cell, the whole way to each end — at every width a window can be.
        //
        // A divider that stops answering while the pane beside it still has room is what a
        // person calls a freeze, and it came from the fraction a moved line is stored in.
        // `divide` floors its way back to cells, and a ratio written as `cells / room` is a
        // hair under that fraction as a double often enough to floor to one cell short of
        // where it was written. The next press worked the fraction out again, got the one it
        // had just stored, said yes and moved nothing — while the opposite direction stepped
        // off the bad value, sometimes two cells at a time. So one pane could be walked down
        // to a single column beside an 82-column neighbour with no way back, and the two
        // sides of one line did not have the same limits in practice even though the rule
        // says they do. (Kaida, 2026-11-25: "it doesn't let you resize after a certain point
        // it just freeze even when there's plenty of space", "pane 1 can get way smaller than
        // pane 2, but they should be the same". A 50x200 window stopped after one press.)
        bool one_cell_per_press = true;
        bool both_ends_reachable = true;
        bool tiled_the_whole_way = true;
        for (int cols = 3; cols <= 320; ++cols) {
            Layout l;
            const int right = l.splitPane(0, Split::LeftRight, 24, cols);
            const int widest = cols - 2; // a cell for the line, a cell for the other pane
            if (widest < 2)
                continue; // a line with nowhere to travel is a three-column window
            for (const int pane : {0, right}) {
                const int starts_at = l.rectOf(pane, 24, cols).cols;
                int narrowest = starts_at;
                int widest_seen = starts_at;
                for (const int delta : {-1, 1}) {
                    for (int press = 0; press < cols + 2; ++press) {
                        const int before = l.rectOf(pane, 24, cols).cols;
                        if (!l.resizeDivider(pane, delta, 24, cols, Split::LeftRight))
                            break;
                        const int after = l.rectOf(pane, 24, cols).cols;
                        // Which way the pane moves depends on which side of the line it is
                        // on, so what every press owes is one cell and not a sign.
                        if (after == before || std::abs(after - before) != 1)
                            one_cell_per_press = false;
                        narrowest = std::min(narrowest, after);
                        widest_seen = std::max(widest_seen, after);
                    }
                }
                if (narrowest != 1 || widest_seen != widest)
                    both_ends_reachable = false;
                if (!tiles(l, 24, cols))
                    tiled_the_whole_way = false;
            }
        }
        expect("every press of a divider moves it exactly one cell, at any width",
               one_cell_per_press);
        expect("and a pane can reach one cell at one end and all but one at the other",
               both_ends_reachable);
        expect("with the screen tiled the whole way along", tiled_the_whole_way);
    }

    {
        // Up and down is the same arithmetic turned a quarter round, and the nested case is
        // where a line belongs to a branch higher up: a stacked place is widened by the
        // split beside it, not the one it was made by.
        bool moved_by_one = true;
        for (int rows = 3; rows <= 120; ++rows) {
            Layout l;
            const int right = l.splitPane(0, Split::LeftRight, rows, 100);
            const int lower = l.splitPane(right, Split::UpDown, rows, 100);
            for (const int pane : {0, right, lower}) {
                for (const Split axis : {Split::LeftRight, Split::UpDown}) {
                    for (const int delta : {-1, 1}) {
                        for (int press = 0; press < rows + 2; ++press) {
                            const CellRect was = l.rectOf(pane, rows, 100);
                            if (!l.resizeDivider(pane, delta, rows, 100, axis))
                                break;
                            const CellRect now = l.rectOf(pane, rows, 100);
                            const int before = axis == Split::LeftRight ? was.cols : was.rows;
                            const int after = axis == Split::LeftRight ? now.cols : now.rows;
                            if (after == before || std::abs(after - before) != 1)
                                moved_by_one = false;
                        }
                    }
                }
                expect("a stacked place still leaves the screen tiled", tiles(l, rows, 100));
            }
        }
        expect("and a nested divider moves one cell at a time too", moved_by_one);
    }

    {
        // The words a preset is written in have to carry a dragged line to the same cell.
        // Writing the ratio as the middle of the cell rather than its edge is what does it:
        // the nearest number the words can hold is still inside the same cell, where an edge
        // lands on a boundary and rounds to whichever side the decimals fall.
        bool kept_the_line = true;
        for (int cols = 3; cols <= 320; ++cols) {
            Layout l;
            l.splitPane(0, Split::LeftRight, 24, cols);
            for (int press = 0; press < 7; ++press)
                static_cast<void>(l.resizeDivider(0, 1, 24, cols, Split::LeftRight));
            const int before = l.rectOf(0, 24, cols).cols;
            std::vector<std::string> complaints;
            const SessionLayout back = SessionLayout::fromText(
                SessionLayout{l, ViewMode::SideBySide}.toText(), complaints);
            if (!complaints.empty() || back.layout.rectOf(0, 24, cols).cols != before)
                kept_the_line = false;
        }
        expect("a dragged divider reads back from a preset in the cell it was left in",
               kept_the_line);
    }

    {
        // A line is described by the room it is in, not by the window it happens to sit in.
        // `rectOfNode` used to answer for a nested branch by treating that branch as the root
        // of the window, so a line inside half the window was reported as running across all
        // of it. The window draws its resize marks from that answer, so place 1's horizontal
        // line grew a second, identical pair of arrows over the places on the other side of
        // the vertical line — the split there is somebody else's, and pressing the arrow does
        // nothing to it. (Kaida, 2026-11-25: "create a vertical pane, then create a horizontal
        // pane, then go to pane 1, create a horizontal pane. then go to resize mode and move a
        // bit, some arrows for resize are duplicated".)
        Layout l;
        const int right = l.splitPane(0, Split::LeftRight, 55, 128);
        const int lower_right = l.splitPane(right, Split::UpDown, 55, 128);
        l.focus(0);
        const int lower_left = l.splitPane(0, Split::UpDown, 55, 128);
        const Layout::DividerInfo ud = l.dividerAlongAxis(0, 55, 128, Split::UpDown);
        expect("the first place's line runs across its own half only",
               ud.ok && ud.at == 27 && ud.from == 0 && ud.span == 63);
        for (const int other : {right, lower_right})
            expect("and no place on the far side of the vertical line is on its run",
                   l.rectOf(other, 55, 128).left >= ud.from + ud.span);
        expect("while the places it does run over are the two it divides",
               l.rectOf(0, 55, 128).top + l.rectOf(0, 55, 128).rows == ud.at &&
                   l.rectOf(lower_left, 55, 128).top == ud.at + 1);
        // The line down the middle is the whole window's, and says so.
        const Layout::DividerInfo lr = l.dividerAlongAxis(0, 55, 128, Split::LeftRight);
        expect("while the line that does run the whole way reports the whole way",
               lr.ok && lr.at == 63 && lr.from == 0 && lr.span == 55);
    }

    {
        // The same fault on a vertical line inside a half, where it was worse than a spare
        // pair of arrows: the line was reported at the column of the branch above it, so its
        // marks were painted on a line it cannot move, and its travel was measured against
        // the whole window's 99 shared cells rather than the 49 it has. A press then stored a
        // ratio that drew the line back where it started — it said yes and moved nothing.
        Layout l;
        const int right = l.splitPane(0, Split::LeftRight, 40, 100);
        const int lower_right = l.splitPane(right, Split::UpDown, 40, 100);
        const int inner = l.splitPane(lower_right, Split::LeftRight, 40, 100);
        const Layout::DividerInfo deep = l.dividerAlongAxis(inner, 40, 100, Split::LeftRight);
        expect("a line inside a half is reported at its own column, not its parent's",
               deep.ok && deep.at == 74);
        expect("and it runs along its own quarter, not the window's height",
               deep.from == 20 && deep.span == 20);
        const int before = l.rectOf(inner, 40, 100).cols;
        expect("so a press on it moves it",
               l.resizeDivider(inner, 1, 40, 100, Split::LeftRight));
        expect("by one cell", l.rectOf(inner, 40, 100).cols == before - 1);
        expect("and the line is where the window draws it",
               l.dividerAlongAxis(inner, 40, 100, Split::LeftRight).at == 75);
        expect("with the place above it left alone",
               l.rectOf(right, 40, 100).cols == 50);
        expect("and the screen still tiled", tiles(l, 40, 100));
    }

    {
        // Equalise is what "unstick this" means.
        Layout l;
        const int right = l.splitPane(0, Split::LeftRight, 24, 80);
        static_cast<void>(l.resizeDivider(right, 25, 24, 80, Split::LeftRight));
        l.equalise();
        expect("equalising puts the divider back in the middle",
               l.rectOf(0, 24, 80).cols == 39);
        expect("and the tiles still add up", tiles(l, 24, 80));
    }

    {
        // No room is a real answer. A split that produced a zero-cell pane would be a pane
        // that cannot be seen, reached by keys, or closed by pointing at.
        Layout l;
        expect("a two-column window has no room to split side by side",
               l.splitPane(0, Split::LeftRight, 24, 2) == -1);
        expect("and a three-column one just does", l.splitPane(0, Split::LeftRight, 24, 3) == 1);
        const Layout three = Layout::grid(1, 3);
        expect("a window too narrow for three panes keeps them all",
               three.paneCount() == 3);
        expect("at one cell each where it must be",
               three.rects(24, 3)[0].cols >= 1);
        expect("and a pane asked for in a direction with no room is refused, not squashed",
               l.splitPane(99, Split::LeftRight, 24, 80) == -1);
    }

    {
        // Walking panes with the keys: reading order, wrapping at the ends.
        Layout l;
        const int right = l.splitPane(0, Split::LeftRight, 24, 80);
        const int lower_right = l.splitPane(right, Split::UpDown, 24, 80);
        const std::vector<int> order = l.panes();
        expect("panes are listed top to bottom, left to right",
               order.size() == 3 && order[0] == 0 && order[1] == right && order[2] == lower_right);
        expect("the new pane starts focused", l.focused() == lower_right);
        expect("one step left goes to the pane above it on the right",
               l.focusStep(-1) == right);
        expect("and stepping off the front arrives at the back",
               l.focusStep(-1) == 0 && l.focusStep(-1) == lower_right);
        l.focus(0);
        expect("a pane can be chosen by number", l.focused() == 0);
        l.focus(77);
        expect("and a number that is not a pane changes nothing", l.focused() == 0);
    }

    {
        // A preset is written by the window and may be edited by a person, so the words
        // have to survive both.
        Layout l;
        const int right = l.splitPane(0, Split::LeftRight, 24, 80);
        static_cast<void>(l.resizeDivider(right, -8, 24, 80, Split::LeftRight));
        static_cast<void>(l.splitPane(right, Split::UpDown, 24, 80, 0.6));
        const std::string text = l.toText();
        std::vector<std::string> problems;
        const Layout back = Layout::fromText(text, problems);
        expect("a layout written out reads back with no complaints",
               problems.empty() && back.paneCount() == 3);
        expect("at the same sizes", back.rects(24, 80) == l.rects(24, 80));
        // The drag left the divider on 31 of the 79 cells a split shares, and the words have
        // to carry exactly that. They carry the middle of the cell the line sits in — 31.5 of
        // 79 — because that is the number which still lands in cell 31 after the decimals
        // have been rounded to six places, and after the same number has been multiplied back
        // out. An edge does not: 31/79 written as a double and floored again came back 30.
        expect("and the ratios are in the words, not guessed",
               text.find("split left-right 0.398735") != std::string::npos);

        // The arrangement from the request, written by hand.
        std::vector<std::string> handwritten;
        const Layout written = Layout::fromText(
            "split left-right 0.5\n  pane\n  split up-down 0.5\n    pane\n    pane\n",
            handwritten);
        expect("a hand-written preset builds the arrangement it describes",
               handwritten.empty() && written.paneCount() == 3);
        expect("and it is the one Kaida described: left half, right half in two",
               written.rects(24, 80)[0].rows == 24 && written.rects(24, 80)[1].left > 0);

        std::vector<std::string> none;
        expect("an empty preset is one pane",
               Layout::fromText("", none).paneCount() == 1 && none.empty());

        std::vector<std::string> bad;
        const Layout wrong = Layout::fromText("pane\nsplit sideways 0.5\n  pane\n  pane\n", bad);
        expect("a direction that is not one of the two is refused by name",
               mentions(bad, "left-right") && mentions(bad, "line 2"));
        expect("and what is left is still a layout rather than nothing",
               wrong.paneCount() >= 1);

        std::vector<std::string> ratio_out;
        static_cast<void>(Layout::fromText("split up-down 1.5\n  pane\n  pane\n", ratio_out));
        expect("a ratio outside the range says so", mentions(ratio_out, "between 0 and 1"));

        std::vector<std::string> nonsense;
        static_cast<void>(Layout::fromText("pane\ngrid 4\n", nonsense));
        expect("a word that means nothing here is said back with its line",
               mentions(nonsense, "line 2") && mentions(nonsense, "grid 4"));

        std::vector<std::string> lopsided;
        const Layout one_child =
            Layout::fromText("split left-right 0.5\n  pane\n", lopsided);
        expect("a split with only one thing under it is not a split",
               one_child.paneCount() == 1 && one_child.splitCount() == 0);

        std::vector<std::string> commented;
        const Layout with_comments = Layout::fromText(
            "# my work layout\n\nsplit left-right 0.5\n  pane\n  pane\n", commented);
        expect("comments and blank lines are allowed and quiet",
               commented.empty() && with_comments.paneCount() == 2);
    }

    {
        // A session's panes and how they are shown. A tab *is* a pane (Kaida, 2026-11-23),
        // so the tab bar is the list of panes and switching views moves nothing: the same
        // shells, arranged at once or waiting behind a bar one at a time.
        SessionLayout s;
        expect("a session starts with one pane, which is one tab",
               s.paneCount() == 1 && s.tabs().size() == 1);
        expect("and shows them side by side until told otherwise",
               s.mode == ViewMode::SideBySide);
        const int second = s.layout.splitPane(0, Split::LeftRight, 24, 80);
        const int third = s.layout.splitPane(s.active(), Split::UpDown, 24, 80);
        expect("splitting a pane adds a tab, because a tab is a pane",
               second >= 0 && third >= 0 && s.paneCount() == 3 && s.tabs().size() == 3);
        expect("the bar is the panes, in the order they sit in the tree",
               s.tabs() == s.layout.panes());

        const std::vector<CellRect> before = s.layout.rects(24, 80);
        const std::vector<int> listed_before = s.tabs();
        const int shown_before = s.active();
        s.mode = ViewMode::Tabs;
        expect("switching to tab view moves not one cell of the tree",
               s.layout.rects(24, 80) == before);
        expect("and the same shells are on the bar in either view", s.tabs() == listed_before);
        expect("with the same one being looked at", s.active() == shown_before);
        s.mode = ViewMode::SideBySide;
        expect("and back again without a complaint", s.layout.rects(24, 80) == before);

        s.focus(s.tabs()[0]);
        expect("along the bar lands on the next pane", s.focusStep(1) == s.tabs()[1]);
        expect("and back off it lands where it came from", s.focusStep(-1) == s.tabs()[0]);
        expect("the bar wraps at both ends instead of sticking",
               s.focusStep(-1) == s.tabs()[2] && s.focusStep(1) == s.tabs()[0]);
        const int to_close = s.tabs()[1];
        expect("closing a pane closes its tab with it", s.layout.closePane(to_close));
        expect("and the bar is one tab shorter", s.tabs().size() == 2);
        expect("with nobody looking at a closed pane", s.active() != to_close);
    }

    {
        // The words an arrangement is saved as, which is what `kai-term layout` prints and
        // a preset file holds.
        SessionLayout s;
        static_cast<void>(s.layout.splitPane(0, Split::LeftRight, 24, 80));
        s.mode = ViewMode::Tabs;
        const std::string text = s.toText();
        std::vector<std::string> problems;
        const SessionLayout back = SessionLayout::fromText(text, problems);
        expect("a session preset reads back with no complaints",
               problems.empty() && back.paneCount() == 2);
        expect("with its panes where they were",
               back.layout.rects(24, 80) == s.layout.rects(24, 80));
        expect("and in the view it was saved in", back.mode == ViewMode::Tabs);
        expect("because the view is a word in the file, not a guess",
               text.rfind("view tabs", 0) == 0);

        problems.clear();
        const SessionLayout sideways = SessionLayout::fromText("view sideways\npane\n", problems);
        expect("a view that is neither is refused",
               !problems.empty() &&
                   problems[0].find("side-by-side or tabs") != std::string::npos);
        expect("and the panes that could be read are still read",
               sideways.paneCount() == 1);

        // The shape this record had that morning, when a tab was a level holding panes.
        // Refusing the word is worth the words to say why: a file written against the old
        // idea is somebody's work, and "I do not know 'window'" tells them nothing.
        problems.clear();
        static_cast<void>(SessionLayout::fromText("window build\n  pane\n", problems));
        expect("the old `window` shape is refused as the change it is",
               !problems.empty() && problems[0].find("a tab is a pane") != std::string::npos);
    }

    {
        // Whose size a session takes when more than one view is looking at it. Chosen over
        // the last-one-to-speak rule the daemon has had from the start, because that made
        // the screen mean whatever arrived last rather than what everybody could show.
        // (Kaida, 2026-11-21: the smallest view wins.)
        const ViewSize big{50, 200};
        const ViewSize small{24, 80};
        const ViewSize middle{30, 120};
        expect("one view's size is the session's size",
               smallestView({big}, {24, 80}).rows == 50);
        expect("two views give the session the smaller of them",
               smallestView({big, small}, {24, 80}) == small);
        expect("in any order", smallestView({small, big}, {24, 80}) == small);
        expect("and the narrowest when the heights tie",
               smallestView({middle, {30, 90}}, {24, 80}) == ViewSize{30, 90});
        expect("a view that has not said its size is nobody's limit",
               smallestView({{0, 0}, middle}, {24, 80}) == middle);
        expect("and with nobody looking the fallback answers",
               smallestView({}, {40, 132}) == ViewSize{40, 132});
    }

    if (failures == 0) {
        std::cout << "PASS: " << checks << " layout checks passed\n";
        return 0;
    }
    std::cout << "FAIL: " << failures << " of " << checks << " layout checks failed\n";
    return 1;
}
