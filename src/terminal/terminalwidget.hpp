#pragma once

#include "screen.hpp"
#include "keyencode.hpp"
#include "mouseencode.hpp"
#include "scrollbar.hpp"
#include "selection.hpp"
#include "text.hpp"
#include "backend.hpp"

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
#include <functional>
#include <QSizeF>

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <utility>

namespace kai {

// Both carry the number of the shell that produced them. A window can be pointed at a
// different shell at runtime, and bytes from the one it left behind must not be
// painted onto the screen of the one it is looking at now — an event queue is not a
// pipe, and it does not empty itself just because the writer stopped.
struct TerminalOutputEvent : QEvent {
    explicit TerminalOutputEvent(std::string data, int generation)
        : QEvent(QEvent::None), data(std::move(data)), generation(generation) {}
    std::string data;
    int generation;
};

struct TerminalExitEvent : QEvent {
    explicit TerminalExitEvent(int generation)
        : QEvent(QEvent::None), generation(generation) {}
    int generation;
};

class TerminalWidget : public QWidget {
public:
    explicit TerminalWidget(int rows = 24, int cols = 80, std::string shell = {})
        : TerminalWidget(rows, cols, std::make_unique<PtyBackend>(rows, cols, std::move(shell))) {
    }

    // A window showing a session that lives somewhere else. Everything past this
    // point is the same: bytes in, screen out, keys back — what differs is on the far
    // side of the backend.
    TerminalWidget(int rows, int cols, std::unique_ptr<TerminalBackend> backend)
        : backend_(std::move(backend)), screen_(rows, cols),
          rows_(rows), cols_(cols), running_(true), shell_alive_(true)
    {
        font_ = QFont("Monospace");
        font_.setPixelSize(14);
        updateCellPixels();
        setFocusPolicy(Qt::StrongFocus);
        // Any-event mouse reporting (?1003) needs motion with no button held.
        setMouseTracking(true);

        startReader();
    }

    // The thread that turns bytes from the far end into events for this window. Its own
    // method because a window can be shown a different shell, and the new one needs a
    // reader of its own. The generation number and the backend are both taken here, at
    // the start of a shell's life, so everything this reader reports afterwards belongs
    // to that shell — including anything it is holding when the window moves on.
    void startReader() {
        const int generation = generation_;
        TerminalBackend* const backend = backend_.get();
        running_ = true;
        shell_alive_ = true;
        reader_ = std::thread([this, backend, generation] {
            char buf[4096];
            while (running_ && shell_alive_) {
                // Wait on the descriptor rather than blocking in read(): a stray
                // job that inherited the terminal can keep the stream open after
                // the shell itself is gone, and the window has to close on the
                // shell leaving, not on whoever else lets go.
                struct pollfd watched {backend->descriptor(), POLLIN, 0};
                const int ready = ::poll(&watched, 1, 200);
                if (ready < 0 && errno == EINTR)
                    continue;
                if (backend->collectsChild() && backend->collect()) {
                    // The shell is gone. Anything it printed on the way out has
                    // already been read and queued as an event, so there is
                    // nothing left to wait for.
                    shell_alive_ = false;
                    QCoreApplication::postEvent(this, new TerminalExitEvent(generation));
                    break;
                }
                if (ready == 0)
                    continue;
                ssize_t n = backend->read(buf, sizeof(buf));
                if (n <= 0) {
                    // The shell closed the terminal. Collect it here so the exit
                    // status means something; the master stays open until
                    // shutdown has joined this thread. (A successful write is no
                    // sign of life — see Pty::write.)
                    if (backend->collectsChild())
                        backend->collect();
                    shell_alive_ = false;
                    QCoreApplication::postEvent(this, new TerminalExitEvent(generation));
                    break;
                }
                QCoreApplication::postEvent(
                    this, new TerminalOutputEvent(std::string(buf, static_cast<size_t>(n)),
                                                  generation));
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
        backend_->stopProcess();
        if (reader_.joinable())
            reader_.join();
        backend_->release();
        shell_alive_ = false;
    }

    ~TerminalWidget() override {
        shutdown();
    }

    // Point this window at a different shell, and stop showing the one it had.
    //
    // The order is the whole of this. The reader has to be joined before the backend it
    // reads from is destroyed; the shell on the far end has to be told whether this
    // window is closing or merely looking elsewhere, because that is what decides
    // whether an unsaved session outlives the window; and the screen has to be emptied,
    // because a window still showing the previous shell would be showing a session that
    // is not there. Anything the old shell sends after this is tagged with a generation
    // this window has stopped listening for.
    void showSession(std::unique_ptr<TerminalBackend> next, bool discard_current) {
        if (!next)
            return;
        running_ = false;
        backend_->stopProcess(discard_current); // also wakes the read the reader is in
        if (reader_.joinable())
            reader_.join();
        backend_->release();

        backend_ = std::move(next);
        ++generation_;
        screen_ = Screen(rows_, cols_);
        scroll_offset_ = 0;
        clearSelection();
        status_.clear();
        // The new shell is asked to fit this window rather than the size it was started
        // at: a session restored into a small window should be a small session.
        backend_->resize(rows_, cols_);
        startReader();
        update();
    }

    // What the window's own keybinds mean, set by whoever owns the window. The widget
    // does not know what a session is, does not draw a menu and has never heard of the
    // daemon; it knows Ctrl+Shift+D was pressed and that somebody asked to be told.
    std::function<void()> on_save_session;
    std::function<void()> on_session_menu;

    // One line the window says for itself, shown in the corner for a moment and then
    // gone. A keybind that keeps your work and says nothing is a keybind that gets
    // pressed twice, and then stopped being trusted.
    void setStatusText(const std::string& text, int show_ms = 4000) {
        status_ = text;
        if (!status_timer_) {
            status_timer_ = new QTimer(this);
            status_timer_->setSingleShot(true);
            connect(status_timer_, &QTimer::timeout, this, [this] {
                status_.clear();
                update();
            });
        }
        status_timer_->start(show_ms);
        update();
    }
    const std::string& statusText() const { return status_; }

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

    // Say where the panes are, so a drag stays inside the one it started in. An empty
    // list is the ordinary terminal: one pane, the whole screen. A host drawing its own
    // splits calls this with the cells each one owns; nothing else has to know it
    // exists.
    void setPanes(std::vector<PaneRect> panes) {
        panes_ = std::move(panes);
        // A layout change moves the walls around any selection already out there.
        // Pulling the ends in to the pane they started from beats leaving a highlight
        // that now runs through a neighbour, and the person can drag again.
        if (!selection_.empty()) {
            const PaneRect pane = paneForDrag(panes_, dragging_ ? anchor_ : selection_.anchor,
                                              rows_, cols_);
            selection_.anchor.row = pane.clampRow(selection_.anchor.row);
            selection_.head.row = pane.clampRow(selection_.head.row);
            selection_.anchor.col = pane.clampEndCol(selection_.anchor.col);
            selection_.head.col = pane.clampEndCol(selection_.head.col);
        }
        update();
    }

    const std::vector<PaneRect>& panes() const { return panes_; }

    // The current text selection, in display coordinates, and the text it covers.
    Selection selection() const { return selection_; }
    std::string selectedText() const {
        const PaneRect pane = paneForDrag(panes_, selection_.anchor, rows_, cols_);
        return selectionTextInPane(pane, selection_,
                                   [&](int r) -> const Row& { return displayRow(r); });
    }

    // Whether a cell is highlighted. The pane a selection started in clips it, because
    // the rows in the middle of a drag run to the end of the line, and past the divider
    // that line belongs to another shell.
    bool selectedAt(int row, int col) const {
        if (!selection_.contains(row, col))
            return false;
        return paneForDrag(panes_, selection_.anchor, rows_, cols_).contains(row, col);
    }

    // Geometry for anything that has to aim at a character: the size of one cell
    // and the pixel position of a cell's top-left corner. Tests use it to send
    // a mouse event at a specific piece of text; a scrollbar or tooltip would
    // want the same numbers.
    QSizeF cellSize() const { return QSizeF(cellWidth(), cellHeight()); }

    // How long a synchronized output block (?2026) may hold a repaint back. Long
    // enough that a redraw of a whole screen fits inside one block, short enough
    // that nothing on screen ever looks stuck.
    static constexpr qint64 kSyncRepaintIntervalMs = 100;

    // Whether this much time since the last repaint is enough to paint again. A
    // pure decision, public so the pacing can be asserted without racing a clock.
    static bool repaintAllowed(bool synchronized_output, qint64 ms_since_last_repaint) {
        return !synchronized_output || ms_since_last_repaint >= kSyncRepaintIntervalMs;
    }
    QPointF cellTopLeft(int row, int col) const {
        return QPointF(padding_ + col * cellWidth(), padding_ + row * cellHeight());
    }

    // Paste as if the text came from the clipboard: newlines become carriage
    // returns and the payload is fenced when the application asked for bracketed
    // paste. Exposed because the middle-click path needs a clipboard, and a test
    // should still be able to check what the terminal would send.
    void pasteText(const std::string& text) {
        if (text.empty() || !shell_alive_ || !backend_->alive())
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
            if (out->generation != generation_)
                return true; // belongs to a shell this window no longer shows
            const int history_before = screen_.historySize();
            const int view_top_before = viewTop();
            screen_.feed(out->data);

            // What an OSC sequence asked of the program around the model. This
            // runs before the replies are drained below, because an OSC 52 read
            // produces one and the application is waiting for it.
            if (auto title = screen_.takeTitle())
                setWindowTitle(QString::fromStdString(*title));
            // A copy request reaches us from the model when the session is a shell
            // started here, and from the daemon when it is a session elsewhere: the
            // program asked the same question either way.
            std::vector<Screen::ClipboardRequest> requests = screen_.takeClipboardRequests();
            for (auto& request : backend_->takeClipboardRequests())
                requests.push_back(std::move(request));
            for (const auto& request : requests) {
                const QClipboard::Mode mode =
                    request.primary ? QClipboard::Selection : QClipboard::Clipboard;
                if (request.query) {
                    // The application is asking to paste, over a link such as ssh
                    // where its own clipboard is not reachable. A session owned by the
                    // daemon is the same case with the link moved: the contents are
                    // here, in this window's clipboard, and the way back to the program
                    // is the byte stream it came up on. The answer has to be given
                    // either way — a view that ignored a read request would leave the
                    // program waiting, and one that mistook it for a write would empty
                    // the clipboard it was asked to read.
                    const std::string contents =
                        QGuiApplication::clipboard()->text(mode).toStdString();
                    if (backend_->answersQueries())
                        screen_.answerClipboardQuery(contents);
                    else
                        static_cast<void>(backend_->write(clipboardQueryReply(contents)));
                    continue;
                }
                // This is the application asking for its contents to be copied,
                // which is on demand like Ctrl+Shift+C is; selecting text in the
                // window still does not touch the clipboard by itself.
                QGuiApplication::clipboard()->setText(
                    QString::fromStdString(request.text), mode);
            }

            // Forward terminal-to-host query replies (DA1, DSR 5n/6n) to the PTY.
            // fish and tmux both probe the terminal at startup, and tmux uses
            // DSR 6n cursor position reports for output leak detection.
            const std::string response = screen_.takeResponse();
            if (!response.empty() && backend_->answersQueries())
                static_cast<void>(backend_->write(response));

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

            // ?2026 synchronized output: an application drawing a whole frame
            // writes it as one block and asks not to be shown half of it. Each read
            // from the PTY already ends in one repaint, but a burst can still land
            // mid-frame, so inside a block repaints are limited to one every
            // kSyncRepaintIntervalMs. The block ending is itself a chunk, so the
            // finished frame is painted then; and a program that turns the mode on
            // and dies cannot freeze the screen, because the next chunk repaints
            // once the interval has passed whether the mode is off or not.
            const qint64 now_ms = repaintClockElapsed();
            if (repaintAllowed(screen_.synchronizedOutput(), now_ms - last_repaint_ms_)) {
                last_repaint_ms_ = now_ms;
                update();
            }
            return true;
        }
        if (auto* leaving = dynamic_cast<TerminalExitEvent*>(e)) {
            // The shell this window has moved on from, saying goodbye late.
            if (leaving->generation != generation_)
                return true;
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
            // Ctrl+Shift+D keeps the shell this window is showing and starts a fresh
            // one in its place; Ctrl+Shift+A asks for the list of kept sessions. Both
            // belong to the window and never reach the shell: sent on, Ctrl+Shift+D
            // would be an end-of-file at the prompt and Ctrl+Shift+A a control
            // character, both of which do the opposite of what was asked.
            if (key == Qt::Key_D && on_save_session) {
                on_save_session();
                return;
            }
            if (key == Qt::Key_A && on_session_menu) {
                on_session_menu();
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

        if (!shell_alive_ || !backend_->alive())
            return;

        const std::string bytes = encodeKey(key, mods, ev->text(),
                                            screen_.applicationCursorKeys());
        if (bytes.empty())
            return;
        static_cast<void>(backend_->write(bytes));
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
            const PaneRect kept = paneForDrag(panes_, anchor_before, rows_, cols_);
            anchor_.row = kept.clampRow(anchor_before.row + (view_top_before - viewTop()));
        }
        // Crawling to the edge of the window stops at the edge of the pane: the rows
        // beyond it are somebody else's shell.
        const PaneRect pane = paneForDrag(panes_, anchor_, rows_, cols_);
        extendSelection(CellPos{direction > 0 ? pane.firstRow() : pane.lastRow(), edge_col_});
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

    // The model has no font and no way to measure one, but answers geometry
    // queries (XTWINOPS) in pixels, so it is told what a cell measures here.
    void updateCellPixels() {
        screen_.setCellPixels(static_cast<int>(cellWidth() + 0.5),
                              static_cast<int>(cellHeight() + 0.5));
    }

    void resizeEvent(QResizeEvent*) override {
        QFontMetricsF fm(font_);
        double cw = fm.horizontalAdvance(QChar('M'));
        if (cw <= 0.0)
            cw = fm.maxWidth();
        const double ch = fm.height();
        if (cw <= 0.0 || ch <= 0.0)
            return;
        updateCellPixels(); // the cell size may have changed with a different font

        const int new_cols = std::max(1, static_cast<int>((width() - 2.0 * padding_) / cw));
        const int new_rows = std::max(1, static_cast<int>((height() - 2.0 * padding_) / ch));
        if (new_rows == rows_ && new_cols == cols_)
            return;

        rows_ = new_rows;
        cols_ = new_cols;
        screen_.resize(rows_, cols_);
        backend_->resize(rows_, cols_);
        // Reflow moves text between rows and columns, so a highlight drawn before
        // the resize would be sitting over different characters afterwards. It is
        // dropped rather than left to drift onto text nobody selected.
        clearSelection();
        clampScroll();
        update();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const Color background = screen_.defaultBackground();
        p.fillRect(0, 0, width(), height(), QColor(background.r, background.g, background.b));

        QFontMetricsF fm(font_);
        double cw = fm.horizontalAdvance(QChar('M'));
        if (cw <= 0.0)
            cw = fm.maxWidth();
        const double ch = fm.height();
        if (cw <= 0.0 || ch <= 0.0)
            return;

        const Color default_bg = background; // an OSC 11 may have changed it
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
                if (selectedAt(r, c) && (cell.text.empty() || cell.width <= 0)) {
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
                if (selectedAt(r, c)) {
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

                // Underline, strike and overline are independent attributes, so
                // each draws its own line. Sharing one variable for the position
                // made the last of them win, which is how a struck-out word with an
                // underline on lost its underline.
                if (cell.attr.underline || cell.attr.strike || cell.attr.overline) {
                    p.setPen(QPen(QColor(fg.r, fg.g, fg.b), 1.0));
                    const double right = x + cw * cell.width;
                    if (cell.attr.underline)
                        p.drawLine(QPointF(x, y + ch - 1.5), QPointF(right, y + ch - 1.5));
                    if (cell.attr.strike)
                        p.drawLine(QPointF(x, y + ch / 2.0), QPointF(right, y + ch / 2.0));
                    if (cell.attr.overline)
                        p.drawLine(QPointF(x, y + 0.5), QPointF(right, y + 0.5));
                }
            }
        }

        // Visible terminal cursor; tmux hides it during redraws via ESC[?25l.
        // It belongs to the live screen, so it is not drawn over history. Its
        // shape is the application's to choose (DECSCUSR); the default is the
        // underline this terminal has always drawn.
        if (scroll_offset_ == 0 && screen_.cursorVisible()) {
            const int cx = std::clamp(screen_.cursorX(), 0, cols_ - 1);
            const int cy = std::clamp(screen_.cursorY(), 0, rows_ - 1);
            const Cell& under = screen_.at(cy, cx);
            const double x = padding_ + cx * cw;
            const double y = padding_ + cy * ch;
            const double span = cw * std::max(1, under.width);
            p.setPen(Qt::NoPen);
            switch (screen_.cursorShape()) {
                case Screen::CursorShape::Block:
                    // A block covers the glyph it is on, which is only readable if
                    // that glyph is drawn again on top in the colour it is now
                    // sitting on. Skip the second pass and the character under the
                    // cursor simply disappears — a block cursor that eats text.
                    p.setBrush(QColor(220, 220, 220));
                    p.drawRect(QRectF(x, y, span, ch));
                    if (!under.text.empty()) {
                        p.setPen(QColor(under.bg.r, under.bg.g, under.bg.b));
                        p.setBrush(Qt::NoBrush);
                        p.drawText(QPointF(x, y + fm.ascent()), QString::fromStdString(under.text));
                    }
                    break;
                case Screen::CursorShape::Bar:
                    p.setBrush(QColor(220, 220, 220, 210));
                    p.drawRect(QRectF(x, y, std::min(2.0, cw), ch));
                    break;
                case Screen::CursorShape::Underline:
                    p.setBrush(QColor(220, 220, 220, 210));
                    p.drawRect(QRectF(x, y + ch - 2.0, span, 2.0));
                    break;
            }
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

        // The window's own line, drawn last and over everything else, because it is a
        // word about the window rather than part of the shell inside it.
        if (!status_.empty()) {
            const QString line = QString::fromStdString(status_);
            const double box_w = fm.horizontalAdvance(line) + 2 * padding_;
            const double box_h = ch + padding_;
            const QRectF box(width() - box_w - padding_, height() - box_h - padding_, box_w,
                             box_h);
            p.setPen(QPen(QColor(220, 220, 220, 200), 1));
            p.setBrush(QColor(20, 20, 20, 235));
            p.drawRoundedRect(box, kScrollBarRadius, kScrollBarRadius);
            p.setPen(QColor(240, 240, 240));
            p.drawText(box, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, line);
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

    std::unique_ptr<TerminalBackend> backend_;
    Screen screen_;
    QFont font_;
    // Declared in the order the constructor initialises them: members are built
    // in declaration order whatever the initialiser list says, so keeping the two
    // in step is what makes the list safe to read.
    int rows_;
    int cols_;
    std::thread reader_;
    std::atomic<bool> running_;
    std::atomic<bool> shell_alive_;
    // Which shell the reader thread is reading. Raised when the window is pointed at
    // another one, so events from the old shell can be recognised and dropped.
    std::atomic<int> generation_{0};
    std::string status_;
    QTimer* status_timer_ = nullptr;
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
    // Where the panes are, when a host has said. Empty means the screen is one pane.
    std::vector<PaneRect> panes_;
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
    // Repaint pacing for ?2026, measured from the widget's own clock.
    QElapsedTimer repaint_clock_;
    qint64 last_repaint_ms_ = 0;

    qint64 repaintClockElapsed() {
        if (!repaint_clock_.isValid()) {
            repaint_clock_.start();
            return 0;
        }
        return repaint_clock_.elapsed();
    }

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
        if (bytes.empty() || !shell_alive_ || !backend_->alive())
            return;
        static_cast<void>(backend_->write(bytes));
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
    // character by character, whole words, or whole rows. Wherever it wants to go, it
    // stops at the edge of the pane the drag started in.
    void extendSelection(const CellPos& to) {
        const PaneRect pane = paneForDrag(panes_, anchor_, rows_, cols_);
        const CellPos inside{pane.clampRow(to.row), pane.clampEndCol(to.col)};
        switch (drag_mode_) {
            case DragMode::Char:
                selection_ = Selection{anchor_, inside};
                break;
            case DragMode::Word:
                selectWord(anchor_, inside, pane);
                break;
            case DragMode::Line:
                // A whole row means the row of the pane, not of the screen: the line
                // next door belongs to a different shell.
                selection_ = Selection{CellPos{anchor_.row, pane.firstCol()},
                                       CellPos{inside.row, pane.endCol()}};
                break;
        }
    }

    // Snap both ends of a drag to word boundaries so dragging across a path
    // selects whole words at both ends.
    void selectWord(const CellPos& from, const CellPos& to, const PaneRect& pane) {
        int from_start = from.col, from_end = from.col;
        int to_start = to.col, to_end = to.col;
        expandToWord(cols_, from.col, [&](int c) { return kindAt(from.row, c); },
                     from_start, from_end);
        expandToWord(cols_, to.col, [&](int c) { return kindAt(to.row, c); }, to_start, to_end);

        // A word that touches the boundary is still only ours up to the boundary:
        // where a neighbour's text begins is its business.
        from_start = std::max(pane.clampEndCol(from_start), pane.firstCol());
        from_end = pane.clampEndCol(from_end);
        to_start = std::max(pane.clampEndCol(to_start), pane.firstCol());
        to_end = pane.clampEndCol(to_end);

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
