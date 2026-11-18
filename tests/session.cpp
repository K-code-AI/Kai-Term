// Headless check of the session daemon: real pty, real socket, no display.
//
// The daemon's promises are the ones that are hardest to check by reading: that a
// shell outlives the window that started it, that a second window sees what is on
// screen, that a view hanging up does not disturb the program running inside, and
// that stopping leaves nothing behind. All of those need a process on the other end
// of a pty and a connection that can be dropped, so this test has both.

#include "../src/ipc/protocol.hpp"
#include "../src/session/daemon.hpp"
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
                       message.find("session shutdown") != std::string::npos);
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
        Frame exit_frame;
        expect("the view is told the session ended", viewer.reply(exit_frame));
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
        expect("the named one is gone", !listed_have(std::to_string(top)));
        expect("and the shell that was number " + std::to_string(top) + " is still here",
               listed_have(top_name));
        expect("nothing else went with it", listed().size() == all.size());
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
