/* Kai-Term — configuration. Two files, one `key = value` syntax: kai-term.conf holds the settings (how the
   terminal looks and behaves), kai-term.bind holds the key bindings. Both are searched for in
   $XDG_CONFIG_HOME/kai-term, then ~/.config/kai-term, then /etc/kai-term — first one found wins, and none
   found anywhere means the built-in defaults below. */
#include "kai-term.h"

#include <ctype.h>
#include <errno.h>
#include <strings.h>     /* strcasecmp for the boolean config values */
#include <sys/stat.h>    /* stat(): config_find() only accepts a real regular file */
#include <unistd.h>

uint32_t cfg_parse_color(const char *s, uint32_t fallback)
{
    while (isspace((unsigned char)*s))
        s++;
    if (s[0] == '#')
        s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    char *end;
    unsigned long long v = strtoull(s, &end, 16);
    if (end == s)
        return fallback;
    while (isspace((unsigned char)*end))
        end++;
    if (*end)
        return fallback;   /* trailing garbage */
    if (strlen(s) <= 6)
        v |= 0xff000000ull;   /* no alpha given: opaque */
    return (uint32_t)v;
}

void config_default(Config *c)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->font, sizeof(c->font), "monospace");
    c->font_size = 14;
    c->cols = 80;
    c->rows = 24;
    c->scrollback = 2000;
    c->fg = DEF_FG;
    c->bg = DEF_BG;
    c->cursor_color = CURSOR_COL;
    c->selection_color = SEL_BG;
    c->opacity = 1.0f;
    c->shell[0] = 0;           /* empty => $SHELL */
    /* The integration script is handed to a recognised shell only (fish/bash/zsh). On by default: it changes
       nothing a user sees until the file manager follows a navigation, and it is the only way to move the
       shell without touching the user's half-typed line. shell_integration = 0 starts every shell exactly as
       earlier sessions did. */
    c->shell_integration = 1;
    c->working_dir[0] = 0;     /* empty => $HOME */
    /* xdg-shell app_id: window rules and taskbar grouping key off it, so the default
       reads like the display name. DEF_APP_ID in kai-term.h is the single source (see below). */
    snprintf(c->app_id, sizeof(c->app_id), DEF_APP_ID);
    c->clear_scrollback = 1;   /* CSI 3 J drops the scrollback (on by default) */
    c->copy_on_release = 0;    /* selecting does not copy unless the user asks (Ctrl+Shift+C does) */
    /* The mouse inside a full-screen program that never asked for the mouse (nano, less, man). Both on:
       in that state the wheel and a click used to do NOTHING at all (the alt grid has no scrollback and a
       mouse report would be typed into the document), so turning them into cursor keys costs the user
       nothing that worked before. See Config in kai-term.h for what each one does and what it can surprise. */
    c->wheel_sends_keys = 1;
    c->click_moves_cursor = 1;
    /* Double click selects a word, triple click the whole logical line. On by default: the user asked for
       it that way, and a single click still behaves exactly as before. */
    c->click_selects_word = 1;
    /* The cursor: a thin bar (there is no `cursor_style` key — the user asked for one look) that is on for
       1000 ms and off for 1000 ms, the classic rhythm. 0 turns the blink off and an application cannot turn
       it back on (see the CURBLINK enum comment). 2 px is the width that reads as a hairline at 14px but is
       still one whole pixel on a HiDPI-free 1x display; render_cursor() clamps it to a third of a cell. */
    c->cursor_blink = 1000;
    c->cursor_width = 2;
    c->repeat_delay = 300;     /* ms before a held key repeats (0 = Kai-Term does not repeat) */
    c->repeat_rate = 30;       /* repeats per second once repeating */
    /* A few pixels of margin on every side. Compositors round the window corners (niri does), and with
       the grid flush against the edge the corner glyphs get clipped. 0 restores the old layout. */
    c->padding = 8;
    /* Multiplexing defaults: the GNOME-terminal-style tab keys. Every one of them is a config string,
       so a user can rebind or unbind (`key_new_tab = none`) any action. */
    c->max_tabs = 8;
    keybind_parse("Ctrl+Shift+T", &c->key_new_tab);
    keybind_parse("Ctrl+Shift+W", &c->key_close_tab);
    keybind_parse("Ctrl+Shift+Right", &c->key_next_tab);
    keybind_parse("Ctrl+Shift+Left", &c->key_prev_tab);
    keymods_parse("Ctrl+Shift", &c->tab_prefix_mods);
    /* How the sessions are shown: the bar appears by itself at the second session (so a single-session
       window looks exactly like it did before multiplexing), and side-by-side view shows at most 4
       sessions at a time — below that a pane is still wide enough to read. */
    c->tab_bar = BAR_AUTO;
    c->split_max = 4;
    keybind_parse("Ctrl+Shift+S", &c->key_toggle_layout);
    /* The in-window file manager. OFF by default: a user who never presses the toggle gets exactly the
       terminal of earlier sessions, and closing it again returns to that state. The band takes 75% of the
       window's height (layout.c clamps that against the real window and always leaves the terminal its
       floor), the preview pane is on, dotfiles are hidden at first, and browsing a directory also moves
       the shell there (fm_follow). */
    c->fm_on = 0;
    c->fm_size = FM_SIZE_DEF;
    c->fm_preview = 1;
    c->fm_show_hidden = 0;
    c->fm_follow = 1;
    /* A pane of the band is focused by CLICKING it. The pointer's position alone must not take the
       keyboard: the wheel used to hand it over before knowing whether the gesture had scrolled
       anything, so a trackpad frame with a sub-cell delta moved the keyboard and did nothing else
       (fm_wheel_focus in src/fm.c owns the rule; the user asked for click-to-focus as the default). */
    c->fm_focus_on_hover = 0;
    keybind_parse("Ctrl+Shift+F", &c->key_file_manager);
    keybind_parse("Ctrl+Shift+Up", &c->key_fm_focus);
    keybind_parse("Ctrl+Shift+Down", &c->key_fm_release);
    /* The file manager's own keys are UNMODIFIED on purpose: they are matched only while the file
       manager has the keyboard, so `Up` still reaches the shell whenever it does not. */
    keybind_parse_plain("Up",        &c->fm_key.up);
    keybind_parse_plain("Down",      &c->fm_key.down);
    keybind_parse_plain("Page_Up",   &c->fm_key.pageup);
    keybind_parse_plain("Page_Down", &c->fm_key.pagedown);
    keybind_parse_plain("Home",      &c->fm_key.home);
    keybind_parse_plain("End",       &c->fm_key.end);
    keybind_parse_plain("Left",      &c->fm_key.left);
    keybind_parse_plain("Right",     &c->fm_key.right);
    keybind_parse_plain("Return",    &c->fm_key.open);
    keybind_parse_plain("p",         &c->fm_key.path);
    keybind_parse_plain("BackSpace", &c->fm_key.parent);
    keybind_parse_plain("r",         &c->fm_key.refresh);
    /* `p` used to be the preview toggle; it is the path key now (s95), and the toggle moved to `v`.
       Both are rebindable, and `none` unbinds either. */
    keybind_parse_plain("v",         &c->fm_key.preview);
    keybind_parse_plain(".",         &c->fm_key.hidden);
    keybind_parse_plain("Escape",    &c->fm_key.quit);
    c->loglevel = L_WARN;
    /* An application MAY repaint the terminal's own colours (OSC 4/10/11/104). On by default because the user
       asked for the capability; the switch exists so it can be turned off without a rebuild (s102 question 2).
       See Config.osc_colors in kai-term.h for what it lets a program do. */
    c->osc_colors = 1;
}

void expand_tilde(const char *in, char *out, size_t cap)
{
    if (in && in[0] == '~' && (in[1] == 0 || in[1] == '/')) {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(out, cap, "%s%s", home, in + 1);
            return;
        }
    }
    snprintf(out, cap, "%s", in && in ? in : "");
}

static void trim(char *s)
{
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        e--;
    *e = 0;
    char *b = s;
    while (*b && isspace((unsigned char)*b))
        b++;
    if (b != s)
        memmove(s, b, strlen(b) + 1);
}

/* Boolean config values: 0 / false / no / off (case-insensitive) mean OFF, anything else means ON.
   Shared by clear_scrollback and copy_on_release so the two cannot drift apart. */
static int cfg_bool(const char *val)
{
    if (!strcmp(val, "0") || !strcasecmp(val, "false") ||
        !strcasecmp(val, "no") || !strcasecmp(val, "off"))
        return 0;
    return 1;
}

/* ---------- key binding specs ("Ctrl+Shift+T") ---------- */

/* Modifier names a binding may use. xkbcommon's own names (Mod1/Mod4) are included because
   `setxkbmap`-style documentation quotes them; main.c maps them back onto the real masks. */
static const struct { const char *name; unsigned bit; } mod_names[] = {
    { "ctrl",    KB_CTRL  }, { "control", KB_CTRL  },
    { "shift",   KB_SHIFT },
    { "alt",     KB_ALT   }, { "mod1",    KB_ALT   },
    { "super",   KB_SUPER }, { "mod4",    KB_SUPER }, { "win", KB_SUPER }, { "gui", KB_SUPER },
};

/* Named non-printable keys. Printable keys need no table: a one-character token IS its keysym. */
static const struct { const char *name; uint32_t sym; } key_names[] = {
    { "BackSpace", KB_KEY_BackSpace },
    { "Tab",       KB_KEY_Tab       },
    { "Return",    KB_KEY_Return    }, { "Enter", KB_KEY_Return },
    { "Escape",    KB_KEY_Escape    }, { "Esc",   KB_KEY_Escape },
    { "space",     KB_KEY_Space     }, { "Space", KB_KEY_Space  },
    { "Left",      KB_KEY_Left      }, { "Right", KB_KEY_Right },
    { "Up",        KB_KEY_Up        }, { "Down",  KB_KEY_Down  },
    { "Home",      KB_KEY_Home      }, { "End",   KB_KEY_End   },
    { "Page_Up",   KB_KEY_Prior     }, { "PageUp", KB_KEY_Prior }, { "Prior", KB_KEY_Prior },
    { "Page_Down", KB_KEY_Next      }, { "PageDown", KB_KEY_Next }, { "Next", KB_KEY_Next },
    { "Insert",    KB_KEY_Insert    }, { "Delete", KB_KEY_Delete },
    { "F1",  KB_KEY_F1  }, { "F2",  KB_KEY_F2  }, { "F3",  KB_KEY_F3  },
    { "F4",  KB_KEY_F4  }, { "F5",  KB_KEY_F5  }, { "F6",  KB_KEY_F6  },
    { "F7",  KB_KEY_F7  }, { "F8",  KB_KEY_F8  }, { "F9",  KB_KEY_F9  },
    { "F10", KB_KEY_F10 }, { "F11", KB_KEY_F11 }, { "F12", KB_KEY_F12 },
};

/* Split a binding spec on '+', trimming spaces. Returns the token count, 0 when the spec is
   malformed or absurdly long (a config line is 512 bytes, so 8 tokens of 31 is generous). */
static int split_spec(const char *spec, char tok[8][32])
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", spec);
    int n = 0;
    char *save = NULL;
    for (char *p = strtok_r(buf, "+", &save); p; p = strtok_r(NULL, "+", &save)) {
        trim(p);
        if (!p[0])
            continue;                       /* "Ctrl++" style empties are ignored */
        if (n >= 8)
            return 0;
        snprintf(tok[n++], 32, "%s", p);
    }
    return n;
}

static unsigned mod_bit_of(const char *tok)
{
    for (size_t i = 0; i < sizeof(mod_names) / sizeof(mod_names[0]); i++)
        if (!strcasecmp(tok, mod_names[i].name))
            return mod_names[i].bit;
    return 0;
}

/* A key token -> its keysym. A single printable character IS its own keysym (X keysyms for ASCII are
   the codepoints), and letters are folded to LOWERCASE: a binding names the KEY, and the Shift state
   is a modifier of its own, so "Ctrl+Shift+T" and "Ctrl+Shift+t" mean the same thing and main.c
   folds the pressed keysym the same way before comparing. */
static int sym_of(const char *tok, uint32_t *sym)
{
    for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++)
        if (!strcasecmp(tok, key_names[i].name)) {
            *sym = key_names[i].sym;
            return 1;
        }
    if (!tok[1]) {
        unsigned char c = (unsigned char)tok[0];
        if (c >= '!' && c <= '~') {           /* printable, not space */
            if (c >= 'A' && c <= 'Z')
                c = (unsigned char)(c - 'A' + 'a');
            *sym = c;
            return 1;
        }
    }
    return 0;
}

static int keybind_parse_into(const char *spec, KeyBind *out, int allow_plain)
{
    out->sym = 0;
    out->mods = 0;
    out->set = 0;
    if (!spec)
        return 0;
    char tok[8][32];
    int n = split_spec(spec, tok);
    if (n <= 0)
        return 0;
    unsigned mods = 0;
    uint32_t sym = 0;
    int have_key = 0;
    for (int i = 0; i < n; i++) {
        unsigned b = mod_bit_of(tok[i]);
        if (b) {
            mods |= b;
            continue;
        }
        uint32_t s;
        if (!sym_of(tok[i], &s)) {
            klog(L_WARN, "config: binding '%s' names an unknown key '%s' (known: a single "
                         "character, or BackSpace Tab Return Escape space Left Right Up Down Home End "
                         "Page_Up Page_Down Insert Delete F1-F12)", spec, tok[i]);
            return 0;
        }
        if (have_key) {
            klog(L_WARN, "config: binding '%s' names more than one key", spec);
            return 0;
        }
        sym = s;
        have_key = 1;
    }
    if (!have_key) {
        klog(L_WARN, "config: binding '%s' names no key", spec);
        return 0;
    }
    if (!mods && !allow_plain) {
        /* Refused on purpose: an unmodified binding would take a plain typing key away from the
           shell, which is never what the user meant to switch a tab with. */
        klog(L_WARN, "config: binding '%s' has no modifier — refusing it (an unmodified binding "
                     "would swallow normal typing)", spec);
        return 0;
    }
    out->sym = sym;
    out->mods = mods;
    out->set = 1;
    return 1;
}

int keybind_parse(const char *spec, KeyBind *out)
{
    return keybind_parse_into(spec, out, 0);
}

/* A binding that MAY be a bare key. Only the file manager's own keys use this: they are matched only
   while the file manager owns the keyboard, so they cannot swallow the shell's normal typing. */
int keybind_parse_plain(const char *spec, KeyBind *out)
{
    return keybind_parse_into(spec, out, 1);
}

/* A modifier-only spec, for the `key_tab_prefix` shortcut (prefix + a digit 1-9). */
int keymods_parse(const char *spec, unsigned *mods)
{
    *mods = 0;
    if (!spec)
        return 0;
    char tok[8][32];
    int n = split_spec(spec, tok);
    if (n <= 0)
        return 0;
    unsigned m = 0;
    for (int i = 0; i < n; i++) {
        unsigned b = mod_bit_of(tok[i]);
        if (!b) {
            klog(L_WARN, "config: '%s' is not a modifier list: '%s' is not a modifier", spec, tok[i]);
            return 0;
        }
        m |= b;
    }
    if (!m)
        return 0;
    *mods = m;
    return 1;
}

/* `key_xxx =` (empty) or none / off / no / 0 unbinds the action. A spec that does not parse keeps the
   binding that is already set (the default), so one typo cannot silently disable tab switching. */
static void set_keybind(KeyBind *kb, const char *key, const char *val)
{
    if (!val[0] || !strcmp(val, "0") || !strcasecmp(val, "none") ||
        !strcasecmp(val, "off") || !strcasecmp(val, "no")) {
        kb->sym = 0;
        kb->mods = 0;
        kb->set = 0;
        klog(L_INFO, "config: %s unbound", key);
        return;
    }
    KeyBind parsed;
    if (!keybind_parse(val, &parsed)) {
        klog(L_WARN, "config: %s = '%s' is not a usable binding (want MOD+MOD+KEY, for example "
                     "Ctrl+Shift+T); keeping the current binding", key, val);
        return;
    }
    *kb = parsed;
}

/* The same, for a file-manager key: a bare key such as `Up` or `r` is legal here. */
static void set_fm_keybind(KeyBind *kb, const char *key, const char *val)
{
    if (!val[0] || !strcmp(val, "0") || !strcasecmp(val, "none") ||
        !strcasecmp(val, "off") || !strcasecmp(val, "no")) {
        kb->sym = 0;
        kb->mods = 0;
        kb->set = 0;
        klog(L_INFO, "config: %s unbound", key);
        return;
    }
    KeyBind parsed;
    if (!keybind_parse_plain(val, &parsed)) {
        klog(L_WARN, "config: %s = '%s' is not a usable key (a single character, a name such as "
                     "Up / Return / Escape, optionally with modifiers); keeping the current binding",
             key, val);
        return;
    }
    *kb = parsed;
}

/* ---------- the two halves of the config surface ----------
   A SETTING (how the terminal looks and behaves) lives in kai-term.conf; a KEY BINDING (which key does
   which action) lives in kai-term.bind. One `key = value` syntax and one line reader serve both files;
   what differs is which NAMES each file accepts, and that is exactly what the two functions below decide.
   The split is by NAME, never by "which file was open": a kai-term.conf written before the split keeps
   working (apply() logs where the line belongs now), and the bind file is read LAST, so for a name both
   files set the bind file wins. */

/* Every key that names a key binding. Returns 1 when `key` is one of them (and has been applied), 0 when
   it is not — the caller then tries apply_setting(). */
static int apply_bind_key(Config *c, const char *key, char *val)
{
    if (!strcmp(key, "key_new_tab")) {
        set_keybind(&c->key_new_tab, key, val);
    } else if (!strcmp(key, "key_close_tab")) {
        set_keybind(&c->key_close_tab, key, val);
    } else if (!strcmp(key, "key_next_tab")) {
        set_keybind(&c->key_next_tab, key, val);
    } else if (!strcmp(key, "key_prev_tab")) {
        set_keybind(&c->key_prev_tab, key, val);
    } else if (!strcmp(key, "key_tab_prefix")) {
        /* prefix + a digit 1-9 jumps to that tab; empty / none / off disables the shortcut */
        if (!val[0] || !strcasecmp(val, "none") || !strcasecmp(val, "off") ||
            !strcasecmp(val, "no") || !strcmp(val, "0")) {
            c->tab_prefix_mods = 0;
            klog(L_INFO, "config: key_tab_prefix disabled");
        } else if (!keymods_parse(val, &c->tab_prefix_mods)) {
            klog(L_WARN, "config: key_tab_prefix = '%s' is not a modifier list (for example "
                         "Ctrl+Shift); keeping the current one", val);
        }
    } else if (!strcmp(key, "key_toggle_layout")) {
        set_keybind(&c->key_toggle_layout, key, val);
    } else if (!strcmp(key, "key_file_manager")) {
        set_keybind(&c->key_file_manager, key, val);
    } else if (!strcmp(key, "key_fm_focus")) {
        set_keybind(&c->key_fm_focus, key, val);
    } else if (!strcmp(key, "key_fm_release")) {
        set_keybind(&c->key_fm_release, key, val);
    } else if (!strcmp(key, "fm_key_up")) {
        set_fm_keybind(&c->fm_key.up, key, val);
    } else if (!strcmp(key, "fm_key_down")) {
        set_fm_keybind(&c->fm_key.down, key, val);
    } else if (!strcmp(key, "fm_key_pageup")) {
        set_fm_keybind(&c->fm_key.pageup, key, val);
    } else if (!strcmp(key, "fm_key_pagedown")) {
        set_fm_keybind(&c->fm_key.pagedown, key, val);
    } else if (!strcmp(key, "fm_key_home")) {
        set_fm_keybind(&c->fm_key.home, key, val);
    } else if (!strcmp(key, "fm_key_end")) {
        set_fm_keybind(&c->fm_key.end, key, val);
    } else if (!strcmp(key, "fm_key_left")) {
        /* Back to the parent directory — the arrow the user expects to mean "up one level". */
        set_fm_keybind(&c->fm_key.left, key, val);
    } else if (!strcmp(key, "fm_key_right")) {
        /* Into the highlighted directory; on a file it does nothing at all, which is what makes
           holding Right through a listing safe. Same on a file as fm_key_open. */
        set_fm_keybind(&c->fm_key.right, key, val);
    } else if (!strcmp(key, "fm_key_open")) {
        /* Enter: descend into the selected directory. On a file it does nothing at all — typing the
           path is fm_key_path's job now (s95), so no key in the band writes at the prompt by
           accident. */
        set_fm_keybind(&c->fm_key.open, key, val);
    } else if (!strcmp(key, "fm_key_path")) {
        /* Type the selected entry's quoted path at the shell prompt, without a newline — the only
           file-manager key that touches the prompt at all, and it never runs anything. */
        set_fm_keybind(&c->fm_key.path, key, val);
    } else if (!strcmp(key, "fm_key_parent")) {
        set_fm_keybind(&c->fm_key.parent, key, val);
    } else if (!strcmp(key, "fm_key_refresh")) {
        set_fm_keybind(&c->fm_key.refresh, key, val);
    } else if (!strcmp(key, "fm_key_preview")) {
        set_fm_keybind(&c->fm_key.preview, key, val);
    } else if (!strcmp(key, "fm_key_hidden")) {
        set_fm_keybind(&c->fm_key.hidden, key, val);
    } else if (!strcmp(key, "fm_key_quit")) {
        set_fm_keybind(&c->fm_key.quit, key, val);
    } else {
        return 0;
    }
    return 1;
}

/* Everything that is NOT a key binding. The unknown-key line stays here: a name neither table knows is
   the one case worth reporting, and it is reported once, from apply(). */
static void apply_setting(Config *c, const char *key, char *val)
{
    if (!strcmp(key, "font")) {
        snprintf(c->font, sizeof(c->font), "%s", val);
    } else if (!strcmp(key, "font_size")) {
        c->font_size = iclamp_(atoi(val), 6, 256);
    } else if (!strcmp(key, "cols")) {
        c->cols = iclamp_(atoi(val), 20, 4096);
    } else if (!strcmp(key, "rows")) {
        c->rows = iclamp_(atoi(val), 4, 4096);
    } else if (!strcmp(key, "scrollback")) {
        c->scrollback = iclamp_(atoi(val), 0, 1000000);
    } else if (!strcmp(key, "foreground")) {
        c->fg = cfg_parse_color(val, c->fg);
    } else if (!strcmp(key, "background")) {
        c->bg = cfg_parse_color(val, c->bg);
    } else if (!strcmp(key, "cursor_color")) {
        c->cursor_color = cfg_parse_color(val, c->cursor_color);
    } else if (!strcmp(key, "selection_color")) {
        /* The selection band, the tab bar's active item and the focus border — one value, three places. */
        c->selection_color = cfg_parse_color(val, c->selection_color);
    } else if (!strcmp(key, "opacity")) {
        float f = (float)atof(val);
        c->opacity = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    } else if (!strcmp(key, "shell")) {
        snprintf(c->shell, sizeof(c->shell), "%s", val);
    } else if (!strcmp(key, "shell_integration")) {
        /* Start a recognised shell (fish/bash/zsh) with Kai-Term's integration script. Off = the shell is
           started exactly as before and the file manager can only ever type `cd` at the prompt. */
        c->shell_integration = cfg_bool(val) ? 1 : 0;
    } else if (!strcmp(key, "working_dir")) {
        expand_tilde(val, c->working_dir, sizeof(c->working_dir));
    } else if (!strcmp(key, "app_id")) {
        /* The compositor's handle for the window: niri/Sway window rules, the taskbar entry and the
         "open terminal here" matches all key off it. An empty value is allowed and means "use the
         built-in default" (DEF_APP_ID), so `app_id =` never produces an empty id. */
        snprintf(c->app_id, sizeof(c->app_id), "%s", val);
        if (!c->app_id[0])
            snprintf(c->app_id, sizeof(c->app_id), DEF_APP_ID);
    } else if (!strcmp(key, "clear_scrollback")) {
        /* 1 (default) = CSI 3 J also drops the scrollback; 0 = keep the history */
        c->clear_scrollback = cfg_bool(val) ? 1 : 0;
    } else if (!strcmp(key, "copy_on_release")) {
        /* 0 (default) = releasing the drag only selects; 1 = also copy to the clipboard.
           Ctrl+Shift+C copies either way. */
        c->copy_on_release = cfg_bool(val) ? 1 : 0;
    } else if (!strcmp(key, "wheel_sends_keys")) {
        /* 1 (default) = in a full-screen program that never asked for the mouse, the wheel sends
           Up/Down presses, which is what scrolls nano/less/man. 0 = the wheel does nothing there. */
        c->wheel_sends_keys = cfg_bool(val) ? 1 : 0;
    } else if (!strcmp(key, "click_moves_cursor")) {
        /* 1 (default) = in that same state, a left click that does not drag walks the program's own
           cursor to the clicked cell. 0 = a click only ever selects text. A drag always selects, and
           Shift + click always selects, whatever this says. */
        c->click_moves_cursor = cfg_bool(val) ? 1 : 0;
    } else if (!strcmp(key, "click_selects_word")) {
        /* 1 (default) = the second and third click of a gesture select a word / a whole line.
           0 = every click selects one cell, as before this existed. */
        c->click_selects_word = cfg_bool(val) ? 1 : 0;
    } else if (!strcmp(key, "cursor_blink")) {
        /* Half-period in ms: the cursor is drawn for this long, then hidden for this long. 0 = steady.
           The upper clamp is only there to stop a typo (`cursor_blink = 100000`) leaving a cursor that
           looks broken; nothing sane needs more than a few seconds. Blinking is additionally gated on the
           window having focus and on DEC 25l — see blink_half_ms() in src/main.c. */
        c->cursor_blink = iclamp_(atoi(val), 0, 10000);
    } else if (!strcmp(key, "cursor_width")) {
        /* px of bar (and of underline). Clamped to at least 1 here and to a third of a cell in
           render_cursor(), so a huge value can never turn the bar back into a block. */
        c->cursor_width = iclamp_(atoi(val), 1, 64);
    } else if (!strcmp(key, "repeat_delay")) {
        /* 0 turns Kai-Term's own repeat off and leaves it to the compositor. */
        c->repeat_delay = iclamp_(atoi(val), 0, 5000);
    } else if (!strcmp(key, "repeat_rate")) {
        c->repeat_rate = iclamp_(atoi(val), 1, 200);
    } else if (!strcmp(key, "padding")) {
        /* px of margin around the whole grid, applied on all four sides (0 = flush with the window
           edge). The clamp keeps a huge value from shrinking the terminal away: term_grid_geometry()
           additionally caps the margin so at least one whole cell always fits. */
        c->padding = iclamp_(atoi(val), 0, 128);
    } else if (!strcmp(key, "max_tabs")) {
        /* How many sessions one window may hold. Clamped to 1..TAB_MAX: 0 would mean a terminal that
           cannot open its own first tab. */
        c->max_tabs = iclamp_(atoi(val), 1, TAB_MAX);
    } else if (!strcmp(key, "tab_bar")) {
        /* off = never draw the bar (the window title and the keyboard are the only way to see the other
           sessions), auto (default) = draw it from the second session on, always = also with one session. */
        if (!strcmp(val, "0") || !strcasecmp(val, "off") || !strcasecmp(val, "no") ||
            !strcasecmp(val, "false") || !strcasecmp(val, "never")) {
            c->tab_bar = BAR_OFF;
        } else if (!strcmp(val, "1") || !strcasecmp(val, "always") || !strcasecmp(val, "on") ||
                   !strcasecmp(val, "yes") || !strcasecmp(val, "true")) {
            c->tab_bar = BAR_ALWAYS;
        } else if (!strcasecmp(val, "auto") || !strcasecmp(val, "multiple")) {
            c->tab_bar = BAR_AUTO;
        } else {
            klog(L_WARN, "config: tab_bar = '%s' is not a value (off, auto or always); keeping %s",
                 val, c->tab_bar == BAR_OFF ? "off" : (c->tab_bar == BAR_ALWAYS ? "always" : "auto"));
        }
    } else if (!strcmp(key, "split_max")) {
        /* Sessions drawn side by side at most. More sessions than that are still open and still listed in
           the bar; the shown window of panes slides to contain the visible one. */
        c->split_max = iclamp_(atoi(val), 1, TAB_MAX);
    } else if (!strcmp(key, "file_manager")) {
        /* Start with the file manager shown. Off by default; the toggle is Ctrl+Shift+F. */
        c->fm_on = cfg_bool(val) ? 1 : 0;
    } else if (!strcmp(key, "fm_size")) {
        /* Percent of the window's height the file-manager band asks for. layout.c clamps this again
           against the real window and always leaves the terminal pane at least FM_TERM_MIN_ROWS rows. */
        c->fm_size = iclamp_(atoi(val), FM_SIZE_MIN, FM_SIZE_MAX);
    } else if (!strcmp(key, "fm_preview")) {
        /* The preview pane next to the browser (toggled at runtime too). Off = the browser is full width. */
        c->fm_preview = cfg_bool(val) ? 1 : 0;
    } else if (!strcmp(key, "fm_show_hidden")) {
        /* Dotfiles are part of the listing from the start (toggled at runtime with fm_key_hidden). */
        c->fm_show_hidden = cfg_bool(val) ? 1 : 0;
    } else if (!strcmp(key, "fm_follow")) {
        /* Navigating the browser also sends `cd <path>` to the shell, so the prompt lands in the
           directory being browsed. Off = the shell stays wherever the user left it. */
        c->fm_follow = cfg_bool(val) ? 1 : 0;
    } else if (!strcmp(key, "fm_focus_on_hover")) {
        /* 1 = a scroll gesture over a pane of the band moves the keyboard to that pane, as earlier
           sessions did. 0 (the default) = a pane is focused by CLICKING it; the wheel over the band
           still scrolls the listing or the preview body, it just leaves the keyboard where it is. */
        c->fm_focus_on_hover = cfg_bool(val) ? 1 : 0;
    } else if (!strcmp(key, "log")) {
        c->loglevel = iclamp_(atoi(val), L_ERROR, L_DEBUG);
    } else if (!strcmp(key, "osc_colors")) {
        /* The off switch for OSC 4/10/11/104. Not applied here: it is process-wide state in src/vt.c
           (g_osc_colors), set by main.c once the settings file has been read, exactly like the default
           ink/paper. A theme file does NOT set it — it is a permission, not a colour. */
        c->osc_colors = cfg_bool(val) ? 1 : 0;
    } else if (!strcmp(key, "theme")) {
        /* A name (looked up as NAME.theme in <config dir>/themes/) or a path. The theme is NOT applied
           here: it is applied after this file has been read in full, so a colour line anywhere in the
           settings file beats it — see src/theme.c and the s102 precedence decision. */
        snprintf(c->theme, sizeof(c->theme), "%s", val);
    } else {
        klog(L_WARN, "config: unknown key '%s'", key);
    }
}

/* One line of either file. `from_bind_file` says which file it came from, and the only thing that decides
   is which NAMES are accepted there: a binding written in kai-term.conf is applied and pointed at
   kai-term.bind, a setting written in kai-term.bind is refused and pointed at kai-term.conf. Either way
   the line is never silently dropped. */
static void apply(Config *c, const char *key, char *val, int from_bind_file)
{
    if (apply_bind_key(c, key, val)) {
        if (!from_bind_file)
            klog(L_INFO, "config: %s = '%s' is a key binding — those live in kai-term.bind now "
                         "(the line still works, and kai-term.bind wins for a name both files set)",
                 key, val);
        return;
    }
    if (from_bind_file) {
        klog(L_WARN, "config: '%s' is not a key binding — kai-term.bind holds bindings only; that "
                     "setting belongs in kai-term.conf", key);
        return;
    }
    /* Record that the SETTINGS file named this key, before applying it: src/theme.c may fill a colour the
       user never mentioned, but never one they did. Recorded here rather than in each branch so a new
       setting is covered the day it is added. */
    config_note_set(c, key);
    apply_setting(c, key, val);
}

/* The list of names the settings file set explicitly. Names, not bits: nothing to register, and the
   lookup is over ~30 short strings once at startup, so the cost is nothing. Overflow is refused loudly
   rather than dropped — a silently unrecorded name is a colour the user set being overwritten by a theme. */
void config_note_set(Config *c, const char *key)
{
    if (!key[0])
        return;
    for (int i = 0; i < c->n_set_keys; i++)
        if (!strcmp(c->set_keys[i], key))
            return;                       /* already recorded */
    if (c->n_set_keys >= CONFIG_SET_KEYS_MAX) {
        klog(L_WARN, "config: cannot record '%s' — the explicit-key list is full (%d)",
             key, CONFIG_SET_KEYS_MAX);
        return;
    }
    snprintf(c->set_keys[c->n_set_keys], sizeof c->set_keys[0], "%s", key);
    c->n_set_keys++;
}

int config_key_was_set(const Config *c, const char *key)
{
    for (int i = 0; i < c->n_set_keys; i++)
        if (!strcmp(c->set_keys[i], key))
            return 1;
    return 0;
}

/* Cut a comment off a config line, in place. One rule for every file Kai-Term reads — kai-term.conf,
   kai-term.bind and a .theme — because getting this wrong once ate every `#RRGGBB` colour in the tree
   (s99). '#' starts a comment in two cases: nothing but whitespace comes before it (a comment line), or
   whitespace comes before it AND whitespace or the end of the line comes after it (a trailing note). The
   second half of that rule is what makes a colour work: `background = #1c1c22` has a space before the
   '#', and the old "whitespace before it is enough" rule cut the line right there, so the documented
   colour syntax set NOTHING at all — measured, Config.bg stayed at its default with that line in the file,
   and an empty value is not even worth a warning because "foreground =" is a legal way to ask for the
   default. `working_dir = /home/me/my#project` still survives (no whitespace before the '#'), and
   `value # note` is still stripped. */
void config_strip_comment(char *line)
{
    for (char *hash = line; *hash; hash++) {
        if (*hash != '#')
            continue;
        int only_ws_before = 1;
        for (char *p = line; p < hash; p++)
            if (!isspace((unsigned char)*p))
                only_ws_before = 0;
        if (only_ws_before ||
            ((hash[-1] == ' ' || hash[-1] == '\t') &&
             (hash[1] == 0 || isspace((unsigned char)hash[1])))) {
            *hash = 0;
            return;
        }
    }
}

/* Where one config file comes from. The first directory below that holds `name` wins and the rest are
   never opened, so a user's own file is never mixed with the system's:

       $XDG_CONFIG_HOME/kai-term/   (only when XDG_CONFIG_HOME is set and non-empty)
       ~/.config/kai-term/
       /etc/kai-term/

   Nothing found anywhere leaves `out` EMPTY, which is how the callers know to keep the built-in defaults
   rather than open a path that does not exist. The search is per FILE NAME, not per directory: a user who
   has a kai-term.conf in ~/.config and no kai-term.bind there still gets the system-wide bindings from
   /etc/kai-term, which is the point of shipping a bind file distro-side. A directory, a FIFO or a dangling
   symlink named kai-term.conf is not a config file, so S_ISREG is checked and the search moves on. */
void config_find(const char *name, char *out, size_t cap)
{
    out[0] = 0;
    char cand[3][4096];
    int n = 0;
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        snprintf(cand[n], sizeof cand[0], "%s/kai-term/%s", xdg, name);
        n++;
    }
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(cand[n], sizeof cand[0], "%s/.config/kai-term/%s", home, name);
        n++;
    }
    snprintf(cand[n], sizeof cand[0], "/etc/kai-term/%s", name);
    n++;
    for (int i = 0; i < n; i++) {
        struct stat st;
        if (stat(cand[i], &st) == 0 && S_ISREG(st.st_mode)) {
            snprintf(out, cap, "%s", cand[i]);
            return;
        }
    }
}

/* Read one file of either kind. `from_bind_file` decides which NAMES are accepted there (see apply()): a
   binding in kai-term.conf is applied with a pointer at where it belongs now, a setting in kai-term.bind
   is refused with the same pointer. Nothing is ever silently dropped. */
static void config_read(Config *c, const char *path, int from_bind_file)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        klog(L_INFO, "no %s file at %s (using defaults)",
             from_bind_file ? "binding" : "config", path);
        return;
    }

    char line[512];
    int lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        config_strip_comment(line);   /* the one '#' rule, shared with src/theme.c — see that function */
        trim(line);
        if (!line[0])
            continue;
        char *eq = strchr(line, '=');
        if (!eq) {
            klog(L_WARN, "config:%d: malformed line '%s'", lineno, line);
            continue;
        }
        *eq = 0;
        char *key = line, *val = eq + 1;
        trim(key);
        trim(val);
        if (!key[0]) {
            klog(L_WARN, "config:%d: empty key", lineno);
            continue;
        }
        apply(c, key, val, from_bind_file);
    }
    fclose(f);
    klog(L_INFO, "%s loaded from %s", from_bind_file ? "bindings" : "config", path);
}

/* The settings file. `path` NULL means the searched one (config_find); an explicit path — what -c gives —
   skips the search entirely and says nothing about the other locations. */
void config_load(Config *c, const char *path)
{
    char found[4096];
    if (!path) {
        config_find("kai-term.conf", found, sizeof found);
        if (!found[0]) {
            klog(L_INFO, "no kai-term.conf in $XDG_CONFIG_HOME/kai-term, ~/.config/kai-term or "
                         "/etc/kai-term — using the built-in defaults");
            return;
        }
        path = found;
    }
    config_read(c, path, 0);
}

/* The key-binding file, read AFTER the settings file so a name both files set ends up as the bind file
   says. Same search order, same syntax, its own -b override. */
void config_load_binds(Config *c, const char *path)
{
    char found[4096];
    if (!path) {
        config_find("kai-term.bind", found, sizeof found);
        if (!found[0]) {
            klog(L_INFO, "no kai-term.bind in $XDG_CONFIG_HOME/kai-term, ~/.config/kai-term or "
                         "/etc/kai-term — using the built-in key bindings");
            return;
        }
        path = found;
    }
    config_read(c, path, 1);
}
