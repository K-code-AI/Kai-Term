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
// So the alternate screen and the region go first, and the rest go after the last glyph
// has been placed, which is where an application would have set them anyway.
inline std::string modePrelude(const Screen& screen) {
    std::string out;
    // Entering the alternate screen erases it, so this comes before anything is drawn
    // and the glyphs land where the application is actually drawing them.
    out += screen.inAlternateScreen() ? "\x1b[?1049h" : "\x1b[?1049l";
    // The region is *cleared* here and said for real afterwards, which is the opposite of
    // what this function used to do — and the bug that came from doing it the other way is
    // exactly the size of the region. Replaying a session's region before the glyphs means
    // every cursor address that follows is clamped inside it: a screen's worth of lines
    // lands on the handful of rows the region allows, each overwriting the last, and what
    // a person sees is a window with one character in the corner. A program that leaves a
    // region behind when it exits — vim, less, anything cut short — leaves that behind in
    // the daemon's model, so the region a snapshot replays need not have anything to do
    // with what is on the screen. Origin mode is cleared for the same reason: it makes
    // cursor addressing relative to the region, and the glyphs are written absolutely.
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

inline std::string screenSnapshot(const Screen& screen) {
    const Color default_fg = screen.defaultForeground();
    const Color default_bg = screen.defaultBackground();

    std::string out;
    out += "\x1b" "[?25l"; // the cursor goes last, so it never flickers mid-paint
    out += "\x1b[0m";
    out += modePrelude(screen);
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
    out += modePostscript(screen);
    return out;
}

} // namespace kai
