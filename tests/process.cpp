// Headless check of what happens to the process on the other end of the
// terminal: how its exit is reported, what a write or a resize does after it is
// gone, and whether closing the window takes the jobs it started with it. None
// of this needs Qt, and none of it can be seen from the widget tests.

#include "../src/process/pty.hpp"

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

int checks = 0;
int failures = 0;

void expect(const char* what, bool ok) {
    ++checks;
    if (ok)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << "\n";
}

void expectNum(const char* what, long got, long want) {
    ++checks;
    if (got == want)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << ": want [" << want << "] got [" << got << "]\n";
}

using Clock = std::chrono::steady_clock;

// Where the test has got to, so a run that stops producing output says what it
// was doing rather than nothing at all.
void step(const char* what) {
    std::cerr << "[step] " << what << std::endl;
}

// Read whatever the child produces until it closes the terminal or the deadline
// passes. A shell echoes what it is given, so the text holds both the command
// and its output.
// A deadline between reads is not a deadline: a read on a terminal blocks until
// something is written, so the wait happens in poll() first.
std::string drain(kai::Pty& pty, int ms) {
    std::string out;
    char buf[4096];
    const auto deadline = Clock::now() + std::chrono::milliseconds(ms);
    while (Clock::now() < deadline) {
        struct pollfd pfd {pty.master(), POLLIN, 0};
        const int ready = ::poll(&pfd, 1, 20);
        if (ready == 0)
            continue;
        const ssize_t n = pty.read(buf, sizeof(buf));
        if (n > 0) {
            out.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0)
            break; // the terminal is closed
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return out;
}

bool waitExit(kai::Pty& pty, int ms) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(ms);
    while (Clock::now() < deadline) {
        if (pty.reap())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return pty.reap();
}

bool processAlive(pid_t pid) {
    return pid > 0 && ::kill(pid, 0) == 0;
}

// The number a shell printed after `marker`, which is how a test learns the
// process id of a job it asked for.
pid_t pidAfter(const std::string& text, const std::string& marker) {
    const std::size_t at = text.rfind(marker);
    if (at == std::string::npos)
        return -1;
    return static_cast<pid_t>(std::strtol(text.c_str() + at + marker.size(), nullptr, 10));
}

} // namespace

int main() {
    // ---- The exit status of the shell is reported, not swallowed ---------
    {
        step("starting a shell");
        kai::Pty pty(24, 80, "/bin/sh");
        expect("a shell starts", pty.alive());
        expect("and takes input", pty.write("exit 7\n"));
        static_cast<void>(drain(pty, 1000));
        expect("the shell is collected", waitExit(pty, 3000));
        expect("and is no longer alive", !pty.alive());
        expectNum("with the status it exited with", pty.exitCode(), 7);
        expectNum("and no signal involved", pty.exitSignal(), 0);
        expect("collecting it again changes nothing", pty.reap());
        expectNum("and the status is still there", pty.exitCode(), 7);
        pty.stop();
        pty.stop(); // shutting down twice must be ordinary, not fatal
    }

    // ---- A shell killed by a signal says so -------------------------------
    {
        step("a shell killed by a signal");
        kai::Pty pty(24, 80, "/bin/sh");
        // SIGKILL rather than SIGTERM: an interactive shell deliberately ignores
        // the latter, so it would prove nothing about how a death by signal is
        // reported.
        static_cast<void>(pty.write("kill -9 $$\n"));
        static_cast<void>(drain(pty, 1000));
        expect("a killed shell is collected", waitExit(pty, 3000));
        expectNum("reported as a signal", pty.exitSignal(), SIGKILL);
        expectNum("with no exit status to report", pty.exitCode(), -1);
    }

    // ---- The end of the shell reaches the reader, and nothing after works -
    {
        step("using a terminal whose shell has gone");
        kai::Pty pty(24, 80, "/bin/sh");
        static_cast<void>(pty.write("exit 0\n"));
        // The window closes because a read says the stream is over, so that has
        // to happen on its own and not merely when someone writes again.
        bool saw_end = false;
        char buf[4096];
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        while (Clock::now() < deadline && !saw_end) {
            struct pollfd pfd {pty.master(), POLLIN, 0};
            if (::poll(&pfd, 1, 50) == 0)
                continue;
            const ssize_t n = pty.read(buf, sizeof(buf));
            if (n <= 0)
                saw_end = true;
        }
        expect("a read reports the end of the stream when the shell exits", saw_end);
        expect("the shell is collected", waitExit(pty, 3000));
        expect("and it is no longer reported as running", !pty.alive());
        // Writing still succeeds after the shell is gone, because the kernel
        // keeps the pair wired together while the master is open; the echo of
        // what was written comes straight back. Nothing may treat a successful
        // write as proof the shell is there, so this only checks that it is
        // harmless.
        expect("writing to it is harmless", pty.write("echo hi\n"));
        pty.resize(40, 120); // the window can still be dragged after the shell exits
        expect("resizing after the shell exits leaves it dead", !pty.alive());
        pty.terminate(); // teardown of something already gone
        expect("terminating it again is fine", pty.exited());
        pty.closeMaster();
        expect("writing to a closed terminal is refused", !pty.write("echo hi\n"));
        expectNum("and reading one reports the end", pty.read(buf, 1), 0);
    }

    // ---- Closing the window takes the jobs with it ------------------------
    {
        step("closing a window with a job in it");
        kai::Pty pty(24, 80, "/bin/sh");
        expect("a job can be started", pty.write("sleep 300 & echo JOB=$!\n"));
        pid_t job = -1;
        for (int i = 0; i < 50 && job <= 0; ++i) {
            const std::string out = drain(pty, 100);
            job = pidAfter(out, "JOB=");
        }
        step("  terminating with a job running");
        expect("the job reported its process id", job > 0);
        expect("and it is running", processAlive(job));
        // Job control gives every job its own process group, which is exactly
        // why signalling the shell's group is not enough.
        pty.terminate();
        bool gone = false;
        for (int i = 0; i < 100 && !gone; ++i) {
            gone = !processAlive(job);
            if (!gone)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        expect("closing the terminal takes the shell's jobs with it", gone);
        expect("and the shell itself is gone", !pty.alive());
    }

    // ---- A shell that ignores those signals still goes --------------------
    {
        step("a shell that ignores hangup and terminate");
        kai::Pty pty(24, 80, "/bin/sh");
        static_cast<void>(pty.write("trap '' HUP TERM; sleep 300 & wait\n"));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        expect("it is still running while it ignores nothing", pty.alive());
        step("  terminating it");
        const auto before = Clock::now();
        pty.terminate();
        const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(
                              Clock::now() - before)
                              .count();
        expect("a shell ignoring hangup and terminate is killed", pty.exited());
        expectNum("by SIGKILL", pty.exitSignal(), SIGKILL);
        expect("and not after an unbounded wait", took < 6000);
    }

    // ---- A shell that cannot be run says so where it can be read ----------
    {
        step("a shell that cannot be run");
        kai::Pty pty(24, 80, "/nonexistent-shell-kai-term-test");
        const std::string out = drain(pty, 1000);
        expect("it explains itself on the terminal", out.find("cannot run") != std::string::npos);
        expect("and exits", waitExit(pty, 2000));
        expectNum("with the conventional status for that", pty.exitCode(), 127);
    }

    if (failures == 0) {
        std::cout << "PASS: " << checks << " process lifecycle checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " process lifecycle checks failed\n";
    return 1;
}
