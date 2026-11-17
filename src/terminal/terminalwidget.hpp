#pragma once

#include "screen.hpp"
#include "keyencode.hpp"
#include "../process/pty.hpp"

#include <QWidget>
#include <QFont>
#include <QFontMetricsF>
#include <QChar>
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QColor>
#include <QRectF>
#include <QPen>
#include <QByteArray>
#include <QCoreApplication>

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <utility>

namespace kai {

struct TerminalOutputEvent : QEvent {
    explicit TerminalOutputEvent(std::string data)
        : QEvent(QEvent::None), data(std::move(data)) {}
    std::string data;
};

struct TerminalExitEvent : QEvent {
    TerminalExitEvent() : QEvent(QEvent::None) {}
};

class TerminalWidget : public QWidget {
public:
    explicit TerminalWidget(int rows = 24, int cols = 80, std::string shell = {})
        : pty_(rows, cols, shell), screen_(rows, cols),
          rows_(rows), cols_(cols), running_(true), shell_alive_(true)
    {
        font_ = QFont("Monospace");
        font_.setPixelSize(14);
        setFocusPolicy(Qt::StrongFocus);

        reader_ = std::thread([this] {
            char buf[4096];
            while (running_ && shell_alive_) {
                ssize_t n = pty_.read(buf, sizeof(buf));
                if (n <= 0) {
                    shell_alive_ = false;
                    QCoreApplication::postEvent(this, new TerminalExitEvent());
                    break;
                }
                QCoreApplication::postEvent(
                    this, new TerminalOutputEvent(std::string(buf, static_cast<size_t>(n))));
            }
        });
    }

    void shutdown() {
        running_ = false;
        pty_.stop();
        if (reader_.joinable())
            reader_.join();
        shell_alive_ = false;
    }

    ~TerminalWidget() override {
        shutdown();
    }

    // Read-only view of the terminal model, for tests and for UI pieces such as
    // a scrollbar that must agree with the painted content.
    const Screen& screen() const { return screen_; }
    int rows() const { return rows_; }
    int cols() const { return cols_; }

    // Lines scrolled back from the live bottom; zero means following the shell.
    int scrollOffset() const { return scroll_offset_; }
    // How far the view could scroll back right now.
    int scrollRange() const { return screen_.inAlternateScreen() ? 0 : screen_.historySize(); }

    bool event(QEvent* e) override {
        if (auto* out = dynamic_cast<TerminalOutputEvent*>(e)) {
            const int history_before = screen_.historySize();
            screen_.feed(out->data);

            // Forward terminal-to-host query replies (DA1, DSR 5n/6n) to the PTY.
            // fish and tmux both probe the terminal at startup, and tmux uses
            // DSR 6n cursor position reports for output leak detection.
            const std::string response = screen_.takeResponse();
            if (!response.empty())
                pty_.write(response);

            // The alternate screen has no history of its own, and the
            // application inside it (vim, less, tmux) owns scrolling there.
            if (screen_.inAlternateScreen())
                scroll_offset_ = 0;
            else
                followScroll(history_before);

            update();
            return true;
        }
        if (dynamic_cast<TerminalExitEvent*>(e)) {
            shell_alive_ = false;
            QCoreApplication::quit();
            return true;
        }
        return QWidget::event(e);
    }

    void keyPressEvent(QKeyEvent* ev) override {
        // Any key that reaches the shell returns the view to the live bottom.
        // Shift+PageUp/PageDown/Home/End are kept by the terminal for scrollback
        // and are never sent to the PTY. The offset counts lines scrolled back
        // from the live bottom, so "older" means a larger offset.
        const Qt::KeyboardModifiers mods = ev->modifiers();
        const int key = ev->key();
        const bool shift = mods.testFlag(Qt::ShiftModifier);
        if (shift && (key == Qt::Key_PageUp || key == Qt::Key_PageDown ||
                      key == Qt::Key_Home || key == Qt::Key_End)) {
            if (key == Qt::Key_PageUp)
                scrollBy(std::max(1, rows_ - 1));
            else if (key == Qt::Key_PageDown)
                scrollBy(-std::max(1, rows_ - 1));
            else if (key == Qt::Key_Home)
                scrollBy(screen_.historySize());
            else
                scrollBy(-screen_.historySize());
            return;
        }

        scroll_offset_ = 0;

        if (!shell_alive_ || !pty_.alive())
            return;

        const std::string bytes = encodeKey(key, mods, ev->text(),
                                            screen_.applicationCursorKeys());
        if (bytes.empty())
            return;
        pty_.write(bytes);
    }

    void mousePressEvent(QMouseEvent*) override {
        setFocus();
    }

    // Wheel scrolls the retained history: a notch away from the user shows
    // older lines. While the alternate screen is active the offset stays at zero
    // and the wheel event is left to Qt so the application behind it can claim
    // mouse reporting later (M2).
    void wheelEvent(QWheelEvent* ev) override {
        const int steps = ev->angleDelta().y() / 120;
        if (steps == 0 || screen_.inAlternateScreen())
            return;
        scrollBy(steps * kWheelRows);
        ev->accept();
    }

    void resizeEvent(QResizeEvent*) override {
        QFontMetricsF fm(font_);
        double cw = fm.horizontalAdvance(QChar('M'));
        if (cw <= 0.0)
            cw = fm.maxWidth();
        const double ch = fm.height();
        if (cw <= 0.0 || ch <= 0.0)
            return;

        const int new_cols = std::max(1, static_cast<int>((width() - 2.0 * padding_) / cw));
        const int new_rows = std::max(1, static_cast<int>((height() - 2.0 * padding_) / ch));
        if (new_rows == rows_ && new_cols == cols_)
            return;

        rows_ = new_rows;
        cols_ = new_cols;
        screen_.resize(rows_, cols_);
        pty_.resize(rows_, cols_);
        clampScroll();
        update();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(0, 0, width(), height(), QColor(30, 30, 30));

        QFontMetricsF fm(font_);
        double cw = fm.horizontalAdvance(QChar('M'));
        if (cw <= 0.0)
            cw = fm.maxWidth();
        const double ch = fm.height();
        if (cw <= 0.0 || ch <= 0.0)
            return;

        const Color default_bg{30, 30, 30};

        for (int r = 0; r < rows_; ++r) {
            const Row& line = displayRow(r);
            // History rows keep the width they were written at, so a row can be
            // narrower than the current screen after a resize.
            const int line_cols = std::min(cols_, static_cast<int>(line.size()));
            for (int c = 0; c < line_cols; ++c) {
                const Cell& cell = line[c];
                if (cell.text.empty() || cell.width <= 0)
                    continue;
                if (cell.attr.hidden)
                    continue;

                const double x = padding_ + c * cw;
                const double y = padding_ + r * ch;

                Color fg = cell.fg;
                Color bg = cell.bg;
                if (cell.attr.reverse)
                    std::swap(fg, bg);

                if (bg.r != default_bg.r || bg.g != default_bg.g || bg.b != default_bg.b)
                    p.fillRect(QRectF(x, y, cw * cell.width, ch), QColor(bg.r, bg.g, bg.b));

                QFont f = font_;
                if (cell.attr.bold)
                    f.setBold(true);
                if (cell.attr.italic)
                    f.setItalic(true);
                p.setFont(f);
                p.setPen(QColor(fg.r, fg.g, fg.b, cell.attr.dim ? 180 : 255));

                QString text = QString::fromUtf8(cell.text.c_str(), cell.text.size());
                p.drawText(QPointF(x, y + fm.ascent()), text);

                if (cell.attr.underline || cell.attr.strike) {
                    p.setPen(QPen(QColor(fg.r, fg.g, fg.b), 1.0));
                    double ly = y + ch - 1.5;
                    if (cell.attr.strike)
                        ly = y + ch / 2.0;
                    p.drawLine(QPointF(x, ly), QPointF(x + cw * cell.width, ly));
                }
            }
        }

        // Visible terminal cursor; tmux hides it during redraws via ESC[?25l.
        // It belongs to the live screen, so it is not drawn over history.
        if (scroll_offset_ == 0 && screen_.cursorVisible()) {
            const int cx = std::clamp(screen_.cursorX(), 0, cols_ - 1);
            const int cy = std::clamp(screen_.cursorY(), 0, rows_ - 1);
            p.fillRect(QRectF(padding_ + cx * cw,
                        padding_ + cy * ch + ch - 2.0,
                        cw, 2.0),
                       QColor(220, 220, 220, 210));
        }
    }

private:
    Pty pty_;
    Screen screen_;
    QFont font_;
    std::thread reader_;
    std::atomic<bool> running_;
    std::atomic<bool> shell_alive_;
    int rows_;
    int cols_;
    double padding_ = 10.0;

    // Rows moved by one wheel notch, and how far the view is scrolled back from
    // the live bottom in lines. Zero means "following the shell".
    static constexpr int kWheelRows = 3;
    int scroll_offset_ = 0;

    void scrollBy(int lines) {
        scroll_offset_ += lines;
        clampScroll();
        update();
    }

    void clampScroll() {
        scroll_offset_ = std::clamp(scroll_offset_, 0, screen_.historySize());
    }

    // Hold a scrolled-back view on the same lines while new output pushes
    // history, the way xterm keeps the display still instead of jumping it.
    void followScroll(int history_before) {
        if (scroll_offset_ > 0)
            scroll_offset_ += screen_.historySize() - history_before;
        clampScroll();
    }

    // Display row r, which may be a scrollback line rather than a live screen
    // row. Rows past the end of history resolve to live screen rows so a
    // partially scrolled view still shows the bottom of the screen.
    const Row& displayRow(int r) const {
        if (scroll_offset_ == 0)
            return screen_.row(r);
        const int idx = screen_.historySize() - scroll_offset_ + r;
        if (const Row* h = screen_.historyRow(idx))
            return *h;
        const int live = idx - screen_.historySize();
        if (live >= 0 && live < screen_.rows())
            return screen_.row(live);
        static const Row kEmptyRow;
        return kEmptyRow;
    }
};

} // namespace kai
