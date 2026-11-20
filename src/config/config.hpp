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
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <sys/stat.h>
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

    // Two colours are the same colour when all three bytes agree. Worth saying in
    // one place: the question "did this colour move?" comes up wherever a picker
    // or a mixer decides whether anything is worth writing to a file, and asking
    // it a channel at a time is how one channel gets left out.
    friend bool operator==(const Rgb&, const Rgb&) = default;
};

// The shape the cursor has until a program asks for a different one. Three
// answers, spelled the way every terminal's config spells them, because a
// setting with three answers should not accept a fourth: `cursor_style = block`
// is the setting, and `cursor_style = blok` is a line that says what it wanted.
enum class CursorStyle { Block, Bar, Underline };

inline bool cursorStyleFromText(const std::string& text, CursorStyle& out) {
    const std::string word = lowerWord(text);
    if (word == "block")
        out = CursorStyle::Block;
    else if (word == "bar")
        out = CursorStyle::Bar;
    else if (word == "underline")
        out = CursorStyle::Underline;
    else
        return false;
    return true;
}

inline const char* cursorStyleText(CursorStyle style) {
    switch (style) {
        case CursorStyle::Block: return "block";
        case CursorStyle::Bar: return "bar";
        case CursorStyle::Underline: return "underline";
    }
    return "underline";
}

inline std::optional<Rgb> colourSetting(const std::optional<std::array<int, 3>>& value) {
    if (!value)
        return std::nullopt;
    return Rgb{(*value)[0], (*value)[1], (*value)[2]};
}

// A whole number in range, or nothing. Every character has to be a digit —
// "14px" is a font size somebody meant, and it gets the same one-line
// complaint as any other value the program cannot honour, rather than a
// silent read of the 14 and a shrug.
inline bool numberFromText(const std::string& text, int low, int high, int& out) {
    if (text.empty())
        return false;
    long long value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + (c - '0');
        if (value > high)
            return false; // stop before a billion columns overflow anything
    }
    if (value < low || value > high)
        return false;
    out = static_cast<int>(value);
    return true;
}

// A fraction in range, spelled the ways people spell fractions: 0.8, .8, 1.
// Signed or trailing junk is refused for the same reason "14px" is.
inline bool fractionFromText(const std::string& text, double low, double high, double& out) {
    if (text.empty())
        return false;
    const char* begin = text.c_str();
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end != begin + text.size())
        return false; // something was left over
    if (!(value >= low && value <= high))
        return false;
    out = value;
    return true;
}

struct Config {
    // Empty means the files said nothing, which is not the same as a value of
    // "never": a setting nobody wrote leaves the built-in answer standing.
    std::string confirm_quit;
    // How the window presents itself, when the files have opinions: a font
    // size in pixels, an opening grid, and how solid the window is against
    // whatever is behind it.
    std::optional<int> font_size;
    std::optional<int> cols;
    std::optional<int> rows;
    // How solid the window is against whatever is behind it, in two halves: the
    // ground the cells sit on, and the ink drawn over it. Separate because the
    // reason to see through a terminal is to see the desktop behind the
    // *background*, and the reason not to is that text you cannot read is worse
    // than a desktop you cannot see. 0 is gone, 1 is solid.
    //
    // `background_opacity` also answers to the older single name `opacity`, which
    // is what it always meant — the file a person already wrote keeps working.
    std::optional<double> background_opacity;
    std::optional<double> foreground_opacity;
    // How many lines a screen remembers above its own top edge. A terminal with
    // no scrollback is a terminal that loses the error you scrolled up to read,
    // and one with no limit is a slow leak, so the number is a decision rather
    // than a constant.
    std::optional<int> scrollback;
    // The air between the grid and the edge of the window, in pixels. Under a
    // compositor that rounds its corners this is the difference between glyphs
    // inside the glass and glyphs clipped by it.
    std::optional<double> padding;
    // The cursor's shape before a program picks its own with DECSCUSR — the
    // default an application may override, not a rule it must obey.
    std::optional<CursorStyle> cursor_style;
    std::optional<Rgb> foreground;
    std::optional<Rgb> background;
    std::optional<Rgb> cursor;
    std::optional<Rgb> selection;
    std::optional<Rgb> accent;
    // The sixteen ANSI colours, written `color0` through `color15` the way
    // every terminal program that has a config has spelled them. A person
    // who has never heard the name "SGR" can still say what `red` means to
    // them, and the picker edits these same sixteen by their plain names.
    std::array<std::optional<Rgb>, 16> palette;
    // One line per oddity, each naming its file and line, for stderr at
    // startup. Files nobody commented on are files that said only things they
    // understood.
    std::vector<std::string> problems;
    std::string settings_path; // where behaviour lives: kai-term.conf
    std::string theme_path;    // where colours live: theme.conf
};

// Where the person's own files go: $XDG_CONFIG_HOME if set, else ~/.config —
// the place every other program with a config has been using since then.
// Empty only when the machine cannot say where home is, which is a container
// quirk rather than a desktop.
inline std::string configRoot() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return xdg;
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/.config";
    return "";
}

// `--config-dir DIR` names a directory to read BOTH files from, directly
// (`DIR/kai-term.conf` and `DIR/theme.conf`). It is the escape hatch Kaida asked
// for: the saved theme supersedes everything by default, and the one way to run
// against a different set of settings is to point the terminal at another
// directory when you launch it. Set once at startup by main.cpp, before anything
// reads a config. An explicit KAI_TERM_CONFIG / KAI_TERM_THEME still wins for the
// single file it names, so tests and one-off single-file overrides keep working.
inline std::string& configDirOverride() {
    static std::string dir;
    return dir;
}
inline void setConfigDir(const std::string& dir) { configDirOverride() = dir; }

// Two files, because two kinds of thing get set. `kai-term.conf` is how the
// program behaves; `theme.conf` is how it looks. The split is not tidiness
// for its own sake: it is what lets the theme picker save colours without
// ever rewriting the file that holds somebody's other decisions — the two
// writers never touch each other's file.
//
// The layering these names aim at is the one every packaged program uses:
// /etc/kai-term/<same names> holding system defaults, read first and
// overridden by the user's files. That layer waits until there is an
// installer with a place to put it (Kaida, 2026-11-19: "later, when we do a
// way to install it"); the names, the order and the per-file parsing are
// chosen now so adding it is a call to parseConfigFile, not a rethink.
inline std::string settingsFilePath() {
    if (const char* override_path = std::getenv("KAI_TERM_CONFIG"))
        return override_path;
    if (!configDirOverride().empty())
        return configDirOverride() + "/kai-term.conf";
    const std::string root = configRoot();
    return root.empty() ? "" : root + "/kai-term/kai-term.conf";
}

// KAI_TERM_THEME points at a different theme file, the same way
// KAI_TERM_CONFIG points at a different settings file: tests get a theme
// without touching anybody's home directory, and a person can keep a second
// palette for a second machine.
inline std::string themeFilePath() {
    if (const char* override_path = std::getenv("KAI_TERM_THEME"))
        return override_path;
    if (!configDirOverride().empty())
        return configDirOverride() + "/theme.conf";
    const std::string root = configRoot();
    return root.empty() ? "" : root + "/kai-term/theme.conf";
}

// Read one file into the config. Both files speak the same key=value, and
// the reader is deliberately lenient about which key appears in which file —
// a colour written in kai-term.conf still works — because refusing to read
// what somebody clearly typed is a worse service than the untidiness. What
// the files say later overrides what they said earlier, which is why
// `loadConfig` reads the theme last: it is the file the picker writes, and
// the thing you picked most recently is the thing you meant.
inline void parseConfigFile(const std::string& path, Config& config) {
    if (path.empty())
        return;
    std::ifstream file(path);
    if (!file)
        return; // no file is the ordinary case, not a complaint

    auto note = [&path, &config](int line, const std::string& what) {
        config.problems.push_back(path + ":" + std::to_string(line) + ": " + what);
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
        } else if (key == "font_size") {
            int got = 0;
            if (!numberFromText(value, 6, 72, got))
                note(line_no, "font_size wants a pixel size from 6 to 72, got '" + value + "'");
            else
                config.font_size = got;
        } else if (key == "cols") {
            int got = 0;
            if (!numberFromText(value, 2, 1000, got))
                note(line_no, "cols wants a number from 2 to 1000, got '" + value + "'");
            else
                config.cols = got;
        } else if (key == "rows") {
            int got = 0;
            if (!numberFromText(value, 2, 1000, got))
                note(line_no, "rows wants a number from 2 to 1000, got '" + value + "'");
            else
                config.rows = got;
        } else if (key == "background_opacity" || key == "opacity") {
            // `opacity` is the older name for the same thing and still reads, so a
            // file written before the two halves were split keeps working.
            double got = 0.0;
            if (!fractionFromText(value, 0.0, 1.0, got))
                note(line_no, key + " wants a number from 0 to 1, got '" + value + "'");
            else
                config.background_opacity = got;
        } else if (key == "foreground_opacity") {
            double got = 0.0;
            if (!fractionFromText(value, 0.0, 1.0, got))
                note(line_no, "foreground_opacity wants a number from 0 to 1, got '" + value + "'");
            else
                config.foreground_opacity = got;
        } else if (key == "scrollback") {
            int got = 0;
            if (!numberFromText(value, 0, 20000, got))
                note(line_no, "scrollback wants a number from 0 to 20000, got '" + value + "'");
            else
                config.scrollback = got;
        } else if (key == "padding") {
            double got = 0.0;
            if (!fractionFromText(value, 0.0, 64.0, got))
                note(line_no, "padding wants a number from 0 to 64, got '" + value + "'");
            else
                config.padding = got;
        } else if (key == "cursor_style") {
            CursorStyle got = CursorStyle::Underline;
            if (!cursorStyleFromText(value, got))
                note(line_no, "cursor_style wants block, bar or underline, got '" + value + "'");
            else
                config.cursor_style = got;
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
        } else if (key.rfind("color", 0) == 0 && key.size() > 5) {
            // color0 through color15, and nothing beyond: `color16` is a
            // number out of range, which is a different sentence from
            // "no such setting", but the same fifteen-second fix.
            int index = -1;
            if (!numberFromText(key.substr(5), 0, 15, index))
                note(line_no, "no setting is called '" + key + "'");
            else
                colour(config.palette[static_cast<std::size_t>(index)]);
        } else {
            note(line_no, "no setting is called '" + key + "'");
        }
    }
}

// Read the person's settings, then the person's theme. The theme is read
// last so it wins where a colour somehow appears in both: the file the
// picker writes is the more recent intention, and the most recent intention
// is the one to keep.
inline Config loadConfig() {
    Config config;
    config.settings_path = settingsFilePath();
    config.theme_path = themeFilePath();
    parseConfigFile(config.settings_path, config);
    parseConfigFile(config.theme_path, config);
    return config;
}

// The way a colour is written, so the picker and any other writer spell it the
// same way the reader wants: lowercase, six digits, leading `#`.
inline std::string colourText(const Rgb& colour) {
    const auto nibble = [](int v) -> char {
        v &= 0xf;
        // Below ten a digit is spelled as itself; ten and up borrow the
        // first six letters, so the letter is `a` plus how far past ten the
        // value is — fifteen is `a`+5=`f`, not `a`+15, which spells `p`.
        return v < 10 ? static_cast<char>('0' + v) : static_cast<char>('a' + (v - 10));
    };
    const int parts[3] = {colour.r, colour.g, colour.b};
    std::string out = "#";
    for (int part : parts) {
        out.push_back(nibble(part >> 4));
        out.push_back(nibble(part));
    }
    return out;
}

// The comment sitting at the end of a value line, if there is one. A value
// that begins with `#` is a colour and not a comment, so the hunt for a
// comment starts after the colour token — the same distinction the reader
// makes, made here so editing a value does not quietly eat the note somebody
// left beside it.
inline std::string trailingComment(const std::string& after_equals) {
    std::size_t i = after_equals.find_first_not_of(" \t");
    if (i == std::string::npos)
        return "";
    if (after_equals[i] == '#') {
        const std::size_t space = after_equals.find_first_of(" \t", i);
        if (space == std::string::npos)
            return "";
        i = space;
    }
    const std::size_t hash = after_equals.find('#', i);
    return hash == std::string::npos ? "" : after_equals.substr(hash);
}

// Make the directory a config file lives in, and its parents if those are
// missing too — the first save from a fresh machine has to create
// ~/.config/kai-term, not just the file inside it.
inline bool ensureParentDirectory(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0)
        return true;
    const std::string parent = path.substr(0, slash);
    std::string built;
    for (std::size_t i = 0; i <= parent.size(); ++i) {
        if (i != parent.size() && parent[i] != '/')
            continue;
        built = parent.substr(0, i);
        if (built.empty() || built == "/")
            continue;
        if (::mkdir(built.c_str(), 0755) != 0 && errno != EEXIST)
            return false;
    }
    return true;
}

// Write settings back to the file a person edited by hand, which means
// respecting everything this function was not asked to change. Comment lines
// and settings it does not touch are copied byte for byte; a line whose key it
// is setting keeps its leading text and its trailing comment and changes only
// the value, so the alignment somebody lined up by hand survives; keys that
// were not in the file are added at the end. A file that did not exist gets a
// short header so the next person to open it knows what it is. Everything is
// written to a sibling temp file and renamed over the original, because a save
// interrupted halfway should leave the old config standing, not a half-written
// one that the reader will then complain about at startup.
inline bool saveConfigValues(const std::string& path,
                             const std::vector<std::pair<std::string, std::string>>& values,
                             std::string& error,
                             const std::string& new_file_header =
                                 "# kai-term settings — edit here or from inside the window\n") {
    if (path.empty()) {
        error = "no config file path is set";
        return false;
    }
    if (!ensureParentDirectory(path)) {
        error = "could not create the directory for " + path;
        return false;
    }

    std::vector<std::string> lines;
    bool had_file = false;
    {
        std::ifstream in(path);
        if (in) {
            had_file = true;
            std::string line;
            while (std::getline(in, line))
                lines.push_back(line);
        }
    }

    std::vector<bool> placed(values.size(), false);
    for (std::string& line : lines) {
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos && line.find_first_not_of(" \t") >= hash)
            continue; // a comment line, not a setting
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = lowerWord(trimSpace(line.substr(0, eq)));
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (placed[i] || values[i].first != key)
                continue;
            const std::string comment = trailingComment(line.substr(eq + 1));
            line = line.substr(0, eq + 1) + " " + values[i].second;
            if (!comment.empty())
                line += " " + comment;
            placed[i] = true;
            break;
        }
    }

    std::string body;
    if (!had_file && lines.empty())
        body = new_file_header;
    for (const std::string& line : lines)
        body += line + "\n";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!placed[i])
            body += values[i].first + " = " + values[i].second + "\n";
    }

    const std::string temp = path + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary);
        if (!out) {
            error = "could not write " + temp;
            return false;
        }
        out << body;
        out.flush();
        if (!out) {
            error = "could not finish writing " + temp;
            out.close();
            std::remove(temp.c_str());
            return false;
        }
    }
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        error = "could not replace " + path;
        std::remove(temp.c_str());
        return false;
    }
    return true;
}

} // namespace kai
