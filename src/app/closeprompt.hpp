#pragma once

// Whether closing a window needs asking about, and what the asking is.
//
// Kept apart from the dialog because the question is the part worth testing and the
// dialog is the part a person has to look at. The question is one line — the answers
// say the rest — because a paragraph underneath a yes-or-no is a wall between a
// person and the buttons. Nothing here knows about Qt.

#include <string>

namespace kai {

// How often closing should interrupt. Said in words because a person has to be able to
// set it without reading this file: `running` is the case where closing actually loses
// something, `always` is for somebody who wants to be asked regardless, `never` is for
// somebody who has a window manager they trust and a habit of closing windows.
enum class ConfirmPolicy {
    Never,
    Running,
    Always,
};

// The words a person writes are the words here: `running`, `always`, `never`. Anything
// else is `running`, which is the one that asks when there is something to lose.
inline ConfirmPolicy confirmPolicy(const std::string& setting) {
    if (setting == "never")
        return ConfirmPolicy::Never;
    if (setting == "always")
        return ConfirmPolicy::Always;
    return ConfirmPolicy::Running;
}

// What a person chose when a window with work in it was closed is three things:
// quit and take the shell along, stay, or save the session and let the window
// go. The window keeps those answers as plain flags rather than as an enum,
// because the question is now asked inside the terminal and answered through a
// callback — see `AppWindow::closeEvent` — and one-off answers do not need a
// type. The words worth testing are the ones below.

// The question to ask before closing, or empty when closing loses nothing and should
// simply happen.
//
// Two things have to be true to interrupt somebody. Something has to be running, and
// closing the window has to be what stops it. A session that was kept goes on whether
// or not a window is looking at it, so closing one of those is not a decision about
// the program at all — it is an ordinary window closing, and asking about it would
// teach the person that windows and sessions are the same thing.
// Whether closing asks is decided by one thing: is there a program running.
//
// It used to also ask whether the session was kept, on the grounds that closing the
// window of a kept session loses nothing. Kaida closed two of those in a row and reported
// the second as a bug, and they are right: keeping a session says what happens to the
// shell, not that the window may throw away whatever is running inside it on the way out.
// Being kept changes only whether the Save Session button is offered, which on a kept
// session would be a second way to press the first one. It does not buy silence.
inline std::string closeQuestion(const std::string& running_program,
                                 ConfirmPolicy policy = ConfirmPolicy::Running) {
    if (policy == ConfirmPolicy::Never)
        return {};
    if (policy == ConfirmPolicy::Always) {
        // Asked regardless, so the question has to be the one that is true either way.
        // "Quit while nothing is running?" is not a question, and naming no program is
        // honest about a window that is holding a shell at a prompt.
        return running_program.empty() ? "Quit Kai-Term?"
                                       : "Quit while " + running_program + " is running?";
    }
    if (running_program.empty())
        return {};
    return "Quit while " + running_program + " is running?";
}

// Saving a session is only on the menu when there is a session for the daemon to hold
// on to. A window running a shell of its own has nothing to save, and offering a third
// button that does the same thing as the first would be a lie with an extra click.
inline bool closeOfferSave(const std::string& session_name, bool session_kept) {
    // A kept session is already saved in everything but the highlight: it goes on
    // running whether or not this window does, so a "save" button would be a second
    // way to press the first one.
    return !session_name.empty() && !session_kept;
}

} // namespace kai
