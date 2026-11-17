// Keyboard ground truth against a real PTY: the bytes the encoder produces are
// written to a PTY master and read back from the other end, so the assertions
// describe what a shell program actually receives rather than what the screen
// model thinks it should look like.
//
// Phase 1 runs the shell's cat in canonical mode with echo off, which is where
// carriage return and erase are interpreted by the line discipline.
// Phase 2 switches the tty to raw mode, where every byte must come back
// unchanged, sequence included.
// Phase 3 replays the mouse reports an application would receive with mouse
// reporting on, classic and extended forms alike.

#include "../src/process/pty.hpp"
#include "../src/terminal/keyencode.hpp"
#include "../src/terminal/mouseencode.hpp"

#include <QString>
#include <QtGlobal>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

namespace {

int failures = 0;
int checks = 0;

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

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Read until the descriptor goes quiet, to swallow a shell's start-up chatter.
void drain(kai::Pty& p, int settle_ms = 400) {
    char buf[4096];
    int idle = 0;
    while (idle < settle_ms) {
        ssize_t n = p.read(buf, sizeof(buf));
        if (n > 0) {
            idle = 0;
            continue;
        }
        if (n == 0 || (n < 0 && errno == EIO))
            return;
        usleep(10000);
        idle += 10;
    }
}

// Read exactly want bytes, or report what happened. Returns false on timeout or
// if the child closed the PTY.
bool read_bytes(kai::Pty& p, std::string& out, std::size_t want, int timeout_ms) {
    char buf[4096];
    int waited = 0;
    while (out.size() < want && waited < timeout_ms) {
        ssize_t n = p.read(buf, sizeof(buf));
        if (n > 0) {
            out.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0 || (n < 0 && errno == EIO)) {
            std::cerr << "  child exited; PTY closed\n";
            return false;
        }
        usleep(10000);
        waited += 10;
    }
    return out.size() >= want;
}

void expect_output(kai::Pty& p, const char* what, const std::string& input,
                   const std::string& want, int timeout_ms = 3000) {
    ++checks;
    p.write(input);
    std::string got;
    if (!read_bytes(p, got, want.size(), timeout_ms)) {
        ++failures;
        std::cerr << "FAIL: " << what << ": timed out, got [" << visible(got) << "]\n";
        return;
    }
    got.resize(want.size());
    if (got != want) {
        ++failures;
        std::cerr << "FAIL: " << what << ": want [" << visible(want) << "] got ["
                  << visible(got) << "]\n";
        return;
    }
    // Nothing further should arrive: a spurious extra byte is a real bug.
    std::string extra;
    if (read_bytes(p, extra, 1, 150)) {
        ++failures;
        std::cerr << "FAIL: " << what << ": unexpected extra output [" << visible(extra) << "]\n";
    }
}

// Raw mode: what goes in must come back byte for byte.
void expect_roundtrip(kai::Pty& p, const char* what, const std::string& bytes) {
    expect_output(p, what, bytes, bytes);
}

// Build a mouse report the way the widget would, then encode it, so the PTY sees
// exactly the bytes the terminal would send rather than a hand-written copy.
std::string report(kai::MouseKey button, kai::MouseAction action, int row, int col,
                   kai::MouseMode mode = kai::MouseMode::Click,
                   kai::MouseCoord coord = kai::MouseCoord::Classic, unsigned held = 0,
                   int mods = 0) {
    kai::MouseReport r;
    r.button = button;
    r.action = action;
    r.row = row;
    r.col = col;
    r.held = held;
    r.mods = mods;
    return kai::encodeMouse(r, mode, coord);
}

} // namespace

int main() {
    kai::Pty p(24, 80, "/bin/sh");
    set_nonblocking(p.master());
    drain(p);

    // Empty prompt so the shell writes nothing of its own, and no local echo so
    // anything we read back came from the program on the other end of the PTY.
    p.write("PS1=''\n");
    drain(p, 300);
    p.write("stty -echo\n");
    drain(p, 300);

    // ---- Phase 1: canonical mode, line discipline active -------------------
    p.write("cat\n");
    drain(p, 300);

    // Enter sends carriage return; ICRNL turns it into a line feed for cat, and
    // ONLCR turns that line feed back into CRLF on the way out. This is the
    // ground truth for the "Enter sends CR" decision in keyencode.hpp.
    expect_output(p, "Enter sends CR, driver delivers a line",
                  "abc" + std::string(1, '\r'), "abc\r\n");

    // Backspace sends 0x7F, which is the line discipline's erase character
    // (stty reports erase = ^?), so the shell sees "ac" not "abc".
    expect_output(p, "Backspace as DEL erases a character",
                  "ab" + std::string(1, '\x7f') + "c" + std::string(1, '\r'), "ac\r\n");

    // A tab is passed through untouched: the tty reports tab0, so tabs are not
    // expanded on output.
    expect_output(p, "Tab passes through",
                  std::string(1, '\t') + "x" + std::string(1, '\r'), "\tx\r\n");

    p.write(std::string(1, '\x04')); // EOF ends cat, control returns to the shell
    drain(p, 400);

    // ---- Phase 2: raw mode, every byte must survive verbatim --------------
    p.write("stty raw -echo\n");
    drain(p, 300);
    p.write("cat\n");
    drain(p, 300);

    auto enc = [](int key, Qt::KeyboardModifiers mods = Qt::NoModifier,
                  const QString& text = QString(), bool app_cursor = false) {
        return kai::encodeKey(key, mods, text, app_cursor);
    };

    const std::vector<std::pair<const char*, std::string>> vectors = {
        {"plain text", "abc"},
        {"UTF-8 text", "\xc3\xa9\xe6\xbc\xa2\xf0\x9f\x98\x80"},
        {"Up", enc(Qt::Key_Up)},
        {"Down", enc(Qt::Key_Down)},
        {"Right", enc(Qt::Key_Right)},
        {"Left", enc(Qt::Key_Left)},
        {"Up in application cursor mode", enc(Qt::Key_Up, Qt::NoModifier, QString(), true)},
        {"Shift+Up", enc(Qt::Key_Up, Qt::ShiftModifier)},
        {"Ctrl+Left", enc(Qt::Key_Left, Qt::ControlModifier)},
        {"Home", enc(Qt::Key_Home)},
        {"End", enc(Qt::Key_End)},
        {"Ctrl+Home", enc(Qt::Key_Home, Qt::ControlModifier)},
        {"Insert", enc(Qt::Key_Insert)},
        {"Delete", enc(Qt::Key_Delete)},
        {"PageUp", enc(Qt::Key_PageUp)},
        {"PageDown", enc(Qt::Key_PageDown)},
        {"F1", enc(Qt::Key_F1)},
        {"F5", enc(Qt::Key_F5)},
        {"F12", enc(Qt::Key_F12)},
        {"Ctrl+F1", enc(Qt::Key_F1, Qt::ControlModifier)},
        {"Return", enc(Qt::Key_Return)},
        {"Backspace", enc(Qt::Key_Backspace)},
        {"Ctrl+Backspace", enc(Qt::Key_Backspace, Qt::ControlModifier)},
        {"Tab", enc(Qt::Key_Tab)},
        {"Shift+Tab", enc(Qt::Key_Tab, Qt::ShiftModifier)},
        {"Escape", enc(Qt::Key_Escape)},
        {"Ctrl+C", enc(Qt::Key_C, Qt::ControlModifier)},
        {"Ctrl+D", enc(Qt::Key_D, Qt::ControlModifier)},
        {"Ctrl+Z", enc(Qt::Key_Z, Qt::ControlModifier)},
        {"Alt+a", enc(Qt::Key_A, Qt::AltModifier, QStringLiteral("a"))},
        {"Shift+Delete", enc(Qt::Key_Delete, Qt::ShiftModifier)},
    };

    for (const auto& v : vectors)
        expect_roundtrip(p, v.first, v.second);

    // A burst arrives as one write; the reader must not split or reorder it.
    expect_roundtrip(p, "arrow-key burst",
                     enc(Qt::Key_Up) + enc(Qt::Key_Up) + enc(Qt::Key_Left) + "ls");

    // ---- Phase 3: mouse reports -------------------------------------------
    // An application inside the PTY turns mouse reporting on the way vim, htop
    // and tmux do. Reports have to arrive byte for byte: the classic form carries
    // the button code and both coordinates as raw bytes, including values above
    // 0x7F, so a tty that stripped bit 7 or translated a control byte would
    // corrupt it in a way no screen diff would show.
    using kai::MouseAction;
    using kai::MouseCoord;
    using kai::MouseKey;
    using kai::MouseMode;

    expect_roundtrip(p, "classic click", report(MouseKey::Left, MouseAction::Press, 0, 0));
    expect_roundtrip(p, "classic click at the bottom-right cell",
                     report(MouseKey::Left, MouseAction::Press, 23, 79));
    expect_roundtrip(p, "classic release", report(MouseKey::Left, MouseAction::Release, 23, 79));
    expect_roundtrip(p, "classic wheel up then down",
                     report(MouseKey::WheelUp, MouseAction::Press, 5, 10) +
                         report(MouseKey::WheelDown, MouseAction::Press, 5, 10));
    expect_roundtrip(p, "classic drag with the left button held",
                     report(MouseKey::None, MouseAction::Motion, 3, 4, MouseMode::Drag,
                            MouseCoord::Classic, kai::mouseKeyBit(MouseKey::Left)));
    expect_roundtrip(p, "classic motion with no button held",
                     report(MouseKey::None, MouseAction::Motion, 3, 4, MouseMode::Any));
    expect_roundtrip(p, "classic press with shift and ctrl",
                     report(MouseKey::Right, MouseAction::Press, 1, 1, MouseMode::Click,
                            MouseCoord::Classic, 0, kai::MouseShift | kai::MouseCtrl));
    // The furthest cell a classic report can name, which puts 0xFF on the wire.
    expect_roundtrip(p, "classic report of the farthest reportable cell",
                     report(MouseKey::Left, MouseAction::Press, 222, 222));
    expect_roundtrip(p, "SGR press then release",
                     report(MouseKey::Left, MouseAction::Press, 2, 79, MouseMode::Click,
                            MouseCoord::Sgr) +
                         report(MouseKey::Left, MouseAction::Release, 2, 79, MouseMode::Click,
                                MouseCoord::Sgr));
    expect_roundtrip(p, "urxvt press",
                     report(MouseKey::Middle, MouseAction::Press, 2, 79, MouseMode::Click,
                            MouseCoord::Urxvt));
    expect_roundtrip(p, "hexadecimal SGR press",
                     report(MouseKey::Left, MouseAction::Press, 0, 255, MouseMode::Click,
                            MouseCoord::SgrHex));

    // One write holding a press, two drags and a release, which is how a fast
    // drag across cells reaches a program.
    expect_roundtrip(p, "drag burst in one write",
                     report(MouseKey::Left, MouseAction::Press, 4, 4, MouseMode::Drag) +
                         report(MouseKey::None, MouseAction::Motion, 4, 7, MouseMode::Drag,
                                MouseCoord::Classic, kai::mouseKeyBit(MouseKey::Left)) +
                         report(MouseKey::None, MouseAction::Motion, 4, 9, MouseMode::Drag,
                                MouseCoord::Classic, kai::mouseKeyBit(MouseKey::Left)) +
                         report(MouseKey::Left, MouseAction::Release, 4, 9, MouseMode::Drag));

    p.write(std::string(1, '\x04'));
    drain(p, 200);
    p.write("stty sane\n");
    drain(p, 200);

    if (failures == 0) {
        std::cout << "PASS: " << checks
                  << " input sequences round-tripped through a real PTY unchanged\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " PTY input checks failed\n";
    return 1;
}
