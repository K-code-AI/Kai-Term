#pragma once

// The session daemon: the process that owns the shells.
//
// The reason this exists is that a terminal window is a fragile thing to keep a
// shell alive inside. Close the window and the shell dies with it; reboot nothing, but
// restart the program, and whatever was running is gone. Moving the pty and the
// screen model into a separate process means a window is then just a view of a
// session, and views come and go.
//
// Two rules shape everything below.
//
// The first is that a descriptor cannot be closed underneath a thread that is blocked
// reading it. So a session's reader thread owns the pty's master for as long as it
// lives: killing a session signals the child, the reader sees the end of the stream,
// and only then does the master get closed — by the thread that was reading it.
//
// The second is that no thread may be left holding a descriptor another thread is
// about to close. So a client socket belongs to the thread that serves it; the
// session only borrows it to write, under the session's client lock, and a thread
// being dropped is woken with shutdown() so it notices on its own and closes the
// descriptor itself.

#include <algorithm>
#include <dirent.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <chrono>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "../config/config.hpp"
#include "../ipc/protocol.hpp"
#include "../ipc/socket.hpp"
#include "../process/job.hpp"
#include "../process/pty.hpp"
#include "../session/layout.hpp"
#include "../session/snapshot.hpp"
#include "../terminal/screen.hpp"

namespace kai {

inline constexpr int kDaemonBacklog = 16;
inline constexpr int kDaemonPollMs = 100;
// A just-created unsaved session has no window attached yet. Give the window
// a moment to attach before the daemon decides it is idle.
inline constexpr int kDaemonIdleGraceMs = 5000;
inline constexpr int kDaemonStopWaitMs = 5000;

// How long a connection may take to introduce itself.
//
// A thread blocked in read() with no limit is a thread belonging to whoever opened the
// socket rather than to the daemon. Nothing real needs five seconds to send a hello —
// the client builds the frame before it connects — but a program that opens the socket
// and stays quiet would otherwise hold a thread, a descriptor and a place in the
// daemon's own bookkeeping until the daemon was restarted, and a daemon holding someone's
// shells is a daemon you have good reasons not to restart.
inline constexpr int kDaemonHelloTimeoutMs = 5000;

// How many retired workers may wait to be joined before the daemon collects them.
// Joining a thread that has already finished costs nothing but a syscall, so this is
// not about sparing work: it is about not letting the list of retired connections grow
// without limit on a daemon that has been up for a week.
inline constexpr std::size_t kFinishedWorkersToJoin = 32;

// How many connections may be served at once.
//
// One thread per connection is the right shape — a person typing at one window has
// nothing to do with a second window waiting for output — but "one thread per
// connection" with no ceiling is a resource the other end gets to decide. Over the
// ceiling the daemon simply stops accepting: the connections sit in the kernel's
// backlog and are served as slots come free, which is a slow moment rather than a
// daemon with two thousand threads in it.
inline constexpr int kDaemonMaxWorkers = 128;

// The daemon's socket is named after the protocol, not just after the daemon.
//
// A daemon left running by an older build answers our commands in the older way, and
// the ways it gets it wrong are the invisible kind: it has no `save`, so the keybind
// that keeps a shell says "unknown command", and it says nothing about whether a
// session was kept, so a listing hides the shells it is holding and a menu comes up
// empty. Both look like a terminal that does not work rather than a program talking to
// something older than itself. Naming the socket after the version means they cannot
// meet: a new build finds no daemon on its own socket and starts one, and the older
// one is reported instead of argued with — see foreignDaemonNotice().
inline std::string daemonSocketName() {
    return std::string("daemon-v") + kProtocolVersion;
}

inline std::string daemonSocketPath() { return sessionSocketPath(daemonSocketName()); }

// Names no session may take. "daemon" is in the list as well as the name in use, so a
// session can never sit on a socket a future or previous build would call its own.
inline bool nameReserved(std::string_view name) {
    return name == "daemon" || name.rfind("daemon-v", 0) == 0;
}

// Quote one word for the shell. A path with a quote in it is rare and a path with a
// space in it is Tuesday, and getting this wrong means a directory name can run a
// command.
inline std::string shellQuote(const std::string& word) {
    std::string out = "'";
    for (const char c : word) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    return out + "'";
}

struct SessionSpec {
    std::string name;
    std::string shell; // empty means whatever the environment asks for
    std::string cwd;   // empty means the daemon's own directory
    int rows = 24;
    int cols = 80;
    // A shell in a window starts unsaved: it is the window's shell, not something the
    // person has decided to keep. Saving is a thing they ask for.
    bool saved = true;
};

struct SessionInfo {
    // Handed out by the daemon in the order shells started, from 0, never reused while
    // that daemon lives. A name is something a person chose and can be typed; an id is
    // what `kai-term list` puts first and what a person types back when there is no name
    // worth reading, which is most of the time.
    int id = -1;
    std::string name;
    pid_t pid = 0;
    int rows = 0;
    int cols = 0;
    int clients = 0;
    // How many shells the session is running, and which of them a view is looking at. A
    // listing that said three while the machine ran one would be the worst kind of lie, so
    // these are counted from the shells rather than read off the arrangement.
    int panes = 1;
    int focused = 0;
    long long started = 0; // seconds since the epoch
    bool alive = true;
    bool saved = true;
    std::string foreground; // what is running out front, when it is not just the shell
};

inline Fields encodeSessionInfo(const SessionInfo& info) {
    return {
        {"id", std::to_string(info.id)},
        {"name", info.name},
        {"pid", std::to_string(static_cast<long long>(info.pid))},
        {"rows", std::to_string(info.rows)},
        {"cols", std::to_string(info.cols)},
        {"clients", std::to_string(info.clients)},
        {"panes", std::to_string(info.panes)},
        {"focused", std::to_string(info.focused)},
        {"started", std::to_string(info.started)},
        {"alive", info.alive ? "1" : "0"},
        {"saved", info.saved ? "1" : "0"},
        {"foreground", info.foreground},
    };
}

inline SessionInfo decodeSessionInfo(const Fields& fields) {
    SessionInfo info;
    info.id = std::stoi(fieldValue(fields, "id", "-1"));
    info.name = fieldValue(fields, "name");
    info.pid = static_cast<pid_t>(std::stoll(fieldValue(fields, "pid", "0")));
    info.rows = std::stoi(fieldValue(fields, "rows", "0"));
    info.cols = std::stoi(fieldValue(fields, "cols", "0"));
    info.clients = std::stoi(fieldValue(fields, "clients", "0"));
    info.panes = std::stoi(fieldValue(fields, "panes", "1"));
    info.focused = std::stoi(fieldValue(fields, "focused", "0"));
    info.started = std::stoll(fieldValue(fields, "started", "0"));
    info.alive = fieldValue(fields, "alive", "1") != "0";
    info.saved = fieldValue(fields, "saved", "1") != "0";
    info.foreground = fieldValue(fields, "foreground");
    return info;
}

namespace detail {

// Blocking read of one frame. Returns false on end of stream, a read error or a
// malformed stream — a caller that cannot resynchronise should hang up rather than
// guess at where the next frame starts.
inline bool nextFrame(int fd, FrameReader& reader, Frame& out) {
    for (;;) {
        if (reader.pop(out))
            return true;
        if (reader.error())
            return false;
        char buf[32768];
        const ssize_t n = readSome(fd, buf, sizeof(buf));
        if (n <= 0)
            return false;
        reader.append(std::string_view(buf, static_cast<std::size_t>(n)));
    }
}

inline bool sendFrame(int fd, MsgKind kind, const std::string& body = {}) {
    return writeAll(fd, encodeFrame(kind, body));
}

} // namespace detail

class Daemon {
public:
    Daemon() = default;
    ~Daemon() { stop(); }

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    bool start(std::string& error) {
        path_ = daemonSocketPath();
        if (path_.empty()) {
            error = "nowhere to put the daemon's socket";
            return false;
        }
        listener_ = bindListen(path_, kDaemonBacklog, error);
        if (listener_ < 0)
            return false;
        writePidFile();
        stopping_ = false;
        return true;
    }

    // Who owns the socket, written beside it for whoever is locked out of it.
    //
    // "a daemon is already listening" is a true answer and only half of one: it does
    // not say which process, and picking the live one out of a crowd that all call
    // themselves `kai-term serve` is exactly the hunt a launch storm turns into
    // `ps`-and-grep. Nothing about running depends on the file — a daemon that cannot
    // write it is still a daemon — and it is only read when a listener is answering,
    // so the one a killed daemon leaves behind cannot send anyone chasing a dead pid.
    std::string pidPath() const { return path_ + ".pid"; }

    void writePidFile() const {
        if (path_.empty())
            return;
        const std::string path = pidPath();
        const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0)
            return;
        const std::string text = std::to_string(static_cast<long>(::getpid())) + "\n";
        static_cast<void>(::write(fd, text.data(), text.size()));
        ::close(fd);
    }

    const std::string& socketPath() const { return path_; }
    int listener() const { return listener_; }

    // Whether the daemon still has something worth keeping alive: a saved session,
    // or a window attached to any session. An unsaved session with no window is not
    // a reason to stay; a crash saves the session, and a window that closes without
    // saving it destroys it. So no saved session and no window means the daemon can
    // stop itself.
    bool needed() {
        {
            std::lock_guard<std::mutex> lock(table_mutex_);
            for (const auto& [name, record] : table_)
                if (record->saved)
                    return true;
        }
        std::lock_guard<std::mutex> lock(all_clients_mutex_);
        return !all_clients_.empty();
    }

    // Accept connections until asked to stop. Each client is served on its own
    // thread, because a person typing at one window has nothing to do with a second
    // window waiting for output.
    void run() {
        while (!stopping_) {
            // Once a session has existed, the daemon is only needed while one is
            // saved or a window is attached. A just-created unsaved session has no
            // window attached yet, so wait a grace period before stopping: the window
            // that asked for it is about to attach.
            if (had_session_ && !needed()) {
                if (!idle_started_) {
                    idle_started_ = true;
                    idle_since_ = std::chrono::steady_clock::now();
                } else if (std::chrono::steady_clock::now() - idle_since_ >=
                           std::chrono::milliseconds(kDaemonIdleGraceMs)) {
                    stopping_ = true;
                    break;
                }
            } else {
                idle_started_ = false;
            }
            struct pollfd pfd {listener_, POLLIN, 0};
            const int ready = ::poll(&pfd, 1, kDaemonPollMs);
            if (stopping_)
                break;
            if (ready <= 0)
                continue;
            // At the ceiling, do not take the next connection at all. Left in the
            // kernel's backlog it waits politely and is served when a slot comes free;
            // accepted, it would be a thread and a descriptor with nothing to say.
            if (active_workers_ >= kDaemonMaxWorkers)
                continue;
            const int fd = ::accept(listener_, nullptr, nullptr);
            if (fd < 0)
                continue;
            // A connection that arrives while we are shutting down is refused rather
            // than served: it would be talking to a daemon that is on its way out.
            if (stopping_) {
                ::close(fd);
                break;
            }
            {
                std::lock_guard<std::mutex> lock(workers_mutex_);
                ++active_workers_;
                // The slot is chosen here and belongs to this one connection. A worker
                // that retired by reaching for "the last one" could be handed an index
                // another connection had already taken over, so it moves its own slot
                // out and leaves an empty one behind for the next connection to fill.
                const std::size_t slot = reuseOrAppendWorkerSlot();
                workers_[slot] = std::thread([this, fd, slot] {
                    serve(fd);
                    // Retire on the way out. A finished thread is no longer a thread —
                    // it is a few words of bookkeeping the kernel keeps until somebody
                    // joins it — and a daemon that kept every connection it had ever
                    // served would be holding the whole history of the day in order to
                    // shut down, joining thousands of threads that ended hours ago.
                    std::lock_guard<std::mutex> done(workers_mutex_);
                    --active_workers_;
                    // The slot may already belong to somebody else: a shutdown
                    // happening at this very moment swaps the slot table out and
                    // takes the handles with it, joining this thread on the way
                    // down. Reaching into the vector it replaced would be a read
                    // past its end; an empty or missing slot simply says the
                    // other half already picked this handle up.
                    if (slot < workers_.size() && workers_[slot].joinable())
                        finished_.push_back(std::move(workers_[slot]));
                    if (finished_.size() >= kFinishedWorkersToJoin)
                        joinFinishedLocked();
                });
            }
        }
    }

    void requestStop() { stopping_ = true; }

    // Hang up on everyone, take the shells with us, and leave no socket behind.
    //
    // A daemon that outlived its sessions would leave them with no master, and a
    // terminal without a master cannot be attached to again; a daemon that kept them
    // alive with nothing to show them would be running jobs nobody can see or stop.
    // So stopping means stopping, and `kai-term list` after that is empty.
    void stop() {
        stopping_ = true;
        {
            std::vector<std::shared_ptr<Record>> records;
            {
                std::lock_guard<std::mutex> lock(table_mutex_);
                for (const auto& [name, record] : table_)
                    records.push_back(record);
            }
            for (const auto& record : records)
                halt(*record);
        }
        // Every served client is woken so its read returns and the thread finishes.
        {
            std::lock_guard<std::mutex> lock(all_clients_mutex_);
            for (const int fd : all_clients_)
                ::shutdown(fd, SHUT_RDWR);
        }
        for (int waited = 0; waited < kDaemonStopWaitMs; waited += kDaemonPollMs) {
            bool busy = false;
            {
                std::lock_guard<std::mutex> lock(table_mutex_);
                busy = !table_.empty();
            }
            if (!busy)
                break;
            ::usleep(kDaemonPollMs * 1000);
        }
        std::vector<std::thread> workers;
        std::vector<std::thread> retired;
        {
            std::lock_guard<std::mutex> lock(workers_mutex_);
            workers.swap(workers_);
            retired.swap(finished_);
        }
        // The retired ones first: they have already ended, so this is the cheap half,
        // and it is the half that would otherwise grow with every window that has ever
        // opened and closed since the daemon started.
        for (std::vector<std::thread>* batch : {&retired, &workers})
            for (std::thread& worker : *batch)
                if (worker.joinable())
                    worker.join();
        if (listener_ >= 0) {
            ::close(listener_);
            listener_ = -1;
            ::unlink(path_.c_str());
            // Both names or neither: a socket that is gone and a pid file that stayed
            // is a note about a daemon nobody can find, pointing at a process number
            // the kernel is free to hand to something else tomorrow.
            ::unlink(pidPath().c_str());
        }
    }

    // One client from hello to hangup. Public so a test can drive it on a thread
    // without a second process.
    void serve(int fd) {
        {
            std::lock_guard<std::mutex> lock(all_clients_mutex_);
            all_clients_.push_back(fd);
        }
        serveConnected(fd);
        {
            std::lock_guard<std::mutex> lock(all_clients_mutex_);
            all_clients_.erase(std::remove(all_clients_.begin(), all_clients_.end(), fd),
                               all_clients_.end());
        }
        ::close(fd);
    }

    // Control interface, used both by the protocol and by the tests.
    // How far back a session remembers, from the same file the window reads. The daemon is
    // where the history a snapshot is built from actually lives, so a deep scrollback means
    // nothing unless it is honoured here too: a view asking for 20000 lines behind a daemon
    // still trimming at a thousand would get a thousand and no explanation why. Read once
    // per daemon, because a person editing the file mid-session is not going to rewrite the
    // history that has already been kept.
    static std::size_t scrollbackLimit() {
        static const std::size_t limit = [] {
            const Config config = loadConfig();
            return config.scrollback ? static_cast<std::size_t>(*config.scrollback)
                                     : Screen::kMaxHistory;
        }();
        return limit;
    }

    bool create(const SessionSpec& spec, std::string& error, int* created_id = nullptr) {
        if (!sessionNameOk(spec.name)) {
            error = "not a usable session name: " + spec.name;
            return false;
        }
        if (nameReserved(spec.name)) {
            error = "that name belongs to the daemon itself";
            return false;
        }
        if (spec.rows < 1 || spec.cols < 1) {
            error = "a session needs at least one row and one column";
            return false;
        }
        if (!spec.cwd.empty()) {
            struct stat st{};
            if (::stat(spec.cwd.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
                error = "not a directory: " + spec.cwd;
                return false;
            }
        }
        // The number is handed out before the shell starts, because the shell
        // carries it: `KAI_SESSION` in the child's environment is what lets an
        // argument-free `save` or `detach` know which session it is standing in.
        const int session_id = next_id_++;
        // The child starts in the directory it was given, rather than being sent a
        // `cd` command after it has started. Typing a command for the person is not
        // the same as starting the shell there, and a shell that rejects the directory
        // says so on its own terminal.

        auto record = std::make_shared<Record>();
        record->id = session_id;
        record->name = spec.name;
        record->shell_program = spec.shell;
        record->working_directory = spec.cwd;
        // How far back a session remembers, from the same file the window reads.
        // The daemon is where the history a snapshot is built from actually lives,
        // so a deep scrollback means nothing unless it is honoured here too: a view
        // asking for 20000 lines behind a daemon still trimming at a thousand would
        // get a thousand and no explanation why. Read once per daemon, because a
        // person editing the file mid-session is not going to rewrite the history
        // that has already been kept.
        // The one leaf of a new session's tree is the first place in the tab bar, and it
        // gets its shell the same way every later one will.
        auto shell = makeShell(*record, 0, spec.rows, spec.cols);
        record->shells.push_back(shell);
        record->rows = spec.rows;
        record->cols = spec.cols;
        record->started = static_cast<long long>(std::time(nullptr));
        record->saved = spec.saved;

        std::shared_ptr<Record> previous;
        {
            std::lock_guard<std::mutex> lock(table_mutex_);
            auto found = table_.find(spec.name);
            if (found != table_.end())
                previous = found->second;
            table_[spec.name] = record;
        }
        if (previous) {
            // Replacing a running session of the same name is what `new` says; the
            // old one is torn down rather than orphaned, because nothing could ever
            // reach it again.
            halt(*previous);
        }

        // The reader thread owns the record from here: it holds a reference of its
        // own, removes it from the table when the shell ends, and closes the master
        // last, from the thread that had been reading it.
        had_session_ = true;
        if (created_id)
            *created_id = record->id;
        std::thread([this, record, shell] { readShell(record, shell); }).detach();
        return true;
    }

    std::vector<SessionInfo> list() const {
        std::vector<SessionInfo> out;
        std::lock_guard<std::mutex> lock(table_mutex_);
        for (const auto& [name, record] : table_)
            out.push_back(describe(*record));
        std::sort(out.begin(), out.end(),
                  [](const SessionInfo& a, const SessionInfo& b) { return a.name < b.name; });
        return out;
    }

    bool kill(const std::string& name, std::string& error) {
        std::shared_ptr<Record> record;
        {
            std::lock_guard<std::mutex> lock(table_mutex_);
            record = resolveLocked(name);
            if (!record) {
                error = "no such session: " + name;
                return false;
            }
        }
        halt(*record);
        return true;
    }

    // Type into a session without becoming a view of it.
    // Type into a session. `pane` is a place in its tab bar, or -1 for the one it is
    // looking at: a script typing into a session with three panes has to be able to say
    // which one, or it is typing into whichever a person happened to click last.
    bool type(const std::string& name, const std::string& text, int pane = -1) {
        std::shared_ptr<Record> record;
        {
            std::lock_guard<std::mutex> lock(table_mutex_);
            record = resolveLocked(name);
        }
        if (!record || record->dead)
            return false;
        const std::shared_ptr<Record::Shell> shell =
            shellAt(record, pane < 0 ? record->focus_slot.load() : pane);
        if (!shell)
            return false;
        return shell->pty->write(text);
    }

    // Keep this one. A window's shell is unsaved until a person says otherwise, and
    // this is the saying; giving it a name at the same time is what makes it
    // findable afterwards. Renaming a saved session works the same way, and a name
    // already taken is refused rather than quietly taken over — the other session has
    // a shell in it that somebody is watching.
    // The name the session answers to when this returns, through `final_name`: a
    // person who typed `save 2 build` and is told "keeping build" should be hearing
    // the record's own name, not an echo of what they typed.
    bool save(const std::string& name, const std::string& wanted, std::string& error,
              std::string* final_name = nullptr) {
        std::lock_guard<std::mutex> lock(table_mutex_);
        // Resolved rather than looked up, because the handle may be an id — and this
        // function holds the table lock from its first line, so resolving is all it may do.
        const std::shared_ptr<Record> record = resolveLocked(name);
        if (!record) {
            error = "no such session: " + name;
            return false;
        }
        if (wanted.empty() || wanted == record->name) {
            record->saved = true;
            if (final_name)
                *final_name = record->name;
            return true;
        }
        if (!sessionNameOk(wanted)) {
            error = "not a usable session name: " + wanted;
            return false;
        }
        if (nameReserved(wanted)) {
            error = "that name belongs to the daemon itself";
            return false;
        }
        if (table_.find(wanted) != table_.end()) {
            error = "a session is already called " + wanted;
            return false;
        }
        table_.erase(record->name);  // the handle may have been an id, not this name
        record->name = wanted;
        record->saved = true;
        table_[wanted] = record;
        if (final_name)
            *final_name = record->name;
        return true;
    }

    // Throw a session away, shell and all, because the person closing the window said
    // to. This is the only way an unsaved session dies on purpose; losing its last
    // view without this call means the view did not choose to leave.
    bool discard(const std::string& name, std::string& error) {
        std::shared_ptr<Record> record;
        {
            std::lock_guard<std::mutex> lock(table_mutex_);
            record = resolveLocked(name);
            if (!record) {
                error = "no such session: " + name;
                return false;
            }
        }
        record->saved = false;
        destroy(record);
        return true;
    }

    // Kick the views without touching the shell.
    bool detachAll(const std::string& name, std::string& error) {
        std::shared_ptr<Record> record;
        {
            std::lock_guard<std::mutex> lock(table_mutex_);
            record = resolveLocked(name);
            if (!record) {
                error = "no such session: " + name;
                return false;
            }
        }
        std::lock_guard<std::mutex> lock(record->clients_mutex);
        // SHUT_RDWR, not SHUT_WR. Half of it has to be read-only on our side, because
        // the thread serving that view is blocked in read() *here*: only shutting down
        // our own receive side makes that return, and a thread left blocked would hold
        // the descriptor open and keep the view counted as watching. The other half
        // tells the program on the far end that the session is gone.
        for (const int fd : record->clients)
            ::shutdown(fd, SHUT_RDWR);
        return true;
    }

private:
    // A slot for the next connection: the first one left empty by a connection that
    // retired, or a new one. Called with workers_mutex_ held. Without the reuse the
    // vector would grow to the highest number of windows open at one moment and stay
    // there for the life of the daemon, holding a moved-from thread each.
    std::size_t reuseOrAppendWorkerSlot() {
        for (std::size_t i = 0; i < workers_.size(); ++i)
            if (!workers_[i].joinable())
                return i;
        workers_.emplace_back();
        return workers_.size() - 1;
    }

    // Collect the retired workers. Called with workers_mutex_ held. A finished thread
    // is only a few words of kernel bookkeeping, so this is tidying rather than saving;
    // what it does save is the shutdown, which would otherwise join every connection the
    // daemon ever served, however long ago they ended.
    void joinFinishedLocked() {
        for (std::thread& worker : finished_) {
            if (!worker.joinable())
                continue;
            // One entry in the list is the thread running this code: a retiring
            // worker moves its own handle out of the slots, and until the lambda
            // returns, that handle names a live thread — itself. Joining yourself
            // is a wait that can never be satisfied, and the runtime says so by
            // throwing. Detaching is the honest word: the thread ends the moment
            // its lambda returns, which is a few statements away.
            if (worker.get_id() == std::this_thread::get_id()) {
                worker.detach();
                continue;
            }
            worker.join();
        }
        finished_.clear();
    }

    struct Record {
        // Set once, when the shell is made, and read-only after that.
        int id = -1;
        std::string name;
        // Whether a person has asked for this one to be kept. Unsaved sessions are the
        // shells inside open windows: kept alive by the daemon so a crash cannot take
        // them, but destroyed when the window says it is closing.
        std::atomic<bool> saved{true};
        // One pane's shell: a program, the screen it is writing, and the size it was
        // told. A session used to *be* one of these; a pane is a shell (Kaida, 2026-11-23),
        // so a session holds one per pane and this is the thing that grows and shrinks when
        // the arrangement does.
        struct Shell {
            int pane = 0; // the leaf of the session's tree this shell was made for
            std::unique_ptr<Pty> pty;
            std::unique_ptr<Screen> screen;
            mutable std::mutex screen_mutex;
            std::atomic<int> rows{24};
            std::atomic<int> cols{80};
            std::atomic<bool> dead{false};
        };

        // One shell per pane. Exactly one today, because the session's tree has one leaf
        // until somebody splits it, which is why the accessor below is allowed to mean "the"
        // shell: the moment the tree can hold a second leaf, callers name the one they want.
        std::vector<std::shared_ptr<Shell>> shells;
        mutable std::mutex shells_mutex;

        // The descriptors looking at one place in the tab bar.
        std::vector<int> clientsSeeing(int slot) const {
            std::lock_guard<std::mutex> lock(clients_mutex);
            std::vector<int> out;
            for (const int fd : clients) {
                const auto seen = view_slot.find(fd);
                if ((seen == view_slot.end() ? focus_slot.load() : seen->second) == slot)
                    out.push_back(fd);
            }
            return out;
        }

        // A view whose place in the tab bar has gone away — the pane was closed, or its
        // program exited — looks at what the session is focused on now rather than at
        // nothing. Whoever moves it sends the screen it is moving to.
        void moveViewsOff(int slot) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (auto& view : view_slot) {
                if (view.second == slot)
                    view.second = focus_slot.load();
            }
        }

        int slotOf(int fd) const {
            std::lock_guard<std::mutex> lock(clients_mutex);
            const auto seen = view_slot.find(fd);
            return seen == view_slot.end() ? focus_slot.load() : seen->second;
        }

        std::shared_ptr<Shell> onlyShell() const {
            std::lock_guard<std::mutex> lock(shells_mutex);
            return shells.empty() ? nullptr : shells.front();
        }

        // What the shells were started with, so that a pane added later starts the same
        // program in the same directory as the first one: a split is a place to work, not a
        // different kind of place.
        std::string shell_program;
        std::string working_directory;
        // Which place in the tab bar a view is looking at. A view shows one pane at a time,
        // because a pane is a program and two programs' output interleaved onto one screen
        // is unreadable rather than twice as informative. A view that has never said is
        // taken to be looking at the pane the session is focused on.
        std::map<int, int> view_slot;
        std::atomic<int> focus_slot{0};
        // The tab bar as last announced, so a resize does not repeat a number nobody
        // asked about. Read and written under the same lock as the client list.
        std::pair<int, int> tabs_sent{-1, -1};
        std::vector<int> clients;
        // What each view looking at this session said its window is, by descriptor. The
        // session's size is worked out from all of them together — see `settleSessionSize`
        // — rather than from whoever sent a message last, and a view that goes away takes
        // its limit with it. Guarded by `clients_mutex` along with the list it parallels.
        std::map<int, ViewSize> views;
        // A listing reads the client count while a view is being added or dropped, so
        // the locks have to be taken through a const record.
        mutable std::mutex clients_mutex;
        std::atomic<bool> stopping{false};
        std::atomic<bool> dead{false};
        std::atomic<int> rows{24};
        std::atomic<int> cols{80};
        // How the session is arranged: its tabs, each a tree of panes with sizes. The
        // session owns this rather than any window showing it, so that a second window, or
        // a tty, or the window opened after the one that arranged it, all find the same
        // arrangement. Nothing draws it yet — the window takes its panes from here in the
        // next step — which is why it can be read and changed before it is visible.
        SessionLayout layout;
        mutable std::mutex layout_mutex;
        long long started = 0;
    };

    // What a handle names. A run of digits is an id, because that is what the first
    // column of a listing says and it would be strange to print a thing people cannot
    // use. A name always wins: somebody may have called a session "7" on purpose, and
    // taking a chosen name away is not how numbers get introduced.
    //
    // Called with the table lock held, which is also what makes reading `id` safe.
    std::shared_ptr<Record> resolveLocked(const std::string& handle) const {
        const auto found = table_.find(handle);
        if (found != table_.end())
            return found->second;
        if (handle.empty() || handle.size() > 9 ||
            handle.find_first_not_of("0123456789") != std::string::npos)
            return nullptr;
        const int wanted = std::stoi(handle);
        for (const auto& entry : table_)
            if (entry.second->id == wanted)
                return entry.second;
        return nullptr;
    }

    SessionInfo describe(const Record& record) const {
        SessionInfo info;
        info.name = record.name;
        info.rows = record.rows;
        info.cols = record.cols;
        info.started = record.started;
        info.alive = !record.dead;
        {
            std::lock_guard<std::mutex> lock(record.clients_mutex);
            info.clients = static_cast<int>(record.clients.size());
        {
            std::lock_guard<std::mutex> lock(record.shells_mutex);
            info.panes = static_cast<int>(record.shells.size());
        }
        info.focused = record.focus_slot;
        }
        // The reader thread reaps, so by the time anyone asks the child may be gone;
        // a listing that shows the pid of something already reaped would be a pid
        // that could belong to anything by then.
        const std::shared_ptr<Record::Shell> shell = record.onlyShell();
        if (shell && shell->pty && shell->pty->alive())
            info.pid = shell->pty->child();
        info.id = record.id;
        info.saved = record.saved;
        // Asked for on every listing, which is how often anyone looks. Asking the
        // kernel costs a syscall and answers a question no amount of bookkeeping
        // inside the daemon could: whether something is running out there right now.
        info.foreground = shell && shell->pty
                              ? foregroundJob(shell->pty->master(), shell->pty->child())
                              : std::string();
        return info;
    }

    // Make the shell go away. The master stays open so the reader can see the end of
    // the stream and close it itself.
    // Take a session out of the table and stop it. The check that the table still
    // holds *this* record matters: a session replaced by a newer one of the same name
    // must not have its replacement removed on its way out.
    void destroy(const std::shared_ptr<Record>& record) {
        {
            std::lock_guard<std::mutex> lock(table_mutex_);
            const auto found = table_.find(record->name);
            if (found != table_.end() && found->second == record)
                table_.erase(found);
        }
        halt(*record);
    }

    void halt(Record& record) {
        record.stopping = true;
        std::lock_guard<std::mutex> lock(record.shells_mutex);
        for (const std::shared_ptr<Record::Shell>& shell : record.shells)
            if (shell->pty)
                shell->pty->terminate();
    }

    // Reads one shell to the end. Every pane has a thread doing this for it, which is what
    // lets one pane's program block, print, or exit without holding up its neighbours.
    // One pane's worth of program, at the size its cell is.
    std::shared_ptr<Record::Shell> makeShell(Record& record, int slot, int rows, int cols) const {
        auto shell = std::make_shared<Record::Shell>();
        shell->pane = slot;
        shell->pty = std::make_unique<Pty>(rows, cols, record.shell_program,
                                          record.working_directory, std::to_string(record.id));
        shell->screen = std::make_unique<Screen>(rows, cols);
        shell->screen->setHistoryLimit(scrollbackLimit());
        shell->rows = rows;
        shell->cols = cols;
        return shell;
    }

    std::shared_ptr<Record::Shell> shellAt(const std::shared_ptr<Record>& record, int slot) const {
        std::lock_guard<std::mutex> lock(record->shells_mutex);
        for (const std::shared_ptr<Record::Shell>& shell : record->shells) {
            if (shell->pane == slot)
                return shell;
        }
        return nullptr;
    }

    static std::string snapshotOf(const Record::Shell& shell) {
        std::lock_guard<std::mutex> lock(shell.screen_mutex);
        return screenSnapshot(*shell.screen);
    }

    // Tell everyone looking at a session what its tab bar holds now. This goes to every
    // view rather than to the views on one pane, because the bar is one thing shared by
    // whoever is watching: a pane added from a second window, or from `set-layout` in a
    // terminal, has to appear in the first window too.
    void announceTabs(const std::shared_ptr<Record>& record) {
        int panes = 0;
        {
            std::lock_guard<std::mutex> lock(record->shells_mutex);
            panes = static_cast<int>(record->shells.size());
        }
        const int focus = record->focus_slot.load();
        std::vector<int> every;
        {
            std::lock_guard<std::mutex> lock(record->clients_mutex);
            if (record->tabs_sent.first == panes && record->tabs_sent.second == focus)
                return;
            record->tabs_sent = {panes, focus};
            every = record->clients;
        }
        const std::string body = encodeFields({{"panes", std::to_string(panes)},
                                               {"focused", std::to_string(focus)}});
        for (const int fd : every)
            static_cast<void>(detail::sendFrame(fd, MsgKind::Tabs, body));
    }

    // Bring the session's shells in line with its arrangement: every place in the tab bar
    // has a shell, a place that has gone loses its shell, and every shell is told the size
    // of the cell it owns — a shell told the window's width would wrap somewhere the window
    // does not, which makes a split a picture of a division rather than a division.
    //
    // A shell belongs to a *place* rather than to a node of the tree, because an arrangement
    // is rewritten wholesale (a preset, `set-layout`, a renumbering after a close) while the
    // work inside the shells goes on. Rebuilding the tree must not be a way of killing every
    // program in the session.
    void reconcileShells(const std::shared_ptr<Record>& record) {
        const std::vector<int> leaves = record->layout.layout.panes();
        const std::vector<CellRect> cells =
            record->layout.layout.rects(record->rows.load(), record->cols.load());

        std::vector<std::shared_ptr<Record::Shell>> fresh;
        std::vector<std::shared_ptr<Record::Shell>> retired;
        {
            std::lock_guard<std::mutex> lock(record->shells_mutex);
            while (record->shells.size() < leaves.size()) {
                const int slot = static_cast<int>(record->shells.size());
                const CellRect cell = static_cast<size_t>(slot) < cells.size()
                                          ? cells[static_cast<size_t>(slot)]
                                          : CellRect{record->rows.load(), record->cols.load(),
                                                     record->rows.load(), record->cols.load()};
                fresh.push_back(makeShell(*record, slot, cell.rows, cell.cols));
                record->shells.push_back(fresh.back());
            }
            // Places that no longer exist. Truncating rather than hunting: the tab bar is
            // the list, so anything past its end has no tab to belong to.
            retired.assign(record->shells.begin() + static_cast<long>(leaves.size()),
                           record->shells.end());
            record->shells.erase(record->shells.begin() + static_cast<long>(leaves.size()),
                                 record->shells.end());
            for (size_t slot = 0; slot < record->shells.size() && slot < cells.size(); ++slot) {
                const std::shared_ptr<Record::Shell>& shell = record->shells[slot];
                shell->pane = static_cast<int>(slot);
                const CellRect& cell = cells[slot];
                if (cell.rows < 1 || cell.cols < 1 ||
                    (cell.rows == shell->rows && cell.cols == shell->cols))
                    continue;
                // The pty before the screen: a program inside asks the terminal how wide it
                // is, so the model should agree with what the program is about to be told.
                shell->pty->resize(cell.rows, cell.cols);
                shell->rows = cell.rows;
                shell->cols = cell.cols;
                std::lock_guard<std::mutex> screen_lock(shell->screen_mutex);
                shell->screen->resize(cell.rows, cell.cols);
            }
        }

        // The pane the session is focused on, named by place rather than by tree node.
        const int focused_leaf = record->layout.layout.focused();
        int focus = 0;
        for (size_t i = 0; i < leaves.size(); ++i) {
            if (leaves[i] == focused_leaf)
                focus = static_cast<int>(i);
        }
        record->focus_slot = focus;
        {
            std::lock_guard<std::mutex> lock(record->clients_mutex);
            for (auto& view : record->view_slot) {
                if (view.second >= static_cast<int>(leaves.size()))
                    view.second = focus;
            }
        }

        for (const std::shared_ptr<Record::Shell>& shell : retired) {
            // Ended rather than orphaned: a program with no place in the tab bar can never
            // be shown again, and its own reader thread is what takes it away.
            shell->pty->terminate();
        }

        // Every view gets the screen of the place it is looking at, at the size that place
        // now has. A resize throws away what a screen was, and only the daemon can say what
        // the same work looks like afterwards.
        for (size_t slot = 0; slot < leaves.size(); ++slot) {
            const std::shared_ptr<Record::Shell> shell = shellAt(record, static_cast<int>(slot));
            if (!shell)
                continue;
            const std::string snapshot = snapshotOf(*shell);
            if (snapshot.empty())
                continue;
            for (const int fd : record->clientsSeeing(static_cast<int>(slot)))
                static_cast<void>(detail::sendFrame(fd, MsgKind::Snapshot, snapshot));
        }

        // A thread per pane, which is what lets one program block, print, or exit without
        // holding up its neighbours.
        for (const std::shared_ptr<Record::Shell>& shell : fresh)
            std::thread([this, record, shell] { readShell(record, shell); }).detach();

        announceTabs(record);
    }

    void readShell(std::shared_ptr<Record> record, std::shared_ptr<Record::Shell> shell) {
        char buf[16384];
        for (;;) {
            const ssize_t got = shell->pty->read(buf, sizeof(buf));
            if (got == 0 || (got < 0 && errno != EINTR))
                break; // the shell closed the terminal, or it is gone
            if (got < 0)
                continue;
            const std::string_view chunk(buf, static_cast<std::size_t>(got));
            std::string answer;
            std::vector<Screen::ClipboardRequest> copies;
            {
                std::lock_guard<std::mutex> lock(shell->screen_mutex);
                shell->screen->feed(chunk);
                answer = shell->screen->takeResponse();
                copies = shell->screen->takeClipboardRequests();
            }
            // The daemon is the terminal as far as the program inside is concerned, so
            // it is the daemon that has to answer when one asks what kind of terminal
            // this is, or where the cursor is. Nobody answering is not a silent
            // failure: fish waits ten seconds for a device attributes reply before it
            // gives up and says so on the screen, and tmux will not start properly at
            // all. A view that attaches later must not answer these as well, or the
            // program gets two replies and takes the wrong one.
            if (!answer.empty())
                static_cast<void>(shell->pty->write(answer));
            // One pane's output goes to the views looking at that pane, and nowhere else:
            // a pane is a program, and two programs' output interleaved onto one screen is
            // unreadable rather than twice as informative.
            const std::vector<int> watching = record->clientsSeeing(shell->pane);
            for (const Screen::ClipboardRequest& request : copies) {
                // Which clipboard it came from travels with it, so a view can put the
                // text where the program asked for it to go.
                broadcast(*record, MsgKind::Clipboard,
                          encodeFields({{"text", request.text},
                                        {"primary", request.primary ? "1" : "0"},
                                        // A program asking for the clipboard's contents
                                        // is the opposite of one offering them, and a
                                        // view that could not tell the two apart would
                                        // answer a request to read by emptying the
                                        // person's clipboard.
                                        {"query", request.query ? "1" : "0"}}));
            }
            const std::string bytes(chunk);
            for (const int fd : watching)
                static_cast<void>(detail::sendFrame(fd, MsgKind::Data, bytes));
        }
        static_cast<void>(shell->pty->reap());
        shell->dead = true;
        // A session is over when its last pane is. With one pane that is every time, and
        // with several it is the reason closing one pane's program must not be mistaken for
        // the shell having exited: the others are still running.
        bool last_one = false;
        {
            std::lock_guard<std::mutex> lock(record->shells_mutex);
            last_one = true;
            for (const std::shared_ptr<Record::Shell>& other : record->shells)
                if (!other->dead)
                    last_one = false;
        }
        if (!last_one) {
            // The session goes on around it: the views looking at that place are moved to
            // what the session is focused on now, and the shell comes off the list so that
            // the count a listing gives is the number of programs actually running.
            record->moveViewsOff(shell->pane);
            {
                std::lock_guard<std::mutex> lock(record->shells_mutex);
                const auto keep = std::remove_if(
                    record->shells.begin(), record->shells.end(),
                    [&shell](const std::shared_ptr<Record::Shell>& s) { return s == shell; });
                record->shells.erase(keep, record->shells.end());
                shell->pty->closeMaster();
            }
            // Said after the list is let go of: the announcement counts the shells, and a
            // lock taken twice by one thread is a thread that never wakes up again.
            announceTabs(record);
            return;
        }
        record->dead = true;

        // Tell the views before letting go, and wake them so they do not sit waiting
        // for output that will never come.
        const Fields exit_fields = {
            {"code", std::to_string(shell->pty->exitCode())},
            {"signal", std::to_string(shell->pty->exitSignal())},
        };
        const std::string exit_body = encodeFields(exit_fields);
        {
            std::lock_guard<std::mutex> lock(record->clients_mutex);
            for (const int fd : record->clients) {
                static_cast<void>(detail::sendFrame(fd, MsgKind::Exit, exit_body));
                ::shutdown(fd, SHUT_RDWR);
            }
        }

        {
            std::lock_guard<std::mutex> lock(table_mutex_);
            const auto found = table_.find(record->name);
            // Only our own entry. A session replaced by name while this shell was on
            // its way out has already taken the name, and erasing that would remove a
            // live session from the listing.
            if (found != table_.end() && found->second.get() == record.get())
                table_.erase(found);
        }
        // Closing here, on the thread that was reading, is the only point at which
        // nothing else can be waiting on that descriptor.
        shell->pty->closeMaster();
    }

    void broadcast(Record& record, MsgKind kind, const std::string& body) {
        std::vector<int> dropped;
        {
            std::lock_guard<std::mutex> lock(record.clients_mutex);
            for (const int fd : record.clients) {
                if (detail::sendFrame(fd, kind, body))
                    continue;
                dropped.push_back(fd);
                // A view that cannot be written to is gone or wedged. Waking it lets
                // its own thread close the descriptor; we never close one here.
                ::shutdown(fd, SHUT_RDWR);
            }
            for (const int fd : dropped) {
                record.clients.erase(
                    std::remove(record.clients.begin(), record.clients.end(), fd),
                    record.clients.end());
                // Its size stops counting here even though the session is not resettled
                // from inside this lock. The thread that owned it is being woken and will
                // settle things on its way out; holding the lock to do it sooner would be
                // resizing while writing screens to the very list being resized for.
                record.views.erase(fd);
            }
        }
    }

    // Settle the session's size from the views looking at it: the size every one of them
    // can show, which is the smallest. With nobody looking there is nothing to be the
    // smallest of, and the session keeps the size it had — a screen that shrank to a
    // default when the last window closed would throw away the very thing a keep is for.
    //
    // Takes no lock and must be called from outside one: settling resizes, and resizing
    // writes a fresh screen to every view, which cannot happen while the list of views is
    // held shut.
    void settleSessionSize(const std::shared_ptr<Record>& record) {
        ViewSize settled {record->rows.load(), record->cols.load()};
        {
            std::lock_guard<std::mutex> lock(record->clients_mutex);
            std::vector<ViewSize> sizes;
            for (const auto& view : record->views)
                sizes.push_back(view.second);
            settled = smallestView(sizes, settled);
        }
        resizeSession(record, settled.rows, settled.cols);
    }

    void resizeSession(const std::shared_ptr<Record>& record, int rows, int cols) {
        if (rows < 1 || cols < 1)
            return;
        if (rows == record->rows && cols == record->cols)
            return;
        // A screen is a thing of a size, so a resize throws away what every view of it
        // was shown, and only the daemon can say what the same screen looks like at the
        // new size.
        record->rows = rows;
        record->cols = cols;
        // Every shell takes the cell its pane owns and every view is given the screen of the
        // place it is looking at, which is what reconciling does.
        reconcileShells(record);
    }

    void attach(int fd, FrameReader& reader, const Fields& hello, const std::string& name) {
        std::shared_ptr<Record> record;
        {
            std::lock_guard<std::mutex> lock(table_mutex_);
            record = resolveLocked(name);
            if (!record) {
                static_cast<void>(detail::sendFrame(
                    fd, MsgKind::Error, encodeFields({{"message", "no such session: " + name}})));
                return;
            }
        }
        const int rows = std::stoi(fieldValue(hello, "rows", "0"));
        const int cols = std::stoi(fieldValue(hello, "cols", "0"));
        // This window's size is one more thing the session has to fit, not an order to
        // resize. Whoever spoke last used to win, which meant two windows on one session
        // reflowed the screen every time the other was touched, and neither got to keep
        // the layout it was showing.
        {
            std::lock_guard<std::mutex> lock(record->clients_mutex);
            record->views[fd] = ViewSize {rows, cols};
        }
        // Before the screen is sent, so that what a new view is shown is the screen at the
        // size it is about to be told about.
        settleSessionSize(record);

        std::string snapshot;
        {
            const std::shared_ptr<Record::Shell> shell =
                shellAt(record, record->focus_slot.load());
            if (shell)
                snapshot = snapshotOf(*shell);
        }
        const std::string welcome = encodeFields(encodeSessionInfo(describe(*record)));
        // Joining the list and being shown the screen happen under one lock, which is
        // what keeps the two in order: a shell that printed between them would
        // otherwise have its output arrive before the screen it is appended to, and a
        // view would be drawn a line from a moment it has not been shown yet.
        // Somebody is looking at it now, which is what a keep was holding the session
        // for. Taking it back into a window therefore uses the keep up: from here the
        // shell is an ordinary one, and only another save — the keybind, or the Detach
        // button on the way out — keeps it alive. It used to stay saved through this,
        // so a session Kaida had saved once went on saving itself every time it was
        // looked at, and closed without a question.
        record->saved = false;
        {
            std::lock_guard<std::mutex> lock(record->clients_mutex);
            record->clients.push_back(fd);
            if (!detail::sendFrame(fd, MsgKind::Welcome, welcome) ||
                !detail::sendFrame(fd, MsgKind::Snapshot, snapshot)) {
                record->clients.erase(
                    std::remove(record->clients.begin(), record->clients.end(), fd),
                    record->clients.end());
                return;
            }
        }

        // The descriptor belongs to this thread and to nobody else, so leaving means
        // taking ourselves off the list first: a session that kept writing to a
        // descriptor we have closed would be writing to whatever number we got next.
        // Whether this view is leaving because a person is closing it. Leaving that
        // unsaid is the interesting case: a crash, a kill -9, a machine that lost its
        // temper. None of those is somebody throwing a shell away, so an unsaved
        // session stays where it is and can be found again.
        bool closing_on_purpose = false;
        struct Guard {
            Daemon& daemon;
            const std::shared_ptr<Record>& record;
            int fd;
            const bool* closing;
            ~Guard() {
                {
                    std::lock_guard<std::mutex> lock(record->clients_mutex);
                    record->clients.erase(std::remove(record->clients.begin(),
                                                      record->clients.end(), fd),
                                          record->clients.end());
                    record->views.erase(fd);
                }
                if (*closing && !record->saved) {
                    daemon.destroy(record);
                    return;
                }
                // A limit stops being a limit when the thing imposing it goes away: the
                // windows still looking at this session get the size they were waiting for.
                daemon.settleSessionSize(record);
            }
        } guard{*this, record, fd, &closing_on_purpose};

        Frame frame;
        while (detail::nextFrame(fd, reader, frame)) {
            switch (frame.kind) {
                case MsgKind::Data: {
                    // The pane this view is looking at, which until the window can show
                    // more than one of them is the session's only pane.
                    const std::shared_ptr<Record::Shell> shell =
                        shellAt(record, record->slotOf(fd));
                    if (shell)
                        static_cast<void>(shell->pty->write(frame.body));
                    break;
                }
                case MsgKind::Resize: {
                    const Fields fields = decodeFields(frame.body);
                    const int want_rows = std::stoi(fieldValue(fields, "rows", "0"));
                    const int want_cols = std::stoi(fieldValue(fields, "cols", "0"));
                    {
                        std::lock_guard<std::mutex> lock(record->clients_mutex);
                        record->views[fd] = ViewSize {want_rows, want_cols};
                    }
                    settleSessionSize(record);
                    break;
                }
                case MsgKind::ShowPane: {
                    // Which place in the tab bar this view is looking at. The window keeps
                    // no copy of what is running, so a tab switch is this: remember the
                    // place for this descriptor, and send the screen of what is running
                    // there. From here the view's own typing follows it.
                    const Fields fields = decodeFields(frame.body);
                    // Asked for by number, and a number is not guaranteed: something on
                    // the other end of the socket could send anything, and a daemon that
                    // threw on `pane=banana` would take every session down with it.
                    const std::string wanted = fieldValue(fields, "pane", "-1");
                    const bool a_number = !wanted.empty() &&
                                          wanted.find_first_not_of("-0123456789") ==
                                              std::string::npos;
                    const int slot = a_number ? std::atoi(wanted.c_str()) : -1;
                    int places = 0;
                    {
                        std::lock_guard<std::mutex> lock(record->shells_mutex);
                        places = static_cast<int>(record->shells.size());
                    }
                    if (slot < 0 || slot >= places) {
                        // Refused out loud. A view that asked for pane 9 and was quietly
                        // left where it was would look like a tab bar that does not work,
                        // when the truth is one wrong number.
                        static_cast<void>(detail::sendFrame(
                            fd, MsgKind::Error,
                            encodeFields({{"reason", "no pane " + std::to_string(slot) +
                                                    " in that session; it has " +
                                                    std::to_string(places)}})));
                        break;
                    }
                    {
                        std::lock_guard<std::mutex> lock(record->clients_mutex);
                        record->view_slot[fd] = slot;
                    }
                    const std::shared_ptr<Record::Shell> shell = shellAt(record, slot);
                    if (shell) {
                        static_cast<void>(detail::sendFrame(fd, MsgKind::Snapshot,
                                                            snapshotOf(*shell)));
                    }
                    break;
                }
                case MsgKind::Detach:
                case MsgKind::Exit: {
                    // `discard=1` is a window saying it is being closed and this shell
                    // was not saved. Without it a detach means "stop showing it to me",
                    // which is the opposite of an instruction to destroy anything.
                    const Fields leaving = decodeFields(frame.body);
                    closing_on_purpose = fieldValue(leaving, "discard") == "1";
                    // The other word in the same frame. A view that leaves without
                    // saying "throw it away" is saying the opposite: keep this. That is
                    // the Detach button, and it has to be a save rather than a shell
                    // that is merely unloved and listed, because otherwise "detach" and
                    // "crashed" look identical to everything that comes after.
                    if (!closing_on_purpose)
                        record->saved = true;
                    return; // the view asks for nothing more
                }
                default:
                    break; // anything else a view sends is its own business
            }
        }
    }

    void serveConnected(int fd) {
        // The handshake is timed out, and only the handshake. Every command a real
        // client sends is built before it connects, so a hello that has not arrived
        // inside the limit is not a slow client — it is a socket that was opened and
        // left, and the thread reading it belongs to the daemon no longer.
        static_cast<void>(setReadTimeout(fd, kDaemonHelloTimeoutMs));
        // The socket is private to its owner, so this should be impossible. It is
        // cheap to check anyway, and what is being handed over is a shell.
        uid_t uid = 0;
        pid_t peer_pid = 0;
        if (peerCredentials(fd, uid, peer_pid) && uid != ::getuid()) {
            static_cast<void>(detail::sendFrame(
                fd, MsgKind::Error, encodeFields({{"message", "not your daemon"}})));
            return;
        }

        FrameReader reader;
        Frame hello;
        if (!detail::nextFrame(fd, reader, hello) || hello.kind != MsgKind::Hello) {
            static_cast<void>(detail::sendFrame(
                fd, MsgKind::Error, encodeFields({{"message", "expected a hello"}})));
            return;
        }
        const Fields fields = decodeFields(hello.body);
        const std::string command = fieldValue(fields, "command", "list");
        const std::string name = fieldValue(fields, "name");

        // Answer `ping` before the version check, because `ping` is how something
        // finds out whether it is talking to the right daemon at all.
        if (command == "ping") {
            static_cast<void>(detail::sendFrame(
                fd, MsgKind::Welcome,
                encodeFields({{"version", kProtocolVersion},
                              {"pid", std::to_string(::getpid())}})));
            return;
        }

        const std::string client_version = fieldValue(fields, "version");
        if (client_version != kProtocolVersion) {
            static_cast<void>(detail::sendFrame(
                fd, MsgKind::Error,
                encodeFields({{"message",
                               "protocol mismatch: this daemon speaks " +
                                   std::string(kProtocolVersion) + ", the client said " +
                                   (client_version.empty() ? "nothing" : client_version) +
                                   ". A daemon from another build of Kai-Term is holding "
                                   "these shells; `kai-term shutdown` stops it."}})));
            return;
        }
        if (command == "list") {
            std::vector<Fields> rows;
            for (const SessionInfo& info : list())
                rows.push_back(encodeSessionInfo(info));
            static_cast<void>(detail::sendFrame(fd, MsgKind::SessionList, encodeRecords(rows)));
            return;
        }
        if (command == "new") {
            SessionSpec spec;
            spec.name = name;
            spec.shell = fieldValue(fields, "shell");
            spec.cwd = fieldValue(fields, "cwd");
            spec.rows = std::stoi(fieldValue(fields, "rows", "24"));
            spec.cols = std::stoi(fieldValue(fields, "cols", "80"));
            spec.saved = fieldValue(fields, "saved", "1") != "0";
            std::string error;
            int created_id = -1;
            if (!create(spec, error, &created_id)) {
                static_cast<void>(
                    detail::sendFrame(fd, MsgKind::Error, encodeFields({{"message", error}})));
                return;
            }
            // The id comes back with the yes, because `kai-term new` promises to
            // print it, and a number the client had to look up after the fact is a
            // number that might belong to a different session by the time it is
            // looked up.
            static_cast<void>(detail::sendFrame(
                fd, MsgKind::Ack,
                encodeFields({{"name", name}, {"id", std::to_string(created_id)}})));
            return;
        }
        if (command == "kill") {
            std::string error;
            if (!kill(name, error)) {
                static_cast<void>(
                    detail::sendFrame(fd, MsgKind::Error, encodeFields({{"message", error}})));
                return;
            }
            static_cast<void>(detail::sendFrame(fd, MsgKind::Ack));
            return;
        }
        if (command == "save") {
            std::string error;
            const std::string wanted = fieldValue(fields, "as");
            std::string kept_name;
            if (!save(name, wanted, error, &kept_name)) {
                static_cast<void>(
                    detail::sendFrame(fd, MsgKind::Error, encodeFields({{"message", error}})));
                return;
            }
            static_cast<void>(detail::sendFrame(
                fd, MsgKind::Ack, encodeFields({{"name", kept_name}})));
            return;
        }
        if (command == "discard") {
            std::string error;
            if (!discard(name, error)) {
                static_cast<void>(
                    detail::sendFrame(fd, MsgKind::Error, encodeFields({{"message", error}})));
                return;
            }
            static_cast<void>(detail::sendFrame(fd, MsgKind::Ack));
            return;
        }
        if (command == "detach") {
            std::string error;
            if (!detachAll(name, error)) {
                static_cast<void>(
                    detail::sendFrame(fd, MsgKind::Error, encodeFields({{"message", error}})));
                return;
            }
            static_cast<void>(detail::sendFrame(fd, MsgKind::Ack));
            return;
        }
        if (command == "send") {
            // Keys from someone who is not looking at the session. A script that
            // wants to type into a shell should not have to become a window first,
            // and a window that attached only to type a line would show the answer to
            // nobody.
            // Same care as everywhere the wire is read: a pane that is not a number is
            // not a pane, and the answer is a refusal rather than a crashed daemon.
            const std::string asked = fieldValue(fields, "pane", "-1");
            const bool numeric = asked.size() > 0 &&
                                 asked.find_first_not_of("-0123456789") == std::string::npos;
            if (!type(name, fieldValue(fields, "text"),
                      numeric ? std::atoi(asked.c_str()) : -1)) {
                static_cast<void>(detail::sendFrame(
                    fd, MsgKind::Error,
                    encodeFields({{"message", "the session would not take the text"}})));
                return;
            }
            static_cast<void>(detail::sendFrame(fd, MsgKind::Ack));
            return;
        }
        if (command == "attach") {
            // A view watching a shell that has nothing to print is the one thing that
            // must not be on a clock: the hello limit is for the handshake, and a
            // person reading a page for ten minutes is not a dead connection.
            static_cast<void>(setReadTimeout(fd, 0));
            attach(fd, reader, fields, name);
            return;
        }
        if (command == "layout") {
            std::shared_ptr<Record> record;
            {
                std::lock_guard<std::mutex> lock(table_mutex_);
                record = resolveLocked(name);
            }
            if (!record) {
                static_cast<void>(detail::sendFrame(
                    fd, MsgKind::Error,
                    encodeFields({{"message", "no such session: " + name}})));
                return;
            }
            std::string text;
            {
                std::lock_guard<std::mutex> lock(record->layout_mutex);
                text = record->layout.toText();
            }
            static_cast<void>(detail::sendFrame(
                fd, MsgKind::Layout, encodeFields({{"name", record->name}, {"text", text}})));
            return;
        }
        if (command == "set-layout") {
            std::shared_ptr<Record> record;
            {
                std::lock_guard<std::mutex> lock(table_mutex_);
                record = resolveLocked(name);
            }
            if (!record) {
                static_cast<void>(detail::sendFrame(
                    fd, MsgKind::Error,
                    encodeFields({{"message", "no such session: " + name}})));
                return;
            }
            const std::string text = fieldValue(fields, "text");
            // Said out loud rather than read as "the default arrangement, please": empty
            // words parse into a window holding a pane, which is a thing to say but not a
            // thing anybody means by sending nothing.
            if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
                static_cast<void>(detail::sendFrame(
                    fd, MsgKind::Error,
                    encodeFields({{"message", "no arrangement in those words"}})));
                return;
            }
            // Read whole before anything is replaced. Half a layout accepted, with the
            // rest of it dropped, is a session quietly rearranged into something nobody
            // asked for — so the complaints come back instead, and the session goes on
            // carrying what it had.
            std::vector<std::string> problems;
            const SessionLayout replacement = SessionLayout::fromText(text, problems);
            if (!problems.empty()) {
                std::string why;
                for (const std::string& problem : problems) {
                    if (!why.empty())
                        why += "; ";
                    why += problem;
                }
                static_cast<void>(
                    detail::sendFrame(fd, MsgKind::Error, encodeFields({{"message", why}})));
                return;
            }
            {
                std::lock_guard<std::mutex> lock(record->layout_mutex);
                record->layout = replacement;
            }
            // The arrangement decides how many programs the session runs, which is the whole
            // reason it is worth saving: a record of panes with one shell behind it is a
            // picture of a workspace rather than one.
            reconcileShells(record);
            static_cast<void>(detail::sendFrame(
                fd, MsgKind::Ack, encodeFields({{"what", "layout"}})));
            return;
        }
        if (command == "shutdown") {
            static_cast<void>(detail::sendFrame(fd, MsgKind::Ack));
            stopping_ = true;
            return;
        }
        static_cast<void>(detail::sendFrame(fd, MsgKind::Error,
                                            encodeFields({{"message", "unknown command: " + command}})));
    }

    std::string path_;
    int listener_ = -1;
    std::atomic<bool> stopping_{false};
    // Whether any session has ever been created. Before one exists the daemon has
    // to stay for the first command; after one exists it can stop when nothing is
    // saved and no window is attached.
    std::atomic<bool> had_session_{false};
    // The grace period starts when the daemon first looks idle, and resets whenever
    // a window attaches or a session is saved.
    bool idle_started_ = false;
    std::chrono::steady_clock::time_point idle_since_{};
    // From 0, and never given out twice. A restarted daemon starts over, which is fine:
    // the shells it had are gone with it, so there is nothing for an old number to mean.
    std::atomic<int> next_id_{0};
    mutable std::mutex table_mutex_;
    std::map<std::string, std::shared_ptr<Record>> table_;
    std::mutex workers_mutex_;
    std::vector<std::thread> workers_;
    // Retired workers, waiting to be joined. Their threads have already ended; only the
    // record of them is left, and joining gives that back.
    std::vector<std::thread> finished_;
    // How many connections are being served right now, which is what the ceiling is on.
    std::atomic<int> active_workers_{0};
    std::mutex all_clients_mutex_;
    std::vector<int> all_clients_;
};

// The other half: what a window or the command line talks to the daemon with.
class DaemonClient {
public:
    ~DaemonClient() { close(); }

    bool connect(std::string& error) { return connectToPath(daemonSocketPath(), error); }

    // As `connect`, to a socket by name rather than to ours. Used to ask whatever
    // else is in the runtime directory what it speaks.
    bool connectToPath(const std::string& path, std::string& error) {
        if (path.empty()) {
            error = "nowhere to look for the daemon";
            return false;
        }
        fd_ = connectTo(path);
        if (fd_ < 0) {
            error = "cannot reach the daemon: " + std::string(std::strerror(errno));
            return false;
        }
        return true;
    }

    bool connected() const { return fd_ >= 0; }
    int descriptor() const { return fd_; }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    // Say what we want and wait for the caller to read the answer. The first reply to
    // an attach is the welcome, and a helper that ate it would leave the caller
    // reading a screen as if it were a greeting.
    bool request(Fields fields) {
        // Every hello says what the client speaks, so a daemon can refuse a
        // conversation it would otherwise take part in and get wrong.
        if (fieldValue(fields, "version").empty())
            fields.emplace_back("version", kProtocolVersion);
        return detail::sendFrame(fd_, MsgKind::Hello, encodeFields(fields));
    }

    bool send(MsgKind kind, const std::string& body = {}) {
        return detail::sendFrame(fd_, kind, body);
    }

    // One control round trip: ask, then take the single frame back.
    bool command(const Fields& fields, Frame& reply, std::string& error) {
        if (!request(fields)) {
            error = "the daemon would not listen";
            return false;
        }
        return nextFrame(reply, error);
    }

    bool nextFrame(Frame& out, std::string& error) {
        if (!detail::nextFrame(fd_, reader_, out)) {
            error = reader_.error() ? "the daemon sent a stream we cannot read"
                                    : "the daemon hung up";
            return false;
        }
        return true;
    }

    const Frame& last() const { return frame_; }

    // True when the reader is holding bytes that have not become a frame yet. A
    // connection being polled has to ask, or the end of the last read sits there
    // unread and looks like silence from a daemon that is in fact answering.
    bool buffered() const { return reader_.buffered() > 0; }

    // Take what the socket has to offer into our own reader, then hand over any whole
    // frames. Polling this connection and reading it with a separate reader would
    // lose the bytes the first read happened to take too far, which for an attach is
    // the screen itself.
    bool readNow(bool& hung_up) {
        char buf[32768];
        const ssize_t n = readSome(fd_, buf, sizeof(buf));
        if (n > 0)
            reader_.append(std::string_view(buf, static_cast<std::size_t>(n)));
        else if (n == 0)
            hung_up = true;
        else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            hung_up = true;
        return !reader_.error();
    }

    bool pop(Frame& out) { return reader_.pop(out); }

    // Attach and hand the bytes both ways to the given callbacks until the session
    // ends or a callback asks to stop. Keeping this generic is what lets the GUI take
    // it over later and a headless test drive it today.
    bool attach(const std::string& name, int rows, int cols,
                const std::function<bool(const std::string& bytes)>& on_output,
                std::string& error) {
        if (!request({{"command", "attach"},
                      {"name", name},
                      {"rows", std::to_string(rows)},
                      {"cols", std::to_string(cols)}})) {
            error = "the daemon would not listen";
            return false;
        }
        Frame welcome;
        if (!nextFrame(welcome, error) || welcome.kind != MsgKind::Welcome) {
            error = welcome.kind == MsgKind::Error ? fieldValue(decodeFields(welcome.body),
                                                                "message", "the daemon refused")
                                                   : "the daemon did not welcome us";
            return false;
        }
        Frame frame;
        while (nextFrame(frame, error)) {
            if (frame.kind == MsgKind::Snapshot || frame.kind == MsgKind::Data) {
                if (on_output && !on_output(frame.body))
                    return true;
                continue;
            }
            if (frame.kind == MsgKind::Exit)
                return true;
        }
        return true;
    }

private:
    int fd_ = -1;
    FrameReader reader_;
    Frame frame_;
};

// Another build's daemon, if one is running beside ours.
//
// The daemon's socket is named for the protocol, which is what stops a new build from
// sending `save` to a daemon that has never heard of it. The cost of that is that the
// older daemon's shells are now invisible: still running, still holding a terminal and
// their memory, and not listed by anything this build can run. That is worth saying
// once, out loud, at the one moment we are in a position to notice.
//
// Something already running is never touched here. A daemon holding shells belongs to
// whoever started them, and a terminal that killed other people's sessions to tidy up
// after an upgrade would be a worse surprise than a line of text.
inline std::string foreignDaemonNotice() {
    const std::string ours = daemonSocketPath();
    const std::string::size_type slash = ours.find_last_of('/');
    if (slash == std::string::npos)
        return {};
    const std::string dir = ours.substr(0, slash);

    std::string first;
    int others = 0;
    DIR* handle = ::opendir(dir.c_str());
    if (handle == nullptr)
        return {};
    while (dirent* entry = ::readdir(handle)) {
        const std::string name = entry->d_name;
        if (name.rfind("daemon", 0) != 0 || name.size() <= 5 ||
            name.compare(name.size() - 5, 5, ".sock") != 0 || dir + "/" + name == ours)
            continue;
        DaemonClient other;
        std::string error;
        if (!other.connectToPath(dir + "/" + name, error))
            continue; // a file left behind by a daemon that has gone
        Frame reply;
        if (!other.command({{"command", "ping"}}, reply, error) ||
            reply.kind != MsgKind::Welcome)
            continue;
        const Fields fields = decodeFields(reply.body);
        const std::string version = fieldValue(fields, "version");
        if (version == kProtocolVersion)
            continue; // ours, under a name this build no longer uses
        ++others;
        if (!first.empty())
            continue;
        const std::string pid = fieldValue(fields, "pid");
        first = "a session daemon from another build of Kai-Term is running on " + dir +
                "/" + name + " (protocol " +
                (version.empty() ? std::string("1 or older") : version) +
                "; this build is " + kProtocolVersion + ")";
        if (!pid.empty())
            first += ", pid " + pid;
        first += ". Its shells are still running but this build cannot reach them; stop "
                 "it with kill " +
                 (pid.empty() ? std::string("<its pid>") : pid) + ".";
    }
    ::closedir(handle);
    if (first.empty())
        return {};
    if (others > 1)
        first += " (and " + std::to_string(others - 1) + " more like it)";
    return first;
}

} // namespace kai
