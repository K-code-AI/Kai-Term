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
#include <QKeyEvent>
#include <QTimer>
#include <QWidget>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
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

// Type a line and press Enter. A pasted line is not a run line: bash and fish both
// turn on bracketed paste, and a shell with it on takes a pasted newline as text inside
// the paste, leaving the command at the prompt. So the commands these checks look for
// are written to print something their own command line does not contain — `${M}-WINDOW`
// is on the screen either way, `FIRST-WINDOW` only if the shell ran it.
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
    // The window reads a config file now, and a test must not be at the mercy
    // of whoever runs it having opinions about their colours. Point it at a
    // file that is not there; the blocks that want a config set it themselves.
    ::setenv("KAI_TERM_CONFIG", (root + "/no-such-config").c_str(), 1);

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
