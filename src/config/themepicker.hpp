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

#include <string>
#include <vector>

namespace kai {

// One line of the picker. The key is the word the file uses; the label is
// the word the person reads. Those are the same word for the five window
// colours — `foreground` is already what it looks like — but the sixteen
// palette entries are `color13` in the file and `bright magenta` on the
// screen, because the file follows the convention every terminal program
// shares while the screen owes the person plain language instead. A field
// with no label is labelled by its key.
struct ThemeField {
    std::string key;
    Rgb value;
    std::string label;
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

    // Left/Right walk the six digits of the setting under the highlight.
    void digitStep(int delta) {
        digit_ = (digit_ + delta + kDigits) % kDigits;
    }

    // A hex digit replaces the one under the cursor and the cursor steps
    // along, because a colour is typed as six digits in a row and making
    // somebody press Right between them is a penalty for typing fast.
    // Returns false for anything that is not a hex digit — the caller may
    // want to know the keystroke was free.
    bool typeHex(char c) {
        const int nibble = hexValue(c);
        if (nibble < 0 || fields_.empty())
            return false;
        Rgb& colour = fields_[static_cast<std::size_t>(row_)].value;
        int* byte = digit_ < 2 ? &colour.r : (digit_ < 4 ? &colour.g : &colour.b);
        const int shift = (digit_ % 2 == 0) ? 4 : 0;
        *byte = (*byte & ~(0xf << shift)) | (nibble << shift);
        digitStep(1);
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
