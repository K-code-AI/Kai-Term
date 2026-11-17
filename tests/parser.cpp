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

    // DA1 answers, and the kitty keyboard query answers too — but does not
    // restore the cursor, which is the trap in sharing a final byte with DECRC.
    {
        bool lok = true;
        kai::Screen y(6, 8);
        y.feed("\x1b[5;5H\x1b[s"); // a saved position to be wrongly restored to
        y.feed("\x1b[1;1H");
        y.takeResponse();
        y.feed("\x1b[c");
        lok &= (y.takeResponse() == "\x1b[?1;2c");
        y.feed("\x1b[?u");
        lok &= (y.cursorY() == 0 && y.cursorX() == 0); // no restore
        lok &= (y.takeResponse() == "\x1b[?0u"); // and an answer, so the app moves on
        if (!lok) fail("DA1 / kitty query");
        ok &= lok;
    }

    // XTWINOPS: the geometry and title queries tmux and friends block on, and
    // the title stack that CSI 22 t and CSI 23 t push and pop.
    {
        bool lok = true;
        kai::Screen y(30, 100);
        y.setCellPixels(9, 18);
        y.takeResponse();
        y.feed("\x1b[18t");
        lok &= (y.takeResponse() == "\x1b[8;30;100t");
        y.feed("\x1b[14t");
        lok &= (y.takeResponse() == "\x1b[4;540;900t");
        y.feed("\x1b[16t");
        lok &= (y.takeResponse() == "\x1b[6;18;9t");
        y.feed("\x1b]0;a title\x07");
        static_cast<void>(y.takeTitle());
        y.feed("\x1b[22;0;0t\x1b]0;while inside\x07");
        lok &= (y.windowTitle() == "while inside");
        y.feed("\x1b[23t");
        lok &= (y.windowTitle() == "a title");
        lok &= (y.takeTitle().value_or("") == "a title");
        y.feed("\x1b[21t");
        lok &= (y.takeResponse() == "\x1b]la title\x07");
        // An empty stack must not invent a title.
        y.feed("\x1b[23t");
        lok &= (y.windowTitle() == "a title");
        if (!lok) fail("XTWINOPS geometry, title, and the title stack");
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

    // A column number past the right edge puts the cursor on the last column.
    // It used to land one past it, which is the position deferred autowrap uses
    // to mean "wrap on the next character" — so the next printable character was
    // written outside the grid and took the process down with it.
    {
        bool lok = true;
        kai::Screen cup(3, 10);
        cup.feed("\x1b[1;99HX"); // CUP far past the right edge, then type
        lok &= (lineText(cup, 0) == "         X");
        lok &= (cup.cursorX() == cup.cols()); // legitimately one-past now
        cup.feed("Y"); // ... and the character after it still wraps
        lok &= (lineText(cup, 1) == "Y");
        if (!lok) fail("CUP past the right edge");
        ok &= lok;
    }
    {
        bool lok = true;
        kai::Screen cuf(3, 10);
        cuf.feed("ab\x1b[10CX"); // CUF running off the end
        lok &= (lineText(cuf, 0) == "ab       X");
        // Running into the right margin with a cursor movement is not the same
        // as being left there by a character: the pending wrap is gone, so the
        // next character overwrites the last column instead of wrapping.
        cuf.feed("\x1b[999CZ");
        lok &= (lineText(cuf, 0) == "ab       Z");
        cuf.feed("W");
        lok &= (lineText(cuf, 1) == "W");
        if (!lok) fail("CUF past the right edge");
        ok &= lok;
    }
    {
        bool lok = true;
        kai::Screen cha(3, 10);
        cha.feed("\x1b[999GZ"); // CHA to a column that does not exist
        lok &= (lineText(cha, 0) == "         Z");
        cha.feed("\x1b[1;1H\x1b[99aY"); // HPR from column 0 by more than a screen
        lok &= (lineText(cha, 0) == "         Y");
        if (!lok) fail("CHA and HPR past the right edge");
        ok &= lok;
    }
    {
        bool lok = true;
        // A cursor saved while one past the last column comes back onto the
        // screen, even after a resize in between made the position impossible.
        kai::Screen saved(3, 10);
        saved.feed("0123456789"); // cursor ends one past the edge, pending wrap
        saved.feed("\x1b[s");
        saved.resize(3, 8);
        saved.feed("\x1b[uQ");
        // One past the last column of the *new* width is fine — that is what
        // filling a line means — but indexing outside the grid is not. Where the
        // reflowed text ends up is the resize test's business; this one is about
        // the character landing somewhere and not outside the screen.
        lok &= (saved.cursorX() <= saved.cols());
        bool found = false;
        for (int r = 0; r < saved.rows(); ++r)
            found |= (lineText(saved, r).find('Q') != std::string::npos);
        lok &= found;
        if (!lok) fail("restore cursor after resize");
        ok &= lok;
    }
    {
        bool lok = true;
        // The arrow-key path is the same clamp; a right arrow at the edge has to
        // stay on the last column.
        kai::Screen arrows(2, 10);
        arrows.feed("0123456789");
        arrows.moveCursor(50, 0);
        lok &= (arrows.cursorX() == arrows.cols() - 1);
        arrows.feed("Q"); // overwrites the last column rather than wrapping
        lok &= (lineText(arrows, 0) == "012345678Q");
        if (!lok) fail("cursor movement clamps to the last column");
        ok &= lok;
    }
    {
        bool lok = true;
        // A position report has to name a column that exists.
        kai::Screen cpr(2, 10);
        cpr.feed("\x1b[1;99H\x1b[6n");
        lok &= (cpr.takeResponse() == "\x1b[1;10R");
        if (!lok) fail("cursor position report after a clamped move");
        ok &= lok;
    }

    // OSC: the commands an application sends to the terminal itself.
    {
        bool lok = true;
        kai::Screen s(4, 20);
        s.feed("\x1b]0;first window\x07");
        lok &= (s.windowTitle() == "first window");
        const auto taken = s.takeTitle();
        lok &= taken.has_value() && *taken == "first window";
        lok &= !s.takeTitle().has_value(); // handed over once, not on every read
        s.feed("\x1b]2;second\x1b\\"); // OSC 2, terminated by ESC backslash
        lok &= (s.windowTitle() == "second");
        s.feed("\x1b]0;par"); // split where a PTY read is likely to split it
        s.feed("tial\x1b\\");
        lok &= (s.windowTitle() == "partial");
        s.feed("\x1b]110;\x07"); // the prompt asking for its default title back
        lok &= (s.windowTitle().empty());
        if (!lok) fail("OSC window title");
        ok &= lok;
    }
    {
        bool lok = true;
        kai::Screen s(4, 20);
        // A command this terminal does not implement is consumed, not drawn.
        s.feed("\x1b]66;whatever\x07X");
        lok &= (s.at(0, 0).text == "X");
        lok &= (s.cursorX() == 1);
        // An application that lost its terminator must not be able to make the
        // terminal eat memory until it is killed.
        s.feed("\x1b]0;");
        for (int i = 0; i < 4000; ++i)
            s.feed(std::string(64, 'x'));
        s.feed("\x07Y");
        lok &= (s.windowTitle().size() <= 65536);
        lok &= (s.at(0, 1).text == "Y");
        if (!lok) fail("OSC is consumed and bounded");
        ok &= lok;
    }
    {
        bool lok = true;
        kai::Screen s(4, 20);
        s.feed("\x1b]52;c;YWJjZA==\x07"); // "abcd"
        auto requests = s.takeClipboardRequests();
        lok &= (requests.size() == 1);
        lok &= (!requests.empty() && requests[0].text == "abcd");
        lok &= (!requests.empty() && !requests[0].primary);
        s.feed("\x1b]52;p;aGVsbG8=\x07"); // "hello", to the selection instead
        requests = s.takeClipboardRequests();
        lok &= (requests.size() == 1 && !requests.empty() && requests[0].primary &&
                requests[0].text == "hello");
        s.feed("\x1b]52;c;YWJj\nZA==\x07"); // wrapped across a line, still decodes
        requests = s.takeClipboardRequests();
        lok &= (!requests.empty() && requests[0].text == "abcd");
        s.feed("\x1b]52;c;\x07"); // no payload: the application wants to read
        requests = s.takeClipboardRequests();
        lok &= (!requests.empty() && requests[0].query);
        static_cast<void>(s.takeResponse());
        s.answerClipboardQuery("abcd");
        lok &= (s.takeResponse() == "\x1b]52;c;YWJjZA==\x07");
        if (!lok) fail("OSC 52 clipboard");
        ok &= lok;
    }
    {
        bool lok = true;
        kai::Screen s(4, 20);
        s.feed("\x1b]7;file://example/home/kaida/My%20Dir\x07");
        lok &= (s.workingDirectory() == "/home/kaida/My Dir");
        if (!lok) fail("OSC 7 working directory");
        ok &= lok;
    }
    {
        bool lok = true;
        kai::Screen s(4, 20);
        s.feed("\x1b]11;rgb:ff/00/00\x07\x1b[2J"); // background red, then clear
        lok &= (s.defaultBackground().r == 255 && s.defaultBackground().g == 0);
        lok &= (s.at(0, 0).bg.r == 255); // cleared cells are the default colour
        s.feed("\x1b]11;?\x07");
        lok &= (s.takeResponse() == "\x1b]11;rgb:ffff/0000/0000\x07");
        s.feed("\x1b]11;\x07"); // hand it back to the terminal's own
        lok &= (s.defaultBackground().r == 30);
        if (!lok) fail("OSC 10/11 default colours");
        ok &= lok;
    }
    {
        bool lok = true;
        kai::Screen s(4, 20);
        s.feed("\x1b]4;1;rgb:00/ff/00\x07"); // recolour red
        lok &= (s.paletteColor(1).g == 255);
        s.feed("\x1b[31mg");
        lok &= (s.at(0, 0).fg.g == 255); // text uses what the palette says now
        s.feed("\x1b]4;1;?\x07");
        lok &= (s.takeResponse() == "\x1b]4;1;rgb:0000/ffff/0000\x07");
        s.feed("\x1b]104;1\x07\x1b[31mh"); // give the entry back
        lok &= (s.at(0, 1).fg.r == 128 && s.at(0, 1).fg.g == 0);
        if (!lok) fail("OSC 4 palette");
        ok &= lok;
    }

    // SGR colours in both spellings, and attributes with a reset of their own.
    {
        bool lok = true;
        kai::Screen s(4, 20);
        s.feed("\x1b[38:2::200:30:40mX"); // truecolour, colon form
        lok &= (s.at(0, 0).fg.r == 200 && s.at(0, 0).fg.g == 30 && s.at(0, 0).fg.b == 40);
        s.feed("\x1b[0m\x1b[38:5:196mY"); // palette colour, colon form
        lok &= (s.at(0, 1).fg.r == 255 && s.at(0, 1).fg.g == 0);
        s.feed("\x1b[0m\x1b[48:5:4mZ"); // and the same for the background
        lok &= (s.at(0, 2).bg.b == 128 && s.at(0, 2).bg.r == 0);
        s.feed("\x1b[0m\x1b[38:2:1:5:6:7mW"); // with a colour-space identifier filled in
        lok &= (s.at(0, 3).fg.r == 5 && s.at(0, 3).fg.g == 6 && s.at(0, 3).fg.b == 7);
        if (!lok) fail("SGR colon sub-parameters");
        ok &= lok;
    }
    {
        bool lok = true;
        kai::Screen s(4, 20);
        s.feed("\x1b[38;2;10;20;30mA"); // the semicolon form, unchanged
        lok &= (s.at(0, 0).fg.r == 10 && s.at(0, 0).fg.b == 30);
        s.feed("\x1b[0m\x1b[31mB"); // red
        lok &= (s.at(0, 1).fg.r == 128);
        // An index outside the palette leaves the colour alone. Inventing a grey
        // for it would look like a deliberate choice.
        s.feed("\x1b[38;5;999mC");
        lok &= (s.at(0, 2).fg.r == 128 && s.at(0, 2).fg.g == 0);
        s.feed("\x1b[38:5:999mD");
        lok &= (s.at(0, 3).fg.r == 128);
        // A truncated specification must not leak into the next parameter: 38;5
        // with nothing after it is not a request for blinking text.
        s.feed("\x1b[0m\x1b[38;5mE");
        lok &= (!s.at(0, 0).attr.blink);
        if (!lok) fail("SGR extended colour range");
        ok &= lok;
    }
    {
        bool lok = true;
        kai::Screen s(4, 20);
        s.feed("\x1b[1m\x1b[21mA"); // 21 is how scripts end a bold run
        lok &= (!s.at(0, 0).attr.bold);
        s.feed("\x1b[8m\x1b[28mB"); // 28 reveals what 8 hid
        lok &= (!s.at(0, 1).attr.hidden);
        s.feed("\x1b[6mC"); // rapid blink, drawn as blink
        lok &= (s.at(0, 2).attr.blink);
        s.feed("\x1b[25mD");
        lok &= (!s.at(0, 3).attr.blink);
        if (!lok) fail("SGR 21, 28 and 6");
        ok &= lok;
    }
    {
        bool lok = true;
        kai::Screen s(4, 20);
        // 39 and 49 mean "the terminal's own colour", which is whatever an OSC 10
        // or 11 made it, not a hardcoded grey.
        s.feed("\x1b]10;rgb:ff/00/00\x07\x1b]11;rgb:00/00/ff\x07");
        s.feed("\x1b[39m\x1b[49mA");
        lok &= (s.at(0, 0).fg.r == 255 && s.at(0, 0).bg.b == 255);
        s.feed("\x1b[0mB");
        lok &= (s.at(0, 1).fg.r == 255); // a reset goes back to those too
        if (!lok) fail("SGR 39/49 follow the OSC colours");
        ok &= lok;
    }
    {
        bool lok = true;
        // Sweeping the screen up with SU loses the same lines a newline would,
        // so they belong in the scrollback rather than being thrown away.
        kai::Screen s(4, 10);
        s.feed("a\r\nb\r\nc\r\nd");
        lok &= (s.historySize() == 0);
        s.feed("\x1b[1S");
        lok &= (s.historySize() == 1);
        lok &= (s.at(0, 0).text == "b");
        if (!lok) fail("SU pushes the scrolled-off line into the scrollback");
        ok &= lok;
    }

    if (ok) {
        std::cout << "PASS\n";
        return 0;
    }
    return 1;
}
