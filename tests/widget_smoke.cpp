// Offscreen widget exercise. This drives the real TerminalWidget: it starts a
// shell in a PTY, delivers synthetic Qt key and wheel events, lets the reader
// thread feed the screen model, and paints the widget into an image.
//
// What this proves: the widget code paths run without crashing, key events turn
// into bytes the shell acts on, output lands in the model, and scrolling back
// changes what is painted. It is not a visual inspection; nothing here checks
// whether the result looks right to a person.

#include "../src/terminal/terminalwidget.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QKeyEvent>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QWheelEvent>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
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

void sendWheel(kai::TerminalWidget* w, int angle_delta_y) {
    QWheelEvent ev(QPointF(20.0, 20.0), QPointF(20.0, 20.0), QPoint(0, 0),
                   QPoint(0, angle_delta_y), Qt::NoButton, Qt::NoModifier,
                   Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(w, &ev);
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

    // Ctrl+C interrupts the shell through the same path; the widget stays alive.
    sendKey(&w, Qt::Key_C, Qt::ControlModifier);
    pump(300);
    expect("widget survives an interrupt", w.screen().rows() == w.rows());

    w.shutdown();

    if (failures == 0) {
        std::cout << "PASS: " << checks
                  << " offscreen widget checks passed (no visual inspection performed)\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " offscreen widget checks failed\n";
    return 1;
}
