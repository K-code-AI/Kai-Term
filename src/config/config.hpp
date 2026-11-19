#pragma once

// The file a person edits to make the terminal theirs.
//
// Key=value rather than TOML, and deliberately so. The words a person has
// already typed a thousand times — `confirm_quit = never` — are worth more than
// the table syntax they will look up, and matching Old-Term's words is most of
// the work of a migration if one ever matters. A parser small enough to read is
// the other half: this file is all of it, and it has no Qt in it, so the rules
// are tested without a display.
//
// The reading rules are the ones people expect from a dozen other dotfiles:
// blank lines and `#` comments are ignored, one setting per line, whitespace
// around the `=` is free, and the first word wins over nothing at all. An
// unknown key is not silently skipped — a person who typed `confirm_exit`
// should be told, because the alternative is an afternoon of wondering why the
// setting is not taking effect — but it is not fatal either; the rest of the
// file still applies.
//
// Precedence: the environment beats the file. The environment is what you type
// once to try something (`KAI_CONFIRM_QUIT=never ./kai-term`); the file is what
// you decide to live with. A living setting should not outrank the thing
// someone is holding in their hand right now.

#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

namespace kai {

inline std::string trimSpace(const std::string& text) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return "";
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

inline std::string lowerWord(std::string word) {
    for (char& c : word)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return word;
}

// `#rrggbb`, or the same without the `#`; nothing else, and no colour names.
// Names are a dictionary nobody can spell consistently across terminals, and a
// wrong colour quietly painting the wrong thing is worse than a refusal that
// says which line and why.
inline bool colourFromText(const std::string& text, std::array<int, 3>& out) {
    std::string hex = text;
    if (!hex.empty() && hex[0] == '#')
        hex = hex.substr(1);
    if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return false;
    const auto part = [&hex](std::size_t at) {
        return std::stoi(hex.substr(at, 2), nullptr, 16);
    };
    out = {part(0), part(2), part(4)};
    return true;
}

// An RGB triple with no Qt anywhere near it; the widget lifts these into its
// own colours when it applies the theme.
struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
};

inline std::optional<Rgb> colourSetting(const std::optional<std::array<int, 3>>& value) {
    if (!value)
        return std::nullopt;
    return Rgb{(*value)[0], (*value)[1], (*value)[2]};
}

struct Config {
    // Empty means the file said nothing, which is not the same as a value of
    // "never": a setting nobody wrote leaves the built-in answer standing.
    std::string confirm_quit;
    std::optional<Rgb> foreground;
    std::optional<Rgb> background;
    std::optional<Rgb> cursor;
    std::optional<Rgb> selection;
    std::optional<Rgb> accent;
    // One line per oddity, each naming the file and line, for stderr at
    // startup. A file nobody commented on is a file that said only things it
    // understood.
    std::vector<std::string> problems;
    std::string path; // where it was read from; empty when there was no file
};

// KAI_TERM_CONFIG says which file, which is how a test gets a config without
// touching anybody's home directory, and how a person keeps a second setup for
// a second machine. Otherwise the usual place: $XDG_CONFIG_HOME, falling back
// to ~/.config, then the file every other program with a config has been using
// since then.
inline std::string configFilePath() {
    if (const char* override_path = std::getenv("KAI_TERM_CONFIG"))
        return override_path;
    std::string root;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        root = xdg;
    else if (const char* home = std::getenv("HOME"); home && *home)
        root = std::string(home) + "/.config";
    if (root.empty())
        return "";
    return root + "/kai-term/config";
}

inline Config loadConfig() {
    Config config;
    config.path = configFilePath();
    if (config.path.empty())
        return config;
    std::ifstream file(config.path);
    if (!file)
        return config; // no file is the ordinary case, not a complaint

    auto note = [&config](int line, const std::string& what) {
        config.problems.push_back(config.path + ":" + std::to_string(line) + ": " + what);
    };

    std::string line;
    int line_no = 0;
    while (std::getline(file, line)) {
        ++line_no;
        const std::size_t whole = line.find_first_not_of(" \t");
        if (whole == std::string::npos)
            continue;
        if (line[whole] == '#')
            continue; // a line that is nothing but a comment
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            note(line_no, "expected key = value");
            continue;
        }
        const std::string key = lowerWord(trimSpace(line.substr(0, eq)));
        std::string value = trimSpace(line.substr(eq + 1));
        if (key.empty()) {
            note(line_no, "a setting needs a name");
            continue;
        }
        // A value that begins with `#` is a colour, not a comment — that is
        // how people write colours — so its token is taken before any comment
        // is stripped and the trailing comment falls away after it. A value
        // that merely has a `#` later is a value with a comment on the end.
        // Every other dotfile makes this choice somewhere; this one says so.
        std::string remainder;
        if (!value.empty() && value[0] == '#') {
            const std::size_t space = value.find_first_of(" \t");
            remainder = space == std::string::npos ? "" : trimSpace(value.substr(space));
            value = space == std::string::npos ? value : trimSpace(value.substr(0, space));
            if (!remainder.empty() && remainder[0] != '#')
                note(line_no, "expected a comment after the colour, got '" + remainder + "'");
        } else {
            const std::size_t comment = value.find('#');
            if (comment != std::string::npos)
                value = trimSpace(value.substr(0, comment));
        }
        auto colour = [&note, &line_no, &value](std::optional<Rgb>& into) {
            std::array<int, 3> rgb{};
            if (value.empty() || !colourFromText(value, rgb)) {
                note(line_no, "expected a colour like #rrggbb, got '" + value + "'");
                return;
            }
            into = Rgb{rgb[0], rgb[1], rgb[2]};
        };
        if (key == "confirm_quit") {
            if (value.empty()) {
                note(line_no, "confirm_quit needs never, running or always");
                continue;
            }
            config.confirm_quit = lowerWord(value);
        } else if (key == "foreground") {
            colour(config.foreground);
        } else if (key == "background") {
            colour(config.background);
        } else if (key == "cursor") {
            colour(config.cursor);
        } else if (key == "selection") {
            colour(config.selection);
        } else if (key == "accent") {
            colour(config.accent);
        } else {
            note(line_no, "no setting is called '" + key + "'");
        }
    }
    return config;
}

} // namespace kai
