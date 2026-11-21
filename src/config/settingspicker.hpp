#pragma once

// The settings menu's arithmetic, with no painting in it.
//
// The menu is the same shape as the colour picker — a list of lines, a highlight
// you walk, a value you edit in place, and a preview that IS the window — so it
// borrows the picker's outcome enum and its digit walking, and keeps its own
// decisions here where they can be tested without a display.
//
// The one rule that shapes everything below is that **an invalid value cannot be
// arrived at**. Every row knows the range its setting accepts, or the list of
// answers it accepts, and every mutator — a typed digit, an arrow, a click, a
// walk past the end — is clamped or wrapped back into it. That is a stronger
// promise than refusing to save a bad number, because a preview is live: a
// setting menu that could briefly show a padding of 4000 pixels would spend the
// window's pixels before writing anything down. The same rule is why a choice
// row is a choice row: `confirm_quit` has three answers, and nothing you can do
// to that row puts a fourth word in the file.

#include "themepicker.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace kai {

enum class SettingKind {
    Integer, // a number in a range, edited one digit at a time
    Choice,  // one of a short list, cycled with the arrows
    Percent, // 1..100, shown three digits wide, saved to the file as a fraction
};

// One line of the menu. As in the colour picker, `key` is the word the config
// file uses and `label` the word the person reads; the key is the only one that
// ever reaches the disk.
struct SettingRow {
    std::string key;
    std::string label;
    SettingKind kind = SettingKind::Integer;
    int value = 0;     // a Choice row's value is its index into `choices`
    int minimum = 0;   // inclusive; ignored by a Choice row
    int maximum = 100; // inclusive; ignored by a Choice row
    int digits = 2;    // the width the value is shown at, leading zeros and all
    std::vector<std::string> choices;
    // A row that opens a panel of its own. Enter on it opens that panel instead of doing
    // nothing, which is why the row can also be a choice row you step with the arrows:
    // the `theme` row changes theme one name at a time *and* opens the colour editor.
    // (Kaida, 2026-11-20: "the Theme editor should be an item in the setting menu under
    // Theme. It would make things more coherent".)
    bool opens = false;
};

// Put a row back inside what its setting accepts, and make its display width
// agree with what it can hold: a row that maxes at 100000 shows five digits, so
// typing in it never shifts the number sideways.
inline void clampSetting(SettingRow& row) {
    if (row.kind == SettingKind::Choice) {
        if (row.choices.empty()) {
            row.value = 0;
            row.digits = 1;
            return;
        }
        const int count = static_cast<int>(row.choices.size());
        row.value = ((row.value % count) + count) % count; // wraps from either end
        std::size_t widest = 1;
        for (const std::string& choice : row.choices)
            widest = std::max(widest, choice.size());
        row.digits = static_cast<int>(widest);
        return;
    }
    if (row.maximum < row.minimum)
        row.maximum = row.minimum;
    row.value = std::clamp(row.value, row.minimum, row.maximum);
    // Wide enough for the largest answer, and for the one on screen now, which
    // a caller may have set outside the range before this row was clamped.
    int span = 1;
    for (int limit = std::max(row.maximum, std::abs(row.value)); limit >= 10; limit /= 10)
        ++span;
    row.digits = std::max(row.digits > 0 ? row.digits : span, span);
}

// The value as the menu shows it: padded to the row's width, so the digits do
// not slide left when a 100 becomes a 5 and the highlighted digit stays over the
// digit it is editing.
inline std::string settingValueText(const SettingRow& row) {
    if (row.kind == SettingKind::Choice) {
        if (row.choices.empty())
            return std::string();
        const int count = static_cast<int>(row.choices.size());
        const int index = ((row.value % count) + count) % count;
        return row.choices[static_cast<std::size_t>(index)];
    }
    const std::string number = row.kind == SettingKind::Percent
                                   ? percentText(row.value)
                                   : std::to_string(row.value);
    std::string padded = number;
    while (static_cast<int>(padded.size()) < row.digits)
        padded.insert(padded.begin(), '0');
    return row.kind == SettingKind::Percent ? padded + "%" : padded;
}

class SettingsPicker {
public:
    explicit SettingsPicker(std::vector<SettingRow> rows) : rows_(std::move(rows)) {
        for (SettingRow& row : rows_)
            clampSetting(row);
        opened_ = rows_;
    }

    const std::vector<SettingRow>& rows() const { return rows_; }
    int row() const { return row_; }
    int digit() const { return digit_; }
    const SettingRow& current() const { return rows_[static_cast<std::size_t>(row_)]; }
    bool empty() const { return rows_.empty(); }

    // Walk the list. The highlight wraps rather than stopping at the ends: a
    // menu that will not go back from its last line makes you walk the whole
    // length of it to reach the first.
    void step(int delta) {
        if (rows_.empty())
            return;
        const int count = static_cast<int>(rows_.size());
        int next = row_ + delta;
        if (next < 0)
            next += count;
        if (next >= count)
            next -= count;
        row_ = std::clamp(next, 0, count - 1);
        digit_ = 0;
    }

    // Walking the digits of a number, and walking the answers of a choice: the
    // same two keys mean "move along this row" either way, which is what a
    // person expects when a row has somewhere to go.
    void digitStep(int delta) {
        SettingRow& r = rows_[static_cast<std::size_t>(row_)];
        clampSetting(r);
        if (r.kind == SettingKind::Choice) {
            cycle(delta);
            return;
        }
        const int span = std::max(1, r.digits);
        int next = digit_ + delta;
        if (next < 0)
            next += span;
        if (next >= span)
            next -= span;
        digit_ = std::clamp(next, 0, span - 1);
    }

    // The digit under the cursor takes the keypress and the cursor moves on, so
    // `0`, `7`, `5` reads as 075 in the three places it should. A choice row has
    // no digits, and says so by taking nothing.
    bool typeDigit(char c) {
        if (c < '0' || c > '9')
            return false;
        SettingRow& r = rows_[static_cast<std::size_t>(row_)];
        clampSetting(r);
        if (r.kind == SettingKind::Choice)
            return false;
        const int span = std::max(1, r.digits);
        if (digit_ >= span)
            digit_ = span - 1;
        // Read the number back as the digits it shows, replace one, and let the
        // row's own range have the last word.
        const std::string shown = settingValueText(r);
        std::string digits;
        for (char c2 : shown) {
            if (c2 >= '0' && c2 <= '9')
                digits.push_back(c2);
        }
        if (digit_ >= static_cast<int>(digits.size()))
            return false;
        digits[static_cast<std::size_t>(digit_)] = c;
        int built = 0;
        for (char d : digits)
            built = built * 10 + (d - '0');
        r.value = built;
        clampSetting(r);
        digitStep(1);
        return true;
    }

    // A choice row answers a letter: the first answer that starts with it. This
    // is the whole of what typing on a row like `confirm_quit` does, and it is
    // why a fourth answer cannot be typed into it.
    bool typeLetter(char c) {
        SettingRow& r = rows_[static_cast<std::size_t>(row_)];
        if (r.kind != SettingKind::Choice || r.choices.empty())
            return false;
        const char want = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        for (std::size_t i = 0; i < r.choices.size(); ++i) {
            if (!r.choices[i].empty() && r.choices[i][0] == want) {
                r.value = static_cast<int>(i);
                return true;
            }
        }
        return false;
    }

    // One step along the row's own scale: a number by one, a choice by one
    // answer, wrapping at the ends because the ends of a short list are next to
    // each other.
    bool cycle(int delta) {
        if (delta == 0)
            return false;
        SettingRow& r = rows_[static_cast<std::size_t>(row_)];
        clampSetting(r);
        if (r.kind == SettingKind::Choice) {
            const int count = std::max(1, static_cast<int>(r.choices.size()));
            r.value = ((r.value + (delta > 0 ? 1 : -1)) % count + count) % count;
            return true;
        }
        // A step that would leave the range stops at the edge rather than
        // wrapping: running past 72 into 8 on a font size is a jump nobody
        // meant, while wrapping a list of three answers is how you get back to
        // the one you started on.
        const int moved = r.value + (delta > 0 ? 1 : -1);
        const int clamped = std::clamp(moved, r.minimum, r.maximum);
        if (clamped == r.value)
            return false;
        r.value = clamped;
        return true;
    }

    // For the mouse: land on a row, and on a digit of it, from where the pointer
    // was pressed. Both are clamped to what exists, so a click below the last
    // line picks the last line rather than nothing.
    bool setRow(int index) {
        if (rows_.empty())
            return false;
        row_ = std::clamp(index, 0, static_cast<int>(rows_.size()) - 1);
        digit_ = 0;
        return true;
    }
    bool setDigit(int index) {
        const SettingRow& r = current();
        if (r.kind == SettingKind::Choice)
            return false;
        digit_ = std::clamp(index, 0, std::max(1, r.digits) - 1);
        return true;
    }

    // Replace what one row offers, and un-remember it as a change: for the case where
    // something outside the menu wrote that setting while the menu waited. The menu must
    // then say what the file says, and not write the older answer back over it.
    bool setRowAnswers(const std::string& key, std::vector<std::string> choices, int value) {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].key != key)
                continue;
            rows_[i].choices = std::move(choices);
            rows_[i].value = value;
            clampSetting(rows_[i]);
            opened_[i] = rows_[i]; // the file already says this; the menu owes nothing
            return true;
        }
        return false;
    }

    // The rows whose value moved since the menu opened. A row put back the way
    // it was is not a change, and nothing is written for it.
    std::vector<SettingRow> changes() const {
        std::vector<SettingRow> moved;
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].value != opened_[i].value)
                moved.push_back(rows_[i]);
        }
        return moved;
    }

    PickerOutcome confirm() { return outcome_ = PickerOutcome::Saved; }
    PickerOutcome cancel() { return outcome_ = PickerOutcome::Cancelled; }
    PickerOutcome outcome() const { return outcome_; }

private:
    std::vector<SettingRow> rows_;
    std::vector<SettingRow> opened_;
    int row_ = 0;
    int digit_ = 0;
    PickerOutcome outcome_ = PickerOutcome::Still;
};

} // namespace kai
