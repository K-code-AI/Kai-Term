// Headless check of the exact bytes Kai-Term sends to the PTY for a mouse event.
// Pure encoder only: no widget, no PTY, no display.
// See tests/input_pty.cpp for these sequences replayed through a real PTY.

#include "../src/terminal/mouseencode.hpp"

#include <cstdio>
#include <iostream>
#include <string>

using namespace kai;

namespace {

int checks = 0;
int failures = 0;

// Render control bytes readably so a failure says what actually went out.
std::string visible(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case 0x1b: out += "ESC"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "<%02X>", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

void expect(const char* what, const std::string& got, const std::string& want) {
    ++checks;
    if (got == want)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << ": want [" << visible(want) << "] got ["
              << visible(got) << "]\n";
}

MouseReport ev(MouseKey button, MouseAction action, int row, int col, unsigned held = 0,
               int mods = 0) {
    MouseReport r;
    r.button = button;
    r.action = action;
    r.row = row;
    r.col = col;
    r.held = held;
    r.mods = mods;
    return r;
}

const char* esc = "\x1b";

// A three-byte classic report as literal bytes, for comparison.
std::string classic(int cb, int col, int row) {
    std::string out = "\x1b[M";
    out += static_cast<char>(32 + cb);
    out += static_cast<char>(32 + col);
    out += static_cast<char>(32 + row);
    return out;
}

} // namespace

int main() {
    // --- what is not reported at all -----------------------------------------
    expect("reporting off", encodeMouse(ev(MouseKey::Left, MouseAction::Press, 0, 0),
                                        MouseMode::Off, MouseCoord::Classic),
           "");
    expect("click mode ignores motion",
           encodeMouse(ev(MouseKey::None, MouseAction::Motion, 0, 0,
                          mouseKeyBit(MouseKey::Left)), MouseMode::Click, MouseCoord::Sgr),
           "");
    expect("drag mode ignores unheld motion",
           encodeMouse(ev(MouseKey::None, MouseAction::Motion, 0, 0, 0), MouseMode::Drag,
                       MouseCoord::Sgr),
           "");
    expect("negative row", encodeMouse(ev(MouseKey::Left, MouseAction::Press, -1, 0),
                                       MouseMode::Click, MouseCoord::Classic),
           "");
    expect("negative column", encodeMouse(ev(MouseKey::Left, MouseAction::Press, 0, -1),
                                          MouseMode::Click, MouseCoord::Classic),
           "");

    // --- classic three-byte form ---------------------------------------------
    // Coordinates travel as one byte each, offset by 32, and are 1-based.
    expect("classic left press",
           encodeMouse(ev(MouseKey::Left, MouseAction::Press, 0, 0), MouseMode::Click,
                       MouseCoord::Classic),
           classic(0, 1, 1));
    expect("classic bottom-right press",
           encodeMouse(ev(MouseKey::Left, MouseAction::Press, 23, 79), MouseMode::Click,
                       MouseCoord::Classic),
           classic(0, 80, 24));
    expect("classic middle press",
           encodeMouse(ev(MouseKey::Middle, MouseAction::Press, 0, 0), MouseMode::Click,
                       MouseCoord::Classic),
           classic(1, 1, 1));
    expect("classic right press",
           encodeMouse(ev(MouseKey::Right, MouseAction::Press, 0, 0), MouseMode::Click,
                       MouseCoord::Classic),
           classic(2, 1, 1));
    // With no release marker, a release is button code 3.
    expect("classic release",
           encodeMouse(ev(MouseKey::Left, MouseAction::Release, 0, 0), MouseMode::Click,
                       MouseCoord::Classic),
           classic(3, 1, 1));
    // The wheel is buttons 64 and 65 in the same numbering.
    expect("classic wheel up",
           encodeMouse(ev(MouseKey::WheelUp, MouseAction::Press, 0, 0), MouseMode::Click,
                       MouseCoord::Classic),
           classic(64, 1, 1));
    expect("classic wheel down",
           encodeMouse(ev(MouseKey::WheelDown, MouseAction::Press, 0, 0), MouseMode::Click,
                       MouseCoord::Classic),
           classic(65, 1, 1));
    // Drag is the 32 bit added to the button being held.
    expect("classic left drag",
           encodeMouse(ev(MouseKey::None, MouseAction::Motion, 0, 0, mouseKeyBit(MouseKey::Left)),
                       MouseMode::Drag, MouseCoord::Classic),
           classic(32, 1, 1));
    expect("classic right drag",
           encodeMouse(ev(MouseKey::None, MouseAction::Motion, 0, 0, mouseKeyBit(MouseKey::Right)),
                       MouseMode::Drag, MouseCoord::Classic),
           classic(34, 1, 1));
    // Any-event mode reports motion with nothing held as button 3 plus the 32 bit.
    expect("classic any-event motion",
           encodeMouse(ev(MouseKey::None, MouseAction::Motion, 0, 0, 0), MouseMode::Any,
                       MouseCoord::Classic),
           classic(35, 1, 1));
    // A wheel turn while a button is held carries the drag bit too.
    expect("classic wheel with button held",
           encodeMouse(ev(MouseKey::WheelUp, MouseAction::Press, 0, 0, mouseKeyBit(MouseKey::Left)),
                       MouseMode::Click, MouseCoord::Classic),
           classic(64 + 32, 1, 1));

    // Modifier bits: Shift 4, Meta 8, Ctrl 16.
    expect("classic shift press",
           encodeMouse(ev(MouseKey::Left, MouseAction::Press, 0, 0, 0, MouseShift),
                       MouseMode::Click, MouseCoord::Classic),
           classic(4, 1, 1));
    expect("classic alt press",
           encodeMouse(ev(MouseKey::Left, MouseAction::Press, 0, 0, 0, MouseAlt),
                       MouseMode::Click, MouseCoord::Classic),
           classic(8, 1, 1));
    expect("classic ctrl press",
           encodeMouse(ev(MouseKey::Left, MouseAction::Press, 0, 0, 0, MouseCtrl),
                       MouseMode::Click, MouseCoord::Classic),
           classic(16, 1, 1));
    expect("classic shift+ctrl drag",
           encodeMouse(ev(MouseKey::None, MouseAction::Motion, 0, 0, mouseKeyBit(MouseKey::Left),
                          MouseShift | MouseCtrl), MouseMode::Drag, MouseCoord::Classic),
           classic(32 + 4 + 16, 1, 1));

    // One byte cannot count past 255 and 32 is already spent, so the largest
    // coordinate that can be reported is 223; the far edge clamps to it rather
    // than the event being dropped.
    expect("classic column clamp",
           encodeMouse(ev(MouseKey::Left, MouseAction::Press, 0, 299), MouseMode::Click,
                       MouseCoord::Classic),
           classic(0, 223, 1));
    expect("classic row clamp",
           encodeMouse(ev(MouseKey::Left, MouseAction::Press, 499, 0), MouseMode::Click,
                       MouseCoord::Classic),
           classic(0, 1, 223));

    // --- SGR ------------------------------------------------------------------
    expect("sgr left press",
           encodeMouse(ev(MouseKey::Left, MouseAction::Press, 0, 0), MouseMode::Click,
                       MouseCoord::Sgr),
           std::string(esc) + "[1;1;0M");
    // SGR names the released button and ends a release with a lower-case m.
    expect("sgr release",
           encodeMouse(ev(MouseKey::Left, MouseAction::Release, 0, 0), MouseMode::Click,
                       MouseCoord::Sgr),
           std::string(esc) + "[1;1;0m");
    expect("sgr right press bottom-right",
           encodeMouse(ev(MouseKey::Right, MouseAction::Press, 2, 79), MouseMode::Click,
                       MouseCoord::Sgr),
           std::string(esc) + "[80;3;2M");
    expect("sgr wheel up",
           encodeMouse(ev(MouseKey::WheelUp, MouseAction::Press, 0, 0), MouseMode::Click,
                       MouseCoord::Sgr),
           std::string(esc) + "[1;1;64M");
    expect("sgr left drag",
           encodeMouse(ev(MouseKey::None, MouseAction::Motion, 0, 0, mouseKeyBit(MouseKey::Left)),
                       MouseMode::Drag, MouseCoord::Sgr),
           std::string(esc) + "[1;1;32M");
    expect("sgr any-event motion",
           encodeMouse(ev(MouseKey::None, MouseAction::Motion, 0, 0, 0), MouseMode::Any,
                       MouseCoord::Sgr),
           std::string(esc) + "[1;1;35M");
    expect("sgr shift+ctrl wheel down",
           encodeMouse(ev(MouseKey::WheelDown, MouseAction::Press, 0, 0, 0, MouseShift | MouseCtrl),
                       MouseMode::Click, MouseCoord::Sgr),
           std::string(esc) + "[1;1;85M");
    // No byte limit, so a wide terminal reports its real column.
    expect("sgr wide column",
           encodeMouse(ev(MouseKey::Left, MouseAction::Press, 0, 255), MouseMode::Click,
                       MouseCoord::Sgr),
           std::string(esc) + "[256;1;0M");

    // --- SGR in hexadecimal ---------------------------------------------------
    expect("sgr hex press",
           encodeMouse(ev(MouseKey::Left, MouseAction::Press, 0, 0), MouseMode::Click,
                       MouseCoord::SgrHex),
           std::string(esc) + "[<1;1;0M");
    expect("sgr hex release",
           encodeMouse(ev(MouseKey::Right, MouseAction::Release, 0, 0), MouseMode::Click,
                       MouseCoord::SgrHex),
           std::string(esc) + "[<1;1;2m");
    expect("sgr hex column 256",
           encodeMouse(ev(MouseKey::Left, MouseAction::Press, 0, 255), MouseMode::Click,
                       MouseCoord::SgrHex),
           std::string(esc) + "[<100;1;0M");

    // --- urxvt ----------------------------------------------------------------
    // cb is offset by 32 and the order is cb, column, row.
    expect("urxvt left press",
           encodeMouse(ev(MouseKey::Left, MouseAction::Press, 2, 79), MouseMode::Click,
                       MouseCoord::Urxvt),
           std::string(esc) + "[32;80;3M");
    expect("urxvt release",
           encodeMouse(ev(MouseKey::Left, MouseAction::Release, 2, 79), MouseMode::Click,
                       MouseCoord::Urxvt),
           std::string(esc) + "[35;80;3M");
    expect("urxvt shift press",
           encodeMouse(ev(MouseKey::Left, MouseAction::Press, 0, 0, 0, MouseShift),
                       MouseMode::Click, MouseCoord::Urxvt),
           std::string(esc) + "[36;1;1M");

    if (failures == 0) {
        std::cout << "PASS: " << checks
                  << " mouse encodings match the expected PTY bytes\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " mouse encodings failed\n";
    return 1;
}
