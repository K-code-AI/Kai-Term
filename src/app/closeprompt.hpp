#pragma once

// Whether closing a window needs asking about, and what to say when it does.
//
// Kept apart from the dialog because the question is the part worth testing and the
// dialog is the part a person has to look at. Nothing here knows about Qt.

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

// What a person chose when a window with work in it was closed.
enum class CloseAnswer {
    CloseAnyway, // quit, and take the shell and whatever is running in it along
    Stay,        // nothing happens; the window stays where it was
    Detach,      // quit the window, keep the session running
};

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
// Being kept changes what the second line says — see `closeDetail` — and whether Detach is
// offered, which would be a second way to press the same button. It does not buy silence.
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

// The second line, which is where the difference between the two ways of quitting is
// said. Ending a window ends a shell; detaching is the one that keeps both, and it is
// worth naming the session it keeps so that the person knows what to look for later.
inline std::string closeDetail(const std::string& session_name, bool session_kept) {
    if (session_kept) {
        // Said because it is the surprising half of a kept session: whatever is running
        // out there keeps running, so this question is about the window, not the work.
        return "This session was kept, so it goes on running either way. Closing takes "
               "the window, not the work in it.";
    }
    if (session_name.empty())
        return "Ending this window ends the shell in it, and whatever is running in "
               "that shell.";
    return "Ending this window ends the shell in it. Detach keeps it running, and " +
           session_name + " will still be there to come back to.";
}

// Detaching is only on the menu when there is a session for the daemon to hold on to.
// A window running a shell of its own has nothing to detach from, and offering a third
// button that does the same thing as the first would be a lie with an extra click.
inline bool closeOfferDetach(const std::string& session_name, bool session_kept) {
    // A kept session is already detached in everything but name: it goes on running
    // whether or not this window does, so a "detach" button would be a second way to
    // press the first one.
    return !session_name.empty() && !session_kept;
}

} // namespace kai
