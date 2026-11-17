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

// One CSI parameter list, built from the codes that make up one cell's rendition.
inline std::string rendition(const Cell& cell, const Color& default_fg, const Color& default_bg) {
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
    if (cell.fg != default_fg)
        add("38;2;" + std::to_string(cell.fg.r) + ";" + std::to_string(cell.fg.g) + ";" +
            std::to_string(cell.fg.b));
    if (cell.bg != default_bg)
        add("48;2;" + std::to_string(cell.bg.r) + ";" + std::to_string(cell.bg.g) + ";" +
            std::to_string(cell.bg.b));
    if (codes.empty())
        return "\x1b[0m";
    // Two complete sequences rather than one reset folded into the parameter list:
    // `CSI 0 ESC` relies on a parser cancelling the unfinished sequence, and this
    // terminal's own parser is what has to read it back.
    return "\x1b[0m\x1b[" + codes + "m";
}

inline bool sameRendition(const Cell& a, const Cell& b) {
    return a.attr == b.attr && a.fg == b.fg && a.bg == b.bg;
}

inline std::string screenSnapshot(const Screen& screen) {
    const Color default_fg = screen.defaultForeground();
    const Color default_bg = screen.defaultBackground();

    std::string out;
    out += "\x1b" "[?25l"; // the cursor goes last, so it never flickers mid-paint
    out += "\x1b[0m";
    out += "\x1b[H\x1b[2J";

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

    // The cursor is parked at the start of each row and the row written across,
    // because a screen can hold gaps: a row that was erased on one end, or a wide
    // glyph whose second half sits in a column nothing else occupies.
    Cell current{};
    current.fg = default_fg;
    current.bg = default_bg;
    bool have_current = false;
    for (int row = 0; row < screen.rows(); ++row) {
        out += "\x1b[" + std::to_string(row + 1) + ";1H";
        // Where the next glyph has to land. Blanks are skipped to keep the snapshot
        // small, which means the writer has to say where it is going next rather than
        // assume it follows the last thing it wrote; without this, "start" and "later"
        // on one row arrive as "startlater".
        int next_col = 0;
        for (int col = 0; col < screen.cols(); ++col) {
            const Cell& cell = screen.at(row, col);
            if (cell.text.empty())
                continue; // blank, which is what the erase left there
            if (cell.width == 0) {
                next_col = col + 1; // the second half of a wide glyph, already written
                continue;
            }
            if (col != next_col)
                out += "\x1b[" + std::to_string(row + 1) + ";" + std::to_string(col + 1) + "H";
            if (!have_current || !sameRendition(current, cell)) {
                out += rendition(cell, default_fg, default_bg);
                current = cell;
                have_current = true;
            }
            out += cell.text;
            next_col = col + std::max(1, cell.width);
        }
    }

    // One thing is knowingly not transferred: a cursor sitting *past* the last column
    // with a pending wrap. No cursor move can express that state, and it does not
    // need to be — the glyph is drawn in the last column either way, and the
    // application that prints next moves its own cursor before it prints.
    const int cursor_col = std::min(screen.cursorX(), screen.cols() - 1);
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
        out += "\x1b" "[?25h";
    return out;
}

} // namespace kai
