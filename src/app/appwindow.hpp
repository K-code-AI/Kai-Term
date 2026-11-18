#pragma once

// The window a person runs: a terminal, plus the three things about a terminal that
// have nothing to do with bytes — keeping a shell, finding the ones that were kept,
// and not throwing work away when it is closed with something running in it.
//
// All three are decided here rather than in the terminal widget, because none of them
// is about the screen. The widget is told which keybinds mean something to its owner
// and which session it is showing; it does not know a daemon exists.

#include "closeprompt.hpp"
#include "../session/cli.hpp"
#include "../session/sessionbackend.hpp"
#include "sessionmenu.hpp"
#include "../terminal/terminalwidget.hpp"

#include <QCloseEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace kai {

// What a window starts out showing, and what to say about how it got that way. The
// size travels with it, because the shell was already started at that size: a window
// built at a different one would spend its first moment resizing the shell that came
// with it, and the two numbers would disagree about what the terminal is.
struct WindowStart {
    std::unique_ptr<TerminalBackend> backend;
    std::string session; // empty when there is no daemon to hold the shell
    std::string note;    // said once, in the corner, when the plan changed
    int rows = 24;
    int cols = 80;
};

// Get a shell for a new window, held by the daemon if there is one.
//
// Starting the daemon is not a step anybody should have to take before their terminal
// works, so it is started here, quietly, and a window that cannot start one carries on
// with a shell of its own. That fallback is said out loud once rather than kept quiet,
// because the difference only shows up later — when a keybind that keeps a shell keeps
// nothing — and a person who was never told will not think to look for it here.
inline WindowStart startWindowBackend(int rows, int cols) {
    std::string error;
    const std::string name = startWindowSession(rows, cols, error);
    if (!name.empty()) {
        try {
            // A daemon from another build is not on our socket, so it cannot answer a
            // command we have and an older one does not — which is the point. It is
            // still holding the shells it started, and nothing we can list will show
            // them, so the window says so once instead of leaving them to be found in
            // `ps` next week.
            return {std::make_unique<SessionBackend>(name, rows, cols), name,
                    foreignDaemonNotice(), rows, cols};
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
    return {std::make_unique<PtyBackend>(rows, cols, std::string()), std::string(),
            "no session daemon: " + (error.empty() ? std::string("it would not start") : error),
            rows, cols};
}

class AppWindow : public kai::TerminalWidget {
public:
    // A window with a shell of its own.
    explicit AppWindow(WindowStart start)
        : TerminalWidget(start.rows, start.cols, std::move(start.backend)),
          session_(std::move(start.session)) {
        bindKeybinds();
        if (!start.note.empty())
            setStatusText(start.note, 8000);
    }

    // A window onto a session that is already running. Throws when the session is not
    // there, because an empty window looks like a working terminal and is not one.
    explicit AppWindow(const std::string& name)
        : TerminalWidget(24, 80, std::make_unique<SessionBackend>(name, 24, 80)),
          session_(name),
          // Showing a session is not keeping it. It arrived kept or unkept; either way
          // this window is now responsible for it, and only a save from here keeps it.
          session_kept_(false) {
        bindKeybinds();
    }

    const std::string& sessionName() const { return session_; }
    // Whether the shell being shown is one the person said to keep. A restored session
    // is one by definition, and a fresh window is not one until the keybind says so.
    bool sessionKept() const { return session_kept_; }

    // What the close prompt would be about: the program running out front, or empty
    // when the shell is sitting at a prompt. A shell running in this process can be
    // asked directly; a session lives in the daemon, which is where the question has to
    // be asked.
    std::string runningProgram() const {
        if (session_.empty())
            return foregroundProgram();
        std::string error;
        for (const SessionInfo& info : listSessions(error)) {
            if (info.name == session_)
                return info.foreground;
        }
        return {};
    }
    // Called after the window is showing a different session, so whoever set the title
    // can set it again.
    std::function<void()> on_session_changed;

    // Ctrl+Shift+S: keep this shell, and start a fresh one in the same window.
    //
    // The window does not close. The thing a person is doing when they press this is
    // not "I have finished with this window" but "I have finished with this shell", and
    // the second one does not need a new window to carry on in.
    //
    // The kept session is renamed into the range a typed command uses, because the
    // `w` names are for shells nobody decided to keep and this one is now a thing they
    // did decide about. Renaming is not fatal if it fails: the session is kept under
    // the name it has, since keeping it is the part that matters.
    void saveCurrentSession() {
        if (session_.empty()) {
            setStatusText("no session daemon, so nothing was kept", 6000);
            return;
        }
        std::string error;
        const std::string kept = nextKeptName();
        std::string saved_as = session_;
        if (!kept.empty() && saveSession(session_, kept, error))
            saved_as = kept;
        else if (!saveSession(session_, std::string(), error)) {
            setStatusText("could not keep this session: " + error, 6000);
            return;
        }
        session_kept_ = true;

        std::unique_ptr<TerminalBackend> fresh;
        const std::string next = startWindowSession(rows(), cols(), error);
        if (!next.empty()) {
            try {
                fresh = std::make_unique<SessionBackend>(next, rows(), cols());
            } catch (const std::exception& e) {
                setStatusText(std::string("kept, but no new shell: ") + e.what(), 8000);
                return;
            }
        } else {
            // The session is safe either way; a window that could not get a second
            // shell carries on showing the one it just kept rather than going blank.
            setStatusText("kept as '" + saved_as + "', but no new shell would start", 8000);
            return;
        }

        // `false`, because there is nothing to discard: the shell behind this one was
        // kept a moment ago, and a window moving on does not end a kept session.
        showSession(std::move(fresh), false);
        session_ = next;
        session_kept_ = false;
        setStatusText("session '" + saved_as + "' kept");
        if (on_session_changed)
            on_session_changed();
    }

    // Where this window could go instead of staying here. That is everything the daemon
    // would put in a listing — kept, or left behind when a window went away without being
    // asked to — minus the one being shown, which is not somewhere else.
    //
    // The left-behind ones belong here. A shell with no window and a program in it is
    // precisely what the menu is for, and `session list` had been saying so all along;
    // offering only the saved ones was half a rule, the same half as a keep that never
    // expires. Separate from the menu itself so what it would say can be checked without
    // a person clicking it.
    std::vector<SessionInfo> restorableSessions() const {
        std::vector<SessionInfo> out;
        std::string error;
        for (const SessionInfo& info : worthListing(listSessions(error))) {
            if (info.name != session_)
                out.push_back(info);
        }
        return out;
    }

    // Ctrl+Shift+A: the kept sessions, in a menu you can point at and click.
    //
    // The listing is taken when the key is pressed rather than kept up to date, because
    // the alternative is a menu that is wrong about what is running the moment a window
    // opens or a session is killed somewhere else.
    void showSessionMenu() {
        QMenu menu(this);
        const std::vector<SessionInfo> choices = restorableSessions();
        if (choices.empty()) {
            QAction* nothing = menu.addAction("nothing to go back to — Ctrl+Shift+S keeps this shell");
            nothing->setEnabled(false);
        }
        for (const SessionInfo& info : choices) {
            QAction* entry = menu.addAction(QString::fromStdString(sessionMenuLabel(info)));
            entry->setData(QString::fromStdString(info.name));
        }
        // Centred on the window rather than under the pointer. The menu is called by a
        // key, and a key has no position; where the pointer happened to stop is a poor
        // place to be asked — over the line you were reading, or jammed against the edge
        // of the screen you left it on. Qt keeps the menu inside the screen from here.
        QAction* chosen = menu.exec(mapToGlobal(QPoint(width() / 2, height() / 2)));
        if (chosen == nullptr || chosen->data().isNull())
            return; // the disabled line, or a menu dismissed without choosing
        restoreSession(chosen->data().toString().toStdString());
    }

    // Bring a kept session into this window.
    void restoreSession(const std::string& name) {
        std::unique_ptr<SessionBackend> restored;
        try {
            restored = std::make_unique<SessionBackend>(name, rows(), cols());
        } catch (const std::exception& e) {
            setStatusText(std::string("could not restore '") + name + "': " + e.what(), 8000);
            return;
        }
        // Leaving this shell on purpose: if it was never kept, that is the word that
        // ends it, and if it was kept the daemon keeps it whatever this says.
        showSession(std::move(restored), true);
        session_ = name;
        // Not kept. The daemon says the same thing the moment this view arrives, and
        // saying it here too is what makes the window's own question honest: closing
        // this window will end that shell unless the person says otherwise, so the
        // status line points at the key that would stop it.
        session_kept_ = false;
        setStatusText("session '" + name + "' restored, and no longer kept"
                      " — Ctrl+Shift+S keeps it");
        if (on_session_changed)
            on_session_changed();
    }

protected:
    // Closing the window. Asked about whenever something is running, kept session or
    // not — see closeprompt.hpp for what each answer costs.
    void closeEvent(QCloseEvent* ev) override {
        const ConfirmPolicy policy = closePolicy();
        // Never means never, so it does not go and ask the daemon what is running just to
        // conclude that it will not say so.
        const std::string running = policy == ConfirmPolicy::Never ? std::string() : runningProgram();
        const std::string question = closeQuestion(running, policy);
        if (tracingClose()) {
            traceClose("session '" + session_ + "' kept=" + (session_kept_ ? "yes" : "no") +
                       ", policy=" + policyWord(policy) + ", running='" + running + "', " +
                       (question.empty() ? "nothing to ask" : "would ask: " + question));
            // Only asked for when it is being traced: this is another round trip to the
            // daemon, in the middle of a close, for the sake of a line nobody normally reads.
            traceClose("the daemon has: " + closeListing());
        }
        if (question.empty()) {
            // Nothing to ask about is not the same as nothing happening. A shell nobody
            // kept is ended by a window being closed on purpose — which is the whole
            // difference between closing a terminal and losing one — and saying so only
            // in the branch where a dialog appeared meant a quiet close left the shell
            // running with no window on it, so every window you closed politely turned
            // into a session you never asked to keep.
            endUnkeptShell();
            TerminalWidget::closeEvent(ev);
            return;
        }

        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle("Kai-Term");
        box.setText(QString::fromStdString(question));
        box.setInformativeText(QString::fromStdString(closeDetail(session_, session_kept_)));
        QPushButton* yes = box.addButton(QMessageBox::Yes);
        QPushButton* no = box.addButton(QMessageBox::No);
        QPushButton* detach = nullptr;
        if (closeOfferDetach(session_, session_kept_))
            detach = box.addButton("Detach session before closing terminal",
                                   QMessageBox::AcceptRole);
        no->setDefault(true);
        box.exec();

        const CloseAnswer answer = answerOf(box.clickedButton(), yes, no, detach);
        if (answer == CloseAnswer::Stay) {
            ev->ignore();
            return;
        }
        if (answer == CloseAnswer::CloseAnyway)
            endUnkeptShell(); // the shutdown in the destructor ends the session too
        // Detach needs nothing here: leaving a session without saying it is being
        // discarded is exactly what keeping it means.
        TerminalWidget::closeEvent(ev);
    }

private:
    // One line on stderr about what a close decided, when KAI_DEBUG_CLOSE is set.
    //
    // It exists because "the window closed without asking" has three answers from here —
    // nothing was running, the session was kept so nothing was being lost, or the
    // desktop's close never reached this window at all — and a person describing the
    // window cannot tell them apart either. What the line says, or the line not appearing
    // at all, is the difference. Quiet by default: a terminal writing over its own shell
    // while a person is typing into it would be a worse complaint than the one being
    // chased.
    static bool tracingClose() { return std::getenv("KAI_DEBUG_CLOSE") != nullptr; }

    // How often closing interrupts. The environment for now, because there is no config
    // file yet; the words are the ones Old-Term's `confirm_quit` takes, so a person who
    // knows one knows the other, and when there is a config file the value moves there
    // without changing what anybody writes.
    static ConfirmPolicy closePolicy() {
        const char* setting = std::getenv("KAI_CONFIRM_QUIT");
        return confirmPolicy(setting != nullptr ? setting : "");
    }

    static std::string policyWord(ConfirmPolicy policy) {
        switch (policy) {
            case ConfirmPolicy::Never:
                return "never";
            case ConfirmPolicy::Always:
                return "always";
            case ConfirmPolicy::Running:
                break;
        }
        return "running";
    }

    void traceClose(const std::string& what) const {
        if (!tracingClose())
            return;
        std::fprintf(stderr, "kai-term: close: %s\n", what.c_str());
        std::fflush(stderr);
        // And into a file, because the window being asked about is usually the one that
        // was started from the terminal the line would have gone to: asked to look at
        // what a closed window printed is a request that cannot be carried out.
        const char* runtime = std::getenv("XDG_RUNTIME_DIR");
        const std::string path =
            std::string(runtime != nullptr && runtime[0] != '\0' ? runtime : "/tmp") +
            "/kai-term-close.log";
        if (FILE* log = std::fopen(path.c_str(), "a")) {
            std::fprintf(log, "close: %s\n", what.c_str());
            std::fclose(log);
        }
    }

    // What the daemon had to say about the shells it holds, at the moment of the close.
    // Whether the shell being shown was in that answer at all is the difference between
    // "nothing is running out front" and "the window was not looking at the same shell
    // the daemon was describing", and those two look identical from a window that says
    // nothing either way.
    std::string closeListing() const {
        std::string error;
        std::string out;
        for (const SessionInfo& info : listSessions(error)) {
            out += info.name + "(fg=" + (info.foreground.empty() ? "-" : info.foreground) +
                   ",clients=" + std::to_string(info.clients) +
                   ",saved=" + (info.saved ? "y" : "n") + ",alive=" + (info.alive ? "y" : "n") + ") ";
        }
        if (!error.empty())
            return "the daemon said: " + error;
        if (out.empty())
            return "nothing at all";
        return out;
    }

    // Say that this window is going away on purpose, so that a shell nobody kept goes
    // with it. A kept session ignores the word, and a window that vanishes without it —
    // a crash, a kill -9 — never says it, which is what keeps that shell reachable.
    void endUnkeptShell() {
        if (!sessionKept())
            discardOnClose();
    }

    static CloseAnswer answerOf(const QAbstractButton* clicked, const QAbstractButton* yes,
                                const QAbstractButton* no, const QAbstractButton* detach) {
        if (clicked == nullptr || clicked == no)
            return CloseAnswer::Stay; // including a dialog closed by its window button
        if (detach != nullptr && clicked == detach)
            return CloseAnswer::Detach;
        if (clicked == yes)
            return CloseAnswer::CloseAnyway;
        return CloseAnswer::Stay;
    }

    // A menu entry says the name, and what is happening in it: a list of names is a
    // list of guesses about which one had the build running.


    std::string nextKeptName() const {
        std::string error;
        return freeName(listSessions(error), "s");
    }

    void bindKeybinds() {
        on_save_session = [this] { saveCurrentSession(); };
        on_session_menu = [this] { showSessionMenu(); };
    }

    std::string session_;
    bool session_kept_ = false;
};

} // namespace kai
