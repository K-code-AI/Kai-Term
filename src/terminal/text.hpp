#pragma once

#include "screen.hpp"
#include "selection.hpp"

#include <string>

namespace kai {

// Text of one row over the half-open column range [first_col, last_col).
//
// Empty cells become a space. The continuation cell of a double-width glyph is
// skipped, so "漢" comes back once rather than twice; a range that opens on that
// continuation contributes nothing, since the glyph it belongs to is cut off.
// Columns beyond the end of the row contribute nothing: scrollback rows keep the
// width they were written at and can be shorter than the current screen.
inline std::string rowText(const Row& row, int first_col, int last_col,
                           bool trim_trailing_spaces = true) {
    const int cols = static_cast<int>(row.size());
    const int first = std::max(0, std::min(first_col, cols));
    const int last = std::max(first, std::min(last_col, cols));

    std::string out;
    int skip_until = -1;
    // Walked from column zero even when the range opens later, because a range
    // that opens on the second cell of a double-width glyph has to know it, and
    // only the cell before says so: an empty cell on its own looks the same.
    for (int c = 0; c < last; ++c) {
        const Cell& cell = row[static_cast<std::size_t>(c)];
        // Decided before the cell is read: a double-width glyph skips the cells
        // after it, never itself.
        const bool continuation = c < skip_until;
        if (cell.width >= 2)
            skip_until = c + cell.width;
        if (c < first || continuation)
            continue;
        out += cell.text.empty() ? " " : cell.text;
    }

    if (trim_trailing_spaces) {
        while (!out.empty() && out.back() == ' ')
            out.pop_back();
    }
    return out;
}

// Text of the whole visible grid, one entry per row joined with "\n". Trailing
// blanks are dropped by default because terminal rows are padded to the full
// width and that padding is not content.
inline std::string screenText(const Screen& s, bool trim_trailing_spaces = true) {
    std::string out;
    for (int r = 0; r < s.rows(); ++r) {
        if (r)
            out += '\n';
        out += rowText(s.row(r), 0, s.cols(), trim_trailing_spaces);
    }
    return out;
}

// Text of a selection, given a way to fetch the row for a display index.
//
// row_at(rows, index) returns the row shown at that display row. The terminal
// passes the live screen here, and the widget passes something that also
// resolves scrollback rows, so selecting history text works while scrolled up
// without duplicating this logic.
template <typename RowAt>
inline std::string selectionTextAt(int cols, const Selection& sel, RowAt&& row_at,
                                   bool trim_trailing_spaces = true) {
    if (sel.empty())
        return {};

    const CellPos f = sel.first();
    const CellPos l = sel.last();
    const int last_row = (l.col > 0) ? l.row : l.row - 1;

    std::string out;
    for (int r = f.row; r <= last_row; ++r) {
        const int lo = (r == f.row) ? f.col : 0;
        const int hi = (r == l.row) ? l.col : cols;
        if (r > f.row)
            out += '\n';
        out += rowText(row_at(r), lo, hi, trim_trailing_spaces);
    }
    return out;
}

// Text of a selection over the visible grid, lines joined with "\n".
//
// A selection that ends in column 0 of a row selected nothing on that row, so
// the trailing empty line is left out instead of adding a blank line to the
// clipboard.
inline std::string selectionText(const Screen& s, const Selection& sel,
                                 bool trim_trailing_spaces = true) {
    return selectionTextAt(s.cols(), sel, [&](int r) -> const Row& { return s.row(r); },
                           trim_trailing_spaces);
}

} // namespace kai
