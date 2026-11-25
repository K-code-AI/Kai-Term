// The settings menu's arithmetic, without a screen.
//
// One sentence is what this file exists to keep: **no sequence of keystrokes,
// clicks or walks past the end leaves a row holding a value its own setting would
// refuse.** That promise matters more here than in an ordinary form because the
// preview is the live window — a bad value is not a save that gets rejected, it
// is the terminal going wrong while somebody is looking at it. So every mutator
// is hammered at its edges, and the padded display is checked too, because a
// cursor over the wrong digit is its own kind of wrong answer.

#include "../src/config/settingspicker.hpp"

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

// The menu the window actually opens, with the ranges the config parser uses. A
// row whose range is wider than the file's is a row that can be driven into a
// value startup would complain about, so the two are checked to agree below.
std::vector<SettingRow> menu() {
    std::vector<SettingRow> rows;
    rows.push_back({"font_size", "font size", SettingKind::Integer, 14, 6, 72});
    rows.push_back({"background_opacity", "background opacity", SettingKind::Percent, 80, 1,
                    100});
    rows.push_back({"foreground_opacity", "foreground opacity", SettingKind::Percent, 100, 1,
                    100});
    rows.push_back({"padding", "padding", SettingKind::Integer, 10, 0, 64});
    rows.push_back({"scrollback", "scrollback", SettingKind::Integer, 1000, 0, 20000});
    rows.push_back({"cursor_style", "cursor style", SettingKind::Choice, 2, 0, 0, 0,
                    {"block", "bar", "underline"}});
    rows.push_back({"confirm_quit", "confirm quit", SettingKind::Choice, 1, 0, 0, 0,
                    {"never", "running", "always"}});
    return rows;
}

const SettingRow& at(const SettingsPicker& picker, const char* key) {
    for (const SettingRow& row : picker.rows()) {
        if (row.key == key)
            return row;
    }
    static const SettingRow missing{};
    return missing;
}

bool goTo(SettingsPicker& picker, const char* key) {
    for (int i = 0; i < static_cast<int>(picker.rows().size()); ++i) {
        if (picker.current().key == key)
            return true;
        picker.step(1);
    }
    return false;
}

} // namespace

int main() {
    // A row that arrives out of range — from a file edited by hand, or a value a
    // future caller gets wrong — is clamped on the way in. The menu never shows a
    // number it could not have produced itself.
    {
        std::vector<SettingRow> rows;
        rows.push_back({"font_size", "", SettingKind::Integer, 9000, 6, 72});
        rows.push_back({"padding", "", SettingKind::Integer, -5, 0, 64});
        rows.push_back({"background_opacity", "", SettingKind::Percent, 400, 1, 100});
        rows.push_back({"confirm_quit", "", SettingKind::Choice, 9, 0, 0, 0,
                        {"never", "running", "always"}});
        const SettingsPicker picker(std::move(rows));
        expect("a font size of nine thousand arrives as the largest font",
               at(picker, "font_size").value == 72);
        expect("a negative padding arrives as no padding", at(picker, "padding").value == 0);
        expect("a percentage of four hundred arrives as fully solid",
               at(picker, "background_opacity").value == 100);
        expect("an answer that is not in the list wraps into the list",
               at(picker, "confirm_quit").value == 0);
    }

    // Values are shown padded to the width they can reach, so the digits do not
    // slide sideways when a number loses a place and the highlighted digit stays
    // over the digit it is editing.
    {
        const SettingsPicker picker(menu());
        expect("a font of 14 shows two digits", settingValueText(at(picker, "font_size")) == "14");
        expect("a scrollback of 1000 shows the width it can grow to",
               settingValueText(at(picker, "scrollback")) == "01000");
        expect("a background of 80 shows 080%",
               settingValueText(at(picker, "background_opacity")) == "080 %");
        expect("a fully solid ink shows 100%",
               settingValueText(at(picker, "foreground_opacity")) == "100 %");
        expect("an answer shows the answer, not a number",
               settingValueText(at(picker, "confirm_quit")) == "running");
        expect("a percentage of five is 005 and not 5",
               settingValueText(SettingRow{"p", "", SettingKind::Percent, 5, 1, 100}) == "005 %");
        // A number row can say what it is counted in, and the unit belongs outside the
        // padded digits: the arrows edit a digit in a column, and a unit that moved the
        // digits would move the caret off the digit being typed.
        // (Kaida, 2026-11-25: "Font size 'Value' px", "Scrollback history length 'Value' Lines".)
        expect("a unit rides after the number it belongs to",
               settingValueText(SettingRow{"f", "", SettingKind::Integer, 18, 6, 72, 2, {},
                                           false, 1, "px"}) == "18 px");
        expect("and the digits keep the width the arrows type into",
               settingValueText(SettingRow{"p", "", SettingKind::Integer, 5, 0, 64, 2, {},
                                           false, 1, "px"}) == "05 px");
    }

    // Typing replaces the digit under the cursor and steps along, and every
    // intermediate value is one the setting accepts.
    {
        SettingsPicker picker(menu());
        goTo(picker, "font_size");
        expect("the first digit is under the cursor to begin with", picker.digit() == 0);
        expect("typing 2 over the tens of 14 gives 24",
               picker.typeDigit('2') && at(picker, "font_size").value == 24);
        expect("and the cursor steps to the next digit", picker.digit() == 1);
        // 04 would be below the smallest font, so the row stops at the smallest
        // font instead of showing a size the window cannot draw.
        picker.setDigit(0);
        expect("typing a zero in the tens stops at the smallest size, not at 4",
               picker.typeDigit('0') && at(picker, "font_size").value == 6);

        goTo(picker, "scrollback");
        // Five digits for a setting that reaches twenty thousand: the leading
        // zeros are shown so a click lands on the digit it is aimed at.
        expect("a five-digit row walks five digits",
               picker.setDigit(0) && picker.setDigit(4) && picker.digit() == 4 &&
                   picker.setDigit(99) && picker.digit() == 4);
        expect("typing into the last digit of 01000 moves it",
               picker.typeDigit('7') && at(picker, "scrollback").value == 1007);

        goTo(picker, "background_opacity");
        picker.setDigit(0);
        expect("a percentage cannot be typed past fully solid",
               picker.typeDigit('9') && picker.typeDigit('9') && picker.typeDigit('9') &&
                   at(picker, "background_opacity").value == 100);
        picker.setDigit(0);
        expect("nor down to nothing", picker.typeDigit('0') && picker.typeDigit('0') &&
                                          picker.typeDigit('0') &&
                                          at(picker, "background_opacity").value == 1);
    }

    // A row with a handful of answers has no digits at all: it steps between its
    // answers, and a letter picks one. There is nothing in here that could put a
    // fourth word in `confirm_quit`.
    {
        SettingsPicker picker(menu());
        goTo(picker, "confirm_quit");
        expect("a digit is not an answer: the keystroke is refused", !picker.typeDigit('7'));
        picker.digitStep(1);
        expect("the arrows walk the answers", at(picker, "confirm_quit").value == 2);
        picker.digitStep(1);
        expect("walking off the end wraps to the first answer",
               at(picker, "confirm_quit").value == 0);
        picker.digitStep(-1);
        expect("and walking back the other way wraps to the last",
               at(picker, "confirm_quit").value == 2);
        expect("a letter picks the answer that starts with it",
               picker.typeLetter('n') && at(picker, "confirm_quit").value == 0);
        expect("a letter that starts nothing is refused, not guessed",
               !picker.typeLetter('z') && at(picker, "confirm_quit").value == 0);
    }

    // `-` and `+`: a number stops at its edges rather than wrapping into a value
    // nobody meant, and a short list wraps, because the ends of a list of three
    // are next to each other.
    {
        SettingsPicker picker(menu());
        goTo(picker, "font_size");
        for (int i = 0; i < 200; ++i)
            static_cast<void>(picker.cycle(1));
        expect("and stops dead at the top instead of rolling to the bottom",
               at(picker, "font_size").value == 72 && !picker.cycle(1));
        for (int i = 0; i < 400; ++i)
            static_cast<void>(picker.cycle(-1));
        expect("and at the bottom", at(picker, "font_size").value == 6 && !picker.cycle(-1));

        goTo(picker, "cursor_style");
        const int shape_before = at(picker, "cursor_style").value;
        for (int i = 0; i < 3; ++i) // one turn of a list of three
            static_cast<void>(picker.cycle(1));
        expect("a choice comes back round its own list to what it was",
               at(picker, "cursor_style").value == shape_before &&
                   settingValueText(at(picker, "cursor_style")) == "underline");
    }

    // A click lands on a row and a digit; both clamp to what exists, so a press
    // below the last line picks the last line rather than nothing at all.
    {
        SettingsPicker picker(menu());
        expect("a click past the bottom lands on the last row",
               picker.setRow(999) && picker.row() == 6);
        expect("and one above the top on the first", picker.setRow(-40) && picker.row() == 0);
        goTo(picker, "padding");
        expect("a click past the end of a value lands on its last digit",
               picker.setDigit(99) && picker.digit() == 1);
        expect("a click on a choice's text has no digit to put a cursor on",
               goTo(picker, "confirm_quit") && !picker.setDigit(2));
    }

    // Only what moved is worth writing back, and a value put back the way it was
    // is not a change at all.
    {
        SettingsPicker picker(menu());
        expect("an untouched menu reports no changes", picker.changes().empty());
        goTo(picker, "padding");
        static_cast<void>(picker.cycle(1));
        expect("one nudged row is one change",
               picker.changes().size() == 1 && picker.changes()[0].key == "padding");
        static_cast<void>(picker.cycle(-1));
        expect("and putting it back is no change at all", picker.changes().empty());
    }

    // The promise, hammered: every row, every digit position, every digit, every
    // step — after each one the row still holds a value its own setting accepts.
    {
        SettingsPicker picker(menu());
        bool always_valid = true;
        for (int row = 0; row < static_cast<int>(picker.rows().size()); ++row) {
            picker.setRow(row);
            const SettingRow& shape = picker.current();
            for (int round = 0; round < 3; ++round) {
                for (int digit = 0; digit < shape.digits + 2; ++digit) {
                    picker.setDigit(digit);
                    for (char c = '0'; c <= '9'; ++c) {
                        static_cast<void>(picker.typeDigit(c));
                        static_cast<void>(picker.typeLetter(c));
                        static_cast<void>(picker.cycle(1));
                        static_cast<void>(picker.cycle(-1));
                        picker.digitStep(1);
                        picker.digitStep(-1);
                        const SettingRow& now = picker.current();
                        if (now.kind == SettingKind::Choice) {
                            if (now.value < 0 ||
                                now.value >= static_cast<int>(now.choices.size()))
                                always_valid = false;
                        } else if (now.value < now.minimum || now.value > now.maximum) {
                            always_valid = false;
                        }
                        if (static_cast<int>(settingValueText(now).size()) < now.digits)
                            always_valid = false;
                    }
                }
            }
            picker.setRow(row); // the next row starts from its own values
        }
        expect("a hundred keystrokes per row never leave a row out of range", always_valid);
    }

    if (failures > 0) {
        std::cerr << failures << " of " << checks << " settings picker checks failed\n";
        return 1;
    }
    std::cout << "PASS: " << checks << " settings picker checks passed\n";
    return 0;
}
