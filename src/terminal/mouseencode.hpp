#pragma once

#include <algorithm>
#include <cstdio>
#include <string>

namespace kai {

// Which mouse-reporting protocol the application asked for. The modes nest:
// drag tracking implies click tracking, any-event tracking implies both.
enum class MouseMode {
    Off,   // no reporting; the terminal keeps the mouse for selection
    Click, // ?1000 press, release and wheel
    Drag,  // ?1002 also motion while a button is held
    Any    // ?1003 also motion with no button held
};

// Coordinate encoding requested by the application. SGR takes precedence over
// urxvt, which takes precedence over the classic three-byte form, the way
// xterm resolves overlapping modes.
enum class MouseCoord { Classic, Urxvt, Sgr, SgrHex };

enum class MouseAction { Press, Release, Motion };

// Mouse buttons as the terminal protocol sees them. Wheel events are buttons in
// this protocol (64 and 65) even though Qt delivers them separately, so the
// encoder takes its own button type and stays testable without a real mouse.
enum class MouseKey {
    Left = 0,
    Middle = 1,
    Right = 2,
    None = 3,
    WheelUp = 4,
    WheelDown = 5
};

constexpr int mouseKeyBit(MouseKey key) { return 1 << static_cast<int>(key); }

// Modifier bits the protocol carries alongside a mouse event. Kept as plain
// bits rather than Qt::KeyboardModifiers so the model stays free of Qt and can
// be tested without a GUI build; the widget converts from Qt.
enum MouseModifierBits {
    MouseShift = 4,
    MouseAlt = 8,
    MouseCtrl = 16
};

// One mouse event, with zero-based row and column. `button` is the button that
// changed; `held` is a bitmask of mouseKeyBit() for the buttons down at the
// time, which is what distinguishes a drag from motion with nothing held.
struct MouseReport {
    MouseKey button = MouseKey::None;
    unsigned held = 0;
    MouseAction action = MouseAction::Press;
    int mods = 0; // MouseShift | MouseAlt | MouseCtrl
    int row = 0;
    int col = 0;
};

namespace detail {

// Button code in the protocol's numbering, before modifier and motion bits.
inline int buttonCode(MouseKey key) {
    switch (key) {
        case MouseKey::Left: return 0;
        case MouseKey::Middle: return 1;
        case MouseKey::Right: return 2;
        case MouseKey::WheelUp: return 64;
        case MouseKey::WheelDown: return 65;
        case MouseKey::None: return 3;
    }
    return 3;
}

// The button a drag is reported for: the highest-priority one held.
inline int heldCode(unsigned held) {
    if (held & mouseKeyBit(MouseKey::Left)) return 0;
    if (held & mouseKeyBit(MouseKey::Middle)) return 1;
    if (held & mouseKeyBit(MouseKey::Right)) return 2;
    return 3; // motion with no button held, only sent in any-event mode
}

} // namespace detail

// Encode one mouse event into the bytes the terminal writes to the PTY.
//
// Returns an empty string when the event should not be reported at all: mouse
// reporting off, or motion in a mode that only reports clicks. Classic encoding
// is limited to 223 columns and rows because the coordinates travel as a single
// byte offset by 32; beyond that the coordinate is clamped, which is what xterm
// does rather than dropping the event.
inline std::string encodeMouse(const MouseReport& ev, MouseMode mode, MouseCoord coord) {
    if (mode == MouseMode::Off)
        return {};
    if (ev.action == MouseAction::Motion && mode == MouseMode::Click)
        return {};
    if (ev.action == MouseAction::Motion && mode == MouseMode::Drag && ev.held == 0)
        return {};
    if (ev.row < 0 || ev.col < 0)
        return {};

    const int mods = ev.mods;

    // cb without modifiers, per action. A release is button code 3 in the
    // classic and urxvt forms, which have no release marker of their own, and
    // the released button's own code in the SGR forms, which end releases with
    // a lower-case m.
    int cb_press = 0;
    int cb_release = 0;
    switch (ev.action) {
        case MouseAction::Press: {
            cb_press = detail::buttonCode(ev.button);
            // A wheel turn while a button is held is reported as motion plus
            // the wheel bit.
            if ((ev.button == MouseKey::WheelUp || ev.button == MouseKey::WheelDown) &&
                ev.held != 0)
                cb_press += 32;
            break;
        }
        case MouseAction::Release:
            cb_press = detail::buttonCode(ev.button);
            cb_release = 3;
            break;
        case MouseAction::Motion:
            cb_press = 32 + detail::heldCode(ev.held);
            break;
    }

    const int cb_classic = cb_press + (ev.action == MouseAction::Release ? cb_release : 0) + mods;
    const int cb_extended = cb_press + mods;

    const int col = ev.col + 1; // protocols are 1-based
    const int row = ev.row + 1;

    switch (coord) {
        case MouseCoord::Sgr:
            return "\x1b[" + std::to_string(col) + ";" + std::to_string(row) + ";" +
                   std::to_string(cb_extended) + (ev.action == MouseAction::Release ? "m" : "M");
        case MouseCoord::SgrHex: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "\x1b[<%x;%x;%x%c", col, row, cb_extended,
                          ev.action == MouseAction::Release ? 'm' : 'M');
            return std::string(buf);
        }
        case MouseCoord::Urxvt:
            // The urxvt form carries the same button numbering as the classic
            // three-byte one, offset by 32, so a release is still button 3.
            return "\x1b[" + std::to_string(cb_classic + 32) + ";" + std::to_string(col) + ";" +
                   std::to_string(row) + "M";
        case MouseCoord::Classic: {
            const int c = std::min(col, 255 - 32);
            const int r = std::min(row, 255 - 32);
            std::string out = "\x1b[M";
            out += static_cast<char>(32 + std::min(cb_classic, 255 - 32));
            out += static_cast<char>(32 + c);
            out += static_cast<char>(32 + r);
            return out;
        }
    }
    return {};
}

} // namespace kai
