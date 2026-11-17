// Offscreen widget exercise. This drives the real TerminalWidget: it starts a
// shell in a PTY, delivers synthetic Qt key and wheel events, lets the reader
// thread feed the screen model, and paints the widget into an image.
//
// What this proves: the widget code paths run without crashing, key events turn
// into bytes the shell acts on, output lands in the model, and scrolling back
// changes what is painted. It is not a visual inspection; nothing here checks
// whether the result looks right to a person.

#include "../src/terminal/mouseencode.hpp"
#include "../src/terminal/terminalwidget.hpp"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QWheelEvent>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

namespace {

int checks = 0;
int failures = 0;

void expect(const char* what, bool value) {
    ++checks;
    if (value)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << '\n';
}

void expectEq(const char* what, long long got, long long want) {
    ++checks;
    if (got == want)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << ": want " << want << " got " << got << '\n';
}

void expectStr(const char* what, const std::string& got, const std::string& want);

// Render control bytes readably so a failure says what actually went out.
std::string visible(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case 0x1b: out += "ESC"; break;
            case '\r': out += "CR"; break;
            case '\n': out += "LF"; break;
            case '\t': out += "TAB"; break;
            case 0x7f: out += "DEL"; break;
            default:
                if (c < 0x20 || c == 0x80) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "<%02X>", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

void expectStr(const char* what, const std::string& got, const std::string& want) {
    ++checks;
    if (got == want)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << ": want [" << visible(want) << "] got ["
              << visible(got) << "]\n";
}

void pump(int ms) {
    for (int i = 0; i < ms / 10; ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    QCoreApplication::processEvents();
}

std::string screenDump(const kai::TerminalWidget& w) {
    std::string out;
    const kai::Screen& s = w.screen();
    for (int r = 0; r < s.rows(); ++r) {
        for (int c = 0; c < s.cols(); ++c) {
            const std::string& t = s.at(r, c).text;
            out += t.empty() ? " " : t;
        }
        out += '\n';
    }
    return out;
}

bool waitFor(const std::function<bool()>& pred, int timeout_ms) {
    for (int waited = 0; waited < timeout_ms; waited += 20) {
        pump(20);
        if (pred())
            return true;
    }
    return pred();
}

void sendText(kai::TerminalWidget* w, const std::string& utf8) {
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier,
                 QString::fromUtf8(utf8.c_str(), static_cast<int>(utf8.size())));
    QCoreApplication::sendEvent(w, &ev);
}

void sendKey(kai::TerminalWidget* w, int key, Qt::KeyboardModifiers mods = Qt::NoModifier,
             const QString& text = QString()) {
    QKeyEvent ev(QEvent::KeyPress, key, mods, text);
    QCoreApplication::sendEvent(w, &ev);
}

// A wheel notch aimed at a pixel, which matters once the application is
// receiving wheel reports and needs to know which cell the turn was over.
void sendWheelAt(kai::TerminalWidget* w, int angle_delta_y, const QPointF& at) {
    QWheelEvent ev(at, at, QPoint(0, 0),
                   QPoint(0, angle_delta_y), Qt::NoButton, Qt::NoModifier,
                   Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(w, &ev);
}

void sendWheel(kai::TerminalWidget* w, int angle_delta_y) {
    sendWheelAt(w, angle_delta_y, QPointF(20.0, 20.0));
}

// A mouse event aimed at a pixel. `buttons` is what is held down while the
// event is delivered, which for a release is nothing.
void sendMouse(kai::TerminalWidget* w, QEvent::Type type, Qt::MouseButton button,
               Qt::MouseButtons buttons, const QPointF& at,
               Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QMouseEvent ev(type, at, at, button, buttons, mods);
    QCoreApplication::sendEvent(w, &ev);
}

// The middle of a cell, so a mouse event lands on the character it means.
QPointF cellPoint(kai::TerminalWidget* w, int row, int col) {
    const QPointF top_left = w->cellTopLeft(row, col);
    const QSizeF cell = w->cellSize();
    return QPointF(top_left.x() + cell.width() * 0.5, top_left.y() + cell.height() * 0.5);
}

// The first row whose text begins with `prefix`, or {-1, -1}. Matching the start
// of a row matters here: the shell echoes a command, so a word typed in a command
// line appears on screen above the line the command actually printed.
kai::CellPos findLine(const kai::TerminalWidget& w, const std::string& prefix) {
    const kai::Screen& s = w.screen();
    for (int r = 0; r < s.rows(); ++r) {
        std::string line;
        for (int c = 0; c < s.cols(); ++c) {
            const std::string& t = s.at(r, c).text;
            line += t.empty() ? " " : t;
        }
        const std::size_t start = line.find_first_not_of(' ');
        if (start == std::string::npos)
            continue;
        if (line.compare(start, prefix.size(), prefix) == 0)
            return kai::CellPos{r, static_cast<int>(start)};
    }
    return kai::CellPos{-1, -1};
}

// Where a piece of text first appears on the live screen, or {-1, -1}.
kai::CellPos findText(const kai::TerminalWidget& w, const std::string& needle) {
    const kai::Screen& s = w.screen();
    for (int r = 0; r < s.rows(); ++r) {
        std::string line;
        for (int c = 0; c < s.cols(); ++c) {
            const std::string& t = s.at(r, c).text;
            line += t.empty() ? " " : t;
        }
        const std::size_t found = line.find(needle);
        if (found != std::string::npos)
            return kai::CellPos{r, static_cast<int>(found)};
    }
    return kai::CellPos{-1, -1};
}

// The first row from the top carrying three plain characters, with the column
// they start at. The scroll tests need a selection that stays on screen when the
// view moves, so it cannot be the line at the bottom where commands run.
kai::CellPos topTextRow(const kai::TerminalWidget& w, std::string& text) {
    const kai::Screen& s = w.screen();
    for (int r = 0; r + 6 < s.rows(); ++r) {
        std::string line;
        for (int c = 0; c < s.cols(); ++c) {
            const std::string& t = s.at(r, c).text;
            line += t.empty() ? " " : t;
        }
        const std::size_t start = line.find_first_not_of(' ');
        if (start == std::string::npos)
            continue;
        // At least two characters with no space in them: a selection ending on a
        // space would be trimmed on extraction and say nothing about where the
        // drag stopped. Numbered lines from `seq` are two characters wide.
        std::size_t run = start;
        while (run < line.size() && line[run] != ' ')
            ++run;
        if (run - start < 2)
            continue;
        text = line.substr(start, std::min<std::size_t>(run - start, 3));
        return kai::CellPos{r, static_cast<int>(start)};
    }
    text.clear();
    return kai::CellPos{-1, -1};
}

std::string capturePath() {
    return "/tmp/kai-widget-capture-" + std::to_string(static_cast<long>(::getpid())) + ".bin";
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Capture exactly what the widget writes to the PTY while `work` runs.
//
// The shell puts the tty in raw mode with echo off and hands stdin to `cat`,
// which copies it to a file until `timeout` ends it. The bytes therefore come
// from the same descriptor a program inside the terminal would read them from,
// which is the only ground truth available for what a mouse report or a paste
// actually looks like on the wire.
//
// `setup` is printed as a shell command run before cat starts, so the shell is
// still reading commands when it changes terminal modes.
std::string captureToPty(kai::TerminalWidget* w, const std::string& setup,
                         const std::function<void()>& work) {
    const std::string path = capturePath();
    std::remove(path.c_str());
    sendText(w, setup + "; stty raw -echo; timeout 2 cat > " + path);
    sendKey(w, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    pump(500); // let cat start
    work();
    pump(2400); // let `timeout` end it
    const std::string bytes = readFile(path);
    sendText(w, "stty sane");
    sendKey(w, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    pump(300);
    return bytes;
}

QImage grabImage(kai::TerminalWidget* w) {
    return w->grab().toImage().convertToFormat(QImage::Format_ARGB32);
}

long long differingPixels(const QImage& a, const QImage& b) {
    if (a.size() != b.size())
        return -1;
    long long n = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y))
                ++n;
        }
    }
    return n;
}

} // namespace

int main(int argc, char** argv) {
    // Force the offscreen platform so this runs with no display server.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // Whether this Qt platform offers a clipboard at all; the offscreen plugin is
    // free not to, and then the checks that need one are reported as skipped.
    bool clipboard_available = true;
    bool selection_supported = true;

    kai::TerminalWidget w(6, 40, "/bin/sh");
    w.resize(700, 500);
    w.show();
    pump(200);

    // The resize event re-derives the grid from font metrics, and the PTY is
    // told about it; the widget and the model must agree.
    expect("grid was recomputed from the widget size", w.rows() >= 2 && w.cols() >= 10);
    expectEq("widget rows match the model", w.rows(), w.screen().rows());
    expectEq("widget cols match the model", w.cols(), w.screen().cols());

    // Silence the prompt so the screen is static once output has arrived.
    sendText(&w, "PS1=''");
    sendKey(&w, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    expect("shell is running and echoing input", waitFor(
        [&] { return screenDump(w).find("PS1=") != std::string::npos; }, 4000));

    // Output longer than the screen must push lines into scrollback.
    sendText(&w, "seq 1 60");
    sendKey(&w, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    expect("typed command reached the shell and output was rendered", waitFor(
        [&] { return screenDump(w).find("60") != std::string::npos; }, 5000));
    expect("output beyond the screen created scrollback",
           waitFor([&] { return w.screen().historySize() > 0; }, 2000));
    expectEq("view starts at the live bottom", w.scrollOffset(), 0);

    const QImage bottom = grabImage(&w);
    expect("widget paints something", bottom.height() > 0 && bottom.width() > 0);

    // Scroll back with the wheel: history must become visible.
    sendWheel(&w, 120);
    expectEq("one wheel notch scrolls the configured rows", w.scrollOffset(), 3);
    expect("scroll range covers the offset", w.scrollOffset() <= w.scrollRange());
    const QImage scrolled = grabImage(&w);
    expect("scrolling back changes what is painted", differingPixels(bottom, scrolled) > 0);

    // Scroll back to the bottom and the paint must return to the original.
    sendWheel(&w, -120);
    expectEq("scrolling down returns to the live bottom", w.scrollOffset(), 0);
    const QImage back_at_bottom = grabImage(&w);
    expect("painting is stable once back at the bottom",
           differingPixels(bottom, back_at_bottom) == 0);

    // Keyboard scrollback: Shift+PageUp scrolls the view, Shift+End returns it,
    // and neither key is sent to the shell.
    sendKey(&w, Qt::Key_PageUp, Qt::ShiftModifier);
    expect("Shift+PageUp scrolls back by about a screen",
           w.scrollOffset() >= std::max(1, w.rows() - 1));
    sendKey(&w, Qt::Key_Home, Qt::ShiftModifier);
    expectEq("Shift+Home reaches the oldest line", w.scrollOffset(), w.scrollRange());
    expect("the oldest line is paintable", w.screen().historyRow(0) != nullptr);
    sendKey(&w, Qt::Key_End, Qt::ShiftModifier);
    expectEq("Shift+End returns to the bottom", w.scrollOffset(), 0);

    // Ordinary typing must snap the view back to the live bottom.
    sendKey(&w, Qt::Key_PageUp, Qt::ShiftModifier);
    expect("scrolled back before typing", w.scrollOffset() > 0);
    sendText(&w, "x");
    expectEq("typing returns the view to the live bottom", w.scrollOffset(), 0);

    // ---- Selecting text with the mouse -------------------------------------
    // Output has to be finished before a drag starts: new output clears a
    // selection so it cannot end up pointing at different text than it covered.
    // The section above left a character on the command line. Clear it first: a
    // command typed behind a stray character does not run, and its output never
    // reaches the screen for the mouse to select.
    sendKey(&w, Qt::Key_U, Qt::ControlModifier);
    pump(150);
    sendText(&w, "printf 'alpha bravo charlie\\n'");
    sendKey(&w, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    expect("text appeared for the mouse to select",
           waitFor([&] { return findLine(w, "alpha bravo charlie").row >= 0; }, 4000));
    const kai::CellPos alpha = findLine(w, "alpha bravo charlie");

    // Press and release in the same cell select nothing.
    sendMouse(&w, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton,
              cellPoint(&w, alpha.row, alpha.col));
    sendMouse(&w, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
              cellPoint(&w, alpha.row, alpha.col));
    expect("a click selects nothing", w.selection().empty());
    pump(500); // let the repeat-click window expire

    // A drag across five cells selects exactly those five characters. The
    // clipboard is loaded with a sentinel first: finishing a drag must leave it
    // alone, because a terminal that copied on release overwrote whatever the
    // user had put there.
    if (QClipboard* clip = QGuiApplication::clipboard())
        clip->setText(QStringLiteral("sentinel"), QClipboard::Clipboard);
    const QImage before_drag = grabImage(&w);
    sendMouse(&w, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton,
              cellPoint(&w, alpha.row, alpha.col));
    sendMouse(&w, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton,
              cellPoint(&w, alpha.row, alpha.col + 5));
    expect("a drag shows its selection before it is released", !w.selection().empty());
    sendMouse(&w, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
              cellPoint(&w, alpha.row, alpha.col + 5));
    expectStr("the drag selected the characters it covered", w.selectedText(), "alpha");
    expect("a selection changes what is painted",
           differingPixels(before_drag, grabImage(&w)) > 0);

    if (QClipboard* clip = QGuiApplication::clipboard()) {
        expectStr("finishing the drag leaves the clipboard alone",
                  clip->text(QClipboard::Clipboard).toStdString(), "sentinel");
        sendKey(&w, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        expectStr("Ctrl+Shift+C copies the selection",
                  clip->text(QClipboard::Clipboard).toStdString(), "alpha");
        expectStr("and Ctrl+Shift+C did not send Ctrl+C to the shell",
                  w.selectedText(), "alpha");
        if (clip->supportsSelection()) {
            expectStr("and put it in the X11-style selection as well",
                      clip->text(QClipboard::Selection).toStdString(), "alpha");
        } else {
            selection_supported = false;
        }
    } else {
        // The offscreen platform may provide no clipboard at all. Say so rather
        // than counting a check that never ran.
        clipboard_available = false;
    }

    // Shift+click moves the end of the selection instead of starting a new one.
    sendMouse(&w, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton,
              cellPoint(&w, alpha.row, alpha.col + 11), Qt::ShiftModifier);
    sendMouse(&w, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
              cellPoint(&w, alpha.row, alpha.col + 11), Qt::ShiftModifier);
    expectStr("Shift+click extends the selection to where it was clicked",
              w.selectedText(), "alpha bravo");
    pump(500);

    // A double click takes the whole word, though both clicks land inside it.
    const kai::CellPos bravo{alpha.row, alpha.col + 6}; // "bravo" starts at column 6
    for (int i = 0; i < 2; ++i) {
        sendMouse(&w, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton,
                  cellPoint(&w, bravo.row, bravo.col + 2));
        sendMouse(&w, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
                  cellPoint(&w, bravo.row, bravo.col + 2));
    }
    expectStr("a double click selects the whole word", w.selectedText(), "bravo");
    pump(500);

    // A triple click takes the line.
    for (int i = 0; i < 3; ++i) {
        sendMouse(&w, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton,
                  cellPoint(&w, bravo.row, bravo.col + 2));
        sendMouse(&w, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
                  cellPoint(&w, bravo.row, bravo.col + 2));
    }
    expectStr("a triple click selects the line", w.selectedText(), "alpha bravo charlie");
    pump(500);

    // ---- A selection stays on its text --------------------------------------
    // Scrolling moves the text under the highlight, so the highlight has to move
    // with it. One notch back is three lines: enough to see the highlight move,
    // not enough to push a line near the top out of the window.
    std::string held;
    const kai::CellPos top = topTextRow(w, held);
    expect("a line near the top of the screen can be selected",
           top.row >= 0 && held.size() >= 2);
    sendMouse(&w, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton,
              cellPoint(&w, top.row, top.col));
    sendMouse(&w, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton,
              cellPoint(&w, top.row, top.col + static_cast<int>(held.size())));
    sendMouse(&w, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
              cellPoint(&w, top.row, top.col + static_cast<int>(held.size())));
    expectStr("a drag selected the start of that line", w.selectedText(), held);
    sendWheelAt(&w, 120, cellPoint(&w, 10, 10));
    expectEq("one notch scrolled the view back three lines", w.scrollOffset(), 3);
    expectStr("scrolling keeps the selection on the same text", w.selectedText(), held);
    sendWheelAt(&w, -120, cellPoint(&w, 10, 10));
    expectEq("and scrolling forward returns to the bottom", w.scrollOffset(), 0);
    expectStr("the selection is still the same text", w.selectedText(), held);

    // Output moves it too. A background loop keeps writing lines while nothing
    // is typed, which is the case that used to throw the selection away: a log
    // you were copying from, still filling up.
    sendText(&w, "for i in $(seq 1 400); do echo tick$i; sleep 0.05; done &");
    sendKey(&w, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    expect("the background loop is writing",
           waitFor([&] { return findLine(w, "tick5").row >= 0; }, 4000));
    const kai::CellPos tick = findLine(w, "tick5");
    sendMouse(&w, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton,
              cellPoint(&w, tick.row, tick.col));
    sendMouse(&w, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton,
              cellPoint(&w, tick.row, tick.col + 5));
    sendMouse(&w, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
              cellPoint(&w, tick.row, tick.col + 5));
    expectStr("the drag selected the line the loop wrote", w.selectedText(), "tick5");
    pump(700); // the loop has written more lines in the meantime
    expectStr("new output does not steal the selection off its text",
              w.selectedText(), "tick5");
    expect("and the selection is dropped once its lines scroll away",
           waitFor([&] { return w.selection().empty(); }, 8000));
    sendText(&w, "kill %1");
    sendKey(&w, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    pump(400);

    // ---- Selecting while scrolled back -------------------------------------
    sendKey(&w, Qt::Key_PageUp, Qt::ShiftModifier);
    expect("scrolled back before selecting retained text", w.scrollOffset() > 0);
    const kai::Row* top_row =
        w.screen().historyRow(w.screen().historySize() - w.scrollOffset());
    expect("the top display row is a retained line", top_row != nullptr);
    if (top_row) {
        std::string want;
        for (const kai::Cell& cell : *top_row)
            want += cell.text.empty() ? " " : cell.text;
        while (!want.empty() && want.back() == ' ')
            want.pop_back();
        const int width = std::min<int>(20, static_cast<int>(want.size()));
        sendMouse(&w, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton,
                  cellPoint(&w, 0, 0));
        sendMouse(&w, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton, cellPoint(&w, 0, width));
        sendMouse(&w, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
                  cellPoint(&w, 0, width));
        expectStr("a drag while scrolled back selects the retained line",
                  w.selectedText().substr(0, static_cast<std::size_t>(width)),
                  want.substr(0, static_cast<std::size_t>(width)));
    }
    sendKey(&w, Qt::Key_End, Qt::ShiftModifier);
    pump(200);

    // ---- Mouse reporting handed to the application -------------------------
    // The shell turns reporting on the way vim or htop do, then hands its stdin
    // to cat so the reports can be read back byte for byte.
    {
        const kai::CellPos word = alpha;
        const std::string got = captureToPty(
            &w, "printf '\\033[?1000;1002;1006h\\033[?2004l'", [&] {
                expect("the application turned mouse reporting on", w.screen().mouseReporting());
                expect("it asked for drag reporting",
                       w.screen().mouseMode() == kai::MouseMode::Drag);
                expect("it asked for SGR coordinates",
                       w.screen().mouseCoord() == kai::MouseCoord::Sgr);
                expect("and it did not ask for bracketed paste", !w.screen().bracketedPaste());

                const QPointF press_at = cellPoint(&w, word.row, word.col);
                const QPointF drag_to = cellPoint(&w, word.row, word.col + 2);
                sendMouse(&w, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton, press_at);
                expect("a press goes to the application instead of selecting",
                       w.selection().empty());
                sendMouse(&w, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton, drag_to);
                // Motion inside the cell already reported is not a new report: a
                // program should not see one per pixel of pointer movement.
                sendMouse(&w, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton,
                          QPointF(drag_to.x() + 1.0, drag_to.y()));
                sendMouse(&w, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton, drag_to);
                sendWheelAt(&w, 120, press_at);
                // With reporting on, a paste is still a paste.
                w.pasteText("one\ntwo\r\nthree\r");
            });

        std::string want;
        auto add = [&](kai::MouseKey button, kai::MouseAction action, int row, int col,
                       unsigned held) {
            kai::MouseReport rep;
            rep.button = button;
            rep.action = action;
            rep.row = row;
            rep.col = col;
            rep.held = held;
            want += kai::encodeMouse(rep, kai::MouseMode::Drag, kai::MouseCoord::Sgr);
        };
        const unsigned left = kai::mouseKeyBit(kai::MouseKey::Left);
        add(kai::MouseKey::Left, kai::MouseAction::Press, word.row, word.col, left);
        add(kai::MouseKey::None, kai::MouseAction::Motion, word.row, word.col + 2, left);
        add(kai::MouseKey::Left, kai::MouseAction::Release, word.row, word.col + 2, 0);
        add(kai::MouseKey::WheelUp, kai::MouseAction::Press, word.row, word.col, 0);
        // Newlines arrive as carriage returns, unfenced here because bracketed
        // paste was turned off for this capture.
        want += "one\rtwo\rthree\r";
        expectStr("the application received exactly the reports it asked for", got, want);
    }

    // ---- Middle click pastes, fenced when the application asks for it ------
    {
        const kai::CellPos word = alpha;
        const std::string got = captureToPty(
            &w, "printf '\\033[?1000;1002;1006l\\033[?2004h'", [&] {
                expect("reporting is off again", !w.screen().mouseReporting());
                expect("the application asked for bracketed paste", w.screen().bracketedPaste());
                if (clipboard_available) {
                    QGuiApplication::clipboard()->setText(
                        QStringLiteral("one\ntwo\r\nthree\r"), QClipboard::Clipboard);
                    // A middle click reads the selection first, so empty it to
                    // prove the clipboard branch is the one that fired.
                    if (QGuiApplication::clipboard()->supportsSelection())
                        QGuiApplication::clipboard()->setText(QString(), QClipboard::Selection);
                    const QPointF at = cellPoint(&w, word.row, word.col);
                    sendMouse(&w, QEvent::MouseButtonPress, Qt::MiddleButton, Qt::MiddleButton, at);
                    sendMouse(&w, QEvent::MouseButtonRelease, Qt::MiddleButton, Qt::NoButton, at);
                } else {
                    w.pasteText("one\ntwo\r\nthree\r");
                }
            });
        expectStr(clipboard_available
                      ? "a middle click pasted the clipboard, fenced, with carriage returns"
                      : "a paste is fenced when asked and newlines become carriage returns",
                  got, std::string("\x1b" "[200~one\rtwo\rthree\r\x1b" "[201~"));
    }

    // Ctrl+C interrupts the shell through the same path; the widget stays alive.
    sendKey(&w, Qt::Key_C, Qt::ControlModifier);
    pump(300);
    expect("widget survives an interrupt", w.screen().rows() == w.rows());

    w.shutdown();

    if (failures == 0) {
        std::cout << "PASS: " << checks
                  << " offscreen widget checks passed (no visual inspection performed)";
        if (!clipboard_available)
            std::cout << " [clipboard unavailable on this platform: copy and middle-click paste"
                         " through the clipboard were not exercised]";
        else if (!selection_supported)
            std::cout << " [this platform has no separate X11-style selection: only the"
                         " clipboard copy was exercised]";
        std::cout << "\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " offscreen widget checks failed\n";
    return 1;
}
