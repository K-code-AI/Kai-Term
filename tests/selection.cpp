// Headless check of text selection: how a drag normalises, which cells it
// covers, how word expansion behaves, and what text comes out of a selection
// over the live grid or over scrolled-back history.
// Pure model only: no widget, no PTY, no display.

#include "../src/terminal/screen.hpp"
#include "../src/terminal/selection.hpp"
#include "../src/terminal/text.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace kai;

namespace {

int checks = 0;
int failures = 0;

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

void expectFalse(const char* what, bool value) {
    ++checks;
    if (!value)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << ": expected false\n";
}

// Classify a column of a plain string as if it were a row of cells.
CellKind kindOfString(const std::string& line, int col) {
    if (col < 0 || col >= static_cast<int>(line.size()))
        return CellKind::Blank;
    const char c = line[static_cast<std::size_t>(col)];
    if (c == ' ')
        return CellKind::Blank;
    const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_';
    return word ? CellKind::Word : CellKind::Other;
}

// Expand a double-click in `line` at `col` and return the selected substring.
std::string wordAt(const std::string& line, int col, int cols = 80) {
    int start = 0, end = 0;
    expandToWord(cols, col, [&](int c) { return kindOfString(line, c); }, start, end);
    if (end <= start)
        return {};
    return line.substr(start, end - start);
}

// A row of cells holding `text`, as the screen would hold it.
Row makeRow(const std::string& text, int cols) {
    Row row(static_cast<std::size_t>(cols));
    for (std::size_t i = 0; i < text.size() && i < row.size(); ++i) {
        row[i].text = std::string(1, text[i]);
        row[i].width = 1;
    }
    return row;
}

} // namespace

int main() {
    // ---- Normalising a drag --------------------------------------------------
    {
        Selection same{CellPos{2, 5}, CellPos{2, 5}};
        expectTrue("a press in one cell is empty", same.empty());
        expectFalse("an empty selection covers nothing", same.contains(2, 5));

        Selection right{CellPos{2, 5}, CellPos{2, 8}};
        expectTrue("rightward drag is ordered", right.ordered());
        expectTrue("ordered first is the anchor", right.first().row == 2 && right.first().col == 5);
        expectTrue("ordered last is the head", right.last().row == 2 && right.last().col == 8);

        Selection left{CellPos{2, 8}, CellPos{2, 5}};
        expectFalse("leftward drag is not ordered", left.ordered());
        expectTrue("reversed first is the head", left.first().row == 2 && left.first().col == 5);
        expectTrue("reversed last is the anchor", left.last().row == 2 && left.last().col == 8);

        // The head column is exclusive, so the two drags cover the same cells.
        for (int c = 0; c < 12; ++c)
            expectTrue("both drag directions cover the same cell",
                       right.contains(2, c) == left.contains(2, c));
    }

    // ---- Which cells a drag covers ------------------------------------------
    {
        Selection one{CellPos{0, 0}, CellPos{0, 1}};
        expectTrue("one cell wide covers its cell", one.contains(0, 0));
        expectFalse("the head column is not covered", one.contains(0, 1));
        expectFalse("a neighbouring row is not covered", one.contains(1, 0));

        Selection block{CellPos{0, 5}, CellPos{2, 3}};
        expectFalse("before the anchor on the first row", block.contains(0, 4));
        expectTrue("from the anchor to the end of the first row", block.contains(0, 5));
        expectTrue("to the last column of the first row", block.contains(0, 79));
        expectTrue("whole middle row from column zero", block.contains(1, 0));
        expectTrue("whole middle row to its end", block.contains(1, 79));
        expectTrue("last row up to the head column", block.contains(2, 2));
        expectFalse("last row stops before the head column", block.contains(2, 3));
        expectFalse("rows after the selection", block.contains(3, 0));
        expectFalse("rows before the selection", block.contains(-1, 0));

        Selection up{CellPos{2, 3}, CellPos{0, 5}};
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 6; ++c)
                expectTrue("dragging upward covers the same cells",
                           up.contains(r, c) == block.contains(r, c));
    }

    // ---- Surviving a shrink -------------------------------------------------
    {
        Selection big{CellPos{1, 60}, CellPos{9, 70}};
        Selection small = big.clamped(4, 20);
        expectTrue("clamped anchor row", small.anchor.row == 1);
        expectTrue("clamped anchor column", small.anchor.col == 20);
        expectTrue("clamped head row", small.head.row == 3);
        expectTrue("clamped head column", small.head.col == 20);
        expectTrue("clamped selection still covers its start", small.contains(1, 20));
    }

    // ---- Word expansion ------------------------------------------------------
    {
        const std::string line = "cd /home/kaida/src 2>&1";
        expect("double-click inside a word takes the word", wordAt(line, 1), "cd");
        expect("double-click at a word start", wordAt(line, 0), "cd");
        expect("double-click at a word end", wordAt(line, 1), "cd");
        expect("a path is split by its slashes", wordAt(line, 5), "home");
        expect("the last component of a path", wordAt(line, 15), "src");
        expect("a slash is one cell", wordAt(line, 3), "/");
        expect("a redirection sign is one cell", wordAt(line, 20), ">");
        expect("digits are part of a word", wordAt(line, 19), "2");
        expect("blank space selects nothing", wordAt(line, 2), "");
        expect("blank past the end selects nothing", wordAt(line, 23), "");

        expect("underscore joins a word", wordAt("run_test.sh", 3), "run_test");
        expect("word at the right edge", wordAt("abc", 2, 3), "abc");
        expect("click past the last column", wordAt("abc", 3, 3), "");
        expect("click before the first column", wordAt("abc", -1, 3), "");
    }

    // ---- Row text ------------------------------------------------------------
    {
        const Row row = makeRow("abc def", 12);
        expect("whole row", rowText(row, 0, 12), "abc def");
        expect("a range of a row", rowText(row, 4, 7), "def");
        expect("padding is trimmed by default", rowText(row, 0, 12), "abc def");
        expect("padding kept when asked", rowText(row, 0, 12, false), "abc def     ");
        expect("a range of only padding", rowText(row, 8, 12), "");
        expect("a range of only padding, untrimmed", rowText(row, 8, 12, false), "    ");
        expect("range past the end of the row", rowText(row, 6, 40), "f");
        expect("reversed range", rowText(row, 5, 2), "");

        // A double-width glyph occupies two cells; the second is a continuation
        // and must not come out a second time.
        Row wide(6);
        wide[0].text = "\xe6\xbc\xa2"; // 漢
        wide[0].width = 2;
        wide[1].width = 0;
        wide[2].text = "x";
        wide[2].width = 1;
        expect("a wide glyph is not duplicated", rowText(wide, 0, 6), "\xe6\xbc\xa2" "x");
        expect("a range starting on a continuation", rowText(wide, 1, 6), "x");
    }

    // ---- Screen text ---------------------------------------------------------
    {
        Screen s(3, 10);
        s.feed("first\r\nsecond\r\nthird");
        expect("every visible row", screenText(s), "first\nsecond\nthird");
        expect("untrimmed screen text keeps padding", screenText(s, false).substr(0, 10),
               "first     ");
    }

    // ---- Selection text ------------------------------------------------------
    {
        Screen s(4, 10);
        s.feed("alpha\r\nbravo\r\ncharlie\r\ndelta");

        expect("nothing selected gives nothing",
               selectionText(s, Selection{CellPos{0, 0}, CellPos{0, 0}}), "");
        expect("part of one row", selectionText(s, Selection{CellPos{0, 0}, CellPos{0, 5}}),
               "alpha");
        expect("part of a row from an offset",
               selectionText(s, Selection{CellPos{1, 1}, CellPos{1, 4}}), "rav");
        expect("a right-to-left drag gives the same text",
               selectionText(s, Selection{CellPos{1, 4}, CellPos{1, 1}}), "rav");
        expect("across rows keeps line breaks and the row offset",
               selectionText(s, Selection{CellPos{0, 2}, CellPos{2, 4}}),
               "pha\nbravo\nchar");
        expect("a whole screen selection",
               selectionText(s, Selection{CellPos{0, 0}, CellPos{3, 10}}),
               "alpha\nbravo\ncharlie\ndelta");
        // Landing in column 0 of the next row selects nothing there, so no
        // trailing blank line ends up in the clipboard.
        expect("a drag ending at column zero drops the empty row",
               selectionText(s, Selection{CellPos{2, 0}, CellPos{3, 0}}), "charlie");
        expect("trailing blanks are not content",
               selectionText(s, Selection{CellPos{0, 0}, CellPos{0, 10}}), "alpha");
        expect("trailing blanks kept on request",
               selectionText(s, Selection{CellPos{0, 0}, CellPos{0, 10}}, false), "alpha     ");
        expect("a selection over blanks only",
               selectionText(s, Selection{CellPos{0, 5}, CellPos{0, 10}}), "");
    }

    // ---- Selecting scrolled-back history ------------------------------------
    // The widget resolves a display row through a callback, so a selection made
    // while scrolled up reads the retained lines rather than the live grid.
    {
        Screen s(3, 10);
        for (int i = 1; i <= 6; ++i)
            s.feed("line" + std::to_string(i) + "\r\n");
        expectTrue("lines scrolled off into history", s.historySize() >= 3);

        const int offset = 2; // scrolled up two lines
        auto display_row = [&](int r) -> const Row& {
            const int idx = r - offset;
            if (idx < 0) {
                const Row* h = s.historyRow(s.historySize() + idx);
                return h ? *h : s.row(0);
            }
            return s.row(idx);
        };

        const std::string got =
            selectionTextAt(s.cols(), Selection{CellPos{0, 0}, CellPos{2, 10}}, display_row);
        expect("selection over history and live rows", got,
               "line" + std::to_string(6 - offset - 1) + "\n" +
                   "line" + std::to_string(6 - offset) + "\nline5");

        // A history row keeps the width it was written at, which can be shorter
        // than the screen is now; a selection must not read past it.
        std::vector<Row> narrow = {makeRow("old", 3), makeRow("newer", 5)};
        auto pick = [&](int r) -> const Row& { return narrow[static_cast<std::size_t>(r)]; };
        expect("selection across rows of different widths",
               selectionTextAt(5, Selection{CellPos{0, 0}, CellPos{1, 5}}, pick), "old\nnewer");
        expect("selection wider than the row it covers",
               selectionTextAt(8, Selection{CellPos{0, 0}, CellPos{0, 8}}, pick), "old");
    }

    {
        // Panes: a drag belongs to the pane it started in and stops at its edge.
        const PaneRect left{0, 0, 10, 40};
        const PaneRect right{0, 40, 10, 40};
        const PaneRect below{10, 0, 10, 80};

        expectTrue("a pane holds its own first row", left.contains(0, 0));
        expectTrue("and its own last row", left.contains(9, 20));
        expectTrue("but not the row a neighbour starts on", !left.contains(10, 20));
        expectTrue("a pane holds its last column", left.contains(5, 39));
        expectTrue("and not the first column of its neighbour", !left.contains(5, 40));

        expectTrue("a row below is pulled up to the pane's last row",
                   left.clampRow(17) == 9);
        expectTrue("a row above is pulled down", left.clampRow(-4) == 0);
        // Ends are exclusive, so stopping at the boundary is legal and one past it is
        // not: the neighbour's first cell is not ours to select.
        expectTrue("a selection end may sit on the boundary", left.clampEndCol(99) == 40);
        expectTrue("and may not go past it", left.clampEndCol(-3) == 0);

        expectTrue("a drag is the pane it started in, not where it reached",
                   paneForDrag({left, right, below}, CellPos{2, 5}, 20, 80).col == 0);
        expectTrue("including when it started in the right-hand one",
                   paneForDrag({left, right, below}, CellPos{2, 60}, 20, 80).col == 40);
        expectTrue("or the one underneath",
                   paneForDrag({left, right, below}, CellPos{15, 60}, 20, 80).row == 10);

        // Saying nothing about panes, or saying it badly, has to leave a terminal that
        // still selects.
        expectTrue("with no panes the screen is one pane",
                   paneForDrag({}, CellPos{7, 63}, 20, 80).cols == 80);
        expectTrue("a press on a divider between panes still selects",
                   paneForDrag({PaneRect{0, 0, 10, 40}, PaneRect{0, 42, 10, 38}},
                               CellPos{5, 41}, 20, 80)
                           .cols == 80);
        expectTrue("and so does a pane described as holding nothing",
                   paneForDrag({PaneRect{0, 0, 0, 0}}, CellPos{1, 1}, 20, 80).rows == 20);

        {
            // The reason for all of it: copying out of one pane gives that pane's
            // text and nothing of its neighbour's, line by line. Twelve columns wide,
            // so the divider is column six.
            // Twelve columns, so each half is six: "lone" and "rtwo" numbered.
            const std::vector<Row> rows = {makeRow("lone01rtwo01", 12),
                                           makeRow("lone02rtwo02", 12)};
            auto pick = [&](int r) -> const Row& { return rows[static_cast<std::size_t>(r)]; };
            expect("across the whole row both halves come out",
                   selectionTextAt(12, Selection{CellPos{0, 0}, CellPos{1, 12}}, pick),
                   "lone01rtwo01\nlone02rtwo02");

            // The same drag, told it belongs to a pane six columns wide: the row in the
            // middle of it stops at the divider instead of running on.
            const PaneRect half{0, 0, 2, 6};
            expect("and only the pane's own part of it",
                   selectionTextInPane(half, Selection{CellPos{0, 0}, CellPos{1, 6}}, pick),
                   "lone01\nlone02");
            expect("a drag in the right-hand pane gets its own text",
                   selectionTextInPane(PaneRect{0, 6, 2, 6},
                                       Selection{CellPos{0, 6}, CellPos{1, 12}}, pick),
                   "rtwo01\nrtwo02");
        }
    }

    if (failures == 0) {
        std::cout << "PASS: " << checks << " selection and text-extraction checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " selection checks failed\n";
    return 1;
}
