// Headless check of the Unix-socket layer: names, paths, binding, stale sockets,
// peer credentials and the write helper. Real sockets, no daemon yet — the daemon
// itself is exercised by tests/session.cpp.

#include "../src/ipc/protocol.hpp"
#include "../src/ipc/socket.hpp"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <sys/stat.h>
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
    std::cerr << "FAIL: " << what << "\n";
}

void expectStr(const char* what, const std::string& got, const std::string& want) {
    ++checks;
    if (got == want)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << ": want [" << want << "] got [" << got << "]\n";
}

std::string root;

std::string inRoot(const std::string& leaf) { return root + "/" + leaf; }

void removeTree() {
    // Only the handful of files this test makes, in a directory it made itself.
    for (const char* leaf : {"work.sock", "plain", "second.sock"})
        ::unlink(inRoot(leaf).c_str());
    ::rmdir(inRoot("kai-term").c_str());
    ::rmdir(root.c_str());
}

} // namespace

int main() {
    root = "/tmp/kai-ipc-test-" + std::to_string(static_cast<long>(::getpid()));
    if (::mkdir(root.c_str(), 0700) != 0 && errno != EEXIST) {
        std::cerr << "cannot create the test directory: " << std::strerror(errno) << "\n";
        return 1;
    }
    ::setenv("XDG_RUNTIME_DIR", root.c_str(), 1);

    // Names become path components, so the interesting cases are the ones that would
    // not be a name anywhere else.
    expect("an ordinary name is fine", sessionNameOk("work"));
    expect("a name with a space is fine", sessionNameOk("my work"));
    expect("a name with accents is fine", sessionNameOk("travail-\xc3\xa9t\xc3\xa9"));
    expect("an empty name is not", !sessionNameOk(""));
    expect("a dot is not", !sessionNameOk("."));
    expect("two dots are not", !sessionNameOk(".."));
    expect("a leading dot is not", !sessionNameOk(".hidden"));
    expect("a separator is not", !sessionNameOk("a/b"));
    expect("an absolute path is not", !sessionNameOk("/etc/passwd"));
    expect("a newline is not", !sessionNameOk("a\nb"));
    expect("a 64-character name is", sessionNameOk(std::string(64, 'n')));
    expect("a 65-character name is not", !sessionNameOk(std::string(65, 'n')));

    {
        const std::string path = sessionSocketPath("work");
        expect("a session gets a socket path", !path.empty());
        expect("inside the runtime directory",
               path.rfind(root + "/kai-term/", 0) == 0);
        expect("named for the session", path.size() > 5 && path.compare(path.size() - 5, 5, ".sock") == 0);
        expect("an unusable name has no path", sessionSocketPath("a/b").empty());
    }
    {
        // sockaddr_un has a short fixed path buffer, and a long enough
        // $XDG_RUNTIME_DIR would overflow it. The answer has to be "no path", not a
        // truncated one that binds somewhere surprising.
        const std::string saved = root;
        const std::string deep = root + "/" + std::string(120, 'd');
        ::setenv("XDG_RUNTIME_DIR", deep.c_str(), 1);
        expect("a path too long for the kernel is refused", sessionSocketPath("work").empty());
        ::setenv("XDG_RUNTIME_DIR", saved.c_str(), 1);
    }

    // Bind, connect, and see who the kernel says is on the other end.
    const std::string path = sessionSocketPath("work");
    {
        std::string error;
        const int listener = bindListen(path, 4, error);
        expect("a daemon can listen on its socket path (" + error + ")", listener >= 0);

        struct stat st {};
        expect("and the socket is private to its owner",
               ::stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);

        const int client = connectTo(path);
        expect("a client can connect", client >= 0);
        const int peer = client >= 0 ? ::accept(listener, nullptr, nullptr) : -1;
        expect("and the daemon can accept it", peer >= 0);

        if (peer >= 0) {
            uid_t uid = 1;
            pid_t pid = 0;
            expect("the kernel reports who is connected", peerCredentials(peer, uid, pid));
            expect("and it is this user", uid == ::getuid());
            expect("and this process", pid == ::getpid());
        }

        // Frames must cross the socket intact, in both directions.
        if (peer >= 0 && client >= 0) {
            expect("a frame is written whole",
                   writeAll(client, encodeFrame(MsgKind::Hello, "name=work\n")));
            char buf[256];
            const ssize_t got = readSome(peer, buf, sizeof(buf));
            FrameReader reader;
            reader.append(std::string_view(buf, got > 0 ? static_cast<std::size_t>(got) : 0));
            Frame frame;
            expect("and read back as one frame", reader.pop(frame));
            expect("with the kind it was sent with", frame.kind == MsgKind::Hello);
            expectStr("and the body it was sent with", frame.body, "name=work\n");

            expect("the other way works too",
                   writeAll(peer, encodeFrame(MsgKind::Welcome, "id=1\n")));
            const ssize_t back = readSome(client, buf, sizeof(buf));
            FrameReader client_reader;
            client_reader.append(
                std::string_view(buf, back > 0 ? static_cast<std::size_t>(back) : 0));
            Frame response;
            expect("as a frame the client can read", client_reader.pop(response));
            expect("of the expected kind", response.kind == MsgKind::Welcome);

            // A peer that is gone is an ordinary event for a daemon, not a fatal one:
            // writing to it has to fail rather than raise SIGPIPE and take the daemon
            // — and every other session with it.
            ::close(peer);
            bool reported_failure = false;
            for (int i = 0; i < 8 && !reported_failure; ++i)
                reported_failure = !writeAll(client, encodeFrame(MsgKind::Data, "gone"));
            expect("writing to a closed peer reports failure instead of killing us",
                   reported_failure);
            ::close(client);
        }

        // Asking whether a daemon is home means connecting to it, which leaves a
        // connection in the accept queue for the daemon to find and drop. That is why
        // this check sits here rather than before the client exchange above: an
        // accept there would have picked up the probe instead of the client, and the
        // test would have spent its time talking to a socket someone had already hung
        // up on.
        std::string again;
        const int second = bindListen(path, 4, again);
        expect("a second daemon on the same path is refused", second < 0);
        expect("because a server is answering there",
               again.find("already listening") != std::string::npos);

        // A killed daemon leaves its socket file behind. The next start has to be able
        // to take the name over, and only because nothing is answering on it.
        ::close(listener);
        struct stat leftover {};
        expect("the socket file is left behind when the daemon goes",
               ::stat(path.c_str(), &leftover) == 0);
        std::string recovery;
        const int rebound = bindListen(path, 4, recovery);
        expect("a new daemon takes over the stale path (" + recovery + ")", rebound >= 0);
        if (rebound >= 0)
            ::close(rebound);
        ::unlink(path.c_str());
    }

    {
        // Something that is not ours, or not a socket, stays exactly where it is.
        const std::string blocked = inRoot("plain");
        ::unlink(blocked.c_str());
        const int plain = ::open(blocked.c_str(), O_CREAT | O_WRONLY, 0600);
        if (plain >= 0)
            ::close(plain);
        std::string error;
        const int fd = bindListen(blocked, 4, error);
        expect("a regular file in the socket's place is not removed", fd < 0);
        expect("and says why", error.find("not a socket") != std::string::npos);
        struct stat still_there {};
        expect("leaving it in place", ::stat(blocked.c_str(), &still_there) == 0);
        ::unlink(blocked.c_str());
    }

    {
        expect("connecting to nothing fails cleanly",
               connectTo(sessionSocketPath("nobody-home")) < 0);
        expect("an empty path is refused without a syscall", connectTo("") < 0);
    }

    removeTree();

    if (failures == 0) {
        std::cout << "PASS: " << checks << " unix socket checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " unix socket checks failed\n";
    return 1;
}
