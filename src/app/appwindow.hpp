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
#include "../terminal/terminalwidget.hpp"

#include <QCloseEvent>
#include <QCursor>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>

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
          session_kept_(true) {
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

    // Ctrl+Shift+D: keep this shell, and start a fresh one in the same window.
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

    // The sessions a menu would offer: the kept ones, minus the one being shown, which
    // is not a thing worth restoring into the window that is already showing it.
    // Separate from the menu itself so that what the menu would say can be checked
    // without a person clicking it.
    std::vector<std::string> keptSessionNames() const {
        std::vector<std::string> names;
        std::string error;
        for (const SessionInfo& info : savedSessions(error)) {
            if (info.name != session_)
                names.push_back(info.name);
        }
        return names;
    }

    // Ctrl+Shift+A: the kept sessions, in a menu you can point at and click.
    //
    // The listing is taken when the key is pressed rather than kept up to date, because
    // the alternative is a menu that is wrong about what is running the moment a window
    // opens or a session is killed somewhere else.
    void showSessionMenu() {
        QMenu menu(this);
        const std::vector<std::string> names = keptSessionNames();
        if (names.empty()) {
            QAction* nothing = menu.addAction("nothing kept yet — Ctrl+Shift+D keeps this shell");
            nothing->setEnabled(false);
        }
        for (const std::string& name : names) {
            QAction* entry = menu.addAction(QString::fromStdString(menuLabel(name)));
            entry->setData(QString::fromStdString(name));
        }
        QAction* chosen = menu.exec(QCursor::pos());
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
        session_kept_ = true;
        setStatusText("session '" + name + "' restored");
        if (on_session_changed)
            on_session_changed();
    }

protected:
    // Closing the window. Asked about only when something is running and closing is
    // what stops it — see closeprompt.hpp for when that is.
    void closeEvent(QCloseEvent* ev) override {
        const std::string running = runningProgram();
        const std::string question = closeQuestion(running, session_kept_);
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
        box.setInformativeText(QString::fromStdString(closeDetail(session_)));
        QPushButton* yes = box.addButton(QMessageBox::Yes);
        QPushButton* no = box.addButton(QMessageBox::No);
        QPushButton* detach = nullptr;
        if (closeOfferDetach(session_))
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
    static std::string menuLabel(const std::string& name) {
        std::string error;
        for (const SessionInfo& info : listSessions(error)) {
            if (info.name != name)
                continue;
            if (!info.alive)
                return name + "   leaving";
            if (!info.foreground.empty())
                return name + "   " + info.foreground;
            return name + "   at a prompt";
        }
        return name;
    }

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
