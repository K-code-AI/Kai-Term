#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace kai {

// Unix-domain sockets for the session daemon.
//
// The socket is a file rather than an abstract-namespace name, so that `ls` shows
// what sessions exist and `socat` can be pointed at one while debugging. That costs
// one thing a nameless socket does not have to pay for: a file left behind by a
// killed daemon, which every later start would otherwise mistake for a live server.
// `bindListen` therefore asks the socket before removing it.

inline constexpr int kSocketPathLimit = static_cast<int>(sizeof(sockaddr_un::sun_path)) - 1;

// A session name becomes part of a path, so it has to be boring: no separators, no
// all-dot names, nothing that could step out of the runtime directory. Everything
// else is the person's business, including spaces and accents.
inline bool sessionNameOk(std::string_view name) {
    if (name.empty() || name.size() > 64)
        return false;
    if (name.front() == '.')
        return false;
    for (const char c : name) {
        if (c == '/' || c == '\0' || c == '\n')
            return false;
    }
    return true;
}

// Where the socket for a session lives. `$XDG_RUNTIME_DIR` is the right place when
// the desktop set it up — it is per-user, private and cleaned up on logout — with a
// directory under /tmp as the fallback for a plain shell, matching what tmux does.
//
// Returns empty when the name is not usable or the resulting path would not fit in
// sockaddr_un, which is the one failure a long home directory can produce.
inline std::string sessionSocketPath(std::string_view name) {
    if (!sessionNameOk(name))
        return {};
    std::string dir;
    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg) {
        dir = std::string(xdg) + "/kai-term";
    } else {
        dir = "/tmp/kai-term-" + std::to_string(static_cast<long>(::getuid()));
    }
    const std::string path = dir + "/" + std::string(name) + ".sock";
    if (static_cast<int>(path.size()) > kSocketPathLimit)
        return {};
    return path;
}

// The directory a socket lives in, created 0700: another user must not be able to
// connect to someone else's shell, and a world-writable directory would let them
// swap the socket file for their own.
inline bool makeSocketDirectory(std::string_view path) {
    std::string dir(path);
    const std::size_t slash = dir.find_last_of('/');
    if (slash == std::string::npos || slash == 0)
        return false;
    dir.resize(slash);
    // $XDG_RUNTIME_DIR is meant to be there already, and normally is. Pointing it at a
    // path of your own is how sessions end up somewhere specific, though, and then the
    // level we make is not the only one missing — so every level is made, not just
    // ours. Without this the daemon cannot bind and the only clue is a socket that
    // never appears.
    for (std::size_t end = dir.find('/', 1);; end = dir.find('/', end + 1)) {
        const std::string level = dir.substr(0, end == std::string::npos ? dir.size() : end);
        if (::mkdir(level.c_str(), 0700) != 0 && errno != EEXIST)
            return false;
        if (end == std::string::npos)
            break;
    }
    struct stat st {};
    // Ours to use only if it is a directory we own and nobody else can read: refuse
    // rather than loosen it or write into a directory someone else owns.
    return ::stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == ::getuid() &&
           (st.st_mode & 0777) == 0700;
}

inline bool fillAddress(const std::string& path, sockaddr_un& addr, socklen_t& len) {
    if (static_cast<int>(path.size()) > kSocketPathLimit)
        return false;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.data(), path.size());
    len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size());
    return true;
}

// Is there a server answering on this path right now? A refused connection means the
// file is a leftover; a successful one means a daemon is live and must not be
// displaced.
inline bool socketHasListener(const std::string& path) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return true; // cannot tell, so assume the safe answer
    sockaddr_un addr {};
    socklen_t len = 0;
    if (!fillAddress(path, addr, len)) {
        ::close(fd);
        return true;
    }
    const bool connected = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), len) == 0;
    ::close(fd);
    return connected;
}

// A listening socket on `path`, with a stale socket file cleared out of the way.
// Returns -1 and sets `error` when it cannot be had.
inline int bindListen(const std::string& path, int backlog, std::string& error) {
    if (path.empty()) {
        error = "no usable socket path";
        return -1;
    }
    if (!makeSocketDirectory(path)) {
        error = "cannot create the socket directory for " + path;
        return -1;
    }
    struct stat st {};
    if (::stat(path.c_str(), &st) == 0) {
        // A socket nobody answers on is a dead daemon's remains. It is only removed
        // if it is a socket and it is ours; anything else in the way is left alone
        // and reported.
        if (!S_ISSOCK(st.st_mode)) {
            error = path + " exists and is not a socket";
            return -1;
        }
        if (st.st_uid != ::getuid()) {
            error = path + " belongs to another user";
            return -1;
        }
        if (socketHasListener(path)) {
            error = "a daemon is already listening on " + path;
            return -1;
        }
        if (::unlink(path.c_str()) != 0) {
            error = std::string("cannot remove the stale socket ") + path + ": " +
                    std::strerror(errno);
            return -1;
        }
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        error = std::string("socket: ") + std::strerror(errno);
        return -1;
    }
    sockaddr_un addr {};
    socklen_t len = 0;
    if (!fillAddress(path, addr, len)) {
        error = "socket path is too long for the kernel";
        ::close(fd);
        return -1;
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
        error = std::string("bind ") + path + ": " + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    // The socket file is the door to a shell. The default umask is often 022, which
    // would leave it readable and writable by anyone on the machine.
    if (::chmod(path.c_str(), 0600) != 0) {
        error = std::string("chmod ") + path + ": " + std::strerror(errno);
        ::close(fd);
        ::unlink(path.c_str());
        return -1;
    }
    if (::listen(fd, backlog) != 0) {
        error = std::string("listen: ") + std::strerror(errno);
        ::close(fd);
        ::unlink(path.c_str());
        return -1;
    }
    error.clear();
    return fd;
}

inline int connectTo(const std::string& path) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    sockaddr_un addr {};
    socklen_t len = 0;
    if (path.empty() || !fillAddress(path, addr, len)) {
        ::close(fd);
        return -1;
    }
    for (;;) {
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), len) == 0)
            return fd;
        if (errno != EINTR) {
            ::close(fd);
            return -1;
        }
    }
}

// Who is on the other end. The kernel's answer, not something the peer claimed,
// which is what makes it worth checking: a session belongs to the person who made
// it and to nobody else.
inline bool peerCredentials(int fd, uid_t& uid, pid_t& pid) {
    struct ucred cred {};
    socklen_t len = sizeof(cred);
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0)
        return false;
    uid = cred.uid;
    pid = cred.pid;
    return true;
}

// MSG_NOSIGNAL rather than write(): the first write to a peer that has gone is
// supposed to raise SIGPIPE, and a daemon that dies because a window closed is worse
// than one that reports the lost client.
inline bool writeAll(int fd, std::string_view bytes) {
    std::size_t off = 0;
    while (off < bytes.size()) {
        const ssize_t n =
            ::send(fd, bytes.data() + off, bytes.size() - off, MSG_NOSIGNAL);
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

// Bytes as they arrive: 0 for a closed peer, -1 for an error, EINTR retried.
inline ssize_t readSome(int fd, char* buf, std::size_t n) {
    for (;;) {
        const ssize_t got = ::read(fd, buf, n);
        if (got >= 0 || errno != EINTR)
            return got;
    }
}

// How long a read may wait before it gives up: 0 turns the limit off again.
//
// A server that blocks in read() with no limit is a server whose thread count is
// whatever the other end decides to open. Something connected that never says a word —
// a port scanner, a monitoring probe, a script that opened a socket and forgot it —
// would otherwise hold a thread and a descriptor for as long as the daemon lives, and
// a daemon holding shells is one you cannot restart. So the handshakes that a real
// client answers in milliseconds are given a limit, and the long waits that belong to
// a person looking at a session are given none.
inline bool setReadTimeout(int fd, int ms) {
    struct timeval tv {};
    if (ms > 0) {
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
    }
    return ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

} // namespace kai
