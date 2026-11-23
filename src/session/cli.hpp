#pragma once

// The client's side of the daemon: `kai-term list`, `kai-term attach 2`, the other
// words that come straight after the program, and the same operations for a window
// that needs a shell to show.
//
// Kept Qt-free on purpose. The daemon does not need any of it, and a test that wants to
// know whether the command line works should not have to start a display to find out.
// The window uses the operations at the bottom of the file, and shares them with the
// command line rather than keeping a second set of answers about what the daemon will
// do and when it will refuse.

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <memory>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <pwd.h>
#include <unistd.h>
#include <vector>

#include "../ipc/protocol.hpp"
#include "../ipc/socket.hpp"
#include "../process/pty.hpp"  // for kDaemonMarkerEnv, said here and unsaid there
#include "daemon.hpp"

namespace kai {

// The shell starts where a person expects a shell to start: the user's home,
// not whatever directory happened to launch the window. A session created by
// `kai-term new` or by a window's Ctrl+Shift+S follows the same rule, and an
// explicit `--cwd` still wins.
inline std::string homeDirectory() {
    const struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir && *pw->pw_dir)
        return pw->pw_dir;
    const char* home = std::getenv("HOME");
    if (home && *home)
        return home;
    return "/";
}

inline constexpr int kDaemonReadyWaitMs = 3000;

// Say what this process is, in the words a process list shows.
//
// The name a process carries — the `comm` that `ps -e`, btop's Program column and
// every `pkill -x` work from — is taken by the kernel from the last part of the path
// the program was exec'd by, and the daemon is exec'd by `/proc/self/exe` so that it
// does not depend on where the binary happened to be found. The last part of that
// path is `exe`, so the process holding every shell anybody kept introduced itself
// to btop as "exe", the single least informative word available. argv[0] already
// says better — that is the Command column — but argv[0] is not what `comm` reads.
// The task name is the one thing settable after the exec, and fifteen characters is
// all it holds, which is why the daemon is `kai-term-daemon` and not a sentence.
inline void nameSelfForProcessLists(const char* name) {
    ::prctl(PR_SET_NAME, name, 0, 0, 0);
}

// Bytes out to the person's terminal. Not the socket helper: `send()` on a pipe or a
// redirected file fails with ENOTSOCK, which would mean an attach printing nothing at
// all to anyone whose output is not a terminal — exactly the case that matters to a
// script.
inline bool writeStdout(std::string_view bytes) {
    std::size_t off = 0;
    while (off < bytes.size()) {
        const ssize_t n = ::write(STDOUT_FILENO, bytes.data() + off, bytes.size() - off);
        if (n > 0) {
            off += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        return false;
    }
    return true;
}

// What the terminal in front of us is, so a session starts at a size that matches the
// window it was started from rather than a number from 1978.
inline void terminalSize(int& rows, int& cols) {
    rows = 24;
    cols = 80;
    struct winsize ws {};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        rows = ws.ws_row;
        cols = ws.ws_col;
    }
}

// Ask the daemon whether anyone is home.
inline bool daemonAlive() {
    DaemonClient client;
    std::string error;
    if (!client.connect(error))
        return false;
    if (!client.request({{"command", "ping"}}))
        return false;
    Frame reply;
    return client.nextFrame(reply, error) && reply.kind == MsgKind::Welcome;
}

// Which process is holding the daemon's socket, in the daemon's own words. Empty when
// there is nothing useful to say — no file, an unreadable one, a number that is not
// running — because a note about a daemon is only worth reading when it is true. The
// socket is asked first: the pid file outlives a daemon that was killed, and a listener
// actually answering is the whole reason a process number is worth naming.
inline std::string liveDaemonPid() {
    const std::string socket = daemonSocketPath();
    if (socket.empty() || !socketHasListener(socket))
        return {};
    const int fd = ::open((socket + ".pid").c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return {};
    char digits[24] = {};
    const ssize_t n = ::read(fd, digits, sizeof(digits) - 1);
    ::close(fd);
    if (n <= 0)
        return {};
    const long pid = std::strtol(digits, nullptr, 10);
    if (pid <= 0 || ::kill(static_cast<pid_t>(pid), 0) != 0)
        return {};
    return std::to_string(pid);
}

// Whether this process was started in order to be the daemon. See kDaemonMarkerEnv.
inline bool weAreTheDaemon() {
    const char* marker = std::getenv(kDaemonMarkerEnv);
    return marker != nullptr && *marker != '\0';
}

// The file a daemon launch is guarded by, next to the socket it guards. It is never
// removed, and it does not need to be: what a lock is is the inode, and what matters
// about it is who is holding it.
inline std::string daemonLaunchLockPath() {
    const std::string socket = daemonSocketPath();
    return socket.empty() ? std::string() : socket + ".lock";
}

// The right to start a daemon, held for as long as one takes to start.
//
// `ensureDaemon` asks a question — is anyone home? — and starts a daemon when the
// answer is no. Asked twice in the same moment, the question gets the same answer
// twice, and each launcher forks; the socket has room for one winner, so the others
// get as far as "a daemon is already listening" and the only thing they contributed
// was processes. A daemon that is alive but slow to answer looks exactly like no
// daemon at all, which is how a loaded machine turns one question into a queue of
// them. The lock makes the asking and the starting one gesture: whoever comes second
// waits, and then finds the daemon the first one made, which is the answer it wanted.
class DaemonLaunchLock {
public:
    DaemonLaunchLock() { acquire(); }
    DaemonLaunchLock(const DaemonLaunchLock&) = delete;
    DaemonLaunchLock& operator=(const DaemonLaunchLock&) = delete;
    ~DaemonLaunchLock() {
        if (fd_ >= 0) {
            ::flock(fd_, LOCK_UN);
            ::close(fd_);
        }
    }

    bool held() const { return fd_ >= 0; }
    // True when it came behind somebody else rather than taking it outright, which is
    // the difference between launching a daemon and joining a launch.
    bool waited() const { return waited_; }

private:
    void acquire() {
        const std::string path = daemonLaunchLockPath();
        if (path.empty() || !makeSocketDirectory(path))
            return;
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd_ < 0)
            return;
        if (::flock(fd_, LOCK_EX | LOCK_NB) == 0)
            return;
        for (int slept = kDaemonPollMs; slept <= kDaemonReadyWaitMs; slept += kDaemonPollMs) {
            ::usleep(kDaemonPollMs * 1000);
            if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) {
                waited_ = true;
                return;
            }
        }
        // Somebody has been launching for three seconds and still is. Starting a
        // second daemon would not beat them; it would only be a second loser.
        ::close(fd_);
        fd_ = -1;
    }

    int fd_ = -1;
    bool waited_ = false;
};

// Defined below, next to the loop it wraps.
inline int serveDaemonForeground(std::string& error);

// Start a daemon in the background. The child takes a session of its own so closing
// the window that started it cannot send it a SIGHUP; its output goes nowhere, since
// there is nobody left to read it and a daemon that dies writing to a closed pipe
// would be a poor way to keep a shell alive.
inline bool startDaemon(std::string& error) {
    // The rule that keeps a launch from becoming a chain: a process that exists in
    // order to be the daemon does not go and look for one. `kai-term serve` typed by
    // hand is under it too, because `serveDaemonForeground` says so before it binds.
    if (weAreTheDaemon()) {
        error = std::string("this process is the daemon; it does not start another one") +
                " (it carries " + kDaemonMarkerEnv + ")";
        return false;
    }
    const DaemonLaunchLock launch;
    if (!launch.held()) {
        error = "another command is starting the daemon; try again in a moment";
        return false;
    }
    // Whoever held the lock may have finished the job while we were queueing, and a
    // daemon that answers is a daemon there is no reason to start.
    if (daemonAlive())
        return true;
    const pid_t pid = ::fork();
    if (pid < 0) {
        error = std::string("cannot start a daemon: ") + std::strerror(errno);
        return false;
    }
    if (pid == 0) {
        ::setsid();
        const int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            ::dup2(devnull, STDIN_FILENO);
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO)
                ::close(devnull);
        }
        // Become a `serve` process rather than staying a copy of the command
        // that happened to start us. A background process that shows up in `ps` as
        // `kai-term new work` cannot be found when someone wants to know what is
        // holding their shells, and cannot be reached by anything that looks at a
        // process list. The session number goes too: a daemon must not inherit
        // somebody's session and hand it to every shell it later starts.
        ::unsetenv("KAI_SESSION");
        // And this goes the other way: from here on we are the daemon, whatever
        // `/proc/self/exe` turns out to be when it runs. If it is not kai-term after
        // all — a test tool built from these sources, say — it inherits the reason
        // it was started, and cannot answer that reason by starting another daemon
        // the way the tool that re-ran itself did.
        ::setenv(kDaemonMarkerEnv, "1", 1);
        char* const serve[] = {const_cast<char*>("kai-term"), const_cast<char*>("serve"),
                               nullptr};
        ::execv("/proc/self/exe", serve);
        _exit(127);
    }
    for (int waited = 0; waited < kDaemonReadyWaitMs; waited += 50) {
        if (daemonAlive()) {
            // Ours, or one started by a command run in the same moment; either way
            // there is something to talk to. It is not waited for: that would be
            // waiting for the sessions to end.
            return true;
        }
        int status = 0;
        if (::waitpid(pid, &status, WNOHANG) == pid) {
            // It gave up instead of answering, and its reason went to /dev/null along
            // with the rest of its output. That trade is right for a background
            // process; it just means the way to see the reason has to be said.
            const std::string holder = liveDaemonPid();
            error = holder.empty()
                        ? "the daemon could not start; run `kai-term serve` to see why"
                        : "a daemon is already listening, at pid " + holder;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // Something may be listening and simply not answering — a daemon stopped mid-step,
    // or one so loaded it cannot get a thread together. That is the case a second
    // daemon cannot fix, so say who to deal with rather than who to start.
    const std::string holder = liveDaemonPid();
    error = "the daemon did not answer within " + std::to_string(kDaemonReadyWaitMs / 1000) +
            " seconds" +
            (holder.empty() ? std::string() : "; pid " + holder + " is listening on it");
    return false;
}

inline bool ensureDaemon(std::string& error) {
    if (daemonAlive())
        return true;
    return startDaemon(error);
}

// What a listing should mention. A shell sitting in an open window is somebody's
// typing, not a session they saved, and a list full of other people's windows is a list
// nobody can act on. Shells left behind by a window that went away unasked are the
// exception: they are recoverable, and this is the only place to find out about them.
inline std::vector<SessionInfo> worthListing(const std::vector<SessionInfo>& sessions) {
    std::vector<SessionInfo> out;
    for (const SessionInfo& info : sessions) {
        if (info.saved || info.clients == 0)
            out.push_back(info);
    }
    return out;
}

// Whether a shell is one the person decided to keep, one they are looking at, or one a
// window left behind when it went away without being asked to.
inline std::string sessionState(const SessionInfo& info) {
    if (!info.alive)
        return "exiting";
    if (info.saved)
        return "running";
    return info.clients > 0 ? "in a window" : "left behind";
}

// Which session this process is standing in, as the daemon wrote it into the
// shell's environment; empty when the shell is not one of ours.
inline std::string currentSessionHandle() {
    const char* id = std::getenv("KAI_SESSION");
    return id && *id ? std::string(id) : std::string();
}

inline void printSessionTable(const std::vector<SessionInfo>& sessions, int here = -1) {
    if (sessions.empty()) {
        std::cout << "no saved sessions\n";
        return;
    }
    // Id order rather than name order. Names sort w1, w10, w11, w2, which is alphabetical
    // and no use at all as a history; an id is handed out in the order the shells started,
    // so reading the list from the top is reading the afternoon in order.
    std::vector<SessionInfo> rows = sessions;
    std::sort(rows.begin(), rows.end(),
              [](const SessionInfo& a, const SessionInfo& b) { return a.id < b.id; });
    std::cout << std::right << std::setw(5) << "ID" << std::left << std::setw(17) << "NAME"
              << std::right << std::setw(8) << "PID"
              << std::setw(9) << "SIZE" << std::setw(7) << "VIEWS" << std::setw(11) << "RUNNING"
              << std::left << "  STARTED"
              << "  STATUS\n";
    for (const SessionInfo& info : rows) {
        const std::time_t when = static_cast<std::time_t>(info.started);
        std::tm local {};
        ::localtime_r(&when, &local);
        char stamp[32] = {0};
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
        // The row for the session you are standing in carries a star, the way a
        // shell marks its own jobs: `save` and `detach` mean this session when you
        // type them here, and the listing should agree with what they will do.
        const std::string id_cell = info.id == here ? "*" + std::to_string(info.id)
                                                    : std::to_string(info.id);
        std::cout << std::right << std::setw(5) << id_cell << std::left << std::setw(17)
                  << info.name << std::right << std::setw(8)
                  << (info.pid > 0 ? std::to_string(static_cast<long long>(info.pid)) : "-")
                  << std::setw(9) << (std::to_string(info.rows) + "x" + std::to_string(info.cols))
                  << std::setw(7) << info.clients << std::setw(11)
                  << (info.foreground.empty() ? "-" : info.foreground) << std::left << "  "
                  << stamp << "  " << sessionState(info) << "\n";
    }
}

// The parts of a terminal a program needs to leave alone. ISIG stays off: Ctrl-C
// belongs to the shell on the other end of the session, not to us, and a person
// copying that habit from a local terminal would be surprised if attaching sent a
// signal to the window instead of interrupting a program in the session.
class RawTerminal {
public:
    explicit RawTerminal(int fd = STDIN_FILENO) : fd_(fd) {
        if (::tcgetattr(fd_, &saved_) != 0)
            return;
        struct termios raw = saved_;
        ::cfmakeraw(&raw);
        raw.c_iflag |= ICRNL; // a person pressing Enter means carriage return
        raw.c_lflag &= ~static_cast<tcflag_t>(ISIG);
        if (::tcsetattr(fd_, TCSADRAIN, &raw) != 0)
            return;
        applied_ = true;
    }

    ~RawTerminal() { restore(); }

    bool applied() const { return applied_; }

    void restore() {
        if (!applied_)
            return;
        applied_ = false;
        static_cast<void>(::tcsetattr(fd_, TCSADRAIN, &saved_));
    }

private:
    int fd_ = STDIN_FILENO;
    struct termios saved_ {};
    bool applied_ = false;
};

inline volatile sig_atomic_t g_resize_requested = 0;

// A signal handler cannot do much: it has to be reentrant, and the daemon is a table
// of sessions with threads and descriptors in it. So the handler says only "stop",
// and the accept loop notices between one connection and the next.
inline Daemon* g_running_daemon = nullptr;

inline void daemonStopSignal(int) {
    if (g_running_daemon)
        g_running_daemon->requestStop();
}

// Sit in the accept loop until asked to leave, then take the sessions with us. Used
// by `serve` in the foreground and by the background daemon a window or
// `new` starts.
inline int serveDaemonForeground(std::string& error) {
    // Said before binding, because whoever starts life as `serve` *is* the daemon,
    // whether a person typed it or `startDaemon` exec'd it, and the rule that a daemon
    // does not start a daemon cannot depend on which of the two it was.
    ::setenv(kDaemonMarkerEnv, "1", 1);
    Daemon daemon;
    if (!daemon.start(error)) {
        error += " (" + daemonSocketPath() + ")";
        // The other half of the answer: refusing to bind over a live daemon is right,
        // and on its own it leaves whoever read it looking for the process.
        const std::string holder = liveDaemonPid();
        if (!holder.empty())
            error += ", held by pid " + holder;
        return 1;
    }
    g_running_daemon = &daemon;
    ::signal(SIGTERM, daemonStopSignal);
    ::signal(SIGINT, daemonStopSignal);
    ::signal(SIGPIPE, SIG_IGN); // a window that goes away mid-write must not kill us
    daemon.run();
    g_running_daemon = nullptr;
    daemon.stop();
    return 0;
}

// Attach, and be a window until the session or the person leaves.
inline int attachSession(const std::string& name, std::string& error) {
    DaemonClient client;
    if (!client.connect(error))
        return 1;

    int rows = 24;
    int cols = 80;
    terminalSize(rows, cols);
    if (!client.request({{"command", "attach"},
                         {"name", name},
                         {"rows", std::to_string(rows)},
                         {"cols", std::to_string(cols)}})) {
        error = "the daemon would not listen";
        return 1;
    }
    Frame welcome;
    if (!client.nextFrame(welcome, error))
        return 1;
    if (welcome.kind == MsgKind::Error) {
        error = fieldValue(decodeFields(welcome.body), "message", "the daemon refused");
        return 1;
    }
    if (welcome.kind != MsgKind::Welcome) {
        error = "the daemon did not welcome us";
        return 1;
    }
    const Fields view = decodeFields(welcome.body);
    const std::string view_id = fieldValue(view, "id", "?");
    const std::string view_name = fieldValue(view, "name", name);

    // Only worth doing when there is a terminal to look after. Piped in, the bytes go
    // through as they are, which is what a script expects.
    const bool tty = ::isatty(STDIN_FILENO) != 0;
    std::unique_ptr<RawTerminal> raw;
    if (tty) {
        raw = std::make_unique<RawTerminal>();
        ::signal(SIGWINCH, [](int) { g_resize_requested = 1; });
        // Say once, before the session's screen takes the terminal over, which
        // session this is and how to get out. It is a notice, not a persistent
        // bar: a raw byte pipe cannot hold a row against a full-screen program
        // that places its cursor by absolute address, so a real status line wants
        // the view to emulate the screen rather than relay it. For now the one key
        // a person needs is put in front of them on the way in, and for a session
        // that only prints lines the notice stays in the scrollback to be found.
        std::fprintf(stderr,
                     "kai-term: viewing session %s (%s) — Ctrl+S leaves it running; "
                     "`kai-term attach %s` comes back\r\n",
                     view_id.c_str(), view_name.c_str(), view_id.c_str());
        std::fflush(stderr);
    }

    bool first_screen = true;
    auto show = [&first_screen](const Frame& frame) {
        if (frame.kind != MsgKind::Data && frame.kind != MsgKind::Snapshot)
            return false;
        std::string bytes = frame.body;
        if (first_screen && frame.kind == MsgKind::Snapshot) {
            // Clear what the command line left behind, so the session's screen is not
            // sitting underneath a prompt.
            bytes = "\x1b" "[H\x1b" "[2J" + bytes;
        }
        first_screen = false;
        static_cast<void>(writeStdout(bytes));
        return true;
    };

    Frame frame;
    int status = 0;
    bool finished = false;
    bool keys_done = false;
    bool left = false; // Ctrl+S, not the session ending
    while (!finished) {
        if (g_resize_requested) {
            g_resize_requested = 0;
            int new_rows = 0;
            int new_cols = 0;
            terminalSize(new_rows, new_cols);
            static_cast<void>(client.send(
                MsgKind::Resize,
                encodeFields({{"rows", std::to_string(new_rows)}, {"cols", std::to_string(new_cols)}})));
        }
        struct pollfd fds[2] = {{STDIN_FILENO, POLLIN, 0}, {client.descriptor(), POLLIN, 0}};
        // A read can hand over more than one frame, so when any is still buffered the
        // wait has to be no wait at all. Polling for two hundred milliseconds with a
        // frame already in hand is how output starts arriving in batches.
        const int ready = ::poll(fds, keys_done ? 1 : 2, client.buffered() ? 0 : 200);
        if (ready < 0 && errno != EINTR)
            break;

        if (fds[1].revents != 0 || client.buffered()) {
            bool hung_up = false;
            // Only ask the socket when it says there is something there. It is a
            // blocking descriptor, and a read started because a frame is *already*
            // buffered would sit waiting for a second frame that may never come.
            if (fds[1].revents != 0 && !client.readNow(hung_up))
                break;
            while (client.pop(frame)) {
                if (frame.kind == MsgKind::Exit) {
                    finished = true; // the session ended; this terminal is ours again
                    break;
                }
                show(frame);
            }
            if (hung_up)
                break;
        }

        if (!keys_done && fds[0].revents != 0) {
            char buf[4096];
            const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
            if (n > 0) {
                const std::string in(buf, static_cast<std::size_t>(n));
                // Ctrl+S on its own leaves the view and leaves the shell running.
                // A terminal cannot tell Ctrl+S from Ctrl+Shift+S — a control
                // letter arrives as one byte and the Shift is not in it — so in an
                // attached view, where there is no window to bind the shifted form
                // to, the plain byte is the way out. Raw mode has already switched
                // flow control off, so this steals nothing a program here could use
                // as XOFF; and only a Ctrl+S that arrives alone leaves, so a paste
                // that happens to carry the byte is handed over whole.
                if (tty && in == std::string(1, '\x13')) {
                    // No `discard`: leaving this view is "stop showing it to me",
                    // which the daemon reads as a keep, not a destruction.
                    static_cast<void>(client.send(MsgKind::Detach, encodeFields({})));
                    left = true;
                    break;
                }
                if (!client.send(MsgKind::Data, in)) {
                    error = "the daemon stopped listening";
                    status = 1;
                    break;
                }
                continue;
            }
            // Nothing more to type. From a terminal that means the person pressed
            // Ctrl-D in a shell that read it before we did, which the shell handles;
            // from a pipe it means the script is finished, and the only sensible
            // reading is to let the output that is on its way arrive and then let go.
            // Someone automating this has `session send` for typing without watching.
            keys_done = true;
            if (!tty) {
                for (int drained = 0; drained < 15 && !finished; ++drained) {
                    struct pollfd out {client.descriptor(), POLLIN, 0};
                    if (::poll(&out, 1, 20) <= 0 && !client.buffered())
                        continue;
                    bool hung_up = false;
                    if (!client.readNow(hung_up))
                        break;
                    while (client.pop(frame)) {
                        if (frame.kind == MsgKind::Exit)
                            finished = true;
                        else
                            show(frame);
                    }
                    if (hung_up)
                        break;
                }
                finished = true; // a script is done typing; the session carries on
            }
        }
    }
    if (raw) {
        raw->restore();
        static_cast<void>(writeStdout("\r\n"));
    }
    if (left) {
        // The terminal is the caller's again. Say what happened and where the
        // shell went, because the natural fear after leaving a session in a
        // hurry is that leaving killed it.
        std::fprintf(stderr,
                     "kai-term: detached from session %s; it is still running — "
                     "`kai-term list` shows it\n",
                     view_id.c_str());
    }
    ::signal(SIGWINCH, SIG_DFL);
    return status;
}

inline std::vector<SessionInfo> listSessions(std::string& error) {
    DaemonClient client;
    std::vector<SessionInfo> none;
    if (!client.connect(error))
        return none;
    Frame reply;
    if (!client.command({{"command", "list"}}, reply, error))
        return none;
    if (reply.kind != MsgKind::SessionList) {
        error = fieldValue(decodeFields(reply.body), "message", "the daemon answered oddly");
        return none;
    }
    for (const Fields& fields : decodeRecords(reply.body))
        none.push_back(decodeSessionInfo(fields));
    return none;
}

// A name nobody is using, so `kai-term new` without one still does the obvious thing.
// Windows and typed commands pick from different ranges so that a window's shell is
// recognisable as one, and cannot be handed the name a person meant for a session.
inline std::string freeName(const std::vector<SessionInfo>& sessions,
                            const std::string& prefix = "s") {
    for (int n = 1; n < 10000; ++n) {
        const std::string candidate = prefix + std::to_string(n);
        const bool taken = std::any_of(sessions.begin(), sessions.end(),
                                       [&](const SessionInfo& s) { return s.name == candidate; });
        if (!taken)
            return candidate;
    }
    return {};
}

// Turn what a person typed — a number from a listing, or a name — into the name
// a window needs. The daemon resolves both on the wire; a window attaches by
// name because the name is what its title bar says and what survives looking the
// session up again later. Name wins over number, the same way the daemon's own
// lookup lets a session called "7" keep meaning itself.
inline std::string resolveHandle(const std::string& handle, std::string& error) {
    const std::vector<SessionInfo> sessions = listSessions(error);
    if (!error.empty())
        return {};
    for (const SessionInfo& info : sessions)
        if (info.name == handle)
            return info.name;
    if (!handle.empty() && handle.find_first_not_of("0123456789") == std::string::npos)
        for (const SessionInfo& info : sessions)
            if (info.id == std::atoi(handle.c_str()))
                return info.name;
    error = "no such session: " + handle;
    return {};
}

// ---------------------------------------------------------------------------
// The same operations for a program rather than a typed command.
//
// A window has to get a shell started, be told which ones were kept, keep the one it
// is showing and throw one away. Those are the verbs above with the parsing taken out;
// they live here so a window and a command line cannot drift into disagreeing about
// what the daemon will accept.
// ---------------------------------------------------------------------------

// One request, one answer. Anything the daemon refuses comes back in `error`, already
// in the words the daemon chose, because a window that invents its own message about a
// refusal would be a window that is wrong about its own daemon.
inline bool daemonCommand(const Fields& command, std::string& error) {
    DaemonClient client;
    if (!client.connect(error))
        return false;
    Frame reply;
    if (!client.command(command, reply, error))
        return false;
    if (reply.kind == MsgKind::Ack)
        return true;
    error = fieldValue(decodeFields(reply.body), "message", "the daemon refused");
    return false;
}

inline bool saveSession(const std::string& name, const std::string& as, std::string& error) {
    return daemonCommand({{"command", "save"}, {"name", name}, {"as", as}}, error);
}

inline bool discardSession(const std::string& name, std::string& error) {
    return daemonCommand({{"command", "discard"}, {"name", name}}, error);
}

// The kept sessions, in the order a menu should show them: oldest first, so the list
// does not rearrange itself under the pointer every time something is saved.
inline std::vector<SessionInfo> savedSessions(std::string& error) {
    std::vector<SessionInfo> kept;
    for (const SessionInfo& info : listSessions(error)) {
        if (info.saved)
            kept.push_back(info);
    }
    std::stable_sort(kept.begin(), kept.end(), [](const SessionInfo& a, const SessionInfo& b) {
        return a.started < b.started;
    });
    return kept;
}

// Start the shell a window will show: unsaved, because it is the shell in that window
// and nobody has asked to keep it yet, and named from the range no typed command uses.
// The daemon is started if nothing is running, which is the only way a window can hold
// on to a shell past its own closing.
inline std::string startWindowSession(int rows, int cols, std::string& error) {
    if (!ensureDaemon(error))
        return {};
    const std::vector<SessionInfo> existing = listSessions(error);
    const std::string name = freeName(existing, "w");
    if (name.empty()) {
        error = "every session name up to w9999 is taken";
        return {};
    }
    if (!daemonCommand({{"command", "new"},
                        {"name", name},
                        {"rows", std::to_string(rows)},
                        {"cols", std::to_string(cols)},
                        {"saved", "0"},
                        {"cwd", homeDirectory()}},
                       error))
        return {};
    return name;
}

// Says, once, if a daemon from another build is holding shells this one cannot see.
// Silence is the wrong default: whoever is reading this output is looking for their
// sessions, and "nothing is running" is only half the truth when something older is
// running under a name this build does not look at.
inline void mentionForeignDaemon() {
    const std::string beside = foreignDaemonNotice();
    if (!beside.empty())
        std::cout << "note: " << beside << "\n";
}

// The help, written once. Every command has one row in the general listing and one
// page of its own, and both are drawn from the same words here: a help text typed out
// twice is two help texts that have not disagreed yet.
// (Kaida, 2026-11-20: "the --help should be rewritten to be clear with 1 argument per
// row following by a little description. for more specific the help command should be
// used with an arguement ex : kai-term list --help".)
struct CommandHelp {
    const char* verb;
    const char* usage;    // what follows the program's name
    const char* summary;  // the one line the general listing shows
    const char* detail;   // the prose only the command's own page carries
    const char* options;  // one row per argument this command takes
};

inline const std::vector<CommandHelp>& commandHelp() {
    static const std::vector<CommandHelp> commands = {
        {"list",
         "list",
         "the shells a daemon is keeping alive; * marks the one you are standing in",
         "Shells opened by a window appear once they have been saved; the number that\n"
         "are open and unsaved is said at the end.\n",
         ""},
        {"new",
         "new [NAME]",
         "start a shell, print its id, and leave you where you were",
         "The shell starts in the directory you are standing in, running the shell your\n"
         "environment names.\n",
         "  NAME                 what to call it; the first free name otherwise\n"},
        {"attach",
         "attach ID | -n NAME [--window]",
         "show one shell in this terminal, or in a window with --window",
         "A shell can be started in a window, picked up in a terminal over ssh, and left\n"
         "running when both go away: each of those is a view of the same shell.\n",
         "  ID                   the number `kai-term list` printed\n"
         "  -n, --name NAME      find it by its name instead of its number\n"
         "  --window             a window onto it, rather than this terminal\n"},
        {"detach",
         "detach",
         "leave the shell you are standing in, without ending it",
         "The shell goes on running with nothing looking at it. Say `save` first if it\n"
         "should outlive the daemon as well.\n",
         ""},
        {"save",
         "save [ID] [NAME]",
         "keep a shell so it outlives the window or terminal holding it",
         "Standing inside the shell, no arguments are needed.\n",
         "  ID                   which shell; the one you are standing in otherwise\n"
         "  NAME                 what to call it\n"},
        {"send",
         "send ID TEXT [--enter] [--pane N]",
         "type into a shell without watching it",
         "What the shell sees is what it would have seen from a keyboard, prompt\n"
         "redrawing itself and all. A shell with more than one pane runs a program\n"
         "in each of them, so `send` says which one it means; without `--pane` it\n"
         "types into the pane that session is looking at.\n",
         "  ID                   which shell\n"
         "  TEXT                 what to type, as one argument\n"
         "  --enter              press Enter after it\n"
         "  --pane N             which pane, counted from 0\n"},
        {"kill",
         "kill ID",
         "end a shell",
         "Whatever window or terminal is watching it finds out the way it would find out\n"
         "about a shell that exited on its own.\n",
         "  ID                   which shell\n"},
        {"layout",
         "layout ID",
         "how a shell is arranged: its tabs, and the panes inside each",
         "The arrangement belongs to the shell rather than to the window showing it, so\n"
         "a second window or a terminal over ssh finds the one that is already there.\n"
         "It is written as words: a `window` line per tab, then that tab's tree of\n"
         "`split` lines and `pane` leaves. Nothing on screen follows these words yet.\n",
         "  ID                   which shell\n"},
        {"set-layout",
         "set-layout ID < FILE",
         "arrange a shell from the words `layout` prints",
         "The whole arrangement is read before any of it is applied, so a file with a\n"
         "typo in it is refused and says which line, and the shell keeps the arrangement\n"
         "it had. Several lines come in the way several lines usually do: redirected.\n",
         "  ID                   which shell\n"},
        {"start",
         "start",
         "start the daemon, if it is not already running",
         "There is one daemon per user and windows start one themselves when they need\n"
         "it, so this is mostly a way of asking whether it is there.\n",
         ""},
        {"shutdown",
         "shutdown",
         "stop the daemon",
         "Shells nobody is looking at, and that were never saved, go with it.\n",
         ""},
        {"serve",
         "serve",
         "run the daemon in this terminal until Ctrl-C",
         "This is what `kai-term start` launches out of sight, and the way to watch a\n"
         "daemon that will not start.\n",
         ""},
    };
    return commands;
}

// One row of a help page: the thing you type, then what it means, lined up.
inline void printHelpRow(const std::string& term, const std::string& text) {
    const std::size_t column = 25;
    std::cout << "  " << term;
    if (term.size() + 2 >= column)
        std::cout << "\n" << std::string(column, ' ');
    else
        std::cout << std::string(column - term.size() - 2, ' ');
    std::cout << text << "\n";
}

// The whole of it. `version` is empty when the caller is not the program itself, and
// then the version line is left out rather than said wrong.
inline int printHelp(const std::string& version = std::string()) {
    if (!version.empty())
        std::cout << "Kai-Term " << version
                  << " — a terminal emulator whose shells outlive their windows\n\n";
    std::cout << "Usage: kai-term [options]                open a window\n"
                 "       kai-term <command> [arguments]    talk to the shells a daemon keeps\n"
                 "\n"
                 "Options\n";
    printHelpRow("-c, --config FILE",
                 "read settings from FILE, and colours from the theme.conf beside it");
    printHelpRow("--config-dir DIR",
                 "read both files from DIR instead of ~/.config/kai-term");
    printHelpRow("-h, --help", "this text; `kai-term <command> --help` answers for one command");
    printHelpRow("--version", "the version, and nothing else");
    std::cout << "\nCommands\n";
    for (const CommandHelp& command : commandHelp())
        printHelpRow(command.usage, command.summary);
    std::cout << "\nSettings live in ~/.config/kai-term — kai-term.conf for how the program behaves,\n"
                 "theme.conf for how it looks — and a window's colours win over a session's own.\n"
                 "Inside a window, Ctrl+Shift+S keeps this shell and Ctrl+Shift+A brings back one\n"
                 "that was kept.\n";
    return 0;
}

// One command's own page, which is what `kai-term <command> --help` is for.
inline int printVerbHelp(const std::string& verb) {
    for (const CommandHelp& command : commandHelp()) {
        if (verb != command.verb)
            continue;
        std::cout << "kai-term " << command.usage << "\n\n  " << command.summary << "\n";
        if (command.detail[0] != '\0')
            std::cout << "\n" << command.detail;
        if (command.options[0] != '\0')
            std::cout << "\n" << command.options;
        std::cout << "\n";
        printHelpRow("-h, --help", "this text");
        return 0;
    }
    std::cerr << "kai-term has no command `" << verb << "`.\n\n";
    return printHelp();
}

// `kai-term <verb> ...`, for the words that mean sessions. Returns an exit code;
// main calls this only for those words and starts a window for everything else.
//
// The verbs come straight after the program, a number from a listing is the handle,
// and the flags that survive are the ones that change what a verb does rather than
// restate what a positional word already said (`--window`, `--enter`, `-n` for the
// names that are sentences). A shell started by the daemon carries its number in
// KAI_SESSION, which is what lets `save` and `detach` be argument-free and lets
// `list` mark where you are standing.
inline int runSessionCommand(int argc, char** argv) {
    const std::string verb = argc > 1 ? argv[1] : "--help";
    std::string error;

    // Words this surface used to answer to. Nothing is kept for compatibility —
    // but a word typed out of habit should say what replaced it rather than
    // open a window or fail like a typo.
    if (verb == "session") {
        std::cerr << "kai-term: `session` is retired; the words come straight after "
                     "`kai-term` — `kai-term list`, `kai-term new`, `kai-term attach 0`.\n";
        return 2;
    }
    if (verb == "open") {
        std::cerr << "kai-term: `open NAME` is retired; `kai-term attach <id> --window` "
                     "opens a window onto a session.\n";
        return 2;
    }
    // `kai-term help` is the same page as `kai-term --help`, and `help <command>` is
    // the same page as `<command> --help`. One text, two ways to ask for it.
    if (verb == "help")
        return argc > 2 ? printVerbHelp(argv[2]) : printHelp();
    if (verb == "--help" || verb == "-h")
        return printHelp();

    // The daemon's own foreground life. No name, no options: it owns everything
    // or nothing.
    if (verb == "serve") {
        // Named before anything else, because the life starting here is the long
        // one, and a process that spends it answering as "exe" is a process nobody
        // can find when they want to know what is holding their shells.
        nameSelfForProcessLists("kai-term-daemon");
        // Say where it is before blocking: this is the way to watch a daemon that
        // will not start, and a debugging session where you cannot tell whether it
        // bound anything is a waste of a debugging session.
        std::cout << "listening on " << daemonSocketPath() << "\n"
                  << "Ctrl-C, or `kai-term shutdown`, to stop\n";
        // Said before the loop rather than after it, because after it there is nobody
        // left to tell.
        mentionForeignDaemon();
        std::cout << std::flush;
        const int status = serveDaemonForeground(error);
        if (!error.empty())
            std::cerr << "kai-term: " << error << "\n";
        return status;
    }

    // One pass, because every verb reads the same shape: flags that change the
    // verb, and positional words that mean a session and then whatever else the
    // verb takes. A word that starts with a dash and is not a flag is an error
    // rather than a mystery: `kai-term atach 2` should not become a session
    // named atach.
    std::vector<std::string> positional;
    std::string named;
    bool use_window = false;
    bool enter = false;
    // Which pane of a session to type into. By default the one it is looking at, because
    // that is the pane a person means when they say "type into that session".
    std::string pane = "-1";
    for (int i = 2; i < argc; ++i) {
        const std::string word = argv[i];
        if (word == "-n" || word == "--name") {
            if (i + 1 >= argc) {
                std::cerr << "kai-term: " << word << " needs a value\n";
                return 2;
            }
            named = argv[++i];
        } else if (word == "--window") {
            use_window = true;
        } else if (word == "--enter") {
            enter = true;
        } else if (word == "--pane") {
            if (i + 1 >= argc) {
                std::cerr << "kai-term: " << word << " needs a value\n";
                return 2;
            }
            pane = argv[++i];
            if (pane.empty() || pane.find_first_not_of("0123456789") != std::string::npos) {
                // Counted from zero, and counted in whole panes: a session cannot be asked
                // what pane 1.5 is, and answering nothing at all is worse than saying so.
                std::cerr << "kai-term: --pane wants a number counted from 0, not '"
                          << pane << "'\n";
                return 2;
            }
        } else if (word == "-h" || word == "--help") {
            // The page for the command that was actually asked for, which is what
            // `kai-term list --help` means.
            return printVerbHelp(verb);
        } else if (word.size() > 1 && word[0] == '-') {
            std::cerr << "kai-term: unknown option " << word << "\n";
            printVerbHelp(verb);
            return 2;
        } else {
            positional.push_back(word);
        }
    }

    // One request to a live daemon, for the verbs that are nothing else.
    auto oneCommand = [&error](const Fields& command, Frame& reply) {
        DaemonClient client;
        if (!client.connect(error))
            return false;
        return client.command(command, reply, error);
    };
    auto refused = [&error](const Frame& reply) {
        if (reply.kind == MsgKind::Ack)
            return false;
        error = fieldValue(decodeFields(reply.body), "message", "the daemon refused");
        return true;
    };
    auto fail = [&error] {
        std::cerr << "kai-term: " << (error.empty() ? "the daemon would not answer" : error)
                  << "\n";
        return 1;
    };

    if (verb == "list") {
        // Nothing running is not an error to report; it is the ordinary answer for
        // someone who has not started a session yet. A daemon that is there and
        // says something else is a different matter, and that one is worth printing.
        DaemonClient client;
        std::string connect_error;
        if (!client.connect(connect_error)) {
            std::cout << "no sessions (nothing is running yet)\n";
            mentionForeignDaemon();
            return 0;
        }
        const std::vector<SessionInfo> sessions = listSessions(error);
        if (!error.empty())
            return fail();
        const std::string here = currentSessionHandle();
        const std::vector<SessionInfo> listed = worthListing(sessions);
        printSessionTable(listed, here.empty() ? -1 : std::atoi(here.c_str()));
        if (listed.size() < sessions.size())
            std::cout << (sessions.size() - listed.size())
                      << " shell(s) are open in windows and not saved yet\n";
        // Someone looking at this listing is looking for their shells. If a daemon
        // from before the upgrade is holding some of them, this is the place.
        mentionForeignDaemon();
        return 0;
    }

    if (verb == "start") {
        if (daemonAlive()) {
            const std::string holder = liveDaemonPid();
            std::cout << (holder.empty() ? std::string("the daemon is already running")
                                         : "the daemon is already running, at pid " + holder)
                      << "\n";
            return 0;
        }
        if (!startDaemon(error))
            return fail();
        std::cout << "started a daemon at " << daemonSocketPath() << "\n";
        mentionForeignDaemon();
        return 0;
    }

    if (verb == "shutdown") {
        // Bringing the daemon down is not about one session, so it takes no id.
        Frame reply;
        if (!oneCommand({{"command", "shutdown"}}, reply))
            return fail();
        std::cout << "the daemon is shutting down\n";
        return 0;
    }

    if (verb == "new") {
        if (positional.size() > 1) {
            std::cerr << "kai-term: new takes one name at most; `kai-term new work`\n";
            return 2;
        }
        if (!ensureDaemon(error))
            return fail();
        std::string name = positional.empty() ? freeName(listSessions(error)) : positional[0];
        // Shell and directory from the environment, because a session is a shell
        // and a shell starts where a person is standing and speaks the dialect
        // they set. The home-directory default belongs to a window's shell, which
        // has no standing to borrow; a session started from a terminal does.
        char cwd_buffer[4096];
        const std::string cwd =
            ::getcwd(cwd_buffer, sizeof(cwd_buffer)) ? cwd_buffer : homeDirectory();
        const char* shell_env = std::getenv("KAI_SHELL");
        if (!shell_env || !*shell_env)
            shell_env = std::getenv("SHELL");
        int rows = 24;
        int cols = 80;
        terminalSize(rows, cols);
        Frame reply;
        if (!oneCommand({{"command", "new"},
                         {"name", name},
                         {"shell", shell_env ? shell_env : ""},
                         {"cwd", cwd},
                         {"rows", std::to_string(rows)},
                         {"cols", std::to_string(cols)}},
                        reply))
            return fail();
        if (refused(reply))
            return fail();
        const Fields ack = decodeFields(reply.body);
        const std::string id = fieldValue(ack, "id", "?");
        std::cout << "started session " << id << " named " << fieldValue(ack, "name", name)
                  << "\n";
        std::cout << "attach with: kai-term attach " << id << "\n";
        return 0;
    }

    if (verb == "attach") {
        if (!named.empty() && !positional.empty()) {
            std::cerr << "kai-term: attach takes an id or -n NAME, not both\n";
            return 2;
        }
        std::string handle = named.empty() && !positional.empty() ? positional[0] : named;
        if (handle.empty()) {
            std::cerr << "kai-term: attach which session? `kai-term list` shows them\n";
            return 2;
        }
        if (use_window) {
            // A window wants the name behind the number, because the name is what
            // the title bar says and what the window will ask the daemon for.
            const std::string name = resolveHandle(handle, error);
            if (name.empty())
                return fail();
            // Become the window rather than raise a child to wait for: this
            // command's whole job from here is being that window.
            char* const windows[] = {const_cast<char*>("kai-term"),
                                     const_cast<char*>("--attach-to"),
                                     const_cast<char*>(name.c_str()), nullptr};
            ::execv("/proc/self/exe", windows);
            std::cerr << "kai-term: the window would not start: " << std::strerror(errno)
                      << "\n";
            return 1;
        }
        // The old shape printed the error only on the path where the session had
        // ended cleanly, which meant `attach 999` said nothing at all and left a
        // person wondering. Whatever the exit code, if there is a reason, say it.
        const int status = attachSession(handle, error);
        if (!error.empty())
            std::cerr << "kai-term: " << error << "\n";
        return status;
    }

    if (verb == "detach") {
        // tmux's `detach-session` takes a target; this one does not, because the
        // session you mean is the one whose prompt you are typing at — the daemon
        // said so in KAI_SESSION when it started this shell. From outside, kicking
        // the views out of somebody else's session is a window's business or a
        // kill, not a word to guess at.
        if (!positional.empty() || !named.empty()) {
            std::cerr << "kai-term: detach takes no arguments; it sends away the views of "
                         "the session you are standing in\n";
            return 2;
        }
        const std::string here = currentSessionHandle();
        if (here.empty()) {
            std::cerr << "kai-term: you are not standing in a session; `kai-term list` "
                         "shows them, and `kai-term attach <id>` stands you in one\n";
            return 2;
        }
        Frame reply;
        if (!oneCommand({{"command", "detach"}, {"name", here}}, reply) || refused(reply))
            return fail();
        std::cout << "sent away the views of session " << here << "\n";
        return 0;
    }

    if (verb == "save") {
        // Inside a session, no arguments: the shell knows its own number. From
        // outside, the id from a listing, and an optional new name after it —
        // `kai-term save 2 build`, where the old surface would have wanted two
        // flags to say the same sentence.
        if (positional.size() > 2) {
            std::cerr << "kai-term: save takes an id and a name at most\n";
            return 2;
        }
        const std::string handle =
            positional.empty() ? currentSessionHandle() : positional[0];
        if (handle.empty()) {
            std::cerr << "kai-term: save needs a session id — or run it inside the session "
                         "you want kept\n";
            return 2;
        }
        const std::string wanted = positional.size() > 1 ? positional[1] : "";
        Frame reply;
        if (!oneCommand({{"command", "save"}, {"name", handle}, {"as", wanted}}, reply) ||
            refused(reply))
            return fail();
        const std::string kept_name = fieldValue(decodeFields(reply.body), "name", handle);
        // The handle first when a new name comes after it: "keeping 2 as build"
        // says which session was meant and what it answers to now. The daemon's
        // answer already carries the new name, so repeating it as the thing that
        // *was* kept would describe a session nobody called that.
        std::cout << (wanted.empty() ? "keeping " + kept_name
                                     : "keeping " + handle + " as " + wanted)
                  << "\n";
        return 0;
    }

    if (verb == "send" || verb == "kill") {
        if (verb == "send" && positional.size() < 2) {
            std::cerr << "kai-term: send needs a session and the text: "
                         "`kai-term send 2 make --enter`\n";
            return 2;
        }
        if (verb == "kill" && positional.size() != 1) {
            std::cerr << "kai-term: kill needs a session id; `kai-term list` shows them\n";
            return 2;
        }
        const std::string handle = positional[0];
        const std::string text =
            verb == "send" ? positional[1] + (enter ? "\n" : "") : "";
        Frame reply;
        if (!oneCommand({{"command", verb},
                         {"name", handle},
                         {"text", text},
                         {"pane", verb == "send" ? pane : "-1"}},
                        reply) ||
            refused(reply))
            return fail();
        if (verb == "send")
            std::cout << "typed into session " << handle << "\n";
        else
            std::cout << "ended session " << handle << "\n";
        return 0;
    }

    if (verb == "layout" || verb == "set-layout") {
        if (positional.size() != 1) {
            std::cerr << "kai-term: " << verb
                      << " needs a session id; `kai-term list` shows them\n";
            return 2;
        }
        const std::string handle = positional[0];
        if (verb == "layout") {
            Frame reply;
            if (!oneCommand({{"command", "layout"}, {"name", handle}}, reply))
                return fail();
            // `refused` knows the answers that mean "here you are", and an arrangement is
            // one of them now: asking for one and being told "no such session" has to print
            // what the daemon said, not what the first word of the answer looked like.
            if (reply.kind != MsgKind::Layout) {
                static_cast<void>(refused(reply));
                return fail();
            }
            // The words exactly as the daemon holds them, newline and all: the usual next
            // move is `> file`, and a file that reads back as the arrangement is the point
            // of writing them as words in the first place.
            std::cout << fieldValue(decodeFields(reply.body), "text", "");
            return 0;
        }
        // From stdin, because an arrangement is several lines and a shell argument is one
        // word. Nothing typed at a terminal arrives here — that would be a person waiting
        // for a prompt that never comes — so an empty input says so rather than quietly
        // arranging a shell into a single pane.
        std::string text;
        for (std::string line; std::getline(std::cin, line);) {
            text += line;
            text += '\n';
        }
        Frame reply;
        if (!oneCommand({{"command", "set-layout"}, {"name", handle}, {"text", text}}, reply) ||
            refused(reply))
            return fail();
        std::cout << "arranged session " << handle << "\n";
        return 0;
    }

    std::cerr << "kai-term: no such command: " << verb << "\n";
    printHelp();
    return 2;
}

} // namespace kai
