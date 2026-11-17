#pragma once

#include <cstdlib>
#include <csignal>
#include <stdexcept>
#include <string>

#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace kai {

class Pty {
public:
    explicit Pty(int rows, int cols, std::string shell = {})
        : master_(-1), child_(-1)
    {
        if (rows < 1) rows = 1;
        if (cols < 1) cols = 1;

        std::string chosen = shell;
        if (chosen.empty()) {
            const char* env = std::getenv("KAI_SHELL");
            if (env && *env) chosen = env;
            if (chosen.empty()) {
                env = std::getenv("SHELL");
                if (env && *env) chosen = env;
            }
            if (chosen.empty()) chosen = "/bin/sh";
            if (access(chosen.c_str(), X_OK) != 0) chosen = "/bin/sh";
        }

        struct winsize ws{static_cast<unsigned short>(rows),
                         static_cast<unsigned short>(cols), 0, 0};
        int master = -1;
        child_ = forkpty(&master, nullptr, nullptr, &ws);
        if (child_ < 0) {
            throw std::runtime_error("forkpty failed");
        }

        if (child_ == 0) {
            // Child branch: forkpty has made the slave the controlling terminal.
            setenv("TERM", "xterm-256color", 1);
            execl(chosen.c_str(), chosen.c_str(), "-i", nullptr);
            _exit(127);
        }

        if (master < 0) {
            throw std::runtime_error("forkpty produced no master descriptor");
        }

        master_ = master;
    }

    ~Pty() { stop(); }

    void resize(int rows, int cols) {
        if (rows < 1) rows = 1;
        if (cols < 1) cols = 1;
        if (master_ < 0) return;
        struct winsize ws{static_cast<unsigned short>(rows),
                         static_cast<unsigned short>(cols), 0, 0};
        if (ioctl(master_, TIOCSWINSZ, &ws) != 0) {
            // Keep going; the caller can decide whether this is fatal.
        }
    }

    void write(const std::string& data) {
        if (master_ < 0) return;
        std::size_t off = 0;
        while (off < data.size()) {
            ssize_t n = ::write(master_, data.data() + off, data.size() - off);
            if (n <= 0) return;
            off += static_cast<std::size_t>(n);
        }
    }

    ssize_t read(char* buf, std::size_t n) {
        if (master_ < 0) return 0;
        return ::read(master_, buf, n);
    }

    bool alive() const { return master_ >= 0 && child_ > 0; }
    int master() const { return master_; }
    pid_t child() const { return child_; }

    void stop() {
        if (master_ >= 0) {
            close(master_);
            master_ = -1;
        }
        if (child_ > 0) {
            int status = 0;
            pid_t r = waitpid(child_, &status, WNOHANG);
            if (r == 0) {
                kill(-child_, SIGTERM);
                for (int i = 0; i < 20; ++i) {
                    r = waitpid(child_, &status, WNOHANG);
                    if (r != 0)
                        break;
                    usleep(100000);
                }
                if (r == 0) {
                    kill(-child_, SIGKILL);
                    waitpid(child_, &status, 0);
                }
            }
            child_ = -1;
        }
    }

private:
    int master_ = -1;
    pid_t child_ = -1;
};

} // namespace kai
