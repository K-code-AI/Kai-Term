// The command surface a person types, exercised through the real binary.
//
// Everything else in the test suite talks to the daemon over the socket or to the
// widget through Qt, which is right for them and blind here: the words, the
// positional ids, the retired verbs that should say what replaced them, the
// KAI_SESSION that lets `save` and `detach` be argument-free, and the listing's
// star are a contract with a pair of hands, and only running the program checks
// it. The test runs in a private $XDG_RUNTIME_DIR, so the daemon it starts is
// its own and it touches nobody else's shells.

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#ifndef KAI_TERM_BIN
#error "the build must say where the kai-term binary is (KAI_TERM_BIN)"
#endif

namespace {

int failures = 0;
int checks = 0;

void expect(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

struct Run {
    int code = -1;
    std::string out; // stdout and stderr together, because both are the answer
};

// Run the real binary with the given words and extra environment, with stdin at
// /dev/null, and collect everything it prints. A command that has not finished
// in fifteen seconds is not slow, it is stuck, and a stuck test is a hung suite.
// The exit status is taken exactly once, from the one wait() that gets it.
Run run(const std::vector<std::string>& args, const std::vector<std::string>& env = {}) {
    Run result;
    int pipe_fd[2];
    if (::pipe(pipe_fd) != 0) {
        result.out = "pipe failed";
        result.code = 200;
        return result;
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        result.out = "fork failed";
        result.code = 200;
        return result;
    }
    if (pid == 0) {
        ::dup2(pipe_fd[1], STDOUT_FILENO);
        ::dup2(pipe_fd[1], STDERR_FILENO);
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
        const int devnull = ::open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDIN_FILENO);
            if (devnull > 2)
                ::close(devnull);
        }
        for (const std::string& assignment : env) {
            const std::size_t eq = assignment.find('=');
            if (eq != std::string::npos)
                ::setenv(assignment.substr(0, eq).c_str(),
                         assignment.substr(eq + 1).c_str(), 1);
        }
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>("kai-term"));
        for (const std::string& word : args)
            argv.push_back(const_cast<char*>(word.c_str()));
        argv.push_back(nullptr);
        ::execv(KAI_TERM_BIN, argv.data());
        std::fprintf(stderr, "exec failed: %s\n", std::strerror(errno));
        _exit(127);
    }
    ::close(pipe_fd[1]);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
        const int left_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                                  std::chrono::steady_clock::now())
                .count());
        if (left_ms <= 0) {
            ::kill(pid, SIGKILL);
            result.out += "<killed for taking too long>";
            break;
        }
        struct pollfd p {pipe_fd[0], POLLIN, 0};
        const int ready = ::poll(&p, 1, left_ms);
        if (ready > 0) {
            char buf[4096];
            const ssize_t n = ::read(pipe_fd[0], buf, sizeof(buf));
            if (n > 0) {
                result.out.append(buf, static_cast<std::size_t>(n));
                continue;
            }
            if (n == 0)
                break; // EOF: every writer is gone, so the answer is complete
            if (n < 0 && errno != EINTR)
                break;
        } else if (ready < 0 && errno != EINTR) {
            break;
        }
    }
    // Whatever is still buffered belongs to the answer too.
    for (;;) {
        char buf[4096];
        const ssize_t n = ::read(pipe_fd[0], buf, sizeof(buf));
        if (n <= 0)
            break;
        result.out.append(buf, static_cast<std::size_t>(n));
    }
    ::close(pipe_fd[0]);
    int status = 0;
    static_cast<void>(::waitpid(pid, &status, 0));
    result.code = WIFEXITED(status) ? WEXITSTATUS(status) : 200;
    return result;
}

// The listing, as the program prints it.
std::string listing(const std::vector<std::string>& env = {}) {
    return run({"list"}, env).out;
}

} // namespace

int main() {
    char tmpl[] = "/tmp/kai-cli-surface-XXXXXX";
    if (!::mkdtemp(tmpl)) {
        std::printf("FAIL: cannot make a runtime directory\n");
        return 1;
    }
    const std::string runtime_dir = tmpl;
    const std::vector<std::string> here = {"XDG_RUNTIME_DIR=" + runtime_dir};

    // A listing with nothing running is an answer, not an error.
    {
        const Run r = run({"list"}, here);
        expect(r.code == 0, "list with no daemon exits 0, got " + std::to_string(r.code));
        expect(contains(r.out, "no sessions"), "list with no daemon says so: " + r.out);
    }

    // Retired words say what replaced them rather than failing like a typo.
    {
        const Run r = run({"session", "list"}, here);
        expect(r.code == 2, "retired `session` exits 2, got " + std::to_string(r.code));
        expect(contains(r.out, "retired") && contains(r.out, "kai-term list"),
               "retired `session` points at the new words: " + r.out);
    }
    {
        const Run r = run({"open", "work"}, here);
        expect(r.code == 2, "retired `open` exits 2");
        expect(contains(r.out, "--window"), "retired `open` says use attach --window: " + r.out);
    }

    // new prints the id, because the id is what the next command needs.
    {
        const Run r = run({"new", "work"}, here);
        expect(r.code == 0, "new exits 0: " + r.out);
        expect(contains(r.out, "started session 0 named work"),
               "new prints id and name: " + r.out);
        expect(contains(r.out, "kai-term attach 0"), "new says how to attach: " + r.out);
    }
    {
        const Run r = run({"new"}, here);
        expect(r.code == 0 && contains(r.out, "started session 1 named s1"),
               "nameless new picks from the kept range: " + r.out);
    }

    // The listing shows both, and the star marks the session you are standing in.
    {
        const std::string out = listing(here);
        expect(contains(out, "work") && contains(out, "s1"), "listing shows both sessions");
        expect(!contains(out, "*0") && !contains(out, "*1"), "from outside, nothing is starred");
    }
    {
        std::vector<std::string> env = here;
        env.push_back("KAI_SESSION=1");
        const std::string out = listing(env);
        expect(contains(out, "*1"), "standing in session 1 stars its row: " + out);
    }

    // save by id with a new name, save from inside with neither, and the refusals
    // in between.
    {
        const Run r = run({"save", "1", "keeper"}, here);
        expect(r.code == 0 && contains(r.out, "keeping 1 as keeper"),
               "save renames by id: " + r.out);
        expect(contains(listing(here), "keeper"), "the new name is what the listing says");
    }
    {
        std::vector<std::string> env = here;
        env.push_back("KAI_SESSION=0");
        const Run r = run({"save"}, env);
        expect(r.code == 0 && contains(r.out, "keeping work"),
               "save with no arguments keeps the session you stand in, by name: " + r.out);
    }
    {
        const Run r = run({"save"}, here);
        expect(r.code == 2 && contains(r.out, "needs a session id"),
               "save from nowhere asks for an id: " + r.out);
    }

    // detach takes no arguments and only works from inside a session.
    {
        const Run r = run({"detach"}, here);
        expect(r.code == 2 && contains(r.out, "not standing in a session"),
               "detach from outside says where you are: " + r.out);
    }
    {
        std::vector<std::string> env = here;
        env.push_back("KAI_SESSION=0");
        const Run r = run({"detach"}, env);
        expect(r.code == 0 && contains(r.out, "sent away the views"),
               "detach from inside answers: " + r.out);
    }

    // send types, and the session's shell knows its own number — the number the
    // daemon wrote, not the one a command inherited: this session was started by
    // a process standing in a fake session 99.
    {
        const Run maker = run({"new", "inherit"},
                              {"XDG_RUNTIME_DIR=" + runtime_dir, "KAI_SESSION=99"});
        expect(maker.code == 0 && contains(maker.out, "started session 2 named inherit"),
               "new from inside a (fake) session still starts its own shell: " + maker.out);
        const Run typed = run({"send", "2", "echo SID=$KAI_SESSION", "--enter"}, here);
        expect(typed.code == 0 && contains(typed.out, "typed into session 2"),
               "send answers: " + typed.out);
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        const Run view = run({"attach", "2"}, here);
        expect(view.code == 0, "a piped attach ends cleanly when its input is done");
        expect(contains(view.out, "SID=2") && !contains(view.out, "SID=99"),
               "the shell carries the daemon's number, not its parent's");
    }

    // An id that is not there is refused by name.
    {
        const Run r = run({"attach", "999"}, here);
        expect(r.code == 1 && contains(r.out, "no such session: 999"),
               "attach to nothing says which nothing: " + r.out);
    }

    // kill by id, and the listing after it.
    {
        const Run k = run({"kill", "2"}, here);
        expect(k.code == 0 && contains(k.out, "ended session 2"), "kill answers: " + k.out);
        expect(!contains(listing(here), "inherit"), "the killed session is off the listing");
    }

    // Ctrl+S leaves an attached view and leaves the shell running — the way
    // out that did not exist before, when stopping watching a session meant
    // ending its shell. It runs over a real pty, because the key only means
    // something when there is a terminal to read it from and hand back.
    {
        const Run mk = run({"new", "escape"}, here);
        expect(mk.code == 0 && contains(mk.out, "started session 3 named escape"),
               "a session to attach and leave: " + mk.out);
        // The id, taken from what `new` printed rather than trusted to be 3.
        std::string sid;
        {
            const std::string pre = "started session ";
            const std::size_t p = mk.out.find(pre);
            if (p != std::string::npos) {
                const std::size_t s = p + pre.size();
                const std::size_t e = mk.out.find_first_not_of("0123456789", s);
                sid = mk.out.substr(s, e - s);
            }
        }
        int master = -1;
        struct winsize ws{};
        ws.ws_row = 24;
        ws.ws_col = 80;
        const pid_t child = ::forkpty(&master, nullptr, nullptr, &ws);
        if (child == 0) {
            ::setenv("XDG_RUNTIME_DIR", runtime_dir.c_str(), 1);
            ::unsetenv("KAI_SESSION");
            char* const av[] = {const_cast<char*>("kai-term"),
                                const_cast<char*>("attach"),
                                const_cast<char*>(sid.c_str()), nullptr};
            ::execv(KAI_TERM_BIN, av);
            _exit(127);
        }
        expect(child > 0, "the attached view started");
        // Let it attach and paint, draining what it prints so the pty does not fill.
        char drain[4096];
        for (int i = 0; i < 20; ++i) {
            struct pollfd p {master, POLLIN, 0};
            if (::poll(&p, 1, 50) <= 0)
                continue;
            if (::read(master, drain, sizeof(drain)) <= 0)
                break;
        }
        // The leave key.
        const char leave = 0x13;
        static_cast<void>(::write(master, &leave, 1));
        int st = 0;
        bool exited = false;
        for (int i = 0; i < 100 && !exited; ++i) {
            if (::waitpid(child, &st, WNOHANG) == child)
                exited = true;
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (master >= 0)
            ::close(master);
        expect(exited, "Ctrl+S made the attached view leave");
        expect(exited && WIFEXITED(st) && WEXITSTATUS(st) == 0,
               "and it left on purpose, status 0, not a killed shell");
        // The shell is still there, now kept, and findable.
        const std::string out = listing(here);
        expect(contains(out, "escape"), "the session is still listed after Ctrl+S: " + out);
    }

    // The daemon itself: shutdown is final, and the listing after it is honest.
    {
        const Run s = run({"shutdown"}, here);
        expect(s.code == 0 && contains(s.out, "shutting down"), "shutdown answers: " + s.out);
        bool gone = false;
        for (int i = 0; i < 50 && !gone; ++i) {
            gone = !contains(listing(here), "work");
            if (!gone)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        expect(gone, "after shutdown the listing is empty again");
    }

    // The help a person reads before typing anything.
    {
        const Run h = run({"--help"}, here);
        expect(h.code == 0 && contains(h.out, "attach ID"), "help shows the surface: " + h.out);
        expect(contains(h.out, "save [ID] [NAME]"), "help shows the argument-free save");
    }

    ::rmdir(runtime_dir.c_str());
    if (failures == 0) {
        std::printf("PASS: %d command surface checks passed\n", checks);
        return 0;
    }
    std::printf("FAIL: %d of %d command surface checks failed\n", failures, checks);
    return 1;
}
