// Who wins a colour: the person's config file or the session's own bytes.
//
// The rule is one sentence — a colour the config named is a LOCK and outranks
// everything the session writes, a colour it did not name is the session's — and
// the whole point of testing it at the `Screen` level, with no Qt and no display,
// is that the rule is arithmetic in the model. The widget is checked elsewhere;
// this is the authority itself, and the specific regressions it prevents: a shell
// writing OSC 10/11 at startup washing a saved theme back to defaults, and a full
// reset (RIS) that a program issues throwing the theme away.

#include "../src/terminal/screen.hpp"

#include <cstdio>
#include <iostream>
#include <optional>
#include <string>

using namespace kai;

namespace {

int checks = 0;
int failures = 0;

void expect(std::string_view what, bool value) {
    ++checks;
    if (value)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << "\n";
}

bool rgb(const Color& c, int r, int g, int b) {
    return c.r == r && c.g == g && c.b == b;
}

} // namespace

int main() {
    // A locked default outranks OSC 10/11; an unlocked one does not.
    {
        Screen s(10, 40);
        s.setDefaults({220, 220, 220}, {30, 30, 30});
        s.lockDefaults(Color{0, 240, 160}, Color{2, 34, 35}); // background = #022223
        s.feed("\x1b]11;rgb:1e/1e/1e\x07\x1b]10;rgb:ff/ff/ff\x07");
        expect("a locked background ignores the shell's OSC 11",
               rgb(s.defaultBackground(), 2, 34, 35));
        expect("and a locked foreground ignores OSC 10",
               rgb(s.defaultForeground(), 0, 240, 160));
    }
    {
        // Only the field that was named is locked; the other stays the session's.
        Screen s(10, 40);
        s.setDefaults({220, 220, 220}, {30, 30, 30});
        s.lockDefaults(std::nullopt, Color{2, 34, 35}); // only the background
        s.feed("\x1b]11;rgb:1e/1e/1e\x07\x1b]10;rgb:ff/00/00\x07");
        expect("a locked background still holds when only it was named",
               rgb(s.defaultBackground(), 2, 34, 35));
        expect("while an unnamed foreground follows the session",
               rgb(s.defaultForeground(), 255, 0, 0));
    }

    // A locked palette entry outranks OSC 4; an unnamed entry does not.
    {
        Screen s(10, 40);
        std::array<std::optional<Color>, 16> locks{};
        locks[1] = Color{0xAF, 0x0F, 0x0F}; // color1 = #af0f0f
        s.lockPalette(locks);
        s.feed("\x1b]4;1;rgb:00/ff/00\x07\x1b]4;2;rgb:00/00/ff\x07");
        expect("a locked palette entry ignores the session's OSC 4",
               rgb(s.paletteColor(1), 0xAF, 0x0F, 0x0F));
        expect("while an unnamed entry the session recoloured is heard",
               rgb(s.paletteColor(2), 0, 0, 255));
    }

    // A program handing its palette recolours back (OSC 104) must not move a locked
    // entry: the config's colour is still the answer, and an entry the config never
    // named goes back to the built-in. This is the "survives what the program does"
    // guarantee, asserted through the one sequence that really does hand colours
    // back — a full reset (RIS) is not implemented, so it would prove nothing.
    {
        Screen s(10, 40);
        std::array<std::optional<Color>, 16> locks{};
        locks[1] = Color{0xAF, 0x0F, 0x0F};
        s.lockPalette(locks);
        s.feed("\x1b]4;1;rgb:00/ff/00\x07\x1b]4;2;rgb:00/ff/00\x07");
        s.feed("\x1b]104\x07"); // the session hands every custom palette entry back
        expect("a locked palette entry survives the session handing its recolours back",
               rgb(s.paletteColor(1), 0xAF, 0x0F, 0x0F));
        expect("while an unnamed recoloured entry goes back to the built-in",
               rgb(s.paletteColor(2), 0, 128, 0)); // built-in green, not the session's
    }

    // Handing the locks back to the session restores the old behaviour exactly.
    {
        Screen s(10, 40);
        s.setDefaults({220, 220, 220}, {30, 30, 30});
        s.lockDefaults(Color{0, 240, 160}, Color{2, 34, 35});
        s.feed("\x1b]11;rgb:1e/1e/1e\x07");
        expect("before clearing, the lock wins", rgb(s.defaultBackground(), 2, 34, 35));
        s.clearColourLocks();
        expect("after clearing, the session's colour is the answer again",
               rgb(s.defaultBackground(), 0x1E, 0x1E, 0x1E));
    }

    // The whole lock set round-trips as one value, which is how the picker preview
    // puts it aside and hands it back untouched on a cancel.
    {
        Screen s(10, 40);
        std::array<std::optional<Color>, 16> locks{};
        locks[1] = Color{0xAF, 0x0F, 0x0F};
        s.lockDefaults(Color{0, 240, 160}, Color{2, 34, 35});
        s.lockPalette(locks);
        const Screen::ColourLocks saved = s.colourLocks();
        s.clearColourLocks();
        s.feed("\x1b]11;rgb:1e/1e/1e\x07\x1b]4;1;rgb:00/ff/00\x07");
        s.setColourLocks(saved);
        expect("a restored lock set is authoritative again",
               rgb(s.defaultBackground(), 2, 34, 35) && rgb(s.paletteColor(1), 0xAF, 0x0F, 0x0F));
    }

    // The write path must use the effective (locked) defaults, not the raw ones.
    // This was the real bug: a session's OSC 10/11 resets the raw defaults, and
    // text written after that wore the session's colours even though the accessors
    // reported the theme — so theming appeared to work on the blank background but
    // not on command output, until opening the picker re-ran setDefaults.
    {
        Screen s(10, 40);
        s.setDefaults({220, 220, 220}, {30, 30, 30});
        s.lockDefaults(Color{0, 240, 160}, Color{2, 34, 35});
        s.feed("\x1b]11;rgb:1e/1e/1e\x07\x1b]10;rgb:ff/ff/ff\x07"); // session repaints defaults
        s.feed("\x1b[0mhi");                                        // reset, then plain text
        expect("plain text written after the session reset its defaults still wears the locked ink",
               rgb(s.at(0, 0).fg, 0, 240, 160));
        expect("and the locked background", rgb(s.at(0, 0).bg, 2, 34, 35));
    }
    {
        // SGR 39 / 49 ("back to the default colour") land on the locked default too,
        // while an explicit colour in between is untouched.
        Screen s(10, 40);
        s.lockDefaults(Color{0, 240, 160}, Color{2, 34, 35});
        s.feed("\x1b[0mX\x1b[31mY\x1b[39mZ"); // X default, Y red, Z back to default
        expect("SGR 39 returns to the locked ink, not the session's", rgb(s.at(0, 2).fg, 0, 240, 160));
        expect("while an explicit red in between stays red", !rgb(s.at(0, 1).fg, 0, 240, 160));
    }
    {
        // A program's full reset (RIS) is the harshest thing a program can do, and
        // it still does not outrank a lock. The reset wrote the built-in
        // 220,220,220 into the CURRENT colour, so everything the shell printed
        // before its next SGR reset — which, at startup, is the first screen — came
        // back un-themed even though every accessor reported the theme.
        Screen s(10, 40);
        s.lockDefaults(Color{0, 240, 160}, Color{2, 34, 35});
        s.feed("\033c");   // RIS (octal: a `\x` escape would swallow the `c`)
        s.feed("hi");     // written with no SGR reset in between
        expect("a full reset does not put the built-in default where a lock is",
               rgb(s.at(0, 0).fg, 0, 240, 160));
        expect("nor the built-in background", rgb(s.at(0, 0).bg, 2, 34, 35));
    }
    {
        // With no lock a reset still means what it always meant: the built-in
        // colours come back. (Written as two literals on purpose — in C++ a `\x`
        // escape swallows every hex digit after it, so `"\x1bchi"` is not RIS at all:
        // followed by "hi", it is U+01BC followed by "hi".)
        Screen s(10, 40);
        s.setDefaults({10, 20, 30}, {40, 50, 60});
        s.feed("\033c" "hi");
        expect("and with nothing locked, a reset really does reset",
               rgb(s.at(0, 0).fg, 220, 220, 220) && rgb(s.at(0, 0).bg, 30, 30, 30));
    }
    {
        // The window's own colours are applied, and then a session's snapshot
        // announces the session's defaults. A rendition sitting on the default
        // follows the default to wherever it MOVES — and under a lock it did not
        // move; the OSC only moved the raw value underneath. Following the raw
        // value is how every cell written after that snapshot came back in the
        // session's colours while every accessor still reported the theme, which is
        // the exact shape of "theming doesn't get applied to command output until
        // the theme menu is opened once" (Kaida, 2026-11-19).
        Screen s(10, 40);
        s.setDefaults({0, 240, 160}, {2, 34, 35}); // what applying a theme does
        s.lockDefaults(Color{0, 240, 160}, Color{2, 34, 35});
        s.feed("\x1b]10;rgb:dc/dc/dc\x07\x1b]11;rgb:1e/1e/1e\x07"); // the snapshot's
        s.feed("hi"); // written with no SGR reset in between
        expect("a session announcing its own defaults does not move locked text",
               rgb(s.at(0, 0).fg, 0, 240, 160));
        expect("nor its background", rgb(s.at(0, 0).bg, 2, 34, 35));
    }
    {
        // With nothing locked, that same OSC 10 still moves the ink of text written
        // straight after it: the follow is what makes an application that sets its
        // colours and then prints get the colours it set.
        Screen s(10, 40);
        s.feed("\x1b]10;rgb:00/ff/00\x07hi");
        expect("with no lock, text written straight after OSC 10 wears the new default",
               rgb(s.at(0, 0).fg, 0, 255, 0));
    }
    {
        // With no lock, written text still follows the session's own default.
        Screen s(10, 40);
        s.feed("\x1b]11;rgb:1e/1e/1e\x07\x1b[0mhi");
        expect("with no lock, written text uses the session's own default",
               rgb(s.at(0, 0).bg, 0x1E, 0x1E, 0x1E));
    }
    {
        // The lock also moves the CURRENT colour when text is sitting on the default,
        // so a shell that writes its prompt before any SGR reset still wears the theme.
        Screen s(10, 40);
        s.lockDefaults(Color{0, 240, 160}, Color{2, 34, 35}); // locked after construction
        s.feed("hi");                                          // no SGR reset first
        expect("text written without a reset follows the lock onto the theme",
               rgb(s.at(0, 0).fg, 0, 240, 160));
    }
    {
        // A default that moves is followed by text already on the default, while an
        // explicit colour the program chose is left where the program put it.
        Screen s(10, 40);
        s.feed("hi");                       // cols 0,1 on the built-in default
        s.setDefaults({0, 0, 0}, {1, 2, 3}); // the default moves
        s.feed("a\x1b[31mR");               // 'a' default (col 2), 'R' explicit red (col 3)
        expect("text written after a moved default follows it", rgb(s.at(0, 2).fg, 0, 0, 0));
        expect("but an explicit colour the program chose is not dragged along",
               rgb(s.at(0, 3).fg, 128, 0, 0));
    }

    if (failures == 0) {
        std::printf("PASS: %d colour-authority checks passed\n", checks);
        return 0;
    }
    std::printf("%d of %d colour-authority checks failed\n", failures, checks);
    return 1;
}
