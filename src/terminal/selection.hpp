#pragma once

#include <algorithm>

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

} // namespace kai
