/* Kai-Term — lightweight Wayland terminal emulator. Shared header. */
#ifndef KAI_TERM_H
#define KAI_TERM_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>   /* mode_t, named by FmEntry/FmPreview — included rather than trusted to come
                            in through string.h, which is what happens to work on glibc today */

/* ---------- logging ---------- */
enum { L_ERROR = 0, L_WARN, L_INFO, L_DEBUG };
extern int kai_term_loglevel;
/* The printf attribute is not decoration: it is what makes the compiler check every klog() call site against
   its format string, and a log line is exactly the code nobody reads until the argument list is wrong. Checked
   before adding it (a copy of the tree, gcc 15 and clang 22): with the attribute in place BOTH compilers still
   report 0 warnings over src/ and tests/, so every existing call site is already format-correct and this costs
   nothing — it is a net for the next one. Guarded because it is a compiler extension: a compiler without it
   compiles the same code, it just checks less. */
void klog(int level, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/* ---------- colors & cell attributes ---------- */
#define DEF_FG      0xffe5e5e5u   /* ARGB */
#define DEF_BG      0xff101014u
#define CURSOR_COL  0xffd8d8d8u
#define SEL_BG      0xff3a4d66u   /* selection highlight background */

/* The ink and the paper Kai-Term uses for a cell nothing has painted. DEF_FG/DEF_BG are the BUILT-IN
   defaults (config_default() starts Config.fg/Config.bg at them, and the shipped template's parity check
   compares against them); these three functions are the RUNTIME values every blank, erased, scrolled-in and
   reflowed cell, and the pen after SGR 39/49 or `ESC c`, actually read. main.c calls the setter once, right
   after the config is loaded and before the first Term is created, so the `background =` line owns the cells
   as well as the glClear colour of the `padding` margin — two different numbers there draw a frame around
   the grid (see the comment on the definition in src/vt.c). Process-wide rather than Term state for the same
   reason as kai_term_loglevel: term_init() memsets the whole Term. */
void     term_set_default_colors(uint32_t fg, uint32_t bg);
uint32_t term_default_fg(void);
uint32_t term_default_bg(void);

/* The 256-entry palette, process-wide runtime state for the same reason as the three functions above
   (term_init() memsets the whole Term, so palette state in a Term dies with every new tab). Entries 0-15
   are the ANSI colours a theme may set; 16-231 (the 6x6x6 cube) and 232-255 (the grey ramp) are computed
   and are not themeable — every terminal computes them the same way and no theme in the wild touches them.
   Filled on first call, so a caller that never initialises anything gets the built-in palette, not zeros.
   term_set_palette_entry() ignores an out-of-range index. term_palette_reset() restores the built-ins and
   is what OSC 104 asks for — deliberately NOT called by term_init() or `ESC c`, so a `reset` at the prompt
   cannot wipe the user's theme.
   One consequence worth knowing: a Cell stores a RESOLVED colour, so changing an entry repaints nothing
   that is already on screen — it changes what future SGR lookups produce. That is how every terminal
   behaves, and it is why a theme file is applied before the first session is created. */
const uint32_t *term_palette(void);
void term_set_palette_entry(int idx, uint32_t argb);
void term_palette_reset(void);
void term_palette_entry_reset(int idx);   /* one entry back to built-in: what `OSC 104;N` asks for */
/* Whether an application may repaint the terminal's OWN colours (OSC 4 / 10 / 11 / 104). Process-wide like the
   palette itself, because it is a property of the window and not of a session. `osc_colors = 0` is the user's
   switch: with it off, nothing running inside the terminal can change a colour. */
void term_set_osc_colors(int on);
int  term_osc_colors(void);

/* Default xdg-shell app_id. Single-sourced here because config_default(), the
   `app_id =` empty-value fallback in config.c and the last-resort fallback in main.c must all agree;
   three string literals that can drift apart is how a default silently changes. */
#define DEF_APP_ID  "Kai-Term"

/* Default window title. mux_title() builds the toplevel title from it; the tab bar shows the same state
   INSIDE the window, the title is what a compositor or a taskbar shows. Single-sourced for the same reason
   as DEF_APP_ID. */
#define DEF_TITLE   "Kai-Term"

/* Max continuation codepoints stored on one emoji anchor cell (base excluded). Covers the
   common ZWJ sequences: family 👨\u200d👩\u200d👧\u200d👦 = 6 continuation codepoints. */
#define CLUSTER_MAX 8

enum {
    ATTR_BOLD      = 1u << 0,
    ATTR_DIM       = 1u << 1,
    ATTR_ITALIC    = 1u << 2,
    ATTR_UNDERLINE = 1u << 3,
    ATTR_INVERSE   = 1u << 4,
    ATTR_HIDDEN    = 1u << 5,
    ATTR_STRIKE    = 1u << 6,
    ATTR_WIDE      = 1u << 7,   /* occupies 2 cells */
};

typedef struct {
    uint32_t fg, bg;
    uint32_t attr;
} Pen;

/* Term.mouse_mode bitmask (DEC private modes). */
enum {
    MOUSE_PRESS = 1 << 0,   /* ?1000: report button press/release */
    MOUSE_DRAG  = 1 << 1,   /* ?1002: also report motion while a button is held */
    MOUSE_SGR   = 1 << 2,   /* ?1006: use SGR (1006) encoding, else legacy X10 */
};

/* The cursor shape DECSCUSR (CSI Ps SP q) asks for, as stored in Term.cur_style. 0 means "the application
   never asked", which is why it is 0 and not the block: term_init() memsets Term, so a fresh session must
   land on Kai-Term's own default (a bar) rather than on an application's request. The mapping from the
   sequence's parameter is in csi_dispatch(): 0/1 -> block, 2 -> block, 3/4 -> bar, 5/6 -> underline, and the
   odd/even split decides the blink half (Term.cur_blink below). */
enum {
    CUR_UNSET    = 0,       /* no DECSCUSR seen: use the configured default */
    CUR_BLOCK    = 1,
    CUR_BAR      = 2,
    CUR_UNDERLINE = 3,
};

/* Term.cur_blink: the blink DECSCUSR asked for. 0 = never asked, so `cursor_blink =` decides. The user's
   setting is the CEILING, not a suggestion: `cursor_blink = 0` means this window never blinks whatever an
   application asks for, and DECSCUSR can only make the cursor LESS flashy (`ESC[2 q` steady block, `ESC[4 q`
   steady bar, `ESC[6 q` steady underline). That is a deliberate difference from xterm, which lets `ESC[1 q`
   switch blinking back on: a user who turned the blink off asked not to have it, and vim sending a shape
   sequence should not overrule that. blink_half_ms() in src/main.c is where the rule lives. */
enum {
    CURBLINK_UNSET = 0,
    CURBLINK_BLINK = 1,
    CURBLINK_STEADY = 2,
};

/* The button codes wl_pointer.button delivers are the Linux kernel's BTN_* values
   (linux/input-event-codes.h), and they are NOT in the order the terminal protocols number the buttons:
   BTN_LEFT 0x110, BTN_RIGHT 0x111, BTN_MIDDLE 0x112, while both the SGR (1006) and the legacy X10
   encodings want 0 = left, 1 = middle, 2 = right. mouse_sgr_button() (vt.c) is the one mapping; the
   constants live here so nothing re-derives them from the definition order. */
enum {
    EVDEV_BTN_LEFT   = 0x110,
    EVDEV_BTN_RIGHT  = 0x111,
    EVDEV_BTN_MIDDLE = 0x112,
};
int mouse_sgr_button(uint32_t evdev_code);   /* 0 left, 1 middle, 2 right, 3 any other button */

/* The wheel is not a button in the kernel's numbering — it is an axis — so the code reported to an
   application that DID ask for the mouse has to be chosen, and the number is not free. 64 = wheel up,
   65 = wheel down is what xterm, kitty, neovim, tmux and ncurses all read as a scroll. Measured
   2026-10-21 (.scratch/s96/nanoprobe) on `nano --mouse`, which asks for ?1000+?1006: ESC[<5;20;12M did
   nothing at all while ESC[<65;20;12M scrolled the text, and in the legacy X10 form 32+65 scrolled while
   32+4 did not — ncurses reads a code of 4 as button 1, so the old 4/5 made the wheel dead in every
   mouse-grabbing program. One pair of numbers is right for BOTH encodings because mouse_send() adds the
   +32 the X10 form wants. Kept in vt.c for the standing reason: main.c is linked into no test binary. */
int mouse_wheel_button(int up);   /* 64 wheel up, 65 wheel down */

typedef struct {
    uint32_t ch;    /* codepoint; 0 = blank */
    uint32_t fg, bg;
    uint32_t attr;
    /* Grapheme-cluster continuation codepoints for an emoji anchor cell (ZWJ sequences,
       variation selectors, skin-tone modifiers). Inline (no heap) so Cell stays safely
       copyable by value across row realloc, DCH/ICH shifts and rotations. nextra==0 means a
       single-codepoint cell. The anchor's ch is the cluster's first codepoint. */
    uint32_t extra[CLUSTER_MAX];
    uint8_t  nextra;
} Cell;

/* ---------- graphics: the kitty image protocol (src/vt.c) ----------
   An application (timg -p k, kitty icat, chafa) sends an image as one or more APC
   sequences:  ESC _ G <key>=<value>;...;<key>=<value> : <base64> ESC \
   timg's actual emission, captured from timg 1.6.3 (see AGENT.md s68):
       ESC_Ga=T,i=<id>,q=2,f=100,m=1;<4096 base64 chars>ESC\   (first chunk)
       ESC_Gq=2,m=0;<rest>ESC\                                (continuation: no a=, no i=)
   so m is the "more chunks follow" flag and f=100 means the payload is a PNG. */

/* One image the application transmitted. The PIXELS live in the model (vt.c) so the grid
   lifetime rules and the unit tests own them; render.c keeps a GL texture per image keyed by
   `serial` and re-uploads when `version` changes (an animation frame replaces the pixels). */
typedef struct KImage {
    int      serial;      /* unique within the Term, never reused: the renderer's texture key */
    int      app_id;      /* the i= the application used (0 = none) */
    int      w, h;        /* decoded pixel size */
    uint8_t *rgba;        /* w*h*4, PREMULTIPLIED: the shader composites c + bg*(1-c.a), the same
                             rule the color-emoji atlas follows, so no GL blending is needed */
    /* The image is anchored to an ABSOLUTE grid row (Grid.row_base space), not to a viewport row:
       a viewport row is invalidated by every scroll, while the absolute row survives rotation,
       eviction and reflow, so the image scrolls with the text it was printed next to and comes
       back when the user scrolls up into history. */
    int64_t  doc_row;
    int      col;         /* grid column of its left edge */
    int      px, py;      /* x=/y= pixel offset inside the anchor cell */
    int      cols, rows;  /* explicit cell span from c=/r= (0 = derive from px and cell size) */
    int      shown;       /* 0 = transmitted with a=t but never placed -> never drawn */
    int      z;           /* z-index, smaller drawn first (kitty default 0) */
    int      alt;         /* 1 = belongs to the alternate screen (drawn only while it is active) */
    int      version;     /* bumped when the pixels are replaced */
    struct KImage *next;
} KImage;

/* A renderer-side GL texture for one model image (render.c owns it). `seen` is stamped with the
   frame number, so an entry no image claimed this frame is deleted after the frame. */
typedef struct {
    int      serial;
    unsigned tex;
    int      w, h, version;
    int      seen;
} ImgTex;

/* Where one image sits in the CURRENT viewport, as computed by term_visible_images().
   crow may be negative (the image's top has scrolled above the viewport) and the span may run
   past the bottom: render.c clips the quad, so it needs the unclipped rect in cell units. */
typedef struct {
    KImage *img;
    int crow, ccol;    /* viewport row/column of the image's top-left cell */
    int crows, ccols;  /* the span to draw */
} ImgView;

/* A chunked image transmission in progress: the control block of the FIRST chunk plus the
   bytes collected so far. Continuation chunks carry only m=, so they inherit everything else. */
typedef struct KittyTx {
    int      active;      /* a transmission is in progress */
    int      more;        /* m=1: more chunks follow */
    uint8_t *buf; size_t len, cap;
    int      act;         /* a= : 't','T','f','c','d','q' (0 = continuation chunk) */
    int      fmt;         /* f= : 24 RGB, 32 RGBA, 48/64/100 PNG */
    int      id;          /* i= */
    int      pid;         /* u= placement id */
    int      use_last;    /* U=1: reuse the most recent image id (timg's grid mode) */
    int      pw, ph;      /* s= / r= pixel size, for raw formats (r= means height there) */
    int      r_raw;       /* r= as received: the key means height for the raw formats and rows for
                             the self-describing ones, and f= may come after it, so the choice is
                             made after the whole control block has been read */
    int      cols, rows;  /* c= / r= cell span, for plot actions (r= means rows there) */
    int      xoff, yoff;  /* x= / y= cell offsets */
    int      z;           /* z= */
    int      q;           /* q= : 1 = answer OK, 2 = answer only on failure */
    int      del;         /* a=d scope: 'A' all, 'I' by id, 'R' at/below cursor, 'C' at cursor */
} KittyTx;

/* ---------- VT parser ---------- */
struct Term;   /* forward */

enum PState {
    PS_GROUND, PS_ESC, PS_CSI, PS_OSC, PS_UTF8, PS_IGNORE, PS_APC, PS_DCS
};

typedef struct Parser {
    struct Term *t;
    int state;
    int npar, par[32];
    int priv;              /* CSI ? */
    int bang;              /* CSI ! */
    int gt;                /* CSI > (DA2 prefix) */
    /* The last intermediate byte (0x20-0x2f) seen in this CSI. It used to be dropped on the floor, which
       made `CSI Ps SP q` (DECSCUSR) indistinguishable from `CSI Ps q` — and DECSCUSR is exactly a sequence
       whose MEANING is the space. Kept as one byte because every sequence in use here has at most one. */
    int inter;
    int utf8_need, utf8_cp;
    char osc[512];
    int osc_len;
    /* APC payload (the kitty graphics sequences). Growable: one chunk is a few KB but a
       different application may put a whole image in one sequence. apc_drop is set when the
       cap is reached, so a garbage stream cannot exhaust memory. */
    char  *apc; size_t apc_len, apc_cap;
    int    apc_drop;
    int    apc_kind;       /* the byte after ESC _: 'G' = graphics, anything else is ignored */
    KittyTx tx;
} Parser;

/* ---------- grid & terminal ---------- */
typedef struct Grid {
    int rows, cols;        /* viewport size */
    int rcap;              /* allocated rows (rows + scrollback_max) */
    Cell **row;            /* row[0..scroll_off-1] scrollback,
                              row[scroll_off..scroll_off+rows-1] viewport,
                              row[scroll_off+rows..rcap-1] free (or holds the newer
                              lines while the user is scrolled back) */
    int scroll_off;        /* index of the viewport top; 0 = oldest retained line,
                              live_off = newest (live) view */
    int live_off;          /* scroll_off value that follows the newest output.
                              scroll_up/scroll_down advance it with scroll_off; user
                              navigation moves scroll_off below it. Always
                              0 <= live_off <= scrollback_max. */
    int scrollback_max;
    /* wrap[i] != 0 -> the line in row[i] was broken by the RIGHT MARGIN, not by the application, so
       it continues at the start of row[i+1]. Without this mark a wrapped line and a hard-broken line
       are indistinguishable and a width change can only truncate; with it the two physical rows can
       be joined back into the logical line the application wrote and re-broken at the new width
       (reflow, see grid_resize). It rotates together with row[] (rot_left/rot_right) and is cleared
       whenever a slot is blanked or its line is erased. */
    uint8_t *wrap;
    /* Absolute (document) row number of slot 0. Slots hold a contiguous window of document
       rows, oldest at slot 0, so slot j == document row row_base + j, and document row A sits
       at viewport row A - row_base - scroll_off. row_base only moves when a row is EVICTED at
       the top (a left rotation of the used region) or when the window slides over its own
       scrollback (erase_scrollback), which is exactly when the oldest rows stop existing. Images
       are anchored by document row (see KImage), which is what lets them survive scrolling,
       eviction and reflow without any per-row bookkeeping. */
    int64_t row_base;
    /* Set to GRID_VALID between grid_init and grid_destroy. term_init frees the grid it is rebuilding
       (the RIS leak fix), so it must be able to tell "there is an old grid here to free" from "this
       Term was never initialized". A plain 0/1 flag is NOT enough: on a stack Term the garbage in
       `valid` is routinely non-zero (measured: 0x7ffd-ish stack halves), which would still free the
       garbage `row` pointer. A distinctive sentinel makes accidental garbage essentially unable to
       look like an initialized grid. */
    int valid;
} Grid;

#define GRID_VALID 0x67726964   /* 'g','r','i','d' */

/* Sizes and kinds a struct below needs before the section that introduced them appears. FM_PATH_MAX is the
   cap for every path in the program — the file manager's, the shell integration's, a session's remembered
   shell directory — so it lives here rather than in the file-manager block, which comes later. */
#define FM_PATH_MAX   4096            /* a directory or file path (matches Config.working_dir) */

/* Shell integration kinds: the shell announces one of these over Kai-Term's APC channel and Term.shell_int
   holds it (see the Term fields and src/shellint.c). */
enum { SHELLINT_NONE = 0, SHELLINT_FISH, SHELLINT_BASH, SHELLINT_ZSH };

typedef struct Term {
    Grid g;
    int cx, cy;            /* cursor, viewport coords (0-based) */
    int save_cx, save_cy;
    int top, bot;          /* scroll region, inclusive, 0-based */
    Pen pen;
    /* No palette here: the 256-colour palette is process-wide runtime state (see term_palette()), because
       term_init() memsets this struct for every new tab and would throw a configured theme away. */
    int wrap_next;         /* deferred wrap pending at right margin */
    int show_cursor;
    int newline_mode;      /* LNM: LF also does CR */
    int mouse_mode;        /* bitmask: MOUSE_PRESS | MOUSE_DRAG | MOUSE_SGR */
    /* DECCKM (DEC private mode 1, "application cursor keys"). An ncurses program (less, man, htop, nano)
       turns it on with ESC[?1h when it takes the screen and off with ESC[?1l on the way out, and while it
       is on the four cursor keys go out as SS3 (ESC O A..D) instead of CSI (ESC [ A..D). Ignoring it is
       not cosmetic: measured 2026-10-21 (.scratch/s96/nanoprobe) nano answers either form but less answers
       ONLY the SS3 one — so while ?1h is ignored, the arrow keys do nothing at all in less. Saved and
       restored across the alternate screen the way xterm does, so a main-screen program that set it cannot
       leave an application-cursor mode behind for the next full-screen program. */
    int app_cursor;
    /* DECSCUSR (CSI Ps SP q): the shape and the blink the APPLICATION asked for, per session, because vim,
       neovim and fish all change them when they take the screen (insert mode vs normal mode) and expect the
       next session on the same window to be unaffected. 0 in either field means "never asked" — term_init()
       memsets Term, so 0 must be the neutral value, and a new tab therefore starts from Kai-Term's own
       default (a bar) rather than from whatever the previous session's program requested. render_cursor()
       (src/render.c) is the one place that turns these two ints plus the config into pixels, and it is a
       pure function so tests/test_render.c can check it without a display. */
    int cur_style;         /* CUR_UNSET / CUR_BLOCK / CUR_BAR / CUR_UNDERLINE */
    int cur_blink;         /* CURBLINK_UNSET / CURBLINK_BLINK / CURBLINK_STEADY */
    int alt_screen;        /* 1 when the alternate screen buffer is active */
    Grid alt;              /* saved MAIN grid while alt_screen is active */
    int alt_saved_cx, alt_saved_cy;  /* main-screen cursor saved on ?1049h */
    int alt_saved_app_cursor;        /* DECCKM of the screen being parked, restored on leaving the alt */
    /* Bracketed paste mode (DEC private mode 2004). An application (bash, zsh, vim, ...) turns it
       on with ESC[?2004h to ask that pasted text be told apart from typed text: the terminal wraps
       the paste in ESC[200~ ... ESC[201~, so embedded newlines stay in the application's own
       insert buffer instead of reaching its command line as "Return" presses. 0 = send raw. */
    int bracketed_paste;
    /* Text selection (viewport coordinates, inclusive). The horizontal edges are
       sub-cell: sel_af/sel_bf are the position within the anchor/current cell in
       thousandths of a cell width (0..1000), so the highlight follows the pointer
       smoothly instead of snapping to whole cells. */
    int sel_active;
    int sel_ax, sel_ay;    /* anchor cell */
    int sel_bx, sel_by;    /* current cell (extends from the anchor) */
    int sel_af, sel_bf;    /* sub-cell horizontal offset 0..1000 within anchor/current */
    char title[256];
    /* Images transmitted by applications (see KImage). A singly-linked list kept oldest first:
       the byte budget evicts from the front, which is also the order the user scrolls past. */
    KImage *images;
    long long image_bytes;      /* decoded bytes held by this session */
    int      image_serial;      /* next unique serial */
    int      image_last_id;     /* i= of the most recent image (a U=1 chunk refers to it) */
    /* Current cell size in px. The image protocol sizes images in PIXELS while the grid is in
       CELLS, and only main.c knows the font metrics, so it pushes the cell size here (and to the
       Pty, which reports it to the PTY in winsize). 0 = unknown: images then fall back to their
       own pixel size as a cell count. */
    int      cell_w, cell_h;
    Parser ps;             /* parser state lives with the terminal */
    /* Grapheme-cluster assembly state: the anchor cell of the emoji cluster currently being
       built, so a ZWJ / variation selector / skin-tone modifier that follows joins that cell
       instead of landing in a cell of its own. cl_cx/cl_cy are VIEWPORT coordinates, so the
       state is dropped whenever rows move (scroll/resize) or the cursor jumps elsewhere.
       cl_w is the number of cells the anchor consumed (1 or 2); cl_join is set after a ZWJ so
       the next emoji base codepoint also joins the same anchor. */
    int cl_valid, cl_cx, cl_cy, cl_w, cl_join;
    /* The shell's input line, remembered so main.c can tell an UNTOUCHED prompt from a line the user
       has already typed on (term_at_prompt). prompt_row holds prompt_cols cells — the whole row the
       cursor was sitting on at the last snapshot — and prompt_cx the cursor column at that moment.
       A snapshot is taken at the end of every output burst EXCEPT while input_pending is set, i.e. while
       bytes the terminal itself sent are still outstanding: output arriving then is the tty echoing that
       typing (or readline redrawing the line), and remembering it would remember the user's half-typed
       command as the prompt. prompt_valid is 0 after anything that moves or re-wraps rows (a resize, a
       reset, CSI 3 J), which means "unknown" — and unknown counts as NOT at a prompt. prompt_cy is the row
       the snapshot is of: comparing it stops a cursor that moved to a DIFFERENT row which happens to look
       like the remembered one (two blank rows) from passing for the prompt. Leaving the alternate screen
       is the one case that re-takes the snapshot even while an echo is outstanding — the main grid comes
       back byte-for-byte, so the row under the restored cursor IS the prompt. See the prompt section in
       src/vt.c. */
    Cell *prompt_row;
    int   prompt_cols, prompt_cx, prompt_cy, prompt_valid, input_pending;
    /* Shell integration (src/shellint.c): a shell that loaded Kai-Term's integration script announces
       itself over Kai-Term's own APC channel (`ESC _ K ; kai-term ; integration ; <kind> ST`), and Kai-Term
       then asks IT to change directory instead of typing `cd` at the prompt. shell_int is 0 until that
       announcement arrives for THIS session, so an un-integrated shell keeps the old typed path. It is
       deliberately preserved across a reset (ESC c), which memsets Term: the script announces once, at
       shell startup, and a `reset` must not silently drop the feature. shell_cd_ack holds the directory the
       integration last confirmed it entered ('' = nothing confirmed yet) — main.c compares it with the
       directory it asked for, which is the only evidence that the control sequence really reached a shell
       that was listening. */
    int  shell_int;                 /* SHELLINT_NONE / _FISH / _BASH / _ZSH */
    char shell_cd_ack[FM_PATH_MAX];
    /* Counts the announcements this session has made. A shell that starts inside another one announces too
       (the scripts wrap a bare `fish`/`bash`/`zsh`), and so does the outer one when the nested shell exits,
       so "a shell announced" is an EVENT, not just a kind — main.c needs it to REVIVE an integration it had
       given up on (`Tab.int_dead`), which a kind alone cannot express (fish -> fish announces twice with the
       same kind). It survives a reset with the announcement itself: both are one fact about the shell. */
    int  shell_int_gen;
    /* Terminal query responses (CPR, DA1, DA2, OSC 10/11 colour queries) queued for the PTY. */
    char resp[256];
    int resp_len;
    /* OSC 10/11: an application setting the default ink/paper. vt.c applies the change to the process-wide
       defaults and sweeps THIS session, then raises the flag; main.c sees it, sweeps every other session and
       re-points the renderer's clear colour and chrome. The old values travel with the flag because the sweep
       is a match-and-replace and the defaults have already moved by the time main.c acts. Zeroed by term_init()
       with the rest of Term: a fresh session has nothing painted that needs sweeping. */
    int      colors_dirty;
    uint32_t old_def_fg, old_def_bg;
} Term;

/* The last directory the shell said it entered (its `cd` ack). main.c compares this with the directory the
   file manager asked for; that comparison is the only evidence the integration has. Clear it before asking
   for a new directory, or the previous navigation's ack answers for this one. */
void term_shell_cd_ack_clear(Term *t);
/* grid */
void grid_init(Grid *g, int rows, int cols, int scrollback);
/* Resize the viewport. A column-count change REFLOWS: soft-wrapped rows are joined back into their
   logical line and re-broken at the new width, so nothing is lost (narrowing pushes lines into
   scrollback and evicts the oldest ones when it overflows, exactly like normal output).
   slot/col (either may be NULL) name a cell of the OLD grid — slot is an absolute index into row[],
   col a column — and come back as that same cell's position in the NEW grid. A line that was
   evicted puts the cursor on the newest surviving line. */
void grid_resize(Grid *g, int rows, int cols, int *slot, int *col);
/* Same, but WITHOUT reflow: rows are truncated/grown in place. Used for the alternate screen,
   where a full-screen application repaints itself after a SIGWINCH instead of expecting the
   terminal to re-wrap its output.
   cur_slot is the absolute row[] slot of the cell that must stay on screen (the cursor), or -1.
   On a height SHRINK the viewport gives up its BOTTOM rows while the cursor has rows below it, and
   only after that its TOP rows (into scrollback). Either way the tracked cell keeps its content
   line; if it instead got pushed out of the viewport the application would repaint at the wrong
   place and leave its old output behind (the zoom leftover-lines bug). */
void grid_resize_plain(Grid *g, int rows, int cols, int cur_slot);
void grid_destroy(Grid *g);
Cell *grid_vprow(Grid *g, int y);   /* viewport row y (0..rows-1) */

/* Window pixel size + cell size + a margin -> the grid size AND the pixel offset the grid starts at.
 * One function because THREE places must agree on this geometry: main.c's cols/rows for the grid and
 * the PTY, main.c's pointer -> cell mapping (selection, SGR mouse reporting), and render.c's quad
 * offsets. Any drift between them puts the highlight on the wrong glyph.
 * Rules: the margin is applied on all four sides (so no glyph sits under a rounded window corner);
 * the margin is clamped so at least one whole cell always fits; the leftover pixels that do not fill a
 * whole cell stay at the RIGHT and BOTTOM, which is what keeps the pointer mapping exact
 * (col = (ptr_x - xoff) / cell_w). pad = 0 is the old edge-to-edge layout (xoff = yoff = 0). */
void term_grid_geometry(int win_w, int win_h, int cell_w, int cell_h, int pad,
                        int *cols, int *rows, int *xoff, int *yoff);

/* terminal */
void term_init(Term *t, int rows, int cols, int scrollback);
/* Re-point everything one session has ALREADY painted at a new default ink/paper (OSC 10/11). Needed because a
   Cell stores a RESOLVED colour, not a palette index: moving the two defaults alone would change only future
   output, so the screen in front of the user would keep the old colours. It is also the only thing keeping
   cell_pristine() true for blank cells across a runtime colour change — the s101 invariant; sweeping only the
   non-pristine cells re-creates that bug. Idempotent. See the comment on the definition in src/vt.c. */
void term_recolor_defaults(Term *t, uint32_t old_fg, uint32_t old_bg,
                           uint32_t new_fg, uint32_t new_bg);
void term_destroy(Term *t);
void term_resize(Term *t, int rows, int cols);
void term_parse(Term *t, const uint8_t *buf, size_t len);

/* Is this session sitting at a prompt the user has not typed on? 1 = the row the cursor is on still
   holds exactly what the shell last printed there, and the cursor has not moved since.
   WHY THIS EXISTS: the file manager writes at the shell's prompt (`cd <dir>`, or the selected path),
   and doing that on top of a half-typed command mangles it and then RUNS the mangled result. Kai-Term
   cannot know where a prompt ENDS — the tty echoes what the user types, and that echo arrives as
   ordinary output — so the rule is a snapshot of the cursor row plus the cursor column, re-taken
   whenever the application produces output that is not the echo of bytes we sent. Typing, a history
   recall (readline redraws the line) and a paste all change that row without any application output
   in between, so they are caught; backspacing everything away, or arrowing back down to an empty
   prompt, redraws it exactly as the shell printed it and the match comes back. 0 also answers for the
   alternate screen, for a session scrolled back into its history, and for one whose rows moved since
   the last snapshot. */
int term_at_prompt(Term *t);
/* Tell the model that the terminal just sent these bytes to the application (a keystroke, a paste, a
   line the file manager typed). A payload holding CR or LF submits the line, which clears the flag; so
   does Ctrl+C, which discards it. That is what lets term_parse tell an echo/redraw (bytes we sent are
   outstanding — do NOT re-take the prompt snapshot) from the application's own output (do). Call it for
   INPUT only: a query answer (DA1, DSR, CPR) or a mouse report is not typing and must not set it. */
void term_note_input(Term *t, const void *buf, size_t len);

/* The wheel and a click inside a program that owns its own screen and never asked for the mouse
   (nano, less, man) can only be delivered as KEYS. Both helpers below answer "which keys?", and both
   are in vt.c rather than main.c for the standing reason: main.c is linked into no test binary.

   WHY NOT A MOUSE REPORT — measured, 2026-10-21 (.scratch/s96/nanoprobe, real nano 9.2 in a real pty):
   a program that has not enabled a mouse protocol does not swallow the report, it treats it as typed
   text. One ESC[<64;20;5M put "64;20;5M" into the user's file and marked the buffer Modified. Reporting
   the wheel to such a program is a data-corruption bug, not a no-op. */

/* The most arrow-key presses either helper will emit. A click cannot be further than cols+rows cells
   away in a grid Kai-Term draws, and a wheel gesture is bounded by the frame that produced it, so this
   is a bound on pathological input, not a limit anyone reaches. */
#define TERM_ARROW_MAX 1024

/* 1 = this session is in the state where the wheel and a click belong to the application as keys:
   the alternate screen, and no mouse protocol the application asked for (?1000/?1002 both count as
   "the application wants the mouse"; ?1006 alone only picks the encoding). */
int term_wants_key_mouse(const Term *t);

/* The bytes this session's application expects for ONE cursor key: SS3 (ESC O A..D) while it holds
   DECCKM (ESC[?1h), CSI (ESC [ A..D) otherwise. `which` is the final byte of either form — A up, B down,
   C right, D left; anything else gives "". One function because the arrow keys the USER presses and the
   ones the wheel and a click are turned into must never disagree about the form the program asked for:
   measured, nano answers either, less answers ONLY the SS3 one. Returns a static string, 3 bytes. */
const char *term_arrow_seq(const Term *t, char which);

/* The wheel, as arrow-key presses. lines > 0 scrolls toward the top of the application's text (wheel
   up), < 0 toward the bottom. Returns the number of bytes written to out, or 0 when this session is not
   in that state or the gesture moves nothing. */
int term_wheel_arrow_keys(const Term *t, int lines, char *out, size_t cap);

/* A left click at viewport cell (col,row), as the arrow-key presses that walk the application's OWN
   cursor there — computed from the cursor position the model already tracks (t->cx/t->cy), so no
   knowledge of the program is needed. Vertical first, then horizontal: measured on nano, Up/Down keep
   the target column, so correcting the column afterwards lands on the clicked cell. The target column
   is clamped to the clicked row's last non-blank cell + 1, because a program's Right at end-of-line
   moves to the NEXT line (measured: a click at column 60 of a 58-column line landed on the row below).
   Returns the number of bytes written to out, or 0 when this session is not in that state, the cell is
   outside the grid, or the cursor is already there. */
int term_click_arrow_keys(const Term *t, int col, int row, char *out, size_t cap);

/* The longest input line term_prompt_line() will hand back. A line of `cols` wide codepoints plus
   CLUSTER_MAX modifiers each cannot come near this at any window size Kai-Term supports, so hitting the
   cap means something absurd — and term_prompt_line() REFUSES rather than truncating a command. */
#define FM_LINE_MAX 8192

/* Can the session's input line be described EXACTLY as "the remembered prompt, then typed text that ends
   exactly at the cursor"? 1 = yes, and `out` (NUL-terminated, at most cap-1 bytes) receives that typed
   text — "" when the line is empty. 0 = no, and `out` is untouched.
   WHY THIS EXISTS: the file manager must follow the browser even when the user has already typed
   something (user request, s79): Kai-Term clears the line, sends `cd <dir>`, and puts what was there back
   on the new prompt. That is only safe if it can say what IS on the line and prove where the line ends,
   which is exactly what this answers. It is deliberately STRICTER than term_at_prompt() in shape and
   LOOSER in one way:
     - requires the prompt prefix (columns 0..prompt_cx-1) to still match the snapshot, so the prompt
       itself was not redrawn under us, and everything right of the cursor to match it too, so the cursor
       sits at the END of the line's content. A cursor parked mid-line is a refusal: erasing from there
       would leave the right half of the text behind, and the `cd` would be typed in front of it.
     - does NOT consult input_pending. The row comparison is the evidence, and Kai-Term's own typed path
       sets that flag while leaving a perfectly describable line behind.
   A wide glyph contributes ONE codepoint (its continuation cell is skipped) and a cluster's extra
   codepoints are appended, so re-typing the returned bytes reproduces the line. A cell never written
   inside the range becomes a space, as in term_selection_text(). If the text does not fit in cap the
   answer is 0: losing half of the user's command is worse than not following the browser. */
int term_prompt_line(Term *t, char *out, size_t cap);

/* The same description, WITHOUT the requirement that the cells right of the cursor match the snapshot:
   it returns the text between the end of the remembered prompt and the cursor, and ignores whatever the
   shell paints from the cursor onwards (fish's grey autosuggestion ghost — see term_prompt_line_is()).
   Use it only when the caller is about to ERASE the whole line and will PROVE the erase worked before it
   writes anything else: the erase is Ctrl+U, which erases backwards from the cursor, so if the user parked
   the cursor in the middle of a command the right half survives, the proof (the whole row equal to the
   remembered prompt) fails, and the caller drops its sequence and logs what it had captured instead of
   writing. That is the price of following the browser across a ghost: in the mid-line case the left half of
   the command is erased and only recoverable from the log. What can never happen is a `cd` typed in front of
   text and RUN — the proof gates every write. */
int term_prompt_line_typed(Term *t, char *out, size_t cap);

/* Does the input line read as the remembered prompt followed by EXACTLY `text`, ending at the cursor?
   Same question as term_prompt_line(), asked for the one string a caller already holds, and with ONE
   difference: it does not require the cells right of the cursor to match the snapshot.
   WHY THAT MATTERS: a shell decorates the line. fish paints a grey autosuggestion immediately right of
   the cursor (measured 2026-10-19: typing `git com` puts `mit -m "Initial commit"` on the row, typing
   `ls -la /tmp` puts a `/` there), and those cells are not part of the command line — pressing Right
   accepts one, BackSpace never has to delete one. A caller that is about to ERASE the line must still
   refuse them (term_prompt_line()), because it cannot tell that ghost from the right half of a command
   the user parked the cursor in front of. A caller that only wants to know whether text IT typed has
   landed must not be fooled by them, and that is this function: the text it compares is between the end
   of the prompt and the cursor, which is exactly where typed text goes and never where a ghost is drawn. */
int term_prompt_line_is(Term *t, const char *text);

/* selection */
typedef struct {
    int r0, c0, f0;   /* start: row, column, offset within that column (0..1000) */
    int r1, c1, f1;   /* end */
} SelSpan;

int  term_selected(Term *t, int col, int row);          /* is cell (col,row) selected? */
void term_selection_span(Term *t, SelSpan *s);          /* normalized reading-order span */
/* Covered sub-range of cell (col,row) in cell-fraction units, clamped to [col,col+1].
   Returns 1 when the cell is covered (then *b > *a), 0 otherwise. */
int  sel_span_cover(const SelSpan *s, int cols, int col, int row, double *a, double *b);
/* Band geometry in buffer pixels for cell (col,row): *x = left edge, *w = width (0 = none).
   Pure math (no GL) so the highlight position is unit-testable. */
void sel_band_pixels(const SelSpan *s, int cols, int col, int row, float cw,
                     float *x, float *w);
int  term_selection_cover(Term *t, int col, int row, double *a, double *b);
/* Should the selection band be PAINTED over cell (col,row)? The cell must already be covered by the
   selection; this only decides whether it holds anything worth highlighting (see the definition in
   vt.c for the exact rule). Lets the renderer skip empty space while term_selection_text() still
   copies it as a space. */
int  term_cell_highlightable(Term *t, int col, int row);

/* How long two presses of the same cell may be apart and still be ONE gesture (double click, triple
   click). One constant for the terminal and for the file manager's browser, which has used 500 ms
   since s63 — the same number the user already feels as "a double click". */
#define CLICK_DCLICK_MS 500

/* The word at (col,row) — the maximal run of non-space cells around it with TRAILING punctuation cut
   off (leading punctuation stays). *out_c0..*out_c1 inclusive; a click on a blank yields the clicked
   cell alone. A wide glyph is never split: the run always covers the anchor and its continuation. */
int term_word_span(Term *t, int col, int row, int *out_c0, int *out_c1);
/* The logical line of `row`: the physical rows a soft-wrapped line spans, joined by the grid's wrap
   marks. *out_r0..*out_r1 inclusive (viewport rows). A hard break (the application's own newline)
   ends it. */
int term_line_span(Term *t, int row, int *out_r0, int *out_r1);
/* Which click in a row this press is: 1 when it starts a new gesture, prev_count + 1 when the same
   cell was pressed again within CLICK_DCLICK_MS. */
int term_click_count(uint32_t time_ms, uint32_t prev_time_ms, int same_cell, int prev_count);
int  term_selection_text(Term *t, char *out, size_t cap); /* copies text, returns length */
void term_select_clear(Term *t);

/* Setting for CSI 3 J ("erase saved lines"): 1 (default) lets it drop the scrollback buffer, 0 keeps
   the history. It is a process-wide setting rather than Term state because `ESC c` / RIS rebuilds the
   Term (term_init) and must not silently re-enable it — same pattern as kai_term_loglevel. */
void term_set_clear_scrollback(int on);

/* ---------- paste (src/vt.c) ---------- */
/* Bracketed paste delimiters, sent around clipboard text when the application asked for mode 2004. */
#define PASTE_START "\x1b[200~"
#define PASTE_END   "\x1b[201~"
/* Build the exact bytes to hand the PTY for a paste of `text` (len bytes, not NUL-terminated):
   the raw text when bracketed==0, else PASTE_START + text + PASTE_END. Returns the length written,
   or -1 when out (cap bytes) is too small — the caller then sends the paste unwrapped rather than
   truncating it. Pure byte math, so it is unit-testable without a display. */
int paste_wrap(int bracketed, const char *text, size_t len, char *out, size_t cap);

/* Which text mime type to ask a clipboard source for: 0 = not text, higher = preferred. A source may
   advertise several; picking "the last one advertised" is an accident of its enumeration order. */
int clipboard_mime_rank(const char *mime);

/* Repair a clipboard block before it is written to the PTY: a NUL byte must never reach the pty, and
   a source that serves UTF-16 (Firefox on Wayland labels UTF-16 code units as UTF-8) is decoded to
   UTF-8 rather than pasted as NUL-littered keystrokes. Copies `in` (len bytes, not NUL-terminated)
   into out (cap bytes) and returns the new length, or -1 when out is too small — the caller then
   sends what it has rather than a truncated block. cap >= len*3/2 + 8 always suffices. When the
   block holds no NUL at all it is copied through byte-for-byte, unchanged. */
int paste_repair(const char *in, size_t len, char *out, size_t cap);

/* ---------- configurable key bindings ----------
   Every name below is a line in kai-term.bind, not in kai-term.conf (config_load_binds reads the former,
   config_load the latter; see the config section). The names are the same `key = value` syntax, and a
   binding left in an older kai-term.conf is still honoured — the split is about where a line BELONGS, not
   about breaking the file a user already wrote. */

/* Modifier bits a binding may name. Deliberately NOT xkbcommon's masks: the config parser must not
   depend on xkbcommon (it is linked into the unit tests), and src/main.c translates the effective
   xkb mask into exactly these four bits before matching. Locks (Caps/Num) are not modifiers here. */
enum {
    KB_CTRL  = 1u << 0,
    KB_SHIFT = 1u << 1,
    KB_ALT   = 1u << 2,
    KB_SUPER = 1u << 3,
};

/* Keysyms for the non-printable keys a binding may name. These are the standard X keysym numbers,
   which is what xkb_state_key_get_one_sym() returns; src/main.c _Static_asserts every one of them
   against the xkbcommon constant, so a typo here cannot silently make a binding dead. */
enum {
    KB_KEY_BackSpace = 0xff08,
    KB_KEY_Tab       = 0xff09,
    KB_KEY_Return    = 0xff0d,
    KB_KEY_Escape    = 0xff1b,
    KB_KEY_Space     = 0x0020,
    KB_KEY_Prior     = 0xff55,   /* Page_Up   */
    KB_KEY_Next      = 0xff56,   /* Page_Down */
    KB_KEY_Home      = 0xff50,
    KB_KEY_End       = 0xff57,
    KB_KEY_Insert    = 0xff63,
    KB_KEY_Delete    = 0xffff,
    KB_KEY_Left      = 0xff51,
    KB_KEY_Up        = 0xff52,
    KB_KEY_Right     = 0xff53,
    KB_KEY_Down      = 0xff54,
    KB_KEY_F1        = 0xffbe,
    KB_KEY_F2        = 0xffbf,
    KB_KEY_F3        = 0xffc0,
    KB_KEY_F4        = 0xffc1,
    KB_KEY_F5        = 0xffc2,
    KB_KEY_F6        = 0xffc3,
    KB_KEY_F7        = 0xffc4,
    KB_KEY_F8        = 0xffc5,
    KB_KEY_F9        = 0xffc6,
    KB_KEY_F10       = 0xffc7,
    KB_KEY_F11       = 0xffc8,
    KB_KEY_F12       = 0xffc9,
};

typedef struct {
    uint32_t sym;    /* the key: a printable codepoint or one of KB_KEY_* */
    unsigned mods;   /* KB_* bits that must ALL be held (at least one: see keybind_parse) */
    int set;         /* 0 = the action is unbound (`key_new_tab = none`), 1 = bound */
} KeyBind;

/* Parse "Ctrl+Shift+T" (case-insensitive, modifiers in any order) into a KeyBind. Returns 1 when the
   spec is usable, 0 when it is not (out is then left unbound, and the caller keeps the default).
   A spec with NO modifier is refused on purpose: an unmodified binding would swallow plain typing.
   `keybind_parse` requires exactly one key; keymods_parse (for the tab-prefix binding) requires
   modifiers only. Both are pure string handling, so tests/test_vt.c can check them without a display. */
int keybind_parse(const char *spec, KeyBind *out);
int keymods_parse(const char *spec, unsigned *mods);
/* The same parser, but an UNMODIFIED key is allowed. Only for the file manager's own keys: they are
   looked at ONLY while the file manager owns the keyboard, so `Up` cannot swallow the shell's typing —
   with the file manager off, or with the terminal focused, that same key still reaches the PTY. */
int keybind_parse_plain(const char *spec, KeyBind *out);

/* The keys the file manager uses while it has the keyboard. Grouped in one struct because they are
   fifteen actions of one feature, and main.c matches them as a set. */
typedef struct {
    KeyBind up, down, pageup, pagedown, home, end;   /* move the selection */
    KeyBind left;      /* go to the parent directory (the same action BackSpace has always had) */
    KeyBind right;     /* descend into the selected directory — and NOTHING on a file */
    KeyBind open;      /* Enter: descend into the selected directory, and NOTHING on a file (s95 —
                          typing a file's path moved to `path`, so no key here writes at the prompt
                          unless the user asked for exactly that) */
    KeyBind path;      /* type the selected entry's quoted path at the prompt, without running it */
    KeyBind parent;    /* Backspace: go to the parent directory */
    KeyBind refresh;     /* re-read the current directory */
    KeyBind preview;     /* show / hide the preview pane */
    KeyBind hidden;      /* show / hide dotfiles */
    KeyBind quit;        /* close the file manager */
} FmKeys;

/* ---------- config ---------- */
/* The cap on "names the settings file set explicitly" (see Config.set_keys). 35 settings today; the count
   of accepted names is pinned by tests/test_vt.c, so this cannot be quietly outgrown. */
#define CONFIG_SET_KEYS_MAX 64

typedef struct Config {
    char font[128];
    int  font_size;        /* px */
    int  cols, rows;       /* initial size */
    int  scrollback;
    uint32_t fg, bg, cursor_color;
    /* The selection highlight, and with it the tab bar's active item and the focus border — all three are the
       same "highlight" colour and were one compile-time macro (SEL_BG) until themes made it a choice. */
    uint32_t selection_color;
    float opacity;         /* 0..1 */
    char shell[256];
    /* 1 (default): a recognised shell (fish, bash, zsh) is started with Kai-Term's integration script, which
       gives the file manager a way to ask the SHELL to change its own directory instead of Kai-Term typing
       `cd` at the prompt (src/shellint.c). 0 = every shell is started exactly as it was before this feature
       and the file manager only ever types. An unrecognised shell is never given the script either. */
    int  shell_integration;
    char working_dir[4096]; /* shell's starting directory; empty => $HOME */
    char app_id[128];       /* xdg-shell app_id the compositor uses to identify/match the window */
    int  clear_scrollback; /* 1 (default): CSI 3 J erases the scrollback buffer */
    /* 1 = releasing the selection drag copies to the clipboard (the old behaviour).
       Default 0: selecting only SELECTS, the user copies with Ctrl+Shift+C. */
    int  copy_on_release;
    /* The mouse inside a full-screen program that never asked for the mouse (nano, less, man). Such a
       program owns its own screen and has no scrollback of the terminal's to scroll, so the only thing
       that can move it is keystrokes — see term_wheel_arrow_keys()/term_click_arrow_keys() in vt.c.
       wheel_sends_keys: 1 (default) = the wheel sends Up/Down presses there, which is what makes nano
       and less scroll. 0 = the wheel does nothing in that state (what it did before this existed).
       click_moves_cursor: 1 (default) = a left click walks the program's own cursor to the clicked cell.
       0 = a click only ever selects text. Both leave every other mouse behaviour exactly as it was: a
       program that DID ask for the mouse (vim, htop, tmux) keeps getting reports, and the main screen
       keeps scrolling its scrollback.
       The honest caveat on click_moves_cursor: it is keystroke synthesis, not mouse support, so a program
       that uses the arrow keys for something other than moving a cursor reacts to a click (alsamixer
       would change the slider it is on). That is why it is a switch and not a law. */
    int  wheel_sends_keys, click_moves_cursor;
    /* 1 (default): a second left click in the same cell within CLICK_DCLICK_MS selects the WORD under the
       pointer and a third selects the whole LOGICAL line (a soft-wrapped line is one line, so every row it
       spans is taken). 0 = every click selects exactly what it selected before this existed: one cell, and
       a drag to extend. What counts as a word is the user's rule — a run of non-space cells with TRAILING
       punctuation cut off, leading punctuation kept — and it lives in term_word_span() (src/vt.c), where a
       test can check it; a wide glyph is never cut in half. A word/line gesture also suppresses the
       synthesised cursor-key presses of click_moves_cursor, so a double click inside nano selects instead
       of walking the program's cursor twice. */
    int  click_selects_word;
    /* The cursor's shape is always the thin bar unless an application asks for another one with DECSCUSR
       (there is deliberately no `cursor_style` key — the user asked for one look). These two say how that
       cursor behaves:
       cursor_blink = the half-period in ms the cursor is on, then off (1000 = one second each way, the
       classic rhythm; 0 = never blink, and an application cannot switch the blink back on — see the
       CURBLINK comment above). Blinking also stops while the window has no focus, which is both the
       classic behaviour and the reason an idle unfocused window does not wake up twice a second.
       cursor_width = how many pixels wide the bar is (and how tall the underline DECSCUSR can ask for is).
       The renderer clamps it again to a third of a cell — wider than that is a block wearing a hat — and
       never below 1 px, because a cell can be narrower than 3 px at a small font size. */
    int  cursor_blink;
    int  cursor_width;
    /* Kai-Term drives its own key repeat. repeat_delay = milliseconds a key must be
       held before it starts repeating (default 300; 0 = leave repeat to the compositor, which is the
       behaviour before client-side repeat existed and the escape hatch if the timings ever get in the
       way). repeat_rate = repeats per second once it starts. */
    int  repeat_delay;
    int  repeat_rate;
    /* Margin in px between the window edge and the first/last cell row/column, applied on all four
       sides. Keeps the corner glyphs out of a compositor's rounded window corners. 0 = the grid
       touches the window edge (the layout before this setting existed). The pixels left over after
       whole cells still belong to the bottom/right, so the bottom margin is padding + remainder. */
    int  padding;
    /* Multiplexing: one window hosts up to max_tabs terminal sessions (tabs), each with its own
       shell, grid, scrollback and selection; only one is visible. The four bindings below are what
       drive it, and tab_prefix_mods + a digit 1-9 jumps to that tab. All of them are config strings
       such as "Ctrl+Shift+T"; see keybind_parse. */
    int  max_tabs;
    KeyBind key_new_tab;
    KeyBind key_close_tab;
    KeyBind key_next_tab;
    KeyBind key_prev_tab;
    unsigned tab_prefix_mods;   /* 0 = the digit shortcut is disabled */
    /* How the sessions of this window are SHOWN (see the layout section below): the tab bar and the
       tab/side-by-side toggle. tab_bar is BAR_OFF/BAR_AUTO/BAR_ALWAYS (AUTO = shown from the second
       session on, so a single-session user sees no bar at all), split_max caps how many sessions are
       drawn side by side, and key_toggle_layout swaps the two views. */
    int  tab_bar;
    int  split_max;
    KeyBind key_toggle_layout;
    /* ---- the in-window file manager (src/fm.c; its band is laid out by src/layout.c) ----
       fm_on: start with the file manager shown. Default off — a user who never presses the toggle sees
       exactly the terminal of earlier sessions, and closing the file manager returns to that state.
       fm_size: the PERCENT of the grid area's height the band asks for (layout.c clamps it again against
       the window, so the terminal always keeps its floor). It is a share and not a row count on purpose:
       the band then keeps its proportion when the window is resized or zoomed.
       fm_preview: the preview pane is shown (it can also be toggled at runtime). fm_show_hidden:
       dotfiles are part of the listing from the start. fm_follow: navigating into a directory also
       sends `cd <path>` to the shell, so the prompt lands where the browser is looking.
       fm_focus_on_hover: 1 lets a scroll gesture over a band pane move the KEYBOARD to that pane, as
       earlier sessions did. 0 (the default) means a pane is focused by CLICKING it — the pointer's
       position alone never takes the keyboard, and a wheel over the band only scrolls. */
    int  fm_on, fm_size, fm_preview, fm_show_hidden, fm_follow, fm_focus_on_hover;
    /* The window-level shortcuts; these carry modifiers like every other terminal binding. */
    KeyBind key_file_manager;    /* show / hide the file manager */
    KeyBind key_fm_focus;        /* the keyboard drives the file-manager panes */
    KeyBind key_fm_release;      /* the keyboard drives the terminal pane again */
    /* The file manager's own keys. Unbinding one (`fm_key_open = none`) leaves that action
       unavailable rather than falling back to the default — same rule as the tab bindings. */
    FmKeys fm_key;
    int  loglevel;
    /* ---- whether an APPLICATION may repaint the terminal (OSC 4 / 10 / 11 / 104) ----
       The user asked for the capability and for an off switch in the same answer (s102 question 2: "yes, but
       they were unsure ('I guess ?')… give them an off switch — osc_colors = 1 — so 'any program in my terminal
       can repaint the window' can be turned off without a rebuild"). On (default): OSC 4 sets a palette entry,
       OSC 10/11 set or query the default ink/paper, OSC 104 puts entries back — which is what lets pywal-style
       tools theme a live Kai-Term with no file at all. Off: all four are ignored and logged at debug level.
       Worth knowing what on means: any program in the terminal can change the colours of the whole window, and
       an OSC 10/11 also overrides the `foreground =`/`background =` lines for the rest of the run. */
    int  osc_colors;
    /* ---- themes (src/theme.c) ----
       `theme = NAME` in kai-term.conf. A value with no '/' is looked up as NAME.theme in the `themes/`
       subdirectory of the three config directories (same order, same first-hit-wins rule as config_find);
       a value with a '/' — or starting with '~' — is a path and is opened directly. Empty = no theme.
       Precedence, the user's own words (s102): "value in the kai-term.conf should supersede the theme".
       The theme is applied AFTER the settings file has been read, and only for the colour keys the
       settings file did not mention itself, so a `background =` line wins wherever it sits in the file. */
    char theme[256];
    /* The names the settings file set explicitly — that is how a theme knows which colours it may NOT
       touch. Names rather than one bit per setting: a new setting needs no registration here, and the
       list is written by config.c's own apply() path, so it cannot drift from the parser. */
    char set_keys[CONFIG_SET_KEYS_MAX][32];
    int  n_set_keys;
} Config;

void config_default(Config *c);
/* The settings file. `path` NULL -> the searched kai-term.conf (see config_find); an explicit path (what
   -c gives) is read as-is and skips the search. */
void config_load(Config *c, const char *path);
/* The key-binding file — every `key_*` / `fm_key_*` name lives there. `path` NULL -> the searched
   kai-term.bind. main.c reads it AFTER config_load(), so for a name both files set the bind file wins;
   a binding still written in kai-term.conf is honoured (with a log line saying where it belongs now),
   and a non-binding setting written in kai-term.bind is refused for the same reason. */
void config_load_binds(Config *c, const char *path);
/* The first of $XDG_CONFIG_HOME/kai-term, ~/.config/kai-term, /etc/kai-term that holds a REGULAR file
   called `name`; empty `out` when none of them does, which means "use the built-in defaults". */
void config_find(const char *name, char *out, size_t cap);
/* Expand a leading "~" (or "~/...") to $HOME and copy the result into out. */
void expand_tilde(const char *in, char *out, size_t cap);
/* Cut a '#' comment off a config line in place. ONE rule for every file Kai-Term reads — kai-term.conf,
   kai-term.bind, a .theme — because the rule was wrong once and ate every `#RRGGBB` value (s99). */
void config_strip_comment(char *line);
/* #RRGGBB / 0xRRGGBB / RRGGBB / AARRGGBB -> ARGB, `fallback` when the text is not a colour. Shared with
   src/theme.c: one colour syntax, one parser, so a theme cannot accept a form the config file rejects. */
uint32_t cfg_parse_color(const char *s, uint32_t fallback);
/* Which settings the SETTINGS file set explicitly (not the built-in defaults, not a theme). config.c
   records a name on the apply_setting path; src/theme.c asks before it overwrites one. */
void config_note_set(Config *c, const char *key);
int  config_key_was_set(const Config *c, const char *key);

/* ---------- themes (src/theme.c) ----------
   A theme is a file of COLOURS in the same `key = value` syntax as kai-term.conf (same '#' comment rule,
   shared function) and also in kitty's `key value` spelling, so an existing kitty theme loads unchanged.
   Accepted names live in one table in src/theme.c and are checked against config/kai-term.theme by
   tests/test_vt.c — the same technique that pins kai-term.conf and kai-term.bind. */
/* Resolve a `theme =` value to a path: NAME -> the first NAME.theme in
   $XDG_CONFIG_HOME/kai-term/themes, ~/.config/kai-term/themes, /etc/kai-term/themes; anything with a '/'
   (or a leading '~') is a path and is used as given. Empty `out` when nothing exists. */
void theme_find(const char *value, char *out, size_t cap);
/* Apply one theme file to `c` and to the palette (entries 0-15). A colour the settings file set
   explicitly is skipped with a log line naming both files. Returns the number of keys applied, or -1 when
   the file cannot be opened. */
int  theme_apply_file(Config *c, const char *path);
/* The whole chain for one Config: resolve c->theme and apply it. Returns the same as theme_apply_file,
   or 0 when no theme is configured. */
int  theme_apply_config(Config *c);

/* ---- importing a theme written for ANOTHER program (`--import-theme`, step 1e) ----
   The whole importer lives here rather than in main(): main() is in no linked test binary, so a flag whose
   work is done in main() would be covered only by the CLI script. What is testable is the part that can get
   a colour WRONG — the sniff, the name mapping, the colour folding — and that is what these functions are.
   Every one of them is headless: file in, file out, no Wayland, no GL. */
/* The formats `--import-theme` reads. THF_UNKNOWN means "nothing matched": the importer then refuses and
   lists what it looked for, it never guesses a format (a guessed format is a wrong colour that looks fine). */
enum {
    THF_KAI = 0,     /* kai-term's own `key = value`, and kitty's `key value` — one reader serves both */
    THF_XRES,        /* Xresources: `XTerm*color1: #dc322f` */
    THF_ALACRITTY,   /* alacritty.toml: `[colors.normal]` + `red = '#cc6666'` */
    THF_KONSOLE,     /* Konsole .colorscheme: `[Color1]` + `Color=237,98,104`, and the legacy [Color Scheme] */
    THF_XFCE,        /* xfce4-terminal terminalrc: `ColorPalette=#..;#..` and ColorBackground/Foreground/Cursor */
    THF_JSON,        /* a Windows Terminal / VS Code colour scheme */
    THF_PLIST,       /* iTerm2 .itermcolors: recognised and REFUSED — see theme_import */
    THF_UNKNOWN
};
const char *theme_format_name(int fmt);
/* Which format `text` is, from its content, not its file name: a theme called `x.conf` may be any of them.
   Returns one of the THF_* values above. */
int theme_sniff(const char *text);

/* The colours one imported file names. A field of 0 means "the file did not set it" — no colour a theme can
   ask for is 0x00000000 (see theme_color in src/theme.c) — so an unset colour falls back to Kai-Term's own
   default rather than to black. */
typedef struct ThemeVals {
    uint32_t fg, bg, cur, sel;
    uint32_t col[16];
    char     name[128];    /* the file's own name, when it carries one */
    int      n_set;        /* colours actually stored, 0-20 */
    int      n_drop;       /* keys read but with nowhere to go, or not understood */
} ThemeVals;

/* Read `text` as format `fmt` into `v`. Returns 0 when at least one colour was found, -1 when the format is
   one Kai-Term refuses or the file holds no colour at all. Every key it drops is logged. */
int theme_parse_text(const char *text, int fmt, ThemeVals *v, const char *src_path);

/* Write `v` as a Kai-Term .theme file. Returns 0 or -1. The output is the native format, so an imported
   theme is then read by the SAME reader as a hand-written one — that round trip is a check in test_vt.c. */
int theme_write_file(const ThemeVals *v, const char *path, const char *src_path, int fmt);

/* Where an imported theme goes: the FIRST themes/ directory of the config search that can be created
   ($XDG_CONFIG_HOME/kai-term/themes, else ~/.config/kai-term/themes). Never /etc — importing must not need
   root. Empty `out` when neither is usable. */
void theme_dest_dir(char *out, size_t cap);
/* The `theme =` value a written path corresponds to: the file name without its .theme suffix. */
void theme_name_of_path(const char *path, char *out, size_t cap);

/* The whole chain: read `src_path`, sniff it, map it, and write <dest dir>/<name>.theme.
   `overwrite` decides what happens when that file already exists. Returns:
     0  written (out_path holds it)
     1  it already exists and overwrite was not granted — the caller asks the user, then calls again
    -1  refused or unreadable (the log says why; nothing was written) */
int theme_import(const char *src_path, char *out_path, size_t cap, int overwrite);

/* ---------- pty ---------- */
typedef struct Pty {
    int mfd;
    pid_t child;
    int rows, cols;
    /* Cell size in px, reported to the child in winsize.ws_xpixel/ws_ypixel. Applications that
       draw images (timg, chafa, kitty icat) need it to turn a pixel-sized image into a cell span:
       measured with timg 1.6.3, it auto-selects the kitty graphics protocol ONLY when the PTY
       reports a non-zero pixel geometry, and without it the image is scaled to the wrong aspect.
       0 = not reported (the behaviour before image support). */
    int cell_w, cell_h;
} Pty;

int  pty_spawn(Pty *p, const char *cmd, int rows, int cols, const char *dir);
void pty_setsize(Pty *p, int rows, int cols);
/* Record the cell size and re-issue TIOCSWINSZ. Zooming changes the cell without changing cols x
   rows, and an application that already asked for the geometry must be told the new numbers. */
void pty_set_cells(Pty *p, int cell_w, int cell_h);
void pty_write(Pty *p, const void *buf, size_t len);

/* ---------- multiplexing: several terminal sessions in one window (src/mux.c) ---------- */

#define TAB_MAX 64          /* hard ceiling; Config.max_tabs is clamped to 1..TAB_MAX */

/* One multiplexed session. `term` is its whole screen state (grid, scrollback, selection, alt
   screen), `pty` its shell. The FILE DESCRIPTORS and the child process belong to src/main.c: mux
   never opens, closes, spawns or reaps anything — it owns the Tab array, the active index, the
   per-tab attention flag and the Term lifetime (term_init on open, term_destroy on close). */
typedef struct Tab {
    Term term;
    Pty  pty;
    /* Output arrived while this tab was NOT the visible one. Cleared when the tab becomes visible. Both
       the tab bar (a leading * in its label) and mux_title() report it, so a session that finished a build
       while hidden is findable. src/main.c only sets it for a session that is NOT drawn at all — a pane the
       user can see needs no mark. */
    int  attention;
    /* Shell integration (src/shellint.c), per session because one window has N shells and each was started
       with its own request file: `cd_req` is the file THIS session's shell reads the wanted directory from
       (empty = this session was started without integration — an unsupported shell, `-e COMMAND`, or
       shell_integration = 0), and `int_dead` records that the announced integration stopped answering, which
       is what a nested shell (a plain bash started inside an integrated fish) looks like from here. Both
       travel with the Tab struct when mux_close() shifts the array, which is why they live here and not in a
       side array indexed by tab number — the index a session had at spawn time is not the index it has after
       an earlier tab closes. */
    char cd_req[FM_PATH_MAX];
    int  int_dead;
    /* The last `Term.shell_int_gen` this session saw. main.c compares the two: a new announcement means a
       different shell is now reading the line (the user started one, or a nested one just exited), and that
       revives `int_dead` — without it, the first shell that failed to answer would keep the whole session on
       the typed path even after an integrated one announced. Travels with the Tab like the two above. */
    int  int_gen;
} Tab;

typedef struct Mux {
    Tab tab[TAB_MAX];
    int n;         /* tabs in use, dense from index 0 (closing shifts the later ones down) */
    int active;    /* the visible tab; -1 when n == 0 */
    int max;       /* Config.max_tabs, clamped to 1..TAB_MAX by mux_init */
} Mux;

void mux_init(Mux *m, int max_tabs);
/* Open a tab: term_init only, the caller spawns the PTY and fills the blank cells. The new tab
   becomes the visible one. Returns its index, or -1 when max_tabs is already reached. */
int  mux_open(Mux *m, int rows, int cols, int scrollback);
/* Close tab idx (term_destroy) and shift the later tabs down so the array stays dense. Returns the
   new active index, or -1 when the last tab is gone — the caller must then quit. The caller owns the
   PTY: capture the Pty BEFORE closing the tab (the shift moves it) and close/reap it separately. */
int  mux_close(Mux *m, int idx);
/* Sweep every session's already-painted cells to a new default ink/paper (OSC 10/11). Idempotent, so the
   session that raised the change and has already swept itself may be included. */
void mux_recolor_defaults(Mux *m, uint32_t old_fg, uint32_t old_bg,
                          uint32_t new_fg, uint32_t new_bg);
int  mux_goto(Mux *m, int idx);   /* clamped to the tabs that exist; returns the active index */
int  mux_next(Mux *m);            /* wrap-around; -1 when there is no tab */
int  mux_prev(Mux *m);
Tab *mux_cur(Mux *m);             /* the visible tab, NULL when there is none */
int  mux_count(const Mux *m);
int  mux_active(const Mux *m);
/* Note output on a hidden tab (ignored for the visible one). Returns 1 when that newly SET the flag,
   so a caller can update the window title once instead of on every PTY read. */
int  mux_touch(Mux *m, int idx);
/* Window title text — what a compositor or a taskbar shows (the tab bar carries the same state inside the
   window). One tab -> DEF_TITLE exactly, so a single-session user sees no change; several ->
   "Kai-Term [2/3]", with "!" appended when a hidden session has output the user has not looked at yet. */
void mux_title(const Mux *m, char *out, size_t cap);

/* ---------- layout: where each session is drawn (src/layout.c) ----------
   Pure geometry, no Wayland/EGL/fd, so the whole tab-bar/split feature is unit-testable
   (tests/test_vt.c links it). It is the single place that decides how the window's pixels are shared:
   the tab-bar strip, one rect per tab item (with its label text), and one rect per visible session
   (pane) together with the cols/rows that fit inside it. main.c resizes every displayed session to its
   pane and maps the pointer through that pane's origin; render.c draws exactly what this says. One
   place decides it, because the drawn cell, the click -> cell mapping and the PTY size must agree —
   the same reason term_grid_geometry() exists (see its comment). */

enum { LAYOUT_TABS = 0, LAYOUT_SPLIT = 1 };

/* tab_bar config value. BAR_AUTO is the default: no bar at all while there is one session (a
   single-session window looks exactly like it did before multiplexing existed), the bar appears with the
   second session. BAR_ALWAYS shows it even with one, BAR_OFF never (then the window title and the
   keyboard bindings are the only way to see/switch sessions). */
enum { BAR_OFF = 0, BAR_AUTO = 1, BAR_ALWAYS = 2 };

#define BAR_LABEL_MAX 40    /* bytes of label text per tab item (UTF-8, cut at a codepoint edge) */

/* One session's box. x/y/w/h are window pixels (top-left origin, like the pointer events); cols/rows is
   what term_grid_geometry() says fits inside it, so the leftover pixels sit at the pane's right/bottom
   exactly like they do at the window's — that is what keeps `col = (ptr_x - pane.x) / cell_w` exact. */
typedef struct {
    int x, y, w, h;
    int cols, rows;
    int tab;           /* the mux index this pane shows */
} PaneGeom;

/* One tab-bar item: a rect inside the bar strip plus the text drawn in it. */
typedef struct {
    int x, w;          /* px inside the window (the strip is one cell row tall) */
    int tab;           /* the mux index this item selects */
    int active;        /* 1 = the session the keyboard talks to (drawn highlighted) */
    int attention;     /* 1 = unread output while hidden (drawn as a leading *) */
    char label[BAR_LABEL_MAX];
} BarItem;

/* The file-manager band: the strip of the grid area the browser and the preview take, ABOVE the
   terminal pane. Decided here and nowhere else, for the usual reason — the painted cells, the pointer
   hit test and fm_set_size() must all agree on how many columns and rows each pane has.
   `on` is 0 when the window is too short to hold the band AND a usable terminal pane: the terminal wins,
   exactly as it wins over the tab bar. cols/rows come from term_grid_geometry() on each rect, so the
   leftover pixels sit at the right/bottom of each pane and `col = (ptr_x - list_x) / cell_w` is exact. */
typedef struct {
    int on;            /* the band is drawn at all */
    int list_x, list_y, list_w, list_h;
    int list_cols, list_rows;
    int prev_on;       /* the preview pane is shown (it can be toggled away) */
    int prev_x, prev_y, prev_w, prev_h;
    int prev_cols, prev_rows;
} FmGeom;

/* layout_hit_fm() answers. FM_HIT_NONE means the point is on a terminal pane, the bar, a margin or a gap. */
enum { FM_HIT_NONE = 0, FM_HIT_LIST = 1, FM_HIT_PREVIEW = 2 };

/* How much of the grid area the band asks for is a PERCENT of its height (Config.fm_size), not a row
   count: the band then holds its share through a resize or a zoom instead of becoming huge or tiny
   relative to the terminal. All three numbers here are layout rules, so they live here and not in
   config.c: clamping only in config.c would let a different caller ask for a 1-row band and get one.
   FM_SIZE_MIN/MAX bound the request; FM_ROWS_MIN is the smallest band worth drawing at all, and
   FM_TERM_MIN_ROWS the floor the terminal pane never goes below. */
#define FM_SIZE_DEF 75
#define FM_SIZE_MIN 10
#define FM_SIZE_MAX 95
#define FM_ROWS_MIN 3
#define FM_TERM_MIN_ROWS 3    /* the terminal pane never shrinks below this many rows */

typedef struct {
    int mode;          /* LAYOUT_TABS / LAYOUT_SPLIT */
    int bar_on;
    int active_tab;    /* the session the keyboard talks to (-1 when there is none): the highlighted
                          item, and the pane the focus border is drawn around */
    int bar_x, bar_y, bar_w, bar_h;   /* the strip itself, window px */
    int nitem;
    BarItem item[TAB_MAX];
    int npane;
    PaneGeom pane[TAB_MAX];
    FmGeom fm;         /* the file-manager band; pane[] is what is LEFT of the grid area after it */
} Layout;

/* What the caller knows; layout_compute turns it into a Layout. */
typedef struct {
    int tab;           /* the mux index */
    int attention;     /* Tab.attention */
    const char *title; /* Term.title (OSC 0/2), may be empty or NULL */
} TabInfo;

typedef struct {
    int mode;
    int win_w, win_h;  /* buffer px */
    int cell_w, cell_h;
    int pad;           /* Config.padding: the margin around EVERYTHING, all four sides */
    int gap;           /* px between panes, and between the bar and the grid */
    int ntab, active;
    int split_max;     /* panes side by side at most (>= 1) */
    int bar;           /* BAR_OFF / BAR_AUTO / BAR_ALWAYS */
    int fm_on;         /* the file manager is shown */
    int fm_preview_on; /* its preview pane is shown too */
    int fm_size;       /* percent of the grid area's height the band asks for (clamped here) */
    TabInfo tab[TAB_MAX];
} LayoutReq;

void layout_compute(const LayoutReq *rq, Layout *out);
/* Hit tests in window px. Both return an INDEX INTO THE LAYOUT ARRAY (not a tab index), -1 when the
   point is in the margin, the gap or outside. */
int layout_hit_bar(const Layout *l, int px, int py);
int layout_hit_pane(const Layout *l, int px, int py);
/* FM_HIT_LIST / FM_HIT_PREVIEW when the point is over one of the file-manager panes, FM_HIT_NONE
   otherwise (including when the band is off). Checked BEFORE layout_hit_pane(): with the band on, the
   terminal pane starts below it, and a click inside the band must never reach the shell. */
int layout_hit_fm(const Layout *l, int px, int py);
/* -1 when that session is not drawn in this layout (a hidden tab in tab mode, or a session outside the
   visible pane window in split mode). */
int layout_pane_of_tab(const Layout *l, int tab);
/* The grid size EVERY session of this layout gets, drawn or not. layout_compute gives every pane the same
   height and, in split mode, the same width, so one size describes them all — and that is what lets main.c
   resize a session that is NOT on screen to the size its pane will have, so switching to it never triggers
   a resize (no SIGWINCH storm, no reflow on every tab switch). 0/0 when the layout has no pane at all. */
void layout_grid_size(const Layout *l, int *cols, int *rows);
/* The window size to ASK the compositor for so that a `cols` x `rows` session really fits, bar included.
   It is the exact inverse of layout_compute() for the startup case, and it belongs here rather than in
   main.c for the same reason the rest of the geometry does: the bar's height is one cell ROW (cell_h), and
   writing cell_w there instead leaves the grid one row short of the configured `rows` for every non-square
   cell — a mistake a check in tests/test_vt.c catches only if the arithmetic is in a module the tests link. */
void layout_initial_size(int cols, int rows, int cell_w, int cell_h, int pad, int gap, int bar,
                         int fm_on, int fm_size, int *win_w, int *win_h);

/* ---------- shell integration (src/shellint.c) ----------
   Kai-Term has exactly one channel to the shell: the pty's input stream. So "change the shell's directory
   without disturbing what the user is typing" cannot mean a side channel — it means the shell's OWN line
   editor must consume the bytes as a key binding (nothing is inserted, nothing is echoed) and the directory
   must travel out of band, in a file. shellint_prepare() writes one script per supported shell into a
   private per-instance directory; shellint_argv() builds the command line that starts the shell so it loads
   that script without replacing the user's own configuration; the script announces itself over Kai-Term's
   APC channel (see kai_apc in src/vt.c), which is what lets main.c prefer this path and fall back to typing
   `cd` at the prompt for every shell that never announced. The directory is NEVER part of the sequence, so a
   stray sequence can never run a path. */

/* "fish" / "bash" / "zsh" / "none" — for logs and for the APC announcement. */
const char *shellint_name(int kind);
/* Which integration a shell path supports (basename match, and a login shell's leading dash is ignored).
   SHELLINT_NONE for anything else, including sh, dash, csh, and a full command line given with -e. */
int shellint_kind_for(const char *shell_path);
/* The control sequence that triggers the shell's handler, as bytes. Measured (AGENT.md s90 + s91): it must be
   (a) something the shell's reader parses as ONE key — fish does not match arbitrary literal byte sequences —
   (b) inert in EVERY supported shell when the binding is absent, so a stray write cannot type garbage at the
   user's prompt (this is what bounds the damage when a nested shell, which never announced, is the one
   reading), (c) unbound by default, so Kai-Term takes a key from nobody, and (d) free of the tty's special
   control characters — `stty -a` on a pty reports `quit = ^\`, so a sequence containing 0x1c would SIGQUIT
   whatever owns the terminal whenever the shell is not reading it. C-x C-] (0x18 0x1d) satisfies all four in
   fish and bash, so there is ONE sequence, not a per-shell table; `kind` is kept because a future shell may
   need its own. zsh gets the same bytes and they fire there too (measured 2026-10-20, s94 — zsh 5.9 in the
   integration gate, ack in 54 ms); its inert-when-unbound behaviour has never been measured. */
const char *shellint_seq(int kind, size_t *len);
/* Create the private directory and write every script into it. `dir` receives the path. Returns 0 on
   success; on any failure nothing is created and the caller must run without integration. */
int shellint_prepare(char *dir, size_t cap);
/* Remove what shellint_prepare() made. Safe to call with an empty or already-removed path. */
void shellint_cleanup(const char *dir);
/* The request file for one session: the shell reads the directory to enter from here. `session` is an id that
   must never repeat within one run — NOT the tab index, which is reused when a tab closes and the array
   shifts. */
void shellint_request_path(const char *dir, int session, char *out, size_t cap);
/* Write `path` into that session's request file (atomically: temp + rename, so the shell can never read a
   half-written path). 0 on success. */
int shellint_write_request(const char *req_path, const char *path);
/* The command line to hand pty_spawn() (which runs it through /bin/sh -c) so the shell loads the
   integration. Returns 0 and fills `cmd` when `kind` is supported. */
int shellint_argv(int kind, const char *dir, const char *req_path, char *cmd, size_t cap);

/* ---------- file manager (src/fm.c) ----------
   An in-window browser: a listing of one directory, a context-sensitive preview of the selected entry,
   and the ordinary terminal below them. fm.c is MODEL ONLY — no Wayland, no EGL, no threads, no
   rendering. That is what makes the feature testable (tests/test_fm.c drives a real directory tree
   headlessly) and what makes it non-blocking: fm_scan_build() and fm_preview_build() are pure functions
   of a path that touch nothing but malloc and the filesystem, so src/main.c can run them on a worker
   thread and install the answer back on the main thread. The two panes are painted into ordinary Cell
   grids, which is what makes them read as part of the terminal: the renderer draws them with the same
   font, colours and passes it uses for a session's grid. */

#define FM_NAME_MAX   256             /* one entry's name (NAME_MAX is 255 on Linux) */
#define FM_ERR_MAX    256             /* one human-readable error line */
#define FM_ENTRIES_MAX 8192           /* entries one listing keeps; the rest are dropped with a warning */
#define FM_PREVIEW_BYTES (256u * 1024u)   /* bytes of a text preview read from disk */
#define FM_PREVIEW_DIR_MAX 512        /* entries shown in a directory preview */
#define FM_PREVIEW_FILE_MAX (16u * 1024u * 1024u)  /* largest image file a preview will read */
#define FM_META_LINES 16              /* metadata lines one preview shows */
#define FM_META_WIDTH 128             /* bytes per metadata line */

/* Accent colours for the two panes, in the header so tests/test_fm.c can pin the colour a token got.
   They are ACCENTS only: the base ink and paper are the configured Config.fg/Config.bg, passed to
   fm_paint(), so the file manager matches the terminal. Chosen to read on a dark background and to
   agree with what ls and editors do (directory blue, executable green, string green, comment grey). */
#define FM_DIR_COL   0xff6cb6ffu   /* directories */
#define FM_LINK_COL  0xff3ddad7u   /* symlinks */
#define FM_EXEC_COL  0xffa5d6a7u   /* owner-executable files */
#define FM_ERR_COL   0xfff26d78u   /* anything that failed */
#define FM_KEY_COL   0xff6cb6ffu   /* keyword, section, heading */
#define FM_STR_COL   0xffc3e88du   /* string literal */
#define FM_NUM_COL   0xfff78c6cu   /* number, shell variable */
#define FM_CMT_COL   0xff8b949eu   /* comment */
#define FM_PP_COL    0xffd2a8ffu   /* preprocessor, decorator, tag */
#define FM_TYPE_COL  0xff4ec9b0u   /* type name, json key, make target */

/* What a listing entry IS. Decided from lstat(), so a symlink is reported as a symlink and never as
   whatever it points at (a dangling link must still be listable). */
enum { FMK_DIR = 0, FMK_FILE, FMK_SYMLINK, FMK_FIFO, FMK_SOCKET, FMK_DEVICE, FMK_MISSING };

/* What the preview pane shows for the selected entry. */
enum { FMP_NONE = 0, FMP_DIR, FMP_TEXT, FMP_IMAGE, FMP_META, FMP_EMPTY };

/* Which region the keyboard drives while the file manager is on. FM_FOCUS_TERM is the state the
   terminal is in when the file manager is off, so closing it is just focus = FM_FOCUS_TERM + on = 0. */
enum { FM_FOCUS_TERM = 0, FM_FOCUS_LIST, FM_FOCUS_PREVIEW };

/* Languages the text preview can colour. Chosen from the file name; FM_LANG_NONE means the text is
   shown in the plain foreground colour. */
enum { FM_LANG_NONE = 0, FM_LANG_C, FM_LANG_PY, FM_LANG_SH, FM_LANG_JSON, FM_LANG_MD,
       FM_LANG_MAKE, FM_LANG_INI, FM_LANG_HTML, FM_LANG_RS, FM_LANG_JS, FM_LANG_GO,
       FM_LANG_CSS, FM_LANG_YAML, FM_LANG_N };

/* One entry of a directory listing. Only the NAME is stored: a path would make one entry 4 KB and a
   capped listing of 8192 entries 32 MB, while fm_entry_path() rebuilds the full path in a caller's
   buffer in one snprintf. */
typedef struct {
    char name[FM_NAME_MAX];
    uint8_t kind;        /* FMK_* */
    uint8_t hidden;      /* name starts with '.' — kept in the listing, hidden from the view */
    uint8_t exec;        /* owner-executable regular file: drawn like one */
    mode_t mode;
    uint64_t size;
    int64_t mtime;       /* seconds, st_mtime */
} FmEntry;

/* The answer to one listing request. Built off the main thread, installed on it. `seq` is the request
   id the caller passed in; an answer whose seq is no longer the newest one is dropped, which is what
   keeps a slow scan of a directory the user already left from replacing the listing on screen. */
typedef struct {
    char dir[FM_PATH_MAX];
    FmEntry *ent;        /* malloc'd, n entries, sorted (directories first) */
    int n;
    int truncated;       /* the directory holds more than max_entries */
    char err[FM_ERR_MAX];
    unsigned long seq;
} FmScan;

/* The answer to one preview request. Only the fields of the chosen `kind` are filled; `meta` is filled
   for every kind, because the header of every preview shows the same facts. */
typedef struct {
    char path[FM_PATH_MAX];
    int kind;            /* FMP_* */
    /* FMP_DIR */
    FmEntry *ent;
    int n;
    /* FMP_TEXT */
    char *text;          /* malloc'd, NUL-terminated, control characters already replaced */
    size_t len;
    int lang;            /* FM_LANG_* */
    /* FMP_IMAGE */
    uint8_t *rgba;       /* malloc'd RGBA8 (NOT premultiplied — the caller does that) */
    int w, h;
    char fmt[8];         /* "png" / "jpg" / "gif" / "webp", from the bytes, not the name */
    /* every kind */
    int truncated;       /* what the entry holds is bigger than what a preview reads: more text than
                            FM_PREVIEW_BYTES, more directory entries than FM_PREVIEW_DIR_MAX, or an
                            image file bigger than FM_PREVIEW_FILE_MAX */
    char meta[FM_META_LINES][FM_META_WIDTH];
    int nmeta;
    char err[FM_ERR_MAX];
    unsigned long seq;      /* the request this answers */
    unsigned long uid;      /* unique per built preview, never reused: the renderer keys the preview's
                               GL texture on this, so a re-installed preview re-uploads and a stale one
                               can never be painted from a recycled malloc address. */
} FmPreview;

/* One painted pane. The renderer walks it exactly like a session grid (row-major, cell (x,y) at
   cell[y*cols+x]), so the file manager needs no new drawing code path beyond "a grid with no cursor
   and no selection". */
typedef struct {
    Cell *cell;
    int cols, rows;
} FmPane;

typedef struct {
    int on;              /* the file manager is shown at all */
    int focus;           /* FM_FOCUS_* */
    int preview_on;      /* the preview pane is shown (it can be toggled away) */
    int show_hidden;     /* dotfiles are part of the view */
    char dir[FM_PATH_MAX];
    char err[FM_ERR_MAX];

    FmEntry *ent;        /* the installed listing (ecap allocated entries) */
    int n, ecap;
    int *vis;            /* vis[0..nvis-1] index ent[]: the view after the hidden filter */
    int nvis, vcap;
    int truncated;       /* the listing is not the whole directory */

    int sel, top;        /* selected position in vis[], first visible row of the list pane */
    int pv_scroll;       /* rows the preview pane's BODY is scrolled down by (text and directory
                            previews). Owned by the wheel over that pane; reset when a new preview is
                            installed, clamped to what the pane can show. The LIST pane has no such field
                            because its scroll is its selection — fm->top follows fm->sel. */
    int list_cols, list_rows, prev_cols, prev_rows;   /* what the layout asked for */
    FmPane list, prev;   /* what the renderer draws */

    unsigned long seq;       /* monotonic request-id issuer, SHARED by both kinds so an id can never
                                name a listing and a preview at once */
    unsigned long scan_req;  /* id of the newest LISTING request (0 = none issued yet) */
    unsigned long pv_req;    /* id of the newest PREVIEW request (0 = none issued yet) */
    int have_listing;        /* ent[] really holds a listing of fm->dir */
    FmPreview *pv;       /* the installed preview, owned here */
    int dirty;           /* the panes must be repainted */
} FileMgr;

void fm_init(FileMgr *fm, const char *dir);
void fm_destroy(FileMgr *fm);
/* The size the layout gave the two panes. Reallocs and blanks the cell grids when it changes. */
void fm_set_size(FileMgr *fm, int list_cols, int list_rows, int prev_cols, int prev_rows);

/* ---- listing: build (worker thread) then install (main thread) ---- */
FmScan *fm_scan_build(const char *dir, unsigned long seq);
/* The same with an explicit entry cap, so a test can pin the truncation behaviour without creating
   FM_ENTRIES_MAX files. fm_scan_build() is this with FM_ENTRIES_MAX. */
FmScan *fm_scan_build_n(const char *dir, unsigned long seq, int max_entries);
void fm_scan_free(FmScan *s);
/* Take ownership of the answer. Returns 0 and frees it when it answers a request that is no longer the
   newest one (the user navigated away while the scan ran). */
int  fm_install_scan(FileMgr *fm, FmScan *s);
/* Ask for a listing of dir (0 = unchanged, already showing it). Bumps fm->seq; the caller hands the new
   seq to the worker. */
int  fm_go(FileMgr *fm, const char *dir, unsigned long *seq);
int  fm_go_sel(FileMgr *fm, unsigned long *seq);    /* descend into the selected directory */
int  fm_parent(FileMgr *fm, unsigned long *seq);
int  fm_refresh(FileMgr *fm, unsigned long *seq);

/* ---- selection ---- */
void fm_select(FileMgr *fm, int pos);
void fm_move(FileMgr *fm, int delta);   /* +1 down, -1 up; keeps the selection inside the view */
void fm_page(FileMgr *fm, int pages);   /* one page = the list pane's entry rows */
void fm_home(FileMgr *fm);
void fm_end(FileMgr *fm);
int  fm_count(const FileMgr *fm);       /* entries in the view */
int  fm_sel(const FileMgr *fm);         /* selected position, -1 when the view is empty */
const FmEntry *fm_sel_entry(const FileMgr *fm);
int  fm_entry_path(const FileMgr *fm, int pos, char *out, size_t cap);
/* Show or hide dotfiles. Rebuilds the view from the listing already in memory — no second scan of the
   disk — and keeps the selection a position in the new view. */
void fm_set_hidden(FileMgr *fm, int show);
/* Which entry of the view the row of the LIST PANE shows, or -1 when that row is chrome (the header or
   the footer) or past the end. The painter owns the row layout, so the hit test lives next to it:
   main.c must not re-derive where the entries start. */
int  fm_row_pos(const FileMgr *fm, int row);
/* Does a wheel gesture over the band move the KEYBOARD into it? Returns the FM_FOCUS_* to apply, or -1
   for "leave the keyboard where it is". `steps` is the whole-cell scroll the gesture produced (0 = it
   scrolled nothing, which must never move the keyboard) and `on_hover` is the `fm_focus_on_hover =`
   setting: 0 (the default) means only a CLICK takes the keyboard, 1 keeps the older behaviour where the
   pane under the pointer decides. A pure decision so the rule is testable — main.c is in no test binary. */
int  fm_wheel_focus(int hit, int steps, int on_hover);
/* Scrolling the PREVIEW pane's body. The wheel over that pane owns pv_scroll (the listing has its own
   selection, the preview has none), and the painter is the only thing that reads it. delta > 0 moves
   toward the END of the file (wheel down). Clamped to [0, fm_preview_scroll_max()], which is 0 for a
   preview with nothing to scroll (an image, plain metadata), so calling it for every kind is harmless.
   It is reset by fm_install_preview(): a new file starts at its beginning. */
void fm_preview_scroll(FileMgr *fm, int delta);
int  fm_preview_scroll_max(const FileMgr *fm);
/* The same scroll, driven by KEYS while the preview pane owns the keyboard (main.c routes the band's
   Page_Up/Page_Down/Home/End here in that state, and to fm_page()/fm_home()/fm_end() in the browser).
   fm_preview_page_rows() is what one page means: the body rows the pane shows at once, so a held Page
   key walks the file with no line shown twice and none skipped. fm_preview_top()/fm_preview_bottom()
   are Home and End. All three go through fm_preview_scroll(), so the clamp has one owner and a preview
   with no body (an image, plain metadata) simply does not move. */
int  fm_preview_page_rows(const FileMgr *fm);
void fm_preview_page(FileMgr *fm, int pages);   /* pages < 0 = toward the top */
void fm_preview_top(FileMgr *fm);
void fm_preview_bottom(FileMgr *fm);

/* ---- preview: build (worker thread) then install (main thread) ---- */
FmPreview *fm_preview_build(const char *path, unsigned long seq);
void fm_preview_free(FmPreview *p);
int  fm_install_preview(FileMgr *fm, FmPreview *pv);   /* same stale-answer rule as fm_install_scan */
/* The path that SHOULD be previewed right now (the selected entry, or the current directory when
   nothing is selected). 1 when it differs from what is installed, and then *seq gets the id to ask
   the worker for and path the path to read. */
int  fm_preview_request(FileMgr *fm, unsigned long *seq, char *path, size_t cap);

/* ---- painting ---- */
/* Fill both cell grids from the current state. fg/bg/sel are the CONFIGURED colours, so the file manager
   matches the terminal instead of inventing a palette — `sel` is the highlight of the selected row, the
   same value the terminal's selection band uses (never the SEL_BG macro, which is only the default). */
void fm_paint(FileMgr *fm, uint32_t fg, uint32_t bg, uint32_t sel);
/* Where the preview picture goes inside the preview pane, in CELLS, aspect ratio preserved for these
 * cell dimensions: the renderer draws the quad and fm.c decides the box, because the box is what a test
 * can check and what keeps a 3000x200 photo from being stretched to the pane. px_w/px_h are the same box
 * in PIXELS — the cell box is that plus up to one cell of ceil() slack, and drawing the cell box would
 * stretch the picture by the slack. */
void fm_image_fit(const FileMgr *fm, int cell_w, int cell_h,
                  int *col, int *row, int *cols, int *rows, int *px_w, int *px_h);

/* ---- small shared formatters (also used by the tests) ---- */
void fm_human_size(uint64_t n, char *out, size_t cap);
void fm_human_time(int64_t t, char *out, size_t cap);
void fm_mode_string(mode_t m, char out[11]);   /* "-rwxr-xr-x" / "drwxr-xr-x" / "lrwxrwxrwx" */
int  fm_lang_for_path(const char *path);       /* FM_LANG_* from a file name */

/* Text preview colouring. State carries from line to line, because a C block comment does not stop at
 * the end of a line. fg has one entry per BYTE of line (all bytes of one codepoint get the same colour,
 * so the painter takes the colour of the lead byte). */
typedef struct { int lang; int in_block; } FmHl;
void fm_hl_init(FmHl *st, int lang);
void fm_hl_line(FmHl *st, const char *line, size_t len, uint32_t *fg, uint32_t base);

/* ---------- font / glyph atlas ---------- */
typedef struct Glyph {
    uint32_t cp;
    int w, h;              /* bitmap size */
    int bearing_x, bearing_y, advance;
    float u0, v0, u1, v1;  /* atlas texcoords */
    void *face;            /* FT_Face this glyph was rasterized from (primary or fallback) */
} Glyph;

/* A fallback font face loaded on demand for codepoints the primary font lacks. */
typedef struct FallbackFace {
    char path[4096];
    void *face;            /* FT_Face */
} FallbackFace;

/* A COLOR emoji glyph: the single glyph HarfBuzz shapes a whole grapheme cluster into,
   rasterized from a color (CBDT/CBLC or SVG) emoji font. Metrics are UNSCALED bitmap
   pixels (the strike's native size); the renderer scales them to the cell at draw time,
   so zooming never re-rasterizes the emoji bitmap. Keyed by the full codepoint sequence. */
typedef struct ColorGlyph {
    uint32_t seq[CLUSTER_MAX + 1];   /* cluster codepoints (anchor first) */
    int      nseq;
    unsigned gid;                    /* glyph id in the color face (0 = no color glyph) */
    int      w, h;                   /* bitmap size in strike px */
    int      bearing_x, bearing_y;   /* bitmap origin in strike px */
    float    u0, v0, u1, v1;         /* color-atlas texcoords */
} ColorGlyph;

typedef struct Font {
    void *ftlib;           /* FT_Library */
    void *ft;              /* FT_Face (primary) */
    int px;                /* current pixel size (used to size fallback faces) */
    int fc_ready;          /* 1 while the fontconfig library is initialized for fallback lookups */
    FallbackFace *fb;      /* fallback faces, keyed by path (linear scan; few entries) */
    int nfb, fbcap;
    int cell_w, cell_h, ascent, descent;
    int atlas_w, atlas_h;
    uint8_t *atlas;        /* alpha8, atlas_w*atlas_h */
    uint32_t atlas_tex;
    int shelf_x, shelf_y, shelf_h;
    Glyph **tab;           /* open-addressing hash by cp */
    unsigned tab_mask, tab_count;
    int atlas_dirty;       /* renderer uploads atlas when set */
    float scale;           /* zoom factor applied at raster time */

    /* ---- color emoji (HarfBuzz shaping + FreeType color bitmaps) ---- */
    int      color_ok;     /* 1 when a color emoji face + hb font are usable */
    void    *cface;        /* FT_Face of the color emoji font (bitmap-only strike) */
    void    *hbf;          /* hb_font_t for cface */
    int      color_px;     /* nominal strike height in px (scale = cell_h / color_px) */
    uint8_t *catlas;       /* RGBA8 color atlas (premultiplied, from BGRA bitmaps) */
    int      catlas_w, catlas_h;
    uint32_t catlas_tex;   /* GL texture of catlas (renderer owns, font marks dirty) */
    int      catlas_dirty;
    int      catlas_broken; /* 1 while the renderer cannot get GPU storage for catlas: draw the mono
                               fallback instead (the renderer sets and clears it, font.c never reads it
                               except through the renderer's own emit path) */
    int      cshelf_x, cshelf_y, cshelf_h;
    ColorGlyph **ctab;     /* open-addressing hash over the cluster sequence */
    unsigned ctab_mask, ctab_count;
} Font;

int  font_init(Font *f, const char *pattern, int px);
int  font_resize(Font *f, int px);   /* re-rasterize the atlas at a new pixel size */
Glyph *font_glyph(Font *f, uint32_t cp);
/* Shape seq[0..n-1] (an emoji grapheme cluster) with HarfBuzz and return its single color
   glyph, or NULL when color emoji are unavailable / the sequence has no color glyph. */
ColorGlyph *font_color_glyph(Font *f, const uint32_t *seq, int n);
void font_destroy(Font *f);

/* ---------- emoji / width predicates (src/vt.c) ---------- */
int cp_is_emoji(uint32_t cp);        /* a base codepoint that may start an emoji cluster */
int cp_is_cluster_extend(uint32_t cp);  /* ZWJ / variation selector / skin tone / tag char */
int cp_is_wide(uint32_t cp);         /* occupies 2 grid cells (CJK + Emoji_Presentation) */
/* Width of a whole grapheme cluster: 1 when the base alone is wide, or when a modifier inside
   the cluster (U+FE0F emoji presentation, U+20E3 keycap) switches an emoji-capable base to the
   wide emoji presentation. seq[0] is the anchor codepoint. */
int cluster_is_wide(const uint32_t *seq, int n);

/* ---------- renderer (EGL + GL) ---------- */
typedef struct Render {
    void *edpy, *esurf, *ectx;   /* EGLDisplay, EGLSurface, EGLContext */
    unsigned prog;
    unsigned vao, vbo;           /* vbo: dynamic per-vertex (fallback) or per-instance data */
    unsigned quad_vbo;           /* static unit-quad corners (a_pos) for the instanced path */
    float *vdata;                /* CPU staging: per-instance records (19 floats each) */
    int vcap;
    float *edata;                /* CPU staging: expanded per-vertex data (fallback only) */
    int ecap;
    int vw, vh;                  /* buffer size in px */
    /* NOTE: there is no grid origin here any more. Since the tab bar / side-by-side layout the frame is
       a list of rectangles (Layout in kai-term.h), each with its own origin, so main.c passes them per
       frame instead of setting one global offset. */
    int u_mvp, u_atlas, u_catlas, u_img, u_sel;
    /* One GL texture per image the model currently shows (see KImage). Keyed by KImage.serial,
       which is unique per session and never reused, so a recycled pointer can never make the
       renderer reuse a stale texture. Entries for images that are not on screen are dropped, so
       the GPU never holds more than what is visible. */
    ImgTex *imgtex;
    int nimgtex, imgtex_cap;
    int img_frame;               /* bumped once per frame; ImgTex.seen == img_frame means "used this
                                    frame", which is how textures of pictures that left the screen go */
    /* The file manager's preview picture: at most one at a time, so it gets one texture instead of a
       cache. Re-uploaded when FmPreview.uid changes (a new preview was installed) or when the pixels
       were resized. fm_px is the premultiplied copy the shader wants — img.c hands out plain RGBA and
       the preview model keeps it that way so its tests can compare decoded bytes. */
    unsigned fm_tex;
    unsigned long fm_tex_uid;
    int fm_tex_w, fm_tex_h;
    uint8_t *fm_px;
    int fm_px_cap;
    void *set_div;               /* glVertexAttribDivisorARB or NULL */
    void *draw_inst;             /* glDrawArraysInstancedARB or NULL */
    int inst_ok;                 /* 1 when both ARB instancing extensions are available */
    float opacity;               /* window background alpha 0..1 (from Config.opacity) */
    uint32_t bg;                 /* configured background ARGB, used to clear the buffer so the
                                    Config.padding margin matches the cell backgrounds */
    uint32_t fg;                 /* configured foreground ARGB: the tab bar's text colour, and the base
                                    the inactive item / separator colours are mixed from. Chrome colours
                                    come from the CONFIG, never from a cell's pen — an application can
                                    repaint its own cells but not the bar that names its tab. */
    uint32_t cursor_color;       /* the `cursor_color =` line: the colour the visible session's cursor is
                                    drawn with, and the base the dimmed cursor of a drawn-but-not-visible
                                    session is mixed from. It lives HERE and not only in Config because a
                                    configured colour that never reaches the renderer is not implemented —
                                    this very key parsed, was stored, was tested at the parser and was read
                                    by nothing (ISSUES.md, s102). */
    uint32_t selection_color;    /* the `selection_color =` line: the selection band (also the shader's
                                    u_sel uniform), the visible session's tab-bar item, and the focus
                                    border. Built-in default is the SEL_BG macro. */
    int cursor_width;            /* the `cursor_width =` line: how many pixels wide the bar cursor is, and
                                    how tall the underline is. Same lesson as cursor_color above — a key
                                    that stops in Config is not implemented — so it reaches the renderer
                                    here and render_cursor() clamps it to the cell (1..cell/3). */
    /* The blink phase of THIS frame: main.c's cursor-blink timer flips W.cursor_on and calls redraw(), and
       the frame copies it here. It is window state, never Term state — term_init() memsets Term for every
       new tab, and a blink that lived in the model would survive a tab switch it has no business surviving.
       Only the visible session's cursor blinks: render_draw() forces 1 for a pane that is drawn but not the
       active one, which is the classic behaviour and keeps a dimmed cursor steady. */
    int cursor_on;
    void *ecfg;                  /* EGLConfig */
    void *nwin;                  /* EGLNativeWindowType (wl_egl_window) */
    /* Texture uploads fail when the GPU is out of memory (this box runs a local LLM that holds most of
       the VRAM). Every upload site logs the failure, and none of them may draw with the texture that
       has no texels — on the NVIDIA driver that faults inside libnvidia-eglcore. These counters back the
       error lines off to about one per two seconds while a retry loop runs, and catlas_wait paces the
       color-atlas retry itself (see render_draw). */
    int atlas_upload_fails, catlas_upload_fails, img_upload_fails, fm_upload_fails;
    int catlas_wait;             /* frames to skip before re-trying a refused color-atlas upload */
} Render;

/* ---------- image decoding (src/img.c) ----------
   The one place bytes become pixels. Both producers share it: src/vt.c (the kitty graphics protocol,
   where an application transmits a picture inline) and src/fm.c (the file manager's preview pane, where
   the picture is read from disk). Output is always RGBA8, never premultiplied — the caller premultiplies
   when it knows where the pixels are going (see img_premultiply and the renderer's a_glyph == 3 path).
   PNG/JPEG/WebP use the system libraries; GIF is decoded in-tree (giflib is not a dependency). */

/* Caps every decoder enforces, so neither a hostile escape stream nor a 30 MB photo can make a frame
   late or exhaust the machine. One definition, because the graphics path and the preview path must
   refuse the same picture. */
#define IMG_DIM_MAX  4096            /* accepted px per side */
#define IMG_PIX_MAX  (16LL << 20)    /* accepted pixels per image */

/* 1 when these bytes are a format img_decode_mem can read. `kind` (8 bytes, may be NULL) comes back as
   "png" / "jpg" / "gif" / "webp", or "" when nothing matched. Recognition is by the bytes themselves,
   never by a file name or by what a protocol's f= field claims. */
int img_sniff(const uint8_t *mem, size_t n, char *kind);
uint8_t *img_decode_png(const uint8_t *mem, size_t n, int *w, int *h);
/* Sniff, then decode. Returns malloc'd RGBA8 (w*h*4) or NULL; the size outputs are 0 on failure.
   `kind` (8 bytes or NULL) reports which decoder ran, which is what the preview pane shows for an image. */
uint8_t *img_decode_mem(const uint8_t *mem, size_t n, int *w, int *h, char *kind);
/* c + bg*(1-c.a) is how the shader composites every texel (no GL blending, and a Wayland EGL buffer is
   premultiplied), so every picture must pass through here once. */
void img_premultiply(uint8_t *rgba, long long npix);

/* ---------- image model helpers (src/vt.c) ---------- */
void term_set_cell_px(Term *t, int w, int h);
/* Fill out[] with the images of this session that are at least partly inside the viewport,
   sorted by z then serial (the order they must be drawn in). Returns the count. Pure grid math,
   so tests/test_vt.c can pin it without a display. */
int  term_visible_images(Term *t, ImgView *out, int cap);
int  term_image_count(const Term *t);

/* ---------- renderer (EGL + GL) ---------- */
/* Every CHROME colour of one frame, derived from the configured palette in Render (bg/fg/cursor_color)
   and from nothing else: the selection band, the tab bar's items and separators, the focus border, and
   the cursor of the visible session and of a session that is drawn but not the visible one. An
   application may repaint its own cells but never the chrome that names its tab (see Render.fg).
   It is a function rather than six locals inside render_draw() for one reason: a configured colour that
   stops in the parser is invisible to every test that only reads Config, so the check has to sit where
   the value becomes a vertex colour. Pure — no GL, no font, no Term, no allocation — so
   tests/test_render.c calls it with no display and no context. */
typedef struct Chrome {
    uint32_t sel_bg;                      /* selection band; also the shader's u_sel uniform */
    uint32_t item_off_bg, item_off_fg;    /* a bar item that is not the visible session */
    uint32_t item_on_bg;                  /* the visible session's bar item */
    uint32_t sep;                         /* the bar's separators */
    uint32_t cursor;                      /* the visible session's cursor */
    uint32_t cursor_off;                  /* a drawn-but-not-visible session: cursor mixed toward bg */
    uint32_t border;                      /* the focus border */
} Chrome;
void render_chrome(const Render *r, Chrome *out);

/* What ONE cursor cell is drawn as. The other half of the s102 lesson: the shape decision must live where a
   test can reach it, not inside the draw loop (render_draw() is in no test binary, exactly like main.c).
   Four outputs, because the three passes of a frame each ask a different question about the same cell, and
   they MUST agree — the comment inside render_draw() ("must match pass 2 exactly") exists because a cursor
   that disagrees with the selection band erases itself. Pure: no GL, no font, no Term, no allocation, so
   tests/test_render.c checks the whole truth table with no display.

   style    CUR_UNSET (no DECSCUSR seen — Kai-Term draws a bar) / CUR_BLOCK / CUR_BAR / CUR_UNDERLINE
   visible  0 draws nothing at all: DEC 25l, a scrolled-back viewport, or the blink phase being off
   band_on  a selection band is painted on this cell. A BLOCK steps aside for it (the band is the highlight
            and the ink is tinted with the cursor colour instead) — that is today's rule and it stays. A BAR
            does not step aside, because its quad is emitted in batch B AFTER the bands, so it survives.
   invert   pass 3 swaps the ink with the cell's own background (a block over no band)
   tint     pass 3 paints the ink IN the cursor colour (a block under a band)
   x,y,w,h  the separate quad's rect in cell-local pixels, origin at the cell's top-left */
typedef struct CursorDraw {
    int block, quad, invert, tint;
    int x, y, w, h;
} CursorDraw;
void render_cursor(int style, int visible, int band_on, int cell_w, int cell_h, int width_px,
                   CursorDraw *out);

int  render_init(Render *r, void *wl_display, void *egl_window, int w, int h, float opacity,
                 uint32_t bg, uint32_t fg, uint32_t cursor_color, uint32_t selection_color,
                 int cursor_width);
/* Draw one frame: the tab bar (if the layout says so), every pane's grid, and the file-manager band if
   the layout has one. Two vertex batches (backgrounds, then bands + glyphs + chrome) with one draw call
   per application picture between them — see the header comment of src/render.c for why the frame cannot
   be a single call.
   `lay` comes from layout_compute() and carries every rectangle; `terms` is indexed by TAB index, so the
   grid of pane p is terms[lay->pane[p].tab] (NULL draws an empty pane). `fm` is the file manager whose
   cell grids the layout laid out (NULL or fm->on == 0 draws no band); its painted panes go through the
   same two passes as a session grid, and its preview picture is drawn between the batches like any other
   picture. Returns 1 when a new buffer was attached, 0 when nothing was drawn (a failed swap, or a
   refused texture upload that the renderer can live without), and -1 when the glyph atlas could not be
   uploaded even after dropping every picture texture: nothing can be drawn at all, and the caller must
   exit rather than keep going (drawing with an unbacked texture segfaults inside the driver). */
int render_draw(Render *r, Font *f, const Layout *lay, Term *const *terms, const FileMgr *fm);
void render_resize(Render *r, int w, int h);
void render_destroy(Render *r);

/* ---------- misc utils ---------- */
static inline int imin_(int a, int b) { return a < b ? a : b; }
static inline int imax_(int a, int b) { return a > b ? a : b; }
static inline int iclamp_(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
/* Float clamp. NEVER use iclamp_ for a float: its parameters are int, so iclamp_(0.5f, 0.0f, 1.0f)
   silently truncates to 0 (that is exactly how window opacity 0.5 became fully transparent). */
static inline float fclamp_(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
int  utf8_encode(uint32_t cp, char *out);   /* returns length */

#endif /* KAI_TERM_H */
