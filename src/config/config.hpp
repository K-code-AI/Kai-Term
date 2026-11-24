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

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
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
    // Whether Ctrl+K, while it waits for its one key, lists the keys it will take along
    // the bottom. On for a person still learning; off for one who has learned them.
    std::string keybind_hints;
    // Which colours to wear, as written: empty or `Default.conf` for the file the program
    // has always used, a bare name for a file in `themes`, and anything with a slash in it
    // for that path. Only ever read from the settings file — a theme is colours, and the
    // settings file is where the decisions live.
    std::string theme;
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
    // The cursor's shape. Named in the file it is the window's and every DECSCUSR the
    // session writes answers to it — the same authority the colours hold, so a shape
    // chosen here is not lost when a session is attached. Left out, a program picks its
    // own shape as the escape was designed to.
    std::optional<CursorStyle> cursor_style;
    // How long each half of the cursor's blink lasts, in milliseconds: lit for that
    // long, dark for the same again, and 0 meaning it stays lit. A cursor that never
    // blinks is easy to lose in a screen full of text and one that blinks quickly is a
    // metronome behind the words you are reading, so it is a number rather than a
    // switch — and 0 is a real answer rather than an off state.
    // (Kaida, 2026-11-21: "can we add cursor blink, with customizable blink speed
    // ( 0 being no blinking)".)
    std::optional<int> cursor_blink;
    std::optional<Rgb> foreground;
    std::optional<Rgb> background;
    std::optional<Rgb> cursor;
    // The highlight under a dragged selection, and the ink drawn on top of it. The file
    // spells them the way kitty does, so a kitty theme is read as it stands; `selection`
    // still reads as the older name for the first of them, the same way `opacity` still
    // reads for `background_opacity`.
    std::optional<Rgb> selection_background;
    std::optional<Rgb> selection_foreground;
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
// Which file `-c FILE` named, when it named one. Kept apart from the directory
// because the two say different things: the directory says where both files live,
// the file says which one holds the settings and leaves the colours to the
// `theme.conf` that lives beside it.
inline std::string& configFileOverride() {
    static std::string file;
    return file;
}
inline void setConfigDir(const std::string& dir) {
    // The directory is a complete answer on its own, so any file named before it is
    // forgotten. Two setters that each mean "the configuration lives here" must not
    // leave a half of the earlier answer standing.
    configFileOverride().clear();
    configDirOverride() = dir;
}

// `-c FILE` / `--config FILE`: the settings come from FILE, and the colours from the
// `theme.conf` in the same directory — one argument for one decision, which is "run
// against this configuration". A directory is accepted for the same reason a shell
// completes one: the question is really where the files are, and the directory answer
// is the same answer with both files in their usual names.
// (Kaida, 2026-11-20: "we need a way to select a different config file to read from on
// launch ... instead of a Dir because theme is now defined in the config".)
inline void setConfigFile(const std::string& path) {
    if (path.empty()) {
        // The way to say "no file", which is also the way to say "no directory":
        // the two were set together, and an empty directory has always meant the
        // usual place.
        configFileOverride().clear();
        configDirOverride().clear();
        return;
    }
    struct stat status {};
    if (::stat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode)) {
        setConfigDir(path);
        return;
    }
    configFileOverride() = path;
    const std::size_t slash = path.find_last_of('/');
    // A bare name means the file sits where the program was started from, so its
    // theme sits there too. Leaving the directory unset would send the colours off
    // to the home directory while the settings stayed here, which is two
    // configurations answering one question.
    configDirOverride() = slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

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
    if (!configFileOverride().empty())
        return configFileOverride();
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

// The name that means "the colours that are already there". Leaving `theme` out and
// writing `theme = Default.conf` are the same sentence, and the file it names is the one
// that has always held the colours — `theme.conf`, which is also what the colour picker
// writes. Nothing moves, and nothing written before this existed needs converting.
// (Kaida, 2026-11-20: "We add a 'Theme = Default.conf' to our config and a sub-directory
// 'themes' in ~/.config/kai-term where they get sourced from by default unless a full
// path for the theme is given".)
inline constexpr const char* kDefaultThemeName = "Default.conf";

// Where the other themes live: a `themes` directory beside the two files, wherever those
// happen to be. A directory named by `-c` brings its own themes with it, because the point
// of pointing somewhere is that everything follows.
inline std::string themesDirectory() {
    if (!configDirOverride().empty())
        return configDirOverride() + "/themes";
    const std::string root = configRoot();
    return root.empty() ? std::string() : root + "/kai-term/themes";
}

// Which file a theme name means. Empty and `Default.conf` both mean the colours the
// program has always used; a name with a slash in it is taken for the path it is —
// "unless a full path for the theme is given" — and anything else is a file in the themes
// directory. KAI_TERM_THEME outranks the lot, because the environment is the thing you are
// holding right now and it has always won over a file.
inline std::string resolveThemePath(const std::string& name) {
    if (const char* override_path = std::getenv("KAI_TERM_THEME"))
        return override_path;
    if (name.empty() || name == kDefaultThemeName)
        return themeFilePath();
    if (name.find('/') != std::string::npos)
        return name;
    const std::string dir = themesDirectory();
    return dir.empty() ? name : dir + "/" + name;
}

// The themes there are to choose from: the default first, then every `.conf` in the themes
// directory in the order a person would read them. A file inside `themes` that is itself
// called `Default.conf` is not offered, because that name already answers to the colours
// the program carries — two rows with one name would be a question, not a choice.
inline std::vector<std::string> listThemes() {
    std::vector<std::string> names{kDefaultThemeName};
    std::vector<std::string> found;
    const std::string dir = themesDirectory();
    if (!dir.empty()) {
        if (DIR* handle = ::opendir(dir.c_str())) {
            while (const dirent* entry = ::readdir(handle)) {
                const std::string name = entry->d_name;
                if (name.size() <= 5 || name.compare(name.size() - 5, 5, ".conf") != 0)
                    continue;
                if (name == kDefaultThemeName)
                    continue;
                found.push_back(name);
            }
            ::closedir(handle);
        }
    }
    std::sort(found.begin(), found.end());
    names.insert(names.end(), found.begin(), found.end());
    return names;
}

// What a theme is called when the config did not say: the default has a name even when
// nobody wrote it down, because the row that shows it has to say something.
inline std::string themeName(const std::string& written) {
    return written.empty() ? std::string(kDefaultThemeName) : written;
}

// A name typed into the picker, turned into a file name. A theme is a `.conf` file, and
// nobody should have to type the extension they can see on every other theme in the list;
// a name that already has a dot in it is left alone, because typing `odd.theme` means
// that file and not `odd.theme.conf`. An empty name is not a file, so it means the one
// that is on.
inline std::string themeFileName(const std::string& typed, const std::string& loaded) {
    if (typed.empty())
        return themeName(loaded);
    if (typed.find('.') != std::string::npos)
        return typed;
    return typed + ".conf";
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
        // Two spellings of the same sentence: `key = value`, which is what this program has
        // always written, and `key value`, which is what kitty writes. A theme file from
        // kitty is a pile of colours somebody already chose, and it should be droppable into
        // `themes/` and read as it stands, dots and all.
        // (Kaida, 2026-11-20: "change the theme config to not require an = between the name
        // and the value, so that kitty themes can be used as is simplifying transition for
        // some users".)
        //
        // An `=` still wins when a line has one, so a value with a space in it keeps its
        // meaning in both spellings, and a file written by this program is read the same way
        // it always was.
        std::string key_text;
        std::string value;
        if (const std::size_t eq = line.find('='); eq != std::string::npos) {
            key_text = trimSpace(line.substr(0, eq));
            value = trimSpace(line.substr(eq + 1));
        } else {
            const std::size_t space = line.find_first_of(" \t", whole);
            if (space == std::string::npos) {
                note(line_no, "expected key = value, or key and value separated by a space");
                continue;
            }
            key_text = trimSpace(line.substr(whole, space - whole));
            value = trimSpace(line.substr(space + 1));
        }
        const std::string key = lowerWord(key_text);
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
        } else if (key == "keybind_hints") {
            if (value.empty()) {
                note(line_no, "keybind_hints needs on or off");
                continue;
            }
            config.keybind_hints = lowerWord(value);
        } else if (key == "theme") {
            // Kept exactly as written, slashes and case and all: it is a file name, and a
            // file system that distinguishes case is not this parser's business.
            if (value.empty()) {
                note(line_no, "theme needs a file name like Default.conf or midnight.conf");
                continue;
            }
            config.theme = value;
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
        } else if (key == "cursor_blink") {
            int got = 0;
            if (!numberFromText(value, 0, 5000, got))
                note(line_no, "cursor_blink wants a number from 0 to 5000, got '" + value + "'");
            else
                config.cursor_blink = got;
        } else if (key == "foreground") {
            colour(config.foreground);
        } else if (key == "background") {
            colour(config.background);
        } else if (key == "cursor") {
            colour(config.cursor);
        } else if (key == "selection_background" || key == "selection") {
            // `selection` is what this file used to call it; the name the window answers to
            // is kitty's, so a theme from over there needs no editing.
            colour(config.selection_background);
        } else if (key == "selection_foreground") {
            // The text on a selected cell. It used to be a constant in the painting — a
            // light grey nobody could change — and a theme that sets it is a theme saying
            // the grey was wrong for it.
            colour(config.selection_foreground);
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

// Read the person's settings, then the colours those settings name. The theme is read
// last so it wins where a colour somehow appears in both: the file the picker writes is
// the more recent intention, and the most recent intention is the one to keep.
//
// The settings are read first for a second reason as well: `theme` lives in them, so
// which colour file exists at all is not known until they have been read. Everything
// still resolves relative to wherever the configuration was pointed, so `-c` moves the
// themes along with the files.
inline Config loadConfig() {
    Config config;
    config.settings_path = settingsFilePath();
    config.theme_path = themeFilePath();
    parseConfigFile(config.settings_path, config);
    config.theme_path = resolveThemePath(config.theme);
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
// were not in the file are added at the end, in the column the rest of the file
// already writes at. A file that did not exist gets a short header so the next
// person to open it knows what it is. Everything is written to a sibling temp
// file and renamed over the original, because a save interrupted halfway should
// leave the old config standing, not a half-written one that the reader will
// then complain about at startup.
//
// A line names its setting in either of two shapes: `key = value`, which is what
// the settings file is written in, and `key value`, which is what a theme file is
// written in. Both are read, because a file somebody already wrote is not wrong
// and because a kitty theme arrives without the sign; `equals_sign` is the word
// for which shape goes back out. It is true for `kai-term.conf` and false for a
// theme, whose `=` form was deprecated on 2026-11-21.
//
// Before that word existed, this function recognised a setting only where the
// line contained `=`. A theme written without one therefore matched nothing,
// nothing was marked as written, and every colour was appended again in the old
// shape — a file with each colour in it twice, the second copy winning, which is
// exactly what the colour editor did to Kaida's themes when he saved over one.
// (Kaida, 2026-11-21: "only the =less layout should be used is what i meant, we
// a re deprecating the value = xx layout".)
inline bool saveConfigValues(const std::string& path,
                             const std::vector<std::pair<std::string, std::string>>& values,
                             std::string& error,
                             const std::string& new_file_header =
                                 "# kai-term settings — edit here or from inside the window\n",
                             bool equals_sign = true) {
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

    // Where a line's key stops and its value starts, in either shape. A `#` sitting at
    // the value's position is a colour rather than a comment — the same distinction the
    // reader makes, and the one that has to be made here too or every colour line in a
    // theme would read as a note somebody left.
    struct Setting {
        std::size_t key_end = 0;  // just past the key's last letter
        std::size_t value_at = 0; // the value's first character
        bool had_equals = false;
    };
    const auto find_setting = [](const std::string& line, Setting& out) {
        const std::size_t key_start = line.find_first_not_of(" \t");
        if (key_start == std::string::npos || line[key_start] == '#')
            return false; // blank, or a note somebody left
        std::size_t i = key_start;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '=')
            ++i;
        out.key_end = i;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            ++i;
        if (i < line.size() && line[i] == '=') {
            out.had_equals = true;
            ++i;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                ++i;
        }
        if (i >= line.size())
            return false; // a key with nothing after it sets nothing
        out.value_at = i;
        return true;
    };

    std::vector<bool> placed(values.size(), false);
    std::size_t column = 0; // the column this file writes its values at
    for (std::string& line : lines) {
        Setting at;
        if (!find_setting(line, at))
            continue;
        const std::string key = lowerWord(trimSpace(line.substr(0, at.key_end)));
        if (at.value_at > column)
            column = at.value_at;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (placed[i] || values[i].first != key)
                continue;
            const std::string comment = trailingComment(line.substr(at.value_at));
            // Whatever separates a key from its value is kept, spaces and all, so the
            // columns somebody lined up by hand stay lined up. Only the sign comes and
            // goes, with the layout this file is written in.
            std::string sep = line.substr(at.key_end, at.value_at - at.key_end);
            if (at.had_equals && !equals_sign) {
                std::string kept;
                for (const char c : sep) {
                    if (c != '=')
                        kept += c;
                }
                sep = kept.empty() ? " " : kept;
            } else if (!at.had_equals && equals_sign) {
                sep = " = ";
            } else if (sep.empty()) {
                sep = " ";
            }
            line = line.substr(0, at.key_end) + sep + values[i].second;
            if (!comment.empty())
                line += " " + comment;
            placed[i] = true;
            break;
        }
    }

    // A colour the theme has never held joins at the column the rest of the file uses, so
    // it lines up with its neighbours instead of arriving at a column of its own. A theme
    // with no lines yet — one being written for the first time — is lined up on the longest
    // colour being written. The settings file keeps the plainer `key = value` for a line it
    // is adding: that file is mostly somebody's own notes and comments, and a setting added
    // at one space is one the eye finds in a diff.
    if (!equals_sign && column == 0) {
        for (const std::pair<std::string, std::string>& v : values)
            column = std::max(column, v.first.size() + 1);
    }

    std::string body;
    if (!had_file && lines.empty())
        body = new_file_header;
    for (const std::string& line : lines)
        body += line + "\n";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (placed[i])
            continue;
        const std::string& key = values[i].first;
        std::string line = key;
        if (equals_sign) {
            line += " = " + values[i].second;
        } else {
            const std::size_t pad = column > key.size() + 1 ? column - key.size() : 1;
            line.append(pad, ' ');
            line += values[i].second;
        }
        body += line + "\n";
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
