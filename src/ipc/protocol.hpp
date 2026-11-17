#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kai {

// The bytes that pass between `kai-term` and `kai-daemon`.
//
// Two shapes are needed and they are genuinely different, so the protocol has two
// layers rather than pretending one covers both. Terminal output is opaque bytes
// that must arrive whole and in order; control is a handful of short key/value
// pairs that a person should be able to recognise in a hex dump while debugging
// something else. A generic serialiser would have made the first awkward and the
// second unverifiable from the outside.
//
// Framing is a big-endian 32-bit length, then a one-byte kind, then the body. The
// length covers the kind byte and the body, so a reader knows whether it has a
// whole message before it looks at the kind.
enum class MsgKind : std::uint8_t {
    // client -> daemon
    Hello = 1,  // fields: name, rows, cols, shell, cwd
    Data = 2,   // body: bytes for the PTY, or, from the daemon, bytes from it
    Resize = 3, // fields: rows, cols
    Detach = 4, // this client is leaving; the session stays
    Kill = 5,   // fields: name
    List = 6,   // no body

    // daemon -> client
    Welcome = 10,   // fields: name, id, rows, cols, pid, shell, created
    Snapshot = 11,  // body: bytes that rebuild the screen from an empty one
    Exit = 12,      // fields: code, signal, reason
    Error = 13,     // fields: reason
    SessionList = 14, // body: records, one per session
    Ack = 15,         // fields: what
    // A program inside a session asked for its text to be copied (OSC 52). The daemon
    // has no clipboard to put it in, so it hands the request to whoever is looking.
    Clipboard = 16, // body: the text to copy
};

// A session's whole screen is small; 16 MiB is far above any snapshot or history
// chunk while still being small enough that a corrupt length cannot ask the daemon
// to reserve a gigabyte.
inline constexpr std::size_t kMaxFrameBytes = 16u * 1024u * 1024u;

inline constexpr std::size_t kFrameHeaderBytes = 5;

// One decoded message.
struct Frame {
    MsgKind kind = MsgKind::Data;
    std::string body;
};

// The complete bytes for one message, ready to write to a socket.
inline std::string encodeFrame(MsgKind kind, std::string_view body = {}) {
    const std::uint32_t total = static_cast<std::uint32_t>(1 + body.size());
    std::string out;
    out.reserve(kFrameHeaderBytes + body.size());
    out.push_back(static_cast<char>((total >> 24) & 0xFF));
    out.push_back(static_cast<char>((total >> 16) & 0xFF));
    out.push_back(static_cast<char>((total >> 8) & 0xFF));
    out.push_back(static_cast<char>(total & 0xFF));
    out.push_back(static_cast<char>(kind));
    out.append(body);
    return out;
}

// Grows a buffer as socket reads arrive and hands out whole frames as they become
// complete. A stream is cut at arbitrary points, so anything that assumes one read
// is one message breaks the first time a paste crosses a packet boundary.
//
// Once `error()` is true it stays true: the stream is no longer aligned on message
// boundaries and there is nothing to recover. The caller should close it.
class FrameReader {
public:
    void append(std::string_view bytes) { buf_.append(bytes); }

    // True and `out` filled when a whole frame is available. False means either
    // "not enough bytes yet" — keep reading — or `error()` is set.
    bool pop(Frame& out) {
        if (error_)
            return false;
        if (buf_.size() < kFrameHeaderBytes)
            return false;
        const auto* h = reinterpret_cast<const unsigned char*>(buf_.data());
        const std::uint32_t total =
            (static_cast<std::uint32_t>(h[0]) << 24) | (static_cast<std::uint32_t>(h[1]) << 16) |
            (static_cast<std::uint32_t>(h[2]) << 8) | static_cast<std::uint32_t>(h[3]);
        if (total < 1 || total > kMaxFrameBytes) {
            error_ = true;
            return false;
        }
        const std::size_t need = kFrameHeaderBytes - 1 + total;
        if (buf_.size() < need)
            return false;
        out.kind = static_cast<MsgKind>(static_cast<unsigned char>(buf_[4]));
        out.body.assign(buf_.substr(5, total - 1));
        buf_.erase(0, need);
        return true;
    }

    bool error() const { return error_; }
    std::size_t buffered() const { return buf_.size(); }

private:
    std::string buf_;
    bool error_ = false;
};

// Short ordered key/value pairs. A vector rather than a map because a session list
// reads better when the fields stay in the order they were written, and because a
// repeated key is a question this layer should not be answering.
using Fields = std::vector<std::pair<std::string, std::string>>;

// Values are text lines, so a value containing a newline or a backslash is escaped
// (`\n`, `\\`) and a key or value containing `=` is escaped as `\x3d`. Anything else
// passes through as the bytes it was given, including UTF-8, which is what makes a
// session name with a person's own characters in it work without a wider encoding
// getting involved.
inline std::string escapeField(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (const char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '=': out += "\\x3d"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

inline std::string unescapeField(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\' || i + 1 >= in.size()) {
            out.push_back(in[i]);
            continue;
        }
        const char next = in[++i];
        switch (next) {
            case '\\': out.push_back('\\'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 'x':
                // Only `\x3d` is ever written; anything else is left as it stands
                // rather than guessing what was meant.
                if (i + 2 < in.size() && in.compare(i + 1, 2, "3d") == 0) {
                    out.push_back('=');
                    i += 2;
                } else {
                    out.push_back('\\');
                    out.push_back(next);
                }
                break;
            default:
                out.push_back('\\');
                out.push_back(next);
                break;
        }
    }
    return out;
}

inline std::string encodeFields(const Fields& fields) {
    std::string out;
    for (const auto& [key, value] : fields) {
        out += escapeField(key);
        out.push_back('=');
        out += escapeField(value);
        out.push_back('\n');
    }
    return out;
}

// Tolerant by design: a line with no `=` and an empty line are skipped rather than
// treated as a protocol violation, because a truncated write should not turn into a
// wall of errors on top of the truncation.
inline Fields decodeFields(std::string_view body) {
    Fields out;
    std::size_t pos = 0;
    while (pos < body.size()) {
        const std::size_t end = body.find('\n', pos);
        const std::string_view line =
            end == std::string_view::npos ? body.substr(pos) : body.substr(pos, end - pos);
        pos = end == std::string_view::npos ? body.size() : end + 1;
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0)
            continue;
        out.emplace_back(unescapeField(line.substr(0, eq)), unescapeField(line.substr(eq + 1)));
    }
    return out;
}

inline std::string fieldValue(const Fields& fields, std::string_view key,
                              std::string_view fallback = {}) {
    for (const auto& [name, value] : fields)
        if (name == key)
            return value;
    return std::string(fallback);
}

// A body holding several records — one per session in a list — where each record is
// itself a field block. Records are separated by a line holding a single record
// separator, which escapeField can never produce inside a value.
inline constexpr std::string_view kRecordSeparator = "\x1e";

inline std::string encodeRecords(const std::vector<Fields>& records) {
    std::string out;
    for (const auto& record : records) {
        out += encodeFields(record);
        out += kRecordSeparator;
        out.push_back('\n');
    }
    return out;
}

inline std::vector<Fields> decodeRecords(std::string_view body) {
    std::vector<Fields> out;
    // A separator closes the record being read; it does not open the next one.
    // Opening on the separator instead would add a phantom record after the last
    // session, since every record's separator is written whether or not another
    // record follows it.
    bool open = false;
    std::size_t pos = 0;
    while (pos < body.size()) {
        const std::size_t sep = body.find('\n', pos);
        const std::string_view line =
            sep == std::string_view::npos ? body.substr(pos) : body.substr(pos, sep - pos);
        pos = sep == std::string_view::npos ? body.size() : sep + 1;
        if (line == kRecordSeparator) {
            if (!open)
                out.push_back(Fields{}); // a record with no fields at all
            open = false;
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0)
            continue;
        if (!open) {
            out.push_back(Fields{});
            open = true;
        }
        out.back().emplace_back(unescapeField(line.substr(0, eq)),
                                unescapeField(line.substr(eq + 1)));
    }
    return out;
}

} // namespace kai
