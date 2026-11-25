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

#include "../process/job.hpp"
#include "../process/pty.hpp"
#include "../terminal/screen.hpp"
#include "../terminal/selection.hpp"

namespace kai {

class TerminalBackend {
  public:
    // What `read` gives back when the program at the other end is simply quiet and there is
    // nothing to hand over — which is not the same as its having gone away, and must not be
    // answered by closing the window. A session idle at a prompt is the ordinary case for
    // this, and it is also the case in which a tab bar change or a refusal is all that has
    // happened.
    static constexpr ssize_t kNothingRightNow = -2;

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

    // Whether `read` would give bytes right now, without anything arriving on the
    // descriptor. A session's first screen comes with the handshake that opens the
    // connection and is held here afterwards, so there is nothing left for a wait on
    // the socket to notice - and a reader that waited anyway would sit out the whole
    // screen until something else made the far end talk.
    virtual bool hasBuffered() const { return false; }

    // The bytes a backend is already holding, without consuming them. A terminal asked to
    // show a session has that session's screen in hand the moment the connection is made,
    // and a widget that is not shown yet does not go looking at its own posted events — so
    // the first screen goes to the model directly, on the GUI thread, instead of waiting
    // for a reader thread to tell a window about something it is already holding.
    virtual std::string heldBytes() const { return {}; }
    virtual void clearHeldBytes() {}

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
    //
    // `closing` says the view is going away for good and what it was showing was never
    // saved. For a shell running here that changes nothing — the process goes either
    // way. For a session owned by the daemon it is the whole question: a view that
    // merely stops watching leaves the shell alone, and a view that is being closed
    // takes an unsaved shell with it.
    // The session's tab bar, when it has changed since the last time it was asked for.
    // A window holding a shell started here has no tab bar to speak of — one program, one
    // screen — and says so by never filling any of this in.
    struct TabBar {
        int panes = 0;   // how many places there are to look at
        int focused = 0; // the one a view lands on when it arrives
        // Which place *this* view is being shown, which is not the same question: two
        // windows on one session look at two places, and a place closing moves the view
        // that was on it without anyone asking. -1 means nobody said, which is what a
        // window with a shell of its own hears, having no session to be told by.
        int mine = -1;
        bool tabs = false; // one at a time with tabs, rather than all of them at once
        // Where each place sits, in cells, in the same order as the places. Empty when the
        // session has said nothing about sizes, which is the same as saying they are equal.
        std::vector<PaneRect> cells;
        // Which number each place is called by, in the same order as the places. A place is
        // numbered by when it was made, not by where it ended up, so the number on a tab
        // outlives the splits made around it. Empty means the session said nothing, which
        // is the same as numbering them in the order they are listed.
        std::vector<int> numbers;
    };
    virtual bool takeTabBar(TabBar& /*bar*/) { return false; }

    // "Show me that pane." Only a session has panes to look between; a window holding a
    // shell of its own is already looking at the only one there is.
    virtual void showPane(int /*pane*/) {}

    // Another place to work, the place in hand ended, and a different way of looking at the
    // places there are. Only a session has places; a window holding a shell started here has
    // the one it is showing, and says nothing when asked.
    // The last thing the session refused to do, in its own words, taken once so that a
    // message somebody has already read is not read again. Only a session can disagree; a
    // window holding a shell started here has nobody to argue with.
    virtual bool takeRefusal(std::string& out) {
        out.clear();
        return false;
    }

    // Make another place. `side_by_side` says which way to divide the place being looked
    // at: side by side puts a vertical divider (a place to the right), otherwise a
    // horizontal one stacks a place underneath. The default is the vertical, side-by-side
    // split, which is what a lone "new" key has always meant.
    virtual void newPane(bool side_by_side = true) {}
    virtual void closePane() {}
    virtual void setViewMode(bool /*tabs*/) {}
    // A hand resize, as a little session: begin (remember for a cancel), move the divider
    // a cell along one axis, and end keeping or undoing it.
    virtual void resizeBegin() {}
    virtual void resizeDivider(int /*dcol*/, int /*drow*/) {}
    virtual void resizeEnd(bool /*commit*/) {}

    virtual void stopProcess(bool /*closing*/ = false) {}
    virtual void release() {}

    // The program running out front, when it is not just the shell, or empty when this
    // kind of backend cannot say. A window showing a daemon's session asks the daemon,
    // which is where the shell actually is.
    virtual std::string foregroundJobName() const { return {}; }
};

// A shell started here, on the other end of a pseudo-terminal.
class PtyBackend final : public TerminalBackend {
public:
    PtyBackend(int rows, int cols, std::string shell = {}, std::string cwd = {})
        : pty_(rows, cols, std::move(shell), std::move(cwd)) {}

    int descriptor() const override { return pty_.master(); }

    ssize_t read(char* buf, std::size_t n) override { return pty_.read(buf, n); }

    bool write(std::string_view bytes) override {
        return pty_.write(std::string(bytes));
    }

    void resize(int rows, int cols) override { pty_.resize(rows, cols); }
    bool alive() const override { return pty_.alive(); }
    bool collectsChild() const override { return true; }
    bool collect() override { return pty_.reap(); }

    // Nothing here is worth keeping after the window goes: a local shell has no
    // session to detach into, which is exactly why saving is what moves one to the
    // daemon in the first place.
    void stopProcess(bool /*closing*/) override { pty_.terminate(); }
    void release() override { pty_.closeMaster(); }

    std::string foregroundJobName() const override {
        return foregroundJob(pty_.master(), pty_.child());
    }

    const Pty& pty() const { return pty_; }

private:
    Pty pty_;
};

} // namespace kai
