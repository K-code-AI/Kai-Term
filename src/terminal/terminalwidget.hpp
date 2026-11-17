#pragma once

#include "screen.hpp"
#include "../process/pty.hpp"

#include <QWidget>
#include <QFont>
#include <QFontMetricsF>
#include <QChar>
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QColor>
#include <QRectF>
#include <QPen>
#include <QByteArray>
#include <QCoreApplication>

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

    bool event(QEvent* e) override {
        if (auto* out = dynamic_cast<TerminalOutputEvent*>(e)) {
            screen_.feed(out->data);

            // Forward terminal-to-host query replies (DA1, DSR 5n/6n) to the PTY.
            // fish and tmux both probe the terminal at startup, and tmux uses
            // DSR 6n cursor position reports for output leak detection.
            const std::string response = screen_.takeResponse();
            if (!response.empty())
                pty_.write(response);

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
        if (!shell_alive_ || !pty_.alive())
            return;

        const int key = ev->key();

        // Fish recognizes ANSI up/down/left/right cursor bindings.
        // Send the matching escape sequence so fish moves its own command-line
        // cursor and updates autosuggestions.
        if (key == Qt::Key_Left || key == Qt::Key_Right ||
            key == Qt::Key_Up || key == Qt::Key_Down) {
            std::string seq;
            if (key == Qt::Key_Left)
                seq = "\x1b" "[D";   // backward-char
            else if (key == Qt::Key_Right)
                seq = "\x1b" "[C";  // forward-char
            else if (key == Qt::Key_Up)
                seq = "\x1b" "[A";  // up-line
            else
                seq = "\x1b" "[B";  // down-line
            pty_.write(seq);
            return;
        }

        std::string bytes;
        QString text = ev->text();
        if (!text.isEmpty()) {
            QByteArray utf8 = text.toUtf8();
            bytes.assign(utf8.constData(), static_cast<size_t>(utf8.size()));
        } else {
            const int k = ev->key();
            if (k == Qt::Key_Return || k == Qt::Key_Enter) {
                bytes = "\n";
            } else if (k == Qt::Key_Backspace) {
                bytes = "\b";
            } else if (k == Qt::Key_Tab) {
                bytes = "\t";
            } else if (k == Qt::Key_Escape) {
                bytes = "\x1b";
            } else if ((ev->modifiers() & Qt::ControlModifier) &&
                       k >= Qt::Key_A && k <= Qt::Key_Z) {
                bytes = std::string(1, static_cast<char>(k - Qt::Key_A + 1));
            }
        }

        if (bytes.empty())
            return;
        if (bytes == "\r")
            bytes = "\n";
        pty_.write(bytes);
    }

    void mousePressEvent(QMouseEvent*) override {
        setFocus();
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
            for (int c = 0; c < cols_; ++c) {
                const Cell& cell = screen_.at(r, c);
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
        if (screen_.cursorVisible()) {
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
};

} // namespace kai
