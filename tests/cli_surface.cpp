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
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <string>
#include <sys/file.h>
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
// /dev/null (or holding `stdin_text`), and collect everything it prints. A command that has not finished
// in fifteen seconds is not slow, it is stuck, and a stuck test is a hung suite.
// The exit status is taken exactly once, from the one wait() that gets it.
//
// `stdin_text` is there because some commands take several lines, and the way a person
// hands several lines to a program is a redirect. A check that wants to exercise that has
// to hand them over the same way, not through a flag nobody would ever type.
Run run(const std::vector<std::string>& args, const std::vector<std::string>& env = {},
        const std::string& stdin_text = {}) {
    Run result;
    int input_fd = -1;
    if (!stdin_text.empty()) {
        char name[] = "/tmp/kai-cli-stdin-XXXXXX";
        const int fd = ::mkstemp(name);
        if (fd >= 0) {
            static_cast<void>(::write(fd, stdin_text.data(), stdin_text.size()));
            ::lseek(fd, 0, SEEK_SET);
            input_fd = fd;
        }
        ::unlink(name); // the descriptor stays open for the child; the name need not
    }
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
        if (input_fd >= 0) {
            ::dup2(input_fd, STDIN_FILENO);
            ::close(input_fd);
        } else {
            const int devnull = ::open("/dev/null", O_RDONLY);
            if (devnull >= 0) {
                ::dup2(devnull, STDIN_FILENO);
                if (devnull > 2)
                    ::close(devnull);
            }
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
    if (input_fd >= 0)
        ::close(input_fd); // the child has its own copy; ours is nobody's business

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

std::string readWholeFile(const std::string& path) {
    std::string out;
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return out;
    char buf[8192];
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0)
            break;
        out.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fd);
    return out;
}

// Which processes are daemons *for this runtime directory*, by pid.
//
// Going by process name would count every kai-term on the machine, including a real
// one belonging to whoever is running the test; what makes a daemon this test's is the
// runtime directory it was started with. The marker is part of the question too, so
// finding exactly one daemon is also finding that the launched process knows what it
// is — which is the thing that keeps a launch from becoming a chain of them.
std::vector<std::string> daemonsFor(const std::string& dir) {
    std::vector<std::string> found;
    DIR* proc = ::opendir("/proc");
    if (proc == nullptr)
        return found;
    const std::string mine = "XDG_RUNTIME_DIR=" + dir;
    while (dirent* entry = ::readdir(proc)) {
        const std::string name = entry->d_name;
        if (name.empty() || name[0] < '0' || name[0] > '9')
            continue;
        if (readWholeFile("/proc/" + name + "/cmdline").find("serve") == std::string::npos)
            continue;
        const std::string env = readWholeFile("/proc/" + name + "/environ");
        if (env.find(mine) != std::string::npos &&
            env.find("KAI_TERM_DAEMON=1") != std::string::npos)
            found.push_back(name);
    }
    ::closedir(proc);
    return found;
}

// The lock a daemon launch takes, found by looking rather than by rebuilding its name:
// the socket is named after the protocol version, and a test should not have to know
// which one that is today.
std::string launchLockIn(const std::string& runtime_dir) {
    const std::string dir = runtime_dir + "/kai-term";
    DIR* handle = ::opendir(dir.c_str());
    if (handle == nullptr)
        return {};
    std::string found;
    while (dirent* entry = ::readdir(handle)) {
        const std::string name = entry->d_name;
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".lock") == 0) {
            found = dir + "/" + name;
            break;
        }
    }
    ::closedir(handle);
    return found;
}

std::string joined(const std::vector<std::string>& parts) {
    std::string out;
    for (const std::string& part : parts) {
        if (!out.empty())
            out += " ";
        out += part;
    }
    return out;
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

    // The words themselves: one row per thing that can be typed, one page per
    // command, and a mistake answered with the page rather than with a window.
    // (Kaida, 2026-11-20: "using an invalid argument should open the --help menu
    // listing all possible argument to use with kai-term. and the --help should be
    // rewritten to be clear with 1 argument per row following by a little description.
    // for more specific the help command should be used with an arguement
    // ex : kai-term list --help".)
    {
        const Run all = run({"--help"}, here);
        expect(all.code == 0, "--help exits 0, got " + std::to_string(all.code));
        expect(contains(all.out, "-c, --config FILE") && contains(all.out, "--config-dir DIR"),
               "--help lists both ways to point at a configuration: " + all.out);
        expect(contains(all.out, "  list  ") && contains(all.out, "  kill ID  "),
               "--help gives every command a row of its own: " + all.out);

        const Run one = run({"list", "--help"}, here);
        expect(one.code == 0, "`list --help` exits 0, got " + std::to_string(one.code));
        expect(contains(one.out, "kai-term list") && !contains(one.out, "Commands"),
               "and answers with the page for that command alone: " + one.out);

        const Run typed = run({"help", "attach"}, here);
        expect(typed.code == 0, "`help attach` exits 0, got " + std::to_string(typed.code));
        expect(contains(typed.out, "attach ID | -n NAME [--window]") &&
                   contains(typed.out, "--window"),
               "and `help <command>` is the same page: " + typed.out);

        const Run typo = run({"--confg-dir", "/tmp"}, here);
        expect(typo.code == 2,
               "a misspelt option exits 2, got " + std::to_string(typo.code));
        expect(contains(typo.out, "unknown option") && contains(typo.out, "-c, --config FILE"),
               "and is answered with the list of options, not with a window: " + typo.out);

        const Run wrong_flag = run({"list", "--pretty"}, here);
        expect(wrong_flag.code == 2,
               "a flag the command does not take exits 2, got " + std::to_string(wrong_flag.code));
        expect(contains(wrong_flag.out, "unknown option --pretty") &&
                   contains(wrong_flag.out, "kai-term list"),
               "and says which command it was asking about: " + wrong_flag.out);

        const Run hungry = run({"-c"}, here);
        expect(hungry.code == 2, "`-c` with nothing after it exits 2");
        expect(contains(hungry.out, "needs a value"),
               "and says what it was missing: " + hungry.out);

        // `-c FILE` is taken out of the words before anything else reads them, so the
        // command after it is the first word again. Left in place, this would be an
        // unknown option — which is a different failure, and now a loud one.
        const Run after = run({"-c", "/nonexistent/kai.conf", "--version"}, here);
        expect(after.code == 0 && contains(after.out, "Kai-Term "),
               "-c before a command leaves the command where it was: " + after.out);

        const Run absent = run({"--config", "/nonexistent/kai.conf", "list"}, here);
        expect(absent.code == 0 && contains(absent.out, "no sessions"),
               "a configuration file that is not there is not a crisis: " + absent.out);
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

    // One daemon, and never a second one started by the first.
    //
    // The launch execs `/proc/self/exe` and trusts that whatever comes back reads the
    // word `serve` and goes and sits on a socket. A helper built from these same
    // sources does not: it opens a window, asks for a daemon, and starts another copy
    // of itself to be one — which is how one command became five hundred processes and
    // a machine at load 270, all of them claiming to be `kai-term serve`. Two things
    // stop that: the marker a launched process carries, so that whatever it turns out
    // to be knows it was started to be the daemon and does not go looking for one; and
    // the lock that makes "is anyone home?" and "then start one" a single gesture
    // rather than a race. Checked through the real binary, in a runtime directory of
    // its own so the counts below are this test's and nobody else's.
    char alone_tmpl[] = "/tmp/kai-cli-alone-XXXXXX";
    const std::string alone_dir = ::mkdtemp(alone_tmpl) ? alone_tmpl : "/tmp/kai-cli-none";
    const std::vector<std::string> alone = {"XDG_RUNTIME_DIR=" + alone_dir};
    {
        const Run r = run({"new", "storm"},
                          {"XDG_RUNTIME_DIR=" + alone_dir, "KAI_TERM_DAEMON=1"});
        expect(r.code != 0, "a daemon asked for another one fails instead of obliging: " + r.out);
        expect(contains(r.out, "does not start another"), "and says why: " + r.out);
        expect(daemonsFor(alone_dir).empty(),
               "and nothing was left running: " + joined(daemonsFor(alone_dir)));
        expect(contains(run({"list"}, alone).out, "no sessions"),
               "and the socket is empty afterwards");
    }
    {
        // Six commands in the same moment. Each one used to answer its own "nobody is
        // home" with a fork, and the socket has room for one winner; what the rest
        // contributed was processes. What is checked here is the outcome — everyone
        // ends up with a daemon and exactly one exists — which is what the person
        // typing notices; the lock that produces it is checked on its own below.
        std::vector<pid_t> kids;
        for (int i = 0; i < 6; ++i) {
            const pid_t kid = ::fork();
            if (kid == 0) {
                ::setenv("XDG_RUNTIME_DIR", alone_dir.c_str(), 1);
                ::unsetenv("KAI_TERM_DAEMON");
                const int null_fd = ::open("/dev/null", O_WRONLY);
                if (null_fd >= 0) {
                    ::dup2(null_fd, STDOUT_FILENO);
                    ::dup2(null_fd, STDERR_FILENO);
                }
                char* const av[] = {const_cast<char*>("kai-term"),
                                    const_cast<char*>("start"), nullptr};
                ::execv(KAI_TERM_BIN, av);
                _exit(127);
            }
            kids.push_back(kid);
        }
        int answered = 0;
        for (const pid_t kid : kids) {
            int st = 0;
            static_cast<void>(::waitpid(kid, &st, 0));
            if (WIFEXITED(st) && WEXITSTATUS(st) == 0)
                ++answered;
        }
        expect(answered == 6,
               "all six got a daemon rather than a loss, got " + std::to_string(answered));
        const std::vector<std::string> daemons = daemonsFor(alone_dir);
        expect(daemons.size() == 1, "one daemon for six requests, got " +
                                        std::to_string(daemons.size()) + ": " + joined(daemons));
    }
    {
        // The number the daemon left beside its socket is its own, so whoever was
        // locked out can name the process instead of hunting it through `ps`.
        const Run s = run({"start"}, alone);
        expect(s.code == 0 && contains(s.out, "already running, at pid "),
               "start names the daemon it found: " + s.out);
        const std::vector<std::string> daemons = daemonsFor(alone_dir);
        expect(daemons.size() == 1 && contains(s.out, "at pid " + daemons[0]),
               "and the number it names is the daemon's: " + s.out);
    }
    {
        // A shell is not a daemon. The marker is cleared on the way in, or a `kai-term
        // new` run from inside a session whose daemon had died would be answered "the
        // daemon does not start another daemon" by someone standing in a shell.
        const Run mk = run({"new", "clean"}, alone);
        expect(mk.code == 0 && contains(mk.out, "started session "), "a shell to look inside: " + mk.out);
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
        // The shell writes what it was given to a file rather than to a screen: an
        // attach reads a moment in a redraw, and whether a variable is set is not
        // something to judge out of a half-painted prompt.
        const std::string probe = alone_dir + "/marker-probe.txt";
        static_cast<void>(::unlink(probe.c_str()));
        static_cast<void>(run({"send", sid,
                               "echo MARKER=$KAI_TERM_DAEMON SID=$KAI_SESSION > " + probe,
                               "--enter"},
                              alone));
        std::string written;
        for (int i = 0; i < 50 && written.empty(); ++i) {
            written = readWholeFile(probe);
            if (written.empty())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        expect(contains(written, "SID=" + sid),
               "the probe ran, and the shell knows its own number: " + written);
        expect(!contains(written, "MARKER=1"),
               "the daemon's marker does not reach its shells: " + written);
        static_cast<void>(::unlink(probe.c_str()));
    }
    {
        const Run s = run({"shutdown"}, alone);
        expect(s.code == 0 && contains(s.out, "shutting down"), "the private daemon stops: " + s.out);
    }
    {
        // The lock, asked for directly rather than inferred from a race. Somebody else
        // is starting a daemon — here, forever, since this test holds the lock and never
        // lets go — so the second command waits, says so, and above all does not fork a
        // daemon of its own. That fork was the pile-up: one question whose every
        // no-answer turned into a process.
        const std::string lock = launchLockIn(alone_dir);
        expect(!lock.empty(), "a launch left its lock beside the socket: " + alone_dir);
        const int held = ::open(lock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        expect(held >= 0 && ::flock(held, LOCK_EX | LOCK_NB) == 0, "the test can hold the lock");
        const Run r = run({"start"}, alone);
        expect(r.code != 0 && contains(r.out, "another command is starting the daemon"),
               "a launch already under way is reported, not doubled: " + r.out);
        expect(daemonsFor(alone_dir).empty(), "and nothing was started behind the lock: " +
                                                  joined(daemonsFor(alone_dir)));
        if (held >= 0) {
            ::flock(held, LOCK_UN);
            ::close(held);
        }
        // Let go, and the same command works: waiting was the point, not refusing.
        const Run after = run({"start"}, alone);
        expect(after.code == 0 && daemonsFor(alone_dir).size() == 1,
               "and the launch goes ahead once the lock is let go: " + after.out);
        const Run done = run({"shutdown"}, alone);
        expect(done.code == 0, "and stops again: " + done.out);
    }

    // The arrangement a session carries, read and written from a shell. Nothing on screen
    // follows these words yet — that is the next step of "Replacing tmux" — so what is
    // checked here is that the words survive the trip out of the daemon and back, and that
    // a bad one is refused rather than half applied.
    {
        const Run mk = run({"new", "arranged"}, here);
        expect(mk.code == 0, "a shell to arrange: " + mk.out);
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
        const Run shown = run({"layout", sid}, here);
        expect(shown.code == 0, "its arrangement can be read: " + shown.out);
        expect(contains(shown.out, "view side-by-side") && contains(shown.out, "pane"),
               "a shell nobody has split is one pane shown side by side: " + shown.out);

        const Run given = run({"set-layout", sid}, here,
                              "view tabs\nsplit left-right 0.5\n  pane\n  pane\n");
        expect(given.code == 0 && contains(given.out, "arranged session"),
               "an arrangement can be handed over: " + given.out);
        const Run holding = run({"layout", sid}, here);
        expect(contains(holding.out, "view tabs") &&
                   contains(holding.out, "split left-right 0.500000"),
               "and the shell holds it, view and divider and all: " + holding.out);

        const Run bad = run({"set-layout", sid}, here, "split sideways\n  pane\n  pane\n");
        expect(bad.code != 0 && contains(bad.out, "left-right"),
               "a split that is not a direction is refused, and says what would be: " + bad.out);
        const Run still_there = run({"layout", sid}, here);
        expect(contains(still_there.out, "view tabs") &&
                   contains(still_there.out, "split left-right"),
               "and the arrangement that was there is still there: " + still_there.out);

        const Run nothing = run({"set-layout", sid}, here);
        expect(nothing.code != 0 && contains(nothing.out, "no arrangement"),
               "and saying nothing at all is not an arrangement: " + nothing.out);

        static_cast<void>(run({"kill", sid}, here));
    }

    // The help a person reads before typing anything.
    {
        const Run h = run({"--help"}, here);
        expect(h.code == 0 && contains(h.out, "attach ID"), "help shows the surface: " + h.out);
        expect(contains(h.out, "save [ID] [NAME]"), "help shows the argument-free save");
        expect(contains(h.out, "set-layout ID < FILE"), "help shows how an arrangement comes in");
    }

    ::rmdir(runtime_dir.c_str());
    ::rmdir(alone_dir.c_str());
    if (failures == 0) {
        std::printf("PASS: %d command surface checks passed\n", checks);
        return 0;
    }
    std::printf("FAIL: %d of %d command surface checks failed\n", failures, checks);
    return 1;
}
