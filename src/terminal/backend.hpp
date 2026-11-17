#pragma once

// What the terminal window talks to, instead of a pty specifically.
//
// The window's job is to turn bytes into a screen and keystrokes into bytes. Where
// those bytes come from — a child process on the other end of a pseudo-terminal, or a
// session owned by a daemon across a socket — is a different question, and keeping it
// separate is what lets a window show a session that outlived the process that started
// it.
//
// Two differences are worth naming, because they are the ones that would otherwise be
// got wrong:
//
// A pty has a child to notice leaving, and reaping it is part of knowing the shell is
// gone. A socket does not: the daemon says so in a frame. Hence the two-part "is the
// far end still there" question rather than one guess.
//
// A pty is a terminal, so it answers the queries programs send — what terminal is
// this, where is the cursor. A window showing a remote session is not: the daemon
// already answered, and answering again would hand the program two replies and let it
// take the wrong one.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "../process/pty.hpp"
#include "../terminal/screen.hpp"

namespace kai {

class TerminalBackend {
public:
    virtual ~TerminalBackend() = default;

    // The descriptor to wait on for output.
    virtual int descriptor() const = 0;

    // Output bytes, already stripped of whatever framing the transport uses. Zero
    // means the far end is finished; negative means an error that is not EINTR.
    virtual ssize_t read(char* buf, std::size_t n) = 0;

    // Keystrokes and paste, on their way to the program.
    virtual bool write(std::string_view bytes) = 0;

    virtual void resize(int rows, int cols) = 0;
    virtual bool alive() const = 0;

    // A local shell is a child, and only collecting it says whether it left. A remote
    // session has no child here, so its transport says so itself and these stay off.
    virtual bool collectsChild() const { return false; }
    virtual bool collect() { return false; }

    // Whether this side is the terminal a program is asking questions of.
    virtual bool answersQueries() const { return true; }

    // A program inside asked for its text to be copied. Locally the model notices;
    // remotely the daemon does and passes it on.
    virtual std::vector<Screen::ClipboardRequest> takeClipboardRequests() {
        return {};
    }

    // Make the far end go away, then let go of the descriptor. Split because a thread
    // may be blocked reading it: the descriptor goes last, once that has finished.
    virtual void stopProcess() {}
    virtual void release() {}
};

// A shell started here, on the other end of a pseudo-terminal.
class PtyBackend final : public TerminalBackend {
public:
    PtyBackend(int rows, int cols, std::string shell = {}) : pty_(rows, cols, std::move(shell)) {}

    int descriptor() const override { return pty_.master(); }

    ssize_t read(char* buf, std::size_t n) override { return pty_.read(buf, n); }

    bool write(std::string_view bytes) override {
        return pty_.write(std::string(bytes));
    }

    void resize(int rows, int cols) override { pty_.resize(rows, cols); }
    bool alive() const override { return pty_.alive(); }
    bool collectsChild() const override { return true; }
    bool collect() override { return pty_.reap(); }

    void stopProcess() override { pty_.terminate(); }
    void release() override { pty_.closeMaster(); }

    const Pty& pty() const { return pty_; }

private:
    Pty pty_;
};

} // namespace kai
