#pragma once

#include "screen.hpp"
#include "keyencode.hpp"
#include "mouseencode.hpp"
#include "scrollbar.hpp"
#include "selection.hpp"
#include "text.hpp"
#include "../process/pty.hpp"

#include <poll.h>

#include <QWidget>
#include <QClipboard>
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
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QSizeF>

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
        // Any-event mouse reporting (?1003) needs motion with no button held.
        setMouseTracking(true);

        reader_ = std::thread([this] {
            char buf[4096];
            while (running_ && shell_alive_) {
                // Wait on the descriptor rather than blocking in read(): a stray
                // job that inherited the terminal can keep the stream open after
                // the shell itself is gone, and the window has to close on the
                // shell leaving, not on whoever else lets go.
                struct pollfd watched {pty_.master(), POLLIN, 0};
                const int ready = ::poll(&watched, 1, 200);
                if (ready < 0 && errno == EINTR)
                    continue;
                if (pty_.reap()) {
                    // The shell is gone. Drain anything still readable first is
                    // not worth it here: whatever it printed on the way out has
                    // already been queued as an event by the read below.
                    shell_alive_ = false;
                    QCoreApplication::postEvent(this, new TerminalExitEvent());
                    break;
                }
                if (ready == 0)
                    continue;
                ssize_t n = pty_.read(buf, sizeof(buf));
                if (n <= 0) {
                    // The shell closed the terminal. Collect it here so the exit
                    // status means something; the master stays open until
                    // shutdown has joined this thread. (A successful write is no
                    // sign of life — see Pty::write.)
                    pty_.reap();
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
        // The child goes first, and the reader is joined before the master is
        // closed. Closing a descriptor under a thread blocked reading it leaves
        // that number free to be handed to somebody else's open() while the read
        // is still in flight, so teardown waits for the reader to notice the end
        // of the stream on its own.
        pty_.terminate();
        if (reader_.joinable())
            reader_.join();
        pty_.closeMaster();
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

    // Where the scroll indicator sits right now, in this widget's pixels. Aimed
    // at by the mouse handlers and by anything that needs to click on it.
    ScrollBar scrollIndicator() const {
        return scrollBarGeometry(QSize(width(), height()), screen_.historySize(), rows_,
                                 scroll_offset_);
    }
    // How far the view could scroll back right now.
    int scrollRange() const { return screen_.inAlternateScreen() ? 0 : screen_.historySize(); }

    // The current text selection, in display coordinates, and the text it covers.
    Selection selection() const { return selection_; }
    std::string selectedText() const {
        return selectionTextAt(cols_, selection_,
                               [&](int r) -> const Row& { return displayRow(r); });
    }

    // Geometry for anything that has to aim at a character: the size of one cell
    // and the pixel position of a cell's top-left corner. Tests use it to send
    // a mouse event at a specific piece of text; a scrollbar or tooltip would
    // want the same numbers.
    QSizeF cellSize() const { return QSizeF(cellWidth(), cellHeight()); }
    QPointF cellTopLeft(int row, int col) const {
        return QPointF(padding_ + col * cellWidth(), padding_ + row * cellHeight());
    }

    // Paste as if the text came from the clipboard: newlines become carriage
    // returns and the payload is fenced when the application asked for bracketed
    // paste. Exposed because the middle-click path needs a clipboard, and a test
    // should still be able to check what the terminal would send.
    void pasteText(const std::string& text) {
        if (text.empty() || !shell_alive_ || !pty_.alive())
            return;

        // A line feed is delivered as Enter, so pasting a script would run
        // commands the user never pressed. Newlines go out as carriage returns
        // and the line discipline turns them back into line feeds.
        std::string out;
        out.reserve(text.size() + 16);
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '\r') {
                // CRLF counts as one newline: drop the CR, the LF becomes a CR.
                if (i + 1 < text.size() && text[i + 1] == '\n')
                    continue;
                out += '\r';
            } else if (c == '\n') {
                out += '\r';
            } else {
                out += c;
            }
        }

        // Bracketed paste tells the application these bytes are pasted text, not
        // typing, so readline and fish do not act on each line as it arrives.
        if (screen_.bracketedPaste())
            out = "\x1b[200~" + out + "\x1b[201~";

        writeToPty(out);
    }

    bool event(QEvent* e) override {
        if (auto* out = dynamic_cast<TerminalOutputEvent*>(e)) {
            const int history_before = screen_.historySize();
            const int view_top_before = viewTop();
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

            // New output moves the text under a selection. The highlight follows
            // it instead of being thrown away, so selecting from a log that is
            // still filling up keeps working; once the selected lines have gone
            // off the top the selection is dropped rather than left pointing at
            // text nobody selected.
            keepSelectionOnScreen(view_top_before);

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

        // A modifier pressed on its own is not a key press in any sense: it is
        // the first half of a chord. Qt delivers it as Key_Control, Key_Shift and
        // so on, and treating it as ordinary input threw away the selection the
        // moment the user started pressing Ctrl+Shift+C to copy that selection.
        if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt ||
            key == Qt::Key_AltGr || key == Qt::Key_Meta)
            return;

        // Ctrl+Shift+C and Ctrl+Shift+V are the terminal's own, handled before
        // anything else and never written to the PTY. Without the early return
        // Ctrl+Shift+C would encode as Ctrl+C and interrupt whatever is running.
        if (mods.testFlag(Qt::ControlModifier) && shift) {
            if (key == Qt::Key_C) {
                copySelection();
                return;
            }
            if (key == Qt::Key_V) {
                scroll_offset_ = 0;
                pasteFromClipboard();
                return;
            }
        }

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
        clearSelection();

        if (!shell_alive_ || !pty_.alive())
            return;

        const std::string bytes = encodeKey(key, mods, ev->text(),
                                            screen_.applicationCursorKeys());
        if (bytes.empty())
            return;
        pty_.write(bytes);
    }

    // Mouse handling splits on what the application asked for. With reporting on
    // (vim, htop, tmux) events are encoded and written to the PTY and the
    // terminal keeps no selection. With it off, the left button drags a
    // selection and the middle button pastes. Holding Shift overrides reporting,
    // the xterm rule, and is how you copy text out of a program that grabbed the
    // mouse.
    void mousePressEvent(QMouseEvent* ev) override {
        setFocus();
        stopEdgeScroll();
        if (scrollBarPress(ev->position()))
            return;
        const CellPos pos = cellAt(ev->position());
        const bool force_local = ev->modifiers().testFlag(Qt::ShiftModifier);

        if (screen_.mouseReporting() && !force_local) {
            MouseReport rep;
            rep.button = mouseKey(ev->button());
            rep.held = heldBits(ev->buttons());
            rep.action = MouseAction::Press;
            rep.mods = mouseMods(ev->modifiers());
            rep.row = pos.row;
            rep.col = pos.col;
            last_reported_ = pos;
            writeToPty(mouseBytes(rep));
            return;
        }

        if (ev->button() == Qt::MiddleButton) {
            pasteFromClipboard();
            return;
        }
        if (ev->button() != Qt::LeftButton)
            return;

        // Shift+click moves the end of an existing selection, which is what
        // every other text surface does and the only way to make a long
        // selection without dragging across the whole window. The gesture that
        // started the selection decides whether it still grows by words or lines.
        if (force_local && !selection_.empty()) {
            dragging_ = true;
            extendSelection(pos);
            update();
            return;
        }

        // Qt 6.11 as packaged here exposes no click count on QMouseEvent, and a
        // synthesized event would not carry one anyway, so presses that land on
        // the same cell in quick succession are counted locally: two select a
        // word, three select the line.
        const int clicks = countClick(pos);
        if (clicks >= 3)
            drag_mode_ = DragMode::Line;
        else if (clicks == 2)
            drag_mode_ = DragMode::Word;
        else
            drag_mode_ = DragMode::Char;
        dragging_ = true;
        anchor_ = pos;
        extendSelection(pos);
        update();
    }

    void mouseMoveEvent(QMouseEvent* ev) override {
        if (scrollbar_drag_) {
            scrollBarDragTo(ev->position());
            return;
        }
        const CellPos pos = cellAt(ev->position());
        const bool force_local = ev->modifiers().testFlag(Qt::ShiftModifier);

        if (screen_.mouseReporting() && !force_local) {
            const bool held = ev->buttons() != Qt::NoButton;
            if (!held && screen_.mouseMode() != MouseMode::Any)
                return;
            // Only cell changes are reported, which is what xterm does and what
            // keeps a slow application from drowning in motion events.
            if (pos.row == last_reported_.row && pos.col == last_reported_.col)
                return;
            MouseReport rep;
            rep.button = MouseKey::None;
            rep.held = heldBits(ev->buttons());
            rep.action = MouseAction::Motion;
            rep.mods = mouseMods(ev->modifiers());
            rep.row = pos.row;
            rep.col = pos.col;
            last_reported_ = pos;
            writeToPty(mouseBytes(rep));
            return;
        }

        if (!dragging_)
            return;
        updateEdgeScroll(pos);
        extendSelection(pos);
        update();
    }

    // Selecting in any other text surface keeps going when the pointer is held
    // against the top or bottom of the window: the view crawls and the selection
    // grows with it. Without this, a drag stops dead one row from the edge and
    // text further away can only be reached by letting go and dragging again.
    void updateEdgeScroll(const CellPos& pos) {
        edge_col_ = pos.col;
        int direction = 0;
        if (pos.row <= 0 && scroll_offset_ < screen_.historySize())
            direction = 1; // held at the top: bring older lines in
        else if (pos.row >= rows_ - 1 && scroll_offset_ > 0)
            direction = -1; // held at the bottom: bring newer lines back
        if (direction == edge_direction_)
            return;
        edge_direction_ = direction;
        if (direction == 0) {
            stopEdgeScroll();
            return;
        }
        if (!edge_timer_) {
            edge_timer_ = new QTimer(this);
            edge_timer_->setInterval(kEdgeScrollMs);
            connect(edge_timer_, &QTimer::timeout, this, [this] { edgeScrollStep(); });
        }
        edge_timer_->start();
    }

    void stopEdgeScroll() {
        edge_direction_ = 0;
        if (edge_timer_)
            edge_timer_->stop();
    }

    void edgeScrollStep() {
        const int direction = edge_direction_;
        const int view_top_before = viewTop();
        const CellPos anchor_before = anchor_;
        const bool had_selection = !selection_.empty();
        scrollBy(direction);
        if (had_selection && selection_.empty()) {
            // The view carried the anchor out of the window and the ordinary rule
            // dropped the selection. Mid-drag that is wrong: the pointer is still
            // held down and still selecting, so keep the drag alive at the edge
            // the anchor left. Text that has scrolled out of reach is not added to
            // the selection; what is on screen is.
            dragging_ = true;
            anchor_.row = std::clamp(anchor_before.row + (view_top_before - viewTop()), 0,
                                     rows_ - 1);
        }
        extendSelection(CellPos{direction > 0 ? 0 : rows_ - 1, edge_col_});
        update();
    }

    // Letting go, or taking the pointer out of the window, ends the crawl.
    void leaveEvent(QEvent* ev) override {
        stopEdgeScroll();
        QWidget::leaveEvent(ev);
    }

    void mouseReleaseEvent(QMouseEvent* ev) override {
        stopEdgeScroll();
        if (scrollbar_drag_) {
            endScrollBarDrag();
            return;
        }
        const CellPos pos = cellAt(ev->position());
        const bool force_local = ev->modifiers().testFlag(Qt::ShiftModifier);

        if (screen_.mouseReporting() && !force_local) {
            MouseReport rep;
            rep.button = mouseKey(ev->button());
            rep.held = heldBits(ev->buttons());
            rep.action = MouseAction::Release;
            rep.mods = mouseMods(ev->modifiers());
            rep.row = pos.row;
            rep.col = pos.col;
            writeToPty(mouseBytes(rep));
            return;
        }

        if (!dragging_)
            return;
        dragging_ = false;
        // A finished drag copies nothing, in either direction. The highlight
        // stays on screen for the user to read, and Ctrl+Shift+C is what puts the
        // text where anything can paste it.
        update();
    }

    // Wheel scrolls the retained history: a notch away from the user shows
    // older lines. With mouse reporting on it is a button for the application
    // instead, which is how scrolling works in vim and less.
    void wheelEvent(QWheelEvent* ev) override {
        const int steps = ev->angleDelta().y() / 120;
        if (steps == 0)
            return;

        const bool force_local = ev->modifiers().testFlag(Qt::ShiftModifier);
        if (screen_.mouseReporting() && !force_local) {
            const CellPos pos = cellAt(ev->position());
            MouseReport rep;
            rep.button = (steps > 0) ? MouseKey::WheelUp : MouseKey::WheelDown;
            rep.held = 0;
            rep.action = MouseAction::Press;
            rep.mods = mouseMods(ev->modifiers());
            rep.row = pos.row;
            rep.col = pos.col;
            writeToPty(mouseBytes(rep));
            ev->accept();
            return;
        }

        // While the alternate screen is active the application owns scrolling.
        if (screen_.inAlternateScreen())
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
        // Reflow moves text between rows and columns, so a highlight drawn before
        // the resize would be sitting over different characters afterwards. It is
        // dropped rather than left to drift onto text nobody selected.
        clearSelection();
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
        // Selection is drawn as a solid band with light text so it stays legible
        // whatever colours the application had chosen for those cells.
        const Color selection_bg{62, 96, 168};
        const Color selection_fg{240, 240, 240};

        for (int r = 0; r < rows_; ++r) {
            const Row& line = displayRow(r);
            // History rows keep the width they were written at, so a row can be
            // narrower than the current screen after a resize.
            const int line_cols = std::min(cols_, static_cast<int>(line.size()));
            for (int c = 0; c < line_cols; ++c) {
                const Cell& cell = line[c];
                const double x = padding_ + c * cw;
                const double y = padding_ + r * ch;

                // Blank cells are skipped below, so the highlight for them has
                // to be painted here or a dragged band would show gaps.
                if (selection_.contains(r, c) && (cell.text.empty() || cell.width <= 0)) {
                    p.fillRect(QRectF(x, y, cw, ch),
                               QColor(selection_bg.r, selection_bg.g, selection_bg.b));
                }

                if (cell.text.empty() || cell.width <= 0)
                    continue;
                if (cell.attr.hidden)
                    continue;

                Color fg = cell.fg;
                Color bg = cell.bg;
                if (cell.attr.reverse)
                    std::swap(fg, bg);
                if (selection_.contains(r, c)) {
                    bg = selection_bg;
                    fg = selection_fg;
                }

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

        // The scroll indicator is painted last, over the grid, so it never takes
        // a column away from the text. It only appears when there is somewhere
        // else to look.
        scroll_bar_ = scrollIndicator();
        if (scroll_bar_.visible) {
            const int alpha = scrollbar_drag_ ? 190 : 90;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(220, 220, 220, alpha));
            p.drawRoundedRect(scroll_bar_.thumb.adjusted(2, 0, -2, 0), kScrollBarRadius,
                              kScrollBarRadius);
        }
    }

private:
    // A press that lands on the indicator belongs to the terminal, not to the
    // application inside it and not to a text selection.
    bool scrollBarPress(const QPointF& where) {
        scroll_bar_ = scrollIndicator();
        if (!scroll_bar_.visible)
            return false;
        const QPoint point = where.toPoint();
        if (!trackContains(scroll_bar_, point))
            return false;
        if (thumbContains(scroll_bar_, point)) {
            scrollbar_drag_ = true;
            scrollbar_grab_ = point.y() - scroll_bar_.thumb.top();
            setCursor(Qt::SizeVerCursor);
        } else if (point.y() < scroll_bar_.thumb.top()) {
            scrollBy(std::max(1, rows_ - 1)); // click above the thumb: older
        } else {
            scrollBy(-std::max(1, rows_ - 1)); // below it: newer
        }
        return true;
    }

    void scrollBarDragTo(const QPointF& where) {
        scroll_bar_ = scrollIndicator();
        const int top = where.toPoint().y() - scrollbar_grab_;
        const int offset = scrollOffsetForThumbY(top, scroll_bar_, screen_.historySize(), rows_);
        scrollBy(offset - scroll_offset_);
        // The thumb follows the pointer even at either end of the scrollback,
        // where the offset itself cannot move.
        scroll_bar_ = scrollBarWithThumbTop(scroll_bar_, top);
        update();
    }

    void endScrollBarDrag() {
        if (!scrollbar_drag_)
            return;
        scrollbar_drag_ = false;
        unsetCursor();
        update();
    }

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
    // Presses landing on the same cell inside this window are one gesture, the
    // same span Qt's double-click interval uses by default.
    static constexpr qint64 kDoubleClickMs = 400;
    int scroll_offset_ = 0;

    // The indicator's geometry, recomputed whenever the screen is painted, and
    // the state of a drag on its thumb.
    ScrollBar scroll_bar_;
    bool scrollbar_drag_ = false;
    int scrollbar_grab_ = 0;

    // Auto-scroll while a selection drag is held against an edge: the timer that
    // crawls the view, which way it is crawling, and the column the pointer is
    // over so the selection grows sideways as well as down.
    static constexpr int kEdgeScrollMs = 60;
    QTimer* edge_timer_ = nullptr;
    int edge_direction_ = 0;
    int edge_col_ = 0;

    // Text selection in display coordinates, plus the drag state that builds it.
    enum class DragMode { Char, Word, Line };

    Selection selection_{};
    bool dragging_ = false;
    DragMode drag_mode_ = DragMode::Char;
    CellPos anchor_{};
    // Repeated presses on one cell, for word and line selection.
    QElapsedTimer click_clock_;
    qint64 last_click_ms_ = 0;
    CellPos last_click_pos_{};
    int click_count_ = 0;
    // Last cell reported to the application, so motion is only sent when the
    // pointer moves onto a different character.
    CellPos last_reported_{};

    void scrollBy(int lines) {
        const int view_top_before = viewTop();
        scroll_offset_ += lines;
        clampScroll();
        keepSelectionOnScreen(view_top_before);
        update();
    }

    // The absolute buffer line shown at the top of the window. Selections are
    // kept on their text by watching how far this moves.
    int viewTop() const { return screen_.historySize() - scroll_offset_; }

    // Slide a selection by however far the text underneath it moved, and drop
    // it once any part of it leaves the window. Called after output and after
    // scrolling, which are the two things that move content on screen.
    void keepSelectionOnScreen(int view_top_before) {
        const int shift = view_top_before - viewTop();
        if (shift == 0)
            return;
        if (selection_.empty()) {
            // A drag that has not produced a range yet still has an anchor, and it
            // has to travel with its text too — otherwise the moment the view
            // moves, the drag is left pointing at whatever moved into the cell it
            // started on.
            if (dragging_)
                anchor_.row = std::clamp(anchor_.row + shift, 0, rows_ - 1);
            return;
        }
        const int anchor_row = selection_.anchor.row + shift;
        const int head_row = selection_.head.row + shift;
        if (anchor_row < 0 || head_row < 0 || anchor_row >= rows_ || head_row >= rows_) {
            selection_ = Selection{};
            dragging_ = false;
            return;
        }
        selection_.anchor.row = anchor_row;
        selection_.head.row = head_row;
        // A drag that resumes after output has to extend the same text, so the
        // end it grew from has to move with it.
        anchor_.row += shift;
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

    // Cell size, derived the way the painter derives it, so hit testing and
    // painting agree about which character is under the cursor.
    double cellWidth() const {
        QFontMetricsF fm(font_);
        double cw = fm.horizontalAdvance(QChar('M'));
        if (cw <= 0.0)
            cw = fm.maxWidth();
        return cw;
    }
    double cellHeight() const {
        QFontMetricsF fm(font_);
        return fm.height();
    }

    // Pixel position to grid cell, in display coordinates: the same row index
    // the painter used, so a selection made while scrolled up stays on the text
    // under the pointer.
    CellPos cellAt(const QPointF& pos) const {
        const double cw = cellWidth();
        const double ch = cellHeight();
        if (cw <= 0.0 || ch <= 0.0)
            return {};
        CellPos out;
        out.row = std::clamp(static_cast<int>((pos.y() - padding_) / ch), 0, rows_ - 1);
        // A click in the right-hand margin selects up to the end of the row,
        // which is column count, one past the last cell.
        out.col = std::clamp(static_cast<int>((pos.x() - padding_) / cw), 0, cols_);
        return out;
    }

    static MouseKey mouseKey(Qt::MouseButton button) {
        switch (button) {
            case Qt::LeftButton: return MouseKey::Left;
            case Qt::MiddleButton: return MouseKey::Middle;
            case Qt::RightButton: return MouseKey::Right;
            default: return MouseKey::None;
        }
    }

    // Qt modifiers to the protocol's modifier bits.
    static int mouseMods(Qt::KeyboardModifiers mods) {
        int bits = 0;
        if (mods & Qt::ShiftModifier) bits |= MouseShift;
        if (mods & Qt::AltModifier) bits |= MouseAlt;
        if (mods & Qt::ControlModifier) bits |= MouseCtrl;
        return bits;
    }

    static unsigned heldBits(Qt::MouseButtons buttons) {
        unsigned bits = 0;
        if (buttons & Qt::LeftButton) bits |= mouseKeyBit(MouseKey::Left);
        if (buttons & Qt::MiddleButton) bits |= mouseKeyBit(MouseKey::Middle);
        if (buttons & Qt::RightButton) bits |= mouseKeyBit(MouseKey::Right);
        return bits;
    }

    std::string mouseBytes(const MouseReport& rep) const {
        return encodeMouse(rep, screen_.mouseMode(), screen_.mouseCoord());
    }

    void writeToPty(const std::string& bytes) {
        if (bytes.empty() || !shell_alive_ || !pty_.alive())
            return;
        pty_.write(bytes);
    }

    // Classification for word-wise selection. Word characters are ASCII letters,
    // digits and underscore, matching what shells use for word-wise movement;
    // anything else visible is selected one cell at a time.
    CellKind kindAt(int row, int col) const {
        const Row& line = displayRow(row);
        if (col < 0 || col >= static_cast<int>(line.size()))
            return CellKind::Blank;
        const std::string& text = line[static_cast<std::size_t>(col)].text;
        if (text.empty())
            return CellKind::Blank;
        const unsigned char c = static_cast<unsigned char>(text[0]);
        const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_';
        return word ? CellKind::Word : CellKind::Other;
    }

    // Count a press that lands on the same cell shortly after the previous one
    // as another click of the same gesture. Returns 1 for a fresh press.
    int countClick(const CellPos& pos) {
        if (!click_clock_.isValid()) {
            click_clock_.start();
            click_count_ = 1;
        } else {
            const bool same_cell =
                last_click_pos_.row == pos.row && last_click_pos_.col == pos.col;
            const bool quick = click_clock_.elapsed() - last_click_ms_ <= kDoubleClickMs;
            click_count_ = (same_cell && quick) ? click_count_ + 1 : 1;
        }
        last_click_pos_ = pos;
        last_click_ms_ = click_clock_.elapsed();
        return click_count_;
    }

    // Grow the selection to `to` the way the gesture that started it wants:
    // character by character, whole words, or whole rows.
    void extendSelection(const CellPos& to) {
        switch (drag_mode_) {
            case DragMode::Char:
                selection_ = Selection{anchor_, to};
                break;
            case DragMode::Word:
                selectWord(anchor_, to);
                break;
            case DragMode::Line:
                selection_ = Selection{CellPos{anchor_.row, 0}, CellPos{to.row, cols_}};
                break;
        }
    }

    // Snap both ends of a drag to word boundaries so dragging across a path
    // selects whole words at both ends.
    void selectWord(const CellPos& from, const CellPos& to) {
        int from_start = from.col, from_end = from.col;
        int to_start = to.col, to_end = to.col;
        expandToWord(cols_, from.col, [&](int c) { return kindAt(from.row, c); },
                     from_start, from_end);
        expandToWord(cols_, to.col, [&](int c) { return kindAt(to.row, c); }, to_start, to_end);

        if (from.row <= to.row)
            selection_ = Selection{CellPos{from.row, from_start}, CellPos{to.row, to_end}};
        else
            selection_ = Selection{CellPos{from.row, from_end}, CellPos{to.row, to_start}};
    }

    void clearSelection() {
        if (selection_.empty() && !dragging_)
            return;
        selection_ = Selection{};
        dragging_ = false;
        update();
    }

    // Copy on demand: Ctrl+Shift+C. Both modes are set so a middle click in
    // another application and a Ctrl+Shift+V in this one both find the same text.
    void copySelection() {
        const std::string text = selectedText();
        if (text.empty())
            return;
        QClipboard* clip = QGuiApplication::clipboard();
        if (!clip)
            return;
        const QString qtext = QString::fromStdString(text);
        clip->setText(qtext, QClipboard::Clipboard);
        if (clip->supportsSelection())
            clip->setText(qtext, QClipboard::Selection);
    }

    void pasteFromClipboard() {
        QClipboard* clip = QGuiApplication::clipboard();
        if (!clip)
            return;
        QString text;
        if (clip->supportsSelection())
            text = clip->text(QClipboard::Selection);
        if (text.isEmpty())
            text = clip->text(QClipboard::Clipboard);
        pasteText(text.toStdString());
    }
};

} // namespace kai
