// Headless check of the scrollback model the terminal UI reads from: which
// lines are retained, in what order, how the cap trims them, and what the
// alternate screen does to history. Also covers DECCKM, which decides the
// shape of the cursor-key bytes the encoder emits.

#include "../src/terminal/screen.hpp"

#include <iostream>
#include <string>

namespace {

int checks = 0;
int failures = 0;

std::string rowText(const kai::Row& r) {
    std::string out;
    for (const auto& c : r)
        out += c.text.empty() ? std::string(" ") : c.text;
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

    // ---- A shrinking resize pushes the rows that no longer fit ------------
    {
        kai::Screen s(3, 4);
        s.feed("L1\r\nL2\r\nL3");
        expect("no history before the resize", std::to_string(s.historySize()), "0");

        s.resize(2, 8);
        expectTrue("shrinking the screen creates history", s.historySize() >= 1);
        expect("the row pushed off the top is the oldest line", historyText(s, 0), "L1");
        expectTrue("rows keep the width they were written at",
                   s.historyRow(0)->size() == 4);
        expectTrue("live rows use the new width", s.row(0).size() == 8);
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

    if (failures == 0) {
        std::cout << "PASS: " << checks << " scrollback and cursor-mode checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " scrollback checks failed\n";
    return 1;
}
