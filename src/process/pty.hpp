#pragma once

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <dirent.h>
#include <fcntl.h>

#include <cctype>
#include <cstdio>
#include <vector>

namespace kai {

// A child process on the other end of a pseudo-terminal.
//
// Two things about this are easy to get wrong and are the reason the interface
// looks the way it does. The first is that a file descriptor cannot be closed
// underneath a thread that is blocked reading it: teardown therefore separates
// "make the child go away" from "close the master", so a caller can stop the
// child, wait for its reader to notice, and only then let go of the descriptor.
// The second is that killing the child is not enough, because a shell leaves
// jobs behind: the child is a session and process-group leader, so the signal
// goes to the group.
class Pty {
public:
    explicit Pty(int rows, int cols, std::string shell = {}, std::string cwd = {},
                 std::string session_id = {})
        : master_(-1), child_(-1)
    {
        if (rows < 1)
            rows = 1;
        if (cols < 1)
            cols = 1;

        // An explicitly requested shell is used as asked, and if it cannot be
        // run the child says so and exits; quietly substituting /bin/sh would
        // leave someone hunting for why their configured shell never appears.
        // Only the guess from the environment is allowed to fall back.
        std::string chosen = shell;
        bool explicit_shell = !chosen.empty();
        const char* env = nullptr;
        if (chosen.empty())
            env = std::getenv("KAI_SHELL");
        if (chosen.empty() && env && *env) {
            chosen = env;
            explicit_shell = true;
        }
        if (chosen.empty())
            env = std::getenv("SHELL");
        if (chosen.empty() && env && *env)
            chosen = env;
        if (!explicit_shell && !chosen.empty() && access(chosen.c_str(), X_OK) != 0)
            chosen.clear();
        if (chosen.empty())
            chosen = "/bin/sh";

        struct winsize ws
        {
            static_cast<unsigned short>(rows), static_cast<unsigned short>(cols), 0, 0
        };
        int master = -1;
        const pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
        child_ = pid;
        if (pid < 0)
            throw std::runtime_error(std::string("forkpty failed: ") + std::strerror(errno));

        if (pid == 0) {
            // forkpty has already made the slave the controlling terminal, so
            // anything written here is what a person sees in the window.
            // The shell starts in the directory it was asked to start in, not by
            // being sent a `cd` command afterwards: a terminal that types a command
            // for the person is not the same as a terminal that starts there.
            if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
                std::fprintf(stderr, "kai-term: cannot start in %s: %s\r\n", cwd.c_str(),
                             std::strerror(errno));
                _exit(127);
            }
            setenv("TERM", "xterm-256color", 1);
            // Which session this shell is, so a command run inside it — `save`,
            // `detach`, `list` marking where you are — can know without being
            // told. Set or cleared, never merely inherited: a daemon started
            // from inside somebody's session would otherwise hand that session's
            // number to every shell it ever starts, and an argument-free `save`
            // would keep a shell nobody is standing in.
            if (!session_id.empty())
                setenv("KAI_SESSION", session_id.c_str(), 1);
            else
                ::unsetenv("KAI_SESSION");
            execl(chosen.c_str(), chosen.c_str(), "-i", nullptr);
            std::fprintf(stderr, "kai-term: cannot run %s: %s\r\n", chosen.c_str(),
                         std::strerror(errno));
            _exit(127);
        }

        if (master < 0)
            throw std::runtime_error("forkpty produced no master descriptor");

        master_ = master;
        // forkpty put the child into a session of its own, so from here on the
        // session is a fixed thing that outlives the child: remember it rather
        // than asking after the child is gone, when there is nothing left to ask.
        const pid_t sid = getsid(pid);
        session_ = sid > 0 ? sid : pid;
    }

    Pty(const Pty&) = delete;
    Pty& operator=(const Pty&) = delete;

    ~Pty() { stop(); }

    void resize(int rows, int cols) {
        if (rows < 1)
            rows = 1;
        if (cols < 1)
            cols = 1;
        if (master_ < 0)
            return;
        struct winsize ws
        {
            static_cast<unsigned short>(rows), static_cast<unsigned short>(cols), 0, 0
        };
        // An application that has already exited answers EINVAL or ENOTTY. That
        // is not worth failing a resize over: the window still has to redraw.
        static_cast<void>(ioctl(master_, TIOCSWINSZ, &ws));
    }

    // False when the bytes could not be handed over at all. Note what a true
    // return does *not* mean: the kernel keeps a pty pair wired together for as
    // long as the master is open, so a write still succeeds after the child has
    // exited, and the echo of it comes back on the next read. Only a read that
    // reports the end, or a reaped child, says the shell is gone — `alive()`.
    bool write(const std::string& data) {
        if (master_ < 0)
            return false;
        std::size_t off = 0;
        while (off < data.size()) {
            ssize_t n = ::write(master_, data.data() + off, data.size() - off);
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

    // 0 when the child has closed the terminal, -1 on an error that is not
    // EINTR, which is retried. Interrupted reads are ordinary here — a GUI
    // process takes signals — and treating one as the end of the shell would
    // close a perfectly good window.
    ssize_t read(char* buf, std::size_t n) {
        if (master_ < 0)
            return 0;
        for (;;) {
            ssize_t got = ::read(master_, buf, n);
            if (got >= 0 || errno != EINTR)
                return got;
        }
    }

    // Collect the child if it has already gone. Safe to call as often as you
    // like, and a call after the child is reaped keeps the status it recorded.
    bool reap() {
        // The reader thread reaps when it sees the end of the stream and the
        // thread shutting the window down reaps too, so exactly one of them may
        // collect the status.
        std::lock_guard<std::mutex> lock(reap_mutex_);
        if (child_ <= 0)
            return exited_;
        int status = 0;
        const pid_t r = waitpid(child_, &status, WNOHANG);
        if (r == 0)
            return false;
        if (r != child_) {
            // ECHILD means something else collected it; treat it as gone rather
            // than spinning on a child that no longer exists.
            if (errno == ECHILD) {
                exited_ = true;
                child_ = -1;
                return true;
            }
            return false;
        }
        recordStatus(status);
        return true;
    }

    bool exited() const { return exited_; }
    // The shell's own exit status, or -1 when a signal ended it.
    int exitCode() const { return exit_code_; }
    // The signal that ended it, or 0 when it exited by itself.
    int exitSignal() const { return exit_signal_; }

    bool alive() const { return master_ >= 0 && child_ > 0 && !exited_; }
    int master() const { return master_; }
    pid_t child() const { return child_; }

    // Ask the child to leave, and make sure it did — along with the jobs it
    // started, which is the part that tends to be forgotten. The master is left
    // open so a reader can drain whatever the child printed on its way out and
    // see the end of the stream for itself.
    void terminate() {
        const pid_t group = child_;
        const pid_t session = session_;
        if (reap()) {
            sweepSession(session, SIGKILL); // jobs whose parent is already gone
            return;
        }
        if (group <= 0)
            return;

        // SIGHUP first, because that is what closing a terminal window has
        // always meant to the process on the other end of it.
        static_cast<void>(kill(-group, SIGHUP));
        static_cast<void>(kill(-group, SIGTERM));
        for (int i = 0; i < 20; ++i) {
            if (reap()) {
                sweepSession(session, SIGKILL);
                return;
            }
            usleep(100000);
        }
        // Still here after two seconds: it is ignoring those, which a shell
        // running a job can legitimately do. Nothing softer is going to work.
        static_cast<void>(kill(-group, SIGKILL));
        {
            std::lock_guard<std::mutex> lock(reap_mutex_);
            int status = 0;
            pid_t r = 0;
            // Bounded, even though SIGKILL cannot be ignored: a process stuck in
            // uninterruptible I/O does not die for anyone, and a teardown that
            // waits for it forever is a window that can never be closed.
            for (int i = 0; i < 50 && r == 0; ++i) {
                r = waitpid(group, &status, WNOHANG);
                if (r == 0)
                    usleep(100000);
            }
            if (r == group)
                recordStatus(status);
        }
        sweepSession(session, SIGKILL);
    }

    // Every process still in the given session. Job control is why the shell's
    // own process group is not enough: an interactive shell puts each job in a
    // group of its own, so `sleep 300 &` is not reached by signalling the
    // shell's group, and closing the window would leave it running. Anything
    // that deliberately left the session (a daemon, a tmux server) has a
    // different session id and is left alone.
    static void sweepSession(pid_t session, int signal) {
        if (session <= 0)
            return;
        DIR* dir = opendir("/proc");
        if (!dir)
            return;
        while (const dirent* entry = readdir(dir)) {
            if (!std::isdigit(static_cast<unsigned char>(entry->d_name[0])))
                continue;
            const std::string path = std::string("/proc/") + entry->d_name + "/stat";
            const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0)
                continue;
            char buf[512];
            const ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n <= 0)
                continue;
            buf[n] = '\0';
            // The second field is the command name in parentheses and may hold
            // spaces and parentheses of its own, so the fields after it are
            // counted from the last one, not the first.
            const char* after = std::strrchr(buf, ')');
            if (!after)
                continue;
            char state = 0;
            int ppid = 0, pgrp = 0, sid = 0;
            if (std::sscanf(after + 1, "%c %d %d %d", &state, &ppid, &pgrp, &sid) != 4)
                continue;
            const long pid = std::strtol(entry->d_name, nullptr, 10);
            if (sid == session && pid != ::getpid())
                static_cast<void>(kill(static_cast<pid_t>(pid), signal));
        }
        closedir(dir);
    }

    void closeMaster() {
        if (master_ >= 0) {
            ::close(master_);
            master_ = -1;
        }
    }

    // The whole teardown, for owners with no reader thread to wait for.
    void stop() {
        terminate();
        closeMaster();
    }

private:
    // Called with reap_mutex_ held, except from a constructor-failure path that
    // nothing else can see yet.
    void recordStatus(int status) {
        exited_ = true;
        child_ = -1;
        if (WIFEXITED(status))
            exit_code_ = WEXITSTATUS(status);
        else
            exit_code_ = -1;
        exit_signal_ = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    }

    int master_ = -1;
    std::atomic<pid_t> child_{-1};
    std::atomic<pid_t> session_{-1};
    std::mutex reap_mutex_;
    std::atomic<bool> exited_{false};
    std::atomic<int> exit_code_{-1};
    std::atomic<int> exit_signal_{0};
};

} // namespace kai
