/* Kai-Term — themes. A theme is a file of COLOURS and nothing else: fonts, sizes, key bindings and every
   other setting stay in kai-term.conf and kai-term.bind, where a user expects to find them.
   The syntax is the one Kai-Term already reads — `key = value`, the same '#' comment rule (config_strip_comment,
   shared on purpose: it was wrong once and ate every `#RRGGBB` value, s99) — and kitty's `key value` spelling
   is accepted too, so an existing kitty theme loads unchanged. The colour syntax is cfg_parse_color's, also
   shared: one syntax, one parser, so a theme can never accept a form the config file rejects.
   Precedence (the user's answer, s102): the theme is applied AFTER kai-term.conf has been read in full, and
   only for the colours the settings file did not mention itself. That is what makes a `background =` line win
   wherever it sits in the file — above or below the `theme =` line — and it is why config.c records the names
   the settings file set (config_note_set) instead of applying a theme line by line.
   Only palette entries 0-15 are themeable. The 6x6x6 cube (16-231) and the grey ramp (232-255) are computed:
   an application that asks for 196 means the cube's brightest red, and a theme that could repaint the cube
   would make every published 256-colour chart a lie about that terminal. */
#include "kai-term.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>    /* stat(): a theme has to be a real regular file, same rule as a config file */

/* The names a theme file may set. ONE table, and tests/test_vt.c reads these strings out of this file and
   compares them with config/kai-term.theme — the same technique that pins kai-term.conf and kai-term.bind,
   so a name added here without being documented fails a check.
   The spellings are kitty's, with Kai-Term's own name beside each as an alias: that is what makes the kitty
   theme corpus drop-in. `canonical` is the kai-term.conf name a user may have set instead, and it is what the
   precedence check looks up — a theme's `cursor` must step aside for a `cursor_color =` line. */
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
