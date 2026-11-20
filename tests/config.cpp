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
        expect("selection read", isColour(config.selection, 0, 255, 0));
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
        writeFile(body_keys, "scrollback = 5000\npadding = 14\ncursor_style = bar\n");
        ::setenv("KAI_TERM_CONFIG", body_keys.c_str(), 1);
        {
            const Config keys = loadConfig();
            expect("the scrollback, the padding and the cursor shape all arrive",
                   keys.problems.empty() && keys.scrollback && *keys.scrollback == 5000 &&
                       keys.padding && *keys.padding == 14.0 && keys.cursor_style &&
                       *keys.cursor_style == CursorStyle::Bar);
        }
        const std::string wrong = dir + "/body-menu-wrong";
        writeFile(wrong, "scrollback = ninety thousand\npadding = 4000\ncursor_style = blok\n");
        ::setenv("KAI_TERM_CONFIG", wrong.c_str(), 1);
        {
            const Config junk = loadConfig();
            expect("a scrollback spelled in words is refused by name",
                   !junk.scrollback && mentions(junk.problems, "scrollback"));
            expect("four thousand pixels of padding is out of range",
                   !junk.padding && mentions(junk.problems, "padding"));
            // A misspelled answer is the one mistake worth calling out by its
            // alternatives: `blok` is a typo, and the person knows both words.
            expect("a cursor shape that is not one of the three is refused",
                   !junk.cursor_style && mentions(junk.problems, "cursor_style") &&
                       mentions(junk.problems, "block"));
            expect("all three said their piece", junk.problems.size() == 3);
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
