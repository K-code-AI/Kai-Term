#include "../src/terminal/screen.hpp"

#include <iostream>
#include <string>

static bool fail(const char* what) {
    std::cerr << "FAIL: " << what << '\n';
    return false;
}

static std::string lineText(const kai::Screen& s, int r) {
    std::string out;
    for (int c = 0; c < s.cols(); ++c) {
        const std::string& t = s.at(r, c).text;
        if (t.empty())
            out += ' ';
        else
            out += t;
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

int main() {
    bool ok = true;

    kai::Screen s(4, 12);

    // Plain text and newline.
    s.feed("hello\r\n");
    ok &= (s.at(0, 0).text == "h");
    ok &= (s.at(0, 1).text == "e");
    ok &= (s.at(0, 2).text == "l");
    ok &= (s.at(0, 3).text == "l");
    ok &= (s.at(0, 4).text == "o");
    if (!ok) fail("plain text");

    // SGR red foreground.
    s.feed("\x1b[31mred\x1b[0m");
    ok &= (s.at(1, 0).text == "r");
    ok &= (s.at(1, 0).fg.r == 128 && s.at(1, 0).fg.g == 0 && s.at(1, 0).fg.b == 0);
    if (!ok) fail("SGR red");

    // Absolute cursor position.
    s.feed("\x1b[3;4Hx");
    ok &= (s.at(2, 3).text == "x");
    if (!ok) fail("cursor position");

    // Wide character occupies two cells.
    s.feed("\x1b[H");
    s.feed("\xf0\x9f\x98\x80");
    ok &= (s.at(0, 0).text == "\xf0\x9f\x98\x80");
    ok &= (s.at(0, 0).width == 2);
    ok &= (s.at(0, 1).text.empty());
    if (!ok) fail("wide character");

    // OSC title is consumed, not printed.
    s.feed("\x1b]0;title\x07hi");
    ok &= (s.at(0, 2).text == "h");
    ok &= (s.at(0, 3).text == "i");
    if (!ok) fail("OSC ignore");

    // Euro sign is one cell of UTF-8.
    s.feed("\x1b[2;2H");
    s.feed("\xe2\x82\xac");
    ok &= (s.at(1, 1).text == "\xe2\x82\xac");
    if (!ok) fail("UTF-8");

    // Scrollback: after four lines, the top line is pushed to history.
    kai::Screen t(3, 5);
    t.feed("a\r\nb\r\nc\r\n");
    ok &= (t.at(0, 0).text == "b");
    ok &= (t.at(1, 0).text == "c");
    if (!ok) fail("scroll");

    // A complete ESC= application-keypad sequence must not consume the
    // ESC of the next sequence (fish emits \e= immediately before \e[K).
    s.clear();
    s.feed("\x1b=\x1b[Kx");
    ok &= (s.at(0, 0).text == "x");
    if (!ok) fail("ESC= / ESC[K boundary");

    // DCS payload must be ignored rather than printed as text.
    s.clear();
    s.feed("\x1bP+q696e646e\x1b\\x");
    ok &= (s.at(0, 0).text == "x");
    if (!ok) fail("DCS ignore");

    // Cursor movement overwrites the character at the moved position.
    s.clear();
    s.feed("abc");
    s.moveCursor(-1, 0);
    s.feed("x");
    ok &= (s.at(0, 2).text == "x");
    if (!ok) fail("cursor move");

    // Alternate screen buffer: enter on ?1049h, clear, and restore on ?1049l.
    s.clear();
    s.feed("before");
    s.feed("\x1b[?1049h");
    ok &= s.at(0, 0).text.empty();
    if (!ok) fail("alternate enter clear");
    s.feed("alt");
    ok &= (s.at(0, 0).text == "a");
    if (!ok) fail("alternate content");
    s.feed("\x1b[?1049l");
    ok &= (s.at(0, 0).text == "b");
    if (!ok) fail("alternate restore");

    // 3J clears the screen and scrollback.
    kai::Screen u(3, 5);
    u.feed("a\r\nb\r\nc\r\n\x1b[3J");
    ok &= u.scrollback().empty();
    ok &= u.at(0, 0).text.empty();
    if (!ok) fail("3J clear");

    // Scroll region + scroll content up (tmux uses ESC[1;20r and ESC[18S).
    s.clear();
    s.feed("a\r\nb\r\nc\r\n");
    s.feed("\x1b[1;3r\x1b[1S");
    ok &= (s.at(0, 0).text == "b");
    ok &= (s.at(1, 0).text == "c");
    ok &= s.at(2, 0).text.empty();
    if (!ok) fail("scroll content up");

    // ---------------------------------------------------------------
    // tmux regression: LF at the bottom of a scroll region must scroll
    // only the region. Outside rows (tmux borders, status line) stay put,
    // and region scrollback must not pollute history.
    {
        bool lok = true;
        kai::Screen v(6, 8);
        v.feed("HEAD\r\n");              // row 0, cursor row 1
        v.feed("\x1b[6;1HSTATUS");       // row 5
        v.feed("\x1b[2;5r");             // region rows 1..4
        lok &= (v.cursorX() == 0 && v.cursorY() == 0); // DECSTBM homes the cursor
        if (!lok) fail("DECSTBM homes cursor");
        v.feed("\x1b[2;1HAAA\r\nBBB\r\nCCC\r\nDDD"); // rows 1..4
        v.feed("\r\n");                  // LF at bottom margin -> region scroll
        lok &= (lineText(v, 0) == "HEAD");
        lok &= (lineText(v, 1) == "BBB");
        lok &= (lineText(v, 2) == "CCC");
        lok &= (lineText(v, 3) == "DDD");
        lok &= (lineText(v, 4) == "");
        lok &= (lineText(v, 5) == "STATUS");
        lok &= (v.cursorY() == 4);
        lok &= v.scrollback().empty();    // region scroll is not history
        if (!lok) fail("region LF scroll isolation");
        ok &= lok;
    }

    // tmux regression: delete/insert line (CSI M / CSI L) inside a region.
    {
        bool lok = true;
        kai::Screen w(5, 4);
        w.feed("1\r\n2\r\n3\r\n4");      // rows 0..3
        w.feed("\x1b[1;1H\x1b[1M");      // DL 1 at row 0
        lok &= (lineText(w, 0) == "2");
        lok &= (lineText(w, 1) == "3");
        lok &= (lineText(w, 2) == "4");
        lok &= (lineText(w, 3) == "");
        lok &= (lineText(w, 4) == "");
        if (!lok) fail("delete line");
        w.feed("\x1b[2;1H\x1b[2L");      // IL 2 at row 1
        lok &= (lineText(w, 0) == "2");
        lok &= (lineText(w, 1) == "");
        lok &= (lineText(w, 2) == "");
        lok &= (lineText(w, 3) == "3");
        lok &= (lineText(w, 4) == "4");
        if (!lok) fail("insert line");
        // IL/DL outside the scroll region must be ignored, like xterm.
        w.feed("\x1b[2;5r\x1b[1;1H\x1b[1M");
        lok &= (lineText(w, 0) == "2");   // row 0 is above the region: untouched
        lok &= (lineText(w, 1) == "");
        if (!lok) fail("DL respects scroll region");
        ok &= lok;
    }

    // ESC M (RI) at the top margin scrolls the region down.
    {
        bool lok = true;
        kai::Screen x(4, 4);
        x.feed("A\r\nB\r\nC\r\nD");      // rows 0..3; region will be rows 0..2
        x.feed("\x1b[1;3r");             // region rows 0..2, homes cursor
        x.feed("\x1bM");                 // RI at top margin: C drops off region bottom
        lok &= (lineText(x, 0) == "");
        lok &= (lineText(x, 1) == "A");
        lok &= (lineText(x, 2) == "B");
        lok &= (lineText(x, 3) == "D");  // below region: untouched
        if (!lok) fail("RI region scroll down");
        ok &= lok;
    }

    // EL1 (CSI 1K) erases through the cursor cell inclusive.
    {
        bool lok = true;
        kai::Screen x(2, 8);
        x.feed("abcde");                 // cursor after 'e' at column 5
        x.feed("\x1b[1K");
        for (int c = 0; c <= 5; ++c)
            lok &= x.at(0, c).text.empty();
        if (!lok) fail("EL1 inclusive erase");
        ok &= lok;
    }

    // Deferred autowrap: reaching the last column does not wrap until the
    // next printable character arrives.
    {
        bool lok = true;
        kai::Screen y(3, 4);
        y.feed("abcd");
        lok &= (y.cursorX() == 4);
        lok &= y.at(1, 0).text.empty();
        y.feed("e");
        lok &= (lineText(y, 0) == "abcd");
        lok &= (lineText(y, 1) == "e");
        lok &= (y.cursorX() == 1);
        if (!lok) fail("deferred autowrap");
        ok &= lok;
    }

    // ICH / DCH on the current line.
    {
        bool lok = true;
        kai::Screen y(3, 4);
        y.feed("abcd\x1b[1;2H\x1b[2@");
        lok &= (lineText(y, 0) == "a  b");
        y.feed("\x1b[3;1Habcd\x1b[3;2H\x1b[2P");
        lok &= (lineText(y, 2) == "ad");
        if (!lok) fail("ICH/DCH");
        ok &= lok;
    }

    // DSR 6n answers with the cursor position; DSR 5n answers terminal OK.
    {
        bool lok = true;
        kai::Screen y(5, 8);
        y.takeResponse();
        y.feed("\x1b[3;5H\x1b[6n");
        lok &= (y.takeResponse() == "\x1b[3;5R");
        y.feed("\x1b[5n");
        lok &= (y.takeResponse() == "\x1b[0n");
        // Pending wrap reports the last column, like xterm.
        y.feed("\x1b[H12345678");        // fill 8 cols, cursor pending at 8
        lok &= (y.takeResponse() == "");  // no query issued
        y.feed("\x1b[6n");
        lok &= (y.takeResponse() == "\x1b[1;8R");
        if (!lok) fail("DSR reports");
        ok &= lok;
    }

    // DA1 answers and private queries (kitty keyboard ESC[?u) produce no reply.
    {
        bool lok = true;
        kai::Screen y(6, 8);
        y.takeResponse();
        y.feed("\x1b[c");
        lok &= (y.takeResponse() == "\x1b[?1;2c");
        y.feed("\x1b[5;5H\x1b[?u");      // must not restore cursor, must not answer
        lok &= (y.cursorY() == 4 && y.cursorX() == 4);
        lok &= (y.takeResponse() == "");
        if (!lok) fail("DA1 / kitty query guard");
        ok &= lok;
    }

    // Private SGR (modifyOtherKeys) must not reset colors.
    {
        bool lok = true;
        kai::Screen y(2, 8);
        y.feed("\x1b[31m\x1b[>4;2mX");
        lok &= (y.at(0, 0).fg.r == 128);
        if (!lok) fail("modifyOtherKeys guard");
        ok &= lok;
    }

    // XTSMGRAPHICS (CSI ? S) must not scroll the screen.
    {
        bool lok = true;
        kai::Screen y(4, 8);
        y.feed("1\r\n2\r\n3");
        y.feed("\x1b[?2048;1;9999;9999S");
        lok &= (lineText(y, 0) == "1");
        lok &= (lineText(y, 1) == "2");
        if (!lok) fail("XTSMGRAPHICS guard");
        ok &= lok;
    }

    // Cursor visibility (DECTCEM) tracks ESC[?25l / ESC[?25h.
    {
        bool lok = true;
        kai::Screen y(2, 8);
        y.feed("\x1b[?25l");
        lok &= !y.cursorVisible();
        y.feed("\x1b[?25h");
        lok &= y.cursorVisible();
        if (!lok) fail("DECTCEM");
        ok &= lok;
    }

    // DECOM: with origin mode, CUP is relative to the scroll region.
    {
        bool lok = true;
        kai::Screen y(5, 8);
        y.feed("\x1b[2;4r");             // region rows 1..3
        y.feed("\x1b[?6h");              // origin mode
        y.feed("\x1b[1;1HX");
        lok &= (y.at(1, 0).text == "X");
        y.feed("\x1b[?6l");
        if (!lok) fail("DECOM origin mode");
        ok &= lok;
    }

    // DECALN (ESC # 8) fills the screen with E; used by tmux to force redraw.
    {
        bool lok = true;
        kai::Screen y(3, 4);
        y.feed("ab\x1b#8");
        lok &= (lineText(y, 0) == "EEEE" && lineText(y, 2) == "EEEE");
        lok &= (y.cursorX() == 0 && y.cursorY() == 0);
        if (!lok) fail("DECALN");
        ok &= lok;
    }

    // RIS (ESC c) resets screen and scroll region.
    {
        bool lok = true;
        kai::Screen y(4, 8);
        y.feed("keep\r\n\x1b[2;4rmore");
        y.feed("\x1b" "c");
        lok &= (lineText(y, 0) == "");
        lok &= (y.cursorX() == 0 && y.cursorY() == 0);
        // Region restored to full screen: overflow scrolls into scrollback.
        y.takeResponse();
        y.feed("A\r\nB\r\nC\r\nD\r\nE");
        lok &= !y.scrollback().empty();
        if (!lok) fail("RIS reset");
        ok &= lok;
    }

    // DEC Special Graphics via ESC ) 0 + SO renders box-drawing glyphs,
    // and SI restores ASCII.
    {
        bool lok = true;
        kai::Screen z(2, 8);
        z.feed("\x1b)0\x0fxqj");         // vertical, horizontal, lower-right
        lok &= (z.at(0, 0).text == "\xe2\x94\x82");
        lok &= (z.at(0, 1).text == "\xe2\x94\x80");
        lok &= (z.at(0, 2).text == "\xe2\x94\x98");
        z.feed("\x0eq");                 // SI: back to ASCII G0
        lok &= (z.at(0, 3).text == "q");
        if (!lok) fail("DEC special graphics SO/SI");
        ok &= lok;
    }

    // ESC D (IND) advances the line without returning to column 0.
    {
        bool lok = true;
        kai::Screen y(3, 8);
        y.feed("ab\x1b" "Dcd");
        lok &= (lineText(y, 0) == "ab");
        lok &= (lineText(y, 1) == "  cd");
        if (!lok) fail("ESC D IND");
        ok &= lok;
    }

    if (ok) {
        std::cout << "PASS\n";
        return 0;
    }
    return 1;
}
