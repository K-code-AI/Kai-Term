#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <string>

namespace kai {

// Encode one Qt key event into the exact bytes a terminal writes to the PTY.
//
// This is a free function with no widget, no PTY and no event-loop state so the
// byte sequences can be asserted headlessly (tests/input.cpp) and replayed
// against a real PTY (tests/input_pty.cpp).
//
// app_cursor_keys mirrors DECCKM (ESC[?1h). vim, less and fish's Vi mode set it
// and then expect SS3 shapes (ESC O A) instead of CSI shapes (ESC [ A).
//
// Shapes follow xterm: unmodified keys send the short form, any modifier adds
// the CSI parameter pair "1;<code>" where code = 1 + shift(1) + alt(2) + ctrl(4).
inline std::string encodeKey(int key, Qt::KeyboardModifiers mods, const QString& text,
                             bool app_cursor_keys = false) {
    const bool shift = mods.testFlag(Qt::ShiftModifier);
    const bool alt = mods.testFlag(Qt::AltModifier);
    const bool ctrl = mods.testFlag(Qt::ControlModifier);
    const int modcode = 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0);
    const bool modified = (modcode != 1);

    // Modifier-bearing named keys: ESC [ 1 ; <code> <final>
    auto csiModified = [modcode](char final_byte) {
        return std::string("\x1b[1;") + static_cast<char>('0' + modcode) + final_byte;
    };
    // Tilde form: ESC [ <num> ~ or ESC [ <num> ; <code> ~
    auto tilde = [modcode, modified](int num) {
        std::string out = "\x1b[" + std::to_string(num);
        if (modified)
            out += ";" + std::to_string(modcode);
        out += "~";
        return out;
    };
    // Cursor keys: CSI in normal mode, SS3 in application mode; modifiers always
    // force the CSI form.
    auto cursorKey = [modcode, modified, app_cursor_keys](char final_byte) {
        if (modified)
            return std::string("\x1b[1;") + static_cast<char>('0' + modcode) + final_byte;
        return std::string(app_cursor_keys ? "\x1bO" : "\x1b[") + final_byte;
    };

    switch (key) {
        case Qt::Key_Left:
            return cursorKey('D');
        case Qt::Key_Right:
            return cursorKey('C');
        case Qt::Key_Up:
            return cursorKey('A');
        case Qt::Key_Down:
            return cursorKey('B');
        case Qt::Key_Home:
            if (modified)
                return csiModified('H');
            return app_cursor_keys ? "\x1bOH" : "\x1b[1~";
        case Qt::Key_End:
            if (modified)
                return csiModified('F');
            return app_cursor_keys ? "\x1bOF" : "\x1b[4~";
        case Qt::Key_Insert:
            return tilde(2);
        case Qt::Key_Delete:
            return tilde(3);
        case Qt::Key_PageUp:
            return tilde(5);
        case Qt::Key_PageDown:
            return tilde(6);
        case Qt::Key_Return:
        case Qt::Key_Enter:
            // Terminals send carriage return; the PTY driver turns it into a
            // line feed in canonical mode (ICRNL).
            return "\r";
        case Qt::Key_Backspace:
            // 0x7F is what xterm and the readline/fish defaults expect;
            // Ctrl+Backspace keeps the historical C-h.
            return ctrl ? std::string(1, '\b') : std::string(1, '\x7f');
        case Qt::Key_Tab:
            if (shift)
                return "\x1b[Z"; // backtab
            return std::string(1, '\t');
        case Qt::Key_Backtab:
            return "\x1b[Z";
        case Qt::Key_Escape:
            return std::string(1, '\x1b');
        case Qt::Key_Space:
            if (ctrl)
                return std::string(1, '\0'); // C-Space is NUL
            break;
        default:
            break;
    }

    // F1-F4 use the SS3 letters unmodified and the CSI letter with modifiers;
    // F5 and up use the tilde form with the vt220 numbering (16, 22, 27 and 30
    // are not function keys).
    if (key >= Qt::Key_F1 && key <= Qt::Key_F20) {
        const int fn = key - Qt::Key_F1 + 1;
        static const char ss3_letter[4] = {'P', 'Q', 'R', 'S'};
        static const int tilde_code[20] = {
            11, 12, 13, 14,       // F1-F4   (also SS3 P Q R S unmodified)
            15, 17, 18, 19,       // F5-F8
            20, 21, 23, 24,       // F9-F12
            25, 26, 28, 29,       // F13-F16
            31, 32, 33, 34        // F17-F20
        };
        if (fn <= 4) {
            if (modified)
                return std::string("\x1b[1;") + static_cast<char>('0' + modcode) + ss3_letter[fn - 1];
            return std::string("\x1bO") + ss3_letter[fn - 1];
        }
        std::string out = "\x1b[" + std::to_string(tilde_code[fn - 1]);
        if (modified)
            out += ";" + std::to_string(modcode);
        out += "~";
        return out;
    }

    // Ctrl+letter is the control character, independent of what the platform
    // put in text(); doing it first keeps Ctrl+C a real 0x03 everywhere.
    if (ctrl && key >= Qt::Key_A && key <= Qt::Key_Z) {
        std::string out;
        if (alt)
            out += '\x1b';
        out += static_cast<char>(key - Qt::Key_A + 1);
        return out;
    }

    // Printable input, including UTF-8 composed by an IME.
    if (!text.isEmpty()) {
        const QByteArray utf8 = text.toUtf8();
        std::string out;
        if (alt)
            out += '\x1b';
        out.append(utf8.constData(), static_cast<std::size_t>(utf8.size()));
        return out;
    }

    // Modifier keys pressed on their own, and anything else with no terminal
    // encoding, produce no bytes.
    return {};
}

} // namespace kai
