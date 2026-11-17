#include "../src/process/pty.hpp"
#include "../src/terminal/screen.hpp"

#include <iostream>
#include <string>

static bool contains(const kai::Screen& s, const std::string& needle) {
    for (int r = 0; r < s.rows(); ++r) {
        std::string line;
        for (int c = 0; c < s.cols(); ++c) {
            const auto& cell = s.at(r, c);
            if (!cell.text.empty())
                line += cell.text;
        }
        if (line.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

int main() {
    kai::Pty p(4, 30, "/bin/sh");
    kai::Screen s(6, 60);

    p.write("echo HELLO\n");

    bool found = false;
    for (int attempt = 0; attempt < 32 && !found; ++attempt) {
        char buf[4096];
        ssize_t n = p.read(buf, sizeof(buf));
        if (n <= 0)
            break;
        s.feed(std::string(buf, static_cast<std::size_t>(n)));
        found = contains(s, "HELLO");
    }

    if (!found) {
        std::cerr << "FAIL: HELLO not found in PTY output\n";
        return 1;
    }

    p.resize(6, 40);
    p.write("stty size\n");

    bool resized = false;
    for (int attempt = 0; attempt < 32 && !resized; ++attempt) {
        char buf[4096];
        ssize_t n = p.read(buf, sizeof(buf));
        if (n <= 0)
            break;
        s.feed(std::string(buf, static_cast<std::size_t>(n)));
        resized = contains(s, "6 40");
    }

    if (!resized) {
        std::cerr << "FAIL: PTY did not report size 6 40\n";
        return 1;
    }

    std::cout << "PASS: PTY output contains HELLO and resize 6 40\n";
    return 0;
}
