// A window that shows two places of one session at the same time.

// This is the visible half of side-by-side view: the arrangement, the sizes and the
// tab bar were all there already, but the window still drew one place and left the
// rest of the screen blank, so pressing Ctrl+Shift+H changed nothing a person could
// see. Here a real daemon runs a real two-pane session, the window is handed a second
// connection pointed at the other place, and the checks ask the two things that
// matter: is each place painted into the cells the session gave it, and does typing
// go to the place whose half of the screen was clicked.

#include "../src/session/daemon.hpp"
#include "../src/session/sessionbackend.hpp"
#include "../src/terminal/terminalwidget.hpp"

#include <QApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace kai;

namespace {

int checks = 0;
int failures = 0;

void expect(std::string_view what, bool value) {
    ++checks;
    if (value)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << "\n";
}

void pump(int ms, int step_ms = 20) {
    for (int elapsed = 0; elapsed < ms; elapsed += step_ms) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
    }
}

bool waitFor(std::function<bool()> pred, int timeout_ms = 6000) {
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += 20) {
        QCoreApplication::processEvents();
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    QCoreApplication::processEvents();
    return pred();
}

std::string onScreen(const kai::TerminalWidget& w) { return w.screen().text(); }

void sendKey(kai::TerminalWidget* w, int key, Qt::KeyboardModifiers mods = Qt::NoModifier,
             const QString& text = QString()) {
    QKeyEvent ev(QEvent::KeyPress, key, mods, text);
    QCoreApplication::sendEvent(w, &ev);
}

// Type a line and press Enter, the way a person does rather than by pasting: a shell
// with bracketed paste on takes a pasted newline as text and leaves the command at the
// prompt, which would make a check that looks for output find the command line instead.
void runLine(kai::TerminalWidget* w, const std::string& command) {
    w->pasteText(command);
    sendKey(w, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
}

void sendMouse(kai::TerminalWidget* w, QEvent::Type type, Qt::MouseButton button,
               Qt::MouseButtons buttons, const QPointF& at) {
    QMouseEvent ev(type, at, at, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

// The middle of a cell, in window pixels — where a click means that cell.
QPointF cellPoint(kai::TerminalWidget* w, int row, int col) {
    const QPointF top_left = w->cellTopLeft(row, col);
    const QSizeF cell = w->cellSize();
    return QPointF(top_left.x() + cell.width() * 0.5, top_left.y() + cell.height() * 0.5);
}

// Whether any ink was drawn in a band of columns of the live grid: the difference
// between a place that is painted there and the blank ground that is not. The ground
// is read from the far right of the same rows, where nothing is expected, so this does
// not have to know the theme's colours.
bool inkInColumns(const QImage& shot, kai::TerminalWidget* w, int from_col, int to_col) {
    if (from_col >= to_col)
        return false;
    const QSizeF cell = w->cellSize();
    const QPointF first = w->cellTopLeft(0, from_col);
    const int top = static_cast<int>(first.y());
    const int left = static_cast<int>(w->cellTopLeft(0, from_col).x());
    const int right = static_cast<int>(w->cellTopLeft(0, to_col).x());
    const int rows = w->rows();
    int inked = 0;
    for (int r = 0; r < rows; ++r) {
        const int y = static_cast<int>(w->cellTopLeft(r, 0).y() + cell.height() * 0.5);
        for (int x = left; x < right && x < shot.width(); ++x) {
            if (shot.pixelColor(x, y) != shot.pixelColor(shot.width() - 2, y))
                ++inked;
        }
    }
    (void)top;
    return inked > 3; // a few stray pixels is anti-aliasing, not text
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const std::string root =
        "/tmp/kai-widget-split-" + std::to_string(static_cast<long>(::getpid()));
    ::mkdir(root.c_str(), 0700);
    ::setenv("XDG_RUNTIME_DIR", root.c_str(), 1);
    ::setenv("KAI_SHELL", "/bin/sh", 1);

    Daemon daemon;
    std::string error;
    if (!daemon.start(error)) {
        std::cerr << "cannot start the daemon: " << error << "\n";
        return 1;
    }
    std::thread serving([&daemon] { daemon.run(); });
    pump(150);

    SessionSpec spec;
    spec.name = "split";
    spec.rows = 24;
    spec.cols = 80;
    expect("a session starts to be split", daemon.create(spec, error));
    pump(400);

    kai::TerminalWidget window(24, 80, std::make_unique<SessionBackend>("split", 24, 80));
    window.resize(800, 500);
    window.show();
    pump(300);

    // A second place, so there are two halves to draw. Asked of the window rather than
    // of the daemon directly, because a place is added the way a person adds one.
    window.newTab();
    expect("the session is running two programs",
           waitFor([&daemon] {
               for (const SessionInfo& info : daemon.list())
                   if (info.name == "split")
                       return info.panes == 2;
               return false;
           }));
    expect("and the window is told the tab bar has two places",
           waitFor([&window] { return window.paneCount() == 2; }));
    expect("shown side by side, which is where two places share the screen",
           waitFor([&window] { return !window.viewIsTabs(); }));

    // A new place takes the focus, so the window would be looking at the second one.
    // Look it back at the first, the way clicking that tab would, so this test says
    // which half is which rather than following wherever the session happened to point.
    window.showPaneAt(0);
    pump(200);
    expect("and the window is looking at the first place", window.shownPane() == 0);

    // Each place prints something only it says, so a marker found in the right place
    // was painted from the right program and not copied from its neighbour.
    expect("the first place is asked to print",
           daemon.type("split", "echo LEFT-PLACE-MARKER\n", 0));
    expect("and the second place too",
           daemon.type("split", "echo RIGHT-PLACE-MARKER\n", 1));
    pump(700);

    // The window is looking at one place; the other is not its screen yet, so its
    // marker must not be on the screen this window shows.
    expect("the place being looked at has its marker on the window's screen",
           onScreen(window).find("LEFT-PLACE-MARKER") != std::string::npos);
    expect("and the other place's marker is not there before it is shown",
           onScreen(window).find("RIGHT-PLACE-MARKER") == std::string::npos);

    // Hand the window a second connection, pointed at the other place. The window
    // gives it a screen and a reader of its own and paints it where the tab bar says
    // that place sits.
    auto second = std::make_unique<SessionBackend>("split", 24, 80);
    second->showPane(1);
    window.setExtraPane(std::move(second), 1);
    pump(500);

    expect("the other place now has a screen inside the window",
           waitFor([&window] {
               return window.extraScreenText().find("RIGHT-PLACE-MARKER") != std::string::npos;
           }));

    // Both places painted at once: ink in the left cell and ink in the right, in the
    // same window. This is the thing that was missing — pressing H used to change
    // nothing a person could see.
    const QImage shot = window.grab().toImage();
    expect("the first place is drawn in the left half",
           inkInColumns(shot, &window, 0, 39));
    expect("and the second place is drawn in the right half",
           inkInColumns(shot, &window, 40, 80));

    // Typing goes to the half that was clicked. The window starts by typing into the
    // place it is looking at; a click moves it, and the marker proves which program
    // took the keystrokes.
    const int right_col = 50; // well inside the second place's cell
    sendMouse(&window, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton,
              cellPoint(&window, 1, right_col));
    sendMouse(&window, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
              cellPoint(&window, 1, right_col));
    pump(120);
    expect("clicking the second place moves the typing to it",
           window.activePane() == 1);
    runLine(&window, "echo TYPED-INTO-SECOND");
    expect("the second place takes what was typed",
           waitFor([&window] {
               return window.extraScreenText().find("TYPED-INTO-SECOND") != std::string::npos;
           }));
    expect("and the first place is left alone",
           onScreen(window).find("TYPED-INTO-SECOND") == std::string::npos);

    // Clicking back moves the typing to the first place.
    sendMouse(&window, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton,
              cellPoint(&window, 1, 5));
    sendMouse(&window, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
              cellPoint(&window, 1, 5));
    pump(120);
    expect("clicking the first place moves the typing back",
           window.activePane() == 0);
    runLine(&window, "echo TYPED-INTO-FIRST");
    expect("the first place takes what was typed",
           waitFor([&window] {
               return onScreen(window).find("TYPED-INTO-FIRST") != std::string::npos;
           }));
    expect("and the second place is left alone",
           window.extraScreenText().find("TYPED-INTO-FIRST") == std::string::npos);

    // Taking the extra place away must not leave a thread reading a closed connection.
    window.setExtraPane(nullptr, -1);
    pump(200);
    expect("the window has no extra place once it is taken away",
           !window.hasExtraPane());

    expect("the session still runs after the second view is let go",
           waitFor([&daemon] {
               for (const SessionInfo& info : daemon.list())
                   if (info.name == "split")
                       return info.alive;
               return false;
           }));

    // The window above was handed its neighbour. A real window onto a session is not:
    // it is given a way to open one and shows its places side by side by itself. That
    // is what a window onto a session does with a connector, and it is checked here on
    // a plain widget so the whole thing runs without a compositor — the same seam the
    // app wires up, minus the app's own config.
    SessionSpec auto_spec;
    auto_spec.name = "auto";
    auto_spec.rows = 24;
    auto_spec.cols = 80;
    expect("a second session starts, to be shown side by side on its own",
           daemon.create(auto_spec, error));
    pump(400);

    kai::TerminalWidget auto_window(24, 80,
                                    std::make_unique<SessionBackend>("auto", 24, 80));
    auto_window.resize(800, 500);
    auto_window.show();
    pump(300);
    // The only thing this window is given that the first one was not: a way to ask for
    // a neighbour connection. Everything after is the window's own doing.
    auto_window.setPaneConnector([&auto_window](int pane) -> std::unique_ptr<TerminalBackend> {
        auto neighbour = std::make_unique<SessionBackend>("auto", auto_window.rows(),
                                                          auto_window.cols());
        neighbour->showPane(pane);
        return neighbour;
    });
    expect("a window with one place shows no neighbour", !auto_window.hasExtraPane());

    // Add a place the way a person does. The window should find the other place and
    // open a connection to it without anyone handing it one.
    auto_window.newTab();
    expect("adding a place makes the window open a neighbour on its own",
           waitFor([&auto_window] { return auto_window.hasExtraPane(); }));
    expect("and the neighbour is a place other than the one being looked at",
           waitFor([&auto_window] {
               return auto_window.hasExtraPane() &&
                      auto_window.extraScreenText().find("RIGHT") == std::string::npos;
           }));
    daemon.type("auto", "echo AUTO-NEIGHBOUR-MARKER\n", auto_window.shownPane() == 0 ? 1 : 0);
    pump(500);
    expect("the neighbour the window opened is showing the other place",
           waitFor([&auto_window] {
               return auto_window.extraScreenText().find("AUTO-NEIGHBOUR-MARKER") !=
                      std::string::npos;
           }));

    // Going back to one-place-at-a-time tabs takes the neighbour away; the window is
    // then the whole screen and has nothing beside it.
    auto_window.toggleViewMode();
    expect("tabs mode leaves no neighbour beside the window",
           waitFor([&auto_window] { return auto_window.viewIsTabs() && !auto_window.hasExtraPane(); }));
    // And coming back to side-by-side opens one again.
    auto_window.toggleViewMode();
    expect("side by side opens the neighbour again",
           waitFor([&auto_window] { return !auto_window.viewIsTabs() && auto_window.hasExtraPane(); }));

    auto_window.shutdown();
    static_cast<void>(daemon.kill("auto", error));

    // Leave nothing running for the next test to trip over.
    static_cast<void>(daemon.kill("split", error));

    // Put the window down before the daemon it is watching, so its readers are joined
    // while the far end is still there to say goodbye; then stop serving and wait.
    window.shutdown();
    daemon.stop();
    serving.join();

    if (failures == 0) {
        std::cout << "PASS: " << checks << " side-by-side pane checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " side-by-side checks failed\n";
    return 1;
}
