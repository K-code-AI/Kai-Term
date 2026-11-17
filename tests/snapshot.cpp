// Headless check of screen snapshots: feed a screen, snapshot it, replay the
// snapshot into an empty one, and require the two to agree everywhere.
//
// This is the property that matters for attach. A person joining a running session
// should see what is on it — the same glyphs, the same colours, the same cursor —
// and nothing should scroll or shift on the way in.

#include "../src/session/snapshot.hpp"
#include "../src/terminal/screen.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>

using namespace kai;

namespace {

int checks = 0;
int failures = 0;

void expect(const char* what, bool value) {
    ++checks;
    if (value)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << "\n";
}

// Snapshot into a screen of the same size and compare cell by cell. The first
// difference is reported with its coordinates, because "the screens differ" is not
// something anyone can act on.
bool roundTrips(const char* what, Screen& source) {
    Screen copy(source.rows(), source.cols());
    copy.feed(screenSnapshot(source));
    ++checks;
    if (copy.historySize() != 0) {
        ++failures;
        std::cerr << "FAIL: " << what << ": the snapshot scrolled the new screen, so "
                  << "history would arrive on top of history\n";
        return false;
    }
    for (int row = 0; row < source.rows(); ++row) {
        for (int col = 0; col < source.cols(); ++col) {
            const Cell& a = source.at(row, col);
            const Cell& b = copy.at(row, col);
            if (a.text == b.text && a.width == b.width && a.attr == b.attr && a.fg == b.fg &&
                a.bg == b.bg)
                continue;
            ++failures;
            std::cerr << "FAIL: " << what << ": cell " << row << "," << col << " ["
                      << a.text << "] w" << a.width << " fg" << (int)a.fg.r << ","
                      << (int)a.fg.g << "," << (int)a.fg.b << " bg" << (int)b.bg.r << ","
                      << (int)b.bg.g << "," << (int)b.bg.b << " became [" << b.text << "] w"
                      << b.width << " fg" << (int)b.fg.r << "," << (int)b.fg.g << ","
                      << (int)b.fg.b << " bg" << (int)b.bg.r << "," << (int)b.bg.g << ","
                      << (int)b.bg.b << "\n";
            return false;
        }
    }
    ++checks;
    // A source cursor may sit one column past the edge with a pending wrap, which no
    // cursor move can express; the snapshot puts it in the last column instead.
    const int want_x = std::min(source.cursorX(), source.cols() - 1);
    if (copy.cursorX() != want_x || copy.cursorY() != source.cursorY()) {
        ++failures;
        std::cerr << "FAIL: " << what << ": cursor at " << copy.cursorY() << ","
                  << copy.cursorX() << " not " << source.cursorY() << "," << want_x
                  << "\n";
        return false;
    }
    ++checks;
    if (copy.cursorVisible() != source.cursorVisible()) {
        ++failures;
        std::cerr << "FAIL: " << what << ": cursor visibility changed\n";
        return false;
    }
    ++checks;
    if (copy.cursorShape() != source.cursorShape()) {
        ++failures;
        std::cerr << "FAIL: " << what << ": cursor shape changed\n";
        return false;
    }
    ++checks;
    if (!(copy.defaultForeground() == source.defaultForeground()) ||
        !(copy.defaultBackground() == source.defaultBackground())) {
        ++failures;
        std::cerr << "FAIL: " << what << ": the application's colours did not arrive\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    {
        // An empty screen stays empty, and the snapshot of one is small enough to be
        // sent on every attach without hesitation.
        Screen source(6, 20);
        const std::string bytes = screenSnapshot(source);
        expect("an empty screen still produces a snapshot", !bytes.empty());
        expect("and stays under a screen's worth of bytes", bytes.size() < 6 * 20 * 4);
        Screen copy(6, 20);
        copy.feed(bytes);
        expect("replaying it leaves the screen empty", copy.at(0, 0).text.empty());
        roundTrips("empty screen", source);
    }

    {
        Screen source(8, 40);
        source.feed("plain text\r\n");
        source.feed("\x1b[1;4;7m" "bold underline reverse\x1b[0m\r\n");
        source.feed("\x1b[31mred\x1b[0m \x1b[42mgreen background\x1b[0m\r\n");
        source.feed("\x1b[38;2;11;22;33mtrue colour\x1b[0m\r\n");
        source.feed("\x1b[9;53mstruck and overlined\x1b[0m\r\n");
        source.feed("\x1b[8mhidden\x1b[0m\r\n");
        source.feed("\x1b[2mdim\x1b[0m \x1b[3mitalic\x1b[0m\r\n");
        expect("the source screen has content", source.at(0, 0).text == "p");
        roundTrips("text and attributes", source);
    }

    {
        // Double-width glyphs occupy two columns and the second holds no text. A
        // snapshot that wrote the glyph twice, or wrote it into the continuation,
        // shifts the rest of the row and everything below it looks subtly wrong.
        Screen source(4, 40);
        source.feed("CJK: \xe4\xbd\xa0\xe5\xa5\xbd done\r\n");
        source.feed("emoji: \xf0\x9f\x96\xa5 tail\r\n");
        source.feed("\xe2\x94\x82\xe2\x94\x80\xe2\x94\x82 box drawing\r\n");
        expect("the wide glyph really is two columns", source.at(0, 5).width == 2);
        roundTrips("double-width glyphs", source);
    }

    {
        Screen source(5, 30);
        // A gap in a row: written here, then the cursor moved somewhere else.
        source.feed("start");
        source.feed("\x1b[3;10H" "later");
        source.feed("\x1b[5;25H" "end");
        expect("the gap is really blank", source.at(0, 7).text.empty());
        roundTrips("gaps between runs of text", source);
    }

    {
        Screen source(4, 20);
        // Text right against the right edge, where a pending wrap is the difference
        // between a correct screen and one with a character added to the next row.
        for (int i = 0; i < 20; ++i)
            source.feed("x");
        roundTrips("text to the last column", source);
    }

    {
        Screen source(4, 20);
        source.feed("\x1b]10;#123456\x07\x1b]11;#fedcba\x07");
        source.feed("on a recoloured default\r\n");
        roundTrips("the application's own colours", source);
        expect("and they really did change",
               source.defaultBackground().r == 0xfe);
    }

    {
        Screen source(4, 30);
        source.feed("\x1b]4;1;#ff0040\x07\x1b]4;9;#00ff80\x07");
        source.feed("\x1b[31mpalette red\x1b[0m \x1b[94mbright blue\x1b[0m\r\n");
        roundTrips("a recoloured palette", source);
        Screen copy(4, 30);
        copy.feed(screenSnapshot(source));
        expect("the customised entry arrives", copy.paletteCustomised(1));
        expect("with the colour it was given", copy.paletteColor(1).r == 255 &&
                                                  copy.paletteColor(1).g == 0 &&
                                                  copy.paletteColor(1).b == 0x40);
        expect("an entry nobody changed is not claimed", !copy.paletteCustomised(7));
    }

    {
        Screen source(4, 20);
        source.feed("\x1b[6 q" "bar cursor\r\n");
        roundTrips("the cursor shape the application chose", source);
        expect("and it is the bar", source.cursorShape() == Screen::CursorShape::Bar);
    }

    {
        Screen source(4, 20);
        source.feed("tmux hides the cursor while it redraws\x1b[?25l");
        roundTrips("a hidden cursor stays hidden", source);
    }

    {
        // A session scrolled off the top: the snapshot carries what is on screen, and
        // must not push the source's history through the bottom of the new screen.
        Screen source(4, 20);
        for (int i = 0; i < 20; ++i)
            source.feed("line \x1b[32m" + std::to_string(i) + "\x1b[0m\r\n");
        expect("the source has history", source.historySize() > 0);
        roundTrips("a screen with history behind it", source);
    }

    {
        // Re-attaching must not accumulate: the same snapshot replayed twice into one
        // screen still shows the screen, not two copies of it.
        Screen source(5, 25);
        source.feed("first attach\r\nsecond attach\r\n");
        const std::string bytes = screenSnapshot(source);
        Screen copy(5, 25);
        copy.feed(bytes);
        copy.feed(bytes);
        expect("a second snapshot replaces rather than appends",
               copy.at(0, 0).text == "f");
        source.feed("\x1b[H");
        expect("and the screen still matches the source",
               copy.at(0, 1).text == "i" && copy.at(1, 0).text == "s");
    }

    if (failures == 0) {
        std::cout << "PASS: " << checks << " screen snapshot checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " snapshot checks failed\n";
    return 1;
}
