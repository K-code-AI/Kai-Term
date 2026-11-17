// Headless check of the exact bytes Kai-Term sends to the PTY for a key event.
// This is the pure encoder only: no widget, no PTY, no display.
// See tests/input_pty.cpp for the same sequences replayed through a real PTY.

#include "../src/terminal/keyencode.hpp"

#include <QString>
#include <QtGlobal>

#include <cstdio>
#include <iostream>
#include <string>

namespace {

int checks = 0;
int failures = 0;

// Render control bytes readably so a failure says what actually went out.
std::string visible(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case 0x1b: out += "ESC"; break;
            case '\r': out += "CR"; break;
            case '\n': out += "LF"; break;
            case '\t': out += "TAB"; break;
            case 0x7f: out += "DEL"; break;
            default:
                if (c < 0x20 || c == 0x80) {
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

std::string enc(int key, Qt::KeyboardModifiers mods = Qt::NoModifier,
                const QString& text = QString(), bool app_cursor = false) {
    return kai::encodeKey(key, mods, text, app_cursor);
}

const char* esc = "\x1b";

} // namespace

int main() {
    // Cursor keys, plain: what fish's default bindings and readline expect.
    expect("Up", enc(Qt::Key_Up), std::string(esc) + "[A");
    expect("Down", enc(Qt::Key_Down), std::string(esc) + "[B");
    expect("Right", enc(Qt::Key_Right), std::string(esc) + "[C");
    expect("Left", enc(Qt::Key_Left), std::string(esc) + "[D");

    // DECCKM application cursor keys: SS3 shape, used by vim and less.
    expect("Up in app mode", enc(Qt::Key_Up, Qt::NoModifier, QString(), true),
           std::string(esc) + "OA");
    expect("Down in app mode", enc(Qt::Key_Down, Qt::NoModifier, QString(), true),
           std::string(esc) + "OB");
    expect("Left in app mode", enc(Qt::Key_Left, Qt::NoModifier, QString(), true),
           std::string(esc) + "OD");

    // Modifiers force the CSI parameter form, including in application mode.
    expect("Shift+Up", enc(Qt::Key_Up, Qt::ShiftModifier), std::string(esc) + "[1;2A");
    expect("Alt+Up", enc(Qt::Key_Up, Qt::AltModifier), std::string(esc) + "[1;3A");
    expect("Ctrl+Up", enc(Qt::Key_Up, Qt::ControlModifier), std::string(esc) + "[1;5A");
    expect("Ctrl+Shift+Up", enc(Qt::Key_Up, Qt::ControlModifier | Qt::ShiftModifier),
           std::string(esc) + "[1;6A");
    expect("Ctrl+Alt+Shift+Left",
           enc(Qt::Key_Left, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier),
           std::string(esc) + "[1;8D");
    expect("Shift+Up in app mode", enc(Qt::Key_Up, Qt::ShiftModifier, QString(), true),
           std::string(esc) + "[1;2A");

    // Home and End: tilde form plain, SS3 in application mode, CSI with modifiers.
    expect("Home", enc(Qt::Key_Home), std::string(esc) + "[1~");
    expect("End", enc(Qt::Key_End), std::string(esc) + "[4~");
    expect("Home in app mode", enc(Qt::Key_Home, Qt::NoModifier, QString(), true),
           std::string(esc) + "OH");
    expect("End in app mode", enc(Qt::Key_End, Qt::NoModifier, QString(), true),
           std::string(esc) + "OF");
    expect("Ctrl+Home", enc(Qt::Key_Home, Qt::ControlModifier), std::string(esc) + "[1;5H");
    expect("Shift+End", enc(Qt::Key_End, Qt::ShiftModifier), std::string(esc) + "[1;2F");

    // Block keys.
    expect("Insert", enc(Qt::Key_Insert), std::string(esc) + "[2~");
    expect("Delete", enc(Qt::Key_Delete), std::string(esc) + "[3~");
    expect("PageUp", enc(Qt::Key_PageUp), std::string(esc) + "[5~");
    expect("PageDown", enc(Qt::Key_PageDown), std::string(esc) + "[6~");
    expect("Shift+Delete", enc(Qt::Key_Delete, Qt::ShiftModifier), std::string(esc) + "[3;2~");
    expect("Ctrl+PageUp", enc(Qt::Key_PageUp, Qt::ControlModifier), std::string(esc) + "[5;5~");

    // Function keys: SS3 letters for F1-F4, vt220 tilde numbers above that.
    expect("F1", enc(Qt::Key_F1), std::string(esc) + "OP");
    expect("F2", enc(Qt::Key_F2), std::string(esc) + "OQ");
    expect("F3", enc(Qt::Key_F3), std::string(esc) + "OR");
    expect("F4", enc(Qt::Key_F4), std::string(esc) + "OS");
    expect("F5", enc(Qt::Key_F5), std::string(esc) + "[15~");
    expect("F6", enc(Qt::Key_F6), std::string(esc) + "[17~");
    expect("F11", enc(Qt::Key_F11), std::string(esc) + "[23~");
    expect("F12", enc(Qt::Key_F12), std::string(esc) + "[24~");
    expect("F13", enc(Qt::Key_F13), std::string(esc) + "[25~");
    expect("F20", enc(Qt::Key_F20), std::string(esc) + "[34~");
    expect("Ctrl+F1", enc(Qt::Key_F1, Qt::ControlModifier), std::string(esc) + "[1;5P");
    expect("Shift+F5", enc(Qt::Key_F5, Qt::ShiftModifier), std::string(esc) + "[15;2~");

    // Editing keys. Enter sends carriage return; the PTY driver turns it into a
    // line feed in canonical mode (verified in tests/input_pty.cpp).
    expect("Return", enc(Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r")), "\r");
    expect("Enter (numpad)", enc(Qt::Key_Enter), "\r");
    expect("Backspace", enc(Qt::Key_Backspace), std::string(1, '\x7f'));
    expect("Ctrl+Backspace", enc(Qt::Key_Backspace, Qt::ControlModifier), std::string(1, '\b'));
    expect("Tab", enc(Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t")), "\t");
    expect("Shift+Tab", enc(Qt::Key_Tab, Qt::ShiftModifier), std::string(esc) + "[Z");
    expect("Backtab", enc(Qt::Key_Backtab), std::string(esc) + "[Z");
    expect("Escape", enc(Qt::Key_Escape, Qt::NoModifier, QStringLiteral("\x1b")),
           std::string(1, '\x1b'));

    // Control characters, independent of what the platform put in text().
    expect("Ctrl+A", enc(Qt::Key_A, Qt::ControlModifier), std::string(1, '\x01'));
    expect("Ctrl+C", enc(Qt::Key_C, Qt::ControlModifier), std::string(1, '\x03'));
    expect("Ctrl+D", enc(Qt::Key_D, Qt::ControlModifier), std::string(1, '\x04'));
    expect("Ctrl+Z", enc(Qt::Key_Z, Qt::ControlModifier), std::string(1, '\x1a'));
    expect("Ctrl+Shift+C", enc(Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier),
           std::string(1, '\x03'));
    expect("Ctrl+Space", enc(Qt::Key_Space, Qt::ControlModifier, QStringLiteral(" ")),
           std::string(1, '\0'));
    expect("Ctrl+Alt+C", enc(Qt::Key_C, Qt::ControlModifier | Qt::AltModifier),
           std::string(esc) + std::string(1, '\x03'));

    // Printable input, with Alt as an escape prefix, and UTF-8 of every length.
    expect("letter", enc(Qt::Key_A, Qt::NoModifier, QStringLiteral("a")), "a");
    expect("Alt+letter", enc(Qt::Key_A, Qt::AltModifier, QStringLiteral("a")),
           std::string(esc) + "a");
    expect("digit", enc(Qt::Key_7, Qt::NoModifier, QStringLiteral("7")), "7");
    expect("two-byte UTF-8", enc(Qt::Key_unknown, Qt::NoModifier, QString::fromUtf8("\xc3\xa9")),
           std::string("\xc3\xa9"));
    expect("three-byte UTF-8", enc(Qt::Key_unknown, Qt::NoModifier, QString::fromUtf8("\xe6\xbc\xa2")),
           std::string("\xe6\xbc\xa2"));
    expect("four-byte UTF-8", enc(Qt::Key_unknown, Qt::NoModifier, QString::fromUtf8("\xf0\x9f\x98\x80")),
           std::string("\xf0\x9f\x98\x80"));
    expect("composed string",
           enc(Qt::Key_unknown, Qt::NoModifier, QString::fromUtf8("a\xcc\x81" "b")),
           std::string("a\xcc\x81" "b"));
    expect("Alt+UTF-8", enc(Qt::Key_unknown, Qt::AltModifier, QString::fromUtf8("\xc3\xa9")),
           std::string(esc) + "\xc3\xa9");

    // Keys with no terminal encoding produce nothing at all.
    expect("Shift alone", enc(Qt::Key_Shift), "");
    expect("Control alone", enc(Qt::Key_Control), "");
    expect("Alt alone", enc(Qt::Key_Alt), "");
    expect("Meta alone", enc(Qt::Key_Meta), "");
    expect("CapsLock", enc(Qt::Key_CapsLock), "");
    expect("unmapped key", enc(Qt::Key_MediaPlay), "");

    if (failures == 0) {
        std::cout << "PASS: " << checks << " key encodings match the expected PTY bytes\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " key encodings failed\n";
    return 1;
}
