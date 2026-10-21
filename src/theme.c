/* A theme is a file of COLOURS and nothing else. The syntax is the one Kai-Term already reads, so a kitty
   theme loads unchanged, and only palette entries 0-15 are themeable: the 6x6x6 cube and the grey ramp are
   computed, and repainting them would make every 256-colour chart a lie about this terminal. */
#include "kai-term.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>     /* strcasecmp / strncasecmp: the other programs' key spellings are not consistent */
#include <sys/stat.h>    /* stat(): a theme has to be a real regular file, same rule as a config file */

/* The names a theme file may set. tests/test_vt.c reads this table out of the source and compares it with
   config/kai-term.theme, so a name added here without being documented fails a check. `canonical` is the
   kai-term.conf name that outranks the theme's own. */
enum { TK_FG, TK_BG, TK_CURSOR, TK_SEL, TK_COLOR, TK_NAME };
typedef struct ThemeKey {
    const char *name;
    int kind;
    int idx;                 /* TK_COLOR: the palette entry, 0-15 */
    const char *canonical;   /* the kai-term.conf name that beats this one; NULL: nothing beats it */
} ThemeKey;
static const ThemeKey THEME_KEYS[] = {
    { "foreground",           TK_FG,     -1, "foreground"       },
    { "background",           TK_BG,     -1, "background"       },
    { "cursor",               TK_CURSOR, -1, "cursor_color"     },
    { "cursor_color",         TK_CURSOR, -1, "cursor_color"     },   /* alias */
    { "selection_background", TK_SEL,    -1, "selection_color"  },
    { "selection_color",      TK_SEL,    -1, "selection_color"  },   /* alias */
    { "color0",               TK_COLOR,   0, NULL },
    { "color1",               TK_COLOR,   1, NULL },
    { "color2",               TK_COLOR,   2, NULL },
    { "color3",               TK_COLOR,   3, NULL },
    { "color4",               TK_COLOR,   4, NULL },
    { "color5",               TK_COLOR,   5, NULL },
    { "color6",               TK_COLOR,   6, NULL },
    { "color7",               TK_COLOR,   7, NULL },
    { "color8",               TK_COLOR,   8, NULL },
    { "color9",               TK_COLOR,   9, NULL },
    { "color10",              TK_COLOR,  10, NULL },
    { "color11",              TK_COLOR,  11, NULL },
    { "color12",              TK_COLOR,  12, NULL },
    { "color13",              TK_COLOR,  13, NULL },
    { "color14",              TK_COLOR,  14, NULL },
    { "color15",              TK_COLOR,  15, NULL },
    { "name",                 TK_NAME,   -1, NULL },   /* metadata: what `--import-theme` names the file */
};

/* Names from other programs that are understood but have nowhere to go. Reported, never guessed at: Kai-Term
   has no per-cell "the text on this colour" concept — the ink of a selected cell is that cell's own
   foreground — so inventing one to satisfy these two keys would be a feature nobody asked for, silently. */
static const struct { const char *name; const char *why; } THEME_DROPPED[] = {
    { "selection_foreground", "Kai-Term draws selected text in the cell's own colour" },
    { "cursor_text_color",    "Kai-Term draws the glyph under the cursor in the cell's own colour" },
};

static void trim_inplace(char *s)
{
    char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
}

/* A colour or nothing. cfg_parse_color answers with a fallback for text that is not one, so the fallback is
   a value no real colour has — 0x00000000 is not a colour a theme can ask for, because a 6-digit value
   always gets alpha 0xff and an 8-digit value with alpha 00 is invisible by definition. */
static int theme_color(const char *s, uint32_t *out)
{
    uint32_t v = cfg_parse_color(s, 0x00000000u);
    if (v == 0x00000000u)
        return 0;
    *out = v;
    return 1;
}

/* One line of a theme. Returns 1 when the value was applied, 0 when it was skipped, refused or dropped. */
static int theme_apply_key(Config *c, const char *key, const char *val, const char *path, int lineno)
{
    const ThemeKey *hit = NULL;
    for (size_t i = 0; i < sizeof THEME_KEYS / sizeof THEME_KEYS[0]; i++) {
        if (!strcmp(key, THEME_KEYS[i].name)) {
            hit = &THEME_KEYS[i];
            break;
        }
    }
    if (!hit) {
        for (size_t i = 0; i < sizeof THEME_DROPPED / sizeof THEME_DROPPED[0]; i++) {
            if (!strcmp(key, THEME_DROPPED[i].name)) {
                klog(L_INFO, "theme:%d: '%s' is understood but has nowhere to go (%s) — ignored",
                     lineno, key, THEME_DROPPED[i].why);
                return 0;
            }
        }
        klog(L_WARN, "theme:%d: unknown key '%s' in %s", lineno, key, path);
        return 0;
    }

    if (hit->kind == TK_NAME) {
        klog(L_DEBUG, "theme: name '%s' (%s)", val, path);
        return 0;                          /* metadata: nothing to apply */
    }

    uint32_t col;
    if (!theme_color(val, &col)) {
        klog(L_WARN, "theme:%d: '%s = %s' is not a colour (#RRGGBB, 0xRRGGBB, RRGGBB or AARRGGBB)",
             lineno, key, val);
        return 0;
    }

    /* Palette entries have no kai-term.conf name, so nothing beats them. The four chrome colours do: a
       colour the user wrote in the settings file always wins, and the log says so by name. */
    if (hit->canonical && config_key_was_set(c, hit->canonical)) {
        klog(L_INFO, "theme: %s from %s ignored — kai-term.conf sets %s", key, path, hit->canonical);
        return 0;
    }

    switch (hit->kind) {
    case TK_COLOR:
        term_set_palette_entry(hit->idx, col);
        break;
    case TK_FG:     c->fg = col;             break;
    case TK_BG:     c->bg = col;             break;
    case TK_CURSOR: c->cursor_color = col;   break;
    case TK_SEL:    c->selection_color = col; break;
    default:        return 0;                /* TK_NAME handled above */
    }
    klog(L_INFO, "theme: %s = %s (%s)", key, val, path);
    return 1;
}

/* Does `name` end in `.theme`? A value that already names a file is used as it stands, so
   `theme = solarized.theme` does not look for `solarized.theme.theme`. */
static int ends_in_theme(const char *name)
{
    size_t n = strlen(name);
    return n > 6 && !strcmp(name + n - 6, ".theme");
}

void theme_find(const char *value, char *out, size_t cap)
{
    out[0] = 0;
    if (!value || !value[0])
        return;

    char expanded[4096];
    expand_tilde(value, expanded, sizeof expanded);

    /* A path — absolute, relative, or '~'-expanded. Checked as a regular file, same rule as config_find:
       a directory or a FIFO named like a theme is not a theme. */
    if (strchr(expanded, '/')) {
        struct stat st;
        if (stat(expanded, &st) == 0 && S_ISREG(st.st_mode))
            snprintf(out, cap, "%s", expanded);
        return;
    }

    /* A name: the first NAME.theme in the themes/ subdirectory of the three config directories. Same order
       and same first-hit-wins rule as config_find, so a user's own theme shadows the system's and the two
       are never mixed. */
    char file[300];
    /* The name is capped where Config.theme is capped (255 chars); spelling the cap out is also what tells
       the compiler this cannot truncate, so the build stays warning-free. */
    if (ends_in_theme(expanded))
        snprintf(file, sizeof file, "%.255s", expanded);
    else
        snprintf(file, sizeof file, "%.255s.theme", expanded);

    char cand[3][4096];
    int n = 0;
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        snprintf(cand[n], sizeof cand[0], "%s/kai-term/themes/%s", xdg, file);
        n++;
    }
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(cand[n], sizeof cand[0], "%s/.config/kai-term/themes/%s", home, file);
        n++;
    }
    snprintf(cand[n], sizeof cand[0], "/etc/kai-term/themes/%s", file);
    n++;
    for (int i = 0; i < n; i++) {
        struct stat st;
        if (stat(cand[i], &st) == 0 && S_ISREG(st.st_mode)) {
            snprintf(out, cap, "%s", cand[i]);
            return;
        }
    }
}

int theme_apply_file(Config *c, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        klog(L_WARN, "theme: cannot open %s", path);
        return -1;
    }
    char line[512];
    int lineno = 0, applied = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        config_strip_comment(line);
        trim_inplace(line);
        if (!line[0])
            continue;
        /* `key = value`, or kitty's `key value` when there is no '=' at all. Both spellings are in the
           wild; refusing one would make half the theme corpus need editing by hand. */
        char *key = line, *val;
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = 0;
            val = eq + 1;
        } else {
            char *sp = line;
            while (*sp && !isspace((unsigned char)*sp))
                sp++;
            if (!*sp) {
                klog(L_WARN, "theme:%d: '%s' has no value", lineno, line);
                continue;
            }
            *sp = 0;
            val = sp + 1;
        }
        trim_inplace(key);
        trim_inplace(val);
        if (!key[0]) {
            klog(L_WARN, "theme:%d: empty key", lineno);
            continue;
        }
        applied += theme_apply_key(c, key, val, path, lineno);
    }
    fclose(f);
    klog(L_INFO, "theme: %d value(s) applied from %s", applied, path);
    return applied;
}

int theme_apply_config(Config *c)
{
    if (!c->theme[0])
        return 0;
    char path[4096];
    theme_find(c->theme, path, sizeof path);
    if (!path[0]) {
        klog(L_WARN, "theme: '%s' was not found — a name is looked up as %s.theme in "
                     "$XDG_CONFIG_HOME/kai-term/themes, ~/.config/kai-term/themes and "
                     "/etc/kai-term/themes; a value with a '/' is a path", c->theme, c->theme);
        return 0;
    }
    klog(L_INFO, "theme: '%s' resolves to %s", c->theme, path);
    return theme_apply_file(c, path);
}

/* --import-theme translates NAMES and COLOUR SPELLINGS from another terminal's file and writes a Kai-Term
   .theme. Two rules: never guess a format (the sniff looks for a marker in the content, not the name) and
   never guess a colour (a value Kai-Term cannot read is refused and logged, not approximated). */

const char *theme_format_name(int fmt)
{
    switch (fmt) {
    case THF_KAI:      return "kai-term/kitty";
    case THF_XRES:     return "Xresources";
    case THF_ALACRITTY: return "alacritty";
    case THF_KONSOLE:  return "Konsole";
    case THF_XFCE:     return "XFCE4 terminal";
    case THF_JSON:     return "JSON (Windows Terminal / VS Code)";
    case THF_PLIST:    return "iTerm2 plist";
    default:           return "unknown";
    }
}

/* Case-insensitive substring. _GNU_SOURCE is on for the whole build (Makefile), so strcasestr is declared. */
static int ci_has(const char *hay, const char *needle)
{
    return strcasestr(hay, needle) != NULL;
}

/* Does this line read as `key value` / `key = value` with a key a Kai-Term theme knows? That is the only
   positive evidence for calling an unmarked file a theme; prose about colours must not pass for one. */
static int kai_key_known(const char *key)
{
    for (size_t i = 0; i < sizeof THEME_KEYS / sizeof THEME_KEYS[0]; i++)
        if (!strcmp(key, THEME_KEYS[i].name))
            return 1;
    for (size_t i = 0; i < sizeof THEME_DROPPED / sizeof THEME_DROPPED[0]; i++)
        if (!strcmp(key, THEME_DROPPED[i].name))
            return 1;
    return 0;
}

/* One line of any line-based theme file: 0 for blank or comment, 1 for a section header, 2 for a pair. The
   FIRST of '=', ':' or a space splits, which keeps `XTerm*faceName: DejaVu Sans Mono` one pair. */
static int split_line(char *line, char *sec, size_t seccap, char *key, size_t kcap,
                      char *val, size_t vcap)
{
    trim_inplace(line);
    if (!line[0])
        return 0;
    if (line[0] == '!' || line[0] == ';' || line[0] == '#')
        return 0;                       /* the comment spelling of every format read here */
    if (line[0] == '[') {
        char *close = strchr(line, ']');
        if (!close)
            return 0;
        *close = 0;
        memmove(line, line + 1, strlen(line + 1) + 1);   /* the bracket is not part of the name */
        trim_inplace(line);
        snprintf(sec, seccap, "%s", line);
        return 1;
    }
    /* The separator is the first '=' or ':' in the line; only when there is neither does a space split a
   kitty-style `key value`. Splitting on the first of the three breaks alacritty's
   `background = '#1d1f21'`, whose value then starts with '='. */
    char *sep = NULL;
    for (char *p = line; *p; p++) {
        if (*p == '=' || *p == ':') {
            sep = p;
            break;
        }
    }
    if (!sep) {
        for (char *p = line; *p; p++)
            if (isspace((unsigned char)*p)) {
                sep = p;
                break;
            }
    }
    if (!sep)
        return 0;
    *sep = 0;
    char *v = sep + 1;
    trim_inplace(line);
    while (*v && isspace((unsigned char)*v))
        v++;
    snprintf(key, kcap, "%s", line);
    snprintf(val, vcap, "%s", v);
    return key[0] ? 2 : 0;
}

/* Xresources comments with '!' — at the start of a line, or after whitespace. The same shape as
   config_strip_comment's '#' rule, and for the same reason: a marker that is part of a value must survive. */
static void strip_bang(char *line)
{
    for (char *p = line; *p; p++) {
        if (*p != '!')
            continue;
        if (p == line || isspace((unsigned char)p[-1])) {
            *p = 0;
            return;
        }
    }
}

int theme_sniff(const char *text)
{
    if (!text || !text[0])
        return THF_UNKNOWN;

    /* An XML plist: the marker is checked before anything else because its colour keys are English words
      (`Red Component`) that no other format uses, and because reading one as text would produce garbage. */
    if (ci_has(text, "<plist") || ci_has(text, "Red Component"))
        return THF_PLIST;

    const char *p = text;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (*p == '{')
        return THF_JSON;

    if (ci_has(text, "[colors.primary]") || ci_has(text, "[colors.normal]") ||
        ci_has(text, "[colors.bright]"))
        return THF_ALACRITTY;

    /* XFCE before Konsole: both spell a colour as `ColorBackground=`, but only terminalrc carries a
       semicolon-separated palette, and that marker is decisive. */
    if (ci_has(text, "ColorPalette="))
        return THF_XFCE;
    if (ci_has(text, "[Color Scheme]") || ci_has(text, "[Background]") ||
        ci_has(text, "[Color0Intense]") || ci_has(text, "ColorBackground="))
        return THF_KONSOLE;

    /* Xresources: a resource name carries a '*' (XTerm*, UXTerm*, *color0) and the separator is ':'. */
    {
        const char *q = text;
        while (q && *q) {
            const char *nl = strchr(q, '\n');
            size_t len = nl ? (size_t)(nl - q) : strlen(q);
            if (len && memchr(q, '*', len) && memchr(q, ':', len)) {
                char probe[256];
                if (len < sizeof probe) {
                    memcpy(probe, q, len);
                    probe[len] = 0;
                    if (probe[0] != '!' && probe[0] != '/' && probe[0] != '#')
                        return THF_XRES;
                }
            }
            q = nl ? nl + 1 : NULL;
        }
    }

    /* kai-term's own spelling, and kitty's: at least one line whose key this file reader knows. */
    {
        char copy[8192];
        snprintf(copy, sizeof copy, "%s", text);
        char *save = NULL;
        for (char *ln = strtok_r(copy, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
            char sec[64], key[128], val[256];
            config_strip_comment(ln);
            if (split_line(ln, sec, sizeof sec, key, sizeof key, val, sizeof val) != 2)
                continue;
            if (kai_key_known(key))
                return THF_KAI;
        }
    }

    return THF_UNKNOWN;
}

/* ---------- colour spellings ---------- */

/* xfconf stores 16-bit components: `#1c1c1c1c1c1c` is R=G=B=0x1c1c. Fold one shift per component — handing
   those digits to cfg_parse_color keeps the LOW 32 bits and yields a plausible, wrong colour. */
static int hex12_color(const char *s, uint32_t *out)
{
    if (*s == '#')
        s++;
    if (strlen(s) != 12)
        return 0;
    for (int i = 0; i < 12; i++)
        if (!isxdigit((unsigned char)s[i]))
            return 0;
    unsigned c[3];
    for (int i = 0; i < 3; i++) {
        char g[5];
        memcpy(g, s + i * 4, 4);
        g[4] = 0;
        c[i] = (unsigned)strtoul(g, NULL, 16) >> 8;   /* 0xffff >> 8 is 0xff: a byte, never 256 */
    }
    *out = 0xff000000u | (c[0] << 16) | (c[1] << 8) | c[2];
    return 1;
}

/* Konsole writes three DECIMAL bytes: `Color=237,98,104`. */
static int triple_color(const char *s, uint32_t *out)
{
    unsigned c[3];
    int n = 0;
    const char *p = s;
    while (n < 3) {
        char *end;
        unsigned long d = strtoul(p, &end, 10);
        if (end == p || d > 255)
            return 0;
        c[n++] = (unsigned)d;
        p = end;
        if (*p != ',')
            break;
        p++;
    }
    if (n != 3)
        return 0;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (*p)
        return 0;
    *out = 0xff000000u | (c[0] << 16) | (c[1] << 8) | c[2];
    return 1;
}

/* One colour in any spelling the imported formats use. Quotes come off first (alacritty writes
   `'#1d1f21'`). Anything left that is not a colour is refused, so `red`, `CellForeground` and `null` never
   become a number. */
static int import_color(const char *in, uint32_t *out)
{
    char buf[64];
    const char *s = in;
    while (*s && isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1]))
        n--;
    if (n >= 2 && (s[0] == '\'' || s[0] == '"') && s[n - 1] == s[0]) {
        s++;
        n -= 2;
    }
    if (n == 0 || n >= sizeof buf)
        return 0;
    memcpy(buf, s, n);
    buf[n] = 0;

    if (hex12_color(buf, out))
        return 1;
    if (triple_color(buf, out))
        return 1;
    uint32_t v = cfg_parse_color(buf, 0x00000000u);
    if (v == 0x00000000u)
        return 0;
    *out = v;
    return 1;
}

/* ---------- the slots a mapped name can fill ---------- */
enum { SL_FG, SL_BG, SL_CUR, SL_SEL, SL_PAL };

static void drop_key(ThemeVals *v, int lineno, const char *key, const char *why)
{
    v->n_drop++;
    klog(L_INFO, "import:%d: '%s' has nowhere to go (%s)", lineno, key, why);
}

/* A theme's own name, from its `name` / `Description` key. ThemeVals.name holds 127 characters and a value
   can reach 1023, so a long name IS cut — and it decides the file an imported theme is written under, so
   the log says how long it was and how much survived. */
static void set_name(ThemeVals *v, const char *val, int lineno)
{
    size_t len = strlen(val);
    if (len >= sizeof v->name) {
        klog(L_WARN, "import:%d: the theme's name is %d bytes — only the first %d are kept",
             lineno, (int)len, (int)sizeof v->name - 1);
        len = sizeof v->name - 1;
    }
    memcpy(v->name, val, len);
    v->name[len] = 0;
}

/* First value wins, and a later one is reported rather than silently preferred: a JSON file can hold several
   schemes, and "the first scheme in the file" is a rule the user can predict. */
static void put_slot(ThemeVals *v, int slot, int idx, uint32_t col, int lineno, const char *key)
{
    uint32_t *where = slot == SL_FG ? &v->fg
                    : slot == SL_BG ? &v->bg
                    : slot == SL_CUR ? &v->cur
                    : slot == SL_SEL ? &v->sel : &v->col[idx];
    if (*where) {
        v->n_drop++;
        klog(L_INFO, "import:%d: '%s' ignored — that colour was already set earlier in the file", lineno, key);
        return;
    }
    *where = col;
    v->n_set++;
}

/* The 8 names the "normal"/"bright" groups use, in palette order. alacritty spells magenta both ways. */
static int palette_name(const char *key, int *idx)
{
    static const char *const n8[] = {
        "black", "red", "green", "yellow", "blue", "purple", "cyan", "white"
    };
    for (int i = 0; i < 8; i++)
        if (!strcasecmp(key, n8[i])) {
            *idx = i;
            return 1;
        }
    if (!strcasecmp(key, "magenta")) {
        *idx = 5;
        return 1;
    }
    return 0;
}

/* `colorN` / `color0` .. `color15` -> the palette entry. */
static int colorN_name(const char *key, int *idx)
{
    if (strncasecmp(key, "color", 5) != 0)
        return 0;
    char *end;
    long n = strtol(key + 5, &end, 10);
    if (end == key + 5 || *end || n < 0 || n > 15)
        return 0;
    *idx = (int)n;
    return 1;
}

/* Konsole names the colour in the SECTION ([Color1], [Background]) and puts the value under `Color`; the
   legacy schema names it in the KEY under [Color Scheme]. One answer for both. */
static int konsole_slot(const char *sec, const char *key, int *slot, int *idx)
{
    const char *what = key;
    if (!strcasecmp(key, "color") && sec && *sec)
        what = sec;
    if (!strcasecmp(what, "background")) {
        *slot = SL_BG;
        return 1;
    }
    if (!strcasecmp(what, "foreground")) {
        *slot = SL_FG;
        return 1;
    }
    if (!strcasecmp(what, "cursor") || !strcasecmp(what, "cursorcolors") ||
        !strcasecmp(what, "colorcursor")) {
        *slot = SL_CUR;
        return 1;
    }
    if (colorN_name(what, idx)) {
        *slot = SL_PAL;
        return 1;
    }
    /* `Color0Intense` .. `Color7Intense` are Konsole's BRIGHT eight: palette entries 8-15, not duplicates of
   Color0-7. The tail is compared only when the name is long enough to have one. */
    size_t wl = strlen(what);
    if (wl > 7 && !strcasecmp(what + wl - 7, "Intense")) {
        char base[64];
        snprintf(base, sizeof base, "%.*s", (int)(wl - 7), what);
        if (colorN_name(base, idx)) {
            *idx += 8;
            *slot = SL_PAL;
            return 1;
        }
    }
    return 0;
}

/* One key/value pair, mapped for the format being read. Returns 1 when a colour was stored. */
static int import_kv(int fmt, const char *sec, const char *key, const char *val,
                     ThemeVals *v, int lineno)
{
    int slot = -1, idx = -1;
    char bare[160];                       /* key with an Xresources `XTerm*` prefix removed */

    switch (fmt) {
    case THF_KAI:
        if (!strcmp(key, "name")) {
            set_name(v, val, lineno);
            return 0;
        }
        if (colorN_name(key, &idx)) {
            slot = SL_PAL;
        } else if (!strcmp(key, "foreground")) {
            slot = SL_FG;
        } else if (!strcmp(key, "background")) {
            slot = SL_BG;
        } else if (!strcmp(key, "cursor") || !strcmp(key, "cursor_color")) {
            slot = SL_CUR;
        } else if (!strcmp(key, "selection_background") || !strcmp(key, "selection_color")) {
            slot = SL_SEL;
        }
        break;

    case THF_XRES: {
        /* `XTerm*color1`, `UXTerm*foreground`, `*background`: the resource class is noise to Kai-Term. */
        const char *star = strrchr(key, '*');
        snprintf(bare, sizeof bare, "%s", star ? star + 1 : key);
        if (colorN_name(bare, &idx))
            slot = SL_PAL;
        else if (!strcasecmp(bare, "foreground"))
            slot = SL_FG;
        else if (!strcasecmp(bare, "background"))
            slot = SL_BG;
        else if (!strcasecmp(bare, "cursorcolor") || !strcasecmp(bare, "cursorColor"))
            slot = SL_CUR;
        break;
    }

    case THF_ALACRITTY:
        if (!sec[0]) {
            drop_key(v, lineno, key, "alacritty colour keys live under a [colors.*] table");
            return 0;
        }
        if (!strcasecmp(sec, "colors.primary")) {
            if (!strcasecmp(key, "background")) slot = SL_BG;
            else if (!strcasecmp(key, "foreground")) slot = SL_FG;
        } else if (!strcasecmp(sec, "colors.cursor")) {
            if (!strcasecmp(key, "cursor"))
                slot = SL_CUR;
            else if (!strcasecmp(key, "text")) {
                drop_key(v, lineno, key, "Kai-Term draws the glyph under the cursor in the cell's own colour");
                return 0;      /* answered: counting it again below would report one key as two */
            }
        } else if (!strcasecmp(sec, "colors.selection")) {
            if (!strcasecmp(key, "background"))
                slot = SL_SEL;
            else if (!strcasecmp(key, "text")) {
                drop_key(v, lineno, key, "Kai-Term draws selected text in the cell's own colour");
                return 0;
            }
        } else if (!strcasecmp(sec, "colors.dim") || !strcasecmp(sec, "colors.vt_pale")) {
            drop_key(v, lineno, key, "Kai-Term has one ink per cell, not a dim variant of the palette");
            return 0;
        } else if (!strcasecmp(sec, "colors.normal") || !strcasecmp(sec, "colors.bright")) {
            if (palette_name(key, &idx)) {
                slot = SL_PAL;
                if (!strcasecmp(sec, "colors.bright"))
                    idx += 8;
            }
        }
        break;

    case THF_KONSOLE:
        if (!strcasecmp(key, "description") && (!strcasecmp(sec, "general") || !sec[0])) {
            set_name(v, val, lineno);
            return 0;
        }
        if (konsole_slot(sec, key, &slot, &idx))
            break;
        if (!strcasecmp(key, "backgroundintense") || !strcasecmp(sec, "BackgroundIntense") ||
            !strcasecmp(key, "foregroundintense") || !strcasecmp(sec, "ForegroundIntense")) {
            drop_key(v, lineno, key, "Kai-Term has one ink and one paper, not a bold variant of each");
            return 0;
        }
        break;

    case THF_XFCE:
        if (!strcasecmp(key, "colorbackground"))
            slot = SL_BG;
        else if (!strcasecmp(key, "colorforeground"))
            slot = SL_FG;
        else if (!strcasecmp(key, "colorcursor"))
            slot = SL_CUR;
        else if (!strcasecmp(key, "colorpalette")) {
            /* `#rrggbb;#rrggbb;...` — the 16 entries in palette order. A short palette fills the entries it
               names and leaves the rest at Kai-Term's own default rather than inventing colours. */
            char copy[1024];
            snprintf(copy, sizeof copy, "%s", val);
            char *save = NULL;
            int n = 0;
            for (char *tok = strtok_r(copy, ";", &save); tok && n < 16;
                 tok = strtok_r(NULL, ";", &save)) {
                uint32_t col;
                if (import_color(tok, &col)) {
                    put_slot(v, SL_PAL, n, col, lineno, key);
                    n++;
                }
            }
            if (n == 0)
                drop_key(v, lineno, key, "no colour in the palette list");
            return n > 0;
        }
        break;

    case THF_JSON:
        if (!strcasecmp(key, "name")) {
            snprintf(v->name, sizeof v->name, "%s", val);
            return 0;
        }
        if (!strcasecmp(key, "background"))
            slot = SL_BG;
        else if (!strcasecmp(key, "foreground"))
            slot = SL_FG;
        else if (!strcasecmp(key, "cursorcolor"))
            slot = SL_CUR;
        else if (!strcasecmp(key, "selectionbackground"))
            slot = SL_SEL;
        else if (!strncasecmp(key, "bright", 6) && palette_name(key + 6, &idx)) {
            idx += 8;                 /* brightBlack .. brightWhite -> entries 8-15 */
            slot = SL_PAL;
        }
        else if (palette_name(key, &idx))
            slot = SL_PAL;          /* black .. white -> entries 0-7 */
        break;

    default:
        return 0;
    }

    if (slot < 0) {
        for (size_t i = 0; i < sizeof THEME_DROPPED / sizeof THEME_DROPPED[0]; i++) {
            if (!strcasecmp(key, THEME_DROPPED[i].name)) {
                drop_key(v, lineno, key, THEME_DROPPED[i].why);
                return 0;
            }
        }
        v->n_drop++;
        klog(L_WARN, "import:%d: '%s' is not a colour Kai-Term has a place for", lineno, key);
        return 0;
    }

    uint32_t col;
    if (!import_color(val, &col)) {
        v->n_drop++;
        klog(L_WARN, "import:%d: '%s' is not a colour Kai-Term can read (#RRGGBB, RRGGBB, r,g,b or "
             "#RRRRGGGGBBBB): '%s'", lineno, key, val);
        return 0;
    }
    put_slot(v, slot, idx, col, lineno, key);
    return 1;
}

/* ---------- the JSON reader ---------- */
/* Deliberately small: it walks the text for `"key" : "value"` pairs and nothing else, tracking brace depth.
   A value that is not a string is skipped, which is what stops `"background": "red"` becoming a colour. */
typedef struct JsonPair {
    char key[128];
    char val[256];
    int  depth;
} JsonPair;

static const char *json_string(const char *p, char *out, size_t cap)
{
    /* p is at the opening quote. Returns the position just past the closing one, or NULL. */
    if (*p != '"')
        return NULL;
    p++;
    size_t n = 0;
    while (*p && *p != '"') {
        char c = *p;
        if (c == '\\' && p[1]) {
            p++;
            c = *p;
        }
        if (n + 1 < cap)
            out[n++] = c;
        p++;
    }
    out[n] = 0;
    if (*p != '"')
        return NULL;
    return p + 1;
}

static int json_pairs(const char *text, JsonPair *pairs, int cap)
{
    int n = 0, depth = 0;
    const char *p = text;
    while (*p) {
        if (*p == '{') {
            depth++;
            p++;
            continue;
        }
        if (*p == '}') {
            depth--;
            p++;
            continue;
        }
        if (*p != '"') {
            p++;
            continue;
        }
        char key[128];
        const char *after = json_string(p, key, sizeof key);
        if (!after) {
            p++;
            continue;
        }
        const char *q = after;
        while (*q && isspace((unsigned char)*q))
            q++;
        if (*q != ':') {            /* a string in an array, or a value we already consumed */
            p = after;
            continue;
        }
        q++;
        while (*q && isspace((unsigned char)*q))
            q++;
        char val[256];
        if (*q != '"') {            /* null / true / false / a number: not a colour, skip it */
            p = after;
            continue;
        }
        const char *vend = json_string(q, val, sizeof val);
        if (!vend) {
            p = after;
            continue;
        }
        if (n < cap) {
            snprintf(pairs[n].key, sizeof pairs[n].key, "%s", key);
            snprintf(pairs[n].val, sizeof pairs[n].val, "%s", val);
            pairs[n].depth = depth;
            n++;
        }
        p = vend;
    }
    return n;
}

/* ---------- parsing one whole file ---------- */

int theme_parse_text(const char *text, int fmt, ThemeVals *v, const char *src_path)
{
    if (!text || !v)
        return -1;
    memset(v, 0, sizeof *v);

    if (fmt == THF_PLIST || fmt == THF_UNKNOWN)
        return -1;                       /* the caller says why; nothing is guessed */

    if (fmt == THF_JSON) {
        /* The scheme's own `name` is the one that sits with its colours: take the nearest `name` at the same
           depth BEFORE the first colour, so a profile name elsewhere in a settings.json is not mistaken for
           the scheme's. */
        JsonPair *pairs = malloc(sizeof(JsonPair) * 512);
        if (!pairs) {
            klog(L_WARN, "import: out of memory reading a JSON theme");
            return -1;
        }
        int n = json_pairs(text, pairs, 512);
        int first_col = -1;
        for (int i = 0; i < n; i++) {
            if (strcasecmp(pairs[i].key, "name")) {
                first_col = i;
                break;
            }
        }
        for (int i = 0; i < n; i++) {
            if (!strcasecmp(pairs[i].key, "name") && first_col >= 0 &&
                pairs[i].depth == pairs[first_col].depth && i < first_col) {
                /* The cap is spelled out: a JSON value is up to 255 bytes and ThemeVals.name is 128, and a
                   theme name longer than that is truncated on purpose rather than silently. */
                snprintf(v->name, sizeof v->name, "%.127s", pairs[i].val);
                break;
            }
        }
        int lineno = 0;
        for (int i = 0; i < n; i++)
            import_kv(THF_JSON, "", pairs[i].key, pairs[i].val, v, ++lineno);
        free(pairs);
    } else {
        char sec[128] = "";
        int lineno = 0;
        const char *p = text;
        while (p && *p) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            char line[2048];
            if (len >= sizeof line)
                len = sizeof line - 1;
            memcpy(line, p, len);
            line[len] = 0;
            lineno++;
            /* '#' ends a trailing note in the two formats that use it; the shared rule keeps a `#RRGGBB`
               value intact. Xresources comments with '!'. */
            if (fmt == THF_KAI || fmt == THF_ALACRITTY)
                config_strip_comment(line);
            else if (fmt == THF_XRES)
                strip_bang(line);
            char key[160], val[1024];
            int kind = split_line(line, sec, sizeof sec, key, sizeof key, val, sizeof val);
            if (kind == 2)
                import_kv(fmt, sec, key, val, v, lineno);
            p = nl ? nl + 1 : NULL;
        }
    }

    if (v->n_set == 0) {
        klog(L_WARN, "import: %s holds no colour Kai-Term can use (%d key(s) read, %d dropped)",
             src_path ? src_path : "(text)", v->n_drop, v->n_drop);
        return -1;
    }
    return 0;
}

/* ---------- writing a Kai-Term .theme ---------- */

static void write_color(FILE *f, const char *key, uint32_t c)
{
    if ((c >> 24) == 0xff)
        fprintf(f, "%s = #%06x\n", key, c & 0xffffff);
    else
        fprintf(f, "%s = #%08x\n", key, c);
}

int theme_write_file(const ThemeVals *v, const char *path, const char *src_path, int fmt)
{
    /* temp + rename: a theme that fails half-written (disk full, quota) would otherwise sit in the themes
       directory and be LOADED by the next launch as if it were complete. */
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        klog(L_WARN, "import: cannot write %s", tmp);
        return -1;
    }
    fprintf(f, "# Kai-Term theme, imported from %s\n", src_path ? src_path : "?");
    fprintf(f, "# Source format: %s\n", theme_format_name(fmt));
    fprintf(f, "# Colours only — fonts, sizes and key bindings live in kai-term.conf and kai-term.bind.\n");
    if (v->name[0])
        fprintf(f, "name = %s\n", v->name);
    if (v->fg)
        write_color(f, "foreground", v->fg);
    if (v->bg)
        write_color(f, "background", v->bg);
    if (v->cur)
        write_color(f, "cursor_color", v->cur);
    if (v->sel)
        write_color(f, "selection_color", v->sel);
    for (int i = 0; i < 16; i++) {
        if (!v->col[i])
            continue;
        char key[16];
        snprintf(key, sizeof key, "color%d", i);
        write_color(f, key, v->col[i]);
    }
    if (ferror(f) || fclose(f) != 0) {
        remove(tmp);
        klog(L_WARN, "import: writing %s failed", tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        remove(tmp);
        klog(L_WARN, "import: cannot move %s into place: %s", tmp, strerror(errno));
        return -1;
    }
    return 0;
}

/* ---------- where it goes, and what it is called ---------- */

/* mkdir -p for the two levels a theme needs. Returns 0 when the directory exists afterwards. */
static int mkdir_p(const char *path)
{
    char buf[4096];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = 0;
        if (mkdir(buf, 0755) != 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST)
        return -1;
    struct stat st;
    return (stat(buf, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : -1;
}

void theme_dest_dir(char *out, size_t cap)
{
    out[0] = 0;
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        snprintf(out, cap, "%s/kai-term/themes", xdg);
        if (mkdir_p(out) == 0)
            return;
    }
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(out, cap, "%s/.config/kai-term/themes", home);
        if (mkdir_p(out) == 0)
            return;
    }
    out[0] = 0;
}

void theme_name_of_path(const char *path, char *out, size_t cap)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(out, cap, "%s", base);
    size_t n = strlen(out);
    if (n > 6 && !strcmp(out + n - 6, ".theme"))
        out[n - 6] = 0;
}

/* A file name out of a theme's own `name` key. Anything that is not a letter, a digit, '.', '_' or '-'
   becomes '-': `13th Floor` -> `13th-Floor`, and a name carrying a '/' cannot write outside the themes
   directory. Leading/trailing '-' and '.' are dropped; an empty result means "use the source file's name". */
static void safe_name(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (const char *p = in; *p && n + 1 < cap; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c))
            out[n++] = (char)c;
        else if (c == '.' || c == '_' || c == '-') {
            if (n && out[n - 1] != '-')
                out[n++] = (char)c;
        } else if (n && out[n - 1] != '-')
            out[n++] = '-';
    }
    while (n && (out[n - 1] == '-' || out[n - 1] == '.'))
        n--;
    out[n] = 0;
    if (n && out[0] == '.')          /* a hidden file is not a theme the search will find */
        out[0] = '-';
}

int theme_import(const char *src_path, char *out_path, size_t cap, int overwrite)
{
    out_path[0] = 0;
    if (!src_path || !src_path[0]) {
        klog(L_WARN, "import: no file named");
        return -1;
    }
    struct stat st;
    if (stat(src_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        klog(L_WARN, "import: %s is not a regular file I can read", src_path);
        return -1;
    }
    if (st.st_size > (off_t)(1 << 20)) {
        klog(L_WARN, "import: %s is %lld bytes — no theme file is that big, so this is not one",
             src_path, (long long)st.st_size);
        return -1;
    }
    FILE *f = fopen(src_path, "r");
    if (!f) {
        klog(L_WARN, "import: cannot open %s", src_path);
        return -1;
    }
    char *text = malloc((size_t)st.st_size + 1);
    if (!text) {
        fclose(f);
        klog(L_WARN, "import: out of memory reading %s", src_path);
        return -1;
    }
    size_t got = fread(text, 1, (size_t)st.st_size, f);
    fclose(f);
    text[got] = 0;

    int fmt = theme_sniff(text);
    if (fmt == THF_PLIST) {
        klog(L_WARN, "import: %s is an iTerm2 plist — refused. Its colours are 0.0-1.0 floats in XML and "
             "there is no iTerm2 here to check a reader against. Read instead: kai-term/kitty, Xresources, "
             "alacritty, Konsole, XFCE4 terminal, JSON (Windows Terminal / VS Code).", src_path);
        free(text);
        return -1;
    }
    if (fmt == THF_UNKNOWN) {
        klog(L_WARN, "import: %s is not a theme I recognise. Looked for: '<plist'/Red Component (iTerm2), "
             "a leading '{' (JSON), [colors.primary] (alacritty), ColorPalette= (XFCE4), [Color Scheme]/"
             "[Background]/ColorBackground= (Konsole), a `Name*key: value` line (Xresources), or a "
             "foreground/background/colorN key (kai-term, kitty).", src_path);
        free(text);
        return -1;
    }

    ThemeVals v;
    if (theme_parse_text(text, fmt, &v, src_path) != 0) {
        free(text);
        return -1;
    }
    free(text);

    char dir[4096];
    theme_dest_dir(dir, sizeof dir);
    if (!dir[0]) {
        klog(L_WARN, "import: nowhere to write — neither $XDG_CONFIG_HOME nor $HOME gives a themes directory");
        return -1;
    }

    char nm[128] = "";
    if (v.name[0])
        safe_name(v.name, nm, sizeof nm);
    if (!nm[0]) {
        const char *base = strrchr(src_path, '/');
        base = base ? base + 1 : src_path;
        char stem[128];
        snprintf(stem, sizeof stem, "%s", base);
        char *dot = strrchr(stem, '.');
        if (dot && dot != stem)
            *dot = 0;
        safe_name(stem, nm, sizeof nm);
    }
    if (!nm[0]) {
        klog(L_WARN, "import: %s gives no usable theme name", src_path);
        return -1;
    }

    snprintf(out_path, cap, "%s/%s.theme", dir, nm);
    if (!overwrite && stat(out_path, &st) == 0) {
        klog(L_INFO, "import: %s already exists — asking before overwriting", out_path);
        return 1;
    }
    if (theme_write_file(&v, out_path, src_path, fmt) != 0) {
        out_path[0] = 0;
        return -1;
    }
    klog(L_INFO, "import: %s (%s) -> %s: %d colour(s), %d key(s) dropped",
         src_path, theme_format_name(fmt), out_path, v.n_set, v.n_drop);
    return 0;
}
