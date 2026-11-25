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

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <sys/stat.h>
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
    // Isolate both files from anybody's real ~/.config for the whole test; the
    // blocks below point them at scratch files as they need them.
    ::setenv("KAI_TERM_CONFIG", (dir + "/nope").c_str(), 1);
    ::setenv("KAI_TERM_THEME", (dir + "/nope-theme").c_str(), 1);

    // The ordinary case: no files. Not an error, not a complaint, nothing set.
    {
        const Config missing = loadConfig();
        expect("no file is not a problem", missing.problems.empty());
        expect("no file sets nothing", missing.confirm_quit.empty() && !missing.background);
        expect("but both places it looked are known",
               missing.settings_path == dir + "/nope" &&
                   missing.theme_path == dir + "/nope-theme");
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
        expect("selection read", isColour(config.selection_background, 0, 255, 0));
        expect("accent, mixed case hex", isColour(config.accent, 0, 0, 255));
    }

    // The window's body: font, opening grid, opacity. And the ranges that
    // keep out the spellings a person might try — `14px` is a font size they
    // meant, and it earns a line of complaint, not a silent half-read.
    const std::string body_file = dir + "/body-settings";
    writeFile(body_file, "font_size = 16\ncols = 120\nrows = 40\nopacity = .85\n");
    ::setenv("KAI_TERM_CONFIG", body_file.c_str(), 1);
    {
        const Config body = loadConfig();
        expect("the body settings all arrive",
               body.problems.empty() && body.font_size && *body.font_size == 16 &&
                   body.cols && *body.cols == 120 && body.rows && *body.rows == 40 &&
                   body.background_opacity && *body.background_opacity > 0.84 &&
                   *body.background_opacity < 0.86);
        // `opacity` is the older single name for the ground. It still reads, so a
        // file written before the two halves were split keeps working — and it says
        // nothing about the ink, which stays at the built-in answer.
        expect("the older `opacity` names the ground, and leaves the ink alone",
               body.background_opacity && !body.foreground_opacity);
    }
    // The two halves of transparency, named separately: a background you can see
    // the desktop through and text that stays solid is the whole point of there
    // being two numbers.
    const std::string glass = dir + "/body-glass";
    writeFile(glass, "background_opacity = 0.55\nforeground_opacity = 1\n");
    ::setenv("KAI_TERM_CONFIG", glass.c_str(), 1);
    {
        const Config both = loadConfig();
        expect("both transparencies arrive, and separately",
               both.problems.empty() && both.background_opacity &&
                   *both.background_opacity > 0.54 && *both.background_opacity < 0.56 &&
                   both.foreground_opacity && *both.foreground_opacity == 1.0);
    }
    {
        // The three settings the config menu also edits. Each has a range or a
        // short list, and a value outside either is a named refusal rather than a
        // silent half-read — the same promise the menu keeps by clamping, kept here
        // for the file somebody edits at 2am.
        const std::string body_keys = dir + "/body-menu-keys";
        writeFile(body_keys,
                  "scrollback = 5000\npadding = 14\ncursor_style = bar\ncursor_blink = 600\n"
                  "divider_width = 3\nsave_name_prompt = off\n");
        ::setenv("KAI_TERM_CONFIG", body_keys.c_str(), 1);
        {
            const Config keys = loadConfig();
            expect("the scrollback, the padding and the cursor shape all arrive",
                   keys.problems.empty() && keys.scrollback && *keys.scrollback == 5000 &&
                       keys.padding && *keys.padding == 14.0 && keys.cursor_style &&
                       *keys.cursor_style == CursorStyle::Bar && keys.cursor_blink &&
                       *keys.cursor_blink == 600);
            // The line between two places, and whether keeping a session asks for a name.
            // (Kaida, 2026-11-24: "the separator should be thinner and equal in size ...
            // maybe even an option in the settings to change it size"; "saving a session
            // should now prompt for a name (can be disabled in setting)".)
            expect("the divider's width and the name question both arrive",
                   keys.problems.empty() && keys.divider_width &&
                       *keys.divider_width == 3 && keys.save_name_prompt == "off");
        }
        // (Kaida, 2026-11-21: "customizable blink speed ( 0 being no blinking)".) The
        // zero has to arrive as a zero rather than as \"nothing was said\", because the
        // two mean different things to the window: one is a cursor that stays lit, the
        // other is whatever the window does when nobody has asked.
        const std::string steady = dir + "/body-blink-steady";
        writeFile(steady, "cursor_blink = 0\n");
        ::setenv("KAI_TERM_CONFIG", steady.c_str(), 1);
        {
            const Config zero = loadConfig();
            expect("a blink of zero is an answer and not an absent setting",
                   zero.problems.empty() && zero.cursor_blink && *zero.cursor_blink == 0);
        }
        const std::string wrong = dir + "/body-menu-wrong";
        writeFile(wrong,
                  "scrollback = ninety thousand\npadding = 4000\ncursor_style = blok\n"
                  "cursor_blink = soon\ndivider_width = 999\nsave_name_prompt = maybe\n");
        ::setenv("KAI_TERM_CONFIG", wrong.c_str(), 1);
        {
            const Config junk = loadConfig();
            expect("a divider wider than a cell is out of range",
                   !junk.divider_width && mentions(junk.problems, "divider_width"));
            expect("and the name question takes on or off, not an opinion",
                   junk.save_name_prompt.empty() &&
                       mentions(junk.problems, "save_name_prompt"));
            expect("a scrollback spelled in words is refused by name",
                   !junk.scrollback && mentions(junk.problems, "scrollback"));
            expect("four thousand pixels of padding is out of range",
                   !junk.padding && mentions(junk.problems, "padding"));
            // A misspelled answer is the one mistake worth calling out by its
            // alternatives: `blok` is a typo, and the person knows both words.
            expect("a cursor shape that is not one of the three is refused",
                   !junk.cursor_style && mentions(junk.problems, "cursor_style") &&
                       mentions(junk.problems, "block"));
            expect("a blink rate that is not a number is refused by name",
                   !junk.cursor_blink && mentions(junk.problems, "cursor_blink"));
            expect("every one of them said its piece", junk.problems.size() == 6);
        }
    }
    {
        // A ground of 0 is allowed — an invisible background is a thing a person
        // may genuinely want — and a negative one is not.
        const std::string edges = dir + "/body-glass-edges";
        writeFile(edges, "background_opacity = 0\nforeground_opacity = -1\n");
        ::setenv("KAI_TERM_CONFIG", edges.c_str(), 1);
        const Config edge = loadConfig();
        expect("a fully transparent ground is accepted",
               edge.background_opacity && *edge.background_opacity == 0.0);
        expect("a negative ink is refused by name",
               !edge.foreground_opacity && mentions(edge.problems, "foreground_opacity"));
    }
    const std::string nonsense = dir + "/body-nonsense";
    writeFile(nonsense, "font_size = 14px\ncols = 0\nopacity = 3\nrows = 9999\n");
    ::setenv("KAI_TERM_CONFIG", nonsense.c_str(), 1);
    {
        const Config junk = loadConfig();
        expect("a font size with units is refused, not half-read",
               !junk.font_size && mentions(junk.problems, "font_size"));
        expect("zero columns is out of range", !junk.cols && mentions(junk.problems, "cols"));
        expect("an opacity of three is out of range",
               !junk.background_opacity && mentions(junk.problems, "opacity"));
        expect("nine thousand rows is out of range", !junk.rows);
        expect("every refusal said its piece, four in all", junk.problems.size() == 4);
    }

    // The sixteen palette entries: `color0` through `color15`, the spelling
    // every terminal program shares. `color16` is the mistake worth naming,
    // because counting from zero is exactly the thing people miscount.
    const std::string palette_file = dir + "/palette-settings";
    writeFile(palette_file, "color0 = #010203\ncolor15 = #ff00ff\ncolor7 = 192,192,192\n");
    ::setenv("KAI_TERM_CONFIG", palette_file.c_str(), 1);
    {
        const Config pal = loadConfig();
        expect("the palette entries arrive",
               pal.palette[0] && pal.palette[0]->r == 1 &&
                   pal.palette[0]->b == 3 && pal.palette[15] && pal.palette[15]->g == 0);
        expect("an entry nobody wrote stays unwritten", !pal.palette[9]);
        expect("a palette value in the wrong spelling is refused",
               !pal.palette[7] && mentions(pal.problems, "192,192,192"));
    }
    const std::string miscount = dir + "/palette-miscount";
    writeFile(miscount, "color16 = #000000\ncolorx = #000000\ncolor = #000000\n");
    ::setenv("KAI_TERM_CONFIG", miscount.c_str(), 1);
    {
        const Config bad = loadConfig();
        expect("color16 is no setting, and says so",
               mentions(bad.problems, "color16"));
        expect("colorx and bare color are no settings either",
               mentions(bad.problems, "colorx") && mentions(bad.problems, "'color'"));
        expect("three refusals, no crashes", bad.problems.size() == 3);
    }

    // Back to the one file the rest of the test keeps rewriting.
    ::setenv("KAI_TERM_CONFIG", path.c_str(), 1);

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
              "cursor_style\n" // a name and no value, in either spelling
              "confirm_quit = ALWAYS\n");
    {
        const Config config = loadConfig();
        expect("the unknown key is named, not skipped",
               mentions(config.problems, "confirm_exit"));
        // A bare word with nothing after it is not a setting in either spelling, and
        // says so. A line of several words *is* read as `key value` now — which is the
        // kitty spelling — so it is announced as the unknown setting it turned out to be.
        expect("a name with no value is announced",
               mentions(config.problems, "expected key = value"));
        expect("and a line of words is announced as the setting it names",
               mentions(config.problems, "'this'"));
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

    // Writing settings back: the file is somebody's, so everything this was
    // not asked to change stays byte for byte.
    const std::string kept = dir + "/kept-config";
    writeFile(kept,
              "# my terminal\n"
              "\n"
              "confirm_quit = never      # loud closings are not for me\n"
              "background   = #1e1e1e   # matched to my editor\n"
              "weird_setting = yes\n"
              "cursor = #ffffff\n");
    {
        std::string error;
        const bool saved = saveConfigValues(
            kept, {{"background", "#0a1428"}, {"accent", "#ff0055"}}, error);
        expect("the save succeeds", saved && error.empty());
        std::ifstream in(kept);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        expect("the header comment survived", text.find("# my terminal") != std::string::npos);
        expect("confirm_quit survived untouched",
               text.find("confirm_quit = never      # loud closings are not for me") !=
                   std::string::npos);
        expect("the unknown setting survived untouched",
               text.find("weird_setting = yes") != std::string::npos);
        expect("the edited line kept its alignment",
               text.find("background   = #0a1428") != std::string::npos);
        expect("and kept the comment somebody left beside it",
               text.find("# matched to my editor") != std::string::npos);
        expect("the old value is gone", text.find("#1e1e1e") == std::string::npos);
        expect("a key that was not there is added",
               text.find("accent = #ff0055") != std::string::npos);
        expect("the untouched cursor line is exactly as it was",
               text.find("cursor = #ffffff") != std::string::npos);
        // And the file it wrote is a file the reader agrees about.
        ::setenv("KAI_TERM_CONFIG", kept.c_str(), 1);
        const Config round = loadConfig();
        // The file we wrote is readable, and the one thing that bothers the
        // reader is the unknown setting the test put there on purpose — which
        // is exactly the point: the save preserved it, and the reader still
        // tells the person about it. A clean read would mean the save had
        // silently dropped the line it was supposed to keep.
        expect("the file reads back with only the preserved oddity to complain about",
               round.problems.size() == 1 &&
                   round.problems[0].find("weird_setting") != std::string::npos);
        expect("the new background reads back",
               round.background && round.background->r == 0x0A &&
                   round.background->b == 0x28);
        expect("the appended accent reads back",
               round.accent && round.accent->r == 255 && round.accent->b == 85);
        expect("confirm_quit still says never", round.confirm_quit == "never");
    }

    // A theme file is written without the `=`, and saving over one has to edit the lines
    // that are there. It did not: only a line containing `=` was recognised, so nothing
    // matched, nothing was marked as written, and every colour went on at the bottom in
    // the old shape — a theme holding each of its colours twice, the copy the reader saw
    // last winning. (Kaida, 2026-11-21: "Overwriting a theme should also Overwrite it for
    // real instead of appending to it"; and on which shape is correct, "only the =less
    // layout should be used is what i meant, we a re deprecating the value = xx layout".)
    const std::string kept_theme = dir + "/kept-theme";
    writeFile(kept_theme,
              "# kai-term colours\n"
              "background            #f7f7f7\n"
              "foreground            #494542   # matched to my editor\n"
              "color0                #090200\n");
    {
        std::string error;
        const bool saved = saveConfigValues(
            kept_theme, {{"background", "#e7f7f7"}, {"color1", "#ea2c20"}}, error,
            "# kai-term colours\n", /*equals_sign=*/false);
        expect("saving over a theme written without the sign succeeds", saved && error.empty());
        std::ifstream in(kept_theme);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        expect("the colour that moved is changed where it stood",
               text.find("background            #e7f7f7") != std::string::npos);
        expect("and nothing was appended in the old shape",
               text.find("background =") == std::string::npos &&
                   text.find(" = #") == std::string::npos);
        expect("the theme holds its background once, not twice",
               text.find("#f7f7f7") == std::string::npos);
        expect("a colour the file never held joins without the sign",
               text.find("color1                #ea2c20") != std::string::npos);
        expect("in line with the colours above it",
               text.find("\ncolor1") > text.find("\nbackground"));
        expect("the untouched colour is exactly as it was",
               text.find("color0                #090200") != std::string::npos);
        expect("and the note beside a colour survives editing another one",
               text.find("# matched to my editor") != std::string::npos);
        // What the whole thing was for: the file the writer produced is the file the
        // reader agrees about, with no line complaining and no colour doubled.
        ::setenv("KAI_TERM_THEME", kept_theme.c_str(), 1);
        ::setenv("KAI_TERM_CONFIG", (dir + "/no-such-config").c_str(), 1);
        const Config back = loadConfig();
        expect("the rewritten theme reads back with nothing to complain about",
               back.problems.empty());
        expect("wearing the new background", back.background && back.background->r == 0xe7);
        expect("the colour that was added",
               back.palette[1] && back.palette[1]->r == 0xea && back.palette[1]->g == 0x2c);
        expect("and the ink the file still says, comment and all",
               back.foreground && back.foreground->r == 0x49);
    }

    // A theme somebody wrote the old way is not wrong, and saving over it converts the
    // lines it touches rather than leaving the file in both shapes at once.
    const std::string old_theme = dir + "/old-shape-theme";
    writeFile(old_theme, "background = #101010\nforeground = #d0d0d0\n");
    {
        std::string error;
        static_cast<void>(saveConfigValues(old_theme, {{"background", "#202020"}}, error,
                                           "# kai-term colours\n", /*equals_sign=*/false));
        std::ifstream in(old_theme);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        expect("a theme written the old way is edited in place, not doubled",
               text.find("background  #202020") != std::string::npos &&
                   text.find("#101010") == std::string::npos);
        expect("and the line this save had no reason to touch keeps its shape",
               text.find("foreground = #d0d0d0") != std::string::npos);
    }

    // colourText is what the picker writes, and a colour made entirely of
    // digits would never show a broken letter. Spell every one of them and
    // read the answer back, because a saved colour the reader refuses is a
    // colour that silently did not save.
    {
        expect("colourText spells a full white as all f's",
               colourText(Rgb{0xff, 0xff, 0xff}) == "#ffffff");
        expect("and a mid colour with letters in every place",
               colourText(Rgb{0xac, 0xbd, 0xce}) == "#acbdce");
        expect("digits still spell digits", colourText(Rgb{0x01, 0x90, 0x55}) == "#019055");
        // Round trip: whatever colourText writes, the reader must accept back.
        const Rgb original{0x11, 0xff, 0x00};
        std::array<int, 3> back{};
        const bool read = colourFromText(colourText(original), back);
        expect("and the reader takes back what colourText wrote",
               read && back[0] == 0x11 && back[1] == 0xff && back[2] == 0x00);
    }

    // A first save on a fresh machine: no file, and maybe no directory.
    const std::string fresh = dir + "/first-time/config";
    {
        std::string error;
        const bool saved = saveConfigValues(fresh, {{"cursor", "#00ff00"}}, error);
        expect("the first save creates the file and its directory", saved && error.empty());
        std::ifstream in(fresh);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        expect("a brand new file says what it is",
               text.find("kai-term settings") != std::string::npos);
        expect("and carries the setting", text.find("cursor = #00ff00") != std::string::npos);
        std::ifstream leftover((fresh + ".tmp").c_str());
        expect("no temp file is left behind", !leftover.good());
    }

    // The two files, and the order they are read in. A colour in the theme
    // file overrides the same colour in the settings file, because the theme
    // is the file the picker writes and the more recent intention; a setting
    // nobody repeated in the theme keeps whatever the settings said.
    const std::string settings = dir + "/two-settings";
    const std::string theme_file = dir + "/two-theme";
    writeFile(settings, "confirm_quit = always\nbackground = #111111\naccent = #222222\n");
    writeFile(theme_file, "background = #eeeeee\n");
    ::setenv("KAI_TERM_CONFIG", settings.c_str(), 1);
    ::setenv("KAI_TERM_THEME", theme_file.c_str(), 1);
    {
        const Config both = loadConfig();
        expect("both files say nothing odd", both.problems.empty());
        expect("confirm_quit comes from the settings file", both.confirm_quit == "always");
        expect("the theme file overrides the settings file for a colour it names",
               both.background && both.background->r == 0xEE);
        expect("a colour only the settings file names survives the theme",
               both.accent && both.accent->r == 0x22);
    }

    // Where the files live, in the order people expect.
    ::setenv("KAI_TERM_CONFIG", "/somewhere/else", 1);
    ::setenv("KAI_TERM_THEME", "/somewhere/palette", 1);
    expect("KAI_TERM_CONFIG points straight at the settings file",
           settingsFilePath() == "/somewhere/else");
    expect("KAI_TERM_THEME points straight at the theme file",
           themeFilePath() == "/somewhere/palette");
    ::unsetenv("KAI_TERM_CONFIG");
    ::unsetenv("KAI_TERM_THEME");
    ::setenv("XDG_CONFIG_HOME", "/xdg", 1);
    expect("otherwise both live in the usual place, by their own names",
           settingsFilePath() == "/xdg/kai-term/kai-term.conf" &&
               themeFilePath() == "/xdg/kai-term/theme.conf");
    ::unsetenv("XDG_CONFIG_HOME");
    ::setenv("HOME", "/home/somebody", 1);
    expect("and ~/.config when XDG is not set",
           settingsFilePath() == "/home/somebody/.config/kai-term/kai-term.conf" &&
               themeFilePath() == "/home/somebody/.config/kai-term/theme.conf");

    // `--config-dir DIR` reads BOTH files from DIR directly — the escape hatch for
    // running against a second set of settings. It loses to an explicit per-file
    // override (so tests and one-off single-file overrides keep working) and beats
    // the default directory.
    {
        const std::string alt = dir;
        writeFile(alt + "/kai-term.conf", "background = #102030\n");
        writeFile(alt + "/theme.conf", "color5 = #0a0b0c\n");
        ::unsetenv("KAI_TERM_CONFIG");
        ::unsetenv("KAI_TERM_THEME");
        setConfigDir(alt);
        expect("--config-dir sends the settings file into DIR",
               settingsFilePath() == alt + "/kai-term.conf");
        expect("and the theme file into that same DIR",
               themeFilePath() == alt + "/theme.conf");
        const Config via_dir = loadConfig();
        expect("and the file in DIR is the one actually read",
               isColour(via_dir.background, 0x10, 0x20, 0x30) &&
                   isColour(via_dir.palette[5], 0x0A, 0x0B, 0x0C));
        ::setenv("KAI_TERM_CONFIG", path.c_str(), 1);
        expect("but KAI_TERM_CONFIG still wins for the single file it names",
               settingsFilePath() == path && themeFilePath() == alt + "/theme.conf");
        ::unsetenv("KAI_TERM_CONFIG");
        setConfigDir(""); // back to the usual place
        expect("and clearing it returns to the default directory",
               themeFilePath().find("/kai-term/theme.conf") != std::string::npos);
        std::remove((alt + "/kai-term.conf").c_str());
        std::remove((alt + "/theme.conf").c_str());
    }

    {
        // `-c FILE`: the settings from that file, and the colours from the `theme.conf`
        // beside it. One argument, because one thing is being decided — which
        // configuration to run against.
        // (Kaida, 2026-11-20: "we need a way to select a different config file to read
        // from on launch with an argument ex : kai-term -c /path/to/config.conf ...
        // instead of a Dir because theme is now defined in the config".)
        const std::string other = dir + "/other";
        ::mkdir(other.c_str(), 0755);
        writeFile(other + "/work.conf", "background = #010203\n");
        writeFile(other + "/theme.conf", "color7 = #040506\n");
        ::unsetenv("KAI_TERM_CONFIG");
        ::unsetenv("KAI_TERM_THEME");
        setConfigFile(other + "/work.conf");
        expect("-c names the file the settings come from",
               settingsFilePath() == other + "/work.conf");
        expect("and the colours come from the theme.conf beside it",
               themeFilePath() == other + "/theme.conf");
        const Config via_file = loadConfig();
        expect("and both of those are the files actually read",
               isColour(via_file.background, 0x01, 0x02, 0x03) &&
                   isColour(via_file.palette[7], 0x04, 0x05, 0x06));
        // A directory handed to `-c` is the same answer with both files under their
        // usual names, which is what a shell completing a path tends to produce.
        setConfigFile(other);
        expect("-c takes a directory too, and reads the usual names from it",
               settingsFilePath() == other + "/kai-term.conf" &&
                   themeFilePath() == other + "/theme.conf");
        ::setenv("KAI_TERM_THEME", theme_file.c_str(), 1);
        expect("while KAI_TERM_THEME still wins for the one file it names",
               themeFilePath() == theme_file);
        ::unsetenv("KAI_TERM_THEME");
        setConfigFile("");
        expect("and saying no file returns to the usual place",
               themeFilePath().find("/kai-term/theme.conf") != std::string::npos &&
               settingsFilePath().find("/kai-term/kai-term.conf") != std::string::npos);
        std::remove((other + "/work.conf").c_str());
        std::remove((other + "/theme.conf").c_str());
        ::rmdir(other.c_str());
    }

    {
        // A theme file from kitty, read as it stands: no `=` anywhere, the palette spelled
        // `color0`…`color15`, and the two selection colours spelled kitty's way. The ask was
        // that dropping a file somebody already wrote should be the whole of the work.
        // (Kaida, 2026-11-20: "change the theme config to not require an = between the name
        // and the value, so that kitty themes can be used as is simplifying transition for
        // some users", and the file itself: "yes this is a kitty theme conf".)
        const std::string home = dir + "/kittydir";
        const std::string themes = home + "/themes";
        ::mkdir(home.c_str(), 0755);
        ::mkdir(themes.c_str(), 0755);
        writeFile(themes + "/paper.conf",
                  "background #1c1c1c\n"
                  "foreground #ddeedd\n"
                  "cursor #e2bbef\n"
                  "selection_background #4d4d4d\n"
                  "color0 #3d352a\n"
                  "color8 #554444\n"
                  "color1 #cd5c5c\n"
                  "color15 #ddccbb\n"
                  "selection_foreground #1c1c1c\n");
        writeFile(home + "/kai-term.conf", "theme = paper.conf\n");
        ::unsetenv("KAI_TERM_CONFIG");
        ::unsetenv("KAI_TERM_THEME");
        setConfigDir(home);
        const Config kitty = loadConfig();
        expect("a kitty theme needs no = to be understood",
               isColour(kitty.background, 0x1c, 0x1c, 0x1c) &&
                   isColour(kitty.foreground, 0xdd, 0xee, 0xdd));
        expect("its colour0 is the black of its own palette",
               isColour(kitty.palette[0], 0x3d, 0x35, 0x2a) &&
                   isColour(kitty.palette[8], 0x55, 0x44, 0x44) &&
                   isColour(kitty.palette[15], 0xdd, 0xcc, 0xbb));
        expect("its selection colour arrives, and so does the ink on it",
               isColour(kitty.selection_background, 0x4d, 0x4d, 0x4d) &&
                   isColour(kitty.selection_foreground, 0x1c, 0x1c, 0x1c));
        expect("and nothing about it was complained about", kitty.problems.empty());

        // The two spellings are the same sentence, not two dialects: a file may mix them,
        // a comment still hangs off the end of a colour, and the older name for the
        // selection colour still reads.
        writeFile(themes + "/paper.conf",
                  "background = #010203\n"          // this program's spelling
                  "foreground #040506\n"            // kitty's
                  "selection #070809\n"             // the older name for the one colour
                  "cursor #0a0b0c # a comment after a colour\n");
        const Config mixed = loadConfig();
        expect("a file may mix the two spellings of the same sentence",
               isColour(mixed.background, 0x01, 0x02, 0x03) &&
                   isColour(mixed.foreground, 0x04, 0x05, 0x06));
        expect("`selection` still reads as the older name for the highlight",
               isColour(mixed.selection_background, 0x07, 0x08, 0x09));
        expect("and a comment still falls off the end of a colour written kitty's way",
               isColour(mixed.cursor, 0x0a, 0x0b, 0x0c) && mixed.problems.empty());

        // What the picker writes is what the reader reads: the screen says `black` and
        // `selection`, the file says `color0` and `selection_background`.
        std::string error;
        std::vector<std::pair<std::string, std::string>> out;
        out.push_back({"color0", "#ff0000"});
        out.push_back({"selection_background", "#00ff00"});
        out.push_back({"selection_foreground", "#0000ff"});
        expect("the palette and the two selection colours write under those names",
               saveConfigValues(themes + "/written.conf", out, error));
        ::setenv("KAI_TERM_THEME", (themes + "/written.conf").c_str(), 1);
        const Config round = loadConfig();
        expect("and read back the same way, by this program or by kitty",
               isColour(round.palette[0], 0xff, 0x00, 0x00) &&
                   isColour(round.selection_background, 0x00, 0xff, 0x00) &&
                   isColour(round.selection_foreground, 0x00, 0x00, 0xff));
        ::unsetenv("KAI_TERM_THEME");
        std::remove((themes + "/paper.conf").c_str());
        std::remove((themes + "/written.conf").c_str());
        std::remove((home + "/kai-term.conf").c_str());
        ::rmdir(themes.c_str());
        ::rmdir(home.c_str());
        setConfigDir("");
    }


    {
        // `theme = ...`: which colours to wear. A bare name is a file in `themes`, a
        // name with a slash is that path, and `Default.conf` — like leaving it out —
        // is the file the colours have always lived in.
        // (Kaida, 2026-11-20: "We add a 'Theme = Default.conf' to our config and a
        // sub-directory 'themes' in ~/.config/kai-term where they get sourced from by
        // default unless a full path for the theme is given".)
        const std::string home = dir + "/themedir";
        const std::string themes = home + "/themes";
        ::mkdir(home.c_str(), 0755);
        ::mkdir(themes.c_str(), 0755);
        writeFile(home + "/kai-term.conf", "theme = midnight.conf\n");
        writeFile(home + "/theme.conf", "background = #111111\n");
        writeFile(themes + "/midnight.conf", "background = #222222\n");
        writeFile(themes + "/b.conf", "background = #333333\n");
        writeFile(themes + "/a.conf", "background = #444444\n");
        writeFile(themes + "/notes.txt", "not a theme\n");
        ::unsetenv("KAI_TERM_CONFIG");
        ::unsetenv("KAI_TERM_THEME");
        setConfigDir(home);
        expect("no theme named is the file the colours have always lived in",
               resolveThemePath("") == home + "/theme.conf");
        expect("and Default.conf says the same thing in words",
               resolveThemePath("Default.conf") == home + "/theme.conf");
        expect("a bare name is a file in the themes directory",
               resolveThemePath("midnight.conf") == themes + "/midnight.conf");
        expect("and a name with a slash in it is the path it is",
               resolveThemePath("/elsewhere/odd.conf") == "/elsewhere/odd.conf");
        const std::vector<std::string> offered = listThemes();
        expect("the themes to choose from are the default and the ones on disk, in order",
               offered == std::vector<std::string>{"Default.conf", "a.conf", "b.conf",
                                                   "midnight.conf"});
        const Config chosen = loadConfig();
        expect("the settings file says which theme, and that file is the one read",
               chosen.theme_path == themes + "/midnight.conf" &&
                   isColour(chosen.background, 0x22, 0x22, 0x22));
        expect("and the theme is remembered as it was written", chosen.theme == "midnight.conf");
        expect("a theme nobody wrote down still has a name to show",
               themeName("") == "Default.conf" && themeName("x.conf") == "x.conf");
        ::setenv("KAI_TERM_THEME", (home + "/theme.conf").c_str(), 1);
        expect("while KAI_TERM_THEME outranks what the file asked for",
               resolveThemePath("midnight.conf") == home + "/theme.conf");
        const Config forced = loadConfig();
        expect("and the colours follow it, not the name",
               isColour(forced.background, 0x11, 0x11, 0x11));
        ::unsetenv("KAI_TERM_THEME");

        writeFile(home + "/kai-term.conf", "theme =\n");
        const Config blank = loadConfig();
        expect("a `theme` with nothing after it is refused by name",
               blank.problems.size() == 1 &&
                   mentions(blank.problems, "theme needs a file name"));
        expect("and the colours fall back to the default file",
               isColour(blank.background, 0x11, 0x11, 0x11));
        setConfigDir("");
        for (const std::string& gone : {themes + "/midnight.conf", themes + "/a.conf",
                                        themes + "/b.conf", themes + "/notes.txt",
                                        home + "/kai-term.conf", home + "/theme.conf"})
            std::remove(gone.c_str());
        ::rmdir(themes.c_str());
        ::rmdir(home.c_str());
    }

    std::remove(path.c_str());
    std::remove(kept.c_str());
    std::remove(fresh.c_str());
    std::remove(settings.c_str());
    std::remove(theme_file.c_str());
    std::remove(body_file.c_str());
    std::remove(nonsense.c_str());
    std::remove(palette_file.c_str());
    std::remove(miscount.c_str());
    std::remove((dir + "/first-time").c_str());
    ::rmdir(dir.c_str());

    if (failures > 0) {
        std::cerr << failures << " of " << checks << " config checks failed\n";
        return 1;
    }
    std::cout << "PASS: " << checks << " config checks passed\n";
    return 0;
}
