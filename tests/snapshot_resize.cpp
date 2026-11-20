// A restored screen must arrive as one whole, or a full-screen program hangs.
//
// When a session changes size the daemon re-sends the whole screen as a snapshot. That
// snapshot has to describe a screen that might be showing a full-screen program, so it
// begins by leaving whatever alternate screen the view was on (so the primary buffer can
// be painted) and re-enters it at the end. A window that is handed only the first half of
// that sees, in the gap between the halves, a screen that has dropped to the primary
// buffer — and because a full-screen program is given the whole window, the grid follows
// that transient down: the window resizes, the session resizes to match, the session
// answers with another snapshot, and the two chase each other through an endless resize
// loop that reflows the entire scrollback on every turn. With nothing to scroll back to a
// snapshot is small and lands in one read, so the transient is never seen and nothing
// happens — which is exactly why btop and tmux hung only once there was history, and ran
// fine before it.
//
// This drives a real widget's reader thread with a snapshot large enough to be split
// across several reads, and asserts the window neither leaves the alternate screen nor
// resizes when the snapshot is applied: the transient must be invisible to the geometry,
// which is only true if the whole buffered run reaches the model in one feed.

#include "../src/session/snapshot.hpp"
#include "../src/terminal/backend.hpp"
#include "../src/terminal/screen.hpp"
#include "../src/terminal/terminalwidget.hpp"

#include <QApplication>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

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

void pump(int ms, int step_ms = 10) {
    for (int elapsed = 0; elapsed < ms; elapsed += step_ms) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
    }
    QCoreApplication::processEvents();
}

bool waitFor(std::function<bool()> pred, int timeout_ms = 4000) {
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += 10) {
        QCoreApplication::processEvents();
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    QCoreApplication::processEvents();
    return pred();
}

// A backend that hands out whatever it has been loaded with, in pieces, and counts every
// time the window tells it the grid changed. It is not a shell; it is a tape of bytes with
// a resize counter, so the test can watch the window react to a snapshot.
class TapeBackend final : public kai::TerminalBackend {
public:
    std::string tape;
    int resizes = 0;
    int last_rows = -1;
    int last_cols = -1;

    TapeBackend() {
        // A descriptor the reader can wait on while the tape is empty. It is never
        // written, so a wait blocks rather than spinning; the tape is the real source,
        // and the reader takes it without waiting whenever hasBuffered() is true.
        if (::pipe(fds_) != 0) {
            fds_[0] = -1;
            fds_[1] = -1;
        }
    }
    ~TapeBackend() override {
        for (int fd : fds_)
            if (fd >= 0)
                ::close(fd);
    }

    int descriptor() const override { return fds_[0]; }

    ssize_t read(char* buf, std::size_t n) override {
        // Only called when hasBuffered() was true, so there is always something here.
        const std::size_t take = std::min(n, tape.size());
        std::memcpy(buf, tape.data(), take);
        tape.erase(0, take);
        return static_cast<ssize_t>(take);
    }

    bool write(std::string_view) override { return true; }
    void resize(int rows, int cols) override {
        ++resizes;
        last_rows = rows;
        last_cols = cols;
    }
    bool alive() const override { return true; }
    bool hasBuffered() const override { return !tape.empty(); }
    bool answersQueries() const override { return true; }
    void stopProcess(bool) override {}
    void release() override {}

private:
    int fds_[2] = {-1, -1};
};

// A screen that has been scrolled full and then taken over by a full-screen program, as a
// snapshot. The history is what makes the snapshot big enough to be split across reads.
std::string bigAlternateSnapshot(int history_rows) {
    kai::Screen screen(24, 80);
    for (int i = 0; i < history_rows; ++i)
        screen.feed("scrollback line " + std::to_string(i) + "\r\n");
    screen.feed("\x1b[?1049h"); // a program takes the whole screen
    screen.feed("\x1b[H");
    screen.feed("FULL-SCREEN PROGRAM");
    return kai::screenSnapshot(screen);
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const std::string snapshot = bigAlternateSnapshot(600);
    // The whole premise is that this snapshot is too big for one read, so a window that
    // feeds read-by-read would split it. If it were not, the test would prove nothing.
    expect("the snapshot is larger than one read", snapshot.size() > 4096);
    expect("and it leaves then re-enters the alternate screen",
           snapshot.find("\x1b[?1049l") != std::string::npos &&
               snapshot.find("\x1b[?1049h") != std::string::npos);

    auto* tape = new TapeBackend();
    kai::TerminalWidget window(24, 80, std::unique_ptr<kai::TerminalBackend>(tape));
    window.resize(900, 560);
    window.show();
    pump(150);

    // First, a bare enter of the alternate screen, so the window is settled inside a
    // full-screen program before the snapshot arrives. Whatever geometry that causes is
    // the baseline the snapshot must not disturb.
    tape->tape = "\x1b[?1049h";
    expect("the window enters the alternate screen",
           waitFor([&] { return window.screen().inAlternateScreen(); }));
    pump(200);
    const int baseline_resizes = tape->resizes;
    const int settled_rows = window.rows();
    const int settled_cols = window.cols();

    // Now the whole snapshot, which the reader will find far bigger than one read.
    tape->tape = snapshot;
    expect("the reader consumes the whole snapshot", waitFor([&] { return tape->tape.empty(); }));
    // Give any (buggy) follow-on resize loop a chance to spin before judging.
    pump(600);

    // The snapshot described the same screen the window was already showing: still inside
    // the program, still the same grid. A window that applied it in pieces would have
    // dropped to the primary buffer mid-way and resized, then resized again on re-entry —
    // so the whole point is that nothing changed.
    expect("the window is still showing the full-screen program",
           window.screen().inAlternateScreen());
    expect("the snapshot caused no resize", tape->resizes == baseline_resizes);
    expect("and the grid is the size it settled at",
           window.rows() == settled_rows && window.cols() == settled_cols);
    // The content is there too, so "no resize" was not just "nothing was applied".
    expect("and the program's screen was painted",
           window.screen().text().find("FULL-SCREEN PROGRAM") != std::string::npos);
    expect("with the scrollback carried along", window.screen().inAlternateScreen() &&
                                                    window.scrollOffset() == 0);

    window.discardOnClose();
    window.shutdown();

    if (failures) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "PASS: " << checks << " snapshot resize checks passed\n";
    return 0;
}
