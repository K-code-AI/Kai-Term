// The config file: what it accepts, what it refuses, and what it says when it
// refuses.
//
// The parser is small enough to read in one sitting, which is the whole reason
// the file is key=value and not something with tables in it — but small is not
// the same as obvious, and the rules people will actually hit are checked here:
// the typo'd key that must be announced rather than skipped, the colour written
// three ways where only two are legal, the environment that has to beat the
// file because the environment is what you are holding right now.
//
// No Qt in the parser, so no Qt here: it runs headless, on a file in a private
// directory, and touches nobody's ~/.config.

#include "../src/app/closeprompt.hpp"
#include "../src/config/config.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
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

// A private directory, so the file under test is the only file there is.
std::string scratch() {
    char tmpl[] = "/tmp/kai-config-test-XXXXXX";
    const char* dir = ::mkdtemp(tmpl);
    return dir != nullptr ? dir : "/tmp";
}

void writeFile(const std::string& path, const std::string& text) {
    std::ofstream out(path);
    out << text;
}

bool mentions(const std::vector<std::string>& problems, std::string_view needle) {
    for (const std::string& line : problems)
        if (line.find(needle) != std::string::npos)
            return true;
    return false;
}

bool isColour(const std::optional<Rgb>& colour, int r, int g, int b) {
    return colour && colour->r == r && colour->g == g && colour->b == b;
}

} // namespace

int main() {
    const std::string dir = scratch();
    const std::string path = dir + "/config";

    // The ordinary case: no file. Not an error, not a complaint, nothing set.
    ::setenv("KAI_TERM_CONFIG", (dir + "/nope").c_str(), 1);
    {
        const Config missing = loadConfig();
        expect("no file is not a problem", missing.problems.empty());
        expect("no file sets nothing", missing.confirm_quit.empty() && !missing.background);
        expect("but the place it looked is known",
               missing.path == dir + "/nope");
    }

    // A file that says everything, spelled the ways people spell it.
    writeFile(path,
              "# my terminal, my colours\n"
              "\n"
              "  confirm_quit = never   # trailing comment, trailing spaces\n"
              "foreground=#DCDCDC\n"
              "background = 1e1e1e\n" // the # is optional
              "cursor = #ff0000\n"
              "selection=#00ff00\n"
              "accent = #0000FF\n");
    ::setenv("KAI_TERM_CONFIG", path.c_str(), 1);
    {
        const Config config = loadConfig();
        expect("a clean file says nothing", config.problems.empty());
        expect("confirm_quit read", config.confirm_quit == "never");
        expect("foreground, with # and uppercase", isColour(config.foreground, 0xDC, 0xDC, 0xDC));
        expect("background, without #", isColour(config.background, 0x1E, 0x1E, 0x1E));
        expect("cursor read", isColour(config.cursor, 255, 0, 0));
        expect("selection read", isColour(config.selection, 0, 255, 0));
        expect("accent, mixed case hex", isColour(config.accent, 0, 0, 255));
    }

    // The settings nobody wrote stay unwritten — which is not the same as
    // being written as the default, because a default can still be overridden
    // by a later layer and a written value cannot.
    writeFile(path, "cursor = #ffffff\n");
    {
        const Config config = loadConfig();
        expect("one setting leaves the others unset",
               isColour(config.cursor, 255, 255, 255) && !config.background &&
                   config.confirm_quit.empty());
    }

    // The typos. Each gets a line naming the line; none of them stops the
    // rest of the file, because losing your colours over a typo'd key is a
    // worse afternoon than being told.
    writeFile(path,
              "confirm_exit = never\n" // the classic: wrong word, right shape
              "this is not a setting\n"
              "background = blue\n" // colour names are not colours
              "cursor = #12\n"
              "selection = #gggggg\n"
              "accent =\n"
              "= orphan\n"
              "confirm_quit = ALWAYS\n");
    {
        const Config config = loadConfig();
        expect("the unknown key is named, not skipped",
               mentions(config.problems, "confirm_exit"));
        expect("a line with no = is announced",
               mentions(config.problems, "expected key = value"));
        expect("a colour name is refused by name", mentions(config.problems, "'blue'"));
        expect("too short is refused", mentions(config.problems, "#12"));
        expect("non-hex is refused", mentions(config.problems, "#gggggg"));
        expect("an empty colour is refused",
               mentions(config.problems, "expected a colour like #rrggbb, got ''"));
        expect("a value with no name is announced", mentions(config.problems, "needs a name"));
        expect("problems say which line of which file",
               mentions(config.problems, path + ":1:"));
        expect("and the one setting that was spelled right still counts",
               config.confirm_quit == "always"); // words are case-free, like the file said
    }

    // The words themselves, and the environment's place above them.
    expect("never is never", confirmPolicy("never") == ConfirmPolicy::Never);
    expect("the file's word goes through the same gate as the environment's",
           confirmPolicy("always") == ConfirmPolicy::Always &&
               confirmPolicy("running") == ConfirmPolicy::Running &&
               confirmPolicy("bogus") == ConfirmPolicy::Running);

    // Where the file lives, in the order people expect.
    ::setenv("KAI_TERM_CONFIG", "/somewhere/else", 1);
    expect("KAI_TERM_CONFIG points straight at the file",
           configFilePath() == "/somewhere/else");
    ::unsetenv("KAI_TERM_CONFIG");
    ::setenv("XDG_CONFIG_HOME", "/xdg", 1);
    expect("otherwise the usual place, with the usual fallback",
           configFilePath() == "/xdg/kai-term/config");
    ::unsetenv("XDG_CONFIG_HOME");
    ::setenv("HOME", "/home/somebody", 1);
    expect("and ~/.config when XDG is not set",
           configFilePath() == "/home/somebody/.config/kai-term/config");

    std::remove(path.c_str());
    ::rmdir(dir.c_str());

    if (failures > 0) {
        std::cerr << failures << " of " << checks << " config checks failed\n";
        return 1;
    }
    std::cout << "PASS: " << checks << " config checks passed\n";
    return 0;
}
