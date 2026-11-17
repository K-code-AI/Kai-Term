#pragma once

#include "mouseencode.hpp"

#include <algorithm>
#include <cstddef>
#include <array>
#include <bitset>
#include <cstdint>
#include <cstdio>
#include <string>
#include <optional>
#include <string_view>
#include <vector>

#include "base64.hpp"
#include <utility>
#include <vector>

namespace kai {

struct Color {
    std::uint8_t r = 220;
    std::uint8_t g = 220;
    std::uint8_t b = 220;
};

struct Attr {
    bool bold = false;
    bool dim = false;
    bool italic = false;
    bool underline = false;
    bool blink = false;
    bool reverse = false;
    bool hidden = false;
    bool strike = false;
};

struct Cell {
    std::string text;
    int width = 0;
    Color fg{220, 220, 220};
    Color bg{30, 30, 30};
    Attr attr{};
};

inline bool operator==(const Color& a, const Color& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}
inline bool operator!=(const Color& a, const Color& b) {
    return !(a == b);
}
inline bool operator==(const Attr& a, const Attr& b) {
    return a.bold == b.bold && a.dim == b.dim && a.italic == b.italic &&
           a.underline == b.underline && a.blink == b.blink && a.reverse == b.reverse &&
           a.hidden == b.hidden && a.strike == b.strike;
}
inline bool operator!=(const Attr& a, const Attr& b) {
    return !(a == b);
}

// A screen row: the cells, plus whether this row is the tail of a line the
// terminal broke because it ran out of columns. The flag is what makes reflow
// possible — a row that wrapped belongs to the row above it, a row the
// application ended with a newline does not.
struct Row {
    std::vector<Cell> cells;
    bool wrapped = false;

    Row() = default;
    explicit Row(std::size_t n) : cells(n) {}

    std::size_t size() const { return cells.size(); }
    bool empty() const { return cells.empty(); }
    void resize(std::size_t n) { cells.resize(n); }
    Cell& at(std::size_t i) { return cells.at(i); }
    const Cell& at(std::size_t i) const { return cells.at(i); }
    Cell& operator[](std::size_t i) { return cells[i]; }
    const Cell& operator[](std::size_t i) const { return cells[i]; }
    auto begin() { return cells.begin(); }
    auto end() { return cells.end(); }
    auto begin() const { return cells.begin(); }
    auto end() const { return cells.end(); }
};

class Screen {
public:
    explicit Screen(int initial_rows = 24, int initial_cols = 80)
        : rows_(std::max(1, initial_rows)), cols_(std::max(1, initial_cols)),
          cursor_x_(0), cursor_y_(0), saved_x_(0), saved_y_(0), saved_valid_(false)
    {
        grid_.assign(rows_, Row(cols_));
        scroll_top_ = 0;
        scroll_bottom_ = rows_ - 1;
    }

    int rows() const { return rows_; }
    int cols() const { return cols_; }
    int cursorX() const { return cursor_x_; }
    int cursorY() const { return cursor_y_; }
    bool cursorVisible() const { return cursor_visible_; }

    void moveCursor(int dx, int dy) {
        pending_wrap_ = false;
        cursor_x_ = std::clamp(cursor_x_ + dx, 0, cols_ - 1);
        cursor_y_ = std::clamp(cursor_y_ + dy, 0, rows_ - 1);
    }

    const Row& row(int r) const { return grid_.at(r); }
    const Cell& at(int r, int c) const { return grid_.at(r).at(c); }
    const std::vector<Row>& scrollback() const { return scrollback_; }

    // Scrollback access for the UI. Index 0 is the oldest retained line and the
    // last entry is the line that scrolled off most recently. Rows keep the
    // width they were written at, so callers must tolerate shorter rows.
    int historySize() const { return static_cast<int>(scrollback_.size()); }
    const Row* historyRow(int i) const {
        if (i < 0 || i >= static_cast<int>(scrollback_.size()))
            return nullptr;
        return &scrollback_[static_cast<std::size_t>(i)];
    }

    // True while the alternate screen (vim, less, tmux) is active. Scrolling the
    // primary-screen history is then the application's job, not the terminal's.
    bool inAlternateScreen() const { return alternate_valid_; }

    // DECCKM (ESC[?1h): applications ask for SS3 cursor sequences.
    bool applicationCursorKeys() const { return application_cursor_keys_; }

    // Mouse reporting requested by the application. The modes nest, so drag or
    // any-event tracking still implies press, release and wheel reporting.
    MouseMode mouseMode() const {
        if (mouse_any_event_) return MouseMode::Any;
        if (mouse_drag_) return MouseMode::Drag;
        if (mouse_click_) return MouseMode::Click;
        return MouseMode::Off;
    }
    bool mouseReporting() const { return mouseMode() != MouseMode::Off; }

    // Coordinate encoding to use, most modern request winning.
    MouseCoord mouseCoord() const {
        if (mouse_sgr_) return MouseCoord::Sgr;
        if (mouse_sgr_hex_) return MouseCoord::SgrHex;
        if (mouse_urxvt_) return MouseCoord::Urxvt;
        return MouseCoord::Classic;
    }

    // ?2004: the application wants pasted text fenced by ESC[200~ and ESC[201~.
    bool bracketedPaste() const { return bracketed_paste_; }

    // Terminal-to-host replies produced by queries (DA1, DSR 5n/6n).
    // The consumer (widget or test harness) drains this and writes it to the PTY.
    std::string takeResponse() {
        std::string out;
        out.swap(response_out_);
        return out;
    }

    // What an OSC command asked the terminal program to do. The model cannot set
    // a window title or touch a clipboard, and the tests drive it with no display
    // attached at all, so it records the request and the widget carries it out.
    struct ClipboardRequest {
        std::string text;     // what to store; empty for a read
        bool primary = false; // the selection rather than the clipboard
        bool query = false;   // the application wants the contents, not the reverse
    };

    // The window title, when an OSC 0/2 has changed it since the last call.
    std::optional<std::string> takeTitle() {
        if (!title_dirty_)
            return std::nullopt;
        title_dirty_ = false;
        return title_;
    }

    std::vector<ClipboardRequest> takeClipboardRequests() {
        std::vector<ClipboardRequest> out;
        out.swap(clipboard_requests_);
        return out;
    }

    // Answers an OSC 52 read request. The reply leaves by the same channel as
    // every other terminal-to-host response.
    void answerClipboardQuery(std::string_view text) {
        response_out_ += "\x1b]52;c;" + base64Encode(text) + "\x07";
    }

    const std::string& windowTitle() const { return title_; }

    // The directory an application says it is in, from OSC 7. Empty until one
    // says so, which a shell only does if it is configured to.
    const std::string& workingDirectory() const { return cwd_; }

    // The colours in use. Cells hold the colour they were written with rather
    // than an index into a table, so an OSC 4/10/11 change shows up in what is
    // drawn afterwards and does not repaint what is already on the screen.
    // The pixel size of one cell. The model has no font and no way to measure
    // one, but a geometry query has to be answered in pixels, so the widget says
    // what it is drawing with.
    void setCellPixels(int width, int height) {
        cell_px_w_ = std::max(1, width);
        cell_px_h_ = std::max(1, height);
    }

    Color defaultForeground() const { return default_fg_; }
    Color defaultBackground() const { return default_bg_; }
    Color paletteColor(int index) const {
        if (index >= 0 && index < 256 && palette_custom_[static_cast<std::size_t>(index)])
            return palette_custom_color_[static_cast<std::size_t>(index)];
        return palette(index);
    }

    void resize(int new_rows, int new_cols) {
        new_rows = std::max(1, new_rows);
        new_cols = std::max(1, new_cols);
        if (new_rows == rows_ && new_cols == cols_)
            return;

        if (!alternate_valid_) {
            // The shell screen keeps its history and its text: lines the
            // terminal had broken are re-broken for the new width.
            reflow(scrollback_, grid_, new_rows, new_cols, cursor_y_, cursor_x_);
        } else {
            // An application on the alternate screen redraws itself when the
            // window changes size, so its screen is only re-fitted. The shell
            // screen it is covering is not going to redraw anything, so that one
            // is reflowed; otherwise leaving vim after a resize would show a
            // shell whose history was still cut at the old width.
            const std::vector<Row> old = grid_;
            const int offset = fitGrid(grid_, new_rows, new_cols);
            (void)offset; // the alternate screen has no scrollback to keep
            reflow(alternate_scrollback_, alternate_grid_, new_rows, new_cols,
                   alternate_cursor_y_, alternate_cursor_x_);
            cursor_x_ = std::min(cursor_x_, new_cols - 1);
            cursor_y_ = std::min(cursor_y_, new_rows - 1);
        }
        rows_ = new_rows;
        cols_ = new_cols;
        // A resize invalidates any scroll region; tmux re-establishes its own.
        scroll_top_ = 0;
        scroll_bottom_ = rows_ - 1;
        origin_mode_ = false;
        pending_wrap_ = false;

        // When an alternate-screen application is running, the saved primary
        // screen and its history have to survive the resize: re-fit them instead
        // of discarding them, or leaving vim after a resize would reveal an
        // empty shell with no scrollback.
        if (alternate_valid_) {
            alternate_scroll_top_ = 0;
            alternate_scroll_bottom_ = new_rows - 1;
        }
    }

    // Reflow a screen and its history to a new width. Rows whose wrapped flag is
    // set are joined back onto the row above, giving the lines the application
    // actually wrote, and those lines are then broken again at the new width.
    // The cursor is kept over the same character and, where the geometry allows,
    // on the same row of the window, so a resize does not shove the prompt away.
    static void reflow(std::vector<Row>& history, std::vector<Row>& grid, int new_rows,
                       int new_cols, int& cursor_y, int& cursor_x) {
        std::vector<Row> all;
        all.reserve(history.size() + grid.size());
        for (const Row& r : history)
            all.push_back(r);
        for (const Row& r : grid)
            all.push_back(r);
        if (all.empty()) {
            grid.assign(new_rows, Row(new_cols));
            history.clear();
            cursor_y = 0;
            cursor_x = 0;
            return;
        }

        const int cursor_row =
            std::min(static_cast<int>(all.size()) - 1, static_cast<int>(history.size()) + cursor_y);
        const int cursor_col = std::min(cursor_x, static_cast<int>(all[cursor_row].size()));

        // Join the physical rows into the lines the application wrote. Continuation
        // cells of a double-width glyph are dropped and rebuilt while re-breaking;
        // every other empty cell becomes an ordinary blank so it can be told apart
        // from a continuation. Each row gives up its trailing blanks first: a row
        // that wrapped because a double-width glyph would not fit ends in one, and
        // joining it in would put a space in the middle of the rejoined line.
        // A cell that carries nothing at all: no text, and no colour or attribute
        // of its own. Only these may be trimmed off the end of a row. A space
        // painted with a background is not blank — that is what a status bar is
        // made of, and losing it would shorten the bar by however much of it was
        // unpainted text.
        auto plainBlank = [](const Cell& c) {
            static const Cell blank{};
            return c.text.empty() && c.fg == blank.fg && c.bg == blank.bg &&
                   !(c.attr != blank.attr);
        };

        std::vector<std::vector<Cell>> lines;
        std::vector<int> line_of(all.size());
        int cursor_line = 0;
        int cursor_offset = 0;
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (i == 0 || !all[i].wrapped)
                lines.emplace_back();
            line_of[i] = static_cast<int>(lines.size()) - 1;
            std::vector<Cell>& out = lines.back();
            std::size_t used = all[i].size();
            while (used > 0 && plainBlank(all[i][used - 1]))
                --used;
            for (std::size_t c = 0; c < used; ++c) {
                const Cell& cell = all[i][c];
                const bool continuation =
                    cell.width == 0 && cell.text.empty() && !out.empty() && out.back().width == 2;
                if (continuation)
                    continue;
                if (i == static_cast<std::size_t>(cursor_row) && c < static_cast<std::size_t>(cursor_col))
                    ++cursor_offset;
                Cell flat = cell;
                if (flat.width == 0)
                    flat.width = 1;
                out.push_back(flat);
            }
        }
        cursor_line = line_of[static_cast<std::size_t>(cursor_row)];

        // Break each line again at the new width.
        std::vector<Row> broken;
        std::vector<int> line_first(lines.size());
        std::vector<int> line_count(lines.size());
        for (std::size_t l = 0; l < lines.size(); ++l) {
            std::vector<Cell>& flat = lines[l];
            line_first[l] = static_cast<int>(broken.size());
            int produced = 0;
            int col = 0;
            Row cur(new_cols);
            auto flush = [&]() {
                cur.wrapped = produced > 0;
                broken.push_back(cur);
                cur = Row(new_cols);
                ++produced;
                col = 0;
            };
            for (const Cell& cell : flat) {
                const int w = std::max(1, cell.width);
                if (col + w > new_cols)
                    flush(); // a double-width glyph will not fit in the last column
                cur[col] = cell;
                if (w == 2) {
                    if (col + 1 < new_cols) {
                        Cell cont;
                        cont.width = 0;
                        cont.fg = cell.fg;
                        cont.bg = cell.bg;
                        cur[col + 1] = cont;
                    }
                    col += 2;
                } else {
                    ++col;
                }
                if (col >= new_cols)
                    flush();
            }
            if (col > 0 || produced == 0)
                flush(); // an empty line is still a line
            line_count[l] = produced;
        }

        // Find where the cursor's character landed.
        int cursor_new = line_first[static_cast<std::size_t>(cursor_line)];
        int new_col = 0;
        {
            int remaining = cursor_offset;
            const int first = line_first[static_cast<std::size_t>(cursor_line)];
            const int count = line_count[static_cast<std::size_t>(cursor_line)];
            for (int r = 0; r < count; ++r) {
                const Row& row = broken[static_cast<std::size_t>(first + r)];
                int real = 0;
                int last_real = 0;
                for (std::size_t c = 0; c < row.size(); ++c) {
                    if (row[c].width == 0 && row[c].text.empty())
                        continue; // continuation of a double-width glyph
                    if (real == remaining) {
                        cursor_new = first + r;
                        new_col = static_cast<int>(c);
                        remaining = -1;
                        break;
                    }
                    ++real;
                    last_real = static_cast<int>(c) + 1;
                }
                if (remaining < 0)
                    break;
                remaining -= real;
                if (r == count - 1) { // the cursor sat past the end of the line
                    cursor_new = first + r;
                    new_col = last_real;
                }
            }
        }

        // Split the reflowed rows back into history and screen, keeping the cursor
        // on the row of the window it was on before.
        const int target = std::clamp(cursor_y, 0, new_rows - 1);
        int split = cursor_new - target;
        split = std::clamp(split, 0, std::max(0, static_cast<int>(broken.size()) - new_rows));
        history.assign(broken.begin(), broken.begin() + split);
        if (history.size() > kMaxHistory)
            history.erase(history.begin(), history.begin() + (history.size() - kMaxHistory));
        grid.assign(broken.begin() + split,
                    broken.begin() + std::min(broken.size(), static_cast<std::size_t>(split + new_rows)));
        while (grid.size() < static_cast<std::size_t>(new_rows))
            grid.push_back(Row(new_cols));
        cursor_y = cursor_new - split;
        cursor_x = std::clamp(new_col, 0, new_cols - 1);
    }

    void clear() {
        for (auto& r : grid_) {
            r.wrapped = false;
            for (auto& c : r)
                clearCell(c); // the defaults, which OSC 10/11 may have changed
        }
        cursor_x_ = cursor_y_ = 0;
        saved_valid_ = false;
        pending_wrap_ = false;
        scrollback_.clear();
    }

    void clearCell(Cell& c) {
        c.text.clear();
        c.width = 0;
        c.fg = default_fg_;
        c.bg = default_bg_;
        c.attr = Attr{};
    }

    void eraseLine(int mode) {
        pending_wrap_ = false;
        const int x = std::min(cursor_x_, cols_ - 1);
        if (mode == 0) {
            for (int c = cursor_x_; c < cols_; ++c)
                clearCell(grid_[cursor_y_][c]);
        } else if (mode == 1) {
            for (int c = 0; c <= x; ++c)
                clearCell(grid_[cursor_y_][c]);
        } else {
            for (auto& c : grid_[cursor_y_])
                clearCell(c);
        }
    }

    void eraseDisplay(int mode) {
        pending_wrap_ = false;
        if (mode == 0) {
            for (int r = cursor_y_; r < rows_; ++r) {
                int start = (r == cursor_y_ ? cursor_x_ : 0);
                for (int c = start; c < cols_; ++c)
                    clearCell(grid_[r][c]);
            }
        } else if (mode == 1) {
            const int x = std::min(cursor_x_, cols_ - 1) + 1;
            for (int r = 0; r <= cursor_y_ && r < rows_; ++r) {
                int end = (r == cursor_y_ ? x : cols_);
                for (int c = 0; c < end; ++c)
                    clearCell(grid_[r][c]);
            }
        } else {
            for (auto& r : grid_) {
                r.wrapped = false; // nothing continues into a cleared screen
                for (auto& c : r)
                    clearCell(c);
            }
            if (mode == 3)
                scrollback_.clear();
        }
    }

    void saveCursor() {
        saved_x_ = cursor_x_;
        saved_y_ = cursor_y_;
        saved_valid_ = true;
    }

    void restoreCursor() {
        if (saved_valid_) {
            // A saved one-past column is not restored: the wrap flag that made
            // it legal was not saved with it, and printing into it would be a
            // write past the end of the row.
            cursor_x_ = std::min(saved_x_, cols_ - 1);
            cursor_y_ = std::min(saved_y_, rows_ - 1);
        }
        pending_wrap_ = false;
    }

    void moveTo(int row, int col) {
        if (origin_mode_) {
            row = std::clamp(row, 0, scroll_bottom_ - scroll_top_) + scroll_top_;
            col = std::clamp(col, 0, cols_ - 1);
        } else {
            row = std::clamp(row, 0, rows_ - 1);
            // The last column is `cols_ - 1`. `cols_` is reserved for the
            // one-past position that deferred autowrap uses, and reaching it any
            // other way means the next character would be written out of bounds,
            // so an out-of-range CUP lands on the last column as it does in
            // xterm rather than anywhere at all.
            col = std::clamp(col, 0, cols_ - 1);
        }
        cursor_y_ = row;
        cursor_x_ = col;
        pending_wrap_ = false;
    }

    // Parser state lives in this object so partial escape sequences and
    // incomplete UTF-8 sequences survive across read chunks.
    void feed(std::string_view bytes);

private:
    int rows_;
    int cols_;
    int cursor_x_;
    int cursor_y_;
    int saved_x_;
    int saved_y_;
    bool saved_valid_;
    std::string response_out_;
    int scroll_top_ = 0;
    int scroll_bottom_ = 0;
    bool alternate_valid_ = false;
    std::vector<Row> alternate_grid_;
    std::vector<Row> alternate_scrollback_;
    int alternate_cursor_x_ = 0;
    int alternate_cursor_y_ = 0;
    int alternate_scroll_top_ = 0;
    int alternate_scroll_bottom_ = 0;
    std::vector<Row> grid_;
    std::vector<Row> scrollback_;
    std::string pending_utf8_;
    int mode_ = 0; // 0 normal, 1 after ESC, 2 CSI, 3 OSC, 4 ignore-one,
                   // 5 DCS payload, 6 DCS saw ESC, 7 ESC intermediate, 8 designate G0
    std::string csi_;
    char esc_intermediate_ = 0;
    bool osc_esc_pending_ = false;
    // An OSC payload with no terminator would otherwise grow without limit, so
    // one over the cap is dropped rather than stored.
    static constexpr std::size_t kMaxOscLength = 65536;
    std::string osc_;
    bool osc_truncated_ = false;
    std::string title_;
    bool title_dirty_ = false;
    std::string cwd_;
    std::vector<ClipboardRequest> clipboard_requests_;
    std::array<Color, 256> palette_custom_color_{};
    std::bitset<256> palette_custom_{};
    Color default_fg_{220, 220, 220};
    Color default_bg_{30, 30, 30};
    int cell_px_w_ = 8;
    int cell_px_h_ = 16;
    // xterm keeps ten; more is a program that forgot to pop, and the oldest
    // title in a stack that deep is not worth remembering.
    static constexpr std::size_t kTitleStackDepth = 10;
    std::vector<std::string> title_stack_;
    Color current_fg_{220, 220, 220};
    Color current_bg_{30, 30, 30};
    Attr current_attr_{};
    bool cursor_visible_ = true;
    bool application_cursor_keys_ = false;
    bool mouse_click_ = false;
    bool mouse_drag_ = false;
    bool mouse_any_event_ = false;
    bool mouse_urxvt_ = false;
    bool mouse_sgr_ = false;
    bool mouse_sgr_hex_ = false;
    bool bracketed_paste_ = false;
    bool autowrap_ = true;
    bool pending_wrap_ = false;
    bool origin_mode_ = false;
    int g0_charset_ = 0; // 0 ASCII, 1 DEC Special Graphics
    int g1_charset_ = 0;
    bool gl_is_g1_ = false;

    Row blankRow() const {
        return Row(cols_);
    }

    // Scrollback depth. Bounded so a runaway `yes` in a loop cannot exhaust
    // memory while still covering a long build log.
    static constexpr std::size_t kMaxHistory = 1000;

    static void pushHistory(std::vector<Row>& history, Row row) {
        history.push_back(std::move(row));
        if (history.size() > kMaxHistory)
            history.erase(history.begin());
    }

    // Re-fit a grid to a new size, keeping the bottom-most rows when shrinking.
    // Returns how many rows were dropped off the top.
    static int fitGrid(std::vector<Row>& grid, int new_rows, int new_cols) {
        const int old_rows = static_cast<int>(grid.size());
        const int offset = std::max(0, old_rows - new_rows);
        std::vector<Row> next(static_cast<std::size_t>(new_rows), Row(new_cols));
        for (int r = 0; r < new_rows; ++r) {
            const int source = offset + r;
            if (source >= old_rows)
                continue;
            const int copy_cols = std::min(new_cols, static_cast<int>(grid[source].size()));
            for (int c = 0; c < copy_cols; ++c)
                next[static_cast<std::size_t>(r)][c] = grid[static_cast<std::size_t>(source)][c];
        }
        grid = std::move(next);
        return offset;
    }

    // Scroll the scroll region up by one line. Content leaving the top of a
    // full-screen region is history and goes to scrollback; content leaving a
    // partial region (tmux panes, status lines) is discarded. The alternate
    // screen has no history at all, which is why vim and less output does not
    // pollute the shell's scrollback.
    void scrollUpInRegion() {
        if (!alternate_valid_ && scroll_top_ == 0 && scroll_bottom_ == rows_ - 1)
            pushHistory(scrollback_, grid_[frontRowIndex()]);
        for (int r = scroll_top_; r < scroll_bottom_; ++r)
            grid_[r] = grid_[r + 1];
        grid_[scroll_bottom_] = blankRow();
    }

    int frontRowIndex() const { return scroll_top_; }

    void scrollDownInRegion() {
        for (int r = scroll_bottom_; r > scroll_top_; --r)
            grid_[r] = grid_[r - 1];
        grid_[scroll_top_] = blankRow();
    }

    // Index (LF without CR semantics): move down one line; when the cursor sits
    // on the bottom margin, scroll the region instead of moving.
    void index() {
        pending_wrap_ = false;
        if (cursor_y_ == scroll_bottom_)
            scrollUpInRegion();
        else if (cursor_y_ < rows_ - 1)
            ++cursor_y_;
    }

    // Reverse index: move up one line; when the cursor sits on the top margin,
    // scroll the region down instead of moving.
    void reverseIndex() {
        pending_wrap_ = false;
        if (cursor_y_ == scroll_top_)
            scrollDownInRegion();
        else if (cursor_y_ > 0)
            --cursor_y_;
    }

    void flushPendingReplacement() {
        if (!pending_utf8_.empty()) {
            putCodepoint(0xFFFD);
            pending_utf8_.clear();
        }
    }

    static int utf8Expected(unsigned char c) {
        if (c < 0x80) return 1;
        if ((c & 0xE0) == 0xC0) return 2;
        if ((c & 0xF0) == 0xE0) return 3;
        if ((c & 0xF8) == 0xF0) return 4;
        return 0;
    }

    static bool validPrefix(const std::string& s) {
        if (s.empty()) return true;
        int len = utf8Expected(static_cast<unsigned char>(s[0]));
        if (len == 0 || static_cast<int>(s.size()) > len) return false;
        for (std::size_t i = 1; i < s.size(); ++i) {
            if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
                return false;
        }
        return true;
    }

    static std::uint32_t decodeUtf8(const std::string& s) {
        if (s.empty()) return 0xFFFD;
        std::uint32_t cp;
        if (s.size() == 2) cp = s[0] & 0x1F;
        else if (s.size() == 3) cp = s[0] & 0x0F;
        else if (s.size() == 4) cp = s[0] & 0x07;
        else return 0xFFFD;
        for (std::size_t i = 1; i < s.size(); ++i)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        return cp;
    }

    static std::string encodeUtf8(std::uint32_t cp) {
        std::string out;
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        return out;
    }

    // DEC Special Graphics (VT220 line-drawing set). ASCII-range bytes that
    // pass through this charset map to box-drawing glyphs.
    static std::uint32_t decSpecialGraphics(std::uint32_t cp) {
        static const std::uint32_t table['~' - '`' + 1] = {
            0x25C6, // ` diamond
            0x2592, // a median strip shading
            0x2409, // b symbol for HT
            0x240C, // c symbol for FF
            0x240D, // d symbol for CR
            0x240A, // e symbol for LF
            0x00B0, // f degree
            0x00B1, // g plus-minus
            0x2424, // h symbol for NL
            0x240B, // i symbol for VT
            0x2518, // j lower right corner
            0x2510, // k upper right corner
            0x250C, // l upper left corner
            0x2514, // m lower left corner
            0x253C, // n crossing lines
            0x23BA, // o horizontal scan 1
            0x23BB, // p horizontal scan 3
            0x2500, // q horizontal line
            0x23BC, // r horizontal scan 7
            0x23BD, // s horizontal scan 9
            0x251C, // t left tee
            0x2524, // u right tee
            0x2534, // v bottom tee
            0x252C, // w top tee
            0x2502, // x vertical line
            0x2264, // y less-or-equal
            0x2265, // z greater-or-equal
            0x03C0, // { pi
            0x2260, // | not-equal
            0x00A3, // } pound
            0x00B7  // ~ middle dot
        };
        if (cp >= '`' && cp <= '~')
            return table[cp - '`'];
        return cp;
    }

    std::uint32_t translateCharset(std::uint32_t cp) const {
        const int cs = gl_is_g1_ ? g1_charset_ : g0_charset_;
        if (cs == 1 && cp < 0x80)
            return decSpecialGraphics(cp);
        return cp;
    }

    static int codepointWidth(std::uint32_t cp) {
        // Basic width classification. Zero-width combining marks attach to the
        // preceding cell; CJK/emoji ranges occupy two cells.
        if (cp < 0x20 || (cp >= 0x7F && cp < 0x80)) return 0;
        if (cp >= 0x1100 && cp <= 0x11FF) return 1;
        if ((cp >= 0x2E80 && cp <= 0x9FFF) ||
            (cp >= 0xA000 && cp <= 0xAFFF) ||
            (cp >= 0xF900 && cp <= 0xFAFF) ||
            (cp >= 0x1F000 && cp <= 0x1FFFD) ||
            (cp >= 0x20000 && cp <= 0x2FFFD) ||
            (cp >= 0x30000 && cp <= 0x3FFFD))
            return 2;
        if ((cp >= 0x300 && cp <= 0x36F) ||
            (cp >= 0x1AB0 && cp <= 0x1AFF) ||
            (cp >= 0x1DC0 && cp <= 0x1DFF) ||
            (cp >= 0x20D0 && cp <= 0x20FF) ||
            (cp >= 0xFE00 && cp <= 0xFE0F) ||
            (cp >= 0xFE20 && cp <= 0xFE2F))
            return 0;
        return 1;
    }

    void putCodepoint(std::uint32_t cp) {
        cp = translateCharset(cp);
        const int w0 = codepointWidth(cp);
        if (w0 == 0) {
            int x = cursor_x_ - 1;
            while (x >= 0 && (grid_[cursor_y_][x].text.empty() || grid_[cursor_y_][x].width == 0))
                --x;
            if (x >= 0)
                grid_[cursor_y_][x].text += encodeUtf8(cp);
            return;
        }

        // Deferred autowrap: the cursor sits one past the last column and the
        // next printable character triggers the wrap.
        if (pending_wrap_) {
            if (autowrap_) {
                cursor_x_ = 0;
                index();
                grid_[cursor_y_].wrapped = true;
            } else {
                cursor_x_ = cols_ - 1; // wrap was disabled while pending
            }
            pending_wrap_ = false;
        }

        int w = w0;
        if (w == 2 && cursor_x_ + 2 > cols_) {
            if (autowrap_ && cursor_x_ != 0) {
                cursor_x_ = 0;
                index();
                grid_[cursor_y_].wrapped = true;
            } else {
                w = 1; // no room for a double-width cell; clip
            }
        }

        if (cursor_y_ < 0 || cursor_y_ >= rows_)
            return;
        // Reaching the one-past column without pending_wrap_ set would be a
        // write past the end of the row, so it is refused here as well as at the
        // places that set the cursor. Whatever an escape sequence claims, the
        // screen never indexes outside its own grid.
        if (cursor_x_ < 0)
            cursor_x_ = 0;
        if (cursor_x_ >= cols_)
            cursor_x_ = cols_ - 1;

        Cell& c = grid_[cursor_y_][cursor_x_];
        c.text = encodeUtf8(cp);
        c.width = w;
        c.fg = current_fg_;
        c.bg = current_bg_;
        c.attr = current_attr_;

        if (w == 2 && cursor_x_ + 1 < cols_) {
            Cell& cont = grid_[cursor_y_][cursor_x_ + 1];
            cont.text.clear();
            cont.width = 0;
            cont.fg = current_fg_;
            cont.bg = current_bg_;
            cont.attr = Attr{};
        }

        cursor_x_ += w;
        if (cursor_x_ >= cols_) {
            if (autowrap_) {
                cursor_x_ = cols_; // one-past representation of the wrap flag
                pending_wrap_ = true;
            } else {
                cursor_x_ = cols_ - 1;
            }
        }
    }

    void newLine() {
        index();
        cursor_x_ = 0;
    }

    void carriageReturn() {
        pending_wrap_ = false;
        cursor_x_ = 0;
    }

    void insertLines(int lines) {
        if (cursor_y_ < scroll_top_ || cursor_y_ > scroll_bottom_)
            return;
        pending_wrap_ = false;
        lines = std::min(lines, scroll_bottom_ - cursor_y_ + 1);
        for (int r = scroll_bottom_; r >= cursor_y_ + lines; --r)
            grid_[r] = grid_[r - lines];
        for (int r = cursor_y_; r < cursor_y_ + lines; ++r)
            grid_[r] = blankRow();
    }

    void deleteLines(int lines) {
        if (cursor_y_ < scroll_top_ || cursor_y_ > scroll_bottom_)
            return;
        pending_wrap_ = false;
        lines = std::min(lines, scroll_bottom_ - cursor_y_ + 1);
        for (int r = cursor_y_; r <= scroll_bottom_ - lines; ++r)
            grid_[r] = grid_[r + lines];
        for (int r = scroll_bottom_ - lines + 1; r <= scroll_bottom_; ++r)
            grid_[r] = blankRow();
    }

    void insertChars(int n) {
        pending_wrap_ = false;
        if (cursor_x_ >= cols_)
            return;
        n = std::min(n, cols_ - cursor_x_);
        Row& line = grid_[cursor_y_];
        for (int c = cols_ - 1; c >= cursor_x_ + n; --c)
            line[c] = line[c - n];
        for (int c = cursor_x_; c < cursor_x_ + n; ++c)
            clearCell(line[c]);
    }

    void deleteChars(int n) {
        pending_wrap_ = false;
        if (cursor_x_ >= cols_)
            return;
        n = std::min(n, cols_ - cursor_x_);
        Row& line = grid_[cursor_y_];
        for (int c = cursor_x_; c + n < cols_; ++c)
            line[c] = line[c + n];
        for (int c = std::max(cols_ - n, cursor_x_); c < cols_; ++c)
            clearCell(line[c]);
    }

    void eraseChars(int n) {
        pending_wrap_ = false;
        Row& line = grid_[cursor_y_];
        for (int c = cursor_x_; c < std::min(cols_, cursor_x_ + n); ++c)
            clearCell(line[c]);
    }

    static int parseNumber(std::string_view s) {
        int v = 0;
        for (char c : s) {
            if (c < '0' || c > '9')
                return 0;
            if (v > 1000000)
                return 1000000;
            v = v * 10 + (c - '0');
        }
        return v;
    }

    // One SGR parameter. Colons introduce sub-parameters, so `38:5:196` is a
    // single parameter carrying a whole colour. An empty sub-parameter means
    // "not given" rather than zero, which matters because `38:2::r:g:b` leaves
    // the colour-space identifier empty on purpose.
    struct SgrParam {
        std::vector<int> parts;
        int value() const { return parts.empty() ? 0 : parts[0]; }
        int part(std::size_t index) const {
            return index < parts.size() ? parts[index] : -1;
        }
    };

    static std::vector<SgrParam> parseSgr(const std::string& params) {
        // A view over the caller's string, deliberately: substr on a std::string
        // returns a temporary, and holding a view of that across the loop is a
        // dangling reference that only shows up once the optimiser reuses the
        // storage — which is exactly how it first appeared, at -O3.
        const std::string_view all = params;
        std::vector<SgrParam> out;
        std::size_t i = 0;
        for (;;) {
            const std::size_t semi = all.find(';', i);
            const std::string_view field =
                all.substr(i, semi == std::string::npos ? std::string::npos : semi - i);
            SgrParam param;
            std::size_t j = 0;
            for (;;) {
                const std::size_t colon = field.find(':', j);
                const std::string_view part = field.substr(
                    j, colon == std::string_view::npos ? std::string_view::npos : colon - j);
                if (part.empty())
                    param.parts.push_back(param.parts.empty() ? 0 : -1);
                else
                    param.parts.push_back(parseNumber(part));
                if (colon == std::string_view::npos)
                    break;
                j = colon + 1;
            }
            out.push_back(std::move(param));
            if (semi == std::string::npos)
                break;
            i = semi + 1;
        }
        return out;
    }

    static std::uint8_t clamp255(int v) {
        return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
    }

    // A palette or truecolour colour, spelled either way: `38;5;196` and
    // `38:5:196`, `38;2;r;g;b` and `38:2::r:g:b`. Different programs pick
    // different spellings, and one that was not accepted would render in the
    // default colour with nothing to say about why. Returns how many following
    // parameters the semicolon form used up. A specification this terminal cannot
    // honour — an index outside the palette, say — leaves the colour as it was
    // rather than inventing a substitute.
    int applyExtendedColour(const std::vector<SgrParam>& p, std::size_t k, bool foreground) {
        const SgrParam& here = p[k];
        Color& slot = foreground ? current_fg_ : current_bg_;

        if (here.parts.size() > 1) { // the colon form carries it all in one field
            const int kind = here.part(1);
            if (kind == 5) {
                const int index = here.part(2);
                if (index >= 0 && index < 256)
                    slot = paletteColor(index);
            } else if (kind == 2) {
                const int off = here.parts.size() >= 6 ? 3 : 2; // colour space id or not
                if (static_cast<int>(here.parts.size()) >= off + 3)
                    slot = Color{clamp255(here.part(off)), clamp255(here.part(off + 1)),
                                 clamp255(here.part(off + 2))};
            }
            return 0;
        }

        if (k + 1 >= p.size())
            return 0; // nothing follows: there is no colour here to apply
        const int kind = p[k + 1].value();
        if (kind == 5) {
            if (k + 2 < p.size()) {
                const int index = p[k + 2].value();
                if (index >= 0 && index < 256)
                    slot = paletteColor(index);
            }
            return static_cast<int>(p.size()) - static_cast<int>(k) - 1;
        }
        if (kind == 2) {
            if (k + 4 < p.size())
                slot = Color{clamp255(p[k + 2].value()), clamp255(p[k + 3].value()),
                             clamp255(p[k + 4].value())};
            return static_cast<int>(p.size()) - static_cast<int>(k) - 1;
        }
        return 0;
    }

    void handleSgr(const std::string& params) {
        // Private SGR like ESC[>4;2m (modifyOtherKeys) must not be parsed as
        // ordinary color/attribute parameters.
        if (!params.empty() && !(params[0] >= '0' && params[0] <= '9') && params[0] != ';')
            return;
        const std::vector<SgrParam> p = parseSgr(params);

        for (std::size_t k = 0; k < p.size(); ++k) {
            const int v = p[k].value();
            if (v == 0) {
                current_fg_ = default_fg_;
                current_bg_ = default_bg_;
                current_attr_ = Attr{};
            } else if (v == 1) {
                current_attr_.bold = true;
            } else if (v == 2) {
                current_attr_.dim = true;
                current_attr_.bold = false;
            } else if (v == 3) {
                current_attr_.italic = true;
            } else if (v == 4) {
                current_attr_.underline = true;
            } else if (v == 5 || v == 6) {
                // 6 is rapid blink. Nothing animates either of them yet, and a
                // cell that blinks slowly is closer to a cell that blinks fast
                // than a plain one is.
                current_attr_.blink = true;
            } else if (v == 7) {
                current_attr_.reverse = true;
            } else if (v == 8) {
                current_attr_.hidden = true;
            } else if (v == 9) {
                current_attr_.strike = true;
            } else if (v == 21) {
                // "not bold", which is what scripts ending a bold run with 21
                // expect. xterm draws a double underline for it instead, and
                // there is no double underline to draw here yet.
                current_attr_.bold = false;
            } else if (v == 22) {
                current_attr_.bold = false;
                current_attr_.dim = false;
            } else if (v == 23) {
                current_attr_.italic = false;
            } else if (v == 24) {
                current_attr_.underline = false;
            } else if (v == 25) {
                current_attr_.blink = false;
            } else if (v == 27) {
                current_attr_.reverse = false;
            } else if (v == 28) {
                current_attr_.hidden = false;
            } else if (v == 29) {
                current_attr_.strike = false;
            } else if (v == 39) {
                current_fg_ = default_fg_;
            } else if (v == 49) {
                current_bg_ = default_bg_;
            } else if (v >= 30 && v <= 37) {
                current_fg_ = paletteColor(v - 30);
            } else if (v >= 40 && v <= 47) {
                current_bg_ = paletteColor(v - 40);
            } else if (v >= 90 && v <= 97) {
                current_fg_ = paletteColor(v - 90 + 8);
            } else if (v >= 100 && v <= 107) {
                current_bg_ = paletteColor(v - 100 + 8);
            } else if (v == 38) {
                k += static_cast<std::size_t>(applyExtendedColour(p, k, true));
            } else if (v == 48) {
                k += static_cast<std::size_t>(applyExtendedColour(p, k, false));
            }
        }
    }

    void enterAlternateScreen() {
        if (alternate_valid_)
            return;
        alternate_grid_ = grid_;
        alternate_scrollback_ = scrollback_;
        alternate_cursor_x_ = cursor_x_;
        alternate_cursor_y_ = cursor_y_;
        alternate_scroll_top_ = scroll_top_;
        alternate_scroll_bottom_ = scroll_bottom_;
        alternate_valid_ = true;

        grid_.assign(rows_, Row(cols_));
        scroll_top_ = 0;
        scroll_bottom_ = rows_ - 1;
        scrollback_.clear();
        cursor_x_ = 0;
        cursor_y_ = 0;
        pending_wrap_ = false;
        origin_mode_ = false;
        saved_valid_ = false;
    }

    void leaveAlternateScreen() {
        if (!alternate_valid_)
            return;
        grid_ = std::move(alternate_grid_);
        scrollback_ = std::move(alternate_scrollback_);
        cursor_x_ = std::min(alternate_cursor_x_, cols_ - 1);
        cursor_y_ = std::min(alternate_cursor_y_, rows_ - 1);
        scroll_top_ = std::min(alternate_scroll_top_, rows_ - 1);
        scroll_bottom_ = std::min(alternate_scroll_bottom_, rows_ - 1);
        if (scroll_top_ > scroll_bottom_) {
            scroll_top_ = 0;
            scroll_bottom_ = rows_ - 1;
        }
        alternate_valid_ = false;
        alternate_grid_.clear();
        alternate_scrollback_.clear();
        pending_wrap_ = false;
        origin_mode_ = false;
    }

    // DECSTBM: per xterm, setting the region homes the cursor and resets
    // origin-relative state. tmux relies on both.
    void setScrollRegion(int top, int bottom) {
        if (top <= 0 && bottom >= rows_ - 1) {
            scroll_top_ = 0;
            scroll_bottom_ = rows_ - 1;
        } else {
            top = std::clamp(top, 0, rows_ - 1);
            bottom = std::clamp(bottom, 0, rows_ - 1);
            if (top >= bottom) {
                scroll_top_ = 0;
                scroll_bottom_ = rows_ - 1;
            } else {
                scroll_top_ = top;
                scroll_bottom_ = bottom;
            }
        }
        cursor_x_ = 0;
        cursor_y_ = origin_mode_ ? scroll_top_ : 0;
        pending_wrap_ = false;
    }

    void resetEverything() {
        clear();
        scroll_top_ = 0;
        scroll_bottom_ = rows_ - 1;
        current_fg_ = Color{220, 220, 220};
        current_bg_ = Color{30, 30, 30};
        current_attr_ = Attr{};
        cursor_visible_ = true;
        application_cursor_keys_ = false;
        mouse_click_ = false;
        mouse_drag_ = false;
        mouse_any_event_ = false;
        mouse_urxvt_ = false;
        mouse_sgr_ = false;
        mouse_sgr_hex_ = false;
        bracketed_paste_ = false;
        autowrap_ = true;
        pending_wrap_ = false;
        origin_mode_ = false;
        g0_charset_ = 0;
        g1_charset_ = 0;
        gl_is_g1_ = false;
    }

    void alignScreenDecALN() {
        Cell probe;
        probe.text = "E";
        probe.width = 1;
        probe.fg = current_fg_;
        probe.bg = current_bg_;
        probe.attr = current_attr_;
        for (auto& r : grid_)
            std::fill(r.begin(), r.end(), probe);
        cursor_x_ = 0;
        cursor_y_ = origin_mode_ ? scroll_top_ : 0;
        pending_wrap_ = false;
    }

    void scrollContentUp(int lines) {
        if (lines <= 0)
            return;
        const int top = scroll_top_;
        const int bottom = scroll_bottom_;
        const int count = bottom - top + 1;
        if (lines > count)
            lines = count;

        // What a sweep of SU pushes off the top is the same text a newline would
        // have pushed off, and belongs in the scrollback just the same. The rule
        // is index()'s: a whole-screen region only, and never on the alternate
        // screen, where the application owns what scrolling means.
        if (!alternate_valid_ && top == 0 && bottom == rows_ - 1) {
            for (int r = 0; r < lines; ++r)
                pushHistory(scrollback_, grid_[r]);
        }

        for (int r = top; r <= bottom - lines; ++r)
            grid_[r] = grid_[r + lines];
        for (int r = bottom - lines + 1; r <= bottom; ++r)
            grid_[r] = blankRow();

        if (cursor_y_ >= top && cursor_y_ <= bottom)
            cursor_y_ = std::clamp(cursor_y_ - lines, top, bottom);
    }

    void scrollContentDown(int lines) {
        if (lines <= 0)
            return;
        const int top = scroll_top_;
        const int bottom = scroll_bottom_;
        const int count = bottom - top + 1;
        if (lines > count)
            lines = count;

        for (int r = bottom; r >= top + lines; --r)
            grid_[r] = grid_[r - lines];
        for (int r = top; r < top + lines; ++r)
            grid_[r] = blankRow();

        if (cursor_y_ >= top && cursor_y_ <= bottom)
            cursor_y_ = std::clamp(cursor_y_ + lines, top, bottom);
    }

    static Color palette(int index) {
        static const Color base[16] = {
            {0, 0, 0}, {128, 0, 0}, {0, 128, 0}, {128, 128, 0},
            {0, 0, 128}, {128, 0, 128}, {0, 128, 128}, {192, 192, 192},
            {128, 128, 128}, {255, 0, 0}, {0, 255, 0}, {255, 255, 0},
            {0, 0, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255}
        };
        if (index < 0) index = 0;
        if (index < 16) return base[index];
        if (index < 232) {
            int n = index - 16;
            int r = n / 36;
            int g = (n / 6) % 6;
            int b = n % 6;
            static const int level[6] = {0, 95, 135, 175, 215, 255};
            return Color{
                static_cast<std::uint8_t>(level[r]),
                static_cast<std::uint8_t>(level[g]),
                static_cast<std::uint8_t>(level[b])
            };
        }
        int g = 8 + (index - 232) * 10;
        return Color{
            static_cast<std::uint8_t>(g),
            static_cast<std::uint8_t>(g),
            static_cast<std::uint8_t>(g)
        };
    }

    void handleMode(const std::string& params, bool set) {
        bool priv = !params.empty() && (params[0] == '?' || params[0] == '>' ||
                                        params[0] == '=' || params[0] == '!');
        std::string body = priv ? params.substr(1) : params;
        std::vector<int> nums;
        std::size_t i = 0;
        while (i < body.size()) {
            std::size_t j = i;
            while (j < body.size() && body[j] != ';') ++j;
            nums.push_back(j == i ? 0 : parseNumber(body.substr(i, j - i)));
            i = (j < body.size() ? j + 1 : j);
        }
        if (nums.empty())
            nums.push_back(0);
        for (int v : nums) {
            if (!priv)
                continue; // non-private modes (e.g. ANSI insert/replace) are ignored
            switch (v) {
                case 1: application_cursor_keys_ = set; break;
                case 6: origin_mode_ = set; break;
                case 7: autowrap_ = set; break;
                case 25: cursor_visible_ = set; break;
                case 1048:
                    if (set) saveCursor(); else restoreCursor();
                    break;
                case 47:
                case 1047:
                case 1049:
                    if (set) enterAlternateScreen(); else leaveAlternateScreen();
                    break;
                case 1000: mouse_click_ = set; break;
                case 1002: mouse_drag_ = set; break;
                case 1003: mouse_any_event_ = set; break;
                case 1006: mouse_sgr_ = set; break;
                case 1015: mouse_urxvt_ = set; break;
                case 1016: mouse_sgr_hex_ = set; break;
                case 2004: bracketed_paste_ = set; break;
                default:
                    // ?12, ?128, ?2026 and the rest are accepted and ignored;
                    // features the terminal does not implement must still not
                    // disturb the application's byte stream.
                    break;
            }
        }
    }

    static int hexDigit(char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    static int parseInt(std::string_view text) {
        if (text.empty())
            return -1;
        int v = 0;
        for (const char c : text) {
            if (c < '0' || c > '9')
                return -1;
            v = v * 10 + (c - '0');
            if (v > (1 << 20))
                return -1;
        }
        return v;
    }

    // OSC is what an application says to the terminal itself rather than about
    // its own text: set my title, copy this, here is the directory I am in.
    // Anything unrecognised is consumed and forgotten, which is what a terminal
    // is for — an application cannot handle a refusal, and an unfamiliar sequence
    // must never end up drawn as characters.
    void processOsc(std::string_view osc) {
        const std::size_t semi = osc.find(';');
        const std::string_view code =
            semi == std::string_view::npos ? osc : osc.substr(0, semi);
        const std::string_view data =
            semi == std::string_view::npos ? std::string_view{} : osc.substr(semi + 1);

        if (code == "0" || code == "2") { // icon and window title, window title
            title_ = std::string(data);
            title_dirty_ = true;
            return;
        }
        if (code == "110") { // the shell prompt asking for the default title back
            title_.clear();
            title_dirty_ = true;
            return;
        }
        if (code == "1" || code == "111") // an icon this terminal does not have
            return;
        if (code == "52") {
            oscClipboard(data);
            return;
        }
        if (code == "7") {
            oscWorkingDirectory(data);
            return;
        }
        if (code == "10") {
            oscColour(data, true);
            return;
        }
        if (code == "11") {
            oscColour(data, false);
            return;
        }
        if (code == "4") {
            oscPalette(data);
            return;
        }
        if (code == "104") {
            resetPalette(data);
            return;
        }
        // OSC 8 (a hyperlink around the text that follows) is consumed here, so
        // the text itself survives; the URI has nowhere to go until links can be
        // followed.
    }

    // OSC 52 ; <targets> ; <base64>. The target letters name which of the
    // clipboard, the selection and the eight cut buffers to write. This terminal
    // has the first two; a letter it has never heard of still means "copy", and
    // no payload at all means the application wants to read instead.
    void oscClipboard(std::string_view data) {
        const std::size_t semi = data.find(';');
        const std::string_view targets =
            semi == std::string_view::npos ? data : data.substr(0, semi);
        const std::string_view payload =
            semi == std::string_view::npos ? std::string_view{} : data.substr(semi + 1);

        bool clipboard = false;
        bool primary = false;
        for (const char c : targets) {
            if (c == 'c' || c == '+' || (c >= '0' && c <= '7'))
                clipboard = true;
            else if (c == 'p' || c == 's' || c == 'q')
                primary = true;
        }
        if (!clipboard && !primary)
            clipboard = true;

        ClipboardRequest request;
        request.primary = primary && !clipboard;
        if (payload.empty()) {
            request.query = true;
        } else {
            request.text = base64Decode(payload);
        }
        clipboard_requests_.push_back(std::move(request));
    }

    // OSC 7 ; file://host/directory. Only the path is kept. The host in it is
    // whatever machine the shell is on, which during an ssh session is not this
    // one, and the terminal has no use for either.
    void oscWorkingDirectory(std::string_view data) {
        constexpr std::string_view scheme = "file://";
        std::string_view path = data;
        if (path.substr(0, scheme.size()) == scheme) {
            path = path.substr(scheme.size());
            const std::size_t slash = path.find('/');
            if (slash == std::string_view::npos)
                path = std::string_view{};
            else
                path = path.substr(slash);
        }
        std::string out;
        out.reserve(path.size());
        for (std::size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '%' && i + 2 < path.size()) {
                const int hi = hexDigit(path[i + 1]);
                const int lo = hexDigit(path[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    out.push_back(static_cast<char>(hi * 16 + lo));
                    i += 2;
                    continue;
                }
            }
            out.push_back(path[i]);
        }
        cwd_ = std::move(out);
    }

    // "rgb:r/g/b" with one to four hex digits per channel, which is both what a
    // terminal reports and what it accepts. Fewer digits are repeated out to
    // sixteen bits, so "rgb:f/0/0" is full red rather than nearly black.
    bool parseColourSpec(std::string_view spec, Color& out) {
        constexpr std::string_view prefix = "rgb:";
        if (spec.substr(0, prefix.size()) == prefix)
            spec = spec.substr(prefix.size());
        static const int kFull[5] = {1, 15, 255, 4095, 65535};
        int channel[3] = {0, 0, 0};
        std::size_t part = 0;
        std::size_t i = 0;
        while (part < 3) {
            const std::size_t slash = spec.find('/', i);
            const std::string_view field = spec.substr(
                i, slash == std::string_view::npos ? std::string_view::npos : slash - i);
            if (field.empty() || field.size() > 4)
                return false;
            int v = 0;
            for (const char c : field) {
                const int d = hexDigit(c);
                if (d < 0)
                    return false;
                v = v * 16 + d;
            }
            channel[part++] = v * 255 / kFull[field.size()];
            if (slash == std::string_view::npos)
                break;
            i = slash + 1;
        }
        if (part < 3)
            return false;
        out = Color{static_cast<std::uint8_t>(channel[0]), static_cast<std::uint8_t>(channel[1]),
                    static_cast<std::uint8_t>(channel[2])};
        return true;
    }

    static std::string colourSpec(Color c) {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "rgb:%04x/%04x/%04x", c.r * 257, c.g * 257, c.b * 257);
        return buf;
    }

    // OSC 10 and 11 are the application's default text and background colours:
    // "?" asks what they are, a colour sets them, and an empty parameter hands
    // back the terminal's own.
    void oscColour(std::string_view data, bool foreground) {
        const std::size_t semi = data.find(';');
        const std::string_view spec =
            semi == std::string_view::npos ? data : data.substr(0, semi);
        Color& slot = foreground ? default_fg_ : default_bg_;
        if (spec == "?") {
            response_out_ += "\x1b]" + std::string(foreground ? "10;" : "11;") + colourSpec(slot) +
                             "\x07";
            return;
        }
        if (spec.empty()) {
            slot = foreground ? Color{220, 220, 220} : Color{30, 30, 30};
            return;
        }
        Color parsed{};
        if (parseColourSpec(spec, parsed))
            slot = parsed;
    }

    // OSC 4 ; index ; colour [; index ; colour]... and the same shape with "?" in
    // place of a colour to ask what an index holds.
    void oscPalette(std::string_view data) {
        std::size_t i = 0;
        while (i < data.size()) {
            const std::size_t at = data.find(';', i);
            if (at == std::string_view::npos)
                break; // an index with no colour after it
            const std::string_view index_text = data.substr(i, at - i);
            const std::size_t next = data.find(';', at + 1);
            const std::string_view spec = data.substr(
                at + 1, next == std::string_view::npos ? std::string_view::npos : next - at - 1);
            const int index = parseInt(index_text);
            if (index >= 0 && index < 256) {
                if (spec == "?") {
                    response_out_ +=
                        "\x1b]4;" + std::to_string(index) + ";" + colourSpec(paletteColor(index)) +
                        "\x07";
                } else {
                    Color parsed{};
                    if (parseColourSpec(spec, parsed)) {
                        palette_custom_color_[static_cast<std::size_t>(index)] = parsed;
                        palette_custom_.set(static_cast<std::size_t>(index));
                    }
                }
            }
            if (next == std::string_view::npos)
                break;
            i = next + 1;
        }
    }

    // OSC 104 hands palette entries back, with no parameter meaning all of them.
    void resetPalette(std::string_view data) {
        if (data.empty() || data == "?") {
            palette_custom_.reset();
            return;
        }
        const std::size_t semi = data.find(';');
        const int index = parseInt(data.substr(0, semi));
        if (index >= 0 && index < 256)
            palette_custom_.reset(static_cast<std::size_t>(index));
    }

    void processCsi(const std::string& params, char final) {
        std::vector<int> p;
        std::size_t i = 0;
        while (i < params.size()) {
            std::size_t j = i;
            while (j < params.size() && params[j] != ';') ++j;
            if (j == i)
                p.push_back(1);
            else
                p.push_back(parseNumber(params.substr(i, j - i)));
            i = (j < params.size() ? j + 1 : j);
        }

        auto get = [&](std::size_t idx, int fallback) {
            if (idx < p.size() && p[idx] > 0) return p[idx];
            return fallback;
        };

        const bool plain_csi = params.empty() || (params[0] >= '0' && params[0] <= '9');

        if (final == 'c' && (params.empty() || params == "0"))
            response_out_ += "\x1b[?1;2c"; // VT102 with AVO; accepted by fish and tmux

        switch (final) {
            case 'H':
            case 'f':
                moveTo(get(0, 1) - 1, get(1, 1) - 1);
                break;
            case 'A':
                pending_wrap_ = false;
                cursor_y_ = std::max(0, cursor_y_ - get(0, 1));
                break;
            case 'B':
                pending_wrap_ = false;
                cursor_y_ = std::min(rows_ - 1, cursor_y_ + get(0, 1));
                break;
            case 'C':
            case 'a':
                pending_wrap_ = false;
                cursor_x_ = std::min(cols_ - 1, cursor_x_ + get(0, 1));
                break;
            case 'D':
            case '`':
                pending_wrap_ = false;
                if (final == '`')
                    cursor_x_ = std::clamp(get(0, 1) - 1, 0, cols_ - 1);
                else
                    cursor_x_ = std::max(0, cursor_x_ - get(0, 1));
                break;
            case 'G':
                cursor_x_ = std::clamp(get(0, 1) - 1, 0, cols_ - 1);
                pending_wrap_ = false;
                break;
            case 'd':
            case 'e':
                if (final == 'e') {
                    pending_wrap_ = false;
                    cursor_y_ = std::min(rows_ - 1, cursor_y_ + get(0, 1));
                } else {
                    moveTo(get(0, 1) - 1, cursor_x_);
                }
                break;
            case 'K':
                eraseLine(get(0, 0));
                break;
            case 'J':
                eraseDisplay(get(0, 0));
                break;
            case 'X':
                eraseChars(get(0, 1));
                break;
            case 'P':
                deleteChars(get(0, 1));
                break;
            case '@':
                insertChars(get(0, 1));
                break;
            case 'L':
                insertLines(get(0, 1));
                break;
            case 'M':
                deleteLines(get(0, 1));
                break;
            case 'm':
                handleSgr(params);
                break;
            case 's':
                if (params.empty() || params[0] == '?' || (params[0] >= '0' && params[0] <= '9'))
                    saveCursor();
                break;
            case 'u':
                if (params == "?") {
                    // The kitty keyboard protocol query. This terminal does not
                    // implement that protocol; saying so with a reply of no flags
                    // is what lets an application get on with starting up rather
                    // than waiting for an answer that never comes. It is not a
                    // cursor restore, whatever the final byte would otherwise mean.
                    response_out_ += "\x1b[?0u";
                    break;
                }
                if (params.empty() || params[0] == '>' || params[0] == '=')
                    restoreCursor();
                break;
            case 't': {
                // XTWINOPS. tmux emits CSI 22;0;0t on entering the alternate
                // screen and CSI 18 t to ask how large the window is, and this
                // machine's own terminfo puts CSI 22;0;0t inside smcup. A program
                // that gets no answer sits waiting for one, so the queries are
                // answered and the title stack is kept.
                const int op = get(0, 1);
                const int sub_param = p.size() > 1 ? p[1] : -1;
                const bool about_title = sub_param != 1; // 1 means the icon only
                switch (op) {
                    case 10: // the icon name; there is no icon to name
                        response_out_ += "\x1b]l\x07";
                        break;
                    case 11:
                    case 21: // the window title
                        response_out_ += "\x1b]l" + title_ + "\x07";
                        break;
                    case 14: // the text area, in pixels
                        response_out_ += "\x1b[4;" + std::to_string(rows_ * cell_px_h_) + ';' +
                                         std::to_string(cols_ * cell_px_w_) + 't';
                        break;
                    case 16: // one cell, in pixels
                        response_out_ += "\x1b[6;" + std::to_string(cell_px_h_) + ';' +
                                         std::to_string(cell_px_w_) + 't';
                        break;
                    case 18: // the text area, in characters
                        response_out_ += "\x1b[8;" + std::to_string(rows_) + ';' +
                                         std::to_string(cols_) + 't';
                        break;
                    case 22: // remember the title...
                        if (about_title) {
                            title_stack_.push_back(title_);
                            if (title_stack_.size() > kTitleStackDepth)
                                title_stack_.erase(title_stack_.begin());
                        }
                        break;
                    case 23: // ... and hand it back
                        if (about_title && !title_stack_.empty()) {
                            title_ = title_stack_.back();
                            title_stack_.pop_back();
                            title_dirty_ = true;
                        }
                        break;
                    default:
                        // 20 (window id), 22/23 with an icon-only parameter and
                        // the rest have nothing to answer here.
                        break;
                }
                break;
            }
            case 'r':
                setScrollRegion(get(0, 1) - 1,
                                get(1, rows_) - 1);
                break;
            case 'S':
                if (plain_csi)
                    scrollContentUp(get(0, 1)); // CSI ? S (XTSMGRAPHICS) must not scroll
                break;
            case 'T':
                if (plain_csi)
                    scrollContentDown(get(0, 1));
                break;
            case 'n':
                if (params == "5" || params.empty())
                    response_out_ += "\x1b[0n";
                else if (params == "6") {
                    const int x = std::min(cursor_x_ + 1, cols_);
                    response_out_ += "\x1b[" + std::to_string(cursor_y_ + 1) + ';' +
                                     std::to_string(x) + 'R';
                }
                break;
            case 'h':
                handleMode(params, true);
                break;
            case 'l':
                handleMode(params, false);
                break;
            case 'p':
                // DECSTR soft reset: ESC [ ! p
                if (params == "!") {
                    scroll_top_ = 0;
                    scroll_bottom_ = rows_ - 1;
                    autowrap_ = true;
                    origin_mode_ = false;
                    cursor_visible_ = true;
                    pending_wrap_ = false;
                }
                break;
            default:
                // Ignore unsupported final bytes rather than corrupting text.
                // (DECSCUSR " q", DECSCNM, and the XTWINOPS operations with
                // nothing to report land here.)
                break;
        }
        (void)plain_csi;
    }
};

} // namespace kai
