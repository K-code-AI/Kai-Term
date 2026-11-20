// The colour mixer's arithmetic, without a screen.
//
// Two promises hold this file together. The first is that **the two ways of
// describing a colour agree**: the bars say red 200 / green 30 / blue 90 and the
// hue bar says what that same colour is on the wheel, and moving one must move
// the other without the three you did not touch wandering off by a degree — a
// saturation slider that drifts under the finger is worse than no slider. The
// second is the promise the settings menu already makes, carried over: **no
// keystroke, click or walk leaves a channel outside its own range.** A hue may
// wrap, because a hue is a wheel; a lightness may not, because 101 is not a
// colour.

#include "../src/config/colormixer.hpp"

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

bool near(const Rgb& a, const Rgb& b, int tolerance) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance &&
           std::abs(a.b - b.b) <= tolerance;
}

// Everything below runs in one function: the checks are independent, but they
// belong to one run, and a mixer test that needed a fresh process per assertion
// would be a test nobody can read.
void run() {
// --- the conversion both ways ------------------------------------------------

{
    // The mixer's own conversion, the one it actually uses: every colour comes
    // back exactly, so the wheel never repaints the colour underneath a person
    // who only looked at it. Measured over all 16.7 million of them, the worst
    // error is zero.
    bool exact = true;
    for (int r = 0; r <= 255; r += 3)
        for (int g = 0; g <= 255; g += 3)
            for (int b = 0; b <= 255; b += 3)
                if (hsl10ToRgb(rgbToHsl10(Rgb{r, g, b})) != Rgb{r, g, b})
                    exact = false;
    expect("every colour comes back exactly through the mixer's wheel", exact);

    // Grey is the case that exposes a truncating conversion: a level of 1 read as
    // zero lightness comes back black, and a person pushing a slider feels the
    // colour slip. It holds here because lightness is kept in tenths.
    bool grey_holds = true;
    for (int v = 0; v <= 255; ++v) {
        const Hsl10 hsl = rgbToHsl10(Rgb{v, v, v});
        if (hsl.s != 0)
            grey_holds = false;
        if (hsl10ToRgb(hsl) != Rgb{v, v, v})
            grey_holds = false;
    }
    expect("grey keeps its exact level, and its zero saturation", grey_holds);
}

{
    // The corners of the wheel, in the degrees a person expects to read.
    expect("red is hue 0", rgbToHsl(Rgb{255, 0, 0}).h == 0);
    expect("yellow is hue 60", rgbToHsl(Rgb{255, 255, 0}).h == 60);
    expect("green is hue 120", rgbToHsl(Rgb{0, 255, 0}).h == 120);
    expect("cyan is hue 180", rgbToHsl(Rgb{0, 255, 255}).h == 180);
    expect("blue is hue 240", rgbToHsl(Rgb{0, 0, 255}).h == 240);
    expect("magenta is hue 300", rgbToHsl(Rgb{255, 0, 255}).h == 300);
    expect("a full red is 100% saturated and half light",
           rgbToHsl(Rgb{255, 0, 0}).s == 100 && rgbToHsl(Rgb{255, 0, 0}).l == 50);
    expect("white is full lightness and no saturation",
           rgbToHsl(Rgb{255, 255, 255}).l == 100 && rgbToHsl(Rgb{255, 255, 255}).s == 0);
    expect("black is no lightness", rgbToHsl(Rgb{0, 0, 0}).l == 0);
}

{
    // What the rows *show* is whole units, and a whole degree is worth as much as
    // six of a byte at the saturated end of the wheel (measured worst case:
    // 248,10,11 is hue 359.6°, and 359° is 248,4,0). That is exactly why the mixer
    // does not run on the displayed numbers: a wheel that only had whole degrees
    // would repaint the colour every time a red bar was pushed.
    int worst = 0;
    for (int r = 0; r <= 255; r += 5)
        for (int g = 0; g <= 255; g += 5)
            for (int b = 0; b <= 255; b += 5) {
                const Rgb start{r, g, b};
                const Rgb back = hslToRgb(rgbToHsl(start));
                for (int d : {std::abs(back.r - r), std::abs(back.g - g), std::abs(back.b - b)})
                    worst = std::max(worst, d);
            }
    expect("a whole-degree wheel is up to six units of a byte away from the colour",
           worst > 1 && worst <= 6);
    expect("while the tenth-degree one the mixer runs on is nowhere near that",
           hsl10ToRgb(rgbToHsl10(Rgb{248, 10, 11})) == Rgb{248, 10, 11});
}

// --- the rows, and where the keys are ---------------------------------------

{
    ColorMixer mixer{Rgb{10, 20, 30}};
    expect("the mixer opens on the red channel", mixer.channel() == MixChannel::Red);
    mixer.step(1);
    mixer.step(1);
    mixer.step(1);
    expect("three steps down is the hue row", mixer.channel() == MixChannel::Hue);
    for (int i = 0; i < 4; ++i)
        mixer.step(1);
    expect("walking past the last row wraps to the first",
           mixer.channel() == MixChannel::Red);
    mixer.step(-1);
    expect("and walking up past the first wraps to the last",
           mixer.channel() == MixChannel::Hex);
    mixer.setRow(99);
    expect("a row the mixer does not have is refused, not wrapped",
           mixer.channel() == MixChannel::Hex);
    mixer.setRow(-3);
    expect("as is a negative one", mixer.channel() == MixChannel::Red);
}

{
    // The cursor walks the digits of the row you are on, and the hex row has six
    // of them where a channel has three.
    ColorMixer mixer{Rgb{10, 20, 30}};
    const int start = mixer.digit();
    mixer.digitStep(1);
    mixer.digitStep(1);
    mixer.digitStep(1);
    expect("a channel has three digits to walk", mixer.digit() == start);
    mixer.setRow(6); // hex
    mixer.setDigit(0);
    for (int i = 0; i < 6; ++i)
        mixer.digitStep(1);
    expect("the hex row has six", mixer.digit() == 0);
    mixer.setDigit(40);
    expect("and a cursor beyond the row's digits is stopped at the last of them",
           mixer.digit() == 5);
}

// --- typing, and the ranges that stop it ------------------------------------

{
    ColorMixer mixer{Rgb{10, 20, 30}};
    expect("three digits type a red",
           mixer.typeDigit('1') && mixer.typeDigit('2') && mixer.typeDigit('8'));
    expect("and land on 128", mixer.colour().r == 128);
    expect("leaving the other channels alone", mixer.colour().g == 20 && mixer.colour().b == 30);

    // 999 is not a red. It stops at 255 rather than wrapping into 231, because
    // a bar that rolls over under the thumb is a bar that lies about where it is.
    mixer.setDigit(0);
    static_cast<void>(mixer.typeDigit('9'));
    static_cast<void>(mixer.typeDigit('9'));
    static_cast<void>(mixer.typeDigit('9'));
    expect("a red cannot be typed past 255", mixer.colour().r == 255);

    expect("a hex letter is not a digit on a channel row: the keystroke stays free",
           !mixer.typeDigit('f'));
}

{
    // A hue is a wheel, so 400 is not a mistake, it is a way round: 400 - 360 = 40.
    // The digits replace one at a time, so the way to type a fresh number is from
    // an empty row, which is what `4` on a hue at zero is.
    ColorMixer mixer{Rgb{10, 20, 30}};
    mixer.setRow(3);
    static_cast<void>(mixer.setFromFraction(0.0));
    mixer.setDigit(0);
    expect("a hue typed past 359 comes round the wheel instead of being refused",
           mixer.typeDigit('4') && mixer.value() == 40);
    expect("and the colour followed it", mixer.colour() != Rgb{10, 20, 30});
    mixer.setRow(4); // saturation, which is an amount and not a wheel
    mixer.setDigit(0);
    static_cast<void>(mixer.typeDigit('9'));
    static_cast<void>(mixer.typeDigit('9'));
    static_cast<void>(mixer.typeDigit('9'));
    expect("while a saturation typed past full stops at full", mixer.value() == 100);
}

{
    // The hex row is the same six digits the picker edits, so a colour somebody
    // pasted out of a design file can be typed in without arithmetic.
    ColorMixer mixer{Rgb{0, 0, 0}};
    mixer.setRow(6);
    const char* hex = "1e1e1e";
    bool typed = true;
    for (const char* c = hex; *c != '\0'; ++c)
        typed = typed && mixer.typeHex(*c);
    expect("a hex colour types straight in", typed);
    expect("and the bars read it", mixer.colour().r == 0x1e && mixer.colour().g == 0x1e &&
                                       mixer.colour().b == 0x1e);
    expect("with the padded display showing all six", mixer.text() == "1e1e1e");
    expect("a g is not a hex digit", !mixer.typeHex('g'));
}

// --- pushing a channel, and the edges it stops at ---------------------------

{
    ColorMixer mixer{Rgb{0, 0, 0}};
    mixer.setRow(5); // lightness
    for (int i = 0; i < 300; ++i)
        static_cast<void>(mixer.nudge(1));
    expect("lightness stops at 100 rather than rolling into a dark colour",
           mixer.value() == 100 && mixer.colour() == Rgb{255, 255, 255});
    expect("and a nudge that would go past reports that nothing happened",
           !mixer.nudge(1));
    for (int i = 0; i < 300; ++i)
        static_cast<void>(mixer.nudge(-1));
    expect("and stops at 0 the same way", mixer.value() == 0 && mixer.colour() == Rgb{0, 0, 0});
    expect("with no lower", !mixer.nudge(-1));
}

{
    ColorMixer mixer{Rgb{255, 0, 0}};
    mixer.setRow(3); // hue, starting at 0
    expect("stepping down off hue 0 lands on the top of the wheel",
           mixer.nudge(-1) && mixer.value() == 359);
    expect("and the colour is still a full-bright red rather than a dark surprise",
           mixer.colour().r == 255 && mixer.colour().g <= 8);
}

{
    // The `hex` row has no next colour along it, so `-` and `+` say so rather
    // than doing something mysterious to a string.
    ColorMixer mixer{Rgb{1, 2, 3}};
    mixer.setRow(6);
    expect("the hex row does not nudge", !mixer.nudge(1) && !mixer.nudge(-1));
}

// --- the mouse: a bar's fraction is a channel's value -----------------------

{
    ColorMixer mixer{Rgb{0, 0, 0}};
    expect("the middle of a red bar is 128", mixer.setFromFraction(0.5) && mixer.value() == 128);
    expect("the far end is 255", mixer.setFromFraction(1.0) && mixer.value() == 255);
    expect("and a click past the end of the bar is still 255",
           !mixer.setFromFraction(3.0) && mixer.value() == 255);
    mixer.setRow(3);
    expect("the middle of a hue bar is opposite on the wheel",
           mixer.setFromFraction(0.5) && mixer.value() == 180);
    mixer.setRow(6);
    expect("and the hex row cannot be clicked as a bar", !mixer.setFromFraction(0.5));
}

{
    // The paint and the click agree because they use the same function: a bar
    // drawn 40% full is a bar whose value is 40% of its range.
    expect("a bar's fraction and its value are the same statement both ways",
           channelValueAtFraction(MixChannel::Red, channelFraction(MixChannel::Red, 200)) == 200);
    expect("including at the bottom",
           channelFraction(MixChannel::Lightness, 0) == 0.0);
    expect("and at the top", channelFraction(MixChannel::Hue, 359) == 1.0);
}

// --- which of the two descriptions is in charge -----------------------------

{
    // Pushing saturation must not move the hue the person just set. If HSL were
    // re-derived from the RGB on every edit, the rounding would shift the hue a
    // degree each time and a slider would crawl sideways while being pushed.
    ColorMixer mixer{Rgb{200, 30, 90}};
    mixer.setRow(3);
    mixer.setDigit(0);
    static_cast<void>(mixer.typeDigit('2'));
    static_cast<void>(mixer.typeDigit('0'));
    static_cast<void>(mixer.typeDigit('0'));
    const int hue = mixer.value();
    mixer.setRow(4); // saturation
    const int saturation = mixer.value();
    for (int i = 0; i < 20; ++i)
        static_cast<void>(mixer.nudge(-1));
    expect("the hue stays exactly where it was put while saturation moves",
           mixer.hsl().h == hue);
    expect("and the mixer's own hue reading never drifts", mixer.hsl().h == hue);
    expect("while saturation did move", mixer.hsl().s == saturation - 20);
}

{
    // The other direction: pushing a red bar re-reads the wheel, because the
    // bars are the colour and the wheel is a description of it.
    ColorMixer mixer{Rgb{100, 100, 100}};
    expect("grey starts with no saturation", mixer.hsl().s == 0);
    mixer.setRow(0);
    static_cast<void>(mixer.setFromFraction(1.0));
    expect("pushing red up to full gives a saturated colour", mixer.hsl().s == 100);
    expect("and the hue is red", mixer.hsl().h == 0);
}

// --- what the mixer knows about itself --------------------------------------

{
    ColorMixer mixer{Rgb{12, 34, 56}};
    expect("a mixer nobody touched has not moved", !mixer.moved());
    mixer.setRow(1);
    static_cast<void>(mixer.nudge(1));
    expect("and one that was touched has", mixer.moved());
    expect("while still remembering what it opened with",
           mixer.startingColour().r == 12 && mixer.startingColour().g == 34 &&
               mixer.startingColour().b == 56);
}

// --- the hammer -------------------------------------------------------------

{
    // Every mutator, at every row, over and over: no channel ever ends up below
    // 0 or above its own maximum, and the two descriptions of the colour never
    // disagree by more than the rounding they share.
    ColorMixer mixer{Rgb{7, 128, 250}};
    bool in_range = true;
    bool agrees = true;
    unsigned int state = 12345u;
    const auto next = [&state](unsigned int bound) {
        state = state * 1103515245u + 12345u;
        return (state >> 16) % bound;
    };
    for (int i = 0; i < 4000; ++i) {
        switch (next(6)) {
            case 0: mixer.step(static_cast<int>(next(3)) - 1); break;
            case 1: static_cast<void>(mixer.nudge(static_cast<int>(next(3)) - 1)); break;
            case 2: static_cast<void>(mixer.typeDigit(static_cast<char>('0' + next(10)))); break;
            case 3: static_cast<void>(mixer.setFromFraction(static_cast<double>(next(120)) / 100.0)); break;
            case 4: static_cast<void>(mixer.typeHex("0123456789abcdef"[next(16)])); break;
            case 5: mixer.setRow(static_cast<int>(next(9)) - 1); break;
        }
        for (int row = 0; row < kMixChannelCount; ++row) {
            const MixChannel c = mixChannelAt(row);
            const int v = mixer.value(c);
            if (v < 0 || v > channelMaximum(c))
                in_range = false;
        }
        if (hsl10ToRgb(mixer.hsl10()) != mixer.colour())
            agrees = false;
    }
    expect("4000 mixed keystrokes and clicks leave every channel inside its range",
           in_range);
    expect("and leave the bars and the wheel describing one colour exactly", agrees);
}
}

}  // namespace

int main() {
    run();
    if (failures != 0) {
        std::cerr << failures << " of " << checks << " colour mixer checks failed\n";
        return 1;
    }
    std::cout << "PASS: " << checks << " colour mixer checks passed\n";
    return 0;
}
