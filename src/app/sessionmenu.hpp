#pragma once

// What the session menu says about a session it could take you to.
//
// The menu is a list of places you might go back to, and the daemon knows several facts
// about each one. Choosing which of them belong on the line is this file's whole subject,
// kept out of the window so it can be read and argued about without a display. Nothing
// here knows about Qt.

#include <string>

#include "../session/daemon.hpp"

namespace kai {

// The id first, because it is what `session list` prints first and what you would type
// back. The name next, because it is what identifies the shell to whoever named it. Then
// what is happening inside, which is the thing actually being chosen between: a shell
// running a build and a shell at a prompt are different places to go, and they look the
// same in a list of names.
inline std::string sessionMenuLabel(const SessionInfo& info) {
    std::string label = std::to_string(info.id) + "  " + info.name;
    if (!info.alive)
        return label + "   leaving";
    label += info.foreground.empty() ? "   at a prompt" : "   " + info.foreground + " running";
    // Said because it is the state nobody chose: a window went away without being asked
    // and the shell carried on. It is not the same as kept — nobody asked for that one —
    // and it is the reason the shell is in this menu at all.
    if (info.clients == 0 && !info.saved)
        return label + "   left behind";
    if (info.saved)
        return label + "   kept";
    return label + "   in another window";
}

}  // namespace kai
