#pragma once

#include <algorithm>
#include <vector>

namespace kai {

// A cell coordinate in the visible grid.
struct CellPos {
    int row = 0;
    int col = 0;
};

// A drag selection over the grid.
//
// anchor is where the drag started and head is where it currently is. Either
// may come after the other, so consumers read the normalised pair through
// first() and last(). The head column is exclusive, the way xterm selects: a
// press and release in the same cell selects nothing, and a drag one cell to
// the right selects the cell the drag started on.
struct Selection {
    CellPos anchor{};
    CellPos head{};

    bool empty() const {
        return anchor.row == head.row && anchor.col == head.col;
    }

    bool ordered() const {
        return anchor.row < head.row || (anchor.row == head.row && anchor.col <= head.col);
    }

    // First selected cell, inclusive.
    CellPos first() const { return ordered() ? anchor : head; }

    // End of the selection, exclusive.
    CellPos last() const { return ordered() ? head : anchor; }

    bool contains(int row, int col) const {
        if (empty())
            return false;
        const CellPos f = first();
        const CellPos l = last();
        if (row < f.row || row > l.row)
            return false;
        if (f.row == l.row)
            return col >= f.col && col < l.col;
        if (row == f.row)
            return col >= f.col;
        if (row == l.row)
            return col < l.col;
        return true;
    }

    // Clamp both ends into a grid so a selection survives a resize that shrinks
    // the visible area.
    Selection clamped(int rows, int cols) const {
        auto clamp_pos = [rows, cols](CellPos p) {
            p.row = std::clamp(p.row, 0, std::max(0, rows - 1));
            p.col = std::clamp(p.col, 0, std::max(0, cols));
            return p;
        };
        return Selection{clamp_pos(anchor), clamp_pos(head)};
    }
};

// What kind of content a cell holds, for word-wise selection.
enum class CellKind {
    Blank, // nothing to select
    Word,  // letter, digit or underscore
    Other  // visible but not part of a word: punctuation, symbols, glyphs
};

// Expand a clicked column to the word around it.
//
// kind_of(col) classifies one column, so this header stays independent of the
// cell representation. Clicking a word selects the whole word; clicking other
// visible content selects that one cell; clicking blank space selects nothing,
// so a stray double-click in the margin does not produce an empty copy.
// The returned range is [start, end); start == end means nothing selected.
template <typename KindOf>
inline void expandToWord(int cols, int col, KindOf kind_of, int& start, int& end) {
    start = col;
    end = col;
    if (col < 0 || col >= cols)
        return;

    const CellKind clicked = kind_of(col);
    if (clicked == CellKind::Blank)
        return;
    if (clicked == CellKind::Other) {
        end = col + 1;
        return;
    }

    start = col;
    while (start > 0 && kind_of(start - 1) == CellKind::Word)
        --start;
    end = col + 1;
    while (end < cols && kind_of(end) == CellKind::Word)
        ++end;
}

// The part of the grid one pane owns, in cell coordinates.
//
// A terminal showing several shells at once has to keep a drag inside the one it
// started in. Text copied out of the left half of a split should not arrive with the
// right half's text glued onto the end of every line, and a drag that wanders down
// into the pane below picks up commands from a shell the person was not looking at.
//
// Rows are inclusive at both ends — the last row of a pane is part of it — while the
// end column is exclusive, because that is how a selection end is written: a drag
// stopping at the pane boundary is a drag that selected up to, but not including, the
// first cell of its neighbour.
struct PaneRect {
    int row = 0;
    int col = 0;
    int rows = 0;
    int cols = 0;

    int firstRow() const { return row; }
    int lastRow() const { return row + rows - 1; }
    int firstCol() const { return col; }
    int endCol() const { return col + cols; }

    bool valid() const { return rows > 0 && cols > 0; }

    // Two panes are the same pane when they cover the same cells. Asked because a view is
    // told its tab bar now and then, and a bar that has not moved should not be redrawn,
    // resized, or announced as news.
    friend bool operator==(const PaneRect& a, const PaneRect& b) {
        return a.row == b.row && a.col == b.col && a.rows == b.rows && a.cols == b.cols;
    }

    bool contains(int r, int c) const {
        return r >= firstRow() && r <= lastRow() && c >= firstCol() && c < endCol();
    }

    int clampRow(int r) const { return std::clamp(r, firstRow(), lastRow()); }

    int clampEndCol(int c) const { return std::clamp(c, firstCol(), endCol()); }
};

// One pane, the screen: what a terminal with no panes has.
inline PaneRect wholeGridPane(int rows, int cols) {
    return PaneRect{0, 0, rows, cols};
}

// The pane a drag belongs to, which is the one holding the cell the drag started in —
// not wherever it happens to have reached.
//
// A host that has not said where its panes are gets the whole grid, and so does a press
// that lands on nothing: a divider between panes, or a cell outside every listed pane.
// A drag that refuses to start is worse than one that is not clipped, and a terminal
// that stops selecting whenever a layout is described badly is a bug report nobody can
// reproduce.
inline PaneRect paneForDrag(const std::vector<PaneRect>& panes, const CellPos& at, int rows,
                            int cols) {
    for (const PaneRect& pane : panes) {
        if (pane.valid() && pane.contains(at.row, at.col))
            return pane;
    }
    return wholeGridPane(rows, cols);
}

} // namespace kai
