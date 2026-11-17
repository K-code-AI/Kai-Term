#include "screen.hpp"

namespace kai {

// These helpers touch private members, so they are defined as member-like
// lambdas inside feed() instead; see the switch in feed() below.

void Screen::feed(std::string_view bytes) {
    for (unsigned char raw : bytes) {
        const char c = static_cast<char>(raw);

        if (mode_ == 2) {
            // CSI: collect parameters until the final byte.
            if (raw >= 0x40 && raw <= 0x7E) {
                processCsi(csi_, c);
                csi_.clear();
                mode_ = 0;
            } else {
                csi_.push_back(c);
            }
            continue;
        }

        if (mode_ == 3) {
            // OSC: a command for the terminal itself, ended by BEL or ESC
            // backslash. The payload is collected and handed over at the end,
            // because it is the command — a title, a clipboard write — and not
            // text to be drawn.
            if (raw == 0x07 || (osc_esc_pending_ && raw == 0x5C)) {
                processOsc(osc_);
                mode_ = 0;
                osc_esc_pending_ = false;
                continue;
            }
            if (osc_esc_pending_) {
                // An ESC that turned out not to introduce the terminator is part
                // of the payload; keeping it beats silently dropping a byte.
                if (!osc_truncated_ && osc_.size() + 1 < kMaxOscLength)
                    osc_.push_back('\x1b');
                osc_esc_pending_ = false;
            }
            if (raw == 0x1B) {
                osc_esc_pending_ = true;
                continue;
            }
            // A payload over the cap is dropped rather than grown into: an
            // application that lost its terminator would otherwise be able to
            // make the terminal eat memory until it was killed.
            if (!osc_truncated_) {
                if (osc_.size() < kMaxOscLength)
                    osc_.push_back(c);
                else
                    osc_truncated_ = true;
            }
            continue;
        }

        if (mode_ == 5) {
            // DCS payload: ignore until ESC backslash.
            if (raw == 0x1B)
                mode_ = 6;
            continue;
        }

        if (mode_ == 6) {
            if (raw == 0x5C)
                mode_ = 0;
            else
                mode_ = 5;
            continue;
        }

        if (mode_ == 4) {
            // Ignore exactly one character, but never swallow a new ESC sequence
            // that starts the next read chunk.
            if (raw == 0x1B)
                mode_ = 1;
            else
                mode_ = 0;
            continue;
        }

        if (mode_ == 7) {
            // ESC with one intermediate byte (e.g. ESC # 8 DECALN). Fe escape
            // finals can start at 0x30 ('8' of DECALN is 0x38).
            if (raw >= 0x30 && raw <= 0x7E) {
                if (esc_intermediate_ == '#' && raw == '8')
                    alignScreenDecALN();
                // other intermediate Fe sequences are ignored
            }
            esc_intermediate_ = 0;
            mode_ = 0;
            continue;
        }

        if (mode_ == 8) {
            // Charset designation: ESC ( X or ESC ) X.
            const bool dec_special = (raw == '0');
            if (esc_intermediate_ == '(')
                g0_charset_ = dec_special ? 1 : 0;
            else
                g1_charset_ = dec_special ? 1 : 0;
            esc_intermediate_ = 0;
            mode_ = 0;
            continue;
        }

        if (mode_ == 1) {
            if (c == '[') {
                mode_ = 2;
                csi_.clear();
            } else if (c == ']') {
                mode_ = 3;
                osc_.clear();
                osc_truncated_ = false;
                osc_esc_pending_ = false;
            } else if (c == 'P') {
                mode_ = 5;
            } else if (c == '7') {
                saveCursor();
                mode_ = 0;
            } else if (c == '8') {
                restoreCursor();
                mode_ = 0;
            } else if (c == 'D') {
                index();       // IND: line feed without carriage return
                mode_ = 0;
            } else if (c == 'E') {
                carriageReturn();
                index();       // NEL
                mode_ = 0;
            } else if (c == 'M') {
                reverseIndex(); // RI
                mode_ = 0;
            } else if (c == 'c') {
                resetEverything();
                mode_ = 0;
            } else if (c == '=' || c == '>') {
                // Complete application-keypad sequences; do not consume the next char.
                mode_ = 0;
            } else if (c == '(' || c == ')') {
                esc_intermediate_ = c;
                mode_ = 8;
            } else if (c == '%' || c == '+' || c == '-' || c == '*' || c == '/') {
                mode_ = 4;
            } else if (raw >= 0x20 && raw <= 0x2F) {
                esc_intermediate_ = c; // e.g. '#' of ESC # 8
                mode_ = 7;
            } else {
                mode_ = 0;
            }
            continue;
        }

        if (raw == 0x1B) {
            flushPendingReplacement();
            mode_ = 1;
            continue;
        }

        if (raw == 0x0D) {
            flushPendingReplacement();
            carriageReturn();
            continue;
        }

        if (raw == 0x0A) {
            flushPendingReplacement();
            // LF is index: move down (scrolling at the bottom margin) and keep
            // the column, like xterm (lfnl). tmux emits bare LFs between rows
            // of redraw sweeps; collapsing LF to CRLF breaks its EL-then-LF
            // erase sweeps and destroys neighbouring panes.
            index();
            continue;
        }

        if (raw == 0x08) {
            flushPendingReplacement();
            pending_wrap_ = false;
            if (cursor_x_ > 0)
                --cursor_x_;
            continue;
        }

        if (raw == 0x09) {
            flushPendingReplacement();
            pending_wrap_ = false;
            cursor_x_ = std::min(cols_, ((cursor_x_ / 8) + 1) * 8);
            if (cursor_x_ >= cols_) {
                cursor_x_ = cols_;
                if (autowrap_)
                    pending_wrap_ = true;
                else
                    cursor_x_ = cols_ - 1;
            }
            continue;
        }

        if (raw == 0x0F) { // SO: GL uses G1
            flushPendingReplacement();
            gl_is_g1_ = true;
            continue;
        }

        if (raw == 0x0E) { // SI: GL uses G0
            flushPendingReplacement();
            gl_is_g1_ = false;
            continue;
        }

        if (raw < 0x20 || raw == 0x7F) {
            flushPendingReplacement();
            continue;
        }

        if (raw < 0x80) {
            flushPendingReplacement();
            putCodepoint(raw);
            continue;
        }

        // UTF-8 continuation. pending_utf8_ may survive across feed() calls.
        if (pending_utf8_.empty()) {
            int len = utf8Expected(raw);
            if (len == 0) {
                putCodepoint(0xFFFD);
                continue;
            }
            pending_utf8_.push_back(c);
            if (len == 1) {
                pending_utf8_.clear();
                putCodepoint(raw);
            }
            continue;
        }

        pending_utf8_.push_back(c);
        if (!validPrefix(pending_utf8_)) {
            pending_utf8_.clear();
            putCodepoint(0xFFFD);
            continue;
        }

        const int len = utf8Expected(static_cast<unsigned char>(pending_utf8_[0]));
        if (static_cast<int>(pending_utf8_.size()) == len) {
            std::uint32_t cp = decodeUtf8(pending_utf8_);
            pending_utf8_.clear();
            putCodepoint(cp);
        }
    }
}

} // namespace kai
