#pragma once
//
// The colour mixer: one colour, edited as six numbers instead of one string.
//
// The colour list is a list of hex values, which is the right shape
// for "change bright magenta to the magenta I used last week" and the wrong
// shape for "make this a bit less purple". Finding that second colour in a
// six-digit field means typing a number and looking at what happened; a mixer
// shows the channels as bars you can push, and a hue you can slide along the
// wheel, so the colour you are hunting for comes into view on the way past it.
//
// The model holds the colour as RGB, because that is what a terminal stores,
// and mirrors it as HSL, because that is how a person describes one. Which of
// the two is authoritative follows the last edit: pushing a bar for saturation
// keeps the hue and lightness the person just set rather than re-deriving them
// from a rounding of the previous frame, which is the difference between a
// slider that stays where you left it and one that drifts under the finger.
//
// As in `settingspicker.hpp`, nothing here can be driven into a value that does
// not exist: every setter clamps to its channel's range, a hue past 359 comes
// back round (it is a wheel), and a lightness past 100 stops at 100 (it is not
// a wheel). The widget paints what this says and a click asks this whether the
// number is allowed, so the mouse cannot reach a colour the keys could not.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "themepicker.hpp"

namespace kai {

// Hue, saturation and lightness, in the units a person reads off a picker:
// degrees, and two percentages.
struct Hsl {
    int h = 0;   // 0..359
    int s = 0;   // 0..100
    int l = 0;   // 0..100
};

// The same three numbers in tenths: 0..3599 degrees and 0..1000 percent.
//
// The mixer shows whole units, because a bar two cells wide cannot mean a tenth
// of a degree to anybody reading it. Storing whole units, though, costs more than
// precision nobody looks at: a hue rounded to a degree is a colour moved by as
// much as six in one channel (measured over every colour, the worst case is
// 248,10,11 coming back as 248,4,0), so re-deriving the wheel from a red bar
// would quietly repaint the colour the person was not editing. Tenths make the
// round trip cost at most one unit of a byte, which is the rounding a byte has
// anyway.
struct Hsl10 {
    int h = 0;  // 0..3599
    int s = 0;  // 0..1000
    int l = 0;  // 0..1000
};

inline Hsl10 rgbToHsl10(const Rgb& c) {
    const double r = c.r / 255.0;
    const double g = c.g / 255.0;
    const double b = c.b / 255.0;
    const double mx = std::max({r, g, b});
    const double mn = std::min({r, g, b});
    const double delta = mx - mn;
    const double lightness = (mx + mn) / 2.0;
    double hue = 0.0;
    double saturation = 0.0;
    if (delta > 0.0) {
        const double denom = 1.0 - std::fabs(mx + mn - 1.0);
        saturation = denom > 0.0 ? delta / denom : 0.0;
        if (mx == r)
            hue = std::fmod((g - b) / delta, 6.0);
        else if (mx == g)
            hue = (b - r) / delta + 2.0;
        else
            hue = (r - g) / delta + 4.0;
        hue *= 60.0;
        if (hue < 0.0)
            hue += 360.0;
    }
    return {static_cast<int>(std::lround(hue * 10.0)),
            static_cast<int>(std::lround(saturation * 1000.0)),
            static_cast<int>(std::lround(lightness * 1000.0))};
}

inline Rgb hsl10ToRgb(const Hsl10& hsl) {
    const double h = std::clamp(hsl.h, 0, 3599) / 3600.0;
    const double s = std::clamp(hsl.s, 0, 1000) / 1000.0;
    const double l = std::clamp(hsl.l, 0, 1000) / 1000.0;
    if (s <= 0.0) {
        const int v = static_cast<int>(std::lround(l * 255.0));
        return {v, v, v};
    }
    const auto channel = [](double p, double q, double t) {
        double tt = t;
        if (tt < 0.0)
            tt += 1.0;
        if (tt > 1.0)
            tt -= 1.0;
        if (tt < 1.0 / 6.0)
            return p + (q - p) * 6.0 * tt;
        if (tt < 1.0 / 2.0)
            return q;
        if (tt < 2.0 / 3.0)
            return p + (q - p) * (2.0 / 3.0 - tt) * 6.0;
        return p;
    };
    const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    const double p = 2.0 * l - q;
    return {static_cast<int>(std::lround(channel(p, q, h + 1.0 / 3.0) * 255.0)),
            static_cast<int>(std::lround(channel(p, q, h) * 255.0)),
            static_cast<int>(std::lround(channel(p, q, h - 1.0 / 3.0) * 255.0))};
}

// Rounding is where a conversion like this is won or lost. Both directions use
// lround rather than a truncating cast, so grey stays grey: a colour with no
// saturation must survive a round trip unchanged, because a person pushing a
// saturation slider back to zero means to be back where they started.
inline Hsl rgbToHsl(const Rgb& c) {
    const Hsl10 precise = rgbToHsl10(c);
    return {static_cast<int>(std::lround(precise.h / 10.0)) % 360,
            static_cast<int>(std::lround(precise.s / 10.0)),
            static_cast<int>(std::lround(precise.l / 10.0))};
}

inline Rgb hslToRgb(const Hsl& hsl) {
    const double h = std::clamp(hsl.h, 0, 359) / 360.0;
    const double s = std::clamp(hsl.s, 0, 100) / 100.0;
    const double l = std::clamp(hsl.l, 0, 100) / 100.0;
    if (s <= 0.0) {
        const int v = static_cast<int>(std::lround(l * 255.0));
        return {v, v, v};
    }
    const auto channel = [](double p, double q, double t) {
        double tt = t;
        if (tt < 0.0)
            tt += 1.0;
        if (tt > 1.0)
            tt -= 1.0;
        if (tt < 1.0 / 6.0)
            return p + (q - p) * 6.0 * tt;
        if (tt < 1.0 / 2.0)
            return q;
        if (tt < 2.0 / 3.0)
            return p + (q - p) * (2.0 / 3.0 - tt) * 6.0;
        return p;
    };
    const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    const double p = 2.0 * l - q;
    return {static_cast<int>(std::lround(channel(p, q, h + 1.0 / 3.0) * 255.0)),
            static_cast<int>(std::lround(channel(p, q, h) * 255.0)),
            static_cast<int>(std::lround(channel(p, q, h - 1.0 / 3.0) * 255.0))};
}

// One editable line of the mixer. `Hex` is a line of its own rather than a
// mirror of the three bars, because some colours arrive as a string somebody
// pasted out of a design file, and typing `#1e1e1e` should be possible without
// working out which of six numbers each pair belongs to.
enum class MixChannel { Red, Green, Blue, Hue, Saturation, Lightness, Hex };

inline constexpr int kMixChannelCount = 7;

// How wide a channel's number gets, which is how many digits it can be typed
// with: 255 is three digits and 100 is three, so the cursor never slides when a
// value loses a place — the same reason the settings menu pads its percentages.
inline int channelDigits(MixChannel c) {
    return c == MixChannel::Hex ? 6 : 3;
}

inline int channelMaximum(MixChannel c) {
    switch (c) {
        case MixChannel::Red: case MixChannel::Green: case MixChannel::Blue: return 255;
        case MixChannel::Hue: return 359;
        case MixChannel::Saturation: case MixChannel::Lightness: return 100;
        case MixChannel::Hex: return 0; // not typed as a number
    }
    return 0;
}

// A hue is a wheel, so it wraps; a saturation is not, so it stops. The
// distinction is the difference between "slide round to the next colour" and
// "the most saturated this colour can be", and a slider that wraps the second
// one teleports you to grey when you were trying to reach full colour.
inline bool channelWraps(MixChannel c) {
    return c == MixChannel::Hue;
}

inline const char* channelKey(MixChannel c) {
    switch (c) {
        case MixChannel::Red: return "red";
        case MixChannel::Green: return "green";
        case MixChannel::Blue: return "blue";
        case MixChannel::Hue: return "hue";
        case MixChannel::Saturation: return "saturation";
        case MixChannel::Lightness: return "lightness";
        case MixChannel::Hex: return "hex";
    }
    return "colour";
}

// The order the rows are painted in, and walked in. The three additive channels
// first because they are what the file stores, then the three a person thinks
// in, then the string that gets pasted around.
inline MixChannel mixChannelAt(int index) {
    static const MixChannel kOrder[kMixChannelCount] = {
        MixChannel::Red,    MixChannel::Green,   MixChannel::Blue,
        MixChannel::Hue,    MixChannel::Saturation, MixChannel::Lightness,
        MixChannel::Hex};
    if (index < 0 || index >= kMixChannelCount)
        return MixChannel::Red;
    return kOrder[index];
}

// Where a bar's filled part ends, as a fraction of the bar. Kept here rather
// than in the widget so the click that sets a value and the paint that shows it
// agree: two answers to "how full is this bar" is how a bar starts lying.
inline double channelFraction(MixChannel c, int value) {
    const int max = channelMaximum(c);
    if (max <= 0)
        return 0.0;
    return std::clamp(value, 0, max) / static_cast<double>(max);
}

// The inverse: a click at this fraction of the bar means this value. The bar is
// only so many pixels wide, so the mapping is deliberately done in fractions
// rather than in pixels — a narrow window gets a short bar, not a coarse one.
inline int channelValueAtFraction(MixChannel c, double fraction) {
    const int max = channelMaximum(c);
    if (max <= 0)
        return 0;
    return static_cast<int>(std::lround(std::clamp(fraction, 0.0, 1.0) * max));
}

class ColorMixer {
public:
    explicit ColorMixer(const Rgb& start)
        : start_(start), rgb_(start), hsl_(rgbToHsl10(start)) {}

    Rgb colour() const { return rgb_; }
    // The wheel as the mixer holds it, and as the rows show it. The precise one
    // is what `colour()` was derived from, so `hsl10ToRgb(mixer.hsl10())` is the
    // same colour as `mixer.colour()` at every moment — the bars and the wheel
    // are two readings of one thing, never two colours.
    Hsl10 hsl10() const { return hsl_; }
    Hsl hsl() const {
        return {static_cast<int>(std::lround(hsl_.h / 10.0)) % 360,
                static_cast<int>(std::lround(hsl_.s / 10.0)),
                static_cast<int>(std::lround(hsl_.l / 10.0))};
    }

    // The row the keys are on. Walking wraps: the mixer is a small panel and a
    // ring is friendlier than a wall, and unlike the settings menu no value here
    // becomes absurd by being at the end of the list.
    int row() const { return row_; }
    MixChannel channel() const { return mixChannelAt(row_); }
    void step(int delta) {
        if (delta == 0)
            return;
        int next = row_ + (delta > 0 ? 1 : -1);
        if (next < 0)
            next = kMixChannelCount - 1;
        if (next >= kMixChannelCount)
            next = 0;
        row_ = next;
        digit_ = 0; // a new row starts at the front of its number
    }
    void setRow(int index) {
        row_ = std::clamp(index, 0, kMixChannelCount - 1);
        digit_ = std::clamp(digit_, 0, channelDigits(channel()) - 1);
    }

    // The value of the current row, or of any row, in that row's own units.
    int value(MixChannel c) const {
        switch (c) {
            case MixChannel::Red: return rgb_.r;
            case MixChannel::Green: return rgb_.g;
            case MixChannel::Blue: return rgb_.b;
            case MixChannel::Hue: return hsl().h;
            case MixChannel::Saturation: return hsl().s;
            case MixChannel::Lightness: return hsl().l;
            case MixChannel::Hex: return 0;
        }
        return 0;
    }
    int value() const { return value(channel()); }

    // One step of `-` or `+`. Hue steps by one degree, which is slow across a
    // wheel but never skips past the colour you were aiming at; the mouse and a
    // typed number are how you cross it quickly.
    //
    // The `hex` row steps too. There is no next colour along a string, but the
    // pair of digits the cursor is sitting on has a next value like any other,
    // and a row that can only be edited by typing is a row the arrows of this
    // program do not reach. (Kaida, 2026-11-20: "everything else should be
    // adjustable with the left right arrows".)
    bool nudge(int delta) {
        if (delta == 0)
            return false;
        const MixChannel c = channel();
        if (c == MixChannel::Hex)
            return nudgeNibble(delta);
        const int max = channelMaximum(c);
        int next = value(c) + (delta > 0 ? 1 : -1);
        if (channelWraps(c)) {
            next = ((next % (max + 1)) + (max + 1)) % (max + 1);
        } else {
            next = std::clamp(next, 0, max);
            if (next == value(c))
                return false; // stopped at the edge rather than wrapping
        }
        apply(c, next);
        return true;
    }

    // The two hex digits under the cursor, one step. They stop at `0` and `f`
    // rather than rolling over, because rolling a digit is how a colour you were
    // nudging turns into a colour you have never seen.
    bool nudgeNibble(int delta) {
        int digits[6] = {rgb_.r >> 4, rgb_.r & 15, rgb_.g >> 4,
                         rgb_.g & 15, rgb_.b >> 4, rgb_.b & 15};
        const std::size_t at = static_cast<std::size_t>(digit_);
        const int next = std::clamp(digits[at] + (delta > 0 ? 1 : -1), 0, 15);
        if (next == digits[at])
            return false;
        digits[at] = next;
        applyRgb(Rgb{(digits[0] << 4) | digits[1], (digits[2] << 4) | digits[3],
                     (digits[4] << 4) | digits[5]});
        return true;
    }

    // Set a channel from a bar the mouse was pushed along, or a click on one.
    bool setFromFraction(double fraction) {
        const MixChannel c = channel();
        if (c == MixChannel::Hex)
            return false;
        return apply(c, channelValueAtFraction(c, fraction));
    }

    // The padded text a row shows, so `005` and `255` line up in the column.
    std::string text(MixChannel c) const {
        if (c == MixChannel::Hex) {
            // Six hex digits with no `#`: the panel writes the hash as part of the
            // row, and a cursor that could land on it would be a cursor editing a
            // character that is not part of the colour.
            const std::string full = colourText(rgb_);
            return full.empty() ? full : full.substr(1);
        }
        return padded(value(c), channelDigits(c));
    }
    std::string text() const { return text(channel()); }

    // Typing a digit replaces the digit the cursor is on, then the cursor moves
    // on — the same gesture as the picker's hex field, so a person who learned
    // one has learned both.
    bool typeDigit(char c) {
        const MixChannel ch = channel();
        if (ch == MixChannel::Hex)
            return typeHex(c);
        if (c < '0' || c > '9')
            return false;
        const int digits = channelDigits(ch);
        const int max = channelMaximum(ch);
        const int v = value(ch);
        std::vector<int> d(static_cast<std::size_t>(digits));
        for (int i = 0; i < digits; ++i)
            d[static_cast<std::size_t>(i)] = (v / static_cast<int>(std::pow(10, digits - 1 - i))) % 10;
        d[static_cast<std::size_t>(digit_)] = c - '0';
        int built = 0;
        for (int x : d)
            built = built * 10 + x;
        // A hue is a wheel, so typing 400 into it is not an error, it is a way
        // round; the others are amounts, and stop at their top.
        apply(ch, channelWraps(ch) ? built % (max + 1) : std::clamp(built, 0, max));
        digitStep(1);
        return true;
    }

    // Six hex digits on the `hex` row, exactly as the picker edits them.
    bool typeHex(char c) {
        const int nibble = hexValue(c);
        if (nibble < 0)
            return false;
        int digits[6] = {rgb_.r >> 4, rgb_.r & 15, rgb_.g >> 4,
                         rgb_.g & 15, rgb_.b >> 4, rgb_.b & 15};
        digits[static_cast<std::size_t>(digit_)] = nibble;
        applyRgb(Rgb{(digits[0] << 4) | digits[1], (digits[2] << 4) | digits[3],
                     (digits[4] << 4) | digits[5]});
        digitStep(1);
        return true;
    }

    int digit() const { return digit_; }
    void digitStep(int delta) {
        const int digits = channelDigits(channel());
        int next = digit_ + (delta > 0 ? 1 : -1);
        if (next < 0)
            next = digits - 1;
        if (next >= digits)
            next = 0;
        digit_ = next;
    }
    void setDigit(int d) {
        digit_ = std::clamp(d, 0, channelDigits(channel()) - 1);
    }

    // The colour the mixer was opened with, so the caller can tell a colour the
    // person moved away from from one they only looked at.
    Rgb startingColour() const { return start_; }
    bool moved() const { return rgb_.r != start_.r || rgb_.g != start_.g || rgb_.b != start_.b; }

private:
    static int hexValue(char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    static std::string padded(int v, int digits) {
        std::string s = std::to_string(std::max(v, 0));
        while (s.size() < static_cast<std::size_t>(digits))
            s = "0" + s;
        return s;
    }

    // Editing an RGB channel re-derives the wheel: the person is describing the
    // colour additively and the wheel should read what the colour now is. At
    // tenth precision that costs nothing — every colour comes back unchanged —
    // so the wheel never repaints the colour underneath it.
    void applyRgb(const Rgb& next) {
        rgb_ = next;
        hsl_ = rgbToHsl10(rgb_);
    }

    // Editing a wheel channel sets that one number in whole units and keeps the
    // other two exactly where the precise reading left them. Re-deriving the
    // wheel from the resulting bytes would round the untouched ones through the
    // conversion, which is how a saturation slider creeps sideways under a
    // finger that is only pushing hue.
    bool apply(MixChannel c, int v) {
        switch (c) {
            case MixChannel::Red: return setRgbChannel(0, v);
            case MixChannel::Green: return setRgbChannel(1, v);
            case MixChannel::Blue: return setRgbChannel(2, v);
            case MixChannel::Hue: return setHsl(0, v);
            case MixChannel::Saturation: return setHsl(1, v);
            case MixChannel::Lightness: return setHsl(2, v);
            case MixChannel::Hex: return false;
        }
        return false;
    }

    bool setRgbChannel(int index, int v) {
        v = std::clamp(v, 0, 255);
        Rgb next = rgb_;
        if (index == 0)
            next.r = v;
        else if (index == 1)
            next.g = v;
        else
            next.b = v;
        if (next.r == rgb_.r && next.g == rgb_.g && next.b == rgb_.b)
            return false;
        applyRgb(next);
        return true;
    }

    bool setHsl(int index, int v) {
        Hsl10 next = hsl_;
        if (index == 0)
            next.h = std::clamp(v, 0, 359) * 10;
        else if (index == 1)
            next.s = std::clamp(v, 0, 100) * 10;
        else
            next.l = std::clamp(v, 0, 100) * 10;
        if (next.h == hsl_.h && next.s == hsl_.s && next.l == hsl_.l)
            return false;
        hsl_ = next;
        rgb_ = hsl10ToRgb(hsl_);
        return true;
    }

    Rgb start_;
    Rgb rgb_;
    Hsl10 hsl_;
    int row_ = 0;
    int digit_ = 0;
};

}  // namespace kai
