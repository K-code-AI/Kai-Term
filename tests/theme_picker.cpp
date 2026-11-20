// The theme picker's arithmetic, without a screen.
//
// Which row the highlight is on, which digit the next keystroke lands on, what
// a typed hex digit does to a colour, and which colours count as changed since
// the picker opened — all of it is plain state, and all of it is what a person
// actually relies on while choosing. The painting can only be looked at; these
// are the parts that can be wrong quietly, so they are checked loudly here.

#include "../src/config/themepicker.hpp"

#include <iostream>
#include <string>
#include <string_view>

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

std::vector<ThemeField> sample() {
    return {
        {"foreground", Rgb{220, 220, 220}},
        {"background", Rgb{30, 30, 30}},
        {"cursor", Rgb{220, 220, 220}},
        {"selection", Rgb{62, 96, 168}},
        {"accent", Rgb{110, 160, 255}},
    };
}

bool isColour(const Rgb& c, int r, int g, int b) {
    return c.r == r && c.g == g && c.b == b;
}

} // namespace

int main() {
    // Walking: rows wrap around, digits wrap around, and the two cursors do
    // not interfere — you can circle the five settings without your digit
    // position jumping around under you.
    {
        ThemePicker picker(sample());
        expect("opens on the first setting at the first digit",
               picker.row() == 0 && picker.digit() == 0);
        picker.step(-1);
        expect("Up from the top wraps to the bottom", picker.row() == 4);
        picker.step(1);
        expect("Down from the bottom wraps to the top", picker.row() == 0);
        picker.digitStep(-1);
        expect("Left from the first digit wraps to the sixth", picker.digit() == 5);
        picker.digitStep(1);
        expect("and Right wraps back", picker.digit() == 0);
        picker.step(2);
        expect("walking rows leaves the digit where it was",
               picker.row() == 2 && picker.digit() == 0);
    }

    // Typing: a hex digit replaces the one under the cursor and the cursor
    // steps along, because six digits in a row is how a colour is typed.
    {
        ThemePicker picker(sample());
        for (const char c : std::string("ff0000"))
            expect("hex digits are accepted", picker.typeHex(c));
        expect("typing ff0000 paints the focused colour red",
               isColour(picker.fields()[0].value, 255, 0, 0));
        expect("the cursor wrapped along with the sixth digit", picker.digit() == 0);
        expect("and it never moved off the row it was on", picker.row() == 0);
        // The untouched settings are untouched.
        expect("the other settings did not move",
               isColour(picker.fields()[1].value, 30, 30, 30) &&
                   isColour(picker.fields()[4].value, 110, 160, 255));
    }

    // Digits land where they look like they do: the first two belong to red,
    // the middle two to green, the last two to blue, high nibble first.
    {
        ThemePicker picker(sample());
        picker.digitStep(2); // green's high nibble
        picker.typeHex('a');
        expect("the third digit is green's high nibble",
               isColour(picker.fields()[0].value, 220, 0xa0 + 12, 220));
        picker.typeHex('3');
        expect("the fourth is green's low nibble",
               isColour(picker.fields()[0].value, 220, 0xa3, 220));
        picker.digitStep(-3); // back to red's low nibble
        picker.typeHex('0');
        expect("the cursor walks backwards to the digit it says",
               isColour(picker.fields()[0].value, 0xd0, 0xa3, 220));
    }

    // Uppercase hex is hex; anything else is not, and says so.
    {
        ThemePicker picker(sample());
        expect("uppercase hex lands", picker.typeHex('A'));
        expect("and reads as lowercase would",
               isColour(picker.fields()[0].value, 0xac, 220, 220));
        expect("'g' is not hex", !picker.typeHex('g'));
        expect("neither is a space", !picker.typeHex(' '));
        expect("and a refused keystroke changed nothing",
               isColour(picker.fields()[0].value, 0xac, 220, 220));
    }

    // Changes: only what moved comes back, so a save rewrites only those
    // lines. And a colour changed and then typed back to what it was is not a
    // change — the file should not notice that you ever hesitated.
    {
        ThemePicker picker(sample());
        expect("a fresh picker has changed nothing", picker.changes().empty());
        for (const char c : std::string("000000"))
            picker.typeHex(c);
        const auto changed = picker.changes();
        expect("one setting changed is one line to write",
               changed.size() == 1 && changed[0].key == "foreground");
        expect("and it carries the new colour", isColour(changed[0].value, 0, 0, 0));
        picker.step(1);
        picker.typeHex('7');
        expect("a second setting joins the list", picker.changes().size() == 2);
        picker.typeHex('3');
        picker.typeHex('3');
        expect("a second setting edited twice is still one line",
               picker.changes().size() == 2);
        // Walk back to the exact original and it is as if you never touched it.
        ThemePicker second(sample());
        for (const char c : std::string("ff0000"))
            second.typeHex(c);
        expect("changed is changed", second.changes().size() == 1);
        for (const char c : std::string("dcdcdc"))
            second.typeHex(c);
        expect("changed back to the opening colour is not a change at all",
               second.changes().empty());
    }

    // Outcomes: the model is asked once what happened and answers consistently.
    {
        ThemePicker picker(sample());
        expect("still up until told otherwise", picker.outcome() == PickerOutcome::Still);
        expect("confirm says saved", picker.confirm() == PickerOutcome::Saved &&
                                        picker.outcome() == PickerOutcome::Saved);
        ThemePicker other(sample());
        expect("cancel says cancelled", other.cancel() == PickerOutcome::Cancelled);
    }

    // The five settings, in the order the file and the README list them,
    // because the picker's rows and the config's keys are the same five words.
    {
        ThemePicker picker(sample());
        const std::vector<std::string> expected = {"foreground", "background", "cursor",
                                                   "selection", "accent"};
        bool same_order = picker.fields().size() == expected.size();
        for (std::size_t i = 0; same_order && i < expected.size(); ++i)
            same_order = picker.fields()[i].key == expected[i];
        expect("the rows are the config keys, in the documented order", same_order);
    }

    // The full board: five window colours and sixteen palette entries, which
    // is the picker the window actually opens now. The walk must circle all
    // twenty-one, and a colour typed on a palette row must be reported under
    // the file's `colorN` spelling — the label is for reading, the key is
    // for saving, and only the key may ever reach the disk.
    {
        std::vector<ThemeField> board = sample();
        for (int i = 0; i < 16; ++i)
            board.push_back({"color" + std::to_string(i), Rgb{0, 0, 0},
                             "name" + std::to_string(i)});
        ThemePicker picker(std::move(board));
        expect("the board is twenty-one rows", picker.fields().size() == 21);
        for (int i = 0; i < 20; ++i)
            picker.step(1);
        expect("walking down reaches the last palette entry", picker.row() == 20);
        picker.step(1);
        expect("and one more wraps back to the top", picker.row() == 0);
        for (int i = 0; i < 5; ++i)
            picker.step(1);
        expect("five steps land on the first palette entry", picker.row() == 5);
        bool typed = true;
        for (const char c : std::string("ff0000"))
            typed &= picker.typeHex(c);
        expect("palette rows take hex like any other", typed);
        const auto changed = picker.changes();
        expect("and the change is reported under the file's spelling",
               changed.size() == 1 && changed[0].key == "color0" &&
                   isColour(changed[0].value, 255, 0, 0));
        expect("the label rides along for the painting, untouched", // fields()[13] is color8
               picker.fields()[13].label == "name8");
    }

    // Transparency is no longer a row in this picker — it is a setting, with a menu
    // of its own — but the two spellings of a percentage still live here, because
    // both panels and the config file read them through the same helpers.
    {
        // The two spellings of one number, both ways round. The file says 0.8, the
        // picker says 80, and 100 is exactly 1 — so a window that saves without
        // touching these rows writes a value that means no change.
        expect("0.8 is 80 and reads back as 0.8",
               percentFromFraction(0.8) == 80 &&
                   fractionText(percentFromFraction(0.8)) == "0.8");
        expect("1 is 100 and writes as 1, not 1.00",
               percentFromFraction(1.0) == 100 && fractionText(100) == "1");
        expect("0.05 keeps both digits",
               fractionText(percentFromFraction(0.05)) == "0.05");
        expect("a fraction of 0 still reads as the picker's floor of 1",
               percentFromFraction(0.0) == 1);
        expect("and 100 means exactly solid", fractionFromPercent(100) == 1.0);
    }

    // The on-screen spelling of a percentage is always three digits wide, so the
    // cursor stays over the digit it names instead of sliding when the number
    // loses a place. The file keeps its own shorter spelling; this is the row.
    {
        expect("100 reads as 100", percentText(100) == "100");
        expect("75 reads as 075", percentText(75) == "075");
        expect("5 reads as 005, not 5", percentText(5) == "005");
        expect("and all three are the same width",
               percentText(100).size() == percentText(75).size() &&
                   percentText(75).size() == percentText(5).size());
    }

    if (failures > 0) {
        std::cerr << failures << " of " << checks << " theme picker checks failed\n";
        return 1;
    }
    std::cout << "PASS: " << checks << " theme picker checks passed\n";
    return 0;
}
