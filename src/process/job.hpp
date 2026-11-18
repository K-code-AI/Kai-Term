#pragma once

// What is running in the foreground of a terminal.
//
// A window that is about to close needs to know whether it is throwing away a shell
// sitting at a prompt or a build that has been going for twenty minutes. Counting
// keystrokes would be a guess; the kernel already knows which process group owns the
// terminal, so the question has a real answer.

#include <string>
#include <fstream>
#include <termios.h>
#include <unistd.h>

namespace kai {

// The command in the foreground of the terminal on the other end of `master`, or empty
// when nothing but the shell itself is out there.
//
// The process group's leader is what gets named, which for a pipeline is the first
// command in it. Naming one of three commands is still worth acting on; what matters is
// that a shell at a prompt is named nothing at all, so a window does not ask permission
// to close when there is nothing to lose.
// A command name is shown to a person inside a sentence, so it has to be a thing that can
// be shown. A control character would move the cursor of the very window that is asking
// the question, or draw a control code where a name should be; nothing real puts one in an
// executable's name, so an unusable one is refused rather than escaped. Bytes >= 0x80 pass
// through: a command may well be named in UTF-8.
//
// Refusing costs something, and it is worth saying what: a process whose name cannot be
// drawn is closed without a question. The alternative — a garbled dialog — is worse, and
// the name comes from the executable, not from anything a program gets to choose.
inline std::string printableCommand(const std::string& raw) {
    for (const char c : raw) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7f)
            return {};
    }
    return raw;
}

inline std::string foregroundJob(int master, pid_t shell) {
    if (master < 0)
        return {};
    const pid_t group = ::tcgetpgrp(master);
    if (group <= 0 || group == shell)
        return {};
    std::ifstream comm("/proc/" + std::to_string(static_cast<long long>(group)) + "/comm");
    std::string name;
    if (!std::getline(comm, name))
        return {};
    return printableCommand(name);
}

} // namespace kai
