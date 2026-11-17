#pragma once

// The client's side of the daemon: `kai-term session ...` in a terminal, and the same
// operations for a window that needs a shell to show.
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
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "../ipc/protocol.hpp"
#include "../ipc/socket.hpp"
#include "daemon.hpp"

namespace kai {

inline constexpr int kDaemonReadyWaitMs = 3000;

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

// Defined below, next to the loop it wraps.
inline int serveDaemonForeground(std::string& error);

// Start a daemon in the background. The child takes a session of its own so closing
// the window that started it cannot send it a SIGHUP; its output goes nowhere, since
// there is nobody left to read it and a daemon that dies writing to a closed pipe
// would be a poor way to keep a shell alive.
inline bool startDaemon(std::string& error) {
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
        // Become a `session serve` process rather than staying a copy of the command
        // that happened to start us. A background process that shows up in `ps` as
        // `kai-term session new -s work --cwd /tmp` cannot be found when someone
        // wants to know what is holding their shells, and cannot be reached by
        // anything that looks at a process list.
        char* const serve[] = {const_cast<char*>("kai-term"), const_cast<char*>("session"),
                               const_cast<char*>("serve"), nullptr};
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
            error = "the daemon could not start; run `kai-term session serve` to see why";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    error = "the daemon did not answer within " + std::to_string(kDaemonReadyWaitMs / 1000) +
            " seconds";
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

inline void printSessionTable(const std::vector<SessionInfo>& sessions) {
    if (sessions.empty()) {
        std::cout << "no saved sessions\n";
        return;
    }
    std::cout << std::left << std::setw(17) << "NAME" << std::right << std::setw(8) << "PID"
              << std::setw(9) << "SIZE" << std::setw(7) << "VIEWS" << std::setw(11) << "RUNNING"
              << std::left << "  STARTED"
              << "  STATUS\n";
    for (const SessionInfo& info : sessions) {
        const std::time_t when = static_cast<std::time_t>(info.started);
        std::tm local {};
        ::localtime_r(&when, &local);
        char stamp[32] = {0};
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
        std::cout << std::left << std::setw(17) << info.name << std::right << std::setw(8)
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
// by `session serve` in the foreground and by the background daemon a window or
// `session new` starts.
inline int serveDaemonForeground(std::string& error) {
    Daemon daemon;
    if (!daemon.start(error)) {
        error += " (" + daemonSocketPath() + ")";
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

    // Only worth doing when there is a terminal to look after. Piped in, the bytes go
    // through as they are, which is what a script expects.
    const bool tty = ::isatty(STDIN_FILENO) != 0;
    std::unique_ptr<RawTerminal> raw;
    if (tty) {
        raw = std::make_unique<RawTerminal>();
        ::signal(SIGWINCH, [](int) { g_resize_requested = 1; });
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
                if (!client.send(MsgKind::Data, std::string(buf, static_cast<std::size_t>(n)))) {
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
    ::signal(SIGWINCH, SIG_DFL);
    return status;
}

struct SessionArgs {
    std::string name;
    std::string shell;
    std::string cwd;
    std::string text;
    std::string as; // the name to save a session under
    bool enter = false;
    int rows = 24;
    int cols = 80;
};

inline bool takeValue(int argc, char** argv, int& i, std::string& out, std::string& error) {
    if (i + 1 >= argc) {
        error = std::string(argv[i]) + " needs a value";
        return false;
    }
    out = argv[++i];
    return true;
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

// A name nobody is using, so `session new` without one still does the obvious thing.
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
                        {"saved", "0"}},
                       error))
        return {};
    return name;
}

// `kai-term session ...`. Returns an exit code, or -1 when the first word was not
// "session" and the caller should carry on starting a window.
inline int runSessionCommand(int argc, char** argv) {
    const std::string verb = argc > 2 ? argv[2] : "--help";
    SessionArgs args;
    terminalSize(args.rows, args.cols);
    std::string error;

    auto usage = [] {
        std::cout << "Usage: kai-term session <command> [options]\n"
                  << "  list                      show saved sessions, and any left behind\n"
                  << "  new [-s NAME] [--shell P] [--cwd D] [--rows N] [--cols N]\n"
                  << "                            start a session the daemon keeps alive\n"
                  << "  save [-s NAME] [--as NEW] keep a session, and name it\n"
                  << "  attach [-s NAME]          show a session in this terminal\n"
                  << "  send [-s NAME] --text T   type into a session without watching it\n"
                  << "  detach [-s NAME]          send away the views, keep the shell\n"
                  << "  kill [-s NAME]            end a session\n"
                  << "  start                     start the daemon, if it is not running\n"
                  << "  shutdown                  end every session and stop the daemon\n";
        return 0;
    };

    if (verb == "--help" || verb == "-h" || verb == "help")
        return usage();

    // The daemon's own foreground life, which is what `session start` forks into and
    // what a foreground `session serve` runs. No name, no options: it owns
    // everything or nothing.
    if (verb == "serve") {
        // Say where it is before blocking: this is the way to watch a daemon that
        // will not start, and a debugging session where you cannot tell whether it
        // bound anything is a waste of a debugging session.
        std::cout << "listening on " << daemonSocketPath() << "\n"
                  << "Ctrl-C, or `kai-term session shutdown`, to stop\n"
                  << std::flush;
        const int status = serveDaemonForeground(error);
        if (!error.empty())
            std::cerr << "kai-term: " << error << "\n";
        return status;
    }

    for (int i = 3; i < argc; ++i) {
        const std::string flag = argv[i];
        std::string value;
        if (flag == "-s" || flag == "--name") {
            if (!takeValue(argc, argv, i, args.name, error)) {
                std::cerr << "kai-term: " << error << "\n";
                return 2;
            }
        } else if (flag == "--shell") {
            if (!takeValue(argc, argv, i, args.shell, error)) {
                std::cerr << "kai-term: " << error << "\n";
                return 2;
            }
        } else if (flag == "--cwd") {
            if (!takeValue(argc, argv, i, args.cwd, error)) {
                std::cerr << "kai-term: " << error << "\n";
                return 2;
            }
        } else if (flag == "--rows") {
            if (!takeValue(argc, argv, i, value, error)) {
                std::cerr << "kai-term: " << error << "\n";
                return 2;
            }
            args.rows = std::atoi(value.c_str());
        } else if (flag == "--text") {
            if (!takeValue(argc, argv, i, args.text, error)) {
                std::cerr << "kai-term: " << error << "\n";
                return 2;
            }
        } else if (flag == "--enter") {
            args.enter = true;
        } else if (flag == "--as") {
            if (!takeValue(argc, argv, i, args.as, error)) {
                std::cerr << "kai-term: " << error << "\n";
                return 2;
            }
        } else if (flag == "--cols") {
            if (!takeValue(argc, argv, i, value, error)) {
                std::cerr << "kai-term: " << error << "\n";
                return 2;
            }
            args.cols = std::atoi(value.c_str());
        } else {
            std::cerr << "kai-term: unknown option " << flag << "\n";
            return 2;
        }
    }

    if (verb == "list") {
        // Nothing running is not an error to report; it is the ordinary answer for
        // someone who has not started a session yet. A daemon that is there and says
        // something else is a different matter, and that one is worth printing.
        DaemonClient client;
        std::string connect_error;
        if (!client.connect(connect_error)) {
            std::cout << "no sessions (nothing is running yet)\n";
            return 0;
        }
        const std::vector<SessionInfo> sessions = listSessions(error);
        if (!error.empty()) {
            std::cerr << "kai-term: " << error << "\n";
            return 1;
        }
        const std::vector<SessionInfo> listed = worthListing(sessions);
        printSessionTable(listed);
        if (listed.size() < sessions.size())
            std::cout << (sessions.size() - listed.size())
                      << " shell(s) are open in windows and not saved yet\n";
        return 0;
    }

    if (verb == "start") {
        if (daemonAlive()) {
            std::cout << "the daemon is already running\n";
            return 0;
        }
        if (!startDaemon(error)) {
            std::cerr << "kai-term: " << error << "\n";
            return 1;
        }
        std::cout << "started a daemon at " << daemonSocketPath() << "\n";
        return 0;
    }

    if (verb == "new") {
        if (!ensureDaemon(error)) {
            std::cerr << "kai-term: " << error << "\n";
            return 1;
        }
        if (args.name.empty())
            args.name = freeName(listSessions(error));
        DaemonClient client;
        if (!client.connect(error)) {
            std::cerr << "kai-term: " << error << "\n";
            return 1;
        }
        Frame reply;
        if (!client.command({{"command", "new"},
                             {"name", args.name},
                             {"shell", args.shell},
                             {"cwd", args.cwd},
                             {"rows", std::to_string(args.rows)},
                             {"cols", std::to_string(args.cols)}},
                            reply, error)) {
            std::cerr << "kai-term: " << error << "\n";
            return 1;
        }
        if (reply.kind != MsgKind::Ack) {
            std::cerr << "kai-term: "
                      << fieldValue(decodeFields(reply.body), "message", "the daemon refused")
                      << "\n";
            return 1;
        }
        std::cout << "started session " << args.name << "\n";
        std::cout << "attach with: kai-term session attach -s " << args.name << "\n";
        return 0;
    }

    if (verb == "shutdown") {
        // Bringing the daemon down is not about one session, so it takes no name.
        DaemonClient client;
        Frame shutdown_reply;
        if (!client.connect(error) ||
            !client.command({{"command", "shutdown"}}, shutdown_reply, error)) {
            std::cerr << "kai-term: " << error << "\n";
            return 1;
        }
        std::cout << "the daemon is shutting down\n";
        return 0;
    }

    if (args.name.empty()) {
        std::cerr << "kai-term: session " << verb << " needs -s NAME\n";
        return 2;
    }

    if (verb == "attach") {
        if (!attachSession(args.name, error)) {
            if (!error.empty())
                std::cerr << "kai-term: " << error << "\n";
            return 0;
        }
        return 1;
    }

    DaemonClient client;
    if (!client.connect(error)) {
        std::cerr << "kai-term: " << error << "\n";
        return 1;
    }
    Frame reply;
    // `send` carries the keys it should type; the others have nothing but a name.
    const std::string text = args.enter ? args.text + "\n" : args.text;
    if (!client.command({{"command", verb},
                         {"name", args.name},
                         {"text", text},
                         {"as", args.as}},
                        reply, error)) {
        std::cerr << "kai-term: " << error << "\n";
        return 1;
    }
    if (reply.kind != MsgKind::Ack) {
        std::cerr << "kai-term: "
                  << fieldValue(decodeFields(reply.body), "message", "the daemon refused") << "\n";
        return 1;
    }
    if (verb == "save")
        std::cout << (args.as.empty() ? "keeping " + args.name
                                      : "keeping " + args.name + " as " + args.as) << "\n";
    if (verb == "kill")
        std::cout << "ended session " << args.name << "\n";
    if (verb == "detach")
        std::cout << "sent away the views of " << args.name << "\n";
    if (verb == "send")
        std::cout << "typed into " << args.name << "\n";
    if (verb == "shutdown")
        std::cout << "the daemon is shutting down\n";
    return 0;
}

} // namespace kai
