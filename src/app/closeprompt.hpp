#pragma once

// Whether closing a window needs asking about, and what to say when it does.
//
// Kept apart from the dialog because the question is the part worth testing and the
// dialog is the part a person has to look at. Nothing here knows about Qt.

#include <string>

namespace kai {

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
inline std::string closeQuestion(const std::string& running_program, bool session_kept) {
    if (running_program.empty() || session_kept)
        return {};
    return "Quit while " + running_program + " is running?";
}

// The second line, which is where the difference between the two ways of quitting is
// said. Ending a window ends a shell; detaching is the one that keeps both, and it is
// worth naming the session it keeps so that the person knows what to look for later.
inline std::string closeDetail(const std::string& session_name) {
    if (session_name.empty())
        return "Ending this window ends the shell in it, and whatever is running in "
               "that shell.";
    return "Ending this window ends the shell in it. Detach keeps it running, and " +
           session_name + " will still be there to come back to.";
}

// Detaching is only on the menu when there is a session for the daemon to hold on to.
// A window running a shell of its own has nothing to detach from, and offering a third
// button that does the same thing as the first would be a lie with an extra click.
inline bool closeOfferDetach(const std::string& session_name) {
    return !session_name.empty();
}

} // namespace kai
