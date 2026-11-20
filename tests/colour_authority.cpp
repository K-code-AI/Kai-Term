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

    if (failures == 0) {
        std::printf("PASS: %d colour-authority checks passed\n", checks);
        return 0;
    }
    std::printf("%d of %d colour-authority checks failed\n", failures, checks);
    return 1;
}
