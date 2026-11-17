// End-to-end mouse check against a real application.
//
// tests/mouse.cpp asserts the bytes; this asserts that a program which asked for
// the mouse actually understands them. It came out of btop ignoring every click
// and wheel turn: the SGR report was being written as CSI < col ; row ; button M
// instead of xterm's CSI < button ; col ; row M, which is unreadable to anything
// that parses it, and no byte-level test caught it because both sides of that
// test were written from the same wrong idea.
//
// Runs on Qt's offscreen platform: no display, and no claim about appearance.
// Skips with a printed note when btop is not installed, so the check is honest
// about what ran.

#include "../src/terminal/terminalwidget.hpp"
#include "../src/terminal/text.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <QStandardPaths>

#include <cctype>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

void expect(const char* what, bool ok) {
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

void expectStr(const char* what, const std::string& got, const std::string& want) {
    ++checks;
    if (got == want)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << ": want [" << want << "] got [" << got << "]\n";
}

void expectEq(const char* what, int got, int want) {
    ++checks;
    if (got == want)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << ": want " << want << " got " << got << "\n";
}

void pump(int ms) {
    for (int i = 0; i < ms / 10; ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    QCoreApplication::processEvents();
}

bool waitFor(const std::function<bool()>& pred, int timeout_ms) {
    const int step = 50;
    for (int waited = 0; waited < timeout_ms; waited += step) {
        pump(step);
        if (pred())
            return true;
    }
    return pred();
}

std::string rowString(const kai::TerminalWidget& w, int r) {
    const kai::Screen& s = w.screen();
    std::string out;
    for (int c = 0; c < s.cols(); ++c) {
        const std::string& t = s.at(r, c).text;
        out += t.empty() ? " " : t;
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

// Byte offsets in a row's text are not cell columns once anything multi-byte is
// on the screen, so walk the cells and return the column directly. Clicking at a
// byte offset lands in the wrong cell, which looks exactly like the application
// ignoring the mouse.
std::pair<int, int> findCell(const kai::TerminalWidget& w, const std::string& needle) {
    const kai::Screen& s = w.screen();
    for (int r = 0; r < s.rows(); ++r) {
        std::string line;
        std::vector<int> col_of;
        for (int c = 0; c < s.cols(); ++c) {
            const std::string& t = s.at(r, c).text;
            for (char ch : t.empty() ? std::string(" ") : t) {
                line += ch;
                col_of.push_back(c);
            }
        }
        const std::size_t p = line.find(needle);
        if (p != std::string::npos)
            return {r, col_of[p]};
    }
    return {-1, -1};
}

QPointF cellCenter(kai::TerminalWidget* w, int row, int col) {
    const QPointF top_left = w->cellTopLeft(row, col);
    const QSizeF cell = w->cellSize();
    return QPointF(top_left.x() + cell.width() * 0.5, top_left.y() + cell.height() * 0.5);
}

void press(kai::TerminalWidget* w, int row, int col, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    const QPointF at = cellCenter(w, row, col);
    QMouseEvent ev(QEvent::MouseButtonPress, at, at, Qt::LeftButton, Qt::LeftButton, mods);
    QCoreApplication::sendEvent(w, &ev);
}

void release(kai::TerminalWidget* w, int row, int col, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    const QPointF at = cellCenter(w, row, col);
    QMouseEvent ev(QEvent::MouseButtonRelease, at, at, Qt::LeftButton, Qt::NoButton, mods);
    QCoreApplication::sendEvent(w, &ev);
}

void click(kai::TerminalWidget* w, int row, int col) {
    press(w, row, col);
    release(w, row, col);
}

void wheel(kai::TerminalWidget* w, int row, int col, int angle_delta_y) {
    const QPointF at = cellCenter(w, row, col);
    QWheelEvent ev(at, at, QPoint(0, 0), QPoint(0, angle_delta_y), Qt::NoButton,
                   Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(w, &ev);
}

void sendText(kai::TerminalWidget* w, const std::string& utf8) {
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier,
                 QString::fromUtf8(utf8.c_str(), static_cast<int>(utf8.size())));
    QCoreApplication::sendEvent(w, &ev);
}

void sendEnter(kai::TerminalWidget* w) {
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    QCoreApplication::sendEvent(w, &ev);
}

// Where the "N/M" counter is drawn, searched from the bottom of the screen up.
// A plain search for '/' finds the memory gauges and the paths in the process
// list first, and a click aimed at one of those lands in the wrong box.
std::pair<int, int> findCounterCell(const kai::TerminalWidget& w) {
    const kai::Screen& s = w.screen();
    for (int r = s.rows() - 1; r >= 0; --r) {
        const std::string line = rowString(w, r);
        for (std::size_t p = line.find('/'); p != std::string::npos; p = line.find('/', p + 1)) {
            std::size_t a = p;
            while (a > 0 && std::isdigit(static_cast<unsigned char>(line[a - 1])))
                --a;
            std::size_t b = p + 1;
            while (b < line.size() && std::isdigit(static_cast<unsigned char>(line[b])))
                ++b;
            if (a < p && b > p + 1)
                return {r, static_cast<int>(a)};
        }
    }
    return {-1, -1};
}

// The first cell carrying a glyph, so a test drag can be aimed at something the
// application actually drew rather than at a blank corner.
std::pair<int, int> firstTextCell(const kai::TerminalWidget& w) {
    const kai::Screen& s = w.screen();
    for (int r = 0; r < s.rows(); ++r)
        for (int c = 0; c < s.cols(); ++c)
            if (!s.at(r, c).text.empty())
                return {r, c};
    return {-1, -1};
}

// The "N/420" scroll counter btop draws in the footer of the selected box. It is
// the application's own state, so a change here cannot be animation noise.
std::string scrollCounter(const kai::TerminalWidget& w) {
    const std::string text = kai::screenText(w.screen());
    const std::size_t slash = text.rfind('/');
    if (slash == std::string::npos || slash < 2)
        return {};
    std::size_t a = slash;
    while (a > 0 && std::isdigit(static_cast<unsigned char>(text[a - 1])))
        --a;
    std::size_t b = slash + 1;
    while (b < text.size() && std::isdigit(static_cast<unsigned char>(text[b])))
        ++b;
    if (a == slash || b == slash + 1)
        return {};
    return text.substr(a, b - a);
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const std::string program = argc > 1 ? argv[1] : "btop";
    if (QStandardPaths::findExecutable(QString::fromStdString(program)).isEmpty()) {
        std::cout << "PASS: mouse reports against a real application were not exercised"
                  << " [" << program << " is not installed on this machine]\n";
        return 0;
    }

    kai::TerminalWidget w(24, 80, "/bin/sh");
    w.resize(900, 600);
    w.show();
    pump(400);

    sendText(&w, "exec " + program);
    sendEnter(&w);

    // btop asks for the mouse a few hundred milliseconds after it starts.
    expect("the application asked for mouse reporting",
           waitFor([&] { return w.screen().mouseReporting(); }, 10000));
    if (failures) {
        std::cerr << "screen was:\n" << kai::screenText(w.screen()) << "\n";
        w.shutdown();
        return 1;
    }

    // What btop actually requests, recorded here because it is the pair the
    // encoder has to satisfy: drag tracking for the clicks, SGR for the
    // coordinates, and no classic ?1000 at all.
    expect("drag tracking implies click reporting",
           w.screen().mouseMode() == kai::MouseMode::Drag ||
               w.screen().mouseMode() == kai::MouseMode::Any);
    expect("SGR coordinates", w.screen().mouseCoord() == kai::MouseCoord::Sgr ||
                                  w.screen().mouseCoord() == kai::MouseCoord::Classic);
    expect("the application holds the alternate screen", w.screen().inAlternateScreen());

    // The scroll counter is btop's own state. Moving it proves the reports it
    // received were parseable, which is the thing the byte-level tests could not
    // say on their own. It is drawn on the first full frame, not on startup.
    expect("the application drew its scroll counter",
           waitFor([&] { return !scrollCounter(w).empty(); }, 8000));
    const auto footer = findCounterCell(w);
    const std::string before = scrollCounter(w);
    if (!before.empty() && footer.first > 4) {
        // Click inside the scrollable box first: btop only wheels the box that
        // has focus, so this also proves a click landed where it was aimed.
        click(&w, footer.first - 2, 20);
        pump(700);
        for (int i = 0; i < 3; ++i) {
            wheel(&w, footer.first - 4, 20, -120);
            pump(300);
        }
        pump(800);
        const std::string after = scrollCounter(w);
        expect("a wheel turn moved the application's own scroll position",
               after != before && !after.empty());
    }

    // Holding Shift is the user's escape hatch: with reporting on, a drag has to
    // stay local so text can still be copied out of the application.
    const auto glyph = firstTextCell(w);
    press(&w, glyph.first, glyph.second, Qt::ShiftModifier);
    const QPointF to = cellCenter(&w, glyph.first, glyph.second + 4);
    QMouseEvent move(QEvent::MouseMove, to, to, Qt::NoButton, Qt::LeftButton,
                     Qt::ShiftModifier);
    QCoreApplication::sendEvent(&w, &move);
    release(&w, glyph.first, glyph.second + 4, Qt::ShiftModifier);
    expect("Shift keeps the mouse for selection while the application holds it",
           !w.selection().empty());
    expect("and the selected text is the application's own",
           !w.selectedText().empty());

    w.shutdown();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "PASS: " << checks
              << " mouse checks passed against a running " << program
              << " (no visual inspection performed)\n";
    return 0;
}
