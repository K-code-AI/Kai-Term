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
#include <atomic>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "../ipc/protocol.hpp"
#include "../ipc/socket.hpp"
#include "../process/job.hpp"
#include "../process/pty.hpp"
#include "../session/snapshot.hpp"
#include "../terminal/screen.hpp"

namespace kai {

inline constexpr int kDaemonBacklog = 16;
inline constexpr int kDaemonPollMs = 100;
inline constexpr int kDaemonStopWaitMs = 5000;

// Reserved because the daemon's own socket lives beside the sessions'. A session
// called "daemon" would be a socket the daemon has already taken.
inline constexpr const char* kDaemonSocketName = "daemon";

inline std::string daemonSocketPath() { return sessionSocketPath(kDaemonSocketName); }

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
    std::string name;
    pid_t pid = 0;
    int rows = 0;
    int cols = 0;
    int clients = 0;
    long long started = 0; // seconds since the epoch
    bool alive = true;
    bool saved = true;
    std::string foreground; // what is running out front, when it is not just the shell
};

inline Fields encodeSessionInfo(const SessionInfo& info) {
    return {
        {"name", info.name},
        {"pid", std::to_string(static_cast<long long>(info.pid))},
        {"rows", std::to_string(info.rows)},
        {"cols", std::to_string(info.cols)},
        {"clients", std::to_string(info.clients)},
        {"started", std::to_string(info.started)},
        {"alive", info.alive ? "1" : "0"},
        {"saved", info.saved ? "1" : "0"},
        {"foreground", info.foreground},
    };
}

inline SessionInfo decodeSessionInfo(const Fields& fields) {
    SessionInfo info;
    info.name = fieldValue(fields, "name");
    info.pid = static_cast<pid_t>(std::stoll(fieldValue(fields, "pid", "0")));
    info.rows = std::stoi(fieldValue(fields, "rows", "0"));
    info.cols = std::stoi(fieldValue(fields, "cols", "0"));
    info.clients = std::stoi(fieldValue(fields, "clients", "0"));
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
        stopping_ = false;
        return true;
    }

    const std::string& socketPath() const { return path_; }
    int listener() const { return listener_; }

    // Accept connections until asked to stop. Each client is served on its own
    // thread, because a person typing at one window has nothing to do with a second
    // window waiting for output.
    void run() {
        while (!stopping_) {
            struct pollfd pfd {listener_, POLLIN, 0};
            const int ready = ::poll(&pfd, 1, kDaemonPollMs);
            if (stopping_)
                break;
            if (ready <= 0)
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
                workers_.emplace_back([this, fd] { serve(fd); });
            }
        }
    }

    void requestStop() { stopping_ = true; }

    // Hang up on everyone, take the shells with us, and leave no socket behind.
    //
    // A daemon that outlived its sessions would leave them with no master, and a
    // terminal without a master cannot be attached to again; a daemon that kept them
    // alive with nothing to show them would be running jobs nobody can see or stop.
    // So stopping means stopping, and `kai-term session list` after that is empty.
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
        {
            std::lock_guard<std::mutex> lock(workers_mutex_);
            workers.swap(workers_);
        }
        for (std::thread& worker : workers) {
            if (worker.joinable())
                worker.join();
        }
        if (listener_ >= 0) {
            ::close(listener_);
            listener_ = -1;
            ::unlink(path_.c_str());
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
    bool create(const SessionSpec& spec, std::string& error) {
        if (!sessionNameOk(spec.name)) {
            error = "not a usable session name: " + spec.name;
            return false;
        }
        if (spec.name == kDaemonSocketName) {
            error = "that name belongs to the daemon itself";
            return false;
        }
        if (spec.rows < 1 || spec.cols < 1) {
            error = "a session needs at least one row and one column";
            return false;
        }
        auto pty = std::make_unique<Pty>(spec.rows, spec.cols, spec.shell);
        if (!spec.cwd.empty() && pty->alive()) {
            // The child is already running by the time we get here, so the way to
            // start it somewhere else is to ask it, once, before anything else is
            // typed. A shell that rejects the directory says so on its own terminal.
            pty->write("cd " + shellQuote(spec.cwd) + "\n");
        }

        auto record = std::make_shared<Record>();
        record->name = spec.name;
        record->pty = std::move(pty);
        record->screen = std::make_unique<Screen>(spec.rows, spec.cols);
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
        std::thread([this, record] { readSession(record); }).detach();
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
            const auto found = table_.find(name);
            if (found == table_.end()) {
                error = "no such session: " + name;
                return false;
            }
            record = found->second;
        }
        halt(*record);
        return true;
    }

    // Type into a session without becoming a view of it.
    bool type(const std::string& name, const std::string& text) {
        std::shared_ptr<Record> record;
        {
            std::lock_guard<std::mutex> lock(table_mutex_);
            const auto found = table_.find(name);
            if (found != table_.end())
                record = found->second;
        }
        if (!record || record->dead)
            return false;
        return record->pty->write(text);
    }

    // Keep this one. A window's shell is unsaved until a person says otherwise, and
    // this is the saying; giving it a name at the same time is what makes it
    // findable afterwards. Renaming a saved session works the same way, and a name
    // already taken is refused rather than quietly taken over — the other session has
    // a shell in it that somebody is watching.
    bool save(const std::string& name, const std::string& wanted, std::string& error) {
        std::lock_guard<std::mutex> lock(table_mutex_);
        const auto found = table_.find(name);
        if (found == table_.end()) {
            error = "no such session: " + name;
            return false;
        }
        const std::shared_ptr<Record> record = found->second;
        if (wanted.empty() || wanted == name) {
            record->saved = true;
            return true;
        }
        if (!sessionNameOk(wanted)) {
            error = "not a usable session name: " + wanted;
            return false;
        }
        if (wanted == kDaemonSocketName) {
            error = "that name belongs to the daemon itself";
            return false;
        }
        if (table_.find(wanted) != table_.end()) {
            error = "a session is already called " + wanted;
            return false;
        }
        table_.erase(found);
        record->name = wanted;
        record->saved = true;
        table_[wanted] = record;
        return true;
    }

    // Throw a session away, shell and all, because the person closing the window said
    // to. This is the only way an unsaved session dies on purpose; losing its last
    // view without this call means the view did not choose to leave.
    bool discard(const std::string& name, std::string& error) {
        std::shared_ptr<Record> record;
        {
            std::lock_guard<std::mutex> lock(table_mutex_);
            const auto found = table_.find(name);
            if (found == table_.end()) {
                error = "no such session: " + name;
                return false;
            }
            record = found->second;
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
            const auto found = table_.find(name);
            if (found == table_.end()) {
                error = "no such session: " + name;
                return false;
            }
            record = found->second;
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
    struct Record {
        std::string name;
        // Whether a person has asked for this one to be kept. Unsaved sessions are the
        // shells inside open windows: kept alive by the daemon so a crash cannot take
        // them, but destroyed when the window says it is closing.
        std::atomic<bool> saved{true};
        std::unique_ptr<Pty> pty;
        std::unique_ptr<Screen> screen;
        mutable std::mutex screen_mutex;
        std::vector<int> clients;
        // A listing reads the client count while a view is being added or dropped, so
        // the locks have to be taken through a const record.
        mutable std::mutex clients_mutex;
        std::atomic<bool> stopping{false};
        std::atomic<bool> dead{false};
        std::atomic<int> rows{24};
        std::atomic<int> cols{80};
        long long started = 0;
    };

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
        }
        // The reader thread reaps, so by the time anyone asks the child may be gone;
        // a listing that shows the pid of something already reaped would be a pid
        // that could belong to anything by then.
        if (record.pty->alive())
            info.pid = record.pty->child();
        info.saved = record.saved;
        // Asked for on every listing, which is how often anyone looks. Asking the
        // kernel costs a syscall and answers a question no amount of bookkeeping
        // inside the daemon could: whether something is running out there right now.
        info.foreground = foregroundJob(record.pty->master(), record.pty->child());
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
        if (record.pty)
            record.pty->terminate();
    }

    void readSession(std::shared_ptr<Record> record) {
        char buf[16384];
        for (;;) {
            const ssize_t got = record->pty->read(buf, sizeof(buf));
            if (got == 0 || (got < 0 && errno != EINTR))
                break; // the shell closed the terminal, or it is gone
            if (got < 0)
                continue;
            const std::string_view chunk(buf, static_cast<std::size_t>(got));
            std::string answer;
            std::vector<Screen::ClipboardRequest> copies;
            {
                std::lock_guard<std::mutex> lock(record->screen_mutex);
                record->screen->feed(chunk);
                answer = record->screen->takeResponse();
                copies = record->screen->takeClipboardRequests();
            }
            // The daemon is the terminal as far as the program inside is concerned, so
            // it is the daemon that has to answer when one asks what kind of terminal
            // this is, or where the cursor is. Nobody answering is not a silent
            // failure: fish waits ten seconds for a device attributes reply before it
            // gives up and says so on the screen, and tmux will not start properly at
            // all. A view that attaches later must not answer these as well, or the
            // program gets two replies and takes the wrong one.
            if (!answer.empty())
                static_cast<void>(record->pty->write(answer));
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
            broadcast(*record, MsgKind::Data, std::string(chunk));
        }
        static_cast<void>(record->pty->reap());
        record->dead = true;

        // Tell the views before letting go, and wake them so they do not sit waiting
        // for output that will never come.
        const Fields exit_fields = {
            {"code", std::to_string(record->pty->exitCode())},
            {"signal", std::to_string(record->pty->exitSignal())},
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
        record->pty->closeMaster();
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
            }
        }
    }

    void resizeSession(Record& record, int rows, int cols) {
        if (rows < 1 || cols < 1)
            return;
        if (rows == record.rows && cols == record.cols)
            return;
        record.rows = rows;
        record.cols = cols;
        // The pty first: a program inside asks the terminal for its size, so the
        // model should agree with what the program is about to be told, not with what
        // it was a moment ago.
        record.pty->resize(rows, cols);
        std::lock_guard<std::mutex> lock(record.screen_mutex);
        record.screen->resize(rows, cols);
    }

    void attach(int fd, FrameReader& reader, const Fields& hello, const std::string& name) {
        std::shared_ptr<Record> record;
        {
            std::lock_guard<std::mutex> lock(table_mutex_);
            const auto found = table_.find(name);
            if (found == table_.end()) {
                static_cast<void>(detail::sendFrame(
                    fd, MsgKind::Error, encodeFields({{"message", "no such session: " + name}})));
                return;
            }
            record = found->second;
        }
        const int rows = std::stoi(fieldValue(hello, "rows", "0"));
        const int cols = std::stoi(fieldValue(hello, "cols", "0"));
        // The view's size is the session's size. With two views of different sizes
        // the last one to speak wins, which is what tmux does with windows nobody is
        // watching and what makes the screen mean one thing.
        resizeSession(*record, rows, cols);

        std::string snapshot;
        {
            std::lock_guard<std::mutex> lock(record->screen_mutex);
            snapshot = screenSnapshot(*record->screen);
        }
        const std::string welcome = encodeFields(encodeSessionInfo(describe(*record)));
        // Joining the list and being shown the screen happen under one lock, which is
        // what keeps the two in order: a shell that printed between them would
        // otherwise have its output arrive before the screen it is appended to, and a
        // view would be drawn a line from a moment it has not been shown yet.
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
                }
                if (*closing && !record->saved)
                    daemon.destroy(record);
            }
        } guard{*this, record, fd, &closing_on_purpose};

        Frame frame;
        while (detail::nextFrame(fd, reader, frame)) {
            switch (frame.kind) {
                case MsgKind::Data:
                    static_cast<void>(record->pty->write(frame.body));
                    break;
                case MsgKind::Resize: {
                    const Fields fields = decodeFields(frame.body);
                    resizeSession(*record, std::stoi(fieldValue(fields, "rows", "0")),
                                  std::stoi(fieldValue(fields, "cols", "0")));
                    break;
                }
                case MsgKind::Detach:
                case MsgKind::Exit: {
                    // `discard=1` is a window saying it is being closed and this shell
                    // was not saved. Without it a detach means "stop showing it to me",
                    // which is the opposite of an instruction to destroy anything.
                    const Fields leaving = decodeFields(frame.body);
                    closing_on_purpose = fieldValue(leaving, "discard") == "1";
                    return; // the view asks for nothing more
                }
                default:
                    break; // anything else a view sends is its own business
            }
        }
    }

    void serveConnected(int fd) {
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

        if (command == "ping") {
            static_cast<void>(detail::sendFrame(
                fd, MsgKind::Welcome, encodeFields({{"version", "1"}, {"pid", std::to_string(::getpid())}})));
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
            if (!create(spec, error)) {
                static_cast<void>(
                    detail::sendFrame(fd, MsgKind::Error, encodeFields({{"message", error}})));
                return;
            }
            static_cast<void>(detail::sendFrame(fd, MsgKind::Ack, encodeFields({{"name", name}})));
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
            if (!save(name, wanted, error)) {
                static_cast<void>(
                    detail::sendFrame(fd, MsgKind::Error, encodeFields({{"message", error}})));
                return;
            }
            static_cast<void>(detail::sendFrame(
                fd, MsgKind::Ack,
                encodeFields({{"name", wanted.empty() ? name : wanted}})));
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
            if (!type(name, fieldValue(fields, "text"))) {
                static_cast<void>(detail::sendFrame(
                    fd, MsgKind::Error,
                    encodeFields({{"message", "the session would not take the text"}})));
                return;
            }
            static_cast<void>(detail::sendFrame(fd, MsgKind::Ack));
            return;
        }
        if (command == "attach") {
            attach(fd, reader, fields, name);
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
    mutable std::mutex table_mutex_;
    std::map<std::string, std::shared_ptr<Record>> table_;
    std::mutex workers_mutex_;
    std::vector<std::thread> workers_;
    std::mutex all_clients_mutex_;
    std::vector<int> all_clients_;
};

// The other half: what a window or the command line talks to the daemon with.
class DaemonClient {
public:
    ~DaemonClient() { close(); }

    bool connect(std::string& error) {
        const std::string path = daemonSocketPath();
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
    bool request(const Fields& fields) {
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

} // namespace kai
