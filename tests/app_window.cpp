// The window a person actually runs, and the two keybinds that make it more than a
// view of one shell.
//
// A daemon is started in this process and the window talks to it over a real socket, so
// what is being checked is the whole path: a window that starts with a shell held by
// the daemon, a keybind that keeps that shell and starts a fresh one in the same
// window, and a kept session coming back into the window that asked for it. The menu
// and the close prompt are drawn by the terminal itself and answered from the
// keyboard, so what is checked here is what they offer, in what order, which answer
// holds the focus, and what each one does — the pixels are for a person to look at.

#include "../src/app/appwindow.hpp"
#include "../src/app/closeprompt.hpp"
#include "../src/session/daemon.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QWidget>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <vector>
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

// Every run of `needle` on the visible screen, top to bottom, with the ink it
// was written in. This asks what a written character was actually painted with,
// which is not what the default-colour accessors report.
//
// A token typed at a prompt is on the screen twice: once in the line the shell
// echoed back, and once in what the command printed. Only the printed one says
// anything about how output is coloured, so callers take the LAST match — the
// echoed command line sits above it.
std::vector<std::pair<int, Color>> cellMatches(const kai::TerminalWidget& w,
                                               const std::string& needle) {
    std::vector<std::pair<int, Color>> hits;
    const Screen& s = w.screen();
    for (int r = 0; r < w.rows(); ++r)
        for (int c = 0; c + static_cast<int>(needle.size()) <= w.cols(); ++c) {
            bool hit = true;
            for (std::size_t k = 0; k < needle.size(); ++k)
                if (s.at(r, static_cast<int>(c) + static_cast<int>(k)).text !=
                    std::string(1, needle[k]))
                    hit = false;
            if (hit)
                hits.emplace_back(r, s.at(r, c).fg);
        }
    return hits;
}

Color lastCellFgWithText(const kai::TerminalWidget& w, const std::string& needle) {
    const std::vector<std::pair<int, Color>> hits = cellMatches(w, needle);
    if (hits.empty())
        return Color{255, 0, 255}; // sentinel: the text was never on screen
    return hits.back().second;
}

void sendKey(kai::TerminalWidget* w, int key, Qt::KeyboardModifiers mods = Qt::NoModifier,
             const QString& text = QString()) {
    QKeyEvent ev(QEvent::KeyPress, key, mods, text);
    QCoreApplication::sendEvent(w, &ev);
}

// Type a line and press Enter. A pasted line is not a run line: bash and fish both
// turn on bracketed paste, and a shell with it on takes a pasted newline as text inside
// the paste, leaving the command at the prompt. So the commands these checks look for
// are written to print something their own command line does not contain — `${M}-WINDOW`
// is on the screen either way, `FIRST-WINDOW` only if the shell ran it.
// The colour editor, opened the way it is opened now: the settings menu, three Ups off
// its first row — `cancel`, `save`, and the last row, which is the theme — then Enter.
// Ctrl+Shift+P used to jump straight in; it is gone, and the row is the way in.
// (Kaida, 2026-11-20: "the Theme editor should be an item in the setting menu under
// Theme"; "ctrl+shift+p, does indeed go away".)
//
// Note what this leaves behind: the settings menu is waiting underneath, unsaved. A test
// that wants the bare terminal back leaves that menu too, with Esc.
void leavePanels(kai::TerminalWidget* w) {
    for (int i = 0; i < 4; ++i) {
        if (!w->mixerActive() && !w->pickerActive() && !w->settingsActive() &&
            !w->settingsSteppedAside())
            return;
        sendKey(w, Qt::Key_Escape);
        pump(30);
    }
}

void openThemeEditor(kai::TerminalWidget* w) {
    leavePanels(w); // start from the bare terminal, not from where the last test stopped
    sendKey(w, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
    pump(60);
    for (int i = 0; i < 3; ++i)
        sendKey(w, Qt::Key_Up);
    pump(40);
    sendKey(w, Qt::Key_Return);
    pump(80);
}

void runLine(kai::TerminalWidget* w, const std::string& command) {
    w->pasteText(command);
    sendKey(w, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
}

bool sees(const Daemon& daemon, const std::string& name,
          const std::function<bool(const SessionInfo&)>& about) {
    for (const SessionInfo& info : daemon.list()) {
        if (info.name == name)
            return about(info);
    }
    return false;
}

// What a session's screen holds, asked of the daemon. Escape bytes, but the text in
// them is still text, which is enough to ask whether the window kept what it said it
// kept.
std::string writtenInto(const std::string& name) {
    DaemonClient client;
    std::string error;
    if (!client.connect(error))
        return {};
    if (!client.request({{"command", "attach"},
                         {"name", name},
                         {"rows", "24"},
                         {"cols", "80"}}))
        return {};
    Frame welcome;
    if (!client.nextFrame(welcome, error))
        return {};
    Frame snapshot;
    std::string raw;
    if (client.nextFrame(snapshot, error) &&
        (snapshot.kind == MsgKind::Snapshot || snapshot.kind == MsgKind::Data))
        raw = snapshot.body;
    client.close();
    // A snapshot is escape bytes with cursor addressing; what a person would read is
    // what a screen model says they would read.
    Screen view(24, 80);
    view.feed(raw);
    return view.text();
}

// What a theme file says about one colour, whatever shape the line is written in. Theme
// files are written `key  value` since the `=` was dropped for them on 2026-11-21, while
// the files already on disk — and the ones these tests seed on purpose, to prove those
// still read — say `key = value`. A check about a colour should not be a check about
// spacing; a check about the shape says so out loud where it means it.
bool theme_says(const std::string& text, const std::string& key, const std::string& colour) {
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t end = text.find('\n', at);
        const std::string line =
            text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? text.size() : end + 1;
        const std::size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#')
            continue; // blank, or a note somebody left
        const std::size_t key_end = line.find_first_of(" \t=", start);
        if (line.substr(start, key_end == std::string::npos ? std::string::npos
                                                            : key_end - start) != key)
            continue;
        const std::size_t value = line.find_first_not_of(" \t=", key_end);
        if (value == std::string::npos)
            continue;
        // A value opening with `#` is a colour and runs to the next space; a note somebody
        // left beside it starts at the `#` after that. The same distinction the reader
        // makes — made wrong here and no colour in the file would ever match.
        const std::size_t value_end = line[value] == '#' ? line.find_first_of(" \t", value)
                                                         : line.find_first_of(" \t#", value);
        if (line.substr(value, value_end == std::string::npos ? std::string::npos
                                                              : value_end - value) == colour)
            return true;
    }
    return false;
}

// How many lines of the file name this colour at all. A theme that was saved by appending
// instead of overwriting holds each colour it saved twice, and the reader takes whichever
// copy it liked — so the count is the thing worth asserting, not the text.
// (Kaida, 2026-11-21: "Overwriting a theme should also Overwrite it for real instead of
// appending to it. to avoid conflict like this".)
int theme_mentions(const std::string& text, const std::string& key) {
    int times = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t end = text.find('\n', at);
        const std::string line =
            text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? text.size() : end + 1;
        const std::size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#')
            continue;
        const std::size_t key_end = line.find_first_of(" \t=", start);
        if (line.substr(start, key_end == std::string::npos ? std::string::npos
                                                            : key_end - start) == key)
            ++times;
    }
    return times;
}

// Close a window the way a desktop closes one — an ordinary close, not a call into the
// teardown underneath it — and take the question it puts up inside itself, if it puts
// one up. The question is drawn by the terminal rather than by the desktop now, so it
// is caught on the widget instead of among the top-level windows, and it is answered
// the way a person answers it: with a key. The answer handed back is Esc, "not now",
// which leaves the window standing so a caller can close it again under different
// weather.
std::string closeAndCatchQuestion(kai::TerminalWidget& window) {
    QMetaObject::invokeMethod(&window, [&window] { window.close(); }, Qt::QueuedConnection);
    if (!waitFor([&window] { return window.overlayActive(); }, 2000))
        return {}; // nothing was asked; the window went about its closing quietly
    const std::string asked = window.overlayQuestion();
    sendKey(&window, Qt::Key_Escape);
    pump(150);
    return asked;
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const std::string root =
        "/tmp/kai-app-window-" + std::to_string(static_cast<long>(::getpid()));
    ::mkdir(root.c_str(), 0700);
    ::setenv("XDG_RUNTIME_DIR", root.c_str(), 1);
    ::setenv("KAI_SHELL", "/bin/sh", 1);
    // The window reads its settings and its theme from files now, and a test
    // must not be at the mercy of whoever runs it having opinions about their
    // colours. Point both at files that are not there; the blocks that want a
    // file set it themselves.
    ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
    ::setenv("KAI_TERM_THEME", (root + "/no-such-theme").c_str(), 1);

    Daemon daemon;
    std::string error;
    if (!daemon.start(error)) {
        std::cerr << "cannot start the daemon: " << error << "\n";
        return 1;
    }
    std::thread serving([&daemon] { daemon.run(); });
    pump(150);

    // A window that just needs a shell should not have to know that a daemon exists,
    // let alone have been started after one.
    kai::AppWindow window(kai::startWindowBackend(24, 80));
    window.resize(900, 600);
    window.show();
    const std::string first_session = window.sessionName();
    expect("the window has a shell", waitFor([&window] { return !window.sessionName().empty(); }));
    expect("held by the daemon, not by the window", first_session.rfind("w", 0) == 0);
    expect("and not kept, because nobody has asked to keep it", !window.sessionKept());
    expect("the daemon is watching that shell",
           waitFor([&daemon, &first_session] {
               return sees(daemon, first_session, [](const SessionInfo& info) {
                   return info.alive && info.clients == 1 && !info.saved;
               });
           }));

    runLine(&window, "M=FIRST; echo ${M}-WINDOW");
    expect("and the window is looking at it",
           waitFor([&window] { return onScreen(window).find("FIRST-WINDOW") != std::string::npos; }));

    // What the close prompt would be about. An empty name means it would not ask at
    // all, so a program has to be running for there to be a question.
    expect("a shell at a prompt is not something running", waitFor([&window] {
               return window.runningProgram().empty();
           }));
    runLine(&window, "sleep 3");
    expect("and a program is",
           waitFor([&window] { return window.runningProgram() == "sleep"; }));

    {
        // Ctrl+Shift+S: keep this shell, start a fresh one here. The window stays,
        // because what ended was the shell, not the window.
        window.saveCurrentSession();
        expect("the window says what it did",
               waitFor([&window] { return window.statusText().find("kept") != std::string::npos; }));
        expect("and the menu would now have something to offer",
               waitFor([&window] { return window.restorableSessions().size() == 1; }));
        const std::string kept = window.restorableSessions().front().name;

        expect("kept under a name a person would choose to type", kept.rfind("s", 0) == 0);
        expect("still holding what was on it",
               waitFor([&kept] { return writtenInto(kept).find("FIRST-WINDOW") != std::string::npos; }));
        // That was a view, even a short one that only wanted to know what the screen
        // said, and a view spends the keep. Nothing was lost by looking; the session is
        // simply an ordinary one again until somebody says keep, which is the whole rule
        // and the reason it is checked here rather than three lines away.
        expect("looking at it spent the keep",
               waitFor([&daemon, &kept] {
                   return sees(daemon, kept, [](const SessionInfo& info) {
                       return info.alive && !info.saved;
                   });
               }));
        expect("and saying keep again holds it", daemon.save(kept, std::string(), error));
        // Fresh, which is said about the screen rather than the name: renaming the kept
        // session frees its old name, and a new shell taking it is the daemon reusing
        // what is no longer in use.
        expect("the window itself starts fresh",
               waitFor([&window, &kept] {
                   return window.sessionName() != kept && !window.sessionKept() &&
                          onScreen(window).find("FIRST-WINDOW") == std::string::npos;
               }));
        expect("with a shell of its own to type into",
               waitFor([&daemon, &window] {
                   return sees(daemon, window.sessionName(), [](const SessionInfo& info) {
                       return info.alive && info.clients == 1 && !info.saved;
                   });
               }));
        expect("and the kept one has nobody looking at it",
               waitFor([&daemon, &kept] {
                   return sees(daemon, kept, [](const SessionInfo& info) {
                       return info.alive && info.saved && info.clients == 0;
                   });
               }));

        // Bringing it back. This is the click in the menu, with the pointer taken out.
        window.restoreSession(kept);
        expect("the kept session comes back into the same window",
               waitFor([&window, &kept] {
                   return window.sessionName() == kept &&
                          onScreen(window).find("FIRST-WINDOW") != std::string::npos;
               }));
        // The report this whole block is about. Bringing a kept session back into a
        // window is somebody looking at it, which is the entire reason it was kept, so
        // the keep is used up there and then: the window does not inherit it and the
        // daemon does not still hold it. Keeping it again is an act, not a memory.
        expect("and looking at it took the keep back", !window.sessionKept());
        expect("the daemon agrees that nobody has asked to keep it",
               waitFor([&daemon, &kept] {
                   return sees(daemon, kept, [](const SessionInfo& info) {
                       return info.alive && info.clients == 1 && !info.saved;
                   });
               }));

        // Kaida's case exactly: a session brought back into a window, something running
        // in it, the window closed. The session being kept says what happens to the
        // shell; it does not say the window may drop whatever is running in it, so this
        // asks. It used to close silently, which is how the report came in.
        runLine(&window, "sleep 6");
        expect("with something running in the session that came back",
               waitFor([&window] { return window.runningProgram() == "sleep"; }));
        const std::string asked = closeAndCatchQuestion(window);
        expect("closing over a kept session asks all the same: " + asked,
               asked == "Quit while sleep is running?");
        expect("and the window is still here, having been answered no", window.isVisible());
        // Let the sleep finish before the next close, which is about a window with
        // nothing in it and would otherwise find itself asking a question.
        expect("and the question can be answered no and the program left running",
               waitFor([&window] { return window.runningProgram().empty(); }, 9000));
        expect("the shell it was replaced by goes with the window that left it",
               waitFor([&daemon, &window, &first_session] {
                   return !sees(daemon, first_session, [](const SessionInfo&) { return true; });
               }));

        // Nothing is running, so nothing is asked. What happens next is the half Kaida
        // was objecting to: this session was saved once, and used to stay saved for the
        // rest of its life however many windows looked at it, so closing went quiet and
        // the shell went on running on a decision nobody made in that moment. Looking at
        // it spent the keep, so now the close ends it. Through the close rather than by
        // calling the teardown by hand: what the window decides is the thing under test,
        // and a test that made the call itself would pass even when the window never did.
        expect("nothing is running when the quiet close is checked",
               window.runningProgram().empty());
        QCloseEvent keep_close;
        QCoreApplication::sendEvent(&window, &keep_close);
        expect("the window lets itself close", keep_close.isAccepted());
        window.shutdown();
        expect("and closing it ends the session that was only ever saved before",
               waitFor([&daemon, &kept] {
                   return !sees(daemon, kept, [](const SessionInfo&) { return true; });
               }));
        // The menu leaves out the session being shown, so once it is back there is
        // nothing left to offer: the point of the list is the ones you are not in.
        expect("and the menu stops offering the one you are in",
               window.restorableSessions().empty());
        static_cast<void>(daemon.kill(kept, error));
    }

    {
        // What the close prompt is built from, asked the way the window asks it. The
        // prompt has never been seen by a person at a real desktop, and the reason it
        // goes missing could be either half: the window not knowing what is running, or
        // the desktop not asking the window to close at all. This rules the first half
        // in or out.
        kai::AppWindow busy(kai::startWindowBackend(24, 80));
        busy.show();
        static_cast<void>(waitFor([&busy] { return !busy.sessionName().empty(); }));
        expect("a window with a program running knows what it is",
               waitFor([&busy] {
                   busy.pasteText("sleep 120");
                   static_cast<void>(QApplication::sendEvent(
                       &busy, new QKeyEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier)));
                   pump(40);
                   return busy.runningProgram() == "sleep";
               }));
        expect("and that is worth asking about", !closeQuestion(busy.runningProgram()).empty());

        // The dialog itself, offscreen. The window is closed the way a desktop closes
        // one, and while the window is waiting for an answer another timer looks for the
        // question among the windows this process owns. Nothing here is seen by an eye:
        // what is being checked is whether the window asks at all, which is the thing
        // reported missing at a real desktop.
        // The word the window says about its own close, asked for and looked for in the
        // file rather than on a terminal: the window being asked about is normally the
        // one that was started from the terminal the line would have gone to, so a
        // diagnostic that can only be read by somebody watching the window being closed
        // is a diagnostic nobody can use.
        ::setenv("KAI_DEBUG_CLOSE", "1", 1);
        const std::string asked = closeAndCatchQuestion(busy);
        expect("a window with something running asks before it goes",
               asked.find("sleep") != std::string::npos);
        expect("the question says what it is worth: " + asked,
               asked.find("Quit while") != std::string::npos);
        expect("and the window is still here, having been answered no", busy.isVisible());
        {
            std::ifstream log(root + "/kai-term-close.log");
            const std::string written = std::string((std::istreambuf_iterator<char>(log)),
                                                    std::istreambuf_iterator<char>());
            expect("and the close said what it saw, where it could be read afterwards",
                   written.find("would ask: Quit while sleep is running?") != std::string::npos);
            expect("and said what the daemon had for it, including what was running",
                   written.find("fg=sleep") != std::string::npos);
        }
        ::unsetenv("KAI_DEBUG_CLOSE");

        // The question is drawn inside the terminal now, and it is answerable from
        // the keyboard. The buttons read Yes / No / Save Session, in that order; No
        // holds the focus, because Enter fires what is focused and the focused
        // answer at a question nobody has read yet must not be the one that ends a
        // build; and Yes, walked to and pressed, really does close the window.
        busy.close();
        expect("the question is put up inside the terminal itself", busy.overlayActive());
        const std::vector<std::string> answers = busy.overlayChoices();
        expect("the buttons read Yes / No / Save Session, in that order",
               answers.size() == 3 && answers[0] == "Yes" && answers[1] == "No" &&
                   answers[2] == "Save Session");
        expect("and No holds the focus, so Enter cannot end the build by accident",
               busy.overlaySelected() == 1);
        sendKey(&busy, Qt::Key_Right);
        expect("Right walks from No to Save Session", busy.overlaySelected() == 2);
        sendKey(&busy, Qt::Key_Left);
        expect("and Left walks back", busy.overlaySelected() == 1);
        // The first letter of an answer answers it wherever the focus is: a
        // person who can read "Yes" should not have to walk to it.
        sendKey(&busy, Qt::Key_Y);
        expect("Y answers Yes without touching the focus, and closes the window",
               waitFor([&busy] { return !busy.isVisible(); }));
        expect("with the question gone along with it", !busy.overlayActive());

        // Said out loud, because the shell in it is running something: this window is
        // being closed by a person, which is the word that ends a shell nobody kept.
        // Left unsaid, the session would be left behind and the next check would find
        // it there and call it a bug in something else.
        busy.discardOnClose();
        busy.shutdown();
        expect("and closing it takes the session with it",
               waitFor([&daemon] { return daemon.list().empty(); }));
    }

    {
        // A window whose shell was never kept, closed with nothing running — the quiet
        // path, where there is nothing worth asking about. Quiet is not the same as
        // harmless: this close still ends a shell nobody asked to keep. It did not use
        // to, and every window closed this way left a session behind that then looked,
        // in `session list`, like one that had been kept.
        const std::string plain_name = [&] {
            kai::AppWindow plain(kai::startWindowBackend(24, 80));
            plain.show();
            static_cast<void>(waitFor([&plain] { return !plain.sessionName().empty(); }));
            expect("a plain window has a shell", !plain.sessionName().empty());
            expect("and nothing to go back to", plain.restorableSessions().empty());
            plain.showSessionMenu();
            expect("with nothing kept, the list says so inside the terminal, one button wide",
                   plain.overlayActive() && plain.overlayChoices().size() == 1 &&
                       plain.overlayChoices()[0] == "OK");
            sendKey(&plain, Qt::Key_Escape);
            expect("which Esc puts away", !plain.overlayActive());
            expect("with nobody else looking at it",
                   waitFor([&daemon, &plain] {
                       return sees(daemon, plain.sessionName(),
                                   [](const SessionInfo& info) { return info.clients == 1; });
                   }));
            const std::string name = plain.sessionName();
            QCloseEvent close;
            QCoreApplication::sendEvent(&plain, &close);
            expect("and it lets itself be closed", close.isAccepted());
            return name;
        }(); // the window goes out of scope here, which is the teardown a person causes
        expect("closing it ends the shell nobody kept",
               waitFor([&daemon, &plain_name] {
                   return !sees(daemon, plain_name, [](const SessionInfo&) { return true; });
               }));
        expect("and nothing at all is left running",
               waitFor([&daemon] { return daemon.list().empty(); }));
    }

    {
        // `KAI_CONFIRM_QUIT=always` reaching the window, rather than only being written
        // down. A shell at a prompt has nothing to lose and is closed without a word, so
        // it is the cleanest proof there is: the same window, asked again with nothing
        // changed but the setting, opens its mouth.
        kai::AppWindow quiet(kai::startWindowBackend(24, 80));
        quiet.show();
        static_cast<void>(waitFor([&quiet] { return !quiet.sessionName().empty(); }));
        QCloseEvent at_rest;
        QCoreApplication::sendEvent(&quiet, &at_rest);
        expect("a shell at a prompt goes down without a word by default", at_rest.isAccepted());
        ::setenv("KAI_CONFIRM_QUIT", "always", 1);
        const std::string asked = closeAndCatchQuestion(quiet);
        expect("and is asked about once the setting says always: " + asked, !asked.empty());
        expect("the question stays true with nothing running: " + asked,
               asked.find("while") == std::string::npos && asked.back() == '?');
        expect("and the window is still here, having been answered no", quiet.isVisible());
        ::unsetenv("KAI_CONFIRM_QUIT");
        quiet.discardOnClose();
        quiet.shutdown();
        pump(200);
    }

    {
        // The config file reaching a real window: colours the widget paints
        // with, the closing answer taken from the file, the environment still
        // outranking it, and a typo in the file said out loud instead of
        // eaten. The parser itself is checked headless in tests/config.cpp;
        // this is the wiring from the file into the window.
        const std::string cfg = root + "/themed-config";
        {
            std::ofstream out(cfg);
            out << "# window test\n"
                   "confirm_quit = always\n"
                   "background = #123456\n"
                   "foreground = #fedcba\n"
                   "accent = #ff00ff\n"
                   "confirm_exit = never\n"; // the typo, announced not skipped
        }
        ::setenv("KAI_TERM_CONFIG", cfg.c_str(), 1);
        // The complaint about the typo'd key is meant for a person's terminal
        // at startup, so the window is built with stderr aimed at a file and
        // what it said is read back — a warning nobody can prove was printed
        // is a warning that can silently stop being.
        const std::string note_file = root + "/config-notes";
        ::fflush(stderr);
        const int saved_err = ::dup(STDERR_FILENO);
        const int sink = ::open(note_file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (sink >= 0)
            ::dup2(sink, STDERR_FILENO);
        kai::AppWindow themed(kai::startWindowBackend(24, 80));
        ::fflush(stderr);
        if (sink >= 0) {
            ::dup2(saved_err, STDERR_FILENO);
            ::close(sink);
        }
        ::close(saved_err);
        themed.show();
        static_cast<void>(waitFor([&themed] { return !themed.sessionName().empty(); }));
        {
            std::ifstream notes(note_file);
            const std::string said((std::istreambuf_iterator<char>(notes)),
                                   std::istreambuf_iterator<char>());
            expect("the typo'd key is said out loud, with its file and line: " + said,
                   said.find("confirm_exit") != std::string::npos &&
                       said.find(cfg) != std::string::npos);
            expect("and the settings it understood are not complained about",
                   said.find("confirm_quit") == std::string::npos &&
                       said.find("accent") == std::string::npos);
        }
        expect("the file's background is the screen's background",
               themed.screen().defaultBackground().r == 0x12 &&
                   themed.screen().defaultBackground().g == 0x34 &&
                   themed.screen().defaultBackground().b == 0x56);
        expect("and its foreground the default ink",
               themed.theme().foreground.r == 0xFE && themed.theme().foreground.b == 0xBA);
        expect("the accent the dialogs wear comes from the file too",
               themed.theme().accent.r == 0xFF && themed.theme().accent.b == 0xFF);
        // The file's `confirm_quit = always` reaching the close path: a shell
        // with nothing running is asked, where the default would let it go.
        const std::string asked = closeAndCatchQuestion(themed);
        expect("confirm_quit from the file reaches the close prompt: " + asked,
               !asked.empty());
        ::setenv("KAI_CONFIRM_QUIT", "never", 1);
        QCloseEvent overridden;
        QCoreApplication::sendEvent(&themed, &overridden);
        expect("and the environment still outranks the file", overridden.isAccepted());
        ::unsetenv("KAI_CONFIRM_QUIT");
        themed.discardOnClose();
        themed.shutdown();
        pump(200);
        std::remove(cfg.c_str());
        std::remove(note_file.c_str());
        // Back to isolation, for every window built after this one.
        ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
        ::setenv("KAI_TERM_THEME", (root + "/no-such-theme").c_str(), 1);
    }

    {
        // The window's body follows the file too, not just its colours:
        // `font_size`, `cols`, `rows`, `opacity` are the settings a person
        // writes by hand, and a file that is read but not obeyed is worse
        // than one that is refused — it teaches people that config files lie.
        const std::string body = root + "/body-config";
        {
            std::ofstream out(body);
            out << "font_size = 12\ncols = 100\nrows = 30\nopacity = 0.8\n";
        }
        ::setenv("KAI_TERM_CONFIG", body.c_str(), 1);
        kai::AppWindow shaped(kai::startWindowBackend(24, 80));
        shaped.show();
        pump(150);
        expect("the config's font size is the window's font", shaped.fontPixelSize() == 12);
        expect("and the window opens at the config's grid, not the built-in one",
               shaped.cols() == 100 && shaped.rows() == 30);
        // Not `windowOpacity()`. That call asks the X11 window manager for
        // `_NET_WM_WINDOW_OPACITY`; a Wayland compositor has no such request, so the
        // number was stored, reported back as 0.8 by the offscreen platform, and
        // done nothing at all on the desktop the setting was written for. The real
        // thing is the window's own alpha, so that is what is checked here and in
        // the transparency block below.
        expect("and the config's opacity is the window's background alpha",
               qFuzzyCompare(shaped.backgroundOpacity(), 0.8));
        expect("with a surface that carries an alpha channel to show for it",
               shaped.testAttribute(Qt::WA_TranslucentBackground));
        shaped.discardOnClose();
        shaped.shutdown();
        pump(200);
        std::remove(body.c_str());
        ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
    }

    {
        // A colour in the config is a LOCK, and a lock outranks the session — the
        // thing Kaida reported: the theme washed back to defaults "when starting
        // fresh, when starting a new session, and even when attaching an old
        // session that was previously themed," because fish and prompt tools write
        // OSC 10/11 at startup and a session's snapshot carries its own colours.
        // The config now wins over both, and a session that wrote a colour the
        // config never named is still heard.
        const std::string cfg = root + "/authoritative";
        {
            std::ofstream out(cfg);
            out << "background = #022223\nforeground = #00f0a0\ncolor1 = #af0f0f\n";
        }
        ::setenv("KAI_TERM_CONFIG", cfg.c_str(), 1);
        ::setenv("KAI_TERM_THEME", (root + "/no-such-theme").c_str(), 1);

        // One window, whose shell paints its OWN background magenta and re-colours
        // red green — the session's real colours, deliberately different from the
        // theme so the checks below cannot pass by accident.
        kai::AppWindow authoring(kai::startWindowBackend(24, 80));
        authoring.show();
        static_cast<void>(waitFor([&authoring] { return !authoring.sessionName().empty(); }));
        const std::string shared = authoring.sessionName();

        // Kaida's exact symptom: does COMMAND OUTPUT wear the theme on the very first
        // window, before Ctrl+Shift+P is ever opened? The snapshot that arrived with
        // the window reset the Screen's raw default colours to the session's; the lock
        // has to govern what text is WRITTEN, not only what the accessors report. Echo
        // a token and read the ink of the cell it landed in — the bug painted it with
        // the session's colour and only the picker's setDefaults corrected it.
        runLine(&authoring, "echo ZEBRA");
        expect("command output appears", waitFor(
                   [&authoring] { return onScreen(authoring).find("ZEBRA") != std::string::npos; }));
        const Color zebra_ink = lastCellFgWithText(authoring, "ZEBRA");
        expect("plain command output wears the theme's ink before the picker is opened",
               zebra_ink.r == 0x00 && zebra_ink.g == 0xF0 && zebra_ink.b == 0xA0);

        // Resizing must not knock the theme off. A resize makes the session resend a
        // snapshot carrying its own colours, and the lock has to survive it — Kaida's
        // "colour change on resize and stop following the theme."
        const QSize before = authoring.size();
        authoring.resize(before.width() + 160, before.height() + 80);
        pump(400);
        expect("the theme still holds the background after a resize",
               authoring.screen().defaultBackground().r == 0x02 &&
                   authoring.screen().defaultBackground().g == 0x22 &&
                   authoring.screen().defaultBackground().b == 0x23);
        runLine(&authoring, "echo ZEBRA2");
        static_cast<void>(waitFor(
            [&authoring] { return onScreen(authoring).find("ZEBRA2") != std::string::npos; }));
        const Color zebra2_ink = lastCellFgWithText(authoring, "ZEBRA2");
        expect("command output after a resize still wears the theme's ink",
               zebra2_ink.r == 0x00 && zebra2_ink.g == 0xF0 && zebra2_ink.b == 0xA0);

        // The shell repaints its own background magenta and re-colours palette
        // entry 1 green, then prints a token in that entry — all in one command, so
        // the token, the recolours and the session's own colours are on the screen
        // together and the checks below cannot pass by accident.
        runLine(&authoring,
                "printf '\\033]11;rgb:ff/00/ff\\007\\033]4;1;rgb:00/ff/00\\007"
                "\\033[31mPALTOKEN\\033[0m\\n'; sleep 40");
        pump(500);

        // The window that ran those bytes still wears the theme: the lock held.
        expect("the config background survives the shell writing OSC 11",
               authoring.screen().defaultBackground().r == 0x02 &&
                   authoring.screen().defaultBackground().g == 0x22 &&
                   authoring.screen().defaultBackground().b == 0x23);
        expect("and the config palette survives the shell writing OSC 4",
               authoring.screen().paletteColor(1).r == 0xAF &&
                   authoring.screen().paletteColor(1).g == 0x0F &&
                   authoring.screen().paletteColor(1).b == 0x0F);
        expect("the token is on the screen in the locked entry",
               waitFor([&authoring] {
                   return onScreen(authoring).find("PALTOKEN") != std::string::npos;
               }));
        expect("and it is painted with the config's red, not the session's green",
               lastCellFgWithText(authoring, "PALTOKEN").r == 0xAF);

        // The 16 palette colours, which is where Kaida saw the theme come off:
        // "the color with the bug are the 16 default color those are the one getting
        // resetted when resizing". A resize makes the session re-send the whole
        // screen, and a snapshot that writes each cell's resolved RGB is a colour no
        // lock can touch — the cell came back wearing the SESSION's entry 1, which
        // the shell had just painted green. A cell now remembers the index it was
        // asked for, the index is what travels, and the view resolves it through its
        // own lock again, so the resize repaints the theme rather than the session.
        authoring.resize(authoring.width() + 200, authoring.height() + 100);
        pump(600);
        expect("the palette token is still on the screen after a resize",
               waitFor([&authoring] {
                   return onScreen(authoring).find("PALTOKEN") != std::string::npos;
               }));
        const Color pal_after = lastCellFgWithText(authoring, "PALTOKEN");
        expect("and it still wears the locked palette colour after a resize",
               pal_after.r == 0xAF && pal_after.g == 0x0F && pal_after.b == 0x0F);
        expect("so it did not fall back to the session's green", pal_after.g != 0xFF);

        // Control: the session really is magenta underneath. Read its snapshot into a
        // plain, unlocked screen — exactly what an attaching window would show without
        // the lock — so the checks above are known to be about the lock and not about
        // a shell that never wrote anything.
        const bool session_is_magenta = [&shared] {
            DaemonClient client;
            std::string err;
            if (!client.connect(err))
                return false;
            if (!client.request({{"command", "attach"},
                                 {"name", shared},
                                 {"rows", "24"},
                                 {"cols", "80"}}))
                return false;
            Frame welcome;
            if (!client.nextFrame(welcome, err))
                return false;
            Frame snapshot;
            std::string raw;
            if (client.nextFrame(snapshot, err) &&
                (snapshot.kind == MsgKind::Snapshot || snapshot.kind == MsgKind::Data))
                raw = snapshot.body;
            client.close();
            Screen plain(24, 80); // no lock: this is the session's own colour
            plain.feed(raw);
            const Color bg = plain.defaultBackground();
            return bg.r == 0xFF && bg.g == 0x00 && bg.b == 0xFF;
        }();
        expect("the session really did paint itself magenta (so the lock is what hides it)",
               session_is_magenta);

        // Attaching a second window to that same session shows the theme, not the
        // session's magenta — Kaida's "attaching an old session" case.
        kai::AppWindow attaching(shared);
        attaching.show();
        pump(300);
        expect("an attached window wears the config theme, not the session's colours",
               attaching.screen().defaultBackground().r == 0x02 &&
                   attaching.screen().defaultBackground().g == 0x22 &&
                   attaching.screen().defaultBackground().b == 0x23);
        expect("and the config palette there too",
               attaching.screen().paletteColor(1).r == 0xAF &&
                   attaching.screen().paletteColor(1).b == 0x0F);

        attaching.discardOnClose();
        attaching.shutdown();
        authoring.discardOnClose();
        authoring.shutdown();
        pump(200);
        std::remove(cfg.c_str());
        ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
    }

    {
        // A colour the config did NOT name is still the session's to set — the lock
        // is only over what the person actually chose, so a program that legitimately
        // recolours is not silenced. With an empty config, the shell's own OSC 11
        // moves the background, exactly as it always did.
        const std::string empty = root + "/empty-config";
        std::ofstream(empty) << "# nothing here\n";
        ::setenv("KAI_TERM_CONFIG", empty.c_str(), 1);
        kai::AppWindow unlocked(kai::startWindowBackend(24, 80));
        unlocked.show();
        static_cast<void>(waitFor([&unlocked] { return !unlocked.sessionName().empty(); }));
        runLine(&unlocked, "printf '\\033]11;rgb:1e/1e/1e\\007'; sleep 20");
        pump(400);
        expect("a colour the config left alone still follows the session",
               unlocked.screen().defaultBackground().r == 0x1E &&
                   unlocked.screen().defaultBackground().g == 0x1E);
        unlocked.discardOnClose();
        unlocked.shutdown();
        pump(200);
        std::remove(empty.c_str());
        ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
    }

    {
        // The theme picker end to end, driven by keys through a real window:
        // Ctrl+Shift+P opens it over the grid, hex typed at it changes the
        // window as you go (the preview is the window), Enter writes the
        // changed colours to the theme file and leaves the settings file
        // alone, and Esc puts the window back with nothing written. What the
        // picker decides is checked headless in tests/theme_picker.cpp; this
        // is the path from the keystroke to the disk.
        const std::string cfg = root + "/picker-config";
        const std::string theme = root + "/picker-theme";
        // A settings file that names a grid, so the window opens at a size the
        // config chose and then gets resized away from it by hand. That is the only
        // shape where a colour save could steal the window's size: re-reading the
        // config after a save applied `cols`/`rows` a second time and snapped the
        // window back to the configured grid, out from under the person who had just
        // resized it (Kaida, 2026-11-19: "it resize my terminal when a new theme is
        // saved"). Saving colours is about colours.
        std::ofstream(cfg) << "cols = 90\nrows = 28\n";
        ::setenv("KAI_TERM_CONFIG", cfg.c_str(), 1);
        ::setenv("KAI_TERM_THEME", theme.c_str(), 1);
        kai::AppWindow window(kai::startWindowBackend(24, 80));
        window.show();
        static_cast<void>(waitFor([&window] { return !window.sessionName().empty(); }));
        window.resize(640, 380);
        pump(200);

        const auto press = [&window](int key, const QString& text = QString()) {
            QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(&window, &ev);
        };
        const auto type_hex = [&press](const std::string& digits) {
            for (char c : digits) {
                const int key = (c >= '0' && c <= '9')
                                    ? (Qt::Key_0 + (c - '0'))
                                    : (Qt::Key_A + (c - 'a'));
                press(key, QString(QChar(c)));
            }
        };
        // Pressing the panel's left-hand word, wherever it was painted. It is not always
        // `save`: once a theme is already sitting at that name the word is `overwrite`,
        // and the question is answered on the same spot the ask appeared.
        const auto click_word = [&window](bool left) {
            const QPointF at = left ? window.pickerSavePoint() : window.pickerCancelPoint();
            QMouseEvent down(QEvent::MouseButtonPress, at, window.mapToGlobal(at.toPoint()),
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &down);
            QMouseEvent up(QEvent::MouseButtonRelease, at, window.mapToGlobal(at.toPoint()),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &up);
            pump(60);
        };

        // Up from the settings menu on to the theme row, and Enter.
        openThemeEditor(&window);
        pump(60);
        expect("the theme row opens the colour editor", window.pickerActive());

        // The picker owns the keyboard: a keystroke that is not a colour or a
        // navigation key is swallowed, not sent to the shell. (We check the
        // picker is still up and the shell got nothing it could echo.)
        press(Qt::Key_Q, "q");
        expect("the picker is still up after a stray letter", window.pickerActive());

        // Walk on to a named colour — the first row is the ground, the second the ink on
        // it — type six digits, and watch the window change under the keystroke.
        press(Qt::Key_Down); // background, then foreground
        const Color before = window.theme().foreground;
        type_hex("11ff00");
        pump(40);
        expect("typing hex repaints the live window (the preview is the window)",
               window.theme().foreground.r == 0x11 && window.theme().foreground.g == 0xFF &&
                   window.theme().foreground.b == 0x00);

        // Enter saves: the theme file gets the new colour, the settings file — a
        // different file — is not written by it, and the window keeps the size it is
        // at. That last one is the whole of the second complaint: a save re-reads
        // the config to refresh the locks, and re-reading must not re-apply the
        // body.
        const int kept_rows = window.rows();
        const int kept_cols = window.cols();
        const int kept_height = window.height();
        // Saving is the word at the bottom of the panel, reached by walking; Enter
        // on a colour now opens the mixer instead (see the mixer block below).
        {
            const QPointF save = window.pickerSavePoint();
            QMouseEvent ev(QEvent::MouseButtonPress, save, window.mapToGlobal(save.toPoint()),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &ev);
            QMouseEvent up(QEvent::MouseButtonRelease, save, window.mapToGlobal(save.toPoint()),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &up);
        }
        pump(60);
        expect("`save` closes the picker", !window.pickerActive());
        expect("saving colours does not resize the window",
               window.rows() == kept_rows && window.cols() == kept_cols);
        expect("and does not resize the window's pixels either",
               window.height() == kept_height);
        {
            std::ifstream in(theme);
            const std::string written((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
            expect("the saved colour is in the theme file: " + written,
                   theme_says(written, "foreground", "#11ff00"));
            expect("and the theme was rewritten, not added to: that colour is named once",
                   theme_mentions(written, "foreground") == 1);
            std::ifstream settings(cfg);
            const std::string settings_text((std::istreambuf_iterator<char>(settings)),
                                            std::istreambuf_iterator<char>());
            expect("and a colour save writes nothing into the settings file: " + settings_text,
                   settings_text == "cols = 90\nrows = 28\n");
        }

        // A fresh window reads that colour back from the theme file.
        kai::AppWindow reopened(kai::startWindowBackend(24, 80));
        expect("the next window reads the chosen colour from the theme file",
               reopened.theme().foreground.r == 0x11 &&
                   reopened.theme().foreground.g == 0xFF &&
                   reopened.theme().foreground.b == 0x00);
        reopened.discardOnClose();
        reopened.shutdown();
        pump(200);

        // Cancel writes nothing and leaves the colours as they were.
        const Color as_they_were = window.theme().background;
        openThemeEditor(&window);
        pump(60);
        type_hex("000000"); // the keys start on the background
        pump(40);
        expect("the cancel path previews too (the window changed)",
               window.theme().background.r == 0 && window.theme().background.g == 0);
        press(Qt::Key_Escape);
        pump(60);
        expect("Esc closes the picker", !window.pickerActive());
        expect("and puts the window's colours back",
               window.theme().background.r == as_they_were.r &&
                   window.theme().background.g == as_they_were.g &&
                   window.theme().background.b == as_they_were.b);
        {
            std::ifstream in(theme);
            const std::string written((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
            expect("and the theme file never heard about the colour that was left",
                   !theme_says(written, "background", "#000000"));
        }

        // A palette row is a row like any other: walk down past the six
        // window colours to `black`, the first palette entry, and the same
        // six digits edit it, save it under its file spelling (`color0`),
        // and hand it to the next window.
        openThemeEditor(&window);
        pump(60);
        for (int i = 0; i < 6; ++i)
            press(Qt::Key_Down); // background … selection foreground, and then black
        type_hex("ff0000");
        pump(40);
        // The theme file exists by now — the save above made it — so writing to the same
        // name is the case that asks first. The answer is the same word, in the same
        // place, pressed a second time.
        // (Kaida, 2026-11-20: "saving under the loaded name asks to overwrite, a new
        // name writes a new theme".)
        click_word(true); // `save`
        expect("writing over a theme that is already there is asked about first",
               window.pickerActive() && window.pickerAskingOverwrite() &&
                   window.pickerTitleText() == QStringLiteral("Overwrite Default.conf?"));
        click_word(false); // `keep it`
        pump(60);
        expect("and `keep it` leaves the file and the colours it holds alone",
               !window.pickerActive());
        {
            std::ifstream in(theme);
            const std::string kept((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            expect("the colour you walked away from is not in the file",
                   !theme_says(kept, "color0", "#ff0000"));
        }
        // Asked twice is not the same as unable: the same walk, answered yes.
        openThemeEditor(&window);
        pump(60);
        for (int i = 0; i < 6; ++i)
            press(Qt::Key_Down); // background … selection foreground, and then black
        type_hex("ff0000");
        pump(40);
        click_word(true); // `save`
        click_word(true); // `overwrite`
        {
            std::ifstream in(theme);
            const std::string written((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
            expect("the palette entry saves under its file spelling: " + written,
                   theme_says(written, "color0", "#ff0000"));
            expect("over the copy the file already had, rather than under it",
                   theme_mentions(written, "color0") == 1);
        }
        kai::AppWindow repainted(kai::startWindowBackend(24, 80));
        expect("and the next window carries the re-coloured black into its palette",
               repainted.theme().palette[0].r == 0xFF &&
                   repainted.theme().palette[0].g == 0 &&
                   repainted.theme().palette[0].b == 0);
        repainted.discardOnClose();
        repainted.shutdown();
        pump(200);
        // The loop a person actually takes, and the one that left a theme holding every
        // colour twice: the editor writes a theme; later the editor is opened on that
        // theme, one colour is changed, and the save goes back over the file already there.
        // That file is written the way themes are written — no `=` — and a save that
        // recognised only a line holding `=` matched nothing in it, so every colour went on
        // again underneath the first and the reader took whichever copy it liked.
        // (Kaida, 2026-11-21: "loading a theme.conf of the new layout and editing the value
        // and saving result in this ... duplicating entry for each Value".)
        const std::string loop_cfg = root + "/loop-config";
        const std::string loop_theme = root + "/loop-theme.conf";
        std::ofstream(loop_cfg) << "cols = 90\nrows = 28\n";
        std::ofstream(loop_theme) << "# kai-term colours\n"
                                     "background            #f7f7f7\n"
                                     "foreground            #494542\n";
        ::setenv("KAI_TERM_CONFIG", loop_cfg.c_str(), 1);
        ::setenv("KAI_TERM_THEME", loop_theme.c_str(), 1);
        {
            kai::AppWindow worn(kai::startWindowBackend(24, 80));
            worn.show();
            static_cast<void>(waitFor([&worn] { return !worn.sessionName().empty(); }));
            const auto tap = [&worn](int key, const QString& text = QString()) {
                QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier, text);
                QCoreApplication::sendEvent(&worn, &ev);
            };
            const auto touch_word = [&worn](bool left) {
                const QPointF at = left ? worn.pickerSavePoint() : worn.pickerCancelPoint();
                QMouseEvent down(QEvent::MouseButtonPress, at, worn.mapToGlobal(at.toPoint()),
                                 Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(&worn, &down);
                QMouseEvent up(QEvent::MouseButtonRelease, at, worn.mapToGlobal(at.toPoint()),
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(&worn, &up);
                pump(60);
            };
            expect("a theme written without the sign is worn by the window that names it",
                   worn.theme().background.r == 0xF7 && worn.theme().foreground.g == 0x45);
            openThemeEditor(&worn);
            pump(60);
            for (const char digit : std::string("12ab34")) { // the editor opens on background
                const int key = (digit >= '0' && digit <= '9')
                                    ? (Qt::Key_0 + (digit - '0'))
                                    : (Qt::Key_A + (digit - 'a'));
                tap(key, QString(QChar(digit)));
            }
            pump(40);
            touch_word(true); // `save`
            touch_word(true); // and the ask to write over the theme that is already there
            pump(80);
            std::ifstream in(loop_theme);
            const std::string text((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            expect("saving over a theme written without the sign changes it where it stood: " +
                       text,
                   theme_says(text, "background", "#12ab34"));
            expect("and holds that colour once, not once and again below it",
                   theme_mentions(text, "background") == 1);
            expect("the colour this save had no reason to touch is still there, once",
                   theme_says(text, "foreground", "#494542") &&
                       theme_mentions(text, "foreground") == 1);
            expect("and nothing in the file went out in the deprecated shape",
                   text.find('=') == std::string::npos);
            worn.discardOnClose();
            worn.shutdown();
        }
        pump(200);

        window.discardOnClose();
        window.shutdown();
        pump(200);
        std::remove(cfg.c_str());
        std::remove(theme.c_str());
        ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
        ::setenv("KAI_TERM_THEME", (root + "/no-such-theme").c_str(), 1);
    }

    {
        // Transparency, end to end. The settings file names the number, the window
        // reads it, the picker previews it as you type, and a save writes it back to
        // the SETTINGS file — next to `cols` and `font_size`, where how-the-window-
        // behaves lives — and not into the colours file, where a number nothing
        // there reads would sit. And the window becomes a surface with an alpha
        // channel, which is the only kind of transparency a Wayland compositor can
        // show: `_NET_WM_WINDOW_OPACITY` is an X11 request that goes nowhere here,
        // and where it does work it fades the text along with the background.
        const std::string glass_cfg = root + "/glass-settings";
        const std::string glass_theme = root + "/glass-theme";
        std::ofstream(glass_cfg) << "background_opacity = 0.6\n";
        ::setenv("KAI_TERM_CONFIG", glass_cfg.c_str(), 1);
        ::setenv("KAI_TERM_THEME", glass_theme.c_str(), 1);
        kai::AppWindow window(kai::startWindowBackend(24, 80));
        window.show();
        static_cast<void>(waitFor([&window] { return !window.sessionName().empty(); }));

        expect("the file's background opacity reaches the window",
               qFuzzyCompare(window.backgroundOpacity(), 0.6));
        expect("and the ink stays solid unless the file says otherwise",
               qFuzzyCompare(window.foregroundOpacity(), 1.0));
        expect("so the window asks for a surface with an alpha channel",
               window.testAttribute(Qt::WA_TranslucentBackground));

        const auto press_key = [&window](int key, const QString& text = QString()) {
            QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(&window, &ev);
        };
        const auto click = [&window](const QPointF& at) {
            const QPointF global = window.mapToGlobal(at.toPoint());
            QMouseEvent down(QEvent::MouseButtonPress, at, global, Qt::LeftButton,
                             Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &down);
            QMouseEvent up(QEvent::MouseButtonRelease, at, global, Qt::LeftButton,
                           Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &up);
        };
        // Transparency is a setting, not a colour, so it is edited in the settings
        // menu — the second row there, under the font size.
        const auto open_picker = [&window] {
            QKeyEvent ev(QEvent::KeyPress, Qt::Key_K,
                         Qt::ControlModifier | Qt::ShiftModifier);
            QCoreApplication::sendEvent(&window, &ev);
        };

        open_picker();
        pump(60);
        expect("the settings menu opens", window.settingsActive());
        press_key(Qt::Key_Down); // font size, then background opacity
        pump(40);
        press_key(Qt::Key_0, "0");
        press_key(Qt::Key_4, "4");
        press_key(Qt::Key_0, "0");
        pump(60);
        expect("typing on the transparency row moves the real alpha as you go",
               qFuzzyCompare(window.backgroundOpacity(), 0.4));

        const int rows_before = window.rows();
        const int cols_before = window.cols();
        click(window.settingsSavePoint());
        pump(80);
        expect("saving transparency does not resize the window",
               window.rows() == rows_before && window.cols() == cols_before);
        {
            std::ifstream in(glass_cfg);
            const std::string written((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
            expect("the percentage is written to the settings file as a fraction: " + written,
                   written.find("background_opacity = 0.4") != std::string::npos);
        }
        {
            std::ifstream in(glass_theme);
            expect("and never into the colours file", !in.good());
        }

        kai::AppWindow reopened(kai::startWindowBackend(24, 80));
        expect("the next window reads the transparency back from the file",
               qFuzzyCompare(reopened.backgroundOpacity(), 0.4));
        reopened.discardOnClose();
        reopened.shutdown();
        pump(200);

        window.discardOnClose();
        window.shutdown();
        pump(200);
        std::remove(glass_cfg.c_str());
        std::remove(glass_theme.c_str());
        ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
        ::setenv("KAI_TERM_THEME", (root + "/no-such-theme").c_str(), 1);
    }

    {
        // The settings menu, end to end. Ctrl+Shift+K opens it over the grid, the
        // preview is the live window, leaving puts the window back and touches no
        // file, and saving writes the lines that moved in the words the reader takes
        // back. The mouse drives it too: a panel only the keyboard can answer is a
        // panel you cannot finish with the thing your hand is already on.
        const std::string menu_cfg = root + "/menu-settings";
        std::ofstream(menu_cfg) << "font_size = 12\npadding = 6\ncursor_blink = 2000\n";
        ::setenv("KAI_TERM_CONFIG", menu_cfg.c_str(), 1);
        ::setenv("KAI_TERM_THEME", (root + "/no-such-theme").c_str(), 1);
        kai::AppWindow window(kai::startWindowBackend(24, 80));
        window.show();
        static_cast<void>(waitFor([&window] { return !window.sessionName().empty(); }));
        pump(120);
        expect("the config's font size and padding reach the window",
               window.fontPixelSize() == 12 && window.padding() == 6.0);

        const auto press_key = [&window](int key, const QString& text = QString()) {
            QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(&window, &ev);
        };
        const auto open_menu = [&window] {
            QKeyEvent ev(QEvent::KeyPress, Qt::Key_K,
                         Qt::ControlModifier | Qt::ShiftModifier);
            QCoreApplication::sendEvent(&window, &ev);
        };
        const auto click = [&window](const QPointF& at) {
            QMouseEvent press(QEvent::MouseButtonPress, at, at, Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, at, at, Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &release);
        };
        const auto fileText = [&menu_cfg] {
            std::ifstream in(menu_cfg);
            return std::string((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        };

        open_menu();
        pump(80);
        expect("Ctrl+Shift+K opens the settings menu over the grid",
               window.settingsActive());
        expect("the menu paints where a pointer can reach it",
               !window.settingsRowRect(0).isNull());

        // The first row is the font. Typing into it changes the window as you go,
        // because a preview of a font that is not the font is no preview.
        press_key(Qt::Key_1, "1");
        press_key(Qt::Key_8, "8");
        pump(80);
        expect("typing a font size changes the window while you type",
               window.fontPixelSize() == 18);

        // Leaving is not saving.
        press_key(Qt::Key_Escape);
        pump(80);
        expect("leaving the menu puts the window back the way it was",
               !window.settingsActive() && window.fontPixelSize() == 12);
        expect("and leaves the file exactly as it was",
               fileText().find("font_size = 12") != std::string::npos);

        // Saving writes the row that moved, in the file's own words.
        open_menu();
        pump(80);
        press_key(Qt::Key_1, "1");
        press_key(Qt::Key_6, "6");
        pump(60);
        // The blink row gets nudged on the way, so the save is checked against a row
        // whose value is not the two-digit font size it was already holding.
        // (Kaida, 2026-11-21: "customizable blink speed ( 0 being no blinking)".)
        for (int step = 0; step < 6; ++step) {
            press_key(Qt::Key_Down);
            pump(20);
        }
        const bool on_blink = window.settingsRowKey(window.settingsCursor().first) ==
                              QStringLiteral("cursor_blink");
        press_key(Qt::Key_Minus);
        pump(60);
        click(window.settingsSavePoint());
        pump(100);
        expect("saving writes the font size to the settings file",
               fileText().find("font_size = 16") != std::string::npos);
        expect("and writes the blink rate the same way",
               on_blink && fileText().find("cursor_blink = 1950") != std::string::npos);
        expect("the window keeps what was chosen", window.fontPixelSize() == 16);
        expect("a line nobody touched is left alone",
               fileText().find("padding = 6") != std::string::npos);

        // The mouse: the row's `+`, then the word `save`.
        open_menu();
        pump(120);
        const int padding_row = 3; // font, background opacity, foreground opacity, padding
        const QPointF plus = window.settingsStepPoint(padding_row, true);
        expect("every row paints a plus a pointer can hit", !plus.isNull());
        click(plus);
        pump(80);
        expect("clicking + on the padding row moves the window", window.padding() == 7.0);
        click(window.settingsSavePoint());
        pump(100);
        expect("clicking save writes it", fileText().find("padding = 7") != std::string::npos);
        expect("and the menu closes when you click it", !window.settingsActive());

        // The two words at the bottom are reachable by walking down: past the last
        // row is `save`, past that `cancel`, past that the first row again. One ring
        // rather than a chord to remember, so the gesture that walks the rows is the
        // same gesture that reaches the words. (Kaida, 2026-11-20: "no tab to reach
        // save and leave, they should be reachable when going down. the interfaces
        // must be Cohesive.")
        const int row_count = window.settingsRowCount(); // asked, not counted by hand
        const auto walk = [&window, &press_key](int steps) {
            for (int i = 0; i < steps; ++i) {
                press_key(Qt::Key_Down);
                pump(20);
            }
        };
        // Steps from the padding row down to one of the two words: the rows below
        // it first, then one step per word.
        const auto to_word = [row_count, padding_row](int word) {
            return row_count - 1 - padding_row + word;
        };

        open_menu();
        pump(120);
        // Only one thing is lit at a time. Walking down to `save` used to leave the
        // last row wearing the highlight as well, and a panel with two things lit is
        // a panel with two answers to the same question. (Kaida, 2026-11-20: "the
        // last options of Colors and settings should not stay highlighted when we go
        // down to save".)
        for (int i = 0; i < 3; ++i) { // the first row back to `cancel`, `save`, the last row
            press_key(Qt::Key_Up);
            pump(20);
        }
        expect("the keys are standing on the last row", window.settingsCursor().second == 0 &&
                                                           window.settingsCursor().first ==
                                                               row_count - 1);
        const QRectF last_row = window.settingsRowRect(row_count - 1);
        // What is lit is the value the keys are on, not the line it sits in: a band
        // across the whole row answers "which line" when the question was "which
        // number". (Kaida, 2026-11-20: "the highlight shouldn't color the whole row,
        // it should look cleaner".)
        const QRectF value_band = window.settingsHighlightRect();
        expect("what is lit is the value, and only the value",
               !value_band.isNull() && last_row.contains(value_band) &&
                   value_band.width() < last_row.width() * 0.6);
        // A pixel near the right end of the band, past the last glyph, where only a
        // highlight could be.
        const QPoint band(static_cast<int>(value_band.right()) - 2,
                          static_cast<int>(value_band.center().y()));
        const QColor selection(window.theme().selection.r, window.theme().selection.g,
                               window.theme().selection.b);
        expect("and the highlight is really painted there",
               window.grab().toImage().pixelColor(band) == selection);

        // A theme whose ordinary ink happens to be its selection ground: a pale theme with
        // a pale highlight reaches this exactly, and the lit value — the one thing the keys
        // are about — was drawn on the band in the band's own colour, so it was not there.
        // A band is the selection's ground, so what stands on it wears the selection's ink.
        // (Kaida, 2026-11-21: "In the settings menus the higlight uses the
        // selection_background, But it should also use selection_foreground. to ensure
        // visibility in the case of foreground being the same value as
        // selection_background".)
        const auto as_worn = window.theme();
        auto camouflaged = window.theme();
        camouflaged.foreground = camouflaged.selection; // the ink *is* the band
        camouflaged.selection_foreground = Color{0x12, 0x34, 0x56}; // and is not, on the band
        window.applyTheme(camouflaged, true);
        pump(60);
        {
            const QImage shot = window.grab().toImage();
            const QColor band_ink(0x12, 0x34, 0x56);
            const QRect box = value_band.toRect().adjusted(4, 4, -4, -4); // clear the corners
            int strokes = 0;
            for (int x = box.left(); x < box.right(); ++x) {
                for (int y = box.top(); y < box.bottom(); ++y) {
                    if (shot.pixelColor(x, y) == band_ink)
                        ++strokes;
                }
            }
            expect("a value lit on a band wearing its own ink is still drawn, in the "
                   "selection's ink",
                   strokes > 20);
            window.applyTheme(as_worn, true); // the rest of the block is about other things
            pump(40);
        }
        walk(1); // and one step down, onto `save`
        expect("walking onto `save` takes the highlight off the last row",
               window.grab().toImage().pixelColor(band) != selection);
        expect("and puts it on the word, below the table altogether",
               !window.settingsHighlightRect().isNull() &&
                   !last_row.intersects(window.settingsHighlightRect()));

        // Rows sit a little apart. Painted exactly one row-height apart they read as
        // one solid block of text with a highlight somewhere in it. (Kaida,
        // 2026-11-20: "every elements could do with 1 or 2 px more space in between
        // them for breathing".)
        const QRectF first_row = window.settingsRowRect(0);
        const QRectF second_row = window.settingsRowRect(1);
        expect("and the rows have air between them",
               second_row.top() - first_row.bottom() >= 1.5);

        open_menu();
        pump(120);
        click(window.settingsStepPoint(padding_row, true)); // 7 -> 8, live
        pump(80);
        expect("a nudge shows in the window before it is saved", window.padding() == 8.0);
        walk(to_word(2)); // the rows below, then `save`, then `cancel`
        press_key(Qt::Key_Return);
        pump(100);
        expect("walking down to `cancel` and pressing Enter leaves without writing",
               !window.settingsActive() && window.padding() == 7.0 &&
                   fileText().find("padding = 7") != std::string::npos);

        open_menu();
        pump(120);
        click(window.settingsStepPoint(padding_row, true)); // 7 -> 8 again
        pump(80);
        walk(to_word(1)); // the rows below, then `save`
        press_key(Qt::Key_Return);
        pump(100);
        expect("walking down to `save` and pressing Enter writes the file the click writes",
               fileText().find("padding = 8") != std::string::npos);

        // The ring runs both ways. Walking down past the last row reaches the two
        // words and then the top of the list again, and Up comes back over them —
        // which is only worth believing if you can see where the keys are, so the
        // widget says. (Kaida, 2026-11-20: "no tab to reach save and leave, they
        // should be reachable when going down. the interfaces must be Cohesive.")
        const auto cursor = [&window] { return window.settingsCursor(); };
        const auto at = [&cursor](int row, int stop) {
            const auto where = cursor();
            expect(("the keys are on row " + std::to_string(row) + ", stop " +
                    std::to_string(stop))
                       .c_str(),
                   where.first == row && where.second == stop);
        };
        open_menu();
        pump(120);
        const int scrollback_row = 4; // font, 2 opacities, padding, then scrollback
        { // on the words, where nothing else is painted
            const QRectF row = window.settingsRowRect(scrollback_row);
            click(QPointF(row.left() + 4.0, row.center().y()));
        }
        pump(60);
        at(scrollback_row, 0);
        // Down the rest of the rows one step at a time, so the block says where the keys
        // are instead of assuming how many rows the menu has today.
        for (int row = scrollback_row + 1; row < row_count; ++row) {
            walk(1);
            at(row, 0);
        }
        walk(1);
        at(row_count - 1, 1); // one step past the last row is `save`
        walk(1);
        at(row_count - 1, 2); // and another is `cancel`
        walk(1);
        at(0, 0); // and another is the top of the list
        press_key(Qt::Key_Up);
        pump(30);
        at(0, 2); // up from the top is `cancel` again
        press_key(Qt::Key_Up);
        pump(30);
        at(0, 1);
        press_key(Qt::Key_Up);
        pump(30);
        at(row_count - 1, 0); // and up from `save` is the bottom of the list
        for (int i = 0; i < row_count - 1 - scrollback_row; ++i) {
            press_key(Qt::Key_Up);
            pump(30);
        }
        at(scrollback_row, 0);
        press_key(Qt::Key_Plus);
        pump(60);
        expect("so the keys that walked back into the rows edit the row they landed on",
               window.historyLimit() == 1001);
        // The blink rate is the one row whose range is in the thousands of something
        // small, so it walks fifty at a press; one millisecond a press is five thousand
        // presses to get from a slow blink to no blink at all.
        // (Kaida, 2026-11-21: "customizable blink speed ( 0 being no blinking)".)
        int blink_row = -1;
        for (int row = 0; row < window.settingsRowCount(); ++row) {
            if (window.settingsRowKey(row) == QStringLiteral("cursor_blink"))
                blink_row = row;
        }
        expect("the menu has a row for the cursor's blink", blink_row >= 0);
        for (int guard = 0;
             guard < 24 && (window.settingsCursor().first != blink_row ||
                            window.settingsCursor().second != 0);
             ++guard) {
            press_key(Qt::Key_Down);
            pump(20);
        }
        const int blink_was = window.settingsRowValue(blink_row);
        press_key(Qt::Key_Plus);
        pump(60);
        expect("and one press moves it by fifty milliseconds, not one",
               window.settingsRowValue(blink_row) == blink_was + 50 &&
                   window.cursorBlink() == blink_was + 50);
        press_key(Qt::Key_Minus);
        pump(60);
        expect("and one press back puts the window's blink where it started",
               window.settingsRowValue(blink_row) == blink_was &&
                   window.cursorBlink() == blink_was);
        press_key(Qt::Key_Escape);
        pump(80);
        expect("while leaving off puts the scrollback back", window.historyLimit() == 1000);        // A value the setting would refuse cannot be arrived at: the arrows stop at
        // the edges rather than wrapping into an absurd number.
        open_menu();
        pump(120);
        for (int i = 0; i < 200; ++i)
            click(window.settingsStepPoint(0, true)); // the font row, `+`, over and over
        pump(120);
        expect("twenty clicks on + stop at the largest font instead of rolling over",
               window.fontPixelSize() == 72);
        press_key(Qt::Key_Escape);
        pump(80);
        expect("and leaving that experiment off puts the font back",
               window.fontPixelSize() == 16);

        kai::AppWindow reopened(kai::startWindowBackend(24, 80));
        expect("the next window reads the saved settings back",
               reopened.fontPixelSize() == 16 && reopened.padding() == 8.0);
        reopened.discardOnClose();
        reopened.shutdown();
        pump(200);

        window.discardOnClose();
        window.shutdown();
        pump(200);
        std::remove(menu_cfg.c_str());
        ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
    }

    {
        // Every setting the file names is worn by the window that reads it. The cursor's
        // shape is the one that used to be lost: the file was read, the session started,
        // and the session's own DECSCUSR — or the one a snapshot replays when a window
        // attaches — answered a moment later and won.
        // (Kaida, 2026-11-21: "It needs to always apply the cursor style supplied by the
        // config file at all times. just like the theme, the Windows hold the cursor
        // style. Make sure other settings are also kept after being applied to the config
        // file".)
        const std::string all_cfg = root + "/every-setting";
        std::ofstream(all_cfg) << "font_size = 15\npadding = 9\nscrollback = 333\n"
                               << "cursor_style = bar\ncursor_blink = 600\n"
                               << "background_opacity = 0.9\nconfirm_quit = never\n";
        ::setenv("KAI_TERM_CONFIG", all_cfg.c_str(), 1);
        kai::AppWindow window(kai::startWindowBackend(24, 80));
        window.show();
        static_cast<void>(waitFor([&window] { return !window.sessionName().empty(); }));
        pump(300);
        expect("a window wears the font, padding, history and cursor its file names",
               window.fontPixelSize() == 15 && window.padding() == 9.0 &&
                   window.historyLimit() == 333 &&
                   window.cursorStyle() == kai::CursorStyle::Bar && window.cursorBlink() == 600);
        // A program inside the session asks for a block. The window keeps the bar its
        // own file named — which is the same thing a snapshot of a session does on
        // attach, and was the whole of the complaint.
        const auto run = [&window](const std::string& line) {
            window.pasteText(line);
            static_cast<void>(QApplication::sendEvent(
                &window, new QKeyEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier)));
            pump(80);
        };
        run("printf '\\033[2 q'");
        pump(400);
        expect("and a program asking for a shape does not move the one the file named",
               window.screen().cursorShape() == kai::Screen::CursorShape::Bar &&
                   window.fontPixelSize() == 15 && window.historyLimit() == 333);
        window.discardOnClose();
        window.shutdown();
        pump(200);

        // The same escape in a window whose file names no shape: nothing is held, and
        // DECSCUSR does what it has always done. Without this half the rule could be
        // satisfied by a shape that never moves at all.
        const std::string bare_cfg = root + "/no-cursor-setting";
        std::ofstream(bare_cfg) << "font_size = 15\n";
        ::setenv("KAI_TERM_CONFIG", bare_cfg.c_str(), 1);
        kai::AppWindow free(kai::startWindowBackend(24, 80));
        free.show();
        static_cast<void>(waitFor([&free] { return !free.sessionName().empty(); }));
        pump(300);
        free.pasteText("printf '\\033[2 q'");
        static_cast<void>(QApplication::sendEvent(
            &free, new QKeyEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier)));
        const bool moved = waitFor(
            [&free] {
                return free.screen().cursorShape() == kai::Screen::CursorShape::Block;
            },
            4000);
        expect("and a file that names no shape leaves it a program's to pick", moved);
        // The blink's default runs the other way round: a file that says nothing blinks,
        // because that is what a terminal does until its person says otherwise.
        // (Kaida, 2026-11-21: asked whether the default should blink, and said yes.)
        expect("and a file that says nothing about the blink blinks",
               free.cursorBlink() == 500 && free.cursorBlinkRunning());
        free.discardOnClose();
        free.shutdown();
        pump(200);

        // And 0 is an answer rather than an absence: the window that reads it starts no
        // timer at all, which is the difference between a steady cursor and a terminal
        // that wakes twice a second to paint nothing.
        const std::string steady_cfg = root + "/steady-cursor";
        std::ofstream(steady_cfg) << "font_size = 15\ncursor_blink = 0\n";
        ::setenv("KAI_TERM_CONFIG", steady_cfg.c_str(), 1);
        kai::AppWindow steady(kai::startWindowBackend(24, 80));
        steady.show();
        pump(200);
        expect("and a blink of 0 is a cursor that stays lit and no timer running",
               steady.cursorBlink() == 0 && !steady.cursorBlinkRunning());
        steady.discardOnClose();
        steady.shutdown();
        pump(200);
        std::remove(steady_cfg.c_str());
        std::remove(all_cfg.c_str());
        std::remove(bare_cfg.c_str());
        ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
    }

    {
        // Themes. The settings menu ends with a row that says which colours the window
        // is wearing, and the answers are the default plus whatever files are in
        // `themes`. Stepping the row paints the window before anything is written,
        // saving writes the name into the settings file, and leaving puts the colours
        // back — a preview you cannot undo is a preview you were tricked into.
        // (Kaida, 2026-11-20: "An option in our interactive settings (Ctrl + Shift + K)
        // to select from the themes inside ~/.config/kai-term/themes would also be
        // needed".)
        const std::string home = root + "/theme-home";
        static_cast<void>(::mkdir(home.c_str(), 0755));
        static_cast<void>(::mkdir((home + "/kai-term").c_str(), 0755));
        static_cast<void>(::mkdir((home + "/kai-term/themes").c_str(), 0755));
        const std::string theme_cfg = home + "/kai-term/kai-term.conf";
        std::ofstream(theme_cfg) << "font_size = 14\n";
        std::ofstream(home + "/kai-term/theme.conf")
            << "background = #102030\nforeground = #eef0f1\n";
        std::ofstream(home + "/kai-term/themes/midnight.conf")
            << "background = #050607\nforeground = #f5f6f7\n";
        // Nothing in the environment names a file here, because the environment
        // outranks the `theme` key and this block is about what that key does.
        ::setenv("XDG_CONFIG_HOME", home.c_str(), 1);
        ::unsetenv("KAI_TERM_CONFIG");
        ::unsetenv("KAI_TERM_THEME");

        kai::AppWindow window(kai::startWindowBackend(24, 80));
        window.show();
        static_cast<void>(waitFor([&window] { return !window.sessionName().empty(); }));
        pump(120);
        expect("with nothing written down the window wears the file colours always lived in",
               window.theme().background.r == 0x10);

        const auto press_key = [&window](int key, const QString& text = QString(),
                                         Qt::KeyboardModifiers mods = Qt::NoModifier) {
            QKeyEvent ev(QEvent::KeyPress, key, mods, text);
            QCoreApplication::sendEvent(&window, &ev);
        };
        const auto open_menu = [&window] {
            QKeyEvent ev(QEvent::KeyPress, Qt::Key_K,
                         Qt::ControlModifier | Qt::ShiftModifier);
            QCoreApplication::sendEvent(&window, &ev);
        };
        const int last_row = window.settingsRowCount() - 1;
        // Up off a row is `cancel`, up again `save`, up again the bottom of the list,
        // so walking up reaches the last row from wherever the last visit left it.
        const auto to_theme_row = [&] {
            open_menu();
            pump(80);
            for (int guard = 0; guard < 24 && (window.settingsCursor().first != last_row ||
                                               window.settingsCursor().second != 0);
                 ++guard) {
                press_key(Qt::Key_Up);
                pump(20);
            }
        };

        to_theme_row();
        expect("the menu ends with a row that says which colours are on",
               window.settingsCursor().first == last_row &&
                   window.settingsCursor().second == 0);
        press_key(Qt::Key_Right);
        pump(80);
        expect("stepping that row puts the other theme on the window, before any saving",
               window.theme().background.r == 0x05);
        press_key(Qt::Key_Escape);
        pump(80);
        expect("and leaving without saving puts the colours back",
               window.theme().background.r == 0x10 && !window.settingsActive());

        to_theme_row();
        press_key(Qt::Key_Right);
        pump(80);
        press_key(Qt::Key_Down); // off the last row, onto `save`
        pump(20);
        press_key(Qt::Key_Return);
        pump(120);
        std::ifstream saved(theme_cfg);
        const std::string written((std::istreambuf_iterator<char>(saved)),
                                  std::istreambuf_iterator<char>());
        expect("saving writes the name into the settings file",
               written.find("theme = midnight.conf") != std::string::npos);

        // A window started afterwards reads that line and wears the theme it names,
        // which is the whole point: the choice outlives the window that made it.
        kai::AppWindow after(kai::startWindowBackend(24, 80));
        after.show();
        static_cast<void>(waitFor([&after] { return !after.sessionName().empty(); }));
        pump(120);
        expect("and the next window reads the name and wears that theme",
               after.theme().background.r == 0x05);
        after.discardOnClose();
        after.shutdown();
        pump(200);

        // The picker's name field: always on the panel, typed into, and the difference
        // between writing a theme over and writing a new one.
        // (Kaida, 2026-11-20: "the picker's name field is always visible — saving under
        // the loaded name asks to overwrite, a new name writes a new theme".)
        const auto type_hex = [&press_key](const std::string& digits) {
            for (char c : digits) {
                const int key = (c >= '0' && c <= '9')
                                    ? (Qt::Key_0 + (c - '0'))
                                    : (Qt::Key_A + (c - 'a'));
                press_key(key, QString(QChar(c)));
            }
        };
        const auto click_at = [&window](const QPointF& at) {
            QMouseEvent down(QEvent::MouseButtonPress, at, window.mapToGlobal(at.toPoint()),
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &down);
            QMouseEvent up(QEvent::MouseButtonRelease, at, window.mapToGlobal(at.toPoint()),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &up);
            pump(60);
        };
        const auto open_picker = [&window] { openThemeEditor(&window); };

        open_picker();
        expect("the picker is up and says which theme its colours came from",
               window.pickerActive() && window.pickerNameText() == QStringLiteral("midnight"));
        expect("with the extension left out, because every theme has one",
               !window.pickerNameText().contains(QStringLiteral(".conf")) &&
                   window.pickerTargetNameText() == QStringLiteral("midnight.conf"));
        expect("the name is on the panel rather than somewhere you have to ask for",
               !window.pickerNameRect().isNull() &&
                   window.pickerPanelRect().contains(window.pickerNameRect()));

        // The field is shut until it is opened, so a letter typed while walking the list
        // cannot quietly rename the theme. (Kaida, 2026-11-20: "we should require
        // pressing e to edit the field".)
        // Up off the first colour wraps: `cancel`, `save`, then the name — the ring back
        // to front, which is fewer presses than twenty-one rows of Down.
        for (int i = 0; i < 3; ++i) {
            press_key(Qt::Key_Up);
            pump(20);
        }
        expect("walking the ring backwards off the colours lands on the name",
               window.pickerCursor().second == 3);
        press_key(Qt::Key_Z, QStringLiteral("z"));
        pump(20);
        expect("and a letter typed at a shut field changes nothing",
               window.pickerNameText() == QStringLiteral("midnight") &&
                   !window.pickerEditing());
        // `e` used to be the key that opened the field. It is not any more, and the
        // letter is still just a letter: the field stays shut and the word stays whole.
        // (Kaida, 2026-11-21: "the Enter key should allow the Editing of the Theme name
        // field, replacing and removing the e key for the edit of that field".)
        press_key(Qt::Key_E, QStringLiteral("e"));
        pump(20);
        expect("and `e` opens nothing, having been replaced by Enter",
               !window.pickerEditing() && window.pickerNameText() == QStringLiteral("midnight"));
        press_key(Qt::Key_Return);
        pump(20);
        expect("Enter opens the field, with the caret at the end of the word",
               window.pickerEditing() && window.pickerCaret() == 8);
        press_key(Qt::Key_Left);
        press_key(Qt::Key_Left);
        pump(20);
        expect("the arrows walk the caret inside the word",
               window.pickerCaret() == 6 && window.pickerEditing());
        press_key(Qt::Key_X, QStringLiteral("x"));
        pump(20);
        expect("and a letter lands where the caret is, not at the end",
               window.pickerNameText() == QStringLiteral("midnigxht") &&
                   window.pickerCaret() == 7);
        press_key(Qt::Key_Backspace);
        pump(20);
        expect("Backspace takes the letter before the caret",
               window.pickerNameText() == QStringLiteral("midnight") &&
                   window.pickerCaret() == 6);
        press_key(Qt::Key_Escape);
        pump(40);
        expect("Esc puts the word back and leaves the panel up",
               window.pickerNameText() == QStringLiteral("midnight") &&
                   !window.pickerEditing() && window.pickerActive());

        press_key(Qt::Key_Return);
        pump(20);
        for (int i = 0; i < 24; ++i)
            press_key(Qt::Key_Backspace);
        expect("the field can be emptied", window.pickerNameText().isEmpty());
        // A capital arrives as the same key with Shift on it, so a field that asked for
        // no modifiers swallowed every capital in a name.
        // (Kaida, 2026-11-21: "make the Field accept Capital letters as it does not
        // currently".)
        press_key(Qt::Key_D, QStringLiteral("D"), Qt::ShiftModifier);
        press_key(Qt::Key_U, QStringLiteral("u"));
        expect("the field takes a capital, and the small letters after it",
               window.pickerNameText() == QStringLiteral("Du"));
        for (const char c : std::string("sk"))
            press_key(Qt::Key_D, QString(QChar(c)));
        expect("and keeps typing into the same word",
               window.pickerNameText() == QStringLiteral("Dusk"));
        expect("while the file it will write carries the extension the other themes have",
               window.pickerTargetNameText() == QStringLiteral("Dusk.conf"));
        press_key(Qt::Key_Return);
        pump(40);
        expect("Enter confirms the name and gets out of the field, leaving the keys on it",
               !window.pickerEditing() && window.pickerCursor().second == 3 &&
                   window.pickerActive() && window.pickerNameText() == QStringLiteral("Dusk"));
        press_key(Qt::Key_Down); // off the name, onto `save`
        pump(40);
        expect("the word below the field is the next thing down",
               window.pickerCursor().second == 1);
        press_key(Qt::Key_Return); // `save`
        pump(150);
        expect("a name nothing is using yet writes without asking",
               !window.pickerActive());
        {
            std::ifstream dusk(home + "/kai-term/themes/Dusk.conf");
            const std::string text((std::istreambuf_iterator<char>(dusk)),
                                   std::istreambuf_iterator<char>());
            expect("a new name is a new file in the themes directory",
                   dusk.good() && theme_says(text, "background", "#050607"));
            std::ifstream settings(theme_cfg);
            const std::string settings_text((std::istreambuf_iterator<char>(settings)),
                                            std::istreambuf_iterator<char>());
            expect("and the settings file is what points at it",
                   settings_text.find("theme = Dusk.conf") != std::string::npos);
            expect("the whole set goes into a file that was not there before",
                   theme_mentions(text, "foreground") == 1);
            // The layout a theme is written in, said out loud: the `key = value` shape is
            // deprecated for themes, so a file the editor makes holds no sign anywhere —
            // which is what left Kaida's themes holding every colour twice.
            expect("and a theme the editor wrote is written without the sign",
                   text.find(" = ") == std::string::npos);
        }

        // The second time, the name is taken — by the theme being worn — so the panel
        // asks, and the two answers are the two words it already had.
        open_picker();
        expect("the panel opens on the name it just wrote",
               window.pickerNameText() == QStringLiteral("Dusk"));
        // The pointer's way in: landing on the field opens it, because a box you can
        // point at but not type into is a label with a border round it.
        click_at(window.pickerNameRect().center());
        expect("pointing at the name puts the keys on it and opens it",
               window.pickerCursor().second == 3 && window.pickerEditing());
        expect("and the thing lit is the name, which is what the keys are on",
               !window.pickerHighlightRect().isNull() &&
                   window.pickerNameRect().contains(window.pickerHighlightRect()));
        press_key(Qt::Key_Return); // back out of the field with the name as it was
        pump(40);
        press_key(Qt::Key_Escape); // and out of the panel, to start again on a colour
        pump(60);
        open_picker();
        type_hex("ff0000"); // the keys start on `foreground`, so this is the row that moves
        pump(60);
        click_at(window.pickerSavePoint());
        expect("saving under a name that is already a theme is asked about",
               window.pickerActive() && window.pickerAskingOverwrite() &&
                   window.pickerTitleText() == QStringLiteral("Overwrite Dusk.conf?"));
        press_key(Qt::Key_Escape);
        pump(60);
        expect("Esc answers the question without throwing the panel away",
               window.pickerActive() && !window.pickerAskingOverwrite());
        click_at(window.pickerSavePoint());
        expect("and asking again is the same word in the same place",
               window.pickerAskingOverwrite());
        click_at(window.pickerCancelPoint());
        pump(80);
        expect("`keep it` leaves the file as it was", !window.pickerActive());
        {
            std::ifstream dusk(home + "/kai-term/themes/Dusk.conf");
            const std::string text((std::istreambuf_iterator<char>(dusk)),
                                   std::istreambuf_iterator<char>());
            expect("nothing was written while the answer was no",
                   !theme_says(text, "foreground", "#ff0000"));
        }

        // The colour rows had the same trouble the name field had: a capital arrives as
        // the same key with Shift on it, so a row that demanded no modifiers swallowed
        // every `F` anybody typed into it.
        open_picker();
        press_key(Qt::Key_Down); // off the ground and on to the ink
        type_hex("000000"); // the row to black, and the cursor back where it started
        pump(40);
        press_key(Qt::Key_F, QStringLiteral("F"), Qt::ShiftModifier);
        pump(40);
        expect("a capital hex digit lands on a colour as readily as a small one",
               window.theme().foreground.r == 0xf0);
        press_key(Qt::Key_Escape);
        pump(60);

        window.discardOnClose();
        window.shutdown();
        pump(200);
        for (const std::string& gone : {theme_cfg, home + "/kai-term/theme.conf",
                                        home + "/kai-term/themes/midnight.conf",
                                        home + "/kai-term/themes/Dusk.conf"})
            std::remove(gone.c_str());
        static_cast<void>(::rmdir((home + "/kai-term/themes").c_str()));
        static_cast<void>(::rmdir((home + "/kai-term").c_str()));
        static_cast<void>(::rmdir(home.c_str()));
        ::unsetenv("XDG_CONFIG_HOME");
        ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
        ::setenv("KAI_TERM_THEME", (root + "/no-such-theme").c_str(), 1);
    }

    {
        // A theme file from kitty, worn by a window. No `=` anywhere in it, the palette
        // spelled `color0`…`color15`, and the ink on a selection spelled the way kitty
        // spells it — which is also the first time that colour was anything a person could
        // change: it used to be a light grey compiled into the painting.
        // (Kaida, 2026-11-21: "use color0-color15 in the config, but keep their name in the
        // UI, So the config has color0 while the UI will still say Black", "yes add selection
        // background to our config too", "and selection foreground".)
        const std::string home = root + "/kitty-home";
        const std::string themes = home + "/kai-term/themes";
        ::mkdir(home.c_str(), 0755);
        ::mkdir((home + "/kai-term").c_str(), 0755);
        ::mkdir(themes.c_str(), 0755);
        const std::string kitty = themes + "/paper.conf";
        std::ofstream(kitty)
            << "background #1c1c1c\n"
            << "foreground #ddeedd\n"
            << "cursor #e2bbef\n"
            << "selection_background #4d4d4d\n"
            << "selection_foreground #1c1c1c\n"
            << "color0 #3d352a\n"
            << "color1 #cd5c5c\n";
        std::ofstream(home + "/kai-term/kai-term.conf") << "theme = paper.conf\n";
        ::setenv("XDG_CONFIG_HOME", home.c_str(), 1);
        ::unsetenv("KAI_TERM_CONFIG");
        ::unsetenv("KAI_TERM_THEME");
        kai::AppWindow window(kai::startWindowBackend(24, 80));
        window.show();
        static_cast<void>(waitFor([&window] { return !window.sessionName().empty(); }));
        pump(200);

        expect("a kitty theme paints this window with no edits to it",
               window.theme().background.r == 0x1c && window.theme().foreground.g == 0xee &&
                   window.theme().cursor.b == 0xef);
        expect("its colour0 is the window's black",
               window.theme().palette[0].r == 0x3d && window.theme().palette[1].r == 0xcd);
        expect("its selection colour arrives", window.theme().selection.r == 0x4d);
        expect("and so does the ink that goes on top of it, which used to be a constant",
               window.theme().selection_foreground.r == 0x1c &&
                   window.theme().selection_foreground.g == 0x1c);

        // The screen's names and the file's names are two lists on purpose: the row says
        // `black` because that is what a person reads, and the file says `color0` because
        // that is what every other terminal calls it.
        openThemeEditor(&window);
        pump(120);
        int ink_row = -1;
        int black_row = -1;
        for (int row = 0; row < window.pickerRowCount(); ++row) {
            if (window.pickerRowLabel(row) == QStringLiteral("selection foreground"))
                ink_row = row;
            if (window.pickerRowLabel(row) == QStringLiteral("black"))
                black_row = row;
        }
        expect("the colour list has a row for the ink on a selection", ink_row >= 0);
        expect("while the row called `black` writes the file's `color0`",
               black_row >= 0 && window.pickerRowKey(black_row) == QStringLiteral("color0"));
        // The order of the list, which is the whole of the argument for having one: the
        // window's own colours in the order you notice them, then each palette colour
        // beside its bright version.
        // (Kaida, 2026-11-21: "the color should be grouped togheter by \"color\" ... Black,
        // Bright Black, Red, Bright Red ... Foreground background cursor ... can stay at
        // the top but in this order: Background, Foreground, Accent, Cursor, Selection
        // Background, Selection Foreground.")
        const QStringList wanted{
            "background",  "foreground", "accent",
            "cursor",       "selection background", "selection foreground",
            "black",        "bright black",         "red",
            "bright red",   "green",      "bright green",
            "yellow",       "bright yellow",        "blue",
            "bright blue",  "magenta",    "bright magenta",
            "cyan",         "bright cyan",          "white",
            "bright white"};
        bool in_order = window.pickerRowCount() == wanted.size();
        for (int row = 0; in_order && row < wanted.size(); ++row)
            in_order = window.pickerRowLabel(row) == wanted.at(row);
        expect("the colours are in the order they are noticed, bright next to plain",
               in_order);
        expect("and a colour is followed by its bright version, not by the next hue",
               window.pickerRowLabel(black_row + 1) == QStringLiteral("bright black") &&
                   window.pickerRowKey(black_row + 1) == QStringLiteral("color8"));
        expect("and the three selection rows keep the names a person reads",
               window.pickerRowLabel(4) == QStringLiteral("selection background") &&
                   window.pickerRowLabel(ink_row) == QStringLiteral("selection foreground") &&
                   window.pickerRowKey(ink_row) == QStringLiteral("selection_foreground"));
        // The name that goes *into the file* for the highlight. Without this the row could
        // quietly keep writing `selection` and every check above would still pass.
        expect("while the highlight writes the file kitty's name for it",
               window.pickerRowKey(4) == QStringLiteral("selection_background"));
        window.discardOnClose();
        window.shutdown();
        pump(200);
        std::remove(kitty.c_str());
        std::remove((home + "/kai-term/kai-term.conf").c_str());
        static_cast<void>(::rmdir(themes.c_str()));
        static_cast<void>(::rmdir((home + "/kai-term").c_str()));
        static_cast<void>(::rmdir(home.c_str()));
        ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
        ::setenv("KAI_TERM_THEME", (root + "/no-such-theme").c_str(), 1);
    }

    {
        // The colour editor is a row of the settings menu now, not a key of its own, and
        // the menu waits behind it with everything you had changed still changed.
        // (Kaida, 2026-11-20: "Instead of having a keybind for the Theme Editor
        // Ctrl+shift+P, the Theme editor should be an item in the setting menu under Theme.
        // It would make things more coherent"; "ctrl+shift+p, does indeed go away"; "Yeah
        // the setting keeps your change until you save or cancel them, even when in a
        // sub-menu".)
        const std::string home = root + "/submenu-home";
        const std::string themes = home + "/kai-term/themes";
        static_cast<void>(::mkdir(home.c_str(), 0755));
        static_cast<void>(::mkdir((home + "/kai-term").c_str(), 0755));
        static_cast<void>(::mkdir(themes.c_str(), 0755));
        const std::string cfg = home + "/kai-term/kai-term.conf";
        std::ofstream(cfg) << "font_size = 14\n";
        std::ofstream(home + "/kai-term/theme.conf")
            << "background = #102030\nforeground = #eef0f1\n";
        std::ofstream(themes + "/one.conf") << "background = #050607\nforeground = #f5f6f7\n";
        std::ofstream(themes + "/two.conf") << "background = #302010\nforeground = #f7f6f5\n";
        ::setenv("XDG_CONFIG_HOME", home.c_str(), 1);
        ::unsetenv("KAI_TERM_CONFIG");
        ::unsetenv("KAI_TERM_THEME");

        kai::AppWindow window(kai::startWindowBackend(24, 80));
        window.show();
        static_cast<void>(waitFor([&window] { return !window.sessionName().empty(); }));
        pump(200);

        const auto press_key = [&window](int key, const QString& text = QString()) {
            QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(&window, &ev);
            pump(30);
        };
        const auto open_menu = [&window] {
            QKeyEvent ev(QEvent::KeyPress, Qt::Key_K,
                         Qt::ControlModifier | Qt::ShiftModifier);
            QCoreApplication::sendEvent(&window, &ev);
            pump(120);
        };
        const auto type_word = [&press_key](const std::string& word) {
            for (char c : word) {
                const int key = (c >= '0' && c <= '9') ? (Qt::Key_0 + (c - '0'))
                                                       : (Qt::Key_A + (c - 'a'));
                press_key(key, QString(QChar(c)));
            }
        };
        // Three Ups off the menu's first row: `cancel`, `save`, and the last row — which
        // is the theme. Walking backwards is the short way to the bottom of a list.
        const auto to_theme_row = [&press_key] {
            for (int i = 0; i < 3; ++i)
                press_key(Qt::Key_Up);
        };

        // The key is gone. Not hidden behind a second meaning, gone: the colours are found
        // where every other decision is found.
        {
            QKeyEvent ev(QEvent::KeyPress, Qt::Key_P,
                         Qt::ControlModifier | Qt::ShiftModifier);
            QCoreApplication::sendEvent(&window, &ev);
        }
        pump(80);
        expect("Ctrl+Shift+P opens nothing at all now",
               !window.pickerActive() && !window.settingsActive() &&
                   !window.mixerActive());

        open_menu();
        const int last_row = window.settingsRowCount() - 1;
        expect("the menu ends with the theme row, which is where the editor lives",
               window.settingsRowKey(last_row) == QStringLiteral("theme") &&
                   window.settingsRowKey(last_row - 1) != QStringLiteral("theme"));
        to_theme_row();
        expect("and the keys walk to it from the top of the list",
               window.settingsCursorRow() == last_row && window.settingsFocus() == 0);
        expect("the line along the bottom says Enter opens the editor, on this row",
               window.panelHintText().contains(QStringLiteral("Enter opens the theme editor")));

        // A change made in the menu, then a trip into the editor and back: the change is
        // where it was, and so are the keys.
        const int font_at_first = window.fontPixelSize();
        press_key(Qt::Key_Down); // `save`
        press_key(Qt::Key_Down); // `cancel`
        press_key(Qt::Key_Down); // the first row, which is the font size
        expect("the walk off the bottom of the list comes back to the top",
               window.settingsCursorRow() == 0 && window.settingsFocus() == 0);
        press_key(Qt::Key_Right);
        press_key(Qt::Key_Right);
        pump(60);
        expect("a row of the menu changes the window before anything is saved",
               window.fontPixelSize() == font_at_first + 2);
        to_theme_row();
        press_key(Qt::Key_Return);
        pump(140);
        expect("Enter on the theme row opens the colour editor", window.pickerActive());
        expect("and the menu steps aside rather than closing",
               window.settingsSteppedAside() && !window.settingsActive());
        expect("one panel at a time: the editor is the thing on the screen",
               !window.pickerPanelRect().isEmpty());
        expect("with the menu's unsaved font still on the window",
               window.fontPixelSize() == font_at_first + 2);

        press_key(Qt::Key_Escape); // out of the editor
        pump(140);
        expect("leaving the editor returns to the menu that was waiting",
               !window.pickerActive() && window.settingsActive() &&
                   !window.settingsSteppedAside());
        expect("on the row the keys went in on",
               window.settingsCursorRow() == last_row && window.settingsFocus() == 0);
        expect("and still holding the change that had not been saved",
               window.fontPixelSize() == font_at_first + 2);

        // Saving inside the editor is a decision, not a preview: coming out and then
        // leaving the menu without saving must not roll it back.
        press_key(Qt::Key_Return);
        pump(140);
        expect("the editor opens from the same row twice", window.pickerActive());
        press_key(Qt::Key_Up); // off the first colour: `cancel`, then `save`
        press_key(Qt::Key_Up);
        press_key(Qt::Key_Up); // ...and the name, which is where a new theme starts
        pump(60);
        expect("the keys are on the name", window.pickerCursor().second == 3);
        press_key(Qt::Key_Return); // Enter opens the field the keys are standing on
        pump(40);
        for (int i = 0; i < 24; ++i)
            press_key(Qt::Key_Backspace); // the worn name goes, and the new one starts clean
        type_word("amber");
        press_key(Qt::Key_Return); // confirm the word — confirming is not saving
        press_key(Qt::Key_Down); // the word below the field is the next thing down
        expect("the word below the name is `save`", window.pickerCursor().second == 1);
        press_key(Qt::Key_Return); // and it saves
        pump(200);
        expect("a new name writes a new theme and the menu comes back",
               !window.pickerActive() && window.settingsActive());
        expect("the theme row now says the name the editor just wrote",
               window.settingsRowChoice(last_row) == QStringLiteral("amber.conf"));
        {
            std::ifstream in(cfg);
            const std::string text((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            expect("and the settings file already points at it",
                   text.find("theme = amber.conf") != std::string::npos);
        }
        press_key(Qt::Key_Down); // the theme row, then `save`
        press_key(Qt::Key_Return);
        pump(200);
        {
            std::ifstream in(cfg);
            const std::string text((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            expect("saving the menu afterwards cannot put the older name back",
                   text.find("theme = amber.conf") != std::string::npos &&
                       text.find("theme = one.conf") == std::string::npos);
            expect("while the row the menu was changed on does get written",
                   text.find("font_size = 16") != std::string::npos);
        }
        expect("and the menu is gone once it has saved", !window.settingsActive());

        // A save inside the editor is a decision somebody wrote to a file, not a preview:
        // leaving the menu afterwards without saving must not roll it back, and the window
        // must not end up contradicting the settings file that now names the new theme.
        open_menu();
        to_theme_row();
        press_key(Qt::Key_Return);
        pump(140);
        expect("the editor opens from the theme row again", window.pickerActive());
        type_word("123456"); // the keys start on `background`
        pump(60);
        expect("typing a colour into the editor changes the window as it is typed",
               window.theme().background.r == 0x12);
        for (int i = 0; i < 3; ++i)
            press_key(Qt::Key_Up); // off the first colour: `cancel`, `save`, the name
        press_key(Qt::Key_Return); // and Enter opens it
        for (int i = 0; i < 24; ++i)
            press_key(Qt::Key_Backspace);
        type_word("second");
        press_key(Qt::Key_Return); // confirm the word
        press_key(Qt::Key_Down); // onto `save`
        expect("the word below the name is `save`, again", window.pickerCursor().second == 1);
        press_key(Qt::Key_Return); // and write it
        pump(200);
        expect("a second new theme writes a second file and the menu is back",
               !window.pickerActive() && window.settingsActive());
        {
            std::ifstream in(themes + "/second.conf");
            const std::string text((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            expect("the colour typed in the editor is in the file the editor wrote",
                   theme_says(text, "background", "#123456"));
        }
        press_key(Qt::Key_Escape); // leave the menu without saving
        pump(140);
        expect("leaving the menu without saving keeps what the editor saved",
               !window.settingsActive() && window.theme().background.r == 0x12);
        {
            std::ifstream in(cfg);
            const std::string text((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            expect("and the settings file still points at the theme that was saved",
                   text.find("theme = second.conf") != std::string::npos);
        }

        window.discardOnClose();
        window.shutdown();
        pump(200);
        std::remove((themes + "/one.conf").c_str());
        std::remove((themes + "/two.conf").c_str());
        std::remove((themes + "/amber.conf").c_str());
        std::remove((themes + "/second.conf").c_str());
        std::remove(cfg.c_str());
        std::remove((home + "/kai-term/theme.conf").c_str());
        static_cast<void>(::rmdir(themes.c_str()));
        static_cast<void>(::rmdir((home + "/kai-term").c_str()));
        static_cast<void>(::rmdir(home.c_str()));
        ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
        ::setenv("KAI_TERM_THEME", (root + "/no-such-theme").c_str(), 1);
    }

    {
        // The colour mixer, end to end. Space on a row of the picker opens it over
        // that one colour; pushing a bar changes the window while it is being
        // pushed; Enter hands the colour back to the row it came from and leaves you
        // in the list you were in; and Esc at either step puts the colour back where
        // it started. The arithmetic is checked headless in tests/color_mixer.cpp —
        // this is the path from a keystroke and a pointer to a file on disk.
        const std::string cfg = root + "/mixer-config";
        const std::string theme = root + "/mixer-theme";
        std::ofstream(cfg) << "font_size = 12\n";
        std::ofstream(theme) << "foreground = #dcdcdc\nbackground = #1e1e1e\n";
        ::setenv("KAI_TERM_CONFIG", cfg.c_str(), 1);
        ::setenv("KAI_TERM_THEME", theme.c_str(), 1);
        kai::AppWindow window(kai::startWindowBackend(24, 80));
        window.show();
        static_cast<void>(waitFor([&window] { return !window.sessionName().empty(); }));
        pump(200);

        const auto press = [&window](int key, const QString& text = QString()) {
            QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(&window, &ev);
        };
        const auto open_picker = [&window] { openThemeEditor(&window); };
        const auto mouse = [&window](QEvent::Type type, const QPointF& at,
                                    Qt::MouseButtons held) {
            QMouseEvent ev(type, at, window.mapToGlobal(at.toPoint()), Qt::LeftButton,
                           held, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &ev);
        };

        open_picker();
        pump(60);
        expect("the picker is open before the mixer is asked for", window.pickerActive());
        // In the list the lit thing is the six digits, and the swatch beside them
        // stays a plain square: it is the colour, not a number anybody is standing
        // on.
        expect("the list lights the value and leaves the swatch alone",
               !window.pickerHighlightRect().isNull() &&
                   window.pickerRowRect(0).contains(window.pickerHighlightRect()) &&
                   !window.pickerHighlightRect().intersects(window.pickerSwatchRect(0)));
        press(Qt::Key_Down); // the ink, which is the colour this block mixes
        pump(20);
        press(Qt::Key_Return);
        pump(60);
        expect("Enter opens the mixer for the colour under the highlight",
               window.mixerActive());
        expect("and it says at the top which colour it is mixing",
               window.mixerTitleText() == QStringLiteral("Mix foreground"));
        expect("and the thing lit is the colour band, not the row it hangs in",
               window.mixerHighlightRect() == window.mixerBarRect(0));
        expect("and the list stays behind it, because mixing is a detour",
               window.pickerActive());

        // Push the red channel along its bar with the pointer, and watch the window's
        // own ink change while the button is still down.
        const QRectF red = window.mixerBarRect(0);
        expect("the mixer painted a bar for the red channel", red.width() > 1.0);
        const Color ink_before = window.theme().foreground;
        mouse(QEvent::MouseButtonPress, QPointF(red.center().x(), red.center().y()),
              Qt::LeftButton);
        pump(40);
        mouse(QEvent::MouseMove, QPointF(red.right() - 1.0, red.center().y()),
              Qt::LeftButton);
        pump(40);
        mouse(QEvent::MouseButtonRelease, QPointF(red.right() - 1.0, red.center().y()),
              Qt::NoButton);
        pump(60);
        expect("pushing the red bar to its end lifts the window's red to full",
               window.theme().foreground.r >= 250);
        expect("while the other channels stay where they were",
               window.theme().foreground.g == ink_before.g &&
                   window.theme().foreground.b == ink_before.b);

        // `save` is the mixer's answer: the colour goes back into the row, and the
        // keys are handed to the list, which is where the next colour is.
        {
            const QPointF save = window.mixerSavePoint();
            QMouseEvent ev(QEvent::MouseButtonPress, save, window.mapToGlobal(save.toPoint()),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &ev);
        }
        pump(60);
        expect("`save` hands the colour back and closes the mixer", !window.mixerActive());
        expect("with the list still open underneath", window.pickerActive());
        expect("and the window still showing the mixed colour",
               window.theme().foreground.r >= 250);

        // Coming back and leaving with Esc must not keep the second colour.
        const Color mixed = window.theme().foreground;
        press(Qt::Key_Return);
        pump(60);
        expect("Enter opens the mixer again", window.mixerActive());
        press(Qt::Key_Minus); // one step down the red channel, live
        pump(40);
        expect("and a nudge is live, as everything in this program is",
               window.theme().foreground.r == mixed.r - 1);
        press(Qt::Key_Escape);
        pump(60);
        expect("Esc goes back without keeping it",
               !window.mixerActive() && window.theme().foreground.r == mixed.r);

        // The list's own `save` is still the thing that writes a file — and the theme
        // is already on disk by now, so the ask comes first and the answer is the same
        // word again.
        const auto click_save = [&window] {
            const QPointF at = window.pickerSavePoint();
            QMouseEvent down(QEvent::MouseButtonPress, at, window.mapToGlobal(at.toPoint()),
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &down);
            QMouseEvent up(QEvent::MouseButtonRelease, at, window.mapToGlobal(at.toPoint()),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &up);
            pump(60);
        };
        click_save();
        expect("saving over the theme that is loaded asks first",
               window.pickerActive() && window.pickerAskingOverwrite());
        click_save();
        pump(120);
        expect("the list's `save` writes and closes", !window.pickerActive());
        std::ifstream theme_in(theme);
        const std::string theme_text((std::istreambuf_iterator<char>(theme_in)),
                                     std::istreambuf_iterator<char>());
        const Color ink = window.theme().foreground;
        const std::string want = kai::colourText(Rgb{ink.r, ink.g, ink.b});
        expect("and the theme file says the colour the window is showing",
               theme_says(theme_text, "foreground", want));
        expect("leaving the background line alone",
               theme_says(theme_text, "background", "#1e1e1e"));

        // A fresh window reads the mixed colour off the disk, which is the only
        // proof that what was previewed is what was written.
        kai::AppWindow reopened(kai::startWindowBackend(24, 80));
        expect("a window opened afterwards reads the mixed colour back",
               reopened.theme().foreground.r == mixed.r &&
                   reopened.theme().foreground.g == mixed.g &&
                   reopened.theme().foreground.b == mixed.b);
        reopened.discardOnClose();
        reopened.shutdown();
        pump(200);
        window.discardOnClose();
        window.shutdown();
        pump(200);
        std::remove(cfg.c_str());
        std::remove(theme.c_str());
        ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
    }


    {
        // A menu in a window too small for it, and the colour a preview is
        // painted in.
        //
        // A panel is laid out from its own content — a title, a table of rows, a
        // line of keys along the bottom — and none of that content knows how big
        // the window is. In a window smaller than it wants to be, a panel used to
        // be painted anyway: its edges, its hint line and its two words running
        // off the screen and over each other. It now takes a smaller drawing font
        // until it fits, and everything in it — the buttons included, which used
        // to measure themselves against the window's font rather than the panel's
        // — is measured in that font. (Kaida, 2026-11-20: "make sure the ui
        // doesn't break when the size change" and "maybe a fallback max font size
        // depending on the windows size, to make sure our menu can fit properly
        // without getting mangled".)
        //
        // And the mixer's preview: the sentence is the colour. Flooding a strip
        // with the mixed colour and writing the sample over it in black or white
        // painted the menu that colour twice over and the sentence not at all.
        // (Kaida, 2026-11-20: "the Quick brown fox line didn't get paint in red
        // when it should be the only thing getting painted red on this menu, since
        // it's the preview".)
        const std::string cfg = root + "/small-panel-config";
        const std::string theme = root + "/small-panel-theme";
        std::ofstream(cfg) << "font_size = 16\n";
        std::ofstream(theme) << "foreground = #ff5ce1\nbackground = #7b101b\n";
        ::setenv("KAI_TERM_CONFIG", cfg.c_str(), 1);
        ::setenv("KAI_TERM_THEME", theme.c_str(), 1);
        kai::AppWindow window(kai::startWindowBackend(24, 80));
        window.show();
        static_cast<void>(waitFor([&window] { return !window.sessionName().empty(); }));
        pump(200);

        const auto press = [&window](int key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
            QKeyEvent ev(QEvent::KeyPress, key, mods);
            QCoreApplication::sendEvent(&window, &ev);
        };
        const auto room = [&window] {
            return QRectF(0.0, 0.0, window.width(), window.height());
        };

        // The colour list, in a window less than half the width it would ask for.
        openThemeEditor(&window);
        pump(80);
        window.resize(420, 300);
        pump(150);
        expect("the colour list fits a window far smaller than it wants to be",
               room().contains(window.pickerPanelRect()));
        expect("and its two words are inside it, not under the rows",
               window.pickerPanelRect().contains(window.pickerSavePoint()));
        expect("and sit below its rows rather than across them",
               window.pickerSavePoint().y() > window.pickerBottomRowRect().bottom());

        // The mixer, opened on the window's own background. This is the case where the
        // ground the sentence sits on is the very colour being mixed, which is what
        // the switch at the end of the strip exists for. The list opens with its keys on
        // the ground, so there is nowhere to walk: Enter is already on the right row.
        press(Qt::Key_Return);
        pump(100);
        const Color mixed = window.theme().background; // the colour being mixed
        const QRectF strip = window.mixerPreviewRect();
        expect("the mixer's preview strip is inside the panel",
               window.mixerPanelRect().contains(strip));
        // The sentence says what it is, and all of it is drawn. A pangram was long
        // enough that the centred draw clipped both ends and the strip read as a
        // sentence with pieces missing.
        // (Kaida, 2026-11-21: 'the preview pane in the Theme editor should read
        // "  0123 #$%& Color Preview" instead of " 0123 #$%& The quick brown fox ...".)
        expect("the strip says what it is rather than exercising every letter",
               AppWindow::mixerSampleText() == QStringLiteral("0123 #$%& Color Preview") &&
                   !AppWindow::mixerSampleText().contains(QStringLiteral("fox")));
        expect("and all of it fits the strip in a window smaller than it wants",
               window.mixerSampleFits());

        const QColor want(mixed.r, mixed.g, mixed.b);
        const auto step = [](const QColor& a, const QColor& b) {
            return std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                             std::abs(a.blue() - b.blue())});
        };
        // The strip's ground and its sentence's ink, read off the painted window. The
        // ground is whatever the strip is mostly made of — a flat fill, against which
        // the glyphs are a few per cent of the pixels — and the ink is the pixel
        // standing furthest from it, taken inside the border and to the left of the
        // switch, neither of which is part of the sentence.
        const auto readStrip = [&step, &strip](const QImage& painted, const QRectF& avoid) {
            const int x0 = static_cast<int>(strip.left()) + 4;
            const int y0 = static_cast<int>(strip.top()) + 4;
            const int y1 = static_cast<int>(strip.bottom()) - 4;
            const int x1 = avoid.isValid() ? std::min(static_cast<int>(strip.right()) - 4,
                                                      static_cast<int>(avoid.left()) - 4)
                                           : static_cast<int>(strip.right()) - 4;
            std::vector<std::pair<QRgb, int>> tally;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const QRgb got = painted.pixelColor(x, y).rgba();
                    bool counted = false;
                    for (std::pair<QRgb, int>& entry : tally) {
                        if (entry.first != got)
                            continue;
                        ++entry.second;
                        counted = true;
                        break;
                    }
                    if (!counted)
                        tally.emplace_back(got, 1);
                }
            }
            std::pair<QRgb, int> modal{0u, -1};
            for (const std::pair<QRgb, int>& entry : tally)
                if (entry.second > modal.second)
                    modal = entry;
            const QColor ground = QColor::fromRgba(modal.first);
            QColor ink = ground;
            int furthest = 0;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const QColor got = painted.pixelColor(x, y);
                    if (step(got, ground) > furthest) {
                        furthest = step(got, ground);
                        ink = got;
                    }
                }
            }
            return std::make_pair(ground, ink);
        };
        const auto click = [&window](const QPointF& at) {
            QMouseEvent down(QEvent::MouseButtonPress, at, at, Qt::LeftButton, Qt::LeftButton,
                             Qt::NoModifier);
            QMouseEvent up(QEvent::MouseButtonRelease, at, at, Qt::LeftButton, Qt::NoButton,
                           Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &down);
            QCoreApplication::sendEvent(&window, &up);
        };

        {
            const auto read = readStrip(window.grab().toImage(), window.mixerGroundSwitchRect());
            expect("the sentence sits on the window's own background to begin with",
                   step(read.first, want) <= 20);
            expect("and the switch that changes it is inside the strip",
                   strip.contains(window.mixerGroundSwitchRect()));
            expect("which starts on the background", window.mixerGround() == 0);
        }
        // The switch, walked by the pointer. The ground changes and the sentence keeps
        // the colour, which is the whole point of it: on the window's own ground the
        // colour being mixed *is* the ground, so the sentence disappears until you move
        // off it — a fact about the colour, and now one you can see rather than one to
        // paint over. (Kaida, 2026-11-20: "The ground should just be of the current
        // background color (with the opacity of the backgroud) and have a switch on the
        // side that alternate between Black / White / Current Background colour".)
        click(window.mixerGroundSwitchRect().center());
        pump(80);
        expect("one click walks the switch to black", window.mixerGround() == 1);
        {
            const auto read = readStrip(window.grab().toImage(), window.mixerGroundSwitchRect());
            expect("the sentence is on black now", step(read.first, QColor(0, 0, 0)) <= 20);
            expect("and it is still the only thing wearing the colour being mixed",
                   step(read.second, want) <= 70);
        }
        click(window.mixerGroundSwitchRect().center());
        pump(80);
        expect("another walks it to white", window.mixerGround() == 2);
        {
            const auto read = readStrip(window.grab().toImage(), window.mixerGroundSwitchRect());
            expect("and the ground is white", step(read.first, QColor(255, 255, 255)) <= 20);
            expect("with the sentence still in the mixed colour", step(read.second, want) <= 70);
        }
        click(window.mixerGroundSwitchRect().center());
        pump(80);
        expect("and the third walks it round to the background again",
               window.mixerGround() == 0);

        // The same switch under the keys. The sentence is a place the cursor can be,
        // reached by walking rather than by a key of its own, because that is how every
        // other part of this program is reached.
        // (Kaida, 2026-11-20: "left right when the cursor is on the preview zone seems
        // nice".)
        const auto type = [&window](const QString& text) {
            QKeyEvent ev(QEvent::KeyPress, Qt::Key_5, Qt::NoModifier, text);
            QCoreApplication::sendEvent(&window, &ev);
        };
        // A click on the switch leaves the keys on the sentence, so walk back down to a
        // channel before asking where Up goes from one.
        press(Qt::Key_Down);
        pump(60);
        expect("and a click there leaves the keys on the sentence to walk down from",
               window.mixerCursor().second == 0);
        press(Qt::Key_Up);
        pump(60);
        expect("walking up off the first channel lands on the sentence",
               window.mixerCursor().second == 3);
        press(Qt::Key_Right);
        pump(60);
        expect("and Right walks its ground", window.mixerGround() == 1);
        press(Qt::Key_Right);
        press(Qt::Key_Right);
        pump(60);
        expect("which wraps at the end rather than stopping", window.mixerGround() == 0);
        press(Qt::Key_Left);
        pump(60);
        expect("and wraps back the other way", window.mixerGround() == 2);
        type("5");
        pump(60);
        expect("while typing on the sentence changes no colour, because it would be a "
               "number nobody is standing on",
               window.theme().background.r == mixed.r && window.mixerGround() == 2);
        press(Qt::Key_Down);
        pump(60);
        expect("and walking down leaves the sentence behind", window.mixerCursor().second == 0);
        press(Qt::Key_Right);
        pump(60);
        expect("where Right is the colour again, not the ground",
               window.theme().background.r != mixed.r);
        press(Qt::Key_Left);
        pump(60);

        // The arrows edit the colour, as they do on every other row of this
        // program; the cursor a typed number wants has moved to Shift+arrows.
        press(Qt::Key_Left);
        pump(60);
        expect("Left takes the channel the mixer is standing on down one, live",
               window.theme().background.r == mixed.r - 1);
        press(Qt::Key_Right);
        pump(60);
        expect("and Right puts it back", window.theme().background.r == mixed.r);
        const int digit_before = window.mixerDigit();
        press(Qt::Key_Right, Qt::ShiftModifier);
        pump(60);
        expect("while Shift+Right moves the digit cursor instead of the colour",
               window.mixerDigit() != digit_before &&
                   window.theme().background.r == mixed.r);
        press(Qt::Key_Left, Qt::ShiftModifier);
        pump(60);
        expect("and Shift+Left takes the cursor back", window.mixerDigit() == digit_before);
        for (int i = 0; i < 6; ++i)
            press(Qt::Key_Down); // down to the `hex` row
        pump(80);
        press(Qt::Key_Left);
        pump(60);
        expect("the hex row takes an arrow too, because typing is not the only way in",
               window.theme().background.r == 0x6b);

        // And the same two panels in a window smaller again.
        window.resize(360, 240);
        pump(150);
        const QRectF panel = window.mixerPanelRect();
        expect("the mixer fits a window much smaller than it wants to be",
               room().contains(panel));
        expect("and the sentence on its strip is still drawn whole at this size",
               window.mixerSampleFits());
        bool bars_inside = true;
        for (int i = 0; i < kMixChannelCount; ++i) {
            const QRectF bar = window.mixerBarRect(i);
            if (bar.isEmpty() || !panel.contains(bar))
                bars_inside = false;
        }
        expect("every bar is inside it", bars_inside);
        expect("and so are its two words", panel.contains(window.mixerSavePoint()));
        expect("with the last channel above them, not underneath",
               window.mixerSavePoint().y() > window.mixerBottomRowRect().bottom());
        expect("and its hint below the words rather than through them",
               window.mixerPanelRect().bottom() > window.mixerSavePoint().y());

        press(Qt::Key_Escape); // out of the mixer
        pump(60);
        expect("leaving the mixer puts the colour back where it was",
               window.theme().background.r == mixed.r);
        press(Qt::Key_Escape); // out of the list
        pump(60);
        // ...and out of the menu that was waiting under it, which is where the list
        // handed the keys back. A second Esc is now the way out, and the menu this
        // test wants is a fresh one, opened from the terminal with its keys on row 0.
        press(Qt::Key_Escape);
        pump(60);
        press(Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
        pump(150);
        expect("the settings menu fits the same small window",
               room().contains(window.settingsPanelRect()));
        expect("and so does the row it is standing on",
               room().contains(window.settingsRowRect(0)));
        expect("with its two words under its last row, not over it",
               window.settingsSavePoint().y() > window.settingsBottomRowRect().bottom());
        press(Qt::Key_Escape);
        pump(80);

        window.discardOnClose();
        window.shutdown();
        pump(200);
        std::remove(cfg.c_str());
        std::remove(theme.c_str());
        ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
    }


    {
        // A panel is as large as what it holds. It used to be as large as its
        // instructions: the line of keys along the bottom was set in the same size as the
        // rows and was never allowed to break, so it was the widest thing in every panel and
        // every panel was built round it. (Kaida, 2026-11-20: "The settings windows are too
        // large and should be more rounded ( the tooltips are the culprit, they take too much
        // space and make the windows larger, tooltips should have a reduced size)".)
        const std::string cfg = root + "/panel-size-config";
        std::ofstream(cfg) << "font_size = 16\n";
        ::setenv("KAI_TERM_CONFIG", cfg.c_str(), 1);
        kai::AppWindow window(kai::startWindowBackend(24, 80));
        window.resize(900, 560);
        window.show();
        static_cast<void>(waitFor([&window] { return !window.sessionName().empty(); }));
        pump(200);

        const auto press = [&window](int key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
            QKeyEvent ev(QEvent::KeyPress, key, mods);
            QCoreApplication::sendEvent(&window, &ev);
        };

        openThemeEditor(&window);
        pump(120);

        // The whole list, when the window has room for it. The list used to cap itself at
        // a dozen rows and scroll the rest in any window, including one with plenty of
        // room; now it takes the height it is given and only scrolls when it genuinely
        // has to — and then it says so.
        // (Kaida, 2026-11-21: "when there is enough space show all color in the theme
        // editor instead of having to scroll. prioritize showing everything without
        // scrolling, and when scrolling is needed can we show a little indicator to say
        // that there is more to be seen if the user scrolls".)
        // The heading is nearer the panel's top edge than the rows are, which is what
        // "a bit more up" looks like: it is a title, not the first line of the thing it
        // titles. (Kaida, 2026-11-21: 'the title "Colours" to be a bit more up and also
        // changed for "Theme Editor"'.)
        const QRectF title = window.panelTitleRect();
        const QRectF first_row = window.pickerRowRect(0);
        expect("the colour editor is called what the row that opens it is about",
               window.pickerTitleText() == QStringLiteral("Theme Editor"));
        expect("and its title sits nearer the top of the panel than the rows do",
               title.top() - window.pickerPanelRect().top() < first_row.top() - title.bottom());
        const int rows = window.pickerRowCount();
        expect("a window with room for the list shows every colour on it",
               rows == 22 && window.pickerVisibleRows() >= rows &&
                   window.pickerFirstRow() == 0);
        expect("and says nothing about there being more to see",
               !window.pickerMoreAbove() && !window.pickerMoreBelow());

        window.resize(430, 320);
        pump(200);
        expect("a window too small for the list scrolls it instead",
               window.pickerVisibleRows() < rows);
        expect("with the keys at the top, that is more below it says",
               !window.pickerMoreAbove() && window.pickerMoreBelow());
        const QRectF down_mark = window.pickerScrollMarkRect(false);
        expect("and the mark for it is inside the panel and clear of the rows",
               window.pickerPanelRect().contains(down_mark) &&
                   !down_mark.intersects(window.pickerRowRect(window.pickerFirstRow())));
        // The mark belongs to the rows, not to the gap. Centred in the air between the
        // list and the words it read as a thing in the middle of nowhere; it sits nearer
        // the last row now. (Kaida, 2026-11-21: "the scrolling indicator on the Theme
        // editor pointing down should be closer to the color a bit more up".)
        const int last_shown = window.pickerFirstRow() + window.pickerVisibleRows() - 1;
        const double to_rows = down_mark.top() - window.pickerRowRect(last_shown).bottom();
        const double to_words = window.pickerSaveRect().top() - down_mark.bottom();
        expect("and nearer the last colour than the words below it",
               to_rows > 0.0 && to_rows < to_words);
        for (int i = 1; i < rows; ++i) { // the last colour, without walking off the list
            press(Qt::Key_Down);
            pump(20);
        }
        expect("walking to the end of it says there is more above instead",
               window.pickerMoreAbove() && !window.pickerMoreBelow());
        const QRectF up_mark = window.pickerScrollMarkRect(true);
        expect("and that mark is clear of the rows too",
               window.pickerPanelRect().contains(up_mark) &&
                   !up_mark.intersects(window.pickerRowRect(rows - 1)));
        const int narrow_hint_lines = window.panelHintLines(); // the keys are on a colour
        press(Qt::Key_Down); // the last colour, then the theme name
        pump(60);
        // At a width where the old wording needed a third line, this is the same check
        // with the room taken away: a name field whose keys take more lines than any
        // other field's is a panel that changes shape under the keys.
        expect("and the same at a width where the old wording wrapped further",
               window.panelHintLines() == narrow_hint_lines &&
                   window.panelHintText() ==
                       QStringLiteral("Arrows to Navigate - Enter to Edit - Escape to Cancel"));
        press(Qt::Key_Up); // back on to the colour for what follows
        pump(40);
        window.resize(900, 560);
        pump(200);
        for (int i = 1; i < rows; ++i) { // and the keys back to the top for what follows
            press(Qt::Key_Up);
            pump(20);
        }

        // The line of keys measured in the size of a panel's own rows — which is exactly how
        // wide a panel had to be to carry it on one line, and so the width every panel used
        // to have.
        QFont hint_voice = window.font();
        hint_voice.setPixelSize(window.panelRowsPixels());
        const double sentence_w = QFontMetricsF(hint_voice).horizontalAdvance(
            QStringLiteral("Arrows to Navigate - Enter to Select - Escape to Cancel"));
        expect("the line of keys is set smaller than the rows above it",
               window.panelHintPixels() < window.panelRowsPixels());
        expect("and broken over the panel's width instead of widening the panel",
               window.panelHintLines() >= 2);
        expect("every line of it inside the panel",
               window.pickerPanelRect().contains(window.panelHintRect()));
        expect("the two words sit above it, not through it",
               window.pickerWordsRect().bottom() <= window.panelHintRect().top());
        // The complaint asked directly. The settings menu's own rows happen to be about as
        // wide as the sentence, so it is the colour list — twenty-one colours, and a panel
        // built round a sentence about arrow keys — where the difference is honest.
        expect("a panel is narrower than the sentence along its bottom",
               window.pickerPanelRect().width() < sentence_w);
        // The theme field said `(e to edit)` after its name, in a value column ten cells
        // wide, and the note walked out through the side of the panel. It is said along the
        // bottom now, where the panel's other keys are said.
        // (Kaida, 2026-11-20: "the (e to edit) tool tip render outside our frame and it's not
        // beatiful at all".)
        const double picker_h = window.pickerPanelRect().height();
        const int hint_lines = window.panelHintLines();
        press(Qt::Key_Up); // up from the first row wraps to `cancel`…
        press(Qt::Key_Up); // …then to `save`…
        press(Qt::Key_Up); // …and once more lands on the theme name
        pump(100);
        expect("the keys are standing on the theme name",
               window.pickerNameRect().contains(window.pickerHighlightRect()));
        // The name's line of keys is the panel's usual line with one word swapped, rather
        // than the usual line with a clause bolted on the front — which is what made the
        // panel taller the moment the keys walked on to the name.
        // (Kaida, 2026-11-21: "The tooltips should stay the same size as every other field
        // even on the name field. The name field should just change the \"Select\" word to
        // \"Edit\" instead of adding a whole new line in the beginning of the tooltip".)
        expect("the name field says the usual keys with one word swapped",
               window.panelHintText() ==
                   QStringLiteral("Arrows to Navigate - Enter to Edit - Escape to Cancel"));
        expect("and takes up exactly as much room as it did on a colour",
               window.panelHintLines() == hint_lines);
        expect("inside the panel, like everything else here",
               window.pickerPanelRect().contains(window.panelHintRect()));
        // The gutter to the right of the panel, along the name's own line. The old note ran
        // out through the frame and into it, which is the thing that was "not beatiful at
        // all" — so this asks whether anything at all was painted out there.
        const QImage shot = window.grab().toImage();
        const int gutter_y = static_cast<int>(window.pickerNameRect().center().y());
        const int edge = static_cast<int>(window.pickerPanelRect().right());
        const QColor clean = shot.pixelColor(edge + 70, gutter_y);
        bool gutter_clean = true;
        for (int x = edge + 3; x < edge + 40; ++x) {
            if (shot.pixelColor(x, gutter_y) != clean)
                gutter_clean = false;
        }
        expect("and nothing at all is painted outside the panel's own frame", gutter_clean);
        expect("and saying them does not resize the panel under the rows",
               std::abs(window.pickerPanelRect().height() - picker_h) < 1.0);
        press(Qt::Key_Escape);
        pump(80);
        // The name and the words used to hang together as one block of three at the bottom of
        // the list. (Kaida, 2026-11-20: "The theme name is too close to the save and cancel
        // buttons it should be a bit higher".)
        expect("the theme name has room of its own above the two words",
               window.pickerNameRect().bottom() + 0.5 * window.pickerNameRect().height() <=
                   window.pickerWordsRect().top());
        press(Qt::Key_Escape);
        pump(80);

        press(Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
        pump(120);
        expect("and the settings menu's words are clear of its hint too",
               window.settingsWordsRect().bottom() <= window.panelHintRect().top());
        // Two gaps the panels had run out of: one under the title, and one where the rows
        // stop and the two words begin. Both asked for in rows, so they are the same shape
        // of gap at every size a panel is drawn at.
        // (Kaida, 2026-11-20: "a bit more space between the title and the actual settings,
        // and a bit more space between the settings and the save / cancel buttons".)
        const double row_h = window.settingsRowRect(0).height();
        expect("the title has room of its own above the rows",
               window.settingsRowRect(0).top() - window.panelTitleRect().bottom() >= 0.5 * row_h);
        expect("and the rows stop short of the two words",
               window.settingsWordsRect().top() - window.settingsBottomRowRect().bottom() >=
                   0.5 * row_h);

        // The corner. Four pixels of rounding on a panel several hundred wide reads as a
        // rectangle with soft edges, which is not what was asked for. This finds where the
        // fill actually starts along the panel's own top row, so it is a question about the
        // painted corner rather than about a constant somebody could set to anything.
        const QImage painted = window.grab().toImage();
        const QRectF panel = window.settingsPanelRect();
        const int left = static_cast<int>(panel.left());
        const int top = static_cast<int>(panel.top());
        const QColor inside = painted.pixelColor(left + 10, top + 10);
        int filled_from = 0;
        for (int x = 0; x < 24; ++x) {
            if (painted.pixelColor(left + x, top + 2) == inside) {
                filled_from = x;
                break;
            }
        }
        expect("and a panel's corner is cut, not merely softened", filled_from >= 4);

        window.discardOnClose();
        window.shutdown();
        pump(200);
        std::remove(cfg.c_str());
        ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
    }


    {
        // The menu and the listing used to disagree. `session list` showed the shells a
        // window had left behind; Ctrl+Shift+A offered only the ones somebody had saved.
        // The one you were hunting for was the one the menu could not see, which is the
        // same half a rule as a keep that never expires: two places answering one
        // question differently. They read from the same list now.
        SessionSpec orphan;
        orphan.name = "orphan";
        orphan.saved = false;
        orphan.rows = 24;
        orphan.cols = 80;
        std::string orphan_error;
        expect("a shell is left behind with nobody looking at it",
               daemon.create(orphan, orphan_error));
        kai::AppWindow window(kai::startWindowBackend(24, 80));
        const std::vector<SessionInfo> choices = window.restorableSessions();
        const SessionInfo* found = nullptr;
        for (const SessionInfo& info : choices) {
            if (info.name == "orphan")
                found = &info;
        }
        expect("the menu offers the shell nobody saved", found != nullptr);
        if (found != nullptr) {
            const std::string label = sessionMenuLabel(*found);
            expect("and says what it is: " + label, label.find("left behind") != std::string::npos);
            expect("with the number a person would type: " + label,
                   label.rfind(std::to_string(found->id) + "  ", 0) == 0);
        }
        // The window you are in is not somewhere else, whatever else is true of it.
        expect("and the menu leaves out the session this window is showing",
               window.sessionName() != "orphan");

        // Ctrl+Shift+A puts the list inside the terminal, and it is walked like any
        // TUI picker: the sessions are rows, Enter goes to the one under the
        // highlight, and the whole thing is gone the moment it has been answered.
        window.showSessionMenu();
        expect("the list is drawn inside the terminal", window.overlayActive());
        const std::vector<std::string> rows = window.overlayChoices();
        expect("and it offers the orphan as a row to choose",
               rows.size() == 1 && rows[0].find("orphan") != std::string::npos);
        sendKey(&window, Qt::Key_Return);
        expect("Enter goes to the session under the highlight",
               waitFor([&window] { return window.sessionName() == "orphan"; }));
        expect("and the list is gone once it has been answered", !window.overlayActive());

        window.shutdown();
        static_cast<void>(daemon.kill("orphan", orphan_error));
    }

    {
        // "Save Session", the third answer, is the one that lets the window go and
        // keeps the shell: after it the window is gone, and the session is still
        // there — saved, with nobody looking at it — waiting for the next window
        // that asks for it.
        kai::AppWindow leaving(kai::startWindowBackend(24, 80));
        leaving.show();
        static_cast<void>(waitFor([&leaving] { return !leaving.sessionName().empty(); }));
        const std::string name = leaving.sessionName();
        runLine(&leaving, "sleep 60");
        expect("with a program running to make the close worth asking about",
               waitFor([&leaving] { return leaving.runningProgram() == "sleep"; }));
        leaving.close();
        expect("the question offers all three answers", leaving.overlayChoices().size() == 3);
        sendKey(&leaving, Qt::Key_S); // S is Save Session, wherever the focus is
        expect("answered Save Session, the window goes",
               waitFor([&leaving] { return !leaving.isVisible(); }));
        expect("and the session is still there, kept for the next window",
               waitFor([&daemon, &name] {
                   return sees(daemon, name, [](const SessionInfo& info) {
                       return info.alive && info.saved;
                   });
               }));
        leaving.shutdown();
        pump(200);
        static_cast<void>(daemon.kill(name, error));
    }

    {
        // Ctrl+Shift+A over a running program asks the same shape of question
        // that the close button asks, because the switch ends this shell and
        // whatever it is doing goes with it. "Save Session" is the answer that
        // keeps the work and still switches: after it the window is looking at
        // the session it chose, and the one it left is still there, saved.
        SessionSpec elsewhere;
        elsewhere.name = "elsewhere";
        elsewhere.saved = false;
        elsewhere.rows = 24;
        elsewhere.cols = 80;
        std::string elsewhere_error;
        expect("a session to go to", daemon.create(elsewhere, elsewhere_error));
        kai::AppWindow window(kai::startWindowBackend(24, 80));
        window.show();
        static_cast<void>(waitFor([&window] { return !window.sessionName().empty(); }));
        const std::string here = window.sessionName();
        runLine(&window, "sleep 90");
        expect("with something running in the one being left",
               waitFor([&window] { return window.runningProgram() == "sleep"; }));
        window.showSessionMenu();
        sendKey(&window, Qt::Key_1); // the first row, chosen by number
        expect("the switch asks the same question the close asks, in the same shape",
               window.overlayActive());
        const std::string asked = window.overlayQuestion();
        expect("it names what would be lost and what would be loaded: " + asked,
               asked.find("sleep") != std::string::npos &&
                   asked.find("elsewhere") != std::string::npos);
        const std::vector<std::string> answers = window.overlayChoices();
        expect("Yes / No / Save Session, No focused",
               answers.size() == 3 && answers[0] == "Yes" && answers[1] == "No" &&
                   answers[2] == "Save Session" && window.overlaySelected() == 1);
        sendKey(&window, Qt::Key_N);
        expect("N keeps the question honest: nothing moved",
               waitFor([&window] { return !window.overlayActive(); }) &&
                   window.sessionName() == here);
        expect("and the program is still running", window.runningProgram() == "sleep");
        // Ask again and answer S: this shell is kept — under a name of the kept
        // range, the way Ctrl+Shift+S renames it — and the window moves on.
        window.showSessionMenu();
        sendKey(&window, Qt::Key_1);
        expect("asked again", window.overlayActive());
        sendKey(&window, Qt::Key_S);
        expect("S saves this shell and switches",
               waitFor([&window] { return window.sessionName() == "elsewhere"; }));
        expect("and the shell that was left is still there, saved",
               waitFor([&daemon] {
                   for (const SessionInfo& info : daemon.list())
                       if (info.saved && info.alive && info.name != "elsewhere")
                           return true;
                   return false;
               }));
        window.shutdown();
        for (const SessionInfo& info : daemon.list())
            static_cast<void>(daemon.kill(info.name, elsewhere_error));
    }

    {
        // Two things the walking got wrong, on a window nobody else is touching:
        // coming back up from `save` skipped the last row, and the left/right arrows
        // moved a cursor nobody asked about instead of changing the value. (Kaida,
        // 2026-11-20: "the last option gets skipped when going back up"; "Arrows
        // should edit the value instead of moving the cursor on everything but the
        // colors".)
        const std::string walk_cfg = root + "/walk-config";
        std::ofstream(walk_cfg) << "font_size = 12\npadding = 7\n";
        ::setenv("KAI_TERM_CONFIG", walk_cfg.c_str(), 1);
        kai::AppWindow walking(kai::startWindowBackend(24, 80));
        walking.resize(900, 600);
        walking.show();
        pump(200);

        const auto walk_key = [&walking](int key) {
            QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier);
            QCoreApplication::sendEvent(&walking, &ev);
        };
        const auto open_settings = [&walking] {
            QKeyEvent ev(QEvent::KeyPress, Qt::Key_K,
                         Qt::ControlModifier | Qt::ShiftModifier);
            QCoreApplication::sendEvent(&walking, &ev);
        };
        const int rows = walking.settingsRowCount(); // asked, not counted by hand
        const int padding_row = 3;

        open_settings();
        pump(120);
        expect("the settings menu is up", walking.settingsActive());
        for (int i = 0; i < rows; ++i) { // past the last row, onto `save`
            walk_key(Qt::Key_Down);
            pump(20);
        }
        expect("walking down off the last row lands on `save`",
               walking.settingsCursor().second == 1);
        walk_key(Qt::Key_Up);
        pump(20);
        expect("and one step back up is the last row, not the one before it",
               walking.settingsCursor().first == rows - 1 && walking.settingsCursor().second == 0);

        const QRectF row = walking.settingsRowRect(padding_row);
        const QPointF at(row.left() + 4.0, row.center().y());
        QMouseEvent press(QEvent::MouseButtonPress, at, walking.mapToGlobal(at.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, at, walking.mapToGlobal(at.toPoint()),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&walking, &press);
        QCoreApplication::sendEvent(&walking, &release);
        pump(40);
        expect("a setting is chosen by clicking its name",
               walking.settingsCursor().first == padding_row &&
                   walking.settingsCursor().second == 0 && walking.padding() == 7.0);
        walk_key(Qt::Key_Right);
        pump(40);
        const double nudged = walking.padding();
        walk_key(Qt::Key_Left);
        pump(40);
        expect("and the arrows change the value rather than moving a cursor",
               nudged == 8.0 && walking.padding() == 7.0);

        walk_key(Qt::Key_Escape);
        pump(60);
        expect("Escape leaves without writing",
               !walking.settingsActive() && walking.padding() == 7.0);

        walking.discardOnClose();
        walking.shutdown();
        pump(200);
        std::remove(walk_cfg.c_str());
    }

    {
        // The same rule, met the way it is met in practice: a shell painting over the
        // window's colours, and then a new shell starting in the same window. (Kaida,
        // 2026-11-20: "a session spawn still gets rid of our theming".) Starting a
        // fresh session builds a brand-new screen, and a new screen starts with the
        // built-in colours and no locks — so the theme went out with the old one and
        // whatever the new shell said came in.
        const std::string hold_cfg = root + "/hold-config";
        const std::string hold_theme = root + "/hold-theme";
        std::ofstream(hold_cfg) << "font_size = 12\ncursor_style = block\n"
                               << "cursor_blink = 200\n";
        std::ofstream(hold_theme)
            << "foreground = #22cc55\nbackground = #05100a\ncolor1 = #ff7700\n";
        ::setenv("KAI_TERM_CONFIG", hold_cfg.c_str(), 1);
        ::setenv("KAI_TERM_THEME", hold_theme.c_str(), 1);

        kai::AppWindow holding(kai::startWindowBackend(24, 80));
        holding.resize(900, 600);
        holding.show();
        pump(300);
        expect("a window with a theme to hold", holding.screenDefaultBackground().r == 0x05 &&
                                                   holding.screenDefaultForeground().g == 0xcc);

        runLine(&holding, "printf '\\033]11;#ff0000\\007\\033]10;#ff0000\\007\\033]4;1;#ff0000\\007'");
        pump(400);
        expect("a shell repainting its own colours does not move the window's theme",
               holding.screenDefaultBackground().r == 0x05 &&
                   holding.screenDefaultForeground().r == 0x22 &&
                   holding.screenPaletteColour(1).g == 0x77 &&
                   holding.screenPaletteColour(1).b == 0x00);

        holding.saveCurrentSession(); // keep this shell, start a fresh one here
        pump(400);
        expect("the fresh shell arrives in a window that still has its theme",
               holding.screenDefaultBackground().r == 0x05 &&
                   holding.screenDefaultBackground().g == 0x10 &&
                   holding.screenDefaultForeground().g == 0xcc);
        expect("and the colours that outrank a shell still outrank it",
               holding.screenPaletteColour(1).r == 0xff &&
                   holding.screenPaletteColour(1).g == 0x77);

        runLine(&holding, "printf '\\033]11;#00ff00\\007'");
        pump(400);
        expect("the new shell cannot paint over it either",
               holding.screenDefaultBackground().r == 0x05);

        // The cursor, same story. A new screen starts with no locks and the shape the
        // window happens to prefer, which was the *default* — and a default is exactly
        // what DECSCUSR outranks. So the fresh session wore the file's shape until it
        // said otherwise, which a shell does the moment it draws a prompt.
        // (Kaida, 2026-11-21: "still loosing cursor style on a new session / when
        // attaching a session. Fresh start still hold the right one from config".)
        expect("the fresh session arrives wearing the shape the file named",
               holding.screen().cursorShape() == kai::Screen::CursorShape::Block);
        runLine(&holding, "printf '\\033[6 q'"); // blinking bar, asked for by the shell
        pump(400);
        expect("and the new shell asking for a bar leaves the file's shape alone",
               holding.screen().cursorShape() == kai::Screen::CursorShape::Block);
        expect("the blink the file named is a thing the window keeps across a session",
               holding.cursorBlink() == 200);

        holding.discardOnClose();
        holding.shutdown();
        pump(200);
        std::remove(hold_cfg.c_str());
        std::remove(hold_theme.c_str());
    }

    {
        // A window holds its theme; a session does not. (Kaida, 2026-11-20: "Theme
        // is lost when attaching a session, which should not happen, a session
        // doesn't hold theme value a windows should … anyway windows hold the
        // theme, not sessions".) The session came up wearing whatever the daemon's
        // screen starts with, and its snapshot re-sends those colours as OSC 10/11 —
        // and re-sends them again on every resize. The window that attaches reads
        // its own config, and that is what its grid wears.
        const std::string wear_cfg = root + "/wear-config";
        const std::string wear_theme = root + "/wear-theme";
        std::ofstream(wear_cfg) << "font_size = 12\ncursor_style = block\n";
        // Only one colour is named. The rest of the theme is whatever the built-in
        // colours are, which is the part that used to be lost: a colour the file did
        // not name was not a lock, and an attached window never applied its theme's
        // defaults at all, so the session's red-on-green came back through the
        // snapshot and stayed.
        std::ofstream(wear_theme) << "color1 = #ff7700\n";
        ::setenv("KAI_TERM_CONFIG", wear_cfg.c_str(), 1);
        ::setenv("KAI_TERM_THEME", wear_theme.c_str(), 1);

        // A session whose shell paints over its own colours before anybody looks:
        // red ink, a green ground, a blue `color1`. Those travel in its snapshot, and
        // they are deliberately nothing like the theme this window is about to read.
        // (Whether the script gets as far as painting is a property of this harness,
        // not of the rule; what the checks below pin down is the rule — the window's
        // theme is what its screen wears, attached or not. Kaida saw the colours go
        // missing on a real attach, 2026-11-20, and the asymmetry that let that
        // happen was `applyTheme` refusing to touch a screen the window had not
        // started. That refusal is gone; see applyTheme.)
        const std::string painter = root + "/paint-colours.sh";
        {
            std::ofstream script(painter);
            script << "#!/bin/sh\n"
                   << "printf '\\033]10;#ff0000\\033\\\\\\n'\n"
                   << "printf '\\033]11;#00ff00\\033\\\\\\n'\n"
                   << "printf '\\033]4;1;#0000ff\\033\\\\\\n'\n"
                   << "printf '\\033[6 q'\n" // a bar, asked for before anybody looks
                   << "while :; do printf '\\033[6 q'; sleep 0.2; done\n";
        }
        ::chmod(painter.c_str(), 0755);

        SessionSpec worn;
        worn.name = "worn";
        worn.shell = painter;
        worn.rows = 24;
        worn.cols = 80;
        worn.saved = false;
        std::string worn_error;
        expect("a session is running to attach to", daemon.create(worn, worn_error));
        pump(600); // long enough for the shell to have painted and gone to sleep

        kai::AppWindow attached(std::string("worn"));
        attached.show();
        pump(300);
        expect("an attached window reads its own theme",
               attached.theme().foreground.r != 0xff);
        // The theme names only `color1`, so the ink and the ground are the theme's
        // own defaults, and those are what the screen must be wearing.
        expect("the screen it is looking at wears the window's theme, not the session's",
               attached.screenDefaultForeground().r == attached.theme().foreground.r &&
                   attached.screenDefaultForeground().g == attached.theme().foreground.g &&
                   attached.screenDefaultBackground().r == attached.theme().background.r &&
                   attached.screenDefaultBackground().g == attached.theme().background.g);
        expect("so the session's red ink did not survive the attach",
               attached.screenDefaultForeground().r != 0xff &&
                   attached.screenDefaultBackground().g != 0xff);
        expect("and a palette entry the theme names outranks the one the session painted",
               attached.screenPaletteColour(1).r == 0xff &&
                   attached.screenPaletteColour(1).g == 0x77);
        // The session has been asking for a bar since before this window arrived — once
        // in the snapshot it replays, and again every fifth of a second after that. The
        // shape the attaching window's file names is the one on screen.
        // (Kaida, 2026-11-21: "still loosing cursor style … when attaching a session".)
        expect("an attached window wears the cursor shape its own file names",
               attached.screen().cursorShape() == kai::Screen::CursorShape::Block);

        // The same session in a window whose file names no shape, so that the check above
        // is known to be a refusal and not a session that never asked. (A check about a
        // lock that passes with the lock removed is not a check; this one is only worth
        // having next to its partner.)
        const std::string wear_cfg_free = root + "/wear-config-free";
        std::ofstream(wear_cfg_free) << "font_size = 12\n";
        ::setenv("KAI_TERM_CONFIG", wear_cfg_free.c_str(), 1);
        {
            kai::AppWindow unshaped(std::string("worn"));
            unshaped.show();
            pump(600);
            expect("and a window whose file names no shape lets the session have its bar",
                   unshaped.screen().cursorShape() == kai::Screen::CursorShape::Bar);
            unshaped.discardOnClose();
            unshaped.shutdown();
            pump(200);
        }
        ::setenv("KAI_TERM_CONFIG", wear_cfg.c_str(), 1);
        std::remove(wear_cfg_free.c_str());

        // A resize is the second way a theme used to wash out: the session answers a
        // new geometry with a whole new snapshot, colours and all.
        attached.resize(560, 300);
        pump(300);
        expect("a resize does not hand the colours back to the session",
               attached.screenDefaultForeground().r == attached.theme().foreground.r &&
                   attached.screenDefaultBackground().g != 0xff &&
                   attached.screenPaletteColour(1).g == 0x77);

        attached.discardOnClose();
        attached.shutdown();
        pump(200);

        // And the way it happens for real: a window that was already showing some other
        // session moves over to this one, throwing away the screen it was drawing —
        // which is the very screen the lock had been written on. A fresh start held the
        // shape and every session after it did not, because the second case came through
        // here. (Kaida, 2026-11-21: "still loosing cursor style on a new session / when
        // attaching a session. Fresh start still hold the right one from config".)
        {
            SessionSpec again;
            again.name = "worn-again";
            again.shell = painter; // the same shell, still asking for a bar
            again.rows = 24;
            again.cols = 80;
            again.saved = false;
            std::string again_error;
            expect("a second session is running to move onto",
                   daemon.create(again, again_error));
            pump(600);

            kai::AppWindow moving(kai::startWindowBackend(24, 80));
            moving.resize(900, 600);
            moving.show();
            pump(300);
            expect("a window with a session of its own to walk away from",
                   moving.screen().cursorShape() == kai::Screen::CursorShape::Block);
            moving.restoreSession("worn-again");
            pump(600);
            expect("moving onto a session that keeps asking for a bar keeps the shape",
                   moving.screen().cursorShape() == kai::Screen::CursorShape::Block);
            moving.discardOnClose();
            moving.shutdown();
            pump(200);
        }

        std::remove(wear_cfg.c_str());
        std::remove(wear_theme.c_str());
        std::remove(painter.c_str());
        ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);
    }

    daemon.stop();
    serving.join();

    ::unlink((root + "/kai-term/daemon.sock").c_str());
    ::rmdir((root + "/kai-term").c_str());
    ::rmdir(root.c_str());

    if (failures == 0) {
        std::cout << "PASS: " << checks << " application window checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " application window checks failed\n";
    return 1;
}
