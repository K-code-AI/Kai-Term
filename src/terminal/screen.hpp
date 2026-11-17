#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
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

using Row = std::vector<Cell>;

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
        cursor_x_ = std::clamp(cursor_x_ + dx, 0, cols_);
        cursor_y_ = std::clamp(cursor_y_ + dy, 0, rows_ - 1);
    }

    const Row& row(int r) const { return grid_.at(r); }
    const Cell& at(int r, int c) const { return grid_.at(r).at(c); }
    const std::vector<Row>& scrollback() const { return scrollback_; }

    // Terminal-to-host replies produced by queries (DA1, DSR 5n/6n).
    // The consumer (widget or test harness) drains this and writes it to the PTY.
    std::string takeResponse() {
        std::string out;
        out.swap(response_out_);
        return out;
    }

    void resize(int new_rows, int new_cols) {
        new_rows = std::max(1, new_rows);
        new_cols = std::max(1, new_cols);
        if (new_rows == rows_ && new_cols == cols_)
            return;

        const std::vector<Row> old = grid_;
        const int old_rows = static_cast<int>(old.size());
        const int offset = std::max(0, old_rows - new_rows);
        grid_.assign(new_rows, Row(new_cols));

        for (int r = 0; r < new_rows; ++r) {
            const int source = offset + r;
            if (source >= old_rows)
                continue;
            const int copy_cols = std::min(new_cols, static_cast<int>(old[source].size()));
            for (int c = 0; c < copy_cols; ++c)
                grid_[r][c] = old[source][c];
        }

        if (offset > 0) {
            cursor_y_ = std::max(0, cursor_y_ - offset);
            // Rows pushed off the top of a full-screen scroll belong in scrollback.
            for (int r = 0; r < offset; ++r) {
                scrollback_.push_back(old[r]);
                if (scrollback_.size() > 1000)
                    scrollback_.erase(scrollback_.begin());
            }
        }
        cursor_y_ = std::min(cursor_y_, new_rows - 1);
        cursor_x_ = std::min(cursor_x_, new_cols);
        rows_ = new_rows;
        cols_ = new_cols;
        // A resize invalidates any scroll region; tmux re-establishes its own.
        scroll_top_ = 0;
        scroll_bottom_ = rows_ - 1;
        origin_mode_ = false;
        pending_wrap_ = false;
        if (alternate_valid_) {
            alternate_grid_.assign(rows_, Row(cols_));
            alternate_scrollback_.clear();
            alternate_cursor_x_ = 0;
            alternate_cursor_y_ = 0;
            alternate_scroll_top_ = 0;
            alternate_scroll_bottom_ = rows_ - 1;
        }
    }

    void clear() {
        for (auto& r : grid_)
            std::fill(r.begin(), r.end(), Cell{});
        cursor_x_ = cursor_y_ = 0;
        saved_valid_ = false;
        pending_wrap_ = false;
        scrollback_.clear();
    }

    void clearCell(Cell& c) {
        c.text.clear();
        c.width = 0;
        c.fg = Color{220, 220, 220};
        c.bg = Color{30, 30, 30};
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
            for (auto& r : grid_)
                for (auto& c : r)
                    clearCell(c);
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
            cursor_x_ = std::min(saved_x_, cols_);
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
            col = std::clamp(col, 0, cols_);
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
    Color current_fg_{220, 220, 220};
    Color current_bg_{30, 30, 30};
    Attr current_attr_{};
    bool cursor_visible_ = true;
    bool autowrap_ = true;
    bool pending_wrap_ = false;
    bool origin_mode_ = false;
    int g0_charset_ = 0; // 0 ASCII, 1 DEC Special Graphics
    int g1_charset_ = 0;
    bool gl_is_g1_ = false;

    Row blankRow() const {
        return Row(cols_);
    }

    // Scroll the scroll region up by one line. Content leaving the top of a
    // full-screen region is history and goes to scrollback; content leaving a
    // partial region (tmux panes, status lines) is discarded.
    void scrollUpInRegion() {
        if (scroll_top_ == 0 && scroll_bottom_ == rows_ - 1) {
            scrollback_.push_back(grid_[frontRowIndex()]);
            if (scrollback_.size() > 1000)
                scrollback_.erase(scrollback_.begin());
        }
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
            } else {
                w = 1; // no room for a double-width cell; clip
            }
        }

        if (cursor_y_ < 0 || cursor_y_ >= rows_)
            return;

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

    static int parseNumber(const std::string& s) {
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

    void handleSgr(const std::string& params) {
        // Private SGR like ESC[>4;2m (modifyOtherKeys) must not be parsed as
        // ordinary color/attribute parameters.
        if (!params.empty() && !(params[0] >= '0' && params[0] <= '9') && params[0] != ';')
            return;
        std::vector<int> p;
        std::size_t i = 0;
        while (i < params.size()) {
            std::size_t j = i;
            while (j < params.size() && params[j] != ';') ++j;
            if (j == i)
                p.push_back(0);
            else
                p.push_back(parseNumber(params.substr(i, j - i)));
            i = (j < params.size() ? j + 1 : j);
        }
        if (p.empty())
            p.push_back(0);

        for (std::size_t k = 0; k < p.size(); ++k) {
            int v = p[k];
            if (v == 0) {
                current_fg_ = Color{220, 220, 220};
                current_bg_ = Color{30, 30, 30};
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
            } else if (v == 5) {
                current_attr_.blink = true;
            } else if (v == 7) {
                current_attr_.reverse = true;
            } else if (v == 8) {
                current_attr_.hidden = true;
            } else if (v == 9) {
                current_attr_.strike = true;
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
            } else if (v == 29) {
                current_attr_.strike = false;
            } else if (v == 39) {
                current_fg_ = Color{220, 220, 220};
            } else if (v == 49) {
                current_bg_ = Color{30, 30, 30};
            } else if (v >= 30 && v <= 37) {
                current_fg_ = palette(v - 30);
            } else if (v >= 40 && v <= 47) {
                current_bg_ = palette(v - 40);
            } else if (v >= 90 && v <= 97) {
                current_fg_ = palette(v - 90 + 8);
            } else if (v >= 100 && v <= 107) {
                current_bg_ = palette(v - 100 + 8);
            } else if (v == 38) {
                if (k + 2 < p.size() && p[k + 1] == 5) {
                    current_fg_ = palette(p[k + 2]);
                    k += 2;
                } else if (k + 4 < p.size() && p[k + 1] == 2) {
                    current_fg_ = Color{
                        static_cast<std::uint8_t>(std::clamp(p[k + 2], 0, 255)),
                        static_cast<std::uint8_t>(std::clamp(p[k + 3], 0, 255)),
                        static_cast<std::uint8_t>(std::clamp(p[k + 4], 0, 255))
                    };
                    k += 4;
                }
            } else if (v == 48) {
                if (k + 2 < p.size() && p[k + 1] == 5) {
                    current_bg_ = palette(p[k + 2]);
                    k += 2;
                } else if (k + 4 < p.size() && p[k + 1] == 2) {
                    current_bg_ = Color{
                        static_cast<std::uint8_t>(std::clamp(p[k + 2], 0, 255)),
                        static_cast<std::uint8_t>(std::clamp(p[k + 3], 0, 255)),
                        static_cast<std::uint8_t>(std::clamp(p[k + 4], 0, 255))
                    };
                    k += 4;
                }
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
        cursor_x_ = std::min(alternate_cursor_x_, cols_);
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
                default:
                    // ?1, ?12, ?128, ?1000..?1006, ?2004, ?2026 etc. are accepted
                    // and ignored; input-side features arrive with M4/M5 work.
                    break;
            }
        }
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
                cursor_x_ = std::min(cols_, cursor_x_ + get(0, 1));
                break;
            case 'D':
            case '`':
                pending_wrap_ = false;
                if (final == '`')
                    cursor_x_ = std::clamp(get(0, 1) - 1, 0, cols_);
                else
                    cursor_x_ = std::max(0, cursor_x_ - get(0, 1));
                break;
            case 'G':
                cursor_x_ = std::clamp(get(0, 1) - 1, 0, cols_);
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
                // ESC[?u is the kitty keyboard query (no reply, no restore);
                // plain ESC[u and ESC[>u variants restore.
                if (params.empty() || params[0] == '>' || params[0] == '=')
                    restoreCursor();
                break;
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
                // (DECSCUSR " q", XTWINOPS "t", DECSCNM, etc. land here.)
                break;
        }
        (void)plain_csi;
    }
};

} // namespace kai
