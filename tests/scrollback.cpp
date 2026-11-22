// Headless check of the scrollback model the terminal UI reads from: which
// lines are retained, in what order, how the cap trims them, and what the
// alternate screen does to history. Also covers the mode bits the input side
// reads: DECCKM, which decides the shape of the cursor-key bytes, and the
// mouse-reporting modes, which decide whether a mouse event is encoded at all.

#include "../src/terminal/mouseencode.hpp"
#include "../src/terminal/screen.hpp"

#include <iostream>
#include <string>

namespace {

int checks = 0;
int failures = 0;

std::string rowText(const kai::Row& r) {
    std::string out;
    int previous_width = 1;
    for (const auto& c : r) {
        // A double-width glyph reserves the cell after it; that cell is not a
        // space, it is the second half of the glyph before it.
        if (c.text.empty() && c.width == 0 && previous_width == 2)
            continue;
        out += c.text.empty() ? std::string(" ") : c.text;
        previous_width = c.width;
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

std::string screenRowText(const kai::Screen& s, int r) {
    return rowText(s.row(r));
}

std::string historyText(const kai::Screen& s, int i) {
    const kai::Row* row = s.historyRow(i);
    return row ? rowText(*row) : std::string("<none>");
}

void expect(const char* what, const std::string& got, const std::string& want) {
    ++checks;
    if (got == want)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << ": want [" << want << "] got [" << got << "]\n";
}

void expectTrue(const char* what, bool value) {
    ++checks;
    if (value)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << ": expected true\n";
}

} // namespace

int main() {
    // ---- Lines leaving the bottom of a full-screen scroll become history ---
    {
        kai::Screen s(3, 10);
        for (int i = 1; i <= 5; ++i)
            s.feed("L" + std::to_string(i) + "\r\n");

        expectTrue("history grows as lines scroll off", s.historySize() >= 2);
        expect("history is oldest first", historyText(s, 0), "L1");
        expect("history keeps every scrolled line", historyText(s, s.historySize() - 1), "L3");
        expect("live screen keeps the newest lines", screenRowText(s, 0), "L4");
        expect("history count matches scrollback()", std::to_string(s.historySize()),
               std::to_string(static_cast<int>(s.scrollback().size())));

        expectTrue("index below zero is rejected", s.historyRow(-1) == nullptr);
        expectTrue("index at the end is rejected", s.historyRow(s.historySize()) == nullptr);
        expectTrue("index past the end is rejected", s.historyRow(s.historySize() + 5) == nullptr);
    }

    // ---- The alternate screen owns its own lines, not the shell's history --
    {
        kai::Screen s(3, 10);
        for (int i = 1; i <= 5; ++i)
            s.feed("L" + std::to_string(i) + "\r\n");
        const int before = s.historySize();
        expectTrue("history exists before the alternate screen", before > 0);

        s.feed("\x1b[?1049h");
        expectTrue("alternate screen is reported", s.inAlternateScreen());
        expect("alternate screen starts without history", std::to_string(s.historySize()), "0");

        for (int i = 1; i <= 6; ++i)
            s.feed("A" + std::to_string(i) + "\r\n");
        expect("alternate-screen lines are not history", std::to_string(s.historySize()), "0");

        s.resize(2, 10);
        expect("a resize on the alternate screen creates no history",
               std::to_string(s.historySize()), "0");
        s.resize(3, 10);

        s.feed("\x1b" "[?1049l");
        expectTrue("primary screen is restored", !s.inAlternateScreen());
        // A shrink inside the alternate screen moves one saved row into history,
        // so the restored history is at least what it was on entry.
        expectTrue("primary history survives a resize inside the alternate screen",
                   s.historySize() >= before);
        expect("restored history still starts at the oldest line", historyText(s, 0), "L1");
        expectTrue("the saved primary screen comes back with content on it",
                   !screenRowText(s, 0).empty());
    }

    // ---- A program hands the screen back without a scroll region -----------
    {
        // A scroll region is a pair of row numbers belonging to the program that
        // set it, and that program is gone when it hands the screen back. Handing
        // the saved region back as well leaves the shell with margins it never
        // asked for, and once the shell's cursor is below the bottom margin there
        // is nothing left for it to do: index() neither moves the cursor nor
        // scrolls, so the prompt pins itself to one row, every command is written
        // over the last one, and no line reaches the history, so the scrollback
        // stops working. `reset` was the only thing that cleared it. Reported by
        // Kaida on 2026-11-21 after leaving nano, whose own region outlived it.
        kai::Screen s(6, 20);
        s.feed("\x1b[1;4r");   // a region on the shell's screen, left behind by something
        s.feed("\x1b[5;1H");   // the prompt sits below it, where the shell left the cursor
        s.feed("\x1b[?1049h"); // a program takes the screen...
        s.feed("\x1b[1;3r");   // ...sets a region for itself...
        s.feed("\x1b[?1049l"); // ...and gives the screen back

        expect("the shell gets the whole screen back from the top",
               std::to_string(s.scrollRegionTop()), "0");
        expect("the shell gets the whole screen back to the bottom",
               std::to_string(s.scrollRegionBottom()), "5");

        for (int i = 1; i <= 12; ++i)
            s.feed("after nano " + std::to_string(i) + "\r\n");
        expectTrue("the shell scrolls again once the program is gone", s.historySize() > 0);
        expect("the newest line is the last thing on the screen", screenRowText(s, 4),
               "after nano 12");

        // The symptom Kaida described: each new line written over the one before
        // it, because the cursor had nowhere to go.
        s.feed("new prompt");
        expect("the prompt lands on a line of its own", screenRowText(s, 5), "new prompt");
        expect("the line above it is the one before the prompt", screenRowText(s, 4),
               "after nano 12");
    }

    // ---- Explicit clears drop history; ordinary clears do not -------------
    {
        kai::Screen s(3, 10);
        for (int i = 1; i <= 5; ++i)
            s.feed("L" + std::to_string(i) + "\r\n");
        expectTrue("history exists before ESC[3J", s.historySize() > 0);

        s.feed("\x1b[2J");
        expectTrue("ESC[2J leaves history alone", s.historySize() > 0);

        s.feed("\x1b[3J");
        expect("ESC[3J drops history", std::to_string(s.historySize()), "0");

        for (int i = 1; i <= 5; ++i)
            s.feed("M" + std::to_string(i) + "\r\n");
        expectTrue("history rebuilds after a clear", s.historySize() > 0);
        s.feed("\x1b" "c"); // RIS
        expect("hard reset drops history", std::to_string(s.historySize()), "0");
    }

    // ---- A shrinking resize keeps the rows that no longer fit -------------
    {
        kai::Screen s(3, 4);
        s.feed("L1\r\nL2\r\nL3");
        expect("no history before the resize", std::to_string(s.historySize()), "0");

        s.resize(2, 8);
        expectTrue("shrinking the screen creates history", s.historySize() >= 1);
        expect("the row pushed off the top is the oldest line", historyText(s, 0), "L1");
        expectTrue("live rows use the new width", s.row(0).size() == 8);
        if (const kai::Row* h = s.historyRow(0))
            expectTrue("retained rows are re-broken at the new width", h->size() == 8);
    }

    // ---- Reflow: what the terminal broke, the terminal re-breaks ----------
    {
        // Twenty columns of text the terminal had to break, then two lines the
        // application ended itself, on a screen 10 columns wide.
        kai::Screen s(4, 10);
        s.feed("ABCDEFGHIJ0123456789\r\nsecond\r\nthird");
        s.resize(4, 5);
        expect("a wrapped line re-breaks at the new width", screenRowText(s, 0), "56789");
        expect("a line the application ended starts a new line", screenRowText(s, 1), "secon");
        expect("and is not glued to the line above it", screenRowText(s, 2), "d");
        expect("the last line is still the last line", screenRowText(s, 3), "third");
        expect("the part that scrolled off is kept", historyText(s, 0), "ABCDE");
        expect("and keeps its order", historyText(s, 2), "01234");

        // Growing back has to give the text that was written, which is only true
        // if the joins were remembered rather than guessed at.
        s.resize(4, 10);
        expect("the wrapped line rejoins", screenRowText(s, 0), "ABCDEFGHIJ");
        expect("and re-breaks at the old width", screenRowText(s, 1), "0123456789");
        expect("a hard newline survived the round trip", screenRowText(s, 2), "second");
        expect("nothing was lost or duplicated", screenRowText(s, 3), "third");
        expect("with nothing left in history", std::to_string(s.historySize()), "0");
        expect("the cursor stayed on the last line", std::to_string(s.cursorY()), "3");
    }

    // ---- The cursor stays over the text it was over -----------------------
    {
        kai::Screen s(3, 20);
        s.feed("line one\r\nline two\r\nline three");
        const int row_before = s.cursorY();
        s.resize(3, 10);
        expect("the cursor stays on the same screen row", std::to_string(s.cursorY()),
               std::to_string(row_before));
        expect("and the line under it is the one it was on", screenRowText(s, s.cursorY()),
               "line three");
        expect("the row above is still the row above", screenRowText(s, 0), "line one");
    }

    // ---- A double-width glyph is never cut in half ------------------------
    {
        kai::Screen s(3, 10);
        s.feed("123456789\xe6\xbc\xa2next"); // nine columns in, then a wide glyph
        expect("a wide glyph with no room moves to the next row", screenRowText(s, 0),
               "123456789");
        expect("and starts the row below", screenRowText(s, 1), "\xe6\xbc\xa2next");
        s.resize(3, 20);
        expect("reflow puts the wide glyph back after its text", screenRowText(s, 0),
               "123456789\xe6\xbc\xa2next");
        expect("leaving the joined line whole", std::to_string(s.row(1).size()), "20");
    }

    // ---- The shell screen behind an application reflows too --------------
    {
        kai::Screen s(4, 10);
        s.feed("0123456789abcdefghij\r\nprompt$ ");
        s.feed("\x1b[?1049h"); // an application takes the screen
        s.feed("alt output");
        s.resize(4, 5);
        expect("the application screen is left for it to redraw", screenRowText(s, 0), "alt o");
        s.feed("\x1b[?1049l");
        // Nothing may be lost or reordered across a resize that happened while
        // the application had the screen. Where the split falls between history
        // and screen follows from keeping the cursor on its own row.
        expect("the shell text survives the resize intact",
               historyText(s, 0) + historyText(s, 1) + historyText(s, 2) + screenRowText(s, 0) +
                   screenRowText(s, 1) + screenRowText(s, 2),
               "0123456789abcdefghijprompt$");
        expect("the shell cursor stays on its row", std::to_string(s.cursorY()), "2");
        expect("still at the end of the prompt", screenRowText(s, s.cursorY()), "t$");
    }

    // ---- A partial scroll region discards; it is not shell history --------
    {
        kai::Screen s(4, 10);
        s.feed("\x1b[1;2r"); // DECSTBM rows 1-2, as tmux panes use
        for (int i = 1; i <= 6; ++i)
            s.feed("P" + std::to_string(i) + "\r\n");
        expect("partial-region scroll does not create history",
               std::to_string(s.historySize()), "0");
    }

    // ---- The cap trims the oldest lines, keeping order intact -------------
    {
        kai::Screen s(3, 10);
        const int total = 1200;
        for (int i = 1; i <= total; ++i)
            s.feed("N" + std::to_string(i) + "\r\n");

        expect("history is capped at 1000 lines", std::to_string(s.historySize()), "1000");
        // A 3-row screen keeps the newest 2 lines, so N(total-2) scrolled off last.
        const int newest = total - 2;
        expect("newest line is retained", historyText(s, s.historySize() - 1),
               "N" + std::to_string(newest));
        expect("oldest retained line is the 1000th from the end", historyText(s, 0),
               "N" + std::to_string(newest - 999));
    }

    // ---- DECCKM decides the cursor-key shape the encoder uses -------------
    {
        kai::Screen s(4, 10);
        expectTrue("cursor keys start in normal mode", !s.applicationCursorKeys());
        s.feed("\x1b[?1h");
        expectTrue("ESC[?1h enables application cursor keys", s.applicationCursorKeys());
        s.feed("\x1b[?1l");
        expectTrue("ESC[?1l disables it again", !s.applicationCursorKeys());
        s.feed("\x1b" "[?1h\x1b" "c");
        expectTrue("hard reset returns to normal cursor keys", !s.applicationCursorKeys());
    }

    // ---- Mouse reporting modes the encoder reads --------------------------
    {
        kai::Screen s(4, 10);
        expectTrue("mouse reporting starts off", !s.mouseReporting());
        expectTrue("no mode means no reporting", s.mouseMode() == kai::MouseMode::Off);
        expectTrue("coordinates start in the classic form",
                   s.mouseCoord() == kai::MouseCoord::Classic);
        expectTrue("bracketed paste starts off", !s.bracketedPaste());

        s.feed("\x1b[?1000h");
        expectTrue("ESC[?1000h reports clicks", s.mouseMode() == kai::MouseMode::Click);
        expectTrue("click reporting is reporting", s.mouseReporting());
        s.feed("\x1b[?1000l");
        expectTrue("ESC[?1000l stops reporting", !s.mouseReporting());

        // The modes nest: drag tracking still reports clicks, and dropping
        // any-event tracking falls back to whatever lower mode is still set.
        s.feed("\x1b[?1002h");
        expectTrue("ESC[?1002h reports drags", s.mouseMode() == kai::MouseMode::Drag);
        s.feed("\x1b[?1003h");
        expectTrue("ESC[?1003h reports all motion", s.mouseMode() == kai::MouseMode::Any);
        s.feed("\x1b[?1003l");
        expectTrue("dropping any-event falls back to drag",
                   s.mouseMode() == kai::MouseMode::Drag);
        s.feed("\x1b[?1000h\x1b[?1002l");
        expectTrue("drag off with click still set reports clicks",
                   s.mouseMode() == kai::MouseMode::Click);
        s.feed("\x1b[?1000l");

        // The most modern coordinate request wins when several are set.
        s.feed("\x1b[?1015h");
        expectTrue("ESC[?1015h asks for urxvt coordinates",
                   s.mouseCoord() == kai::MouseCoord::Urxvt);
        s.feed("\x1b[?1006h");
        expectTrue("ESC[?1006h outranks urxvt", s.mouseCoord() == kai::MouseCoord::Sgr);
        s.feed("\x1b[?1006l");
        expectTrue("dropping SGR falls back to urxvt",
                   s.mouseCoord() == kai::MouseCoord::Urxvt);
        s.feed("\x1b[?1015l\x1b[?1016h");
        expectTrue("ESC[?1016h asks for hexadecimal SGR",
                   s.mouseCoord() == kai::MouseCoord::SgrHex);
        s.feed("\x1b[?1016l\x1b[?1005h");
        expectTrue("ESC[?1005h asks for UTF-8 coordinates",
                   s.mouseCoord() == kai::MouseCoord::Utf8);
        s.feed("\x1b[?1006h");
        expectTrue("ESC[?1006h outranks UTF-8 coordinates",
                   s.mouseCoord() == kai::MouseCoord::Sgr);
        s.feed("\x1b[?1006l");
        expectTrue("dropping SGR falls back to the UTF-8 form still asked for",
                   s.mouseCoord() == kai::MouseCoord::Utf8);
        s.feed("\x1b[?1005l");
        expectTrue("ESC[?1005l stops asking for UTF-8 coordinates",
                   s.mouseCoord() == kai::MouseCoord::Classic);

        s.feed("\x1b[?2026h");
        expectTrue("ESC[?2026h asks for synchronized output", s.synchronizedOutput());
        s.feed("\x1b[?2026l");
        expectTrue("ESC[?2026l ends the synchronized block", !s.synchronizedOutput());

        s.feed("\x1b[?2004h");
        expectTrue("ESC[?2004h asks for bracketed paste", s.bracketedPaste());
        s.feed("\x1b[?2004l");
        expectTrue("ESC[?2004l stops asking", !s.bracketedPaste());

        // An application that exits without cleaning up must not leave the
        // terminal in a mode the next program did not ask for.
        s.feed("\x1b[?1000h\x1b[?1002h\x1b[?1003h\x1b[?1005h\x1b[?1006h\x1b[?1015h\x1b[?1016h\x1b[?2004h\x1b[?2026h");
        expectTrue("all modes are on before the reset", s.mouseMode() == kai::MouseMode::Any);
        s.feed("\x1b" "c");
        expectTrue("hard reset stops mouse reporting", !s.mouseReporting());
        expectTrue("hard reset restores classic coordinates",
                   s.mouseCoord() == kai::MouseCoord::Classic);
        expectTrue("hard reset stops bracketed paste", !s.bracketedPaste());
        expectTrue("hard reset ends a synchronized block", !s.synchronizedOutput());
    }

    if (failures == 0) {
        std::cout << "PASS: " << checks
                  << " scrollback, cursor-mode and mouse-mode checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " scrollback checks failed\n";
    return 1;
}
