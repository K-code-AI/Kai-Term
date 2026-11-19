// What would this window paint, right now?
//
// `kai-term open NAME` in a test, with the picture written out instead of put on a
// monitor. It goes through the code a person goes through — AppWindow around a session
// name, shown, left alone — and then asks the widget what it would draw, before anybody
// has touched a key.
//
// With "swap" it walks the other path: a window that already exists, mapped and showing
// its own shell, is told to show a session through showSession, which is what Ctrl+Shift+A
// does. `open NAME` builds a window around the session, and that one has been photographed
// doing the right thing; the menu's path had never been looked at.
//
// With "prompt" or "menu" it photographs the two dialogs the terminal now draws inside
// itself — the question asked when a window is closed with something running, and the
// list Ctrl+Shift+A puts up — and a second photograph after a key, so the moved focus
// is looked at too. These pictures are what stand in for the eyes this machine does
// not have.
//
// With "alt" it photographs the margin question: a stand-in full-screen program in a
// btop-ish blue, which should own the whole window with no grey-black ring around it,
// and then the same window after the program left, where the margin should be back.
#include "../../src/app/appwindow.hpp"
#include "../../src/session/sessionbackend.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>

#include <iostream>
#include <memory>
#include <string>

namespace {

void pump(int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

void shot(kai::AppWindow& window, const char* file, const char* when)
{
    const QImage image = window.grab().toImage();
    int ink = 0;
    for (int y = 0; y < image.height(); y += 3)
        for (int x = 0; x < image.width(); x += 3)
            if (image.pixelColor(x, y).value() > 60)
                ++ink;
    std::cout << when << ": " << image.width() << "x" << image.height()
              << " bright_pixels=" << ink << " offset=" << window.scrollOffset()
              << " history=" << window.screen().historySize() << std::endl;
    static_cast<void>(image.save(QString::fromUtf8(file)));
}

void press(kai::AppWindow& window, int key)
{
    QApplication::sendEvent(&window, new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
}

// The two dialogs, over a shell that has said something, so the pictures show what a
// dialog actually sits on: text, not a void.
int photographDialogs(const std::string& mode, int argc, char** argv)
{
    QApplication app(argc, argv);
    std::unique_ptr<kai::AppWindow> window =
        std::make_unique<kai::AppWindow>(kai::startWindowBackend(24, 80));
    window->resize(1000, 700);
    window->show();
    pump(1200);
    window->pasteText("seq 1 12");
    press(*window, Qt::Key_Return);
    pump(800);

    if (mode == "alt") {
        // A btop-ish blue, one line against the very first row and column of
        // the grid to show where it starts, and the frame text a real program
        // would keep clear of the rounded corners.
        window->pasteText(
            "printf '\\033[?1049h\\033[48;2;43;52;70m\\033[2J\\033[1;1H>| full screen "
            "owns the window, edge to edge |\\033[12;1H>| its own frame minds the "
            "corners |\\033[0m'");
        press(*window, Qt::Key_Return);
        pump(900);
        shot(*window, "/tmp/view-alt.png", "full-screen program, no margin");
        window->pasteText("printf '\\033[?1049l'");
        press(*window, Qt::Key_Return);
        pump(900);
        shot(*window, "/tmp/view-primary-again.png", "shell back, margin back");
        window->shutdown();
        return 0;
    }

    if (mode == "prompt") {
        window->askInTerminal("Quit while btop is running?", std::string(),
                              {"Yes", "No", "Save Session"}, 1, [](int) {});
        pump(250);
        shot(*window, "/tmp/view-prompt.png", "close prompt, No focused");
        press(*window, Qt::Key_Left);
        pump(250);
        shot(*window, "/tmp/view-prompt-left.png", "close prompt, Yes focused");
    } else {
        window->chooseInTerminal(
            "Go to another session",
            {"1  s1   btop running   kept",
             "2  s2   at a prompt   left behind",
             "4  w3   make running   in another window",
             "7  s7   at a prompt"},
            [](int) {});
        pump(250);
        shot(*window, "/tmp/view-menu.png", "session list, first row focused");
        press(*window, Qt::Key_Down);
        pump(250);
        shot(*window, "/tmp/view-menu-down.png", "session list, moved down");
    }
    window->shutdown();
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: grabview <session> [swap] | grabview prompt | grabview menu"
                  << " | grabview alt\n";
        return 2;
    }
    const std::string mode = argv[1];
    if (mode == "prompt" || mode == "menu" || mode == "alt")
        return photographDialogs(mode, argc, argv);

    QApplication app(argc, argv);
    const bool swap = argc > 2 && std::string(argv[2]) == "swap";

    std::unique_ptr<kai::AppWindow> window =
        swap ? std::make_unique<kai::AppWindow>(kai::startWindowBackend(24, 80))
             : std::make_unique<kai::AppWindow>(std::string(argv[1]));
    window->resize(1000, 700);
    window->show();

    pump(1800);
    if (swap) {
        shot(*window, "/tmp/view-old-shell.png", "the shell it started with");
        window->showSession(std::make_unique<kai::SessionBackend>(argv[1], window->rows(),
                                                                  window->cols()),
                            true);
        pump(1500);
    }
    shot(*window, "/tmp/view-before-key.png", "without a key");
    std::cout << "window " << window->width() << "x" << window->height()
              << " grid " << window->rows() << "x" << window->cols()
              << std::endl;

    // A key the shell cannot see, so nothing about the session changes.
    QApplication::sendEvent(window.get(), new QKeyEvent(QEvent::KeyPress, Qt::Key_Shift,
                                                        Qt::NoModifier));
    QApplication::sendEvent(window.get(), new QKeyEvent(QEvent::KeyRelease, Qt::Key_Shift,
                                                        Qt::NoModifier));
    pump(600);
    shot(*window, "/tmp/view-after-key.png", "after a shift");

    std::cout << "--- screen the model is holding ---\n"
              << window->screen().text().substr(0, 240) << "\n--- end ---\n" << std::endl;
    window->shutdown();
    return 0;
}
