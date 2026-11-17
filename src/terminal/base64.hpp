#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace kai {

// Base64, because OSC 52 — the escape sequence an application uses to ask for
// its contents to be copied — carries its payload that way. It has to work in
// the screen model, which knows nothing about any GUI toolkit, so this is plain
// bytes in, bytes out.

inline int base64Value(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

// Decodes as much as is well formed and stops at the first byte that cannot be
// part of the data. Terminals are told to be forgiving here: some programs wrap
// the payload across lines, and a stray newline is not a reason to lose the
// whole selection someone asked to copy.
inline std::string base64Decode(std::string_view in) {
    std::string out;
    out.reserve(in.size() / 4 * 3 + 3);
    int acc = 0;
    int bits = 0;
    for (const char c : in) {
        if (c == '=')
            break;
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t' || c == '\a')
            continue;
        const int v = base64Value(c);
        if (v < 0)
            break;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

inline std::string base64Encode(std::string_view in) {
    static constexpr char kChars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 3 <= in.size()) {
        const unsigned int v = (static_cast<unsigned char>(in[i]) << 16) |
                               (static_cast<unsigned char>(in[i + 1]) << 8) |
                               static_cast<unsigned char>(in[i + 2]);
        out.push_back(kChars[(v >> 18) & 0x3F]);
        out.push_back(kChars[(v >> 12) & 0x3F]);
        out.push_back(kChars[(v >> 6) & 0x3F]);
        out.push_back(kChars[v & 0x3F]);
        i += 3;
    }
    const std::size_t left = in.size() - i;
    if (left == 1) {
        const unsigned int v = static_cast<unsigned char>(in[i]) << 16;
        out.push_back(kChars[(v >> 18) & 0x3F]);
        out.push_back(kChars[(v >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (left == 2) {
        const unsigned int v = (static_cast<unsigned char>(in[i]) << 16) |
                               (static_cast<unsigned char>(in[i + 1]) << 8);
        out.push_back(kChars[(v >> 18) & 0x3F]);
        out.push_back(kChars[(v >> 12) & 0x3F]);
        out.push_back(kChars[(v >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

} // namespace kai
