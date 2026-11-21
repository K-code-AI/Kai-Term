// The three panels, photographed in windows of several sizes.
//
// `kai-term-grabview` photographs a window at its own size. This one asks the
// other question — what does a panel do when the window is smaller than the
// panel wants to be? A menu is laid out from its own content: a hint line, a
// table of rows, a title. None of those knows how wide the window is, so a
// small window used to have a panel drawn partly off the edge of it, and the
// only way to see that is to look.
//
// It writes one picture per panel per size into KAI_PANELVIEW_DIR (default
// /tmp/kai-panelview), on a theme with a strong red ground and a bright ink,
// because a panel that has lost a third of its width is obvious in those colours
// and invisible in grey on grey.
//
// The window carries a shell of its own rather than one held by the daemon: what
// is being looked at is a panel's geometry, and a daemon would only add a second
// thing that can move the window's size underneath it.
//
// Not a check. It writes pictures; the checks are in tests/app_window.cpp.

#include "../../src/app/appwindow.hpp"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {

void pump(int ms) {
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

void press(kai::AppWindow& window, int key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QKeyEvent ev(QEvent::KeyPress, key, mods);
    QCoreApplication::sendEvent(&window, &ev);
}

// One picture, taken once the widget has actually painted at the size it was
// asked for. `grab()` on a widget that has not been repainted since the resize
// hands back nothing, and a missing file is a worse answer than a slow one.
void shot(kai::AppWindow& window, const std::string& dir, const std::string& what,
          int w, int h) {
    QImage image;
    for (int attempt = 0; attempt < 20 && image.isNull(); ++attempt) {
        window.repaint();
        image = window.grab().toImage();
        if (image.isNull())
            pump(50);
    }
    const std::string file = dir + "/" + what + "-" + std::to_string(w) + "x" +
                             std::to_string(h) + ".png";
    if (image.isNull()) {
        std::cout << what << " at " << w << "x" << h << ": nothing to photograph"
                  << std::endl;
        return;
    }
    static_cast<void>(image.save(QString::fromUtf8(file.c_str())));
    std::cout << what << " at " << w << "x" << h << " -> " << file << " (window is "
              << window.width() << "x" << window.height() << ")" << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    const char* dir_env = std::getenv("KAI_PANELVIEW_DIR");
    const std::string dir = dir_env ? dir_env : "/tmp/kai-panelview";
    static_cast<void>(QDir().mkpath(QString::fromUtf8(dir.c_str())));

    const std::string cfg = dir + "/panelview.conf";
    const std::string theme = dir + "/theme.conf";
    std::ofstream(cfg) << "font_size = 14\n";
    // A deep red ground and a bright magenta ink, the colours of the picture
    // Kaida sent, so a panel edge that ran off the window shows up.
    std::ofstream(theme) << "foreground = #ff5ce1\n"
                         << "background = #7b101b\n"
                         << "selection = #3a4a8c\n"
                         << "accent = #ff5ce1\n";
    ::setenv("KAI_TERM_CONFIG", cfg.c_str(), 1);
    ::setenv("KAI_TERM_THEME", theme.c_str(), 1);

    kai::WindowStart start;
    start.backend = std::make_unique<kai::PtyBackend>(24, 80, std::string(),
                                                      kai::homeDirectory());
    start.rows = 24;
    start.cols = 80;
    kai::AppWindow window(std::move(start));
    window.show();
    pump(400);

    const kai::Color& ink = window.theme().foreground;
    const kai::Color& ground = window.theme().background;
    std::cout << "theme ink #" << std::hex << int{ink.r} << int{ink.g} << int{ink.b}
              << " ground #" << int{ground.r} << int{ground.g} << int{ground.b}
              << std::dec << std::endl;

    const int sizes[][2] = {{960, 540}, {620, 460}, {430, 320}, {330, 220}};

    // The settings menu on its own.
    press(window, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
    pump(150);
    for (const auto& s : sizes) {
        window.resize(s[0], s[1]);
        pump(150);
        shot(window, dir, "settings", s[0], s[1]);
    }
    // The last row of that menu is the theme, and it is the way into the colour editor
    // now that the editor has no key of its own. Photographed here because the line along
    // the bottom says something different on this one row.
    // (Kaida, 2026-11-20: "the Theme editor should be an item in the setting menu under
    // Theme".)
    for (int i = 0; i < 3; ++i) {
        press(window, Qt::Key_Up); // off the first row: `cancel`, `save`, the theme
        pump(40);
    }
    // Back to the biggest window first: a photograph named for one size should be that
    // size, and the size loop above left the window at its smallest.
    window.resize(960, 540);
    pump(150);
    shot(window, dir, "settings-theme", 960, 540);
    press(window, Qt::Key_Return);
    pump(150);

    // The colour list, then the mixer opened out of it.
    for (const auto& s : sizes) {
        window.resize(s[0], s[1]);
        pump(150);
        shot(window, dir, "picker", s[0], s[1]);
    }
    // The name line, with the keys on it and the field shut: the theme's name without
    // its extension, and the keystroke that opens it said out loud. These three are
    // looked at for their words, so they are taken at the biggest size, not at the
    // smallest size the loop above happened to stop on.
    window.resize(960, 540);
    pump(150);
    for (int i = 0; i < 3; ++i) {
        press(window, Qt::Key_Up); // off the first colour: `cancel`, `save`, the name
        pump(40);
    }
    shot(window, dir, "picker-name", 960, 540);

    // Enter opens the field, and the caret is where the next letter goes.
    press(window, Qt::Key_Return);
    pump(120);
    shot(window, dir, "picker-edit", 960, 540);
    press(window, Qt::Key_Escape); // the word goes back, the panel stays
    pump(80);

    // And the question, which is the same panel with different words on the bottom.
    press(window, Qt::Key_Down); // off the name, onto `save`
    pump(80);
    press(window, Qt::Key_Return); // `save`, and the name is taken
    pump(120);
    shot(window, dir, "picker-overwrite", 960, 540);
    press(window, Qt::Key_Escape); // back to the panel, still open
    pump(80);
    press(window, Qt::Key_Escape); // out of the colour list, into the menu that waited
    pump(120);
    shot(window, dir, "settings-returned", 960, 540);   // at 960x540, as named
    press(window, Qt::Key_Escape); // and out of the menu
    pump(120);
    // Opened again so the mixer below is entered from a fresh list, which starts with its
    // keys on the ground; one step down is the ink, and that is what these shots mix.
    press(window, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
    pump(150);
    for (int i = 0; i < 3; ++i) {
        press(window, Qt::Key_Up);
        pump(40);
    }
    press(window, Qt::Key_Return);
    pump(150);

    press(window, Qt::Key_Down); // background, then foreground
    pump(60);
    press(window, Qt::Key_Return);
    pump(150);
    for (const auto& s : sizes) {
        window.resize(s[0], s[1]);
        pump(150);
        shot(window, dir, "mixer", s[0], s[1]);
    }

    // And the mixer opened on the window's own background, which is the case a
    // coloured strip gets wrong: the colour being mixed is nearly the colour of
    // the panel it is being mixed in, so the sentence has to keep the colour and
    // the ground under it has to step aside.
    press(window, Qt::Key_Escape);
    pump(120);
    press(window, Qt::Key_Up); // off `foreground`, onto `background`
    pump(60);
    press(window, Qt::Key_Return);
    pump(150);
    for (const auto& s : sizes) {
        window.resize(s[0], s[1]);
        pump(150);
        shot(window, dir, "mixer-bg", s[0], s[1]);
    }

    // The ground the sentence sits on, in the three states the switch walks. The
    // mixer is open on the window's own background here, which is the case the
    // default gets "wrong" on purpose: the colour being mixed *is* the ground, and
    // black and white are what the word at the end of the strip is for.
    press(window, Qt::Key_Up);  // off the first channel, onto the sentence
    pump(120);
    const int ground_sizes[][2] = {{960, 540}, {430, 320}};
    for (const char* name : {"ground-background", "ground-black", "ground-white"}) {
        for (const auto& s : ground_sizes) {
            window.resize(s[0], s[1]);
            pump(150);
            shot(window, dir, name, s[0], s[1]);
        }
        press(window, Qt::Key_Right);
        pump(120);
    }

    window.discardOnClose();
    window.shutdown();
    pump(200);
    std::remove(cfg.c_str());
    std::remove(theme.c_str());
    return 0;
}
