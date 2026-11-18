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

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: grabview <session> [swap]\n";
        return 2;
    }
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
