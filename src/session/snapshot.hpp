#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

#include "../terminal/screen.hpp"

namespace kai {

// Bytes that rebuild a screen inside a terminal that starts out empty.
//
// This is what makes attaching to a running session show what is on it rather than a
// blank window that fills in the next time something prints. Replaying the session's
// raw output would have been cheaper, but it would rebuild the screen at the size the
// bytes were written for: attach from a different window size and every line is
// rewrapped against the wrong width. Asking the model what is on screen now, after it
// has been resized to the attaching window, is the version that is simply correct.
//
// Attributes are written as a full rendition whenever a cell differs from the one
// before it rather than as a minimal diff. The extra bytes only appear where
// attributes actually change, and "set it back to default then set what this cell
// needs" cannot drift out of step with whatever the previous cell left on.
inline std::string colourHex(const Color& c) {
    static const char* digits = "0123456789abcdef";
    std::string out = "#";
    out.push_back(digits[(c.r >> 4) & 0xF]);
    out.push_back(digits[c.r & 0xF]);
    out.push_back(digits[(c.g >> 4) & 0xF]);
    out.push_back(digits[c.g & 0xF]);
    out.push_back(digits[(c.b >> 4) & 0xF]);
    out.push_back(digits[c.b & 0xF]);
    return out;
}

// The palette entry to name for a cell's colour, or -1 when the exact triple
// should travel instead.
//
// A snapshot has to say which colour a cell wears, and there are two different
// answers it could give. The RGB is the sending screen's opinion of that colour;
// the index is what the program asked for. The index is the one that survives
// the trip, because the view on the other side resolves it again through its own
// authorities — a colour the person locked in their config file outranks the
// session there, exactly as it does for live output. An explicit RGB is a colour
// no lock can touch, so a snapshot that sent RGB was the one path that outran a
// saved theme: a resize re-sends the whole screen, and every palette-coloured
// cell came back wearing the session's colours (Kaida, 2026-11-19: "the color
// with the bug are the 16 default colors, those are the one getting resetted
// when resizing").
//
// The cell itself says which of the two it is, in `Cell::fg_role`, because an RGB
// cannot say it: a program that asked for palette entry 1 and a program that
// asked for #800000 leave the same three bytes behind, and only the first of them
// is the view's to recolour. A cell that asked for an exact colour sends the
// exact colour, and is painted that colour in every view, which is what the
// program asked for.
//
// The remembered index is checked against the colour the sending screen actually
// resolved. Where they disagree — a cell written by a path that had no role to
// remember, say — the RGB on the screen is the truth and travels.
inline int colourRole(const Screen& screen, const Color& colour, std::uint8_t role) {
    if (role >= 16)
        return -1; // an exact colour, or the default: no index answers for it
    return screen.paletteColor(role) == colour ? static_cast<int>(role) : -1;
}

// The SGR that puts one colour on, in the form that keeps it a role: an index
// when the program asked for one, the exact triple when it did not. `group` is 38
// for a foreground and 48 for a background.
inline std::string colourSgr(const Screen& screen, int group, const Color& colour,
                             std::uint8_t role) {
    const int index = colourRole(screen, colour, role);
    if (index >= 0)
        return std::to_string(group) + ";5;" + std::to_string(index);
    return std::to_string(group) + ";2;" + std::to_string(colour.r) + ";" +
           std::to_string(colour.g) + ";" + std::to_string(colour.b);
}

// One CSI parameter list, built from the codes that make up one cell's rendition.
//
// `screen` is the one the snapshot is leaving from, and it is asked what a cell's
// palette index resolves to there so the index can travel instead of the RGB. See
// `colourRole()`.
inline std::string rendition(const Screen& screen, const Cell& cell,
                             const Color& default_fg, const Color& default_bg) {
    std::string codes;
    auto add = [&codes](std::string_view code) {
        if (!codes.empty())
            codes += ';';
        codes += code;
    };
    if (cell.attr.bold)
        add("1");
    if (cell.attr.dim)
        add("2");
    if (cell.attr.italic)
        add("3");
    if (cell.attr.underline)
        add("4");
    if (cell.attr.blink)
        add("5");
    if (cell.attr.reverse)
        add("7");
    if (cell.attr.hidden)
        add("8");
    if (cell.attr.strike)
        add("9");
    if (cell.attr.overline)
        add("53");
    // A cell wearing the default colour says nothing here, which is the point:
    // the `CSI 0` below leaves the receiver on its own default, and its own
    // default is the colour its config file locked.
    if (cell.fg != default_fg)
        add(colourSgr(screen, 38, cell.fg, cell.fg_role));
    if (cell.bg != default_bg)
        add(colourSgr(screen, 48, cell.bg, cell.bg_role));
    if (codes.empty())
        return "\x1b[0m";
    // Two complete sequences rather than one reset folded into the parameter list:
    // `CSI 0 ESC` relies on a parser cancelling the unfinished sequence, and this
    // terminal's own parser is what has to read it back.
    return "\x1b[0m\x1b[" + codes + "m";
}

inline bool sameRendition(const Cell& a, const Cell& b) {
    // The roles are part of the rendition, not a detail of how it was spelled:
    // a cell painted with palette entry 1 next to one painted with the identical
    // #800000 look the same here and mean different things to a view that recolours
    // entry 1. Comparing the RGBs alone would let the second cell ride on the first
    // cell's sequence and arrive as the wrong kind of colour.
    return a.attr == b.attr && a.fg == b.fg && a.bg == b.bg && a.fg_role == b.fg_role &&
           a.bg_role == b.bg_role;
}

// The text of one history row, with blank columns kept as spaces so a gap in
// the row does not become two words run together. A wide glyph's continuation
// column is skipped rather than counted as a blank.
inline std::string rowPlainText(const Row* row) {
    std::string out;
    if (!row)
        return out;
    for (std::size_t col = 0; col < row->cells.size();) {
        const Cell& cell = row->cells[col];
        if (cell.text.empty()) {
            out.push_back(' ');
            ++col;
        } else {
            out += cell.text;
            col += std::max(1, cell.width);
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

// The modes the application asked for, replayed around the glyphs.
//
// A snapshot of the grid alone arrives as a picture of a terminal rather than one. The
// mouse stays with the window's own text selection instead of the application that asked
// for it; a program on the alternate screen leaves the lines it never drew still visible
// around its frame; arrow keys go out as plain CSI to an application reading SS3; a paste
// arrives unquoted to an application that asked for it quoted. All four were reported by
// Kaida from btop in a re-attached session, within one sitting, and all four are this
// omission: the modes were never sent, so the new view behaved like a terminal nobody had
// asked anything of.
//
// Split in two because some of them change what the coordinates *mean*. Origin mode makes
// cursor addressing relative to the scroll region, and the glyphs are painted with
// absolute addresses; wrap off would make a full row behave differently as it is written.
// So the alternate screen is left and the region is cleared before the glyphs, and the
// rest are set after the last glyph has been placed, which is where an application would
// have set them anyway.
inline std::string modePrelude() {
    std::string out;
    // Leave whatever alternate screen the view is on, so the primary buffer is the
    // one this snapshot paints. The alternate screen is entered again below when
    // the session is in one.
    out += "\x1b[?1049l";
    // Clear the region and origin for painting; they are restored afterwards.
    out += "\x1b[r\x1b[?6l";
    return out;
}

inline std::string modePostscript(const Screen& screen) {
    std::string out;
    auto set = [&out](int mode, bool on) {
        out += "\x1b[?" + std::to_string(mode) + (on ? "h" : "l");
    };
    // Said both ways even where on is the default, because the view being told may have
    // been used by another session a moment before, and "leave it as you found it" is not
    // something a snapshot can rely on.
    set(1, screen.applicationCursorKeys());
    // The region and origin come back last, once nothing is left to paint: the
    // application is entitled to the screen it asked for, just not to it while the
    // picture of the screen is being drawn.
    if (screen.scrollRegionTop() != 0 || screen.scrollRegionBottom() != screen.rows() - 1)
        out += "\x1b[" + std::to_string(screen.scrollRegionTop() + 1) + ";" +
               std::to_string(screen.scrollRegionBottom() + 1) + "r";
    set(6, screen.originMode());
    set(7, screen.autoWrap());
    set(2004, screen.bracketedPaste());
    switch (screen.mouseMode()) {
        case MouseMode::Off:
            break;
        case MouseMode::Click:
            set(1000, true);
            break;
        case MouseMode::Drag:
            set(1000, true);
            set(1002, true);
            break;
        case MouseMode::Any:
            set(1000, true);
            set(1002, true);
            set(1003, true);
            break;
    }
    // The coordinate encoding, named by the number that selected it: 1005 utf-8, 1006
    // SGR, 1015 urxvt, 1016 SGR hex. An application that asked for one and got another
    // reads every click as a different cell.
    switch (screen.mouseCoord()) {
        case MouseCoord::Classic:
            break;
        case MouseCoord::Utf8:
            set(1005, true);
            break;
        case MouseCoord::Sgr:
            set(1006, true);
            break;
        case MouseCoord::Urxvt:
            set(1015, true);
            break;
        case MouseCoord::SgrHex:
            set(1016, true);
            break;
    }
    return out;
}

// Paint one grid — the primary screen or the alternate screen — without moving
// the cursor, so the caller can decide where the cursor goes afterwards.
inline std::string paintGrid(const Screen& screen, bool primary) {
    const Color default_fg = screen.defaultForeground();
    const Color default_bg = screen.defaultBackground();
    std::string out;
    Cell current{};
    current.fg = default_fg;
    current.bg = default_bg;
    bool have_current = false;
    for (int row = 0; row < screen.rows(); ++row) {
        out += "\x1b[" + std::to_string(row + 1) + ";1H";
        int next_col = 0;
        for (int col = 0; col < screen.cols(); ++col) {
            const Cell& cell = primary ? screen.primaryAt(row, col) : screen.at(row, col);
            if (cell.text.empty())
                continue;
            if (cell.width == 0) {
                next_col = col + 1;
                continue;
            }
            if (col != next_col)
                out += "\x1b[" + std::to_string(row + 1) + ";" + std::to_string(col + 1) + "H";
            if (!have_current || !sameRendition(current, cell)) {
                out += rendition(screen, cell, default_fg, default_bg);
                current = cell;
                have_current = true;
            }
            out += cell.text;
            next_col = col + std::max(1, cell.width);
        }
    }
    return out;
}

// Paint one history row at the top of the screen, keeping the renditions the
// line had when it was written. A history row is a row of cells, not a string:
// a warning printed in red is red in the scrollback too, and the same is true of
// bold, underline and reverse. The row is written with absolute cursor addresses
// for gaps, so blank columns stay where they were.
inline std::string paintHistoryRow(const Screen& screen, const Row* row, int cols,
                             const Color& default_fg, const Color& default_bg) {
    std::string out = "[1;1H";
    if (!row)
        return out;
    Cell current{};
    current.fg = default_fg;
    current.bg = default_bg;
    bool have_current = false;
    int next_col = 0;
    const int limit = std::min(static_cast<int>(row->cells.size()), cols);
    for (int col = 0; col < limit; ++col) {
        const Cell& cell = row->cells[static_cast<std::size_t>(col)];
        if (cell.text.empty())
            continue;
        if (cell.width == 0) {
            next_col = col + 1;
            continue;
        }
        if (col != next_col)
            out += "[1;" + std::to_string(col + 1) + "H";
        if (!have_current || !sameRendition(current, cell)) {
            out += rendition(screen, cell, default_fg, default_bg);
            current = cell;
            have_current = true;
        }
        out += cell.text;
        next_col = col + std::max(1, cell.width);
    }
    return out;
}

inline std::string screenSnapshot(const Screen& screen) {
    const Color default_fg = screen.defaultForeground();
    const Color default_bg = screen.defaultBackground();
    const int rows = screen.rows();
    const int cols = screen.cols();

    std::string out;
    out += "\x1b[?25l"; // the cursor goes last, so it never flickers mid-paint
    out += "\x1b[0m";
    out += modePrelude();
    // Clear the screen and its scrollback first: a snapshot replaces the state
    // it is showing, so replaying it twice must not stack two histories.
    out += "\x1b[H\x1b[3J";

    // Colours the application asked for. Cells store resolved RGB, so the text would
    // survive without these, but an application that later prints with the *default*
    // colour or with a palette index would get this terminal's defaults instead of
    // the session's.
    out += "\x1b]10;" + colourHex(default_fg) + "\x07";
    out += "\x1b]11;" + colourHex(default_bg) + "\x07";
    for (int index = 0; index < 256; ++index) {
        if (!screen.paletteCustomised(index))
            continue;
        out += "\x1b]4;" + std::to_string(index) + ";" + colourHex(screen.paletteColor(index)) +
               "\x07";
    }

    // What has already scrolled away, said first with the renditions it had. Restoring a
    // session should put you back where you were, and a session kept for an hour with
    // nowhere to scroll back to is a session whose work you have lost — the visible screen
    // is the last page of it, not the whole of it. A warning printed in red is red in the
    // scrollback too, not a plain line that lost its colour on the way across. Only the
    // tail goes on the wire: a snapshot is sent to every view of a session every time one
    // of them changes size, so a scrollback of tens of thousands of lines would be a great
    // deal of bytes to move to see a screen that holds eighty of them.
    //
    // A session inside a full-screen program has two buffers: the alternate grid that
    // is showing, and the primary grid and history that come back when the program leaves.
    // Both are carried here. The primary history is written first, then the primary grid,
    // and only then the alternate screen is entered and painted.
    //
    // Each row is written at the top and then one line feed at the bottom pushes it
    // into the scrollback. Writing it at the bottom instead would push whatever was
    // already at the top, which is not the row being restored.
    constexpr int kSnapshotHistoryRows = 2000;
    const int history = screen.primaryHistorySize();
    for (int i = history > kSnapshotHistoryRows ? history - kSnapshotHistoryRows : 0;
         i < history; ++i) {
        const Row* row = screen.primaryHistoryRow(i);
        if (!row)
            continue;
        out += "\x1b[1;1H";
        out += paintHistoryRow(screen, row, cols, default_fg, default_bg);
        out += "\x1b[" + std::to_string(rows) + ";1H";
        out += "\r\n";
    }

    // The primary screen, painted before entering the alternate screen so that
    // entering it saves that screen and its history.
    out += paintGrid(screen, true);

    // The primary region and cursor are set before entering the alternate screen,
    // because entering it saves them along with the grid and history.
    const int primary_top = screen.primaryScrollTop();
    const int primary_bottom = screen.primaryScrollBottom();
    if (primary_top != 0 || primary_bottom != rows - 1)
        out += "\x1b[" + std::to_string(primary_top + 1) + ";" +
               std::to_string(primary_bottom + 1) + "r";
    const int primary_col = std::min(screen.primaryCursorX(), cols - 1);
    out += "\x1b[" + std::to_string(screen.primaryCursorY() + 1) + ";" +
           std::to_string(primary_col + 1) + "H";

    if (screen.inAlternateScreen()) {
        out += "\x1b[?1049h";
        out += paintGrid(screen, false);
    }

    out += modePostscript(screen);

    // The visible cursor goes after the modes, so restoring the region does not
    // home it away.
    const int cursor_col = std::min(screen.cursorX(), cols - 1);
    out += "\x1b[" + std::to_string(screen.cursorY() + 1) + ";" +
           std::to_string(cursor_col + 1) + "H";
    switch (screen.cursorShape()) {
        case Screen::CursorShape::Block:
            out += "\x1b[2 q";
            break;
        case Screen::CursorShape::Bar:
            out += "\x1b[6 q";
            break;
        case Screen::CursorShape::Underline:
            out += "\x1b[4 q";
            break;
    }
    if (screen.cursorVisible())
        out += "\x1b[?25h";
    return out;
}

} // namespace kai
