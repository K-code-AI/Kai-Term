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
#include <QShowEvent>
#include <QTimerEvent>
#include <QColor>
#include <QRectF>
#include <QPen>
#include <QByteArray>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <functional>
#include <QSizeF>

#include <algorithm>
#include <atomic>
#include <cctype>
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

        // The screen the backend is already holding, before its reader is let loose: a
        // window built around a session should have that session's screen the moment it
        // exists, not whenever an event finally reaches a widget that may not be shown yet.
        feedWhatIsHeld();
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
                // Bytes the backend is already holding are not waiting on the
                // descriptor to become readable, so they are taken without asking.
                // A session restored into a window arrives in the handshake that
                // opens the connection and then sits in the backend's own buffer:
                // waiting on the socket anyway left that window blank until something
                // made the shell talk, and the whole screen appeared on the first
                // keypress. It looked like a painting problem and was a reading one.
                if (!backend->hasBuffered()) {
                    // Wait on the descriptor rather than blocking in read(): a stray
                    // job that inherited the terminal can keep the stream open after
                    // the shell itself is gone, and the window has to close on the
                    // shell leaving, not on whoever else lets go.
                    struct pollfd watched {backend->descriptor(), POLLIN, 0};
                    const int ready = ::poll(&watched, 1, 200);
                    if (ready < 0 && errno == EINTR)
                        continue;
                    if (ready == 0)
                        continue;
                }
                if (backend->collectsChild() && backend->collect()) {
                    // The shell is gone. Anything it printed on the way out has
                    // already been read and queued as an event, so there is
                    // nothing left to wait for.
                    shell_alive_ = false;
                    QCoreApplication::postEvent(this, new TerminalExitEvent(generation));
                    break;
                }
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

    // Say that this window is being closed and what it is showing was not kept, so
    // that the teardown in the destructor ends the session rather than only leaving it.
    // Nothing else calls this. A window merely going away is not somebody throwing
    // work away, and a terminal that assumed it was would be losing builds.
    void discardOnClose() { closing_on_close_ = true; }

    // What is running out front in this window, when it is not just the shell. Empty
    // for a session owned by the daemon, which is the one place that can answer that
    // about a shell it owns; whoever is asking asks the daemon instead.
    std::string foregroundProgram() const { return backend_->foregroundJobName(); }

    void shutdown() {
        running_ = false;
        // The child goes first, and the reader is joined before the master is
        // closed. Closing a descriptor under a thread blocked reading it leaves
        // that number free to be handed to somebody else's open() while the read
        // is still in flight, so teardown waits for the reader to notice the end
        // of the stream on its own.
        backend_->stopProcess(closing_on_close_);
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
        // A dialog belongs to the shell it was asked about. The shell it was
        // asked about is gone; the answer is not owed to whoever arrives next.
        overlay_ = Overlay{};
        // The new shell is asked to fit this window rather than the size it was started
        // at: a session restored into a small window should be a small session.
        backend_->resize(rows_, cols_);
        feedWhatIsHeld();
        startReader();
        update();
        // Asked for again and again for a moment, because the one update() above is worth
        // nothing if it lands before the window has been handed a surface: a compositor
        // that is still decorating a fresh window swallows the repaint, and the screen the
        // session sent a moment later has nothing to be drawn into. The person sees a blank
        // window that fills in the instant a key is pressed — the keystroke is not what
        // fetches the content, it is only the next thing that asks for a paint. This asks
        // on its own until one has actually happened.
        if (repaint_timer_ == 0)
            repaint_timer_ = startTimer(kRepaintPollMs);
    }

    // What the window's own keybinds mean, set by whoever owns the window. The widget
    // does not know what a session is, does not draw a menu and has never heard of the
    // daemon; it knows Ctrl+Shift+S was pressed and that somebody asked to be told.
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

    // -----------------------------------------------------------------------
    // The terminal's own dialogs: the question asked when a window is closed
    // with something running in it, and the list of sessions you could go
    // back to. Both are drawn by this widget in the terminal's own font and
    // colours, and both take the keyboard while they are up, so they read as
    // part of the terminal rather than as a window from somewhere else that
    // happened to land on top of it. A dialog that arrives in its own native
    // window brings its own theme, font and button order with it, and the
    // first thing it does is remind you that the terminal is a rectangle the
    // desktop happens to be drawing.
    //
    // The answer comes back as an index into the choices, or -1 when the
    // question went away without one being given — Esc, or a click outside
    // the panel, both of which mean "not now" and nothing more.
    // -----------------------------------------------------------------------

    // A question, some detail under it (which may be empty — one clean line is
    // often the whole question), and a row of buttons. The arrow keys or Tab
    // walk the buttons; the first letter of an answer picks it from wherever
    // the focus is, because a person who can read "Yes" should not have to
    // walk to it. `focus` is the button that starts out highlighted, and it
    // is worth choosing with care: Enter fires it, so the safe answer is the
    // one that should sit under the finger.
    void askInTerminal(const std::string& question, const std::string& detail,
                       const std::vector<std::string>& choices, int focus,
                       std::function<void(int)> answer) {
        overlay_ = Overlay{};
        overlay_.active = true;
        overlay_.lines.push_back(question);
        if (!detail.empty())
            overlay_.lines.push_back(detail);
        overlay_.choices = choices;
        if (focus >= 0 && focus < static_cast<int>(choices.size()))
            overlay_.selected = focus;
        overlay_.answer = std::move(answer);
        update();
    }

    // A title and a list of entries walked with Up/Down (or j/k), jumped to
    // with the number keys, chosen with Enter and put away with Esc — the
    // way every TUI picker works, because a menu you can only point at is a
    // menu that stops working the moment the pointer is elsewhere.
    void chooseInTerminal(const std::string& title, const std::vector<std::string>& entries,
                          std::function<void(int)> answer) {
        overlay_ = Overlay{};
        overlay_.active = true;
        overlay_.vertical = true;
        overlay_.lines.push_back(title);
        overlay_.choices = entries;
        overlay_.hint = "up/down choose · enter go · esc close · 1-9 jump";
        overlay_.answer = std::move(answer);
        update();
    }

    bool overlayActive() const { return overlay_.active; }
    // What is being asked and what it is being asked with — exposed so the
    // words and the order of the answers can be checked without an eye on a
    // screen, and so a screenshot tool can photograph a dialog on purpose.
    std::string overlayQuestion() const {
        return overlay_.lines.empty() ? std::string() : overlay_.lines.front();
    }
    const std::vector<std::string>& overlayChoices() const { return overlay_.choices; }
    int overlaySelected() const { return overlay_.selected; }

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
        return scrollBarGeometry(QSize(width(), height()), scrollRange(), rows_,
                                 scroll_offset_);
    }
    // How far the view could scroll back right now, which is the only number the view
    // should ever clamp itself against. An application that asked for the whole screen
    // (vim, less, btop, tmux) is drawing into a grid with no history above it, and the
    // history it cannot see belongs to the shell it is covering: offering it as
    // scrollback would show a full-screen program's viewer a screenful of somebody
    // else's text, and offer a scroll bar for a buffer that is not the one on screen.
    // So the answer is zero, and everything here — the indicator, the wheel,
    // Shift+PageUp, dragging the thumb, dragging to an edge — asks this rather than the
    // model, which is why it is one function and not a check in each place.
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
        const double pad = activePadding();
        return QPointF(pad + col * cellWidth(), pad + row * cellHeight());
    }

    // How far the grid is inset right now. An ordinary shell keeps the margin:
    // a borderless Wayland window has rounded corners, and a glyph in the corner
    // cell is clipped by the compositor rather than read. A full-screen
    // application holding the alternate screen wants the whole window — it
    // paints its own frame and knows where its own glyphs have to stay clear
    // of — so the inset goes to zero while it holds the screen, and the grid
    // grows to take the room the margin used to hold. The clash it removes is
    // a real one: the margin is the terminal's background, and btop's blue
    // inside a grey-black ring is two programs drawing one window.
    double activePadding() const {
        return screen_.inAlternateScreen() ? 0.0 : padding_;
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

    // A screen we are already holding does not need to be sent to us. A session's first
    // screen sits in the backend the moment its connection is made, and the only route
    // from there into the model was a reader thread posting an event to this widget —
    // which is worth nothing while the widget has not been shown, because a hidden widget
    // does not go looking at its own posted events. That is the black window on re-attach:
    // the bytes were there, the model never saw them, and the first keystroke was the next
    // thing to touch the queue. So the first screen is fed here, on the GUI thread, before
    // anybody is asked to paint.
    void feedWhatIsHeld() {
        if (!backend_)
            return;
        const std::string all = backend_->heldBytes();
        if (all.empty())
            return;
        backend_->clearHeldBytes();
        screen_.feed(all);
        if (auto title = screen_.takeTitle())
            setWindowTitle(QString::fromStdString(*title));
        if (screen_.inAlternateScreen())
            scroll_offset_ = 0;
        // A session can arrive already inside a full-screen program, and the
        // grid it was built for was inset for a shell it is not showing.
        syncGridToScreen();
        update();
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

            // The same bytes that changed whose screen this is may have changed
            // how much room the grid has: an application taking the screen over
            // gets the margin back and grows into it.
            syncGridToScreen();

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
            // The falling edge is never throttled. A block that ends less than an interval
            // after the repaint taken at its start used to be dropped — and the comment
            // here promised for years that the finished frame is painted at the end of the
            // block, which is the very frame the throttle was throwing away. A session's
            // screen arrives as one such block: painted empty at the start, filled in
            // inside the interval, and never asked to be drawn again. The person sees a
            // blank window until a keystroke asks on the screen's behalf.
            const bool block_ended = synced_block_open && !screen_.synchronizedOutput();
            synced_block_open = screen_.synchronizedOutput();
            if (block_ended
                || repaintAllowed(screen_.synchronizedOutput(), now_ms - last_repaint_ms_)) {
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

        // While one of the terminal's own dialogs is up it owns the keyboard.
        // Every key belongs to it and nothing reaches the shell: a stray
        // letter typed at a question would otherwise land in the command line
        // underneath, and pressing Enter at a "Quit while make is running?"
        // must mean the highlighted answer, not a newline someone else has to
        // explain.
        if (overlay_.active) {
            overlayKeyPress(key, mods);
            return;
        }

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
            // Ctrl+Shift+S keeps the shell this window is showing and starts a fresh
            // one in its place; Ctrl+Shift+A asks for the list of kept sessions. Both
            // belong to the window and never reach the shell: sent on, Ctrl+Shift+A
            // would be a control character at the prompt and Ctrl+Shift+S at least a
            // pause in flow control, both the opposite of what was asked.
            //
            // Here Qt can tell Ctrl+Shift+S from Ctrl+S, so plain Ctrl+S is left alone
            // and goes to the shell, where it means what it always meant. A plain
            // terminal cannot tell the two apart at all — a control letter arrives as
            // one byte and Shift is not in it — which is why the attached view speaks of
            // Ctrl+S and this window speaks of Ctrl+Shift+S. The letter is the same on
            // purpose: it is the one that means keep, in both places.
            if (key == Qt::Key_S && on_save_session) {
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
                scrollBy(scrollRange());
            else
                scrollBy(-scrollRange());
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
        // A dialog is up: the press either answers it or dismisses it, and it
        // is never a selection drag or a scroll-bar grab pretending not to be.
        if (overlay_.active) {
            overlayPress(ev->position());
            return;
        }
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
        if (overlay_.active) {
            overlayHover(ev->position());
            return;
        }
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
        if (pos.row <= 0 && scroll_offset_ < scrollRange())
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
        // The view does not scroll out from under a dialog: while one is up
        // the wheel has nothing to do, and saying so is better than letting
        // the question float away from the text behind it.
        if (overlay_.active) {
            ev->accept();
            return;
        }
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

    // A window being shown is a window about to be handed a surface by something that has
    // not finished its own work yet. From this moment a paint is wanted until one lands.
    void showEvent(QShowEvent* e) override {
        QWidget::showEvent(e);
        shown_paint_count_ = paint_count_;
        if (repaint_timer_ == 0)
            repaint_timer_ = startTimer(kRepaintPollMs);
        repaint_deadline_.invalidate();
    }

    // The asking for a paint, for as long as one has not happened. It stops by itself once
    // the window has drawn, so a window that was shown normally is not left redrawing
    // itself for no reason, and a window that was shown into a surface that was not there
    // yet keeps asking until something appears. It gives up after a while too: a window
    // nobody ever looks at is not worth asking forever.
    void timerEvent(QTimerEvent* e) override {
        if (e->timerId() != repaint_timer_) {
            QWidget::timerEvent(e);
            return;
        }
        if (paint_count_ > shown_paint_count_) {
            killTimer(repaint_timer_);
            repaint_timer_ = 0;
            return;
        }
        if (!repaint_deadline_.isValid())
            repaint_deadline_.start();
        if (repaint_deadline_.elapsed() > kRepaintAskMs) {
            killTimer(repaint_timer_);
            repaint_timer_ = 0;
            return;
        }
        update();
    }

    void resizeEvent(QResizeEvent*) override {
        applyGridGeometry();
    }

    // Work out how many rows and columns fit the window at the padding the
    // screen is in — zero for a full-screen application, the margin for a
    // shell — and tell the model and the shell when that changed. Called on
    // every resize and every change of hands between the shell and an
    // application taking the screen over, because both change the room.
    void applyGridGeometry() {
        QFontMetricsF fm(font_);
        double cw = fm.horizontalAdvance(QChar('M'));
        if (cw <= 0.0)
            cw = fm.maxWidth();
        const double ch = fm.height();
        if (cw <= 0.0 || ch <= 0.0)
            return;
        updateCellPixels(); // the cell size may have changed with a different font

        const double pad = activePadding();
        geometry_padding_ = pad;
        const int new_cols = std::max(1, static_cast<int>((width() - 2.0 * pad) / cw));
        const int new_rows = std::max(1, static_cast<int>((height() - 2.0 * pad) / ch));
        if (new_rows == rows_ && new_cols == cols_) {
            update();
            return;
        }

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

    // The screen changing hands between the shell and an application taking the
    // whole thing over changes the room the grid has, because the margin comes
    // and goes with it. Called after anything that feeds the model, and cheap
    // when nobody changed hands: one comparison against the padding the
    // geometry was last built for.
    void syncGridToScreen() {
        if (screen_.inAlternateScreen() == (geometry_padding_ == 0.0))
            return;
        applyGridGeometry();
    }

    void paintEvent(QPaintEvent*) override {
        ++paint_count_;
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
        // Where the grid starts: inset for a shell, flush for the application
        // holding the alternate screen.
        const double grid_pad = activePadding();
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
                const double x = grid_pad + c * cw;
                const double y = grid_pad + r * ch;

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
            const double x = grid_pad + cx * cw;
            const double y = grid_pad + cy * ch;
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

        // A dialog, if one is up, sits over everything: it is the thing being
        // read at this moment, and it belongs to this terminal rather than to
        // the desktop around it.
        paintOverlay(p);
    }

private:
    // -----------------------------------------------------------------------
    // The dialog itself: state, keys, pointer, geometry and paint.
    //
    // The shape is deliberately small — a few lines of text and a set of
    // choices — because everything richer wants layout machinery, and layout
    // machinery is how a terminal ends up looking like a form letter. Two
    // shapes cover what a terminal actually asks: a question with a row of
    // buttons, and a list you walk.
    // -----------------------------------------------------------------------
    struct Overlay {
        bool active = false;
        std::vector<std::string> lines;   // the question, or a title and its detail
        std::vector<std::string> choices; // the buttons, or the rows to choose between
        bool vertical = false;            // a list walked with Up/Down, not a button row
        int selected = 0;
        std::string hint;                 // the keys, said once, under the list
        std::function<void(int)> answer;  // the index picked; -1 means it went away
    };

    // Where the panel and each choice landed, computed the same way for the
    // painter and for the pointer, so a click hits what it is sitting on.
    struct OverlayLayout {
        QRectF panel;
        std::vector<QString> lines;   // wrapped, as they are painted
        int question_lines = 1;       // how many of those belong to the first source line
        std::vector<QString> choices; // one per entry of Overlay::choices
        std::vector<QRectF> boxes;    // one per choice, in the same order
        QRectF hint;
    };

    // Break a paragraph into lines that fit `max_width`, on spaces. The
    // detail line of a question is long enough that it has to wrap, and a
    // dialog whose text runs off its own panel is a dialog you cannot read.
    static std::vector<QString> wrapLine(const QString& text, double max_width,
                                         const QFontMetricsF& fm) {
        std::vector<QString> out;
        QString current;
        const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString& word : words) {
            const QString trial =
                current.isEmpty() ? word : current + QLatin1Char(' ') + word;
            if (!current.isEmpty() && fm.horizontalAdvance(trial) > max_width) {
                out.push_back(current);
                current = word;
            } else {
                current = trial;
            }
        }
        if (!current.isEmpty() || out.empty())
            out.push_back(current);
        return out;
    }

    OverlayLayout overlayLayout() const {
        OverlayLayout lay;
        QFontMetricsF fm(font_);
        const double cw = cellWidth();
        const double ch = cellHeight();
        const double pad = 1.5 * ch;
        // A dialog nearly as wide as the window is a wall; one barely wider
        // than its text cannot hold a wrapped line. Somewhere near five
        // sixths of the width, and never wider than the text needs.
        const double inner_max = std::max(240.0, width() * 0.8) - 2 * pad;

        double inner_w = 0.0;
        for (std::size_t i = 0; i < overlay_.lines.size(); ++i) {
            const std::vector<QString> wrapped =
                wrapLine(QString::fromStdString(overlay_.lines[i]), inner_max, fm);
            if (i == 0)
                lay.question_lines = static_cast<int>(wrapped.size());
            for (const QString& piece : wrapped) {
                inner_w = std::max(inner_w, fm.horizontalAdvance(piece));
                lay.lines.push_back(piece);
            }
        }
        for (const std::string& choice : overlay_.choices) {
            const QString label = QString::fromStdString(choice);
            inner_w = std::max(inner_w, fm.horizontalAdvance(label) + 2 * cw);
            lay.choices.push_back(label);
        }
        if (!overlay_.hint.empty())
            inner_w = std::max(inner_w, fm.horizontalAdvance(QString::fromStdString(overlay_.hint)));

        const double lines_h = lay.lines.size() * ch;
        const double gap = 0.9 * ch; // between the text and the choices
        const double box_h = overlay_.vertical ? ch + 6.0 : ch + 8.0;
        const double row_gap = 2.0 * cw; // between buttons, horizontally
        const int n = static_cast<int>(lay.choices.size());
        double choices_w = 0.0;
        if (n > 0) {
            if (overlay_.vertical) {
                choices_w = inner_w; // rows are full-width bands, not per-label boxes
            } else {
                for (const QString& label : lay.choices)
                    choices_w += fm.horizontalAdvance(label) + 2 * cw;
                choices_w += (n - 1) * row_gap;
            }
        }
        const double choices_h = n > 0 ? n * box_h + (n - 1) * (overlay_.vertical ? 3.0 : 0.0) : 0.0;
        const double hint_h = overlay_.hint.empty() ? 0.0 : 0.5 * ch + ch;

        const double panel_w = std::max({inner_w, choices_w}) + 2 * pad;
        const double panel_h = pad + lines_h + (n > 0 || hint_h > 0 ? gap : 0.0) + choices_h +
                               hint_h + pad;
        lay.panel = QRectF((width() - panel_w) / 2.0, (height() - panel_h) / 2.0, panel_w,
                           panel_h);

        double y = lay.panel.y() + pad + lines_h + (n > 0 || hint_h > 0 ? gap : 0.0);
        if (n > 0 && !overlay_.vertical) {
            double x = lay.panel.center().x() - choices_w / 2.0;
            for (std::size_t i = 0; i < lay.choices.size(); ++i) {
                const double w = fm.horizontalAdvance(lay.choices[i]) + 2 * cw;
                lay.boxes.push_back(QRectF(x, y, w, box_h));
                x += w + row_gap;
            }
        } else {
            for (int i = 0; i < n; ++i) {
                lay.boxes.push_back(QRectF(lay.panel.x() + pad, y, inner_w, box_h));
                y += box_h + 3.0;
            }
        }
        if (!overlay_.hint.empty())
            lay.hint = QRectF(lay.panel.x(), lay.panel.bottom() - pad - ch, panel_w, ch);
        return lay;
    }

    // Hand the answer over and put the dialog away before the callback runs:
    // the callback may close the window or switch the session, and either
    // way it should not find a live overlay pointing back at itself.
    void finishOverlay(int which) {
        const std::function<void(int)> answer = std::move(overlay_.answer);
        overlay_ = Overlay{};
        update();
        if (answer)
            answer(which);
    }

    void overlayKeyPress(int key, Qt::KeyboardModifiers mods) {
        const int count = static_cast<int>(overlay_.choices.size());
        const bool plain = mods == Qt::NoModifier || mods == Qt::KeypadModifier;
        auto step = [this, count](int delta) {
            if (count == 0)
                return;
            overlay_.selected = (overlay_.selected + delta + count) % count;
            update();
        };

        if (key == Qt::Key_Escape || (key == Qt::Key_G && mods.testFlag(Qt::ControlModifier))) {
            finishOverlay(-1);
            return;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            finishOverlay(count > 0 ? overlay_.selected : -1);
            return;
        }
        if (key == Qt::Key_Tab) {
            step(1);
            return;
        }
        if (key == Qt::Key_Backtab) {
            step(-1);
            return;
        }

        if (overlay_.vertical) {
            if (key == Qt::Key_Up || (plain && key == Qt::Key_K)) {
                step(-1);
                return;
            }
            if (key == Qt::Key_Down || (plain && key == Qt::Key_J)) {
                step(1);
                return;
            }
            if (key == Qt::Key_Home) {
                overlay_.selected = 0;
                update();
                return;
            }
            if (key == Qt::Key_End && count > 0) {
                overlay_.selected = count - 1;
                update();
                return;
            }
            // A numbered list is picked by number, the way every picker list
            // has ever worked: one key instead of one key per row skipped.
            if (plain && key >= Qt::Key_1 && key <= Qt::Key_9) {
                const int which = key - Qt::Key_1;
                if (which < count)
                    finishOverlay(which);
                return;
            }
            return;
        }

        if (key == Qt::Key_Left || (plain && key == Qt::Key_H)) {
            step(-1);
            return;
        }
        if (key == Qt::Key_Right || (plain && key == Qt::Key_L)) {
            step(1);
            return;
        }
        // A letter whose first letter is an answer answers it, wherever the
        // focus happens to be: at a question with Yes and No in front of you,
        // Y and N are answers, and making somebody walk a button they can read
        // is a detour invented by toolkits. It runs after the movement keys
        // above so that a plain `l` still moves when no answer starts with it.
        if (plain && key >= Qt::Key_A && key <= Qt::Key_Z) {
            for (std::size_t i = 0; i < overlay_.choices.size(); ++i) {
                const std::string& label = overlay_.choices[i];
                if (!label.empty() &&
                    std::toupper(static_cast<unsigned char>(label[0])) ==
                        static_cast<char>(key)) {
                    finishOverlay(static_cast<int>(i));
                    return;
                }
            }
        }
    }

    void overlayPress(const QPointF& where) {
        const OverlayLayout lay = overlayLayout();
        for (std::size_t i = 0; i < lay.boxes.size(); ++i) {
            if (lay.boxes[i].contains(where)) {
                finishOverlay(static_cast<int>(i));
                return;
            }
        }
        if (lay.panel.contains(where))
            return; // a click inside the panel that hit no choice chooses nothing
        finishOverlay(-1); // a click outside it is the same word as Esc
    }

    // Over a list, the pointer is a selector: where it rests is what Enter
    // would choose, the same rule every TUI menu follows.
    void overlayHover(const QPointF& where) {
        if (!overlay_.vertical)
            return;
        const OverlayLayout lay = overlayLayout();
        for (std::size_t i = 0; i < lay.boxes.size(); ++i) {
            if (!lay.boxes[i].contains(where))
                continue;
            if (overlay_.selected != static_cast<int>(i)) {
                overlay_.selected = static_cast<int>(i);
                update();
            }
            return;
        }
    }

    void paintOverlay(QPainter& p) const {
        if (!overlay_.active)
            return;
        const OverlayLayout lay = overlayLayout();
        QFontMetricsF fm(font_);
        const Color bg = screen_.defaultBackground();
        const Color fg = screen_.defaultForeground();

        // The panel is the terminal's own surface — its background, dimmed a
        // little so the grid behind it reads as "behind" — with the terminal's
        // own font on it. Nothing about it belongs to the desktop, which is
        // the entire point of drawing the question here rather than in a
        // window the window manager decorates for you.
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(fg.r, fg.g, fg.b, 80), 1));
        p.setBrush(QColor(std::clamp(static_cast<int>(bg.r * 0.62), 0, 255),
                          std::clamp(static_cast<int>(bg.g * 0.62), 0, 255),
                          std::clamp(static_cast<int>(bg.b * 0.62 + 12), 0, 255), 255));
        p.drawRoundedRect(lay.panel, kScrollBarRadius + 1, kScrollBarRadius + 1);

        p.setRenderHint(QPainter::Antialiasing, false);
        p.setFont(font_);
        double y = lay.panel.y() + 1.5 * cellHeight() + fm.ascent();
        for (std::size_t i = 0; i < lay.lines.size(); ++i) {
            // The question is said at full strength; the detail under it is a
            // quieter voice, so the eye goes to the question first.
            const bool detail = static_cast<int>(i) >= lay.question_lines;
            p.setPen(detail ? QColor(fg.r, fg.g, fg.b, 190) : QColor(fg.r, fg.g, fg.b));
            p.drawText(QRectF(lay.panel.x(), y - fm.ascent(), lay.panel.width(), fm.height()),
                       Qt::AlignHCenter | Qt::AlignVCenter, lay.lines[i]);
            y += cellHeight();
        }

        for (std::size_t i = 0; i < lay.boxes.size(); ++i) {
            const bool focused = static_cast<int>(i) == overlay_.selected;
            if (overlay_.vertical) {
                // The focused row wears the same band as a selection made
                // with the pointer, because it means the same thing: this is
                // the line you are on.
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setPen(Qt::NoPen);
                p.setBrush(focused ? QColor(62, 96, 168) : QColor(0, 0, 0, 0));
                if (focused)
                    p.drawRoundedRect(lay.boxes[i], kScrollBarRadius, kScrollBarRadius);
                p.setRenderHint(QPainter::Antialiasing, false);
                p.setPen(focused ? QColor(240, 240, 240) : QColor(fg.r, fg.g, fg.b, 225));
                p.drawText(lay.boxes[i].adjusted(8, 0, -8, 0),
                           Qt::AlignLeft | Qt::AlignVCenter, lay.choices[i]);
                continue;
            }
            // Buttons, kitty's way: no fill to speak of, a rule around the
            // word, and the highlighted one lit in blue rather than painted
            // over, so the row reads as part of the screen it is on.
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(focused ? QColor(110, 160, 255) : QColor(fg.r, fg.g, fg.b, 150), 1));
            p.setBrush(focused ? QColor(110, 160, 255, 40) : QColor(0, 0, 0, 0));
            p.drawRoundedRect(lay.boxes[i], kScrollBarRadius + 1, kScrollBarRadius + 1);
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(focused ? QColor(240, 244, 255) : QColor(fg.r, fg.g, fg.b, 200));
            p.drawText(lay.boxes[i], Qt::AlignCenter, lay.choices[i]);
        }

        if (!lay.hint.isEmpty()) {
            p.setPen(QColor(fg.r, fg.g, fg.b, 140));
            p.drawText(lay.hint, Qt::AlignHCenter | Qt::AlignVCenter,
                       QString::fromStdString(overlay_.hint));
        }
    }

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
        const int offset = scrollOffsetForThumbY(top, scroll_bar_, scrollRange(), rows_);
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
    // How many paints this window has ever been asked to do, and how many it had done the
    // last time it was shown. A window asked to draw before it has a surface does the work
    // and nothing comes of it; these two say whether anything has been drawn since.
    qint64 paint_count_ = 0;
    qint64 shown_paint_count_ = 0;
    int repaint_timer_ = 0;
    bool synced_block_open = false;
    QElapsedTimer repaint_deadline_;
    static constexpr int kRepaintPollMs = 120;
    static constexpr int kRepaintAskMs = 3000;
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
    Overlay overlay_;
    bool closing_on_close_ = false;
    double padding_ = 10.0;
    // The padding the current rows/cols were computed with. It is the memory of
    // whose screen this is — the shell's, inset, or a full-screen application's,
    // edge to edge — and the thing `syncGridToScreen()` compares against to
    // notice that the screen changed hands.
    double geometry_padding_ = padding_;

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
        // Against what this view may scroll, not against what the model is holding: a
        // full-screen application's rows are not in this view's reach, and the offset
        // has to come back to zero when one takes the screen over.
        scroll_offset_ = std::clamp(scroll_offset_, 0, scrollRange());
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
        // The same inset the painter used, so a click at the edge of a
        // full-screen application's frame lands on the character it is on.
        const double pad = activePadding();
        CellPos out;
        out.row = std::clamp(static_cast<int>((pos.y() - pad) / ch), 0, rows_ - 1);
        // A click in the right-hand margin selects up to the end of the row,
        // which is column count, one past the last cell.
        out.col = std::clamp(static_cast<int>((pos.x() - pad) / cw), 0, cols_);
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
