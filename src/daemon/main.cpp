// kai-daemon: the process that keeps sessions alive when no window is looking.
//
// Normally nothing needs to be started by hand — `kai-term session new` starts one if
// there is none — but a person debugging the protocol, or wanting to see what the
// daemon is saying as it happens, runs this in a terminal instead.

#include "../session/cli.hpp"

#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    const std::string_view first = argc > 1 ? std::string_view(argv[1]) : std::string_view();
    if (first == "--help" || first == "-h") {
        std::cout << "Usage: kai-daemon [--socket]\n"
                     "Owns terminal sessions so they outlive the window that opened them.\n"
                     "  --socket  print where the daemon listens, and exit\n";
        return 0;
    }
    if (first == "--socket") {
        std::cout << kai::daemonSocketPath() << "\n";
        return 0;
    }

    std::string error;
    std::cout << "kai-daemon: listening on " << kai::daemonSocketPath() << "\n"
              << "kai-daemon: Ctrl-C, or `kai-term session shutdown`, to stop\n"
              << std::flush;
    const int status = kai::serveDaemonForeground(error);
    if (!error.empty())
        std::cerr << "kai-daemon: " << error << "\n";
    return status;
}
