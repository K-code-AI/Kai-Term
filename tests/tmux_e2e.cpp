// Headless tmux regression test.
//
// Runs a real tmux client on a PTY, feeds its output bytes through kai's
// Screen (answering DA/DSR queries like the GUI widget does), splits the
// window into two panes, scrolls output in each pane, clears each pane, and
// after every stage compares the Screen grid against tmux's own view of the
// world (capture-pane) plus separator/status sanity checks.
//
// This targets the historical bug class: LF treated as CRLF (which wrecks
// tmux's EL+LF erase sweeps), scroll-region-unaware scrolling, ignored
// IL/DL, missing DSR replies — all tmux-only rendering corruption.
//
// Layout note: tmux 3.6a with default config draws panes WITHOUT outer
// borders (content starts at column 0; one separator column between panes).
// The border thickness is inferred from the pane geometry plus the number of
// capture-pane lines, so the test also works on tmux builds that do draw
// full borders.

#include "../src/terminal/screen.hpp"

#include <poll.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using clock_ = std::chrono::steady_clock;

long elapsed_ms(clock_::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock_::now() - since).count();
}

bool write_all(int fd, const std::string& data) {
    std::size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n <= 0)
            return false;
        off += static_cast<std::size_t>(n);
    }
    return true;
}

// Pump PTY -> Screen and Screen query responses -> PTY until quiet.
void drain(int master, kai::Screen& sc, int total_ms, int idle_ms) {
    const auto start = clock_::now();
    auto last_data = start;
    bool got_data = false;
    for (;;) {
        const long total = elapsed_ms(start);
        const long quiet = elapsed_ms(last_data);
        if (total >= total_ms)
            break;
        if (got_data && quiet >= idle_ms)
            break;
        int wait = 50;
        if (wait > total_ms - static_cast<int>(total))
            wait = total_ms - static_cast<int>(total);
        pollfd p{master, POLLIN, 0};
        const int r = poll(&p, 1, wait);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (r == 0)
            continue;
        if (!(p.revents & POLLIN))
            break; // HUP/ERR: tmux client exited
        char buf[8192];
        const ssize_t n = ::read(master, buf, sizeof(buf));
        if (n <= 0)
            break;
        last_data = clock_::now();
        got_data = true;
        if (const char* dump_path = std::getenv("KAI_E2E_DUMP")) {
            if (FILE* f = fopen(dump_path, "ab")) {
                fwrite(buf, 1, static_cast<std::size_t>(n), f);
                fclose(f);
            }
        }
        sc.feed(std::string_view(buf, static_cast<std::size_t>(n)));
        const std::string resp = sc.takeResponse();
        if (!resp.empty() && !write_all(master, resp))
            break;
    }
}

std::string run_capture(const std::string& cmd) {
    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        return out;
    std::array<char, 4096> buf{};
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe))
        out += buf.data();
    pclose(pipe);
    return out;
}

std::vector<std::string> split_lines(std::string s) {
    if (!s.empty() && s.back() == '\n')
        s.pop_back();
    std::vector<std::string> lines;
    std::size_t pos = 0;
    for (;;) {
        const std::size_t nl = s.find('\n', pos);
        if (nl == std::string::npos) {
            lines.push_back(s.substr(pos));
            break;
        }
        lines.push_back(s.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return lines;
}

std::string rstrip(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r'))
        s.pop_back();
    return s;
}

// Raw pane frame as tmux reports it.
struct PaneFrame {
    int left = -1, top = -1, width = -1, height = -1;
    bool valid() const { return left >= 0 && top >= 0 && width > 0 && height > 0; }
};

PaneFrame pane_frame(const std::string& tmux_base, const std::string& target) {
    PaneFrame f;
    const std::string out = run_capture(tmux_base +
        " display-message -p -t " + target +
        " '#{pane_left} #{pane_top} #{pane_width} #{pane_height}' 2>/dev/null");
    std::istringstream iss(out);
    iss >> f.left >> f.top >> f.width >> f.height;
    if (!iss)
        f.left = -1;
    return f;
}

// Content geometry derived from the frame plus the capture line count.
// capture-pane emits exactly one line per content row, so
// (frame_height - capture_lines) / 2 is the border thickness; borders are
// symmetric, so the horizontal inset is the same.
struct ContentBox {
    int left = 0, top = 0, width = 0, height = 0;
    int border = 0;
};

ContentBox content_box(const PaneFrame& f, const std::vector<std::string>& capture_lines) {
    ContentBox b;
    b.height = static_cast<int>(capture_lines.size());
    if (b.height > f.height)
        b.height = f.height;
    b.border = (f.height - b.height) / 2;
    b.width = f.width - 2 * b.border;
    b.left = f.left + b.border;
    b.top = f.top + b.border;
    return b;
}

// Prompt marker to look for inside each pane (set from main per shell mode).
std::string g_prompt_marker = "KAI>";

bool compare_stage(const kai::Screen& sc, const std::string& tmux_base,
                   int rows, int cols, const std::string& stage, bool split) {
    bool ok = true;
    const PaneFrame f0 = pane_frame(tmux_base, "kaie2e:0.0");
    PaneFrame f1;
    if (split)
        f1 = pane_frame(tmux_base, "kaie2e:0.1");
    if (!f0.valid() || (split && !f1.valid())) {
        std::cerr << "FAIL[" << stage << "]: could not read pane geometry from tmux\n";
        return false;
    }

    struct Item { PaneFrame f; std::string target; const char* name; };
    std::vector<Item> items{{Item{f0, "kaie2e:0.0", "left"}}};
    if (split)
        items.push_back(Item{f1, "kaie2e:0.1", "right"});

    int border = -1;
    for (const Item& it : items) {
        const std::vector<std::string> cap =
            split_lines(run_capture(tmux_base + " capture-pane -p -t " + it.target));
        const ContentBox b = content_box(it.f, cap);
        if (border < 0)
            border = b.border;

        std::string want;
        for (int i = 0; i < b.height; ++i) {
            std::string line = cap[static_cast<std::size_t>(i)];
            line.resize(static_cast<std::size_t>(
                            std::max<int>(b.width, static_cast<int>(line.size()))), ' ');
            want += rstrip(line) + '\n';
        }
        std::string got;
        for (int i = 0; i < b.height; ++i) {
            std::string line;
            for (int j = 0; j < b.width; ++j) {
                const std::string& t = sc.at(b.top + i, b.left + j).text;
                if (t.empty())
                    line += ' ';
                else
                    line += t;
            }
            got += rstrip(line) + '\n';
        }

        if (want != got) {
            ok = false;
            std::cerr << "FAIL[" << stage << "]: " << it.name << " pane does not match tmux "
                      << "(content box " << b.left << "," << b.top << " " << b.width << "x"
                      << b.height << " border=" << b.border << ")\n";
            const std::vector<std::string> wl = split_lines(want), gl = split_lines(got);
            int shown = 0;
            for (std::size_t i = 0; i < wl.size() && shown < 6; ++i) {
                if (i >= gl.size() || wl[i] != gl[i]) {
                    std::cerr << "  row " << i << "\n    tmux: |" << wl[i] << "|\n    kai : |"
                              << (i < gl.size() ? gl[i] : std::string("<missing>")) << "|\n";
                    ++shown;
                }
            }
        }
        if (want.find(g_prompt_marker) == std::string::npos) {
            ok = false;
            std::cerr << "FAIL[" << stage << "]: " << it.name
                      << " pane capture has no prompt (sanity). First lines:\n";
            for (std::size_t i = 0; i < split_lines(want).size() && i < 5; ++i)
                std::cerr << "    |" << split_lines(want)[i] << "|\n";
        }
    }

    if (split && border >= 0) {
        // Separator: every column between the two content boxes must be
        // drawn on every pane row. Borderless layouts have exactly one
        // separator column; bordered layouts have each pane's border column.
        const int lo = f0.left + f0.width - border;
        const int hi = f1.left + border - 1;
        const int pane_rows = rows - 1 - 2 * border; // exclude status + outer borders
        for (int c = lo; c <= hi; ++c) {
            for (int r = border; r < border + pane_rows && r < rows - 1; ++r) {
                if (c < 0 || c >= cols || sc.at(r, c).text.empty()) {
                    ok = false;
                    std::cerr << "FAIL[" << stage << "]: separator missing at row " << r
                              << " col " << c << "\n";
                    break;
                }
            }
        }
    }
    if (border > 0) {
        // Outer frame only exists in bordered layouts.
        for (int c = 1; c < cols - 1; ++c) {
            if (sc.at(0, c).text.empty()) {
                ok = false;
                std::cerr << "FAIL[" << stage << "]: top border missing at col " << c << "\n";
                break;
            }
        }
        for (int r = 0; r < rows - 1; ++r) {
            if (sc.at(r, 0).text.empty() || sc.at(r, cols - 1).text.empty()) {
                ok = false;
                std::cerr << "FAIL[" << stage << "]: side border missing at row " << r << "\n";
                break;
            }
        }
    }
    // Status line row must be filled.
    if (sc.at(rows - 1, 1).text.empty()) {
        ok = false;
        std::cerr << "FAIL[" << stage << "]: status line missing\n";
    }

    std::cout << (ok ? "ok  " : "BAD ") << stage << " (border=" << border << ")\n";
    return ok;
}

} // namespace

int main() {
    char sock[64];
    std::snprintf(sock, sizeof(sock), "kaie2e-%d", static_cast<int>(getpid()));
    char conf[80];
    std::snprintf(conf, sizeof(conf), "/tmp/kai-e2e-%d.conf", static_cast<int>(getpid()));
    const std::string tmux_base = std::string("tmux -L ") + sock;
    const int rows = 24, cols = 80;

    // KAI_E2E_SHELL=fish exercises the user's real stack (fish inside tmux);
    // the default bash mode is deterministic for CI.
    const char* shell_mode = std::getenv("KAI_E2E_SHELL");
    const bool use_fish = shell_mode && std::string(shell_mode) == "fish";
    g_prompt_marker = use_fish ? "\xe2\x9d\xaf" : "KAI>";

    {
        FILE* cf = fopen(conf, "w");
        if (!cf) {
            std::cerr << "FAIL: cannot write tmux conf\n";
            return 1;
        }
        if (use_fish) {
            fputs("set -g default-command fish\n"
                  "set -g status on\n"
                  "set -s escape-time 10\n",
                  cf);
        } else {
            fputs("set -g default-shell /bin/sh\n"
                  "set -g default-command \"PS1=\'KAI> \' bash --norc --noprofile -i\"\n"
                  "set -g status on\n"
                  "set -s escape-time 10\n",
                  cf);
        }
        fclose(cf);
    }

    run_capture(tmux_base + " kill-server 2>/dev/null");

    struct winsize ws{};
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(cols);
    int master = -1;
    const pid_t child = forkpty(&master, nullptr, nullptr, &ws);
    if (child < 0) {
        std::cerr << "FAIL: forkpty: " << std::strerror(errno) << '\n';
        return 1;
    }
    if (child == 0) {
        setenv("TERM", "xterm-256color", 1);
        setenv("PS1", "KAI> ", 1);
        if (use_fish) {
            execlp("tmux", "tmux", "-L", sock, "-f", conf,
                   "new-session", "-s", "kaie2e", "fish", static_cast<char*>(nullptr));
        }
        execlp("tmux", "tmux", "-L", sock, "-f", conf,
               "new-session", "-s", "kaie2e", "bash", "--norc", "--noprofile", "-i",
               static_cast<char*>(nullptr));
        _exit(127);
    }

    kai::Screen sc(rows, cols);
    bool ok = true;

    drain(master, sc, 4000, 500);
    ok &= compare_stage(sc, tmux_base, rows, cols, "startup", false);

    write_all(master, "\x02" "%"); // C-b % : split vertically (right pane)
    drain(master, sc, 3000, 500);
    ok &= compare_stage(sc, tmux_base, rows, cols, "after split", true);

    // Right pane is active: flood it past the pane height (forces region
    // scrolling plus EL/LF sweeps and IL/DL line redraws), then clear it.
    write_all(master, "seq 1 60\n");
    drain(master, sc, 4000, 500);
    ok &= compare_stage(sc, tmux_base, rows, cols, "right pane output", true);

    write_all(master, "clear\n");
    drain(master, sc, 3000, 500);
    ok &= compare_stage(sc, tmux_base, rows, cols, "right pane clear", true);

    // Move focus to the left pane and repeat.
    write_all(master, "\x02o");
    drain(master, sc, 2000, 400);
    write_all(master, "seq 1 60\n");
    drain(master, sc, 4000, 500);
    ok &= compare_stage(sc, tmux_base, rows, cols, "left pane output", true);

    write_all(master, "clear\n");
    drain(master, sc, 3000, 500);
    ok &= compare_stage(sc, tmux_base, rows, cols, "left pane clear", true);

    // And once more in the right pane while the left pane holds content —
    // the exact pattern from the bug report.
    write_all(master, "echo LEFTMARKER\n");
    drain(master, sc, 2000, 400);
    write_all(master, "\x02o");
    drain(master, sc, 2000, 400);
    write_all(master, "seq 1 60\n");
    drain(master, sc, 4000, 500);
    ok &= compare_stage(sc, tmux_base, rows, cols, "right output with left content", true);
    write_all(master, "clear\n");
    drain(master, sc, 3000, 500);
    ok &= compare_stage(sc, tmux_base, rows, cols, "final clear", true);

    // Resize nudge: tmux re-renders the whole layout; kai must follow.
    {
        struct winsize ws2{};
        ws2.ws_row = static_cast<unsigned short>(rows);
        ws2.ws_col = static_cast<unsigned short>(60);
        ioctl(master, TIOCSWINSZ, &ws2);
        sc.resize(rows, 60);
        drain(master, sc, 3000, 600);
        ok &= compare_stage(sc, tmux_base, rows, 60, "after resize 60 cols", true);
    }

    // Teardown.
    run_capture(tmux_base + " kill-server 2>/dev/null");
    drain(master, sc, 1000, 200);
    close(master);
    int status = 0;
    for (int i = 0; i < 50; ++i) {
        if (waitpid(child, &status, WNOHANG) != 0)
            break;
        usleep(100000);
    }
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    run_capture(tmux_base + " kill-server 2>/dev/null");
    unlink(conf);

    if (ok) {
        std::cout << "PASS: tmux E2E matches tmux ground truth at all stages\n";
        return 0;
    }
    std::cerr << "FAIL: tmux E2E mismatch (see stage dumps above)\n";
    return 1;
}
