// Headless check of the session daemon: real pty, real socket, no display.
//
// The daemon's promises are the ones that are hardest to check by reading: that a
// shell outlives the window that started it, that a second window sees what is on
// screen, that a view hanging up does not disturb the program running inside, and
// that stopping leaves nothing behind. All of those need a process on the other end
// of a pty and a connection that can be dropped, so this test has both.

#include "../src/ipc/protocol.hpp"
#include "../src/session/daemon.hpp"
#include "../src/session/cli.hpp"
#include "../src/terminal/screen.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace kai;

namespace {

int checks = 0;
int failures = 0;

void expect(std::string_view what, bool value) {
    ++checks;
    if (value)
        return;
    ++failures;
    std::cerr << "FAIL: " << "expect " << what << "\n";
}

// Enough of a byte string to read, with the control characters named.
std::string show(std::string_view bytes) {
    std::string out;
    for (const char c : bytes) {
        if (out.size() > 200) {
            out += "...";
            break;
        }
        if (c == '\x1b')
            out += "<ESC>";
        else if (c == '\a')
            out += "<BEL>";
        else if (c == '\n')
            out += "<LF>";
        else if (c == '\r')
            out += "<CR>";
        else if (static_cast<unsigned char>(c) < 32)
            out += "^" + std::string(1, static_cast<char>(c + 64));
        else
            out += c;
    }
    return out;
}

std::string root;

void cleanup() {
    ::unlink((root + "/kai-term/daemon.sock").c_str());
    ::rmdir((root + "/kai-term").c_str());
    ::rmdir(root.c_str());
}

// A client that gives up rather than blocking forever. A daemon that never answers
// has to fail a test, not stall one.
void giveUpSoon(int fd) {
    struct timeval tv {0, 200000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

struct TestClient {
    int fd = -1;
    FrameReader reader;

    ~TestClient() {
        if (fd >= 0)
            ::close(fd);
    }

    bool connect() {
        fd = connectTo(daemonSocketPath());
        if (fd < 0)
            return false;
        giveUpSoon(fd);
        return true;
    }

    bool send(MsgKind kind, const std::string& body = {}) {
        return writeAll(fd, encodeFrame(kind, body));
    }

    // A client says what protocol it speaks, so that a daemon can refuse a
    // conversation it would take part in and get wrong. `say_version` is false only for
    // the check about what happens when something does not.
    bool hello(Fields fields, bool say_version = true) {
        if (say_version && fieldValue(fields, "version").empty())
            fields.emplace_back("version", kProtocolVersion);
        return send(MsgKind::Hello, encodeFields(fields));
    }

    // One control reply. Timeouts look like a hangup here, which is the honest
    // reading: nothing is coming.
    bool reply(Frame& out) {
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (reader.pop(out))
                return true;
            if (reader.error())
                return false;
            struct pollfd pfd {fd, POLLIN, 0};
            if (::poll(&pfd, 1, 250) <= 0)
                continue;
            char buf[32768];
            const ssize_t n = readSome(fd, buf, sizeof(buf));
            if (n < 0)
                continue; // a read timeout is another chance, not the end
            if (n == 0)
                return false;
            reader.append(std::string_view(buf, static_cast<std::size_t>(n)));
        }
        return false;
    }

    // Read output frames until `needle` shows up in the text, or give up. Returns the
    // bytes seen, whether or not the thing being looked for ever arrived.
    std::string waitFor(std::string_view needle, int timeout_ms = 6000) {
        std::string text;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            struct pollfd pfd {fd, POLLIN, 0};
            if (::poll(&pfd, 1, 100) > 0) {
                char buf[32768];
                const ssize_t n = readSome(fd, buf, sizeof(buf));
                if (n > 0)
                    reader.append(std::string_view(buf, static_cast<std::size_t>(n)));
            }
            Frame frame;
            while (reader.pop(frame)) {
                if (frame.kind == MsgKind::Data || frame.kind == MsgKind::Snapshot)
                    text += frame.body;
            }
            if (!needle.empty() && text.find(needle) != std::string::npos)
                return text;
        }
        return text;
    }

    // A snapshot is escape bytes; what a person would see is what a screen model says
    // they would see.
    static std::string render(const std::string& bytes, int rows, int cols) {
        Screen screen(rows, cols);
        screen.feed(bytes);
        return screen.text();
    }
};

bool createSession(const std::string& name, int rows = 24, int cols = 80,
                   bool saved = true, std::string* error = nullptr) {
    TestClient client;
    if (!client.connect())
        return false;
    if (!client.hello({{"command", "new"},
                       {"name", name},
                       {"rows", std::to_string(rows)},
                       {"cols", std::to_string(cols)},
                       {"saved", saved ? "1" : "0"}}))
        return false;
    Frame reply;
    if (!client.reply(reply)) {
        if (error)
            *error = "the daemon said nothing";
        return false;
    }
    if (reply.kind != MsgKind::Ack) {
        if (error)
            *error = fieldValue(decodeFields(reply.body), "message", "refused");
        return false;
    }
    return true;
}

std::vector<SessionInfo> listed() {
    TestClient client;
    std::vector<SessionInfo> none;
    if (!client.connect() || !client.hello({{"command", "list"}}))
        return none;
    Frame reply;
    if (!client.reply(reply) || reply.kind != MsgKind::SessionList)
        return none;
    for (const Fields& fields : decodeRecords(reply.body))
        none.push_back(decodeSessionInfo(fields));
    return none;
}

// The size a session carries, once the daemon has had a moment to settle on one. Sizes
// move on a message rather than in this thread, so a check waits rather than guessing.
bool settles(const std::string& name, int rows, int cols) {
    for (int attempt = 0; attempt < 60; ++attempt) {
        for (const SessionInfo& info : listed()) {
            if (info.name == name && info.rows == rows && info.cols == cols)
                return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

// What a session's arrangement is, and a new one for it to carry. Both are one-shot control
// commands, the same shape as `list`: say what you want in the hello, read the answer, hang
// up. Nothing draws the arrangement yet, which is the point of asking for it first: every
// later step is a client of the record the daemon holds.
bool askLayout(const std::string& name, std::string& text, std::string* error = nullptr) {
    TestClient client;
    if (!client.connect() || !client.hello({{"command", "layout"}, {"name", name}})) {
        if (error)
            *error = "cannot ask the daemon";
        return false;
    }
    Frame reply;
    if (!client.reply(reply)) {
        if (error)
            *error = "the daemon said nothing";
        return false;
    }
    if (reply.kind != MsgKind::Layout) {
        if (error)
            *error = fieldValue(decodeFields(reply.body), "message", "refused");
        return false;
    }
    text = fieldValue(decodeFields(reply.body), "text", "");
    return true;
}

std::string giveLayout(const std::string& name, const std::string& text) {
    TestClient client;
    if (!client.connect() ||
        !client.hello({{"command", "set-layout"}, {"name", name}, {"text", text}}))
        return "cannot reach the daemon";
    Frame reply;
    if (!client.reply(reply))
        return "the daemon said nothing";
    if (reply.kind == MsgKind::Ack)
        return "";
    return fieldValue(decodeFields(reply.body), "message", "refused");
}

// How many shells a session is running, and which of them a view is looking at. Both are
// listed because a person standing outside the session has no other way to ask, and because
// "the record says three and the machine runs one" is exactly the kind of lie a listing can
// tell.
int listedPanes(const std::string& name) {
    for (const SessionInfo& info : listed()) {
        if (info.name == name)
            return info.panes;
    }
    return -1;
}

int listedFocused(const std::string& name) {
    for (const SessionInfo& info : listed()) {
        if (info.name == name)
            return info.focused;
    }
    return -1;
}

// Type into one named pane rather than whichever the session happens to be looking at.
bool sendPane(const std::string& name, int pane, const std::string& text) {
    TestClient client;
    if (!client.connect() || !client.hello({{"command", "send"},
                                            {"name", name},
                                            {"text", text},
                                            {"pane", std::to_string(pane)}}))
        return false;
    Frame reply;
    return client.reply(reply) && reply.kind == MsgKind::Ack;
}

// Attach and leave the client connected; the caller decides when it goes away.
bool attach(TestClient& client, const std::string& name, int rows, int cols,
            std::string& snapshot, std::string* error = nullptr) {
    if (!client.connect()) {
        if (error)
            *error = "cannot reach the daemon";
        return false;
    }
    if (!client.hello({{"command", "attach"},
                       {"name", name},
                       {"rows", std::to_string(rows)},
                       {"cols", std::to_string(cols)}})) {
        if (error)
            *error = "cannot say hello";
        return false;
    }
    Frame welcome;
    if (!client.reply(welcome)) {
        if (error)
            *error = "no welcome";
        return false;
    }
    if (welcome.kind != MsgKind::Welcome) {
        if (error)
            *error = fieldValue(decodeFields(welcome.body), "message", "refused");
        return false;
    }
    Frame frame;
    if (!client.reply(frame) || frame.kind != MsgKind::Snapshot) {
        if (error)
            *error = "no snapshot";
        return false;
    }
    snapshot = frame.body;
    return true;
}

} // namespace

// Whether the listing has a session under this exact name.
bool listed_have(const std::string& name) {
    for (const SessionInfo& info : listed())
        if (info.name == name)
            return true;
    return false;
}

int main() {
    root = "/tmp/kai-session-test-" + std::to_string(static_cast<long>(::getpid()));
    if (::mkdir(root.c_str(), 0700) != 0 && errno != EEXIST) {
        std::cerr << "cannot create the test directory\n";
        return 1;
    }
    ::setenv("XDG_RUNTIME_DIR", root.c_str(), 1);
    // A known shell, so what comes back is predictable and the test does not depend
    // on which shell whoever is running this happens to prefer.
    ::setenv("KAI_SHELL", "/bin/sh", 1);

    Daemon daemon;
    {
        std::string error;
        expect("the daemon starts (" + error + ")", daemon.start(error));
        expect("and its socket is where it said it would be",
               daemon.socketPath() ==
               root + "/kai-term/daemon-v" + std::string(kProtocolVersion) + ".sock");
        // The name carries the protocol number, which is the whole of how a daemon from
        // an older build stops being mistaken for ours: it is not sitting on the socket
        // this build looks at, so a new build starts its own instead of sending it a
        // `save` it has never heard of.
        expect("and the socket is named for the protocol it speaks",
               daemon.socketPath().find("v" + std::string(kProtocolVersion)) !=
                   std::string::npos);
    }
    std::thread serving([&daemon] { daemon.run(); });

    {
        TestClient client;
        expect("a client can connect", client.connect());
        expect("and ask whether anyone is home", client.hello({{"command", "ping"}}));
        Frame reply;
        expect("someone answers", client.reply(reply));
        expect("with a welcome", reply.kind == MsgKind::Welcome);
        expect("that says which protocol",
               fieldValue(decodeFields(reply.body), "version") == kProtocolVersion);
    }

    {
        TestClient client;
        expect("a hello nobody understands is refused",
               client.connect() && writeAll(client.fd, encodeFrame(MsgKind::Data, "x")));
        Frame reply;
        expect("politely", client.reply(reply) && reply.kind == MsgKind::Error);
    }

    {
        // A daemon from another build, still running beside this one.
        //
        // The socket is named for the protocol, so it is not the socket this build
        // talks to — that is what keeps a `save` from being sent to a daemon that has
        // never heard of it. What is left is to say that it is there: its shells are
        // still running, and nothing this build can list is going to show them.
        expect("nothing else is running", foreignDaemonNotice().empty());

        std::string bind_error;
        const int older = bindListen(sessionSocketPath("daemon-v1"), 4, bind_error);
        expect("an older daemon's socket appears beside ours", older >= 0);
        std::thread older_daemon([&older] {
            const int fd = ::accept(older, nullptr, nullptr);
            if (fd < 0)
                return;
            FrameReader reader;
            Frame hello;
            for (int attempt = 0; attempt < 20 && !reader.pop(hello); ++attempt) {
                struct pollfd pfd {fd, POLLIN, 0};
                if (::poll(&pfd, 1, 250) <= 0)
                    continue;
                char buf[4096];
                const ssize_t n = readSome(fd, buf, sizeof(buf));
                if (n > 0)
                    reader.append(std::string_view(buf, static_cast<std::size_t>(n)));
            }
            static_cast<void>(writeAll(fd, encodeFrame(MsgKind::Welcome, encodeFields({
                                                                    {"version", "1"},
                                                                    {"pid", "4242"}}))));
            ::close(fd);
        });

        const std::string notice = foreignDaemonNotice();
        expect("this build notices the other daemon",
               notice.find("another build") != std::string::npos);
        expect("says which protocol it speaks", notice.find("protocol 1") != std::string::npos);
        expect("and which process is holding the shells",
               notice.find("4242") != std::string::npos);
        expect("without mistaking it for this build's own",
               notice.find("daemon-v" + std::string(kProtocolVersion)) == std::string::npos);
        older_daemon.join();
        ::close(older);
    }

    {
        // A client built against another protocol is refused by name, and told what to
        // do about it. Letting it through is the worse option: an old client would
        // accept answers it reads differently and report success about things that did
        // not happen, which is a much harder thing to debug than a refusal.
        for (const char* said : {"1", ""}) {
            TestClient client;
            bool ok = client.connect();
            if (ok) {
                Fields fields{{"command", "list"}};
                if (*said != '\0')
                    fields.emplace_back("version", said);
                ok = client.send(MsgKind::Hello, encodeFields(fields));
            }
            Frame reply;
            const bool refused = ok && client.reply(reply) && reply.kind == MsgKind::Error;
            expect(refused ? "a client from another protocol is refused"
                           : "and so is one that says nothing",
                   refused);
            if (refused) {
                const std::string message = fieldValue(decodeFields(reply.body), "message");
                expect("and told it was the protocol",
                       message.find("protocol mismatch") != std::string::npos);
                expect("and what to do about it",
                       message.find("kai-term shutdown") != std::string::npos);
            }
        }
    }

    {
        std::string error;
        expect("a session can be started", createSession("work", 24, 80, true, &error));
        expect("and it is number 0, the first one this daemon handed out",
               listed().size() == 1 && listed()[0].id == 0);
        const auto sessions = listed();
        expect("and appears in the list", sessions.size() == 1);
        if (!sessions.empty()) {
            expect("under the name it was given", sessions[0].name == "work");
            expect("alive", sessions[0].alive);
            expect("with a process behind it", sessions[0].pid > 0);
            expect("at the size it was asked for",
                   sessions[0].rows == 24 && sessions[0].cols == 80);
            expect("with nobody looking at it yet", sessions[0].clients == 0);
            expect("started recently enough to be this run",
                   sessions[0].started > 1700000000);
        }
    }

    {
        std::string error;
        expect("a name the daemon already uses for itself is refused",
               !createSession("daemon", 24, 80, true, &error));
        expect("and says why", error.find("daemon") != std::string::npos);
        error.clear();
        expect("so is a name that is not a name", !createSession("a/b", 24, 80, true, &error));
        expect("and a size that is not a size", !createSession("tiny", 0, 80, true, &error));
        expect("with something to say about it", !error.empty());
    }

    {
        // The point of the whole exercise: bytes typed into a view reach the shell,
        // and what the shell prints comes back out of the daemon.
        TestClient viewer;
        std::string snapshot;
        std::string error;
        expect("a view can attach", attach(viewer, "work", 24, 80, snapshot, &error));
        expect("and is shown the screen (" + error + ")", !snapshot.empty());
        expect("typed input is accepted", viewer.send(MsgKind::Data, "echo kai-marker-9182\n"));
        const std::string text = viewer.waitFor("kai-marker-9182");
        expect("and echoed back to the view", text.find("kai-marker-9182") != std::string::npos);
        expect("the view is counted as watching",
               listed().size() == 1 && listed()[0].clients == 1);

        // A second window joining later has to see what is already on screen, not an
        // empty one that fills in when the next thing prints.
        TestClient late;
        std::string late_snapshot;
        expect("a second view can attach", attach(late, "work", 24, 80, late_snapshot));
        const std::string shown = TestClient::render(late_snapshot, 24, 80);
        expect("and is shown what the first one typed",
               shown.find("kai-marker-9182") != std::string::npos);

        // Both are looking at one session, so both see what it prints next.
        expect("input from one view goes to the shell",
               late.send(MsgKind::Data, "echo shared-output\n"));
        expect("and the other view sees it",
               viewer.waitFor("shared-output").find("shared-output") != std::string::npos);

        // Hanging up on one side must not disturb the program on the other.
        const int dropped = late.fd;
        late.fd = -1;
        ::close(dropped);
        expect("a view can go away", viewer.send(MsgKind::Data, "echo after-a-hangup\n"));
        expect("without disturbing the shell",
               viewer.waitFor("after-a-hangup").find("after-a-hangup") != std::string::npos);
        expect("and the session is still there", listed().size() == 1);

        // The view's size is the session's size, for the shell as well as for us.
        expect("a view can change size",
               viewer.send(MsgKind::Resize, encodeFields({{"rows", "10"}, {"cols", "40"}})));
        bool resized = false;
        for (int attempt = 0; attempt < 40 && !resized; ++attempt) {
            const auto sessions = listed();
            resized = !sessions.empty() && sessions[0].rows == 10 && sessions[0].cols == 40;
            if (!resized)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        expect("and the session follows", resized);

        TestClient third;
        std::string third_snapshot;
        expect("so a new view is shown the screen at its own size",
               attach(third, "work", 10, 40, third_snapshot));
        expect("which is the size the session settled on",
               TestClient::render(third_snapshot, 10, 40).find("after-a-hangup") != std::string::npos);

        // Detaching a view from the outside leaves the shell alone.
        TestClient boss;
        expect("a view can be sent away from outside",
               boss.connect() &&
                   boss.hello({{"command", "detach"}, {"name", "work"}}));
        Frame ack;
        expect("with an acknowledgement", boss.reply(ack) && ack.kind == MsgKind::Ack);
        bool nobody_watching = false;
        int seen_clients = -1;
        for (int attempt = 0; attempt < 40 && !nobody_watching; ++attempt) {
            const auto sessions = listed();
            seen_clients = sessions.empty() ? -1 : sessions[0].clients;
            nobody_watching = !sessions.empty() && sessions[0].clients == 0;
            if (!nobody_watching)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        expect("and every view looking at it is sent away (still " +
                   std::to_string(seen_clients) + " watching)",
               nobody_watching);
        expect("the session itself is still alive",
               !listed().empty() && listed()[0].alive);

        // What a shell does while nobody is watching is the reason the daemon exists:
        // a view that comes back finds the same shell, not a new one.
        TestClient returned;
        std::string returned_snapshot;
        std::string returned_error;
        expect("and a view coming back finds it there",
               attach(returned, "work", 24, 80, returned_snapshot, &returned_error));
        expect("still listening (" + returned_error + ")",
               returned.send(MsgKind::Data, "echo after-detach\n"));
        expect("still answering",
               returned.waitFor("after-detach").find("after-detach") != std::string::npos);
    }

    {
        // Two views of one session. The size the session carries is the size every view
        // looking at it can show, not the size of whoever spoke last: with the last-one-to-
        // speak rule a small window and a big one on the same session reflowed it every time
        // the other was touched, so neither window got to keep a screen and the shell
        // reflowed twice for one gesture. (Kaida, 2026-11-21: the smallest view wins.)
        expect("a session starts with nobody looking at it", createSession("shared"));
        TestClient small_view;
        TestClient big_view;
        std::string snapshot;
        expect("a small view attaches", attach(small_view, "shared", 24, 80, snapshot));
        expect("and a bigger one joins it", attach(big_view, "shared", 50, 200, snapshot));
        expect("the session keeps the smaller of the two, not the last one to arrive",
               settles("shared", 24, 80));

        expect("the small view growing lets the session grow to the next smallest",
               small_view.send(MsgKind::Resize, encodeFields({{"rows", "30"},
                                                              {"cols", "100"}})) &&
                   settles("shared", 30, 100));

        // The reason this is not simply "the size of the first window": a limit stops being
        // a limit when the thing imposing it goes away, and a session that stayed small
        // after its small window closed would keep every window that came after it small.
        ::close(small_view.fd);
        small_view.fd = -1;
        expect("and when the limiting view goes away the session grows to the one left",
               settles("shared", 50, 200));

        // Nobody looking is not a reason to resize: the shell's screen is what it is, and a
        // session that picked a default the moment the last window closed would throw away
        // the layout it was keeping for the next one.
        ::close(big_view.fd);
        big_view.fd = -1;
        bool quiet = false;
        for (int attempt = 0; attempt < 20 && !quiet; ++attempt) {
            quiet = settles("shared", 50, 200);
            if (!quiet)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        expect("with nobody looking the session keeps the size it had", quiet);

        // Leave the room as it was found: the blocks after this one count the sessions the
        // daemon is holding and take the first of them as theirs, so one left over here
        // gets counted as theirs and they fail for a reason that is not in them.
        TestClient tidying;
        if (tidying.connect()) {
            static_cast<void>(tidying.hello({{"command", "kill"}, {"name", "shared"}}));
            Frame ack;
            static_cast<void>(tidying.reply(ack));
        }
        for (int attempt = 0; attempt < 60; ++attempt) {
            bool still_there = false;
            for (const SessionInfo& info : listed())
                still_there = still_there || info.name == "shared";
            if (!still_there)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    {
        // The arrangement a session carries, which is step 1 of replacing tmux: the session
        // holds where the work is, not the window that happens to be showing it. Nothing
        // draws this yet — everything after is a client of it, which is why it comes first.
        std::string text;
        expect("a session has an arrangement from the moment it exists",
               createSession("arranged") && askLayout("arranged", text));
        expect("a session nobody has split is one pane, shown side by side",
               text == "view side-by-side\npane\n");

        std::string message =
            giveLayout("arranged", "split left-right 0.5\n  pane\n  pane\n");
        expect("an arrangement can be handed to a session (" + message + ")", message.empty());
        expect("and reads back as the same arrangement",
               askLayout("arranged", text) &&
                   text == "view side-by-side\nsplit left-right 0.500000\n  pane\n  pane\n");

        // Panes and the view they are shown in travel with the session, so a second window
        // — or a tty, or the window opened after this one closed — arrives at the same
        // arrangement rather than at whatever the terminal happens to be.
        message = giveLayout("arranged",
                             "view tabs\nsplit left-right 0.5\n  pane\n  split up-down 0.25\n"
                             "    pane\n    pane\n");
        expect("a session can carry several panes and the view they are shown in (" +
                   message + ")",
               message.empty());
        expect("and all three panes come back, in tab view",
               askLayout("arranged", text) && text.find("view tabs") == 0 &&
                   text.find("split up-down 0.250000") != std::string::npos);

        // The words are the record's only shape, so a typo has to be a refusal at the door
        // rather than a session quietly rearranged into something nobody asked for.
        message = giveLayout("arranged", "split sideways\n  pane\n  pane\n");
        expect("a split that is not a direction is refused (" + message + ")",
               !message.empty() && message.find("left-right") != std::string::npos);
        expect("and the arrangement that was there is still there",
               askLayout("arranged", text) && text.find("view tabs") == 0 &&
                   text.find("split up-down") != std::string::npos);
        message = giveLayout("arranged", "split left-right 1.5\n  pane\n  pane\n");
        expect("so is a share that is not a share (" + message + ")",
               !message.empty() && message.find("between 0 and 1") != std::string::npos);
        message = giveLayout("arranged", "");
        expect("and so is nothing at all (" + message + ")", !message.empty());

        expect("a session that is not there is said so", !askLayout("nowhere", text));

        TestClient tidying;
        if (tidying.connect()) {
            static_cast<void>(tidying.hello({{"command", "kill"}, {"name", "arranged"}}));
            Frame ack;
            static_cast<void>(tidying.reply(ack));
        }
    }

    {
        // A split means a second program. The record has said how many panes a session has
        // since step 1; this is the daemon honouring it, which is the difference between a
        // split and a mirror.
        std::string message;
        expect("a session to split", createSession("split-me"));
        expect("one pane is one shell", listedPanes("split-me") == 1 &&
                                              listedFocused("split-me") == 0);

        message = giveLayout("split-me", "split left-right 0.5\n  pane\n  pane\n");
        expect("an arrangement with two panes is accepted (" + message + ")", message.empty());
        expect("and the session is running two shells", listedPanes("split-me") == 2);
        expect("with one of them being looked at", listedFocused("split-me") == 0);

        // Each pane's program is its own: what one prints is not in the other, and what one
        // is told about the window is its own cell rather than the whole of it.
        expect("typing into the left pane is taken by the left pane",
               sendPane("split-me", 0, "echo ZERO-ONLY\n"));
        expect("and the right pane has its own", sendPane("split-me", 1, "echo ONE-ONLY\n"));
        TestClient watcher;
        std::string shown;
        expect("a view attaches to see the pane being looked at",
               attach(watcher, "split-me", 24, 80, shown));
        std::string text = TestClient::render(shown, 24, 80);
        // The pane the session is focused on, which an arrangement read from words puts at
        // the start of the tab bar: the left one. Not whichever shell was started last —
        // "what you are looking at" and "what appeared last" are different questions, and a
        // view that answered the second would move under a person whenever a pane started
        // something in the background.
        expect("and sees that pane's shell", text.find("ZERO-ONLY") != std::string::npos);
        expect("without the other pane's output mixed into it",
               text.find("ONE-ONLY") == std::string::npos);
        // 80 columns split left-right at half is 39, a divider, and 40. A shell told the
        // window's width would wrap where the window does not, which makes the split a
        // picture of a division rather than a division. Asked over the wire, so the answer
        // arrives on a frame of its own whenever the shell gets round to it.
        // What a shell was told about its size is what it answers, so ask it — but ask
        // once it is actually reading. A command typed while a shell is still starting is
        // typed at nothing, and a check that blames the terminal for that is blaming the
        // wrong thing: say something, wait to see it come back, then ask the question.
        std::string raw = shown;
        auto asked_and_saw = [&](const char* command, const char* answer) {
            static_cast<void>(sendPane("split-me", 0, command));
            // Reading is its own step: a daemon that has written something is not the same
            // as a view that has seen it, and a check that only looks in its own queue
            // would wait forever on bytes still in the socket.
            raw += watcher.waitFor(answer, 4000);
            text = TestClient::render(raw, 24, 80);
            return text.find(answer) != std::string::npos;
        };
        expect("and the shell is reading what is typed to it",
               asked_and_saw("echo READY-PANE-0\n", "READY-PANE-0"));
        // 80 columns split left-right at half is 39, a divider, and 40. A shell told the
        // window's width would wrap where the window does not, which makes a split a
        // picture of a division rather than a division.
        expect("and the shell was told its own width [" + text + "]",
               asked_and_saw("stty size\n", "24 39") && text.find("80") == std::string::npos);

        // Going back to one pane keeps the shell that still has a place in the tab bar,
        // because a shell belongs to a place rather than to a node of the tree.
        message = giveLayout("split-me", "pane\n");
        expect("an arrangement with one pane is accepted (" + message + ")", message.empty());
        expect("and the session is running one shell again", listedPanes("split-me") == 1);
        // The screen a view is handed after the change is a snapshot, not the bytes it has
        // been sent so far, so the surviving pane is read from what arrives now.
        text = TestClient::render(watcher.waitFor("ZERO-ONLY", 2000), 24, 80);
        expect("and the shell that still has a place is the one still there",
               text.find("ZERO-ONLY") != std::string::npos);
        expect("with the other one gone", text.find("ONE-ONLY") == std::string::npos);

        TestClient tidying;
        if (tidying.connect()) {
            static_cast<void>(tidying.hello({{"command", "kill"}, {"name", "split-me"}}));
            Frame ack;
            static_cast<void>(tidying.reply(ack));
        }
    }

    {
        // A tab IS a pane, so switching tabs is one question to the daemon — "show me that
        // one" — rather than the window holding its own notion of what is running. A view
        // still looks at one pane at a time; what changes is which.
        expect("a session to look around in", createSession("look-here"));
        static_cast<void>(
            giveLayout("look-here", "split left-right 0.5\n  pane\n  pane\n"));
        static_cast<void>(sendPane("look-here", 0, "echo LEFT-TAB\n"));
        static_cast<void>(sendPane("look-here", 1, "echo RIGHT-TAB\n"));

        TestClient viewer;
        std::string shown;
        expect("a view arrives looking at the pane the session is looking at",
               attach(viewer, "look-here", 24, 80, shown) &&
                   TestClient::render(shown, 24, 80).find("LEFT-TAB") != std::string::npos);

        // A window opening onto a session that already has tabs draws them on its first
        // paint, which means the tab bar travels with the welcome rather than only with a
        // change nobody has made yet.
        {
            TestClient joiner;
            std::string joined;
            expect("a view arrives at a session with two places",
                   attach(joiner, "look-here", 24, 80, joined));
            bool told = false;
            Frame frame;
            for (int attempt = 0; attempt < 80 && !told; ++attempt) {
                // Read before popping. The tab bar travels just after the screen, so on a
                // quiet machine it is already in the buffer when the attach returns and on a
                // busy one it is still on its way; a check that only looks at what has
                // already arrived passes or fails depending on how loaded the machine is,
                // which is not a fact about the daemon.
                struct pollfd pfd {joiner.fd, POLLIN, 0};
                if (::poll(&pfd, 1, 25) > 0) {
                    char buf[8192];
                    const ssize_t n = readSome(joiner.fd, buf, sizeof(buf));
                    if (n > 0)
                        joiner.reader.append(
                            std::string_view(buf, static_cast<std::size_t>(n)));
                }
                while (joiner.reader.pop(frame)) {
                    // Set, never assigned: the tab bar is followed immediately by whatever
                    // the shell is printing, and a check that lets the next frame answer the
                    // question the tab bar already answered fails depending on how fast the
                    // machine is.
                    if (frame.kind == MsgKind::Tabs &&
                        fieldValue(decodeFields(frame.body), "panes", "0") == "2")
                        told = true;
                }
            }
            expect("and is handed the tab bar as it arrives", told);
        }


        expect("asking to be shown the other pane is taken",
               viewer.send(MsgKind::ShowPane, encodeFields({{"pane", "1"}})));
        expect("and the view is given that pane's screen",
               TestClient::render(viewer.waitFor("RIGHT-TAB", 4000), 24, 80)
                   .find("RIGHT-TAB") != std::string::npos);

        // A second view, still on the first pane, is what shows that the move was a move
        // and not a copy: one program's output reaching two screens would be the mixing
        // that a split exists to stop.
        TestClient other;
        std::string other_shown;
        expect("another view can be looking at the first pane",
               attach(other, "look-here", 24, 80, other_shown) &&
                   TestClient::render(other_shown, 24, 80).find("LEFT-TAB") != std::string::npos);
        static_cast<void>(viewer.send(MsgKind::Data, "echo TYPED-ON-THE-RIGHT\n"));
        expect("what a view types goes to the pane it is looking at",
               TestClient::render(viewer.waitFor("TYPED-ON-THE-RIGHT", 4000), 24, 80)
                   .find("TYPED-ON-THE-RIGHT") != std::string::npos);
        expect("and not to the one it came away from",
               TestClient::render(other.waitFor("TYPED-ON-THE-RIGHT", 1000), 24, 80)
                   .find("TYPED-ON-THE-RIGHT") == std::string::npos);

        // A pane number past the end of the tab bar is refused rather than obeyed. A view
        // that asked for pane 9 and was given a blank screen would be reported as a broken
        // terminal, when the truth is a wrong number.
        // Whatever the switch left unread comes off first: the thing being looked for is
        // the answer to this question, not the last piece of news about the previous one.
        static_cast<void>(viewer.waitFor("", 150));
        expect("a pane that is not in the tab bar is asked for anyway",
               viewer.send(MsgKind::ShowPane, encodeFields({{"pane", "9"}})));
        Frame answer;
        bool answered = false;
        for (int attempt = 0; attempt < 40 && !answered; ++attempt) {
            struct pollfd pfd {viewer.fd, POLLIN, 0};
            if (::poll(&pfd, 1, 100) > 0) {
                char buf[8192];
                const ssize_t n = readSome(viewer.fd, buf, sizeof(buf));
                if (n > 0)
                    viewer.reader.append(std::string_view(buf, static_cast<std::size_t>(n)));
            }
            Frame frame;
            while (viewer.reader.pop(frame)) {
                // Output keeps arriving while a person works; only an answer counts.
                if (frame.kind == MsgKind::Error || frame.kind == MsgKind::Exit) {
                    answer = frame;
                    answered = true;
                }
            }
        }
        expect("and the answer says so", answer.kind == MsgKind::Error);
        expect("in words that name the pane ([" + answer.body + "])",
               fieldValue(decodeFields(answer.body), "message").find("pane") !=
                   std::string::npos);

        // And when the pane a view was looking at stops existing — the arrangement shrank
        // and its program went with it — the view is left looking at a live program rather
        // than at the last picture of a dead one.
        static_cast<void>(giveLayout("look-here", "pane\n"));
        static_cast<void>(viewer.send(MsgKind::Data, "echo STILL-READING\n"));
        expect("a view whose pane went away is moved to one that is still running",
               TestClient::render(viewer.waitFor("STILL-READING", 4000), 24, 80)
                   .find("STILL-READING") != std::string::npos);

        // The tab bar a window draws is the session's, not the window's: a pane added from
        // a second window, or from `set-layout` in a terminal, has to show up in the first
        // one. So the daemon says so, to everyone looking, whenever the tab bar changes.
        {
            TestClient watcher;
            std::string watcher_shown;
            expect("a view is watching the tab bar",
                   attach(watcher, "look-here", 24, 80, watcher_shown));
            static_cast<void>(
                giveLayout("look-here", "split left-right 0.5\n  pane\n  pane\n"));
            Frame news;
            bool heard = false;
            for (int attempt = 0; attempt < 40 && !heard; ++attempt) {
                struct pollfd pfd {watcher.fd, POLLIN, 0};
                if (::poll(&pfd, 1, 100) > 0) {
                    char buf[8192];
                    const ssize_t n = readSome(watcher.fd, buf, sizeof(buf));
                    if (n > 0)
                        watcher.reader.append(std::string_view(buf, static_cast<std::size_t>(n)));
                }
                Frame frame;
                while (watcher.reader.pop(frame)) {
                    if (frame.kind == MsgKind::Tabs) {
                        news = frame;
                        heard = true;
                    }
                }
            }
            expect("and is told when a pane appears ([" + news.body + "])", heard);
            const Fields bar = decodeFields(news.body);
            expect("the tab bar now has two places", fieldValue(bar, "panes", "0") == "2");
            expect("with the first one looked at", fieldValue(bar, "focused", "-1") == "0");
        }

    {
        // The two keys a person reaches for: another place to work, and a different way of
        // looking at the places there are. Both are the session's business, since the
        // session is what owns the panes — a window that kept its own notion of how many
        // tabs there were would disagree with the next window the first time one of them
        // added one.
        auto tabs_say = [](TestClient& viewer, int panes, const std::string& mode) {
            for (int attempt = 0; attempt < 80; ++attempt) {
                struct pollfd pfd {viewer.fd, POLLIN, 0};
                if (::poll(&pfd, 1, 100) > 0) {
                    char buf[8192];
                    const ssize_t n = readSome(viewer.fd, buf, sizeof(buf));
                    if (n > 0)
                        viewer.reader.append(std::string_view(buf, static_cast<std::size_t>(n)));
                }
                Frame frame;
                while (viewer.reader.pop(frame)) {
                    if (frame.kind != MsgKind::Tabs)
                        continue;
                    const Fields fields = decodeFields(frame.body);
                    if (fieldValue(fields, "panes", "0") == std::to_string(panes) &&
                        (mode.empty() || fieldValue(fields, "mode") == mode))
                        return true;
                }
            }
            return false;
        };
        // The next Tabs frame about a bar of `panes` places, whole, so that more than one
        // thing about it can be looked at without racing the announcement twice.
        auto await_tabs = [](TestClient& viewer, int panes) {
            for (int attempt = 0; attempt < 80; ++attempt) {
                struct pollfd pfd {viewer.fd, POLLIN, 0};
                if (::poll(&pfd, 1, 100) > 0) {
                    char buf[8192];
                    const ssize_t n = readSome(viewer.fd, buf, sizeof(buf));
                    if (n > 0)
                        viewer.reader.append(std::string_view(buf, static_cast<std::size_t>(n)));
                }
                Frame frame;
                while (viewer.reader.pop(frame)) {
                    if (frame.kind == MsgKind::Tabs &&
                        fieldValue(decodeFields(frame.body), "panes", "0") ==
                            std::to_string(panes))
                        return decodeFields(frame.body);
                }
            }
            return Fields{};
        };

        auto refused_with = [](TestClient& viewer, const std::string& about) {
            for (int attempt = 0; attempt < 80; ++attempt) {
                struct pollfd pfd {viewer.fd, POLLIN, 0};
                if (::poll(&pfd, 1, 100) > 0) {
                    char buf[8192];
                    const ssize_t n = readSome(viewer.fd, buf, sizeof(buf));
                    if (n > 0)
                        viewer.reader.append(std::string_view(buf, static_cast<std::size_t>(n)));
                }
                Frame frame;
                while (viewer.reader.pop(frame)) {
                    if (frame.kind == MsgKind::Error &&
                        fieldValue(decodeFields(frame.body), "message").find(about) !=
                            std::string::npos)
                        return true;
                }
            }
            return false;
        };

        expect("a session to add tabs to", createSession("tab-keys"));
        TestClient window;
        std::string shown;
        expect("with a window looking at it", attach(window, "tab-keys", 24, 80, shown));
        expect("and the tab bar as it stands", tabs_say(window, 1, "side-by-side"));

        static_cast<void>(window.send(MsgKind::Data, "echo FIRST-PLACE-MARKER\n"));
        expect("the window is looking at the first place",
               TestClient::render(window.waitFor("FIRST-PLACE-MARKER", 4000), 24, 80)
                   .find("FIRST-PLACE-MARKER") != std::string::npos);

        expect("a new tab is asked for", window.send(MsgKind::NewPane));
        expect("and the session is running another program",
               listedPanes("tab-keys") == 2);
        const Fields grown = await_tabs(window, 2);
        expect("the window is told the tab bar grew", fieldValue(grown, "panes") == "2");
        // Where each place sits, in cells: 80 columns split at half is 39, a divider, and
        // 40. A window drawing the places side by side has to know that without going and
        // asking the session's layout for it, because the layout is the daemon's and the
        // drawing is now.
        expect("and where each place sits on the screen ([" + fieldValue(grown, "cells") +
                   "])",
               fieldValue(grown, "cells") == "0,0,24,39;0,40,24,40");
        // A key that says "new" should land you in the new thing. Staying where you were
        // would mean a second keystroke every single time, on the one key whose whole job
        // is getting you out of the way you were in.
        static_cast<void>(window.send(MsgKind::Data, "echo IN-NEW-PLACE\n"));
        expect("and the window is looking at the new place",
               TestClient::render(window.waitFor("IN-NEW-PLACE", 4000), 24, 80)
                   .find("IN-NEW-PLACE") != std::string::npos);

        // The view mode is a fact about the session, said back with the tab bar, so that a
        // second window opens already knowing whether it is looking at tabs or splits.
        // Asked in this order because the session starts side by side: a change that is not
        // a change is not announced, and a check that waits on it is waiting for a message
        // nobody promised.
        expect("the window asks for one place at a time",
               window.send(MsgKind::SetViewMode, encodeFields({{"mode", "tabs"}})));
        expect("and the tab bar carries that mode", tabs_say(window, 2, "tabs"));
        expect("the places side by side is asked for",
               window.send(MsgKind::SetViewMode, encodeFields({{"mode", "side-by-side"}})));
        expect("and comes back on the tab bar", tabs_say(window, 2, "side-by-side"));
        expect("a mode that is neither is refused",
               window.send(MsgKind::SetViewMode, encodeFields({{"mode", "diagonal"}})));
        expect("naming the two there are (so the words are learnable)",
               refused_with(window, "side-by-side or tabs"));

        expect("the place being looked at is closed", window.send(MsgKind::ClosePane));
        expect("and the session runs one program again", listedPanes("tab-keys") == 1);
        expect("closing the last place is refused rather than emptying the session",
               window.send(MsgKind::ClosePane));
        expect("with a word about what to do instead (end the session)",
               refused_with(window, "last"));

        // A program that exits stops being a place in the tab bar, rather than staying on
        // the books as a place with nothing in it — which the next resize would fill with a
        // fresh program nobody asked for.
        static_cast<void>(window.send(MsgKind::NewPane));
        expect("a place is made to watch one leave", listedPanes("tab-keys") == 2);
        static_cast<void>(window.send(MsgKind::Data, "exit\n"));
        bool one_again = false;
        for (int attempt = 0; attempt < 120 && !one_again; ++attempt) {
            one_again = listedPanes("tab-keys") == 1;
            if (!one_again)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        expect("the place whose program exited is gone from the tab bar", one_again);
        // A resize is the daemon laying the panes out again, which is exactly where a place
        // still listed but empty would come back to life.
        static_cast<void>(
            window.send(MsgKind::Resize, encodeFields({{"rows", "30"}, {"cols", "90"}})));
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        expect("and a resize does not bring it back", listedPanes("tab-keys") == 1);
        expect("the window is still looking at a program that answers",
               [&]() {
                   static_cast<void>(window.send(MsgKind::Data, "echo STILL-HERE\n"));
                   return TestClient::render(window.waitFor("STILL-HERE", 4000), 24, 80)
                       .find("STILL-HERE") != std::string::npos;
               }());

        TestClient tidying;
        if (tidying.connect()) {
            static_cast<void>(tidying.hello({{"command", "kill"}, {"name", "tab-keys"}}));
            Frame ack;
            static_cast<void>(tidying.reply(ack));
        }
    }

        TestClient tidying;
        if (tidying.connect()) {
            static_cast<void>(tidying.hello({{"command", "kill"}, {"name", "look-here"}}));
            Frame ack;
            static_cast<void>(tidying.reply(ack));
        }
    }

    {
        TestClient lost;
        std::string snapshot;
        std::string error;
        expect("attaching to something that is not there says so",
               !attach(lost, "nowhere", 24, 80, snapshot, &error));
        expect("and says what is missing", error.find("nowhere") != std::string::npos);
    }

    {
        // Exit the shell from inside: every view is told, and the session leaves the
        // listing rather than lingering as something that cannot be attached to.
        expect("a shell can be told to leave",
               [] {
                   TestClient c;
                   std::string snap;
                   if (!attach(c, "work", 24, 80, snap))
                       return false;
                   return c.send(MsgKind::Data, "exit\n");
               }());
        bool gone = false;
        for (int attempt = 0; attempt < 100 && !gone; ++attempt) {
            gone = listed().empty();
            if (!gone)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        expect("and it goes away on its own", gone);
    }

    {
        // The daemon has to answer the questions a program asks — what terminal is
        // this, where is the cursor — because it is the only thing on the other end of
        // the pty when nobody is looking. A program that gets no answer waits: fish
        // spends ten seconds on the device attributes query before announcing that its
        // terminal is broken, and tmux will not start properly at all.
        //
        // Asking an interactive shell to observe the answer does not work: bash reads
        // ahead, so its line editor takes the reply the moment the question is asked
        // and beeps at it. So the session runs a program that asks and listens itself.
        const std::string asker_path = root + "/ask-what-terminal";
        {
            std::ofstream script(asker_path);
            // The tty has to be taken out of canonical mode first: it holds input
            // back until a newline arrives, and a terminal reply has none, so the
            // listener would wait for a line that never comes. Out of echo as well, so
            // that what is read is the reply itself rather than the line discipline's
            // caret rendering of it. No `timeout` around the read either: it puts the
            // reader in a process group of its own, and a group that is not the
            // foreground one is stopped the moment it touches the terminal.
            script << "#!/bin/sh\n"
                      "stty -icanon -echo 2>/dev/null\n"
                      "printf '\\033[5n' > /dev/tty\n"
                      "sleep 1\n"
                      "reply=$(dd bs=1 count=4 2>/dev/null)\n"
                      "stty icanon echo 2>/dev/null\n"
                      "printf 'REPLY[%s]\\n' \"$reply\"\n"
                      "sleep 1\n";
        }
        ::chmod(asker_path.c_str(), 0755);
        {
            TestClient client;
            bool made = client.connect();
            static_cast<void>(
                made && client.hello({{"command", "new"},
                                      {"name", "asking"},
                                      {"shell", asker_path},
                                      {"rows", "10"},
                                      {"cols", "60"}}));
            Frame reply_frame;
            made = made && client.reply(reply_frame) && reply_frame.kind == MsgKind::Ack;
            expect("a session can run a program instead of a shell", made);
        }
        TestClient asker;
        std::string snapshot;
        std::string error;
        expect("with a view attached", attach(asker, "asking", 10, 60, snapshot, &error));
        const std::string answered = asker.waitFor("REPLY[", 12000);
        const std::size_t at = answered.find("REPLY[");
        const bool replied = at != std::string::npos &&
                             answered.find("\x1b" "[0n", at) != std::string::npos;
        if (!replied) {
            std::cerr << "  saw: " << show(answered) << "\n";
            ++checks;
            ++failures;
            std::cerr << "FAIL: the daemon answered the terminal query\n";
        } else {
            expect("the daemon answered the terminal query", true);
        }
        TestClient cleaner;
        static_cast<void>(
            cleaner.connect() && cleaner.hello({{"command", "kill"}, {"name", "asking"}}));
        for (int attempt = 0; attempt < 100 && !listed().empty(); ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        expect("and the session is put away", listed().empty());
    }

    {
        // A program that asks for its text to be copied is heard by the daemon, which
        // has no clipboard of its own and so passes the request to whoever is looking.
        // Its own session, because the answer to the question above leaves a couple of
        // bytes in the shell's input, and those would mangle the next command typed
        // into it — which is worth knowing, and worth not building a test on.
        expect("a session for a program that wants to copy", createSession("copied", 24, 80));
        TestClient copier;
        std::string ignored;
        bool heard_the_copy = false;
        if (attach(copier, "copied", 24, 80, ignored)) {
            expect("the copy request is sent",
                   copier.send(MsgKind::Data,
                               "printf '\\033]52;c;aGVsbG8=\\a' > /dev/tty\n"));
            // The request arrives as a frame of its own, not as bytes to draw: it is
            // something the terminal is asked to do, not something to show.
            for (int attempt = 0; attempt < 80 && !heard_the_copy; ++attempt) {
                struct pollfd pfd {copier.fd, POLLIN, 0};
                if (::poll(&pfd, 1, 100) > 0) {
                    char buf[16384];
                    const ssize_t n = readSome(copier.fd, buf, sizeof(buf));
                    if (n > 0)
                        copier.reader.append(
                            std::string_view(buf, static_cast<std::size_t>(n)));
                }
                Frame frame;
                while (copier.reader.pop(frame)) {
                    if (frame.kind != MsgKind::Clipboard)
                        continue;
                    const Fields fields = decodeFields(frame.body);
                    if (fieldValue(fields, "text") == "hello" &&
                        fieldValue(fields, "primary") == "0")
                        heard_the_copy = true;
                }
            }
        }
        expect("and it reaches the view with the text decoded", heard_the_copy);


        TestClient tidier;
        static_cast<void>(
            tidier.connect() && tidier.hello({{"command", "kill"}, {"name", "copied"}}));
        for (int attempt = 0; attempt < 100 && !listed().empty(); ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    {
        // The other direction: a program asking for what is in the clipboard, which is
        // how `scp host:file` and friends paste over a link. The daemon cannot answer
        // this one — the clipboard is on the machine with the window — so the request
        // has to reach a view, and whatever the view sends back has to reach the
        // program. Both halves are checked here, with this client being the view.
        //
        // The reply is read by count rather than until it ends, because non-canonical
        // reads return whatever is there and `dd` would sit waiting for bytes nobody is
        // going to send. Twenty is exactly what the reply below is: ESC, `]52;c;`,
        // twelve bytes of base64 for "clipped", and a BEL.
        const std::string asker_path = root + "/ask-clipboard";
        {
            std::ofstream script(asker_path);
            script << "#!/bin/sh\n"
                   << "stty -icanon -echo\n"
                   << "printf '\\033]52;c;?\\a' > /dev/tty\n"
                   << "reply=$(dd bs=1 count=20 2>/dev/null)\n"
                   << "stty icanon echo\n"
                   << "printf 'PASTE[%s]\\n' \"$reply\" | cat -v\n";
        }
        ::chmod(asker_path.c_str(), 0755);

        TestClient maker;
        bool made = maker.connect();
        made = made && maker.hello({{"command", "new"},
                                    {"name", "pasting"},
                                    {"shell", asker_path},
                                    {"rows", "10"},
                                    {"cols", "60"}});
        Frame ack;
        made = made && maker.reply(ack) && ack.kind == MsgKind::Ack;
        expect("a session starts that wants to paste", made);

        TestClient viewer;
        std::string ignored;
        bool heard_the_request = false;
        bool answered = false;
        std::string seen;
        // `cat -v` in the program keeps the reply printable; shown raw it would be
        // another escape sequence for the screen model to obey rather than to show.
        const std::string answer_on_screen = "^[]52;c;Y2xpcHBlZA==^G";
        if (made && attach(viewer, "pasting", 10, 60, ignored)) {
            // Keep reading after the answer has gone back out: the point of the check
            // is what the program does with it, and that arrives afterwards.
            for (int attempt = 0;
                 attempt < 120 && seen.find(answer_on_screen) == std::string::npos;
                 ++attempt) {
                struct pollfd pfd {viewer.fd, POLLIN, 0};
                if (::poll(&pfd, 1, 100) > 0) {
                    char buf[16384];
                    const ssize_t n = readSome(viewer.fd, buf, sizeof(buf));
                    if (n > 0)
                        viewer.reader.append(std::string_view(buf, static_cast<std::size_t>(n)));
                }
                Frame frame;
                while (viewer.reader.pop(frame)) {
                    if (frame.kind == MsgKind::Clipboard) {
                        const Fields fields = decodeFields(frame.body);
                        if (fieldValue(fields, "query") == "1") {
                            heard_the_request = true;
                            // What a window would send: the clipboard's contents, in
                            // the bytes the terminal was asked for them in.
                            answered = viewer.send(MsgKind::Data,
                                                   "\x1b" "]52;c;Y2xpcHBlZA==" "\x07");
                        }
                    } else if (frame.kind == MsgKind::Data || frame.kind == MsgKind::Snapshot) {
                        seen += frame.body;
                    }
                }
            }
        }
        expect("the read request reaches the view as a read", heard_the_request);
        expect("the view answers it", answered);
        expect("and the answer reaches the program that asked",
               seen.find(answer_on_screen) != std::string::npos);
        TestClient tidier;
        static_cast<void>(
            tidier.connect() && tidier.hello({{"command", "kill"}, {"name", "pasting"}}));
        for (int attempt = 0; attempt < 100 && !listed().empty(); ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    {
        expect("another session starts", createSession("second", 30, 100));
        TestClient viewer;
        std::string snapshot;
        expect("with a view on it", attach(viewer, "second", 30, 100, snapshot));
        expect("killing it is acknowledged",
               [] {
                   TestClient c;
                   return c.connect() && c.hello({{"command", "kill"}, {"name", "second"}}) &&
                          [&c] {
                              Frame reply;
                              return c.reply(reply) && reply.kind == MsgKind::Ack;
                          }();
               }());
        // Read until the session says it ended. Whatever else was on the way — the tab bar
        // it was handed on arrival, output that had not been taken yet — a client keeps
        // reading past, and a check that looks only at the next frame is checking the order
        // things happened to arrive in.
        Frame exit_frame;
        bool told = false;
        for (int attempt = 0; attempt < 100 && !told; ++attempt) {
            Frame frame;
            if (!viewer.reply(frame))
                break;
            if (frame.kind == MsgKind::Exit) {
                exit_frame = frame;
                told = true;
            }
        }
        expect("the view is told the session ended", told);
        expect("with an exit frame", exit_frame.kind == MsgKind::Exit);
        expect("carrying how it ended",
               decodeFields(exit_frame.body).size() == 2);
        bool gone = false;
        for (int attempt = 0; attempt < 100 && !gone; ++attempt) {
            gone = listed().empty();
            if (!gone)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        expect("and it is no longer listed", gone);
        expect("killing something already gone is refused",
               [] {
                   TestClient c;
                   return c.connect() && c.hello({{"command", "kill"}, {"name", "second"}}) &&
                          [&c] {
                              Frame reply;
                              return c.reply(reply) && reply.kind == MsgKind::Error;
                          }();
               }());
    }

    {
        // A shell in a window is not a session anybody decided to keep. It runs under
        // the daemon so that a crash cannot take it, but it stays unsaved until the
        // person says otherwise, and it dies when the window says it is closing —
        // unless the window never got the chance to say anything, which is the whole
        // difference between being closed and being killed.
        auto findInfo = [](const std::string& name) {
            SessionInfo missing;
            missing.alive = false;
            for (const SessionInfo& info : listed()) {
                if (info.name == name)
                    return info;
            }
            return missing;
        };
        {
            TestClient client;
            bool made = client.connect();
            made = made && client.hello({{"command", "new"},
                                         {"name", "win"},
                                         {"saved", "0"},
                                         {"rows", "10"},
                                         {"cols", "60"}});
            Frame ack;
            made = made && client.reply(ack) && ack.kind == MsgKind::Ack;
            expect("a window's shell starts out unsaved", made);
        }
        expect("and is not one of the saved sessions", !findInfo("win").saved);
        expect("and is running", findInfo("win").alive);
        expect("with nothing out front", findInfo("win").foreground.empty());

        expect("the shell takes a command", daemon.type("win", "sleep 30\n"));
        std::string running = findInfo("win").foreground;
        for (int attempt = 0; attempt < 60 && running.empty(); ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            running = findInfo("win").foreground;
        }
        expect("and the daemon says what is running out front", running == "sleep");

        {
            TestClient saver;
            bool kept = saver.connect();
            kept = kept && saver.hello({{"command", "save"}, {"name", "win"}, {"as", "kept"}});
            Frame ack;
            kept = kept && saver.reply(ack) && ack.kind == MsgKind::Ack;
            expect("saving it keeps it", kept);
        }
        expect("under the name it was given", findInfo("kept").saved && findInfo("kept").alive);
        expect("and not under the one it started with", findInfo("win").name.empty());
        expect("still with the same thing running", findInfo("kept").foreground == "sleep");

        {
            // A name in use belongs to a shell somebody is watching, so taking it over
            // would be losing their session to rename someone else's.
            expect("another shell starts", createSession("taken"));
            TestClient grabber;
            bool refused = grabber.connect();
            refused = refused && grabber.hello(
                                      {{"command", "save"}, {"name", "kept"}, {"as", "taken"}});
            Frame reply_frame;
            refused = refused && grabber.reply(reply_frame);
            expect("a name already taken is refused",
                   refused && reply_frame.kind == MsgKind::Error &&
                       findInfo("kept").saved && findInfo("taken").saved);
        }

        {
            // Closing on purpose. The view says so as it goes, and that is the only
            // thing that destroys an unsaved session.
            expect("a third shell starts", createSession("closing", 10, 60, false));
            TestClient viewer;
            std::string snapshot;
            std::string error;
            expect("with a window looking at it",
                   attach(viewer, "closing", 10, 60, snapshot, &error));
            expect("and saying it is being closed",
                   viewer.send(MsgKind::Detach, encodeFields({{"discard", "1"}})));
            viewer.send(MsgKind::Exit);
            for (int attempt = 0; attempt < 100 && !findInfo("closing").name.empty(); ++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            expect("the shell goes with it", findInfo("closing").name.empty());
        }

        {
            // And the case that matters more: nobody said anything. A window that is
            // killed, crashes or loses its machine leaves the shell where it is, because
            // silence is not a person throwing work away.
            expect("a fourth shell starts", createSession("abandoned", 10, 60, false));
            TestClient viewer;
            std::string snapshot;
            std::string error;
            expect("with a window looking at it",
                   attach(viewer, "abandoned", 10, 60, snapshot, &error));
            ::close(viewer.fd);
            viewer.fd = -1; // gone without a word, the way a crash goes
            bool still_there = false;
            for (int attempt = 0; attempt < 100 && !still_there; ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                const SessionInfo info = findInfo("abandoned");
                still_there = !info.name.empty() && info.alive && info.clients == 0;
            }
            expect("the shell is left behind rather than lost", still_there);
            expect("and it is still not saved", !findInfo("abandoned").saved);
            {
                TestClient cleaner;
                static_cast<void>(
                    cleaner.connect() && cleaner.hello({{"command", "discard"},
                                                        {"name", "abandoned"}}));
                Frame ack;
                static_cast<void>(cleaner.reply(ack));
            }
            for (int attempt = 0; attempt < 100 && !findInfo("abandoned").name.empty(); ++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            expect("and discarding it is what finally ends it",
                   findInfo("abandoned").name.empty());
        }

        expect("the saved one outlives all of it", findInfo("kept").saved);
        expect("and nothing is left unsaved", [&] {
            for (const SessionInfo& info : listed()) {
                if (!info.saved)
                    return false;
            }
            return true;
        }());
    }

    {
        // Ids. A listing that sorts w1, w10, w11 is alphabetical and no use as a
        // history, and a shell nobody named still has to be pointable at.
        std::string error;
        const std::vector<SessionInfo> before = listed();
        int previous_top = -1;
        for (const SessionInfo& info : before)
            if (info.id > previous_top)
                previous_top = info.id;
        expect("a session to number", createSession("numbered", 24, 80));
        const std::vector<SessionInfo> all = listed();
        int top = previous_top;
        std::string top_name;
        for (const SessionInfo& info : all) {
            if (info.id > top) {
                top = info.id;
                top_name = info.name;
            }
        }
        expect("the newest shell carries the highest number", top_name == "numbered");
        expect("a fresh number is above every one already given out, because none are "
               "reused",
               top > previous_top);

        // Two properties a person can actually rely on when reading the list: no two
        // shells answer to the same number, and the numbers run in the order the shells
        // started, so the list reads as an afternoon rather than as a dictionary. Shells
        // that have gone are not in the listing, which is why the check is about order
        // and not about the numbers being next to each other.
        bool distinct = true;
        bool in_start_order = true;
        for (const SessionInfo& a : all) {
            for (const SessionInfo& b : all) {
                if (a.name == b.name)
                    continue;
                if (a.id == b.id)
                    distinct = false;
                if (a.started < b.started && a.id >= b.id)
                    in_start_order = false;
            }
        }
        expect("no two shells share a number", distinct);
        expect("and the numbers run in the order the shells started", in_start_order);

        expect("a number is a handle: it types into that shell",
               daemon.type(std::to_string(top), "echo BY-ID\n"));
        expect("and a number that was never handed out is not a handle",
               !daemon.type(std::to_string(top + 500), "echo NOPE\n"));

        // The precedence, said plainly: a name wins. Somebody may have typed `new -n 7`
        // on purpose, and handing out numbers is no reason to take away what they chose.
        // Naming a new shell after a number already in use is the only way to find out
        // which of the two the daemon answers to.
        expect("a session named after another one's number",
               createSession(std::to_string(top), 24, 80));
        expect("killing that number kills the one that was named",
               daemon.kill(std::to_string(top), error));
        // A kill says so before the shell's own reader thread has taken it out of the
        // listing, so this waits rather than reading a moment that has not happened yet.
        bool named_gone = false;
        for (int attempt = 0; attempt < 60 && !named_gone; ++attempt) {
            named_gone = !listed_have(std::to_string(top));
            if (!named_gone)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        expect("the named one is gone", named_gone);
        expect("and the shell that was number " + std::to_string(top) + " is still here",
               listed_have(top_name));
        expect("nothing else went with it", listed().size() == all.size());
    }

    {
        // A window's shell starts where a shell starts, not where the executable
        // happens to live. That is the user's home, and it is the shell's own
        // starting directory rather than a `cd` command typed into it.
        std::string error;
        const std::string home = homeDirectory();
        const std::string session = startWindowSession(24, 80, error);
        expect("a window's shell is started", !session.empty());
        TestClient view;
        std::string snapshot;
        expect("and the view attaches", attach(view, session, 24, 80, snapshot, &error));
        expect("no cd command was typed",
               TestClient::render(snapshot, 24, 80).find("cd ") == std::string::npos);
        view.send(MsgKind::Data, "pwd\n");
        const std::string text = view.waitFor(home);
        expect("and pwd says the user's home", text.find(home) != std::string::npos);
        ::close(view.fd);
        view.fd = -1;
    }

    {
        // A layout can be handed to a session whole, and a whole layout can be deeper than
        // the window is wide: twenty columns divided four times over leaves rooms two columns
        // across, then one, then none at all. A shell started in a room with no columns in it
        // has a screen with no line in it, and the first character its program writes walks
        // off the end of a vector — which does not close a pane politely. It takes the daemon
        // down, and every window on every session with it.
        expect("a session to crowd", createSession("crowded", 24, 20));
        TestClient crowd;
        std::string crowd_screen;
        expect("with a view looking at it", attach(crowd, "crowded", 24, 20, crowd_screen));
        // Six panes across twenty columns, divided one after another: the rooms get to two
        // columns, then one, and the last division has nothing left to divide.
        const std::string deep =
            "split left-right\n"
            "  split left-right\n"
            "    split left-right\n"
            "      split left-right\n"
            "        split left-right\n"
            "          pane\n"
            "          pane\n"
            "        pane\n"
            "      pane\n"
            "    pane\n"
            "  pane\n"
            "pane\n";
        const std::string refused = giveLayout("crowded", deep);
        // The reason has to be the one that is true — there is no room — because a person
        // reading it needs to know to widen the window rather than send it again.
        expect("an arrangement with no room in it is refused (said: " + refused + ")",
               !refused.empty() && refused.find("no room") != std::string::npos);
        expect("and the session is left carrying what it had",
               listedPanes("crowded") == 1);
        expect("the daemon is still standing", listedPanes("crowded") >= 1);
        // The marker is written as two halves with nothing between them, so that the only
        // place the whole word can appear is where the shell printed it. Typed at twenty
        // columns the command itself wraps, and a check that accepts the echo of what it sent
        // is checking that the terminal can repeat itself.
        expect("the view is still looking at a program that answers",
               [&crowd]() {
                   static_cast<void>(crowd.send(MsgKind::Data, "echo AFTER-\"\"DEEP\n"));
                   return TestClient::render(crowd.waitFor("AFTER-DEEP", 5000), 24, 20)
                       .find("AFTER-DEEP") != std::string::npos;
               }());
        // And one that does fit is still taken: refusing what has no room must not turn into
        // refusing to divide at all.
        expect("an arrangement with room in it is still taken",
               giveLayout("crowded",
                          "split left-right\n"
                          "  pane\n"
                          "  pane\n")
                   .empty());
        expect("and the session runs the programs it was given", listedPanes("crowded") == 2);
        ::close(crowd.fd);
        crowd.fd = -1;
    }

    {
        expect("one more session for the way out", createSession("last", 24, 80));
        TestClient watcher;
        std::string snapshot;
        expect("with a view attached", attach(watcher, "last", 24, 80, snapshot));
        daemon.stop();
        expect("stopping empties the list", listed().empty());
    }

    serving.join();

    {
        struct stat st {};
        expect("the socket is left no more", ::stat(daemonSocketPath().c_str(), &st) != 0);
        TestClient late;
        expect("and nothing answers afterwards", !late.connect());
    }

    cleanup();

    if (failures == 0) {
        std::cout << "PASS: " << checks << " session daemon checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " session checks failed\n";
    return 1;
}
