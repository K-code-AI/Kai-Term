#pragma once

// The state of the theme picker, with no painting in it.
//
// The picker is the third shape the terminal draws over its own grid — after
// the question with buttons and the list you walk — and like the other two,
// the decisions go here and the pixels stay in the widget: which setting the
// highlight is on, which digit of its colour the next keystroke will change,
// what the colours are now that somebody has been typing. Split that way
// the whole interaction is testable without a display, which matters more here
// than at a yes/no question, because there is real arithmetic under the keys.
//
// The interaction is deliberately old-fashioned: a list of lines, a highlight
// you walk, and hex typed straight in — the digits land on the screen as you type
// them because the preview is the window itself. No sliders, no named-colour
// list to scroll: a terminal has an honest way to say a colour and it is six
// hex digits. Somebody who wants `tomato` can look up its number; somebody who
// wants *a bit brighter* has the preview to say so.

#include "config.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace kai {

enum class FieldKind { Colour, Percent };

// A percentage row holds an integer 1..100 in `value.r`, and the file holds the
// same thing as a fraction 0..1. One place converts the two, so the picker can
// never write a number its own reader would refuse, and 100 always means exactly
// 1 — fully solid — which is also the answer a window whose file says nothing has.
//
// The floor is 1 rather than 0 on purpose: 0 on the ground row is an invisible
// terminal that still eats your keystrokes, and 0 on the ink row is a terminal you
// cannot read. Both stay reachable by editing the file directly, if somebody truly
// wants them; the picker does not walk you into them one keystroke at a time.
inline int percentFromFraction(double fraction) {
    const double scaled = fraction * 100.0;
    long percent = scaled >= 0 ? static_cast<long>(scaled + 0.5) : 0;
    if (percent < 1)
        percent = 1;
    if (percent > 100)
        percent = 100;
    return static_cast<int>(percent);
}

inline double fractionFromPercent(int percent) {
    if (percent < 1)
        percent = 1;
    if (percent > 100)
        percent = 100;
    return static_cast<double>(percent) / 100.0;
}

// The file's spelling of a percentage: whole tenths lose their trailing zero, so
// 80 is `0.8` and 100 is `1` rather than `0.80` and `1.00`. A config file is
// something you open in an editor, and it should read like the number you typed.
inline std::string fractionText(int percent) {
    const int clamped = percent < 1 ? 1 : (percent > 100 ? 100 : percent);
    if (clamped == 100)
        return "1";
    if (clamped % 10 == 0)
        return "0." + std::to_string(clamped / 10);
    // 5 percent is 0.05, not 0.5 — the digit is in the hundredths place, and
    // writing it in the tenths would save a background twelve times more solid
    // than the number on the screen said.
    if (clamped < 10)
        return "0.0" + std::to_string(clamped);
    return "0." + std::to_string(clamped);
}

// A percentage on screen is always three digits wide: 100, 075, 005. The number
// is walked one digit at a time, and a value that changes width as it is typed
// moves the digits out from under a cursor that stayed where it was — the third
// digit of `5` would be nothing at all, and of `50` the second one. Padded, the
// cursor is always over the digit it says it is, which is the whole claim a
// highlighted digit makes. (Kaida, 2026-11-19: "it's quite hard to have the
// cursor in the right spot when it keep change from a 3 digit to 2 digit to 1
// digit value … keep it at 100% 075% 005% so the cursor is always accurate".)
inline std::string percentText(int percent) {
    const int clamped = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    std::string digits = std::to_string(clamped);
    while (digits.size() < 3)
        digits.insert(digits.begin(), '0');
    return digits;
}

// One line of the picker. The key is the word the file uses; the label is
// the word the person reads. Those are the same word for the five window
// colours — `foreground` is already what it looks like — but the sixteen
// palette entries are `color13` in the file and `bright magenta` on the
// screen, because the file follows the convention every terminal program
// shares while the screen owes the person plain language instead. A field
// with no label is labelled by its key.
//
// A Colour field spells `value` as r,g,b. A Percent field (transparency) uses
// only `value.r`, the integer percent 0..100, because the picker is one list of
// editable rows and a percentage edits like a colour does — digit by digit —
// just over three digits instead of six and in decimal instead of hex.
struct ThemeField {
    std::string key;
    Rgb value;
    std::string label;
    FieldKind kind = FieldKind::Colour;
};

enum class PickerOutcome { Still, Saved, Cancelled };

class ThemePicker {
public:
    // The five settings, in the order the picker lists them — the same order
    // the README example uses, so the file you write by hand and the screen
    // you pick on read the same top to bottom.
    explicit ThemePicker(std::vector<ThemeField> fields) : fields_(std::move(fields)) {
        for (const ThemeField& field : fields_)
            opened_.push_back(field.value);
    }

    // Up/Down walk the settings.
    void step(int delta) {
        if (fields_.empty())
            return;
        const int count = static_cast<int>(fields_.size());
        row_ = (row_ + delta + count) % count;
    }

    // Left/Right walk the digits of the setting under the highlight — six for a
    // colour, three for a percentage.
    void digitStep(int delta) {
        const int count = digitsOnRow();
        digit_ = (digit_ + delta + count) % count;
    }

    // A digit replaces the one under the cursor and the cursor steps along,
    // because a value is typed digit by digit in a row and making somebody press
    // Right between them is a penalty for typing fast. A colour takes hex; a
    // percentage takes decimal and is capped at 100. Returns false for anything
    // the focused row cannot accept — the caller may want to know the keystroke
    // was free.
    bool typeHex(char c) {
        if (fields_.empty())
            return false;
        ThemeField& field = fields_[static_cast<std::size_t>(row_)];
        if (field.kind == FieldKind::Percent)
            return typePercent(c);
        const int nibble = hexValue(c);
        if (nibble < 0)
            return false;
        Rgb& colour = field.value;
        int* byte = digit_ < 2 ? &colour.r : (digit_ < 4 ? &colour.g : &colour.b);
        const int shift = (digit_ % 2 == 0) ? 4 : 0;
        *byte = (*byte & ~(0xf << shift)) | (nibble << shift);
        digitStep(1);
        return true;
    }

    // For the mouse: land on a row, and on a digit of it. Both clamp to what
    // exists, so a click below the last line picks the last line rather than
    // nothing, and a click past the end of a value picks its last digit.
    bool setRow(int index) {
        if (fields_.empty())
            return false;
        row_ = std::clamp(index, 0, static_cast<int>(fields_.size()) - 1);
        digit_ = 0;
        return true;
    }
    bool setDigit(int index) {
        if (fields_.empty())
            return false;
        const int count = digitsOnRow();
        digit_ = std::clamp(index, 0, count - 1);
        return true;
    }

    PickerOutcome confirm() { return outcome_ = PickerOutcome::Saved; }
    PickerOutcome cancel() { return outcome_ = PickerOutcome::Cancelled; }
    PickerOutcome outcome() const { return outcome_; }

    const std::vector<ThemeField>& fields() const { return fields_; }
    int row() const { return row_; }
    int digit() const { return digit_; }

    // The lines worth writing back: a colour picked is not a colour typed, so
    // saving leaves the file exactly as it was except for what changed. A
    // setting that already matched keeps its line, its spacing and its
    // comments untouched.
    std::vector<ThemeField> changes() const {
        std::vector<ThemeField> out;
        for (std::size_t i = 0; i < fields_.size(); ++i)
            if (!same(fields_[i].value, opened_[i]))
                out.push_back(fields_[i]);
        return out;
    }

private:
    static constexpr int kDigits = 6;

    // A colour row has six hex digits; a percentage row has three decimal ones.
    int digitsOnRow() const {
        if (fields_.empty())
            return kDigits;
        return fields_[static_cast<std::size_t>(row_)].kind == FieldKind::Percent ? 3 : kDigits;
    }

    // A percentage is a 1..100 integer held in value.r, edited as three digits
    // (hundreds, tens, ones) so "8", "0" lands on 080 and 100 is the ceiling — the
    // value can never read as more than fully solid, and never as nothing at all.
    bool typePercent(char c) {
        if (c < '0' || c > '9')
            return false;
        int v = fields_[static_cast<std::size_t>(row_)].value.r;
        int d[3] = {v / 100, (v / 10) % 10, v % 10};
        d[digit_] = c - '0';
        int nv = d[0] * 100 + d[1] * 10 + d[2];
        if (nv > 100)
            nv = 100;
        if (nv < 1)
            nv = 1;
        fields_[static_cast<std::size_t>(row_)].value.r = nv;
        digitStep(1);
        return true;
    }

    static int hexValue(char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    static bool same(const Rgb& a, const Rgb& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    }

    std::vector<ThemeField> fields_;
    std::vector<Rgb> opened_;
    int row_ = 0;
    int digit_ = 0;
    PickerOutcome outcome_ = PickerOutcome::Still;
};

} // namespace kai
