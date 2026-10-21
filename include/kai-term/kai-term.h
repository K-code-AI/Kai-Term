/* Kai-Term — lightweight Wayland terminal emulator. Shared header. */
#ifndef KAI_TERM_H
#define KAI_TERM_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>   /* mode_t, named by FmEntry/FmPreview; included directly rather than relying on another header */

/* ---------- logging ---------- */
enum { L_ERROR = 0, L_WARN, L_INFO, L_DEBUG };
extern int kai_term_loglevel;
/* The format attribute is what makes the compiler check every klog call site against its format string.
   Guarded because it is a compiler extension: without it the code is the same, only less checked. */
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

/* The ink and paper every blank, erased, scrolled-in and reflowed cell actually reads; DEF_FG/DEF_BG
   are only the built-in defaults these start at. main.c calls the setter once, after the config loads
   and before the first Term exists. Process-wide because term_init() memsets the whole Term. */
void     term_set_default_colors(uint32_t fg, uint32_t bg);
uint32_t term_default_fg(void);
uint32_t term_default_bg(void);

/* The 256-entry palette. Entries 0-15 are the ANSI colours a theme may set; 16-231 (6x6x6 cube) and
   232-255 (grey ramp) are computed and never themeable. Filled on first call, so a caller that never
   initialises anything gets the built-in palette, not zeros. A Cell stores a RESOLVED colour, so
   changing an entry changes future SGR lookups and repaints nothing already on screen. */
const uint32_t *term_palette(void);
void term_set_palette_entry(int idx, uint32_t argb);
void term_palette_reset(void);
void term_palette_entry_reset(int idx);   /* one entry back to built-in: `OSC 104;N`. term_palette_reset() is the whole-palette form; neither is
   called by term_init() or `ESC c`, so a `reset` at the prompt cannot wipe the user's theme */
/* Whether an application may repaint the terminal's OWN colours (OSC 4 / 10 / 11 / 104). Process-wide
   like the palette: it is a property of the window, not of a session. */
void term_set_osc_colors(int on);
int  term_osc_colors(void);

/* Default xdg-shell app_id. config_default(), the `app_id =` empty-value fallback in config.c and the
   last-resort fallback in main.c all read this one literal so they cannot drift apart. */
#define DEF_APP_ID  "Kai-Term"

/* Default window title: what a compositor or taskbar shows, built by mux_title(). The tab bar shows the
   same state inside the window. */
#define DEF_TITLE   "Kai-Term"

/* Max continuation codepoints on one emoji anchor cell (base excluded). 8 covers a ZWJ family emoji,
   which needs 6. */
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
   never asked", so a fresh session lands on Kai-Term's own default (a bar) rather than on an
   application's request. csi_dispatch() maps the parameter: 0/1 and 2 -> block, 3/4 -> bar,
   5/6 -> underline; odd/even picks the blink half (Term.cur_blink). */
enum {
    CUR_UNSET    = 0,       /* no DECSCUSR seen: use the configured default */
    CUR_BLOCK    = 1,
    CUR_BAR      = 2,
    CUR_UNDERLINE = 3,
};

/* The blink DECSCUSR asked for. 0 = never asked, so `cursor_blink =` decides. The user's setting is the
   CEILING, not a suggestion: DECSCUSR can only make the cursor less flashy, never restart a blink the
   user turned off. blink_half_ms() in src/main.c is where the rule lives. */
enum {
    CURBLINK_UNSET = 0,
    CURBLINK_BLINK = 1,
    CURBLINK_STEADY = 2,
};

/* wl_pointer.button delivers the kernel's BTN_* codes (linux/input-event-codes.h), which are NOT in the
   order the terminal protocols number the buttons: SGR (1006) and legacy X10 want 0 left, 1 middle,
   2 right. mouse_sgr_button() is the one mapping. */
enum {
    EVDEV_BTN_LEFT   = 0x110,
    EVDEV_BTN_RIGHT  = 0x111,
    EVDEV_BTN_MIDDLE = 0x112,
};
int mouse_sgr_button(uint32_t evdev_code);   /* 0 left, 1 middle, 2 right, 3 any other button */

/* ---- what a pointer press MEANS (implemented in src/vt.c) ----
   main.c asks this ONE question and gates every branch of its chain of early returns on the answer, so
   the order the branches are tried in (which is what a user feels) is testable rather than buried in an
   event handler that no test binary links. */
typedef struct {
    int btn;            /* the SGR button from mouse_sgr_button(): 0 left, 1 middle, 2 right, 3 other */
    int pressed;        /* 0 on release — a release is a different gesture, answered separately */
    int dialog_on;      /* the confirm-quit box is up: it owns the pointer, press AND release */
    int hit_bar;        /* the point is on a tab-bar item */
    int hit_fm;         /* the point is inside the file-manager band */
    int app_mouse;      /* the application asked for mouse reports (MOUSE_PRESS | MOUSE_DRAG) */
    int middle_paste;   /* conf.middle_click_paste — the opt-in this feature is gated on */
} PtrPress;

enum {
    PTR_NONE = 0,    /* nothing owns this press: fall through (a right click, or a release we do not track) */
    PTR_DIALOG,      /* the confirm-quit box swallows it — answering is layout_hit_dialog()'s decision */
    PTR_BAR,         /* the tab bar: a left click shows the session, a middle click closes it */
    PTR_FM,          /* the file-manager band: an entry selection, never a report, never a text selection */
    PTR_REPORT,      /* report the button to the application that asked for the mouse */
    PTR_SELECT,      /* the left button in a pane: selection, the double/triple gesture, click-to-place */
    PTR_PASTE        /* the middle button in a pane, opt-in: paste the clipboard */
};
int ptr_press_action(const PtrPress *p);

/* The wheel is an axis, not a button, in the kernel's numbering, so the code reported to an application
   that asked for the mouse is chosen here: 64 up, 65 down is what xterm, kitty, neovim, tmux and
   ncurses all read as a scroll. One pair serves both encodings because mouse_send() adds the +32 that
   the legacy X10 form wants. */
int mouse_wheel_button(int up);   /* 64 wheel up, 65 wheel down */

typedef struct {
    uint32_t ch;    /* codepoint; 0 = blank */
    uint32_t fg, bg;
    uint32_t attr;
    /* Grapheme-cluster continuation codepoints for an emoji anchor cell (ZWJ sequences, variation selectors,
   skin-tone modifiers). Inline (no heap) so Cell stays safely copyable by value across row realloc,
   DCH/ICH shifts and rotations. nextra == 0 means a single-codepoint cell. */
    uint32_t extra[CLUSTER_MAX];
    uint8_t  nextra;
} Cell;

/* ---------- graphics: the kitty image protocol (src/vt.c) ----------
   An application (timg -p k, kitty icat, chafa) sends an image as one or more APC sequences:
     ESC _ G <key>=<value>;...;<key>=<value> : <base64> ESC \
   m=1 means more chunks follow, f=100 means the payload is a PNG. */

/* One image the application transmitted. The PIXELS live in the model (vt.c) so the grid lifetime rules
   and the unit tests own them; render.c keeps a GL texture per image keyed by `serial` and re-uploads
   when `version` changes. */
typedef struct KImage {
    int      serial;      /* unique within the Term, never reused: the renderer's texture key */
    int      app_id;      /* the i= the application used (0 = none) */
    int      w, h;        /* decoded pixel size */
    uint8_t *rgba;        /* w*h*4, PREMULTIPLIED: the shader composites c + bg*(1-c.a), the same rule the color-emoji atlas
   follows, so no GL blending is needed */
    /* Anchored to an ABSOLUTE grid row (Grid.row_base space), not a viewport row: a viewport row is
   invalidated by every scroll, so the image scrolls with the text it was printed next to and comes back
   from history. */
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

/* A renderer-side GL texture for one model image (render.c owns it). `seen` is stamped with the frame
   number, so an entry no image claimed this frame is deleted after the frame. */
typedef struct {
    int      serial;
    unsigned tex;
    int      w, h, version;
    int      seen;
} ImgTex;

/* Where one image sits in the CURRENT viewport, as computed by term_visible_images(). crow may be
   negative and the span may run past the bottom: render.c clips the quad, so it needs the unclipped
   rect in cell units. */
typedef struct {
    KImage *img;
    int crow, ccol;    /* viewport row/column of the image's top-left cell */
    int crows, ccols;  /* the span to draw */
} ImgView;

/* A chunked image transmission in progress: the control block of the FIRST chunk plus the bytes collected
   so far. Continuation chunks carry only m=, so they inherit everything else. */
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
    int      r_raw;       /* r= as received: the key means height for the raw formats and rows for the self-describing ones, and f=
   may come after it, so the choice waits for the whole control block */
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
    /* The last intermediate byte (0x20-0x2f) seen in this CSI. Dropping it made `CSI Ps SP q` (DECSCUSR)
   indistinguishable from `CSI Ps q`, and DECSCUSR is a sequence whose MEANING is the space. One byte
   because every sequence in use here has at most one. */
    int inter;
    int utf8_need, utf8_cp;
    char osc[512];
    int osc_len;
    /* APC payload (the kitty graphics sequences). Growable: one chunk is a few KB but a different application
   may put a whole image in one sequence. apc_drop is set when the cap is reached, so a garbage stream
   cannot exhaust memory. */
    char  *apc; size_t apc_len, apc_cap;
    int    apc_drop;
    int    apc_kind;       /* the byte after ESC _: 'G' = graphics, anything else is ignored */
    KittyTx tx;
} Parser;

/* ---------- grid & terminal ---------- */
typedef struct Grid {
    int rows, cols;        /* viewport size */
    int rcap;              /* allocated rows (rows + scrollback_max) */
    Cell **row;            /* row[0..scroll_off-1] scrollback, row[scroll_off..scroll_off+rows-1] viewport, the rest free or holding
                              newer lines while the user is scrolled back */
    int scroll_off;        /* index of the viewport top; 0 = oldest retained line,
                              live_off = newest (live) view */
    int live_off;          /* scroll_off value that follows the newest output. scroll_up/scroll_down
                              advance it with scroll_off; user navigation moves scroll_off
                              below it. Always 0 <= live_off <= scrollback_max. */
    int scrollback_max;
    /* wrap[i] != 0 -> the line in row[i] was broken at the RIGHT MARGIN, not by the application, so it
   continues at the start of row[i+1]. Without it a wrapped line and a hard-broken line are the same and
   a width change can only truncate instead of reflow (see grid_resize). Rotates with row[]. */
    uint8_t *wrap;
    /* Absolute (document) row number of slot 0: slot j is document row row_base + j, and document row A
   sits at viewport row A - row_base - scroll_off. row_base moves only when a row is evicted at the top
   or the window slides over its own scrollback (erase_scrollback) — when the oldest rows stop existing. */
    int64_t row_base;
    /* GRID_VALID between grid_init and grid_destroy: term_init() frees the grid it is rebuilding, and a 0/1
   flag cannot tell that from stack garbage, which is routinely non-zero and would free a garbage `row`
   pointer. */
    int valid;
} Grid;

#define GRID_VALID 0x67726964   /* 'g','r','i','d' */

/* Sizes and kinds a struct below needs before the section that introduced them appears. FM_PATH_MAX is
   the cap for every path in the program — the file manager's, the shell integration's, a session's. */
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
    /* DECCKM (DEC private mode 1). While it is on the four cursor keys go out as SS3 (ESC O A..D) instead of
   CSI (ESC [ A..D); less answers only the SS3 form, so ignoring this mode leaves its arrow keys dead.
   Saved and restored across the alternate screen, the way xterm does. */
    int app_cursor;
    /* DECSCUSR (CSI Ps SP q): the shape and blink the APPLICATION asked for, per session — vim, neovim and
   fish change them when they take the screen and expect the next session on the same window to be
   unaffected. 0 = never asked, so a new tab starts from Kai-Term's own default (a bar). */
    int cur_style;         /* CUR_UNSET / CUR_BLOCK / CUR_BAR / CUR_UNDERLINE */
    int cur_blink;         /* CURBLINK_UNSET / CURBLINK_BLINK / CURBLINK_STEADY */
    int alt_screen;        /* 1 when the alternate screen buffer is active */
    Grid alt;              /* saved MAIN grid while alt_screen is active */
    int alt_saved_cx, alt_saved_cy;  /* main-screen cursor saved on ?1049h */
    int alt_saved_app_cursor;        /* DECCKM of the screen being parked, restored on leaving the alt */
    /* Bracketed paste (DEC private mode 2004): while on, a paste is wrapped in ESC[200~ ... ESC[201~, so
   embedded newlines stay in the application's insert buffer instead of acting as Return. 0 = raw. */
    int bracketed_paste;
    /* Text selection (viewport coordinates, inclusive). sel_af/sel_bf are sub-cell: position within the cell
   in thousandths of a cell width, so the highlight follows the pointer instead of snapping to cells. */
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
    /* Current cell size in px. The image protocol sizes in PIXELS while the grid is in CELLS, and only main.c
   knows the font metrics, so it pushes them here (and to the Pty's winsize). 0 = unknown: an image then
   falls back to its own pixel size as a cell count. */
    int      cell_w, cell_h;
    Parser ps;             /* parser state lives with the terminal */
    /* Grapheme-cluster assembly: the anchor cell of the cluster being built, so a ZWJ / variation selector /
   skin-tone modifier joins it instead of landing in its own cell. cl_cx/cl_cy are VIEWPORT coordinates,
   so the state dies whenever rows move or the cursor jumps; cl_join is set after a ZWJ to keep joining. */
    int cl_valid, cl_cx, cl_cy, cl_w, cl_join;
    /* The shell's input line at the last snapshot, so main.c can tell an untouched prompt from a line the
   user has already typed on (term_at_prompt). No snapshot while input_pending is set: output arriving
   then is the tty echoing our own typing, and remembering it would store a half-typed command as the
   prompt. prompt_valid = 0 after anything that moves or re-wraps rows, and unknown counts as NOT at a
   prompt; prompt_cy is compared so a different row that looks identical cannot pass for the prompt. */
    Cell *prompt_row;
    int   prompt_cols, prompt_cx, prompt_cy, prompt_valid, input_pending;
    /* Shell integration (src/shellint.c): a shell that loaded Kai-Term's integration script announces itself
   over Kai-Term's own APC channel, and Kai-Term then asks IT to change directory instead of typing `cd`
   at the prompt. 0 until that announcement arrives for THIS session. Preserved across a reset (ESC c):
   the script announces once, at shell startup. */
    int  shell_int;                 /* SHELLINT_NONE / _FISH / _BASH / _ZSH */
    char shell_cd_ack[FM_PATH_MAX];
    /* Announcements this session has made: a nested shell announces on the way in and the outer one when it
   exits, so main.c needs the count and not just the kind to notice a shell came back. */
    int  shell_int_gen;
    /* Terminal query responses (CPR, DA1, DA2, OSC 10/11 colour queries) queued for the PTY. */
    char resp[256];
    int resp_len;
    /* OSC 10/11: an application setting the default ink/paper. vt.c changes the process-wide defaults and
   sweeps THIS session, then raises the flag; main.c sees it, sweeps every other session and re-points
   the renderer. The old values travel with the flag because the sweep is a match-and-replace and the
   defaults have already moved by then. */
    int      colors_dirty;
    uint32_t old_def_fg, old_def_bg;
    /* BEL: raised by the parser, consumed by main.c. An EVENT, not a state: main.c clears it and records the
   moment on ITS clock, so a bell in a session nobody can see cannot leave a flash armed. A BEL that
   TERMINATES a string (OSC / APC / DCS use 0x07 as ST) never reaches exec_c0() and does not ring. */
    int      bell_rang;
} Term;

/* The last directory the shell said it entered. Clear it before asking for a new one, or the previous
   navigation's ack answers for this one. */
void term_shell_cd_ack_clear(Term *t);
/* grid */
void grid_init(Grid *g, int rows, int cols, int scrollback);
/* Resize the viewport. A column-count change REFLOWS: soft-wrapped rows are joined into their logical line
   and re-broken at the new width, so nothing is lost (narrowing pushes lines into scrollback like normal
   output). slot/col (either may be NULL) name a cell of the OLD grid — slot an absolute index into row[] —
   and come back as that cell's position in the NEW grid; a line that was evicted puts the cursor on the
   newest surviving one. */
void grid_resize(Grid *g, int rows, int cols, int *slot, int *col);
/* Same, but WITHOUT reflow: rows are truncated/grown in place. Used for the alternate screen, where a
   full-screen application repaints itself after a SIGWINCH instead of expecting the terminal to re-wrap its
   output. cur_slot is the absolute row[] slot of the cell that must stay on screen (the cursor), or -1. On a
   height SHRINK the viewport gives up its BOTTOM rows while the cursor has rows below it, and only then its
   TOP rows: if the tracked cell were pushed out, the application would repaint at the wrong place. */
void grid_resize_plain(Grid *g, int rows, int cols, int cur_slot);
void grid_destroy(Grid *g);
Cell *grid_vprow(Grid *g, int y);   /* viewport row y (0..rows-1) */

/* Window pixel size + cell size + a margin -> the grid size AND the pixel offset the grid starts at. One
 * function because THREE places must agree on this geometry: main.c's cols/rows for the grid and the PTY,
 * main.c's pointer -> cell mapping, and render.c's quad offsets; drift puts the highlight on the wrong
 * glyph. Rules: the margin applies on all four sides and is clamped so at least one whole cell fits; the
 * leftover pixels that do not fill a whole cell stay at the RIGHT and BOTTOM, which is what keeps
 * `col = (ptr_x - xoff) / cell_w` exact. */
void term_grid_geometry(int win_w, int win_h, int cell_w, int cell_h, int pad,
                        int *cols, int *rows, int *xoff, int *yoff);

/* terminal */
void term_init(Term *t, int rows, int cols, int scrollback);
/* Re-point everything one session has ALREADY painted at a new default ink/paper (OSC 10/11). Needed
   because a Cell stores a RESOLVED colour, not a palette index: moving the two defaults alone would
   change only future output. Also what keeps blank cells pristine across a colour change. Idempotent. */
void term_recolor_defaults(Term *t, uint32_t old_fg, uint32_t old_bg,
                           uint32_t new_fg, uint32_t new_bg);
void term_destroy(Term *t);
void term_resize(Term *t, int rows, int cols);
void term_parse(Term *t, const uint8_t *buf, size_t len);

/* Is this session sitting at a prompt the user has not typed on? 1 = the cursor's row still holds exactly
   what the shell last printed there and the cursor has not moved since. The file manager writes at the
   prompt, and writing on a half-typed command mangles it and then RUNS the result — and Kai-Term cannot
   know where a prompt ENDS, because the tty echoes typing as ordinary output. 0 for the alternate
   screen, a scrolled-back session, or rows that moved since the last snapshot. */
int term_at_prompt(Term *t);
/* Tell the model that the terminal just sent these bytes to the application (a keystroke, a paste, a line
   the file manager typed). A payload with CR or LF submits the line and clears the flag; so does Ctrl+C.
   Call it for INPUT only: a query answer (DA1, DSR, CPR) or a mouse report is not typing. */
void term_note_input(Term *t, const void *buf, size_t len);

/* The wheel and a click inside a program that owns its own screen and never asked for the mouse (nano,
   less, man) can only be delivered as KEYS: a program with no mouse protocol treats a report as typed
   text — one ESC[<64;20;5M put "64;20;5M" into the user's file and marked the buffer Modified. Both
   helpers below answer "which keys?". */

/* The most arrow-key presses either helper will emit. A click is never further than cols+rows cells away,
   so this bounds pathological input rather than limiting anything a user reaches. */
#define TERM_ARROW_MAX 1024

/* 1 = this session is in the state where the wheel and a click belong to the application as keys: the
   alternate screen, and no mouse protocol asked for (?1006 alone only picks the encoding). */
int term_wants_key_mouse(const Term *t);

/* The bytes this session's application expects for ONE cursor key: SS3 (ESC O A..D) while it holds DECCKM
   (ESC[?1h), CSI (ESC [ A..D) otherwise. `which` is the final byte of either form — A up, B down, C
   right, D left; anything else gives "". Returns a static string, 3 bytes. */
const char *term_arrow_seq(const Term *t, char which);

/* The wheel, as arrow-key presses. lines > 0 scrolls toward the top of the application's text (wheel up),
   < 0 toward the bottom. Returns the bytes written to out, or 0 when this session is not in that state or
   the gesture moves nothing. */
int term_wheel_arrow_keys(const Term *t, int lines, char *out, size_t cap);

/* A left click at viewport cell (col,row), as the arrow-key presses that walk the application's OWN cursor
   there, from the cursor position the model already tracks. Vertical first, then horizontal: Up/Down keep
   the target column, which is clamped to that row's last non-blank cell + 1 because a program's Right at
   end-of-line moves to the NEXT line. Returns the bytes written, or 0 when this session is not in that
   state, the cell is outside the grid, or the cursor is already there. */
int term_click_arrow_keys(const Term *t, int col, int row, char *out, size_t cap);

/* The longest input line term_prompt_line() will hand back. Hitting the cap means something absurd, and it
   REFUSES rather than truncating a command. */
#define FM_LINE_MAX 8192

/* Can the input line be described EXACTLY as "the remembered prompt, then typed text ending at the cursor"?
   1 = yes, and `out` receives that typed text ("" when the line is empty); 0 = no, `out` untouched — a
   text too long for cap is refused rather than truncated. The prompt prefix and everything right of the
   cursor must still match the snapshot, so a cursor parked mid-line is a refusal: erasing from there
   would leave the right half behind and type the `cd` in front of it. A wide glyph counts as ONE
   codepoint, so re-typing the result reproduces the line. */
int term_prompt_line(Term *t, char *out, size_t cap);

/* The same description, but the cells right of the cursor need not match the snapshot: it returns the text
   between the end of the remembered prompt and the cursor, ignoring what the shell paints from the cursor
   onwards (fish's grey autosuggestion ghost). Use it only when the caller will ERASE the whole line and
   PROVE the erase worked before writing: the erase is Ctrl+U, which erases backwards, so with the cursor
   mid-command the right half survives, the proof fails and the caller drops its sequence. */
int term_prompt_line_typed(Term *t, char *out, size_t cap);

/* Does the input line read as the remembered prompt followed by EXACTLY `text`, ending at the cursor? Like
   term_prompt_line() but without requiring the cells right of the cursor to match: fish paints a grey
   autosuggestion there, and those cells are not part of the command line. The text compared sits between
   the end of the prompt and the cursor, where typed text goes and never where a ghost is drawn. */
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
/* Should the selection band be PAINTED over cell (col,row)? Being covered is not enough: this says whether
   the cell holds anything worth highlighting, so the renderer skips empty space that copies as a space. */
int  term_cell_highlightable(Term *t, int col, int row);

/* How long two presses of the same cell may be apart and still be ONE gesture (double click, triple click).
   One constant for the terminal and for the file manager's browser. */
#define CLICK_DCLICK_MS 500

/* The word at (col,row) — the maximal run of non-space cells around it with TRAILING punctuation cut off
   (leading punctuation stays); a click on a blank yields the clicked cell alone. A wide glyph is never
   split: the run covers the anchor and its continuation. */
int term_word_span(Term *t, int col, int row, int *out_c0, int *out_c1);
/* The same rule over one row of cells handed in directly, for a grid that is not a session's: the file
   manager's preview pane is a flat Cell array with the same ATTR_WIDE convention. */
int cell_row_word_span(const Cell *rc, int cols, int col, int *out_c0, int *out_c1);
/* The logical line of `row`: the physical rows a soft-wrapped line spans, joined by the grid's wrap marks,
   ending at a hard break (the application's own newline). *out_r0..*out_r1 inclusive (viewport rows). */
int term_line_span(Term *t, int row, int *out_r0, int *out_r1);
/* Which click in a row this press is: 1 when it starts a new gesture, prev_count + 1 when the same
   cell was pressed again within CLICK_DCLICK_MS. */
int term_click_count(uint32_t time_ms, uint32_t prev_time_ms, int same_cell, int prev_count);
int  term_selection_text(Term *t, char *out, size_t cap); /* copies text, returns length */
void term_select_clear(Term *t);

/* ---- the visual bell ----
   A BEL is an event, not a pixel: vt.c raises a per-session flag, main.c turns it into a timestamp on its
   own CLOCK_MONOTONIC clock (vt.c reads no clock, so the model stays testable), and a frame asks one pure
   question about it. */
#define BELL_FLASH_MS 250   /* long enough to notice, short enough that repeated BELs are feedback, not a strobe */
#define BELL_MIX_MAX  25    /* % toward the ink: the flash sits under the glyphs, which must stay readable */

/* 0..1000: how strong the flash is right now for a session that rang at `rang_ms`. 0 means "draw nothing":
   no ring yet, at or after the deadline, a clock that went backwards, or a duration of 0. Falls linearly
   from 1000 at the ring so the field fades out instead of snapping off. Both clocks are ms since boot;
   long long because an int ms counter wraps after 24.8 days and a terminal open over a weekend is not an
   edge case. */
int bell_strength(long long now_ms, long long rang_ms, int dur_ms);

/* BELL_OURS_MS: how long after Kai-Term turns a CLICK into cursor-key presses a BEL still counts as that
   program's answer to the presses rather than a message to the user. The BEL arrives 0-1 ms after the
   write, so 250 ms is wide headroom yet far below what a person does after a click. Unrelated to
   BELL_FLASH_MS: one is a flash's length, the other a mute's. */
#define BELL_OURS_MS 250

/* 1 when a BEL arriving at `now_ms` is the program's answer to keystrokes Kai-Term synthesized for a click
   at `synth_ms`, so the pane must NOT flash: Kai-Term typed those keys, so the program's complaint about
   them is not addressed to the user. 0 when nothing was ever synthesized (`synth_ms` 0), the clock went
   backwards, the window is 0, or the press burst is old enough that the BEL belongs to something else.
   The parser raises Term.bell_rang either way — muting the FLASH is a decision about the picture. */
int bell_is_ours(long long now_ms, long long synth_ms, int window_ms);

/* ---------- a plain click opens a URL (src/url.c) ----------
   URL_MAX ceilings what is handed to an opener: the scan happily finds a 3000-character run, and a "URL"
   that long is a program printing a line that starts with a scheme. It also bounds the pending-open buffer. */
#define URL_MAX 2048

/* The program a URL is handed to: the desktop's own opener, so Kai-Term needs no browser and no .desktop
   parsing. A constant and not a config key on purpose — a key holding a program name is a way to make the
   terminal exec anything. execvp, never a shell: no character of the URL is ever interpreted. */
#define URL_OPENER_PROGRAM "xdg-open"

/* A URL run in the grid, inclusive, in viewport rows and columns. A run can cross rows because a soft-wrapped
   line is ONE line the application never saw broken — the same fact a triple click rests on. */
typedef struct {
    int r0, c0, r1, c1;
} UrlSpan;

/* The URL run under pane cell (col,row), or 0 when there is none. The run is ASCII-only: a space, a
   never-written cell, a quote or an angle bracket end it, and so does a non-ASCII or wide cell — which
   CUTS a URL in half, so a run with such a cell on its right is REFUSED outright. It must contain
   `http://` or `https://` (case-insensitive) and something after it. Trailing marks that stick to the run
   rather than belonging to it are cut (. , : ; ! ? and a closer with no matching opener); a trailing
   SLASH is kept, since `https://x/` and `https://x` are different pages. Rows are joined with
   term_line_span(), so a URL the window broke is one run and one the program broke with a newline is not. */
int term_url_span(Term *t, int col, int row, UrlSpan *sp);

/* The bytes of a span, NUL-terminated into out (cap bytes). Returns the length, 0 when it does not fit (nothing
   is written in that case — half a URL must never be opened), and -1 when the run is longer than URL_MAX. */
int term_url_text(Term *t, const UrlSpan *sp, char *out, size_t cap);

/* Which cells of a span sit on viewport row `r`: the first row starts at c0, the last ends at c1, and rows a
   soft-wrapped run crosses are whole (the break happened AT the margin). 0 = this row holds none of it.
   Asked by term_url_text() and by the hover underline, so the underline never covers cells the opener
   would not read. `cols` is the grid the span was found in (Term.g.cols), not a pane's drawn width. */
int url_span_row(const UrlSpan *sp, int r, int cols, int *from, int *to);

/* Is this string something to hand the opener? 1 = yes. It must carry a scheme Kai-Term opens for, hold no
   control character, no space and no DEL, be no longer than URL_MAX, and not start with '-' — an argument
   starting with a dash is read as an OPTION by the opener. */
int url_is_openable(const char *url);

/* The gates on a plain click, as ONE decision. main.c owns the geometry and the clock and fills these; url.c
   owns the order, so the precedence is a table a check can cover rather than an event handler. */
typedef struct {
    int enabled;          /* conf.click_opens_url */
    int released;         /* a URL opens on RELEASE: a press cannot know whether a second click follows */
    int single;           /* this was the only click of the gesture (2 and 3 select a word / a line) */
    int no_drag;          /* the press and the release were in the same cell */
    int shift;            /* Shift means "selection", here as everywhere else */
    int app_owns_click;   /* term_wants_key_mouse(): the program that owns its screen keeps its click */
    int url_found;        /* term_url_span() + term_url_text() answered a URL */
} UrlClick;

/* WHY a plain click did not open. A value rather than an early return so "I clicked and nothing happened"
   has an answer; the order is the order of the gates and the first refusal wins. NONE = all passed. */
enum {
    URL_REFUSE_NONE = 0,
    URL_REFUSE_OFF,          /* click_opens_url = 0 */
    URL_REFUSE_NOT_RELEASE,  /* a press cannot know whether a second click follows */
    URL_REFUSE_GESTURE,      /* the 2nd or 3rd click: it selects a word or a line instead */
    URL_REFUSE_MOVED,        /* the pointer changed cell between press and release: that is a drag, and a drag selects */
    URL_REFUSE_SHIFT,        /* Shift + click means selection */
    URL_REFUSE_APP,          /* the program owns its own screen: less/nano/man keep click-to-place-cursor */
    URL_REFUSE_NO_ADDRESS    /* nothing under the pointer is an http(s) address */
};
int url_refusal(const UrlClick *c);

/* The same answer in words, for the log. Kept here rather than written at the call site so the wording a check
   pins and the wording the user reads are one thing. */
const char *url_refusal_name(int why);

/* 1 = this click opens. Exactly `url_refusal(c) == URL_REFUSE_NONE`, and pinned to be so. */
int url_click_opens(const UrlClick *c);

/* The same gates seen from the pointer instead of from a click: 1 = draw the hover underline on the run
   under the pointer. A SEPARATE struct because a hover has gates a click does not (the pointer may be over
   the bar, the band, a margin or the box; a drag may be live) and lacks others (no press, no release, no
   click count) — but the ANSWER comes from url_refusal(), the same function the click asks, with the
   click-only gates filled in as a hover implies: an underline is a PROMISE a plain click there opens. */
typedef struct {
    int enabled;          /* conf.click_opens_url — the same switch the click obeys */
    int in_pane;          /* the pointer is over a terminal pane, not the bar, the band, a margin or the box */
    int dragging;         /* a selection drag is live: the pointer is selecting text, not aiming at a link */
    int shift;            /* Shift is held, so the click this hover promises would select instead */
    int app_owns_click;   /* term_wants_key_mouse(): less/nano/man keep that click */
    int url_found;        /* term_url_span() + term_url_text() answered an address that would be opened */
} UrlHover;
int url_hover_shows(const UrlHover *h);

/* The click waits out the double-click window before it opens, so double-clicking a URL selects the word
   and opens NOTHING. 1 when the wait is over; STRICTLY greater than `window_ms`, because
   term_click_count() counts a press at exactly the window as still the same gesture and the two must
   agree. 0 for nothing pending (press_ms 0), a backwards clock, or a wait not yet over. */
int url_open_due(long long now_ms, long long press_ms, int window_ms);

/* What main.c passes as that window: the double-click window itself, so the two rules cannot disagree about when
   a gesture stops being a single click. */
#define URL_OPEN_WAIT_MS CLICK_DCLICK_MS

/* Start `program` with ONE argument, detached: setsid, stdio on /dev/null, Kai-Term's own descriptors
   closed (closefrom) and blocked signals unblocked before the exec — a signal mask survives exec, so
   xdg-open would otherwise run with SIGCHLD blocked, and without closefrom a browser inherits every
   session's pty master and keeps those ptys alive after their shells are gone. The child is not waited
   for: main.c's signalfd reaps it. Returns 0 when the child was started, -1 when fork failed. */
int url_spawn(const char *program, const char *arg);

/* CSI 3 J ("erase saved lines"): 1 (default) lets it drop the scrollback buffer, 0 keeps the history.
   Process-wide rather than Term state because `ESC c` rebuilds the Term and must not re-enable it. */
void term_set_clear_scrollback(int on);

/* ---------- paste (src/vt.c) ---------- */
/* Bracketed paste delimiters, sent around clipboard text when the application asked for mode 2004. */
#define PASTE_START "\x1b[200~"
#define PASTE_END   "\x1b[201~"
/* The exact bytes to hand the PTY for a paste of `text` (len bytes, not NUL-terminated): the raw text when
   bracketed==0, else PASTE_START + text + PASTE_END. Returns the length written, or -1 when out (cap
   bytes) is too small — the caller then sends the paste unwrapped rather than truncating it. */
int paste_wrap(int bracketed, const char *text, size_t len, char *out, size_t cap);

/* Which text mime type to ask a clipboard source for: 0 = not text, higher = preferred. A source may
   advertise several; picking "the last one advertised" is an accident of its enumeration order. */
int clipboard_mime_rank(const char *mime);

/* Repair a clipboard block before it reaches the PTY: a NUL byte must never get there, and a source serving
   UTF-16 (Firefox on Wayland labels UTF-16 code units as UTF-8) is decoded rather than pasted as
   NUL-littered keystrokes. Copies `in` (len bytes) into out (cap bytes), returns the new length or -1 if
   out is too small; cap >= len*3/2 + 8 always suffices. A block with no NUL is copied byte-for-byte. */
int paste_repair(const char *in, size_t len, char *out, size_t cap);

/* ---------- configurable key bindings ----------
   Every name below is a line in kai-term.bind, not kai-term.conf, with the same `key = value` syntax. A
   binding left in an older kai-term.conf is still honoured — the split is about where a line BELONGS. */

/* Modifier bits a binding may name. Deliberately NOT xkbcommon's masks: the config parser must not depend
   on xkbcommon (it is linked into the unit tests), and main.c translates the xkb mask into exactly these
   four bits. Locks (Caps/Num) are not modifiers here. */
enum {
    KB_CTRL  = 1u << 0,
    KB_SHIFT = 1u << 1,
    KB_ALT   = 1u << 2,
    KB_SUPER = 1u << 3,
};

/* Keysyms for the non-printable keys a binding may name: standard X keysym numbers, which is what
   xkb_state_key_get_one_sym() returns. main.c _Static_asserts each against the xkbcommon constant. */
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

/* confirm_quit config value. CONFIRM_RUNNING is the default: a close that would kill a running foreground
   command asks first; an idle session closes without a question. */
enum { CONFIRM_NEVER = 0, CONFIRM_RUNNING = 1, CONFIRM_ALWAYS = 2 };

/* bell config value: what a BEL does. BELL_VISUAL (the default) washes that session's field toward the ink
   for BELL_FLASH_MS; BELL_OFF draws nothing. There is no audio half — Kai-Term has no sound output, so
   "off" means "no bell is visible", not "the bell is silenced". BELL_OFF is 0 because the value is only
   ever compared against a named action, never tested for truthiness. */
enum { BELL_OFF = 0, BELL_VISUAL = 1 };

typedef struct {
    uint32_t sym;    /* the key: a printable codepoint or one of KB_KEY_* */
    unsigned mods;   /* KB_* bits that must ALL be held (at least one: see keybind_parse) */
    int set;         /* 0 = the action is unbound (`key_new_tab = none`), 1 = bound */
} KeyBind;

/* Parse "Ctrl+Shift+T" (case-insensitive, modifiers in any order) into a KeyBind. Returns 1 when the spec
   is usable, 0 when it is not (out is left unbound and the caller keeps the default). A spec with NO
   modifier is refused on purpose: an unmodified binding would swallow plain typing. */
int keybind_parse(const char *spec, KeyBind *out);
int keymods_parse(const char *spec, unsigned *mods);
/* The same parser, but an UNMODIFIED key is allowed — only for the file manager's own keys, which are looked
   at only while the file manager owns the keyboard. */
int keybind_parse_plain(const char *spec, KeyBind *out);

/* The keys the file manager uses while it has the keyboard. Grouped in one struct because they are
   fifteen actions of one feature, and main.c matches them as a set. */
typedef struct {
    KeyBind up, down, pageup, pagedown, home, end;   /* move the selection */
    KeyBind left;      /* go to the parent directory (the same action BackSpace has always had) */
    KeyBind right;     /* descend into the selected directory — and NOTHING on a file */
    KeyBind open;      /* Enter: descend into the selected directory, and NOTHING on a file — no key here
                          writes at the prompt unless that is the key's whole job */
    KeyBind path;      /* type the selected entry's quoted path at the prompt, without running it */
    KeyBind parent;    /* Backspace: go to the parent directory */
    KeyBind refresh;     /* re-read the current directory */
    KeyBind preview;     /* show / hide the preview pane */
    KeyBind hidden;      /* show / hide dotfiles */
    KeyBind quit;        /* close the file manager */
} FmKeys;

/* ---------- config ---------- */
/* The cap on "names the settings file set explicitly" (see Config.set_keys). The number of accepted names is
   pinned by tests/test_vt.c, so this cap cannot be quietly outgrown. */
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
    /* The mouse inside a full-screen program that never asked for the mouse (nano, less, man): it owns its
       own screen, so the only thing that can move it is keystrokes — see term_wheel_arrow_keys() and
       term_click_arrow_keys(). A program that DID ask for the mouse (vim, htop, tmux) keeps getting reports. */
    int  wheel_sends_keys;    /* 1 (default): the wheel sends Up/Down presses there */
    int  click_moves_cursor;  /* 1 (default): a left click walks the program's own cursor to the clicked cell.
                                 Keystroke synthesis, not mouse support: a program that uses the arrow keys for
                                 something other than moving a cursor reacts to a click (alsamixer moves the
                                 slider it is on). A program that cannot honour a press answers with a BEL, and
                                 Kai-Term does not flash for a bell it caused itself — see bell_is_ours(). */
    /* 1 (default): a second left click in the same cell within CLICK_DCLICK_MS selects the WORD under the
   pointer and a third selects the whole LOGICAL line (a soft-wrapped line counts as one line). 0 = one
   cell per click, extended by dragging. What counts as a word lives in term_word_span(); a wide glyph is
   never cut in half. A word/line gesture also suppresses click_moves_cursor's synthesized presses. */
    int  click_selects_word;
    /* middle_click_paste: 0 (default) = a middle click closes a session on the tab bar, selects in the
   file-manager band, is reported to a program that asked for the mouse, and does NOTHING in a plain
   terminal pane. 1 = that last case pastes the CLIPBOARD (not the X11 primary). The precedence — bar >
   band > application > pane — is ptr_press_action(), one checkable decision in src/vt.c. */
    int  middle_click_paste;
    /* click_opens_url: 1 (default) = a single left click on an `http://`/`https://` run in a terminal pane
   opens it with the desktop's opener. A key and not a law, because it launches a program. It never takes
   a click that already belongs to something else: a drag still selects, a second click selects the WORD
   and opens nothing, Shift+click means selection, the bar and the band keep their clicks, and a program
   that asked for the mouse or owns its own screen keeps its click. url_click_opens() holds the rules. */
    int  click_opens_url;
    /* bell: what a BEL does — BELL_VISUAL (the default, a short flash of that session's field) or BELL_OFF. On
   by default because a BEL is how readline, `less`, `man`, vim and sudo say "no". It flashes the PANE,
   not the window margin: the margin is the same colour as the cells, so a margin-only flash would read as
   a smaller rectangle inside the terminal. */
    int  bell;
    /* The cursor's shape is always the thin bar unless an application asks for another with DECSCUSR
       (there is deliberately no `cursor_style` key). cursor_blink = the half-period in ms the cursor is
       on, then off (0 = never blink, and an application cannot switch the blink back on); blinking also
       stops while the window has no focus. cursor_width = how many pixels wide the bar is (and how tall
       an underline can be); the renderer clamps it to a third of a cell and never below 1 px. */
    int  cursor_blink;
    int  cursor_width;
    /* Kai-Term drives its own key repeat. repeat_delay = ms a key must be held before it starts repeating
   (0 = leave repeat to the compositor); repeat_rate = repeats per second once it starts. */
    int  repeat_delay;
    int  repeat_rate;
    /* Margin in px between the window edge and the first/last cell row/column, on all four sides. Keeps the
   corner glyphs out of a compositor's rounded window corners. The pixels left over after whole cells
   belong to the bottom/right, so the bottom margin is padding + remainder. */
    int  padding;
    /* Multiplexing: one window hosts up to max_tabs terminal sessions, each with its own shell, grid,
   scrollback and selection; only one is visible. tab_prefix_mods + a digit 1-9 jumps to that tab; the
   bindings are config strings such as "Ctrl+Shift+T" (see keybind_parse). */
    int  max_tabs;
    KeyBind key_new_tab;
    KeyBind key_close_tab;
    KeyBind key_next_tab;
    KeyBind key_prev_tab;
    unsigned tab_prefix_mods;   /* 0 = the digit shortcut is disabled */
    /* How the sessions of this window are SHOWN (see the layout section): tab_bar is BAR_OFF/BAR_AUTO/
   BAR_ALWAYS (AUTO = shown from the second session on), split_max caps how many sessions are drawn side
   by side, and key_toggle_layout swaps the two views. */
    int  tab_bar;
    int  split_max;
    KeyBind key_toggle_layout;
    /* confirm_quit: what a close does when something is still running — CONFIRM_NEVER / CONFIRM_RUNNING (the
   default) / CONFIRM_ALWAYS. One gate covers closing the WINDOW and closing a TAB, because a tab is where
   work is lost more often. The predicate is pty_fg_busy(). */
    int  confirm_quit;
    /* ---- the in-window file manager (src/fm.c; its band is laid out by src/layout.c) ----
   fm_on: start with the file manager shown (default off). fm_size: the PERCENT of the grid area's height
   the band asks for — a share and not a row count, so it keeps its proportion across a resize (layout.c
   clamps it against the window). fm_preview / fm_show_hidden: the preview pane and dotfiles from the
   start. fm_follow: navigating also sends `cd <path>` to the shell, so the prompt lands where the browser
   is looking. fm_focus_on_hover: 1 lets a scroll over a band pane move the KEYBOARD there; 0 (default)
   means a pane is focused by CLICKING it. */
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
   On (default): OSC 4 sets a palette entry, OSC 10/11 set or query the default ink/paper, OSC 104 puts
   entries back — what lets pywal-style tools theme a live Kai-Term with no file. Off: all four are
   ignored and logged at debug level. What on means: any program in the terminal can change the colours of
   the whole window, and an OSC 10/11 overrides the `foreground =`/`background =` lines for the run. */
    int  osc_colors;
    /* ---- themes (src/theme.c) ----
   `theme = NAME`: a value with no '/' is looked up as NAME.theme in the `themes/` subdirectory of the
   three config directories (first hit wins); a value with a '/' or starting with '~' is a path. Empty =
   no theme. The theme is applied AFTER the settings file and only for the colour keys the settings file
   did not mention, so a `background =` line wins wherever it sits in the file. */
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
/* The key-binding file — every `key_*` / `fm_key_*` name lives there. main.c reads it AFTER config_load(),
   so for a name both files set the bind file wins; a binding still written in kai-term.conf is honoured
   (with a log line saying where it belongs now). */
void config_load_binds(Config *c, const char *path);
/* The first of $XDG_CONFIG_HOME/kai-term, ~/.config/kai-term, /etc/kai-term that holds a REGULAR file
   called `name`; empty `out` when none of them does, which means "use the built-in defaults". */
void config_find(const char *name, char *out, size_t cap);
/* Expand a leading "~" (or "~/...") to $HOME and copy the result into out. */
void expand_tilde(const char *in, char *out, size_t cap);
/* Cut a '#' comment off a config line in place. ONE rule for every file Kai-Term reads — kai-term.conf,
   kai-term.bind, a .theme — because a wrong rule eats every `#RRGGBB` value. */
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
   shared function) and also in kitty's `key value` spelling, so an existing kitty theme loads unchanged. */
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

/* ---- importing a theme written for ANOTHER program (`--import-theme`) ----
   The importer lives here rather than in main() so the part that can get a colour WRONG — the sniff, the
   name mapping, the colour folding — is headless and testable: file in, file out, no Wayland, no GL. */
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
   ask for is 0x00000000 — so an unset colour falls back to Kai-Term's default rather than to black. */
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

/* The whole chain: read `src_path`, sniff it, map it, and write <dest dir>/<name>.theme. Returns:
     0  written (out_path holds it)
     1  it already exists and overwrite was not granted — the caller asks the user, then calls again
    -1  refused or unreadable (the log says why; nothing was written) */
int theme_import(const char *src_path, char *out_path, size_t cap, int overwrite);

/* ---------- pty ---------- */
typedef struct Pty {
    int mfd;
    pid_t child;
    int rows, cols;
    /* Cell size in px, reported to the child in winsize.ws_xpixel/ws_ypixel. Applications that draw images
   (timg, chafa, kitty icat) need it to turn a pixel-sized image into a cell span; timg selects the kitty
   graphics protocol only when the PTY reports non-zero pixel geometry. 0 = not reported. */
    int cell_w, cell_h;
} Pty;

int  pty_spawn(Pty *p, const char *cmd, int rows, int cols, const char *dir);
void pty_setsize(Pty *p, int rows, int cols);
/* Record the cell size and re-issue TIOCSWINSZ. Zooming changes the cell without changing cols x
   rows, and an application that already asked for the geometry must be told the new numbers. */
void pty_set_cells(Pty *p, int cell_w, int cell_h);
void pty_write(Pty *p, const void *buf, size_t len);
/* Is something still RUNNING in this pty? The pure half is fg_busy(), checkable without a process:
   tcgetpgrp() on the pty MASTER gives its foreground process group and Kai-Term never calls tcsetpgrp,
   so the shell owns that value — idle at the prompt it IS the shell's pid, a foreground command moves it
   elsewhere. fg_busy() exists for the edge cases: a DEAD session (tcgetpgrp answers 0, which a naive
   `!= child` would call "running") and a session with no pty. A job backgrounded with `&` reads idle. */
int fg_busy(pid_t fg, pid_t child);
int pty_fg_busy(const Pty *p);
/* What is running here, by name — the box says "nano", not "a command". proc_comm_name() parses what
   /proc/<pid>/comm holds (trim the newline, refuse an empty or control-character name, never cut a UTF-8
   sequence) so the decision is checkable without a process; pty_fg_name() is the syscall half. */
int proc_comm_name(const char *raw, char *out, size_t cap);
int pty_fg_name(const Pty *p, char *out, size_t cap);

/* ---------- multiplexing: several terminal sessions in one window (src/mux.c) ---------- */

#define TAB_MAX 64          /* hard ceiling; Config.max_tabs is clamped to 1..TAB_MAX */

/* One multiplexed session. `term` is its whole screen state, `pty` its shell. The FILE DESCRIPTORS and the
   child process belong to src/main.c: mux never opens, closes, spawns or reaps anything — it owns the Tab
   array, the active index, the per-tab attention flag and the Term lifetime. */
typedef struct Tab {
    Term term;
    Pty  pty;
    /* Output arrived while this tab was NOT the visible one. Cleared when the tab becomes visible. Both the tab
   bar (a leading * in its label) and mux_title() report it. main.c only sets it for a session that is NOT
   drawn at all — a pane the user can see needs no mark. */
    int  attention;
    /* Shell integration (src/shellint.c), per session because one window has N shells, each started with its
   own request file: `cd_req` is the file THIS session's shell reads the wanted directory from (empty =
   started without integration); `int_dead` records that the announced integration stopped answering.
   Both travel with the Tab when mux_close() shifts the array, so they cannot live in an array indexed by
   tab number. */
    char cd_req[FM_PATH_MAX];
    int  int_dead;
    /* The last `Term.shell_int_gen` this session saw. main.c compares the two: a new announcement means a
   different shell is now reading the line (the user started one, or a nested one exited), which revives
   `int_dead`. Travels with the Tab like the two above. */
    int  int_gen;
    /* The CLOCK_MONOTONIC ms of the last BEL this session rang, 0 = never. Window state on main.c's clock (the
   model reads no clock), and it must travel with the Tab when mux_close() shifts the array.
   render_draw() asks bell_strength() with it per pane, so only the pane that rang flashes. */
    long long bell_ms;
    /* The CLOCK_MONOTONIC ms of the last cursor-key burst Kai-Term synthesized for a CLICK in this session
   (0 = never). Stamped by main.c where the presses are sent, read by bell_is_ours() where a BEL becomes a
   flash. The WHEEL path is deliberately not stamped — see click_moves_cursor above. */
    long long synth_ms;
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
/* How many of these sessions have something running that a close would kill, filling idx[] (tab indices,
   at most `max`) with up to TAB_MAX of them. Returns 0 when every session is idle — the case where
   `confirm_quit = running` lets the close through with no question. */
int mux_running(const Mux *m, int *idx, int max);
/* The one-row question a TAB close shows; `what` is CONFIRM_CLOSE_TAB for that. The CONFIRM_QUIT branches
   are not what a user sees — a WINDOW close asks with confirm_warning() and a box — but stay because this
   is a public entry point whose safety contract (cap 0, NULL buffer, small cap) is checked through them.
   n/idx come from mux_running(): "sessions 2 and 4 are still working" is information a yes/no answer
   cannot carry. Returns the bytes written, always NUL-terminated. */
/* What a close is asking about. CONFIRM_NONE is 0 and main.c's `confirm_kind` doubles as "is a question
   up?", so a question kind must never be 0: with CONFIRM_QUIT = 0 a WINDOW close would set the same value
   that means "nothing is being asked" — no box drawn, no modal keyboard, close already swallowed. */
enum { CONFIRM_NONE = 0, CONFIRM_QUIT = 1, CONFIRM_CLOSE_TAB = 2 };
int confirm_message(int what, int n, const int *idx, char *out, size_t cap);
/* The sentence for the WINDOW box: `Warning ! "nano" is still running. Are you sure you want to close it ?`
   names[] is the parallel list of process names from pty_fg_name(); a NULL or empty entry falls back to
   "session N" rather than inventing one. Several sessions pluralise and list at most three names. */
int confirm_warning(int n, const int *idx, const char *const *names, char *out, size_t cap);

/* ---------- layout: where each session is drawn (src/layout.c) ----------
   Pure geometry, no Wayland/EGL/fd, so the tab-bar/split feature is unit-testable (tests/test_vt.c links
   it). One place decides how the window's pixels are shared — the bar strip, one rect per tab item, one
   rect per pane with the cols/rows that fit inside it — because the drawn cell, the click -> cell mapping
   and the PTY size must agree. main.c resizes each displayed session to its pane; render.c draws exactly
   what this says. */

enum { LAYOUT_TABS = 0, LAYOUT_SPLIT = 1 };

/* tab_bar config value. BAR_AUTO is the default: no bar while there is one session, the bar appears with
   the second. BAR_ALWAYS shows it even with one, BAR_OFF never (then the window title and the keyboard
   bindings are the only way to see/switch sessions). */
enum { BAR_OFF = 0, BAR_AUTO = 1, BAR_ALWAYS = 2 };

#define BAR_LABEL_MAX 40    /* bytes of label text per tab item (UTF-8, cut at a codepoint edge) */
#define NOTICE_MAX   128    /* bytes of confirm-quit question (UTF-8, cut at a codepoint edge). Bigger
                               than confirm_message()'s longest row, so the row shown is cut by the
                               window width and never by the buffer it travels in. */

/* ---- the confirm-quit DIALOG: what a WINDOW close asks with ----
   A tab close asks with the one-row notice above (it costs a grid row and stays out of the way). A window
   close asks with a box in the middle of the window and two clickable buttons, because that close cannot
   be undone and a stray Mod+Q hits it. The box is drawn OVER the terminal, so it costs no row. */
#define PROC_NAME_MAX   64   /* bytes of a process name read out of /proc/<pid>/comm */
#define DIALOG_MAX      256  /* bytes of the dialog's sentence */
#define DIALOG_LINES    4    /* most lines the sentence is wrapped into */
#define DIALOG_LINE_MAX 128  /* bytes in one wrapped line (>= the widest sentence that fits a window) */
#define DIALOG_BTN_MAX  8    /* bytes of a button label */

typedef struct {
    int x, y, w, h;                /* window px */
    int label_x;                   /* where the label starts inside the field — geometry's decision, so the
                                      label cannot be centred in one file and clipped in another */
    int yes;                       /* the ANSWER this button gives: 1 confirms the close, 0 cancels. Storing the
                                      answer, not a name, keeps a hit test working when labels change. */
    char label[DIALOG_BTN_MAX];
} DialogBtn;

typedef struct {
    int on;                        /* 0 when the box cannot fit the window at all */
    int x, y, w, h;                /* the box, window px */
    int text_x, text_y;            /* the top-left of the FIRST text line, window px. The padding is a geometry
                                      decision, so the renderer does not recompute it and drift. */
    int nline;
    char line[DIALOG_LINES][DIALOG_LINE_MAX];   /* the sentence, wrapped here so the renderer draws exactly what geometry said */
    int nbtn;
    DialogBtn btn[2];              /* [0] = Yes, [1] = No */
} Dialog;

/* One session's box. x/y/w/h are window pixels (top-left origin, like pointer events); cols/rows is what
   term_grid_geometry() says fits inside, so leftover pixels sit at the pane's right/bottom and
   `col = (ptr_x - pane.x) / cell_w` stays exact. */
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

/* The file-manager band: the strip of the grid area the browser and the preview take, ABOVE the terminal
   pane. Decided here and nowhere else — the painted cells, the pointer hit test and fm_set_size() must
   agree on how many columns and rows each pane has. `on` is 0 when the window is too short to hold the
   band AND a usable terminal pane: the terminal wins, as it wins over the tab bar. */
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
   count, so it holds its share through a resize or a zoom. FM_SIZE_MIN/MAX bound the request, FM_ROWS_MIN
   is the smallest band worth drawing and FM_TERM_MIN_ROWS the floor the terminal never goes below. Clamped
   here rather than in config.c, so no caller can ask for a 1-row band and get one. */
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
    /* The confirm-quit question: one row of chrome at the BOTTOM of the grid area, costing the panes one row.
       `on` is 0 when the window is too short to give up that row — the terminal wins, as it does over the
       bar and the band. `notice` holds the text ALREADY cut to the row's columns, so the clipping is a
       layout decision a test can check rather than something the draw loop does unchecked. */
    int notice_on;
    int notice_x, notice_y, notice_w, notice_h;
    char notice[NOTICE_MAX];
    Dialog dialog;     /* the window-close question: a centred box with two clickable buttons, drawn over the
                          terminal so it costs no grid row (see the notice rule above for the tab row) */
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
    int notice_on;     /* the confirm-quit question is up (it costs one grid row — see Layout) */
    const char *notice_text;   /* the row's text; NULL or empty with notice_on draws an empty row */
    int dialog_on;     /* the WINDOW-close question: a centred box, drawn over the terminal, costing no row */
    const char *dialog_text;   /* its sentence, from confirm_warning() */
    TabInfo tab[TAB_MAX];
} LayoutReq;

void layout_compute(const LayoutReq *rq, Layout *out);
/* Hit tests in window px. Both return an INDEX INTO THE LAYOUT ARRAY (not a tab index), -1 when the
   point is in the margin, the gap or outside. */
int layout_hit_bar(const Layout *l, int px, int py);
int layout_hit_pane(const Layout *l, int px, int py);
/* Which button of the confirm-quit DIALOG owns a pointer position: an index into Layout.dialog.btn[], or
   -1 anywhere else (inside the box but off both buttons included). Checked BEFORE every other hit test:
   while the box is up it owns the pointer, or a click on a button would also select a tab under it. */
int layout_hit_dialog(const Layout *l, int px, int py);
/* FM_HIT_LIST / FM_HIT_PREVIEW when the point is over one of the file-manager panes, FM_HIT_NONE
   otherwise. Checked BEFORE layout_hit_pane(): with the band on, a click inside it must never reach the
   shell. */
int layout_hit_fm(const Layout *l, int px, int py);
/* -1 when that session is not drawn in this layout (a hidden tab in tab mode, or a session outside the
   visible pane window in split mode). */
int layout_pane_of_tab(const Layout *l, int tab);
/* Is a visual-bell flash on screen RIGHT NOW? 1 when any session this layout DRAWS rang within `dur_ms` of
   `now_ms`; 0 always for `dur_ms <= 0`, which is how `bell = off` answers without a branch of its own. A
   session the user cannot see has no field to wash out — the bar's `*` is its mark — and must not keep
   the window redrawing for a picture that cannot change. */
int mux_bell_live(const Mux *m, const Layout *lay, long long now_ms, int dur_ms);
/* The grid size EVERY session of this layout gets, drawn or not. Every pane has the same height and, in
   split mode, the same width, so one size describes them all: main.c can resize a session that is NOT on
   screen to the size its pane will have, so switching to it never triggers a resize. 0/0 if no pane. */
void layout_grid_size(const Layout *l, int *cols, int *rows);
/* The window size to ASK the compositor for so that a `cols` x `rows` session really fits, bar included.
   The exact inverse of layout_compute() for the startup case. The bar's height is one cell ROW (cell_h):
   using cell_w there leaves the grid one row short for every non-square cell. */
void layout_initial_size(int cols, int rows, int cell_w, int cell_h, int pad, int gap, int bar,
                         int fm_on, int fm_size, int *win_w, int *win_h);

/* ---------- shell integration (src/shellint.c) ----------
   Kai-Term has one channel to the shell: the pty's input stream. So "change the shell's directory without
   disturbing what the user is typing" means the shell's OWN line editor consumes the bytes as a key
   binding (nothing inserted, nothing echoed) and the directory travels out of band, in a file. The
   directory is NEVER part of the sequence, so a stray sequence can never run a path. */

/* "fish" / "bash" / "zsh" / "none" — for logs and for the APC announcement. */
const char *shellint_name(int kind);
/* Which integration a shell path supports (basename match, and a login shell's leading dash is ignored).
   SHELLINT_NONE for anything else, including sh, dash, csh, and a full command line given with -e. */
int shellint_kind_for(const char *shell_path);
/* The control sequence that triggers the shell's handler, as bytes. It must be (a) something the shell's
   reader parses as ONE key, (b) inert in every supported shell when the binding is absent, so a stray
   write cannot type garbage at the prompt, (c) unbound by default, and (d) free of the tty's special
   control characters — `stty -a` reports `quit = ^\`, so a sequence containing 0x1c would SIGQUIT
   whatever owns the terminal. C-x C-] (0x18 0x1d) satisfies all four in fish and bash, so there is ONE
   sequence, not a per-shell table. */
const char *shellint_seq(int kind, size_t *len);
/* Create the private directory and write every script into it. `dir` receives the path. Returns 0 on
   success; on any failure nothing is created and the caller must run without integration. */
int shellint_prepare(char *dir, size_t cap);
/* Remove what shellint_prepare() made. Safe to call with an empty or already-removed path. */
void shellint_cleanup(const char *dir);
/* The request file for one session: the shell reads the directory to enter from here. `session` must never
   repeat within one run — NOT the tab index, which is reused when a tab closes and the array shifts. */
void shellint_request_path(const char *dir, int session, char *out, size_t cap);
/* Write `path` into that session's request file (atomically: temp + rename, so the shell can never read a
   half-written path). 0 on success. */
int shellint_write_request(const char *req_path, const char *path);
/* The command line to hand pty_spawn() (which runs it through /bin/sh -c) so the shell loads the
   integration. Returns 0 and fills `cmd` when `kind` is supported. */
int shellint_argv(int kind, const char *dir, const char *req_path, char *cmd, size_t cap);

/* ---------- file manager (src/fm.c) ----------
   An in-window browser: a listing of one directory, a context-sensitive preview of the selected entry, and
   the ordinary terminal below them. fm.c is MODEL ONLY — no Wayland, no EGL, no threads, no rendering — so
   fm_scan_build() and fm_preview_build() are pure functions of a path that main.c can run on a worker
   thread and install back on the main thread. The two panes are painted into ordinary Cell grids, so they
   read as part of the terminal. */

#define FM_NAME_MAX   256             /* one entry's name (NAME_MAX is 255 on Linux) */
#define FM_ERR_MAX    256             /* one human-readable error line */
#define FM_ENTRIES_MAX 8192           /* entries one listing keeps; the rest are dropped with a warning */
#define FM_PREVIEW_BYTES (256u * 1024u)   /* bytes of a text preview read from disk */
#define FM_PREVIEW_DIR_MAX 512        /* entries shown in a directory preview */
#define FM_PREVIEW_FILE_MAX (16u * 1024u * 1024u)  /* largest image file a preview will read */
#define FM_META_LINES 16              /* metadata lines one preview shows */
#define FM_META_WIDTH 128             /* bytes per metadata line */

/* Accent colours for the two panes, in the header so tests/test_fm.c can pin the colour a token got.
   ACCENTS only: the base ink and paper are Config.fg/Config.bg, passed to fm_paint(), so the file manager
   matches the terminal. Chosen to read on a dark background and to agree with ls and editors. */
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

/* One entry of a directory listing. Only the NAME is stored: a path would make a capped listing of 8192
   entries 32 MB, while fm_entry_path() rebuilds the full path in a caller's buffer in one snprintf. */
typedef struct {
    char name[FM_NAME_MAX];
    uint8_t kind;        /* FMK_* */
    uint8_t hidden;      /* name starts with '.' — kept in the listing, hidden from the view */
    uint8_t exec;        /* owner-executable regular file: drawn like one */
    mode_t mode;
    uint64_t size;
    int64_t mtime;       /* seconds, st_mtime */
} FmEntry;

/* The answer to one listing request. Built off the main thread, installed on it. An answer whose `seq` is no
   longer the newest one is dropped, so a slow scan cannot replace the listing of a directory the user has
   already left. */
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
    unsigned long uid;      /* unique per built preview, never reused: the renderer keys the preview's GL
                               texture on this, so a stale one can never be painted from a recycled address */
} FmPreview;

/* One painted pane. The renderer walks it exactly like a session grid (row-major, cell (x,y) at
   cell[y*cols+x]), so the file manager needs no new drawing path. */
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
    int pv_scroll;       /* rows the preview pane's BODY is scrolled down by. Owned by the wheel over that
                            pane; reset when a new preview is installed, clamped to what the pane can show.
                            The LIST pane has no such field: its scroll is its selection (top follows sel). */
    /* A text selection INSIDE the preview pane, in that pane's cell coordinates, endpoints inclusive and
       normalised so (pv_ax,pv_ay) comes first in reading order. Viewport-relative like a session's: the
       cells it names hold whatever the pane shows NOW, so it is cleared when the preview is re-installed,
       when pv_scroll moves, or when the pane is resized. The band is painted into the cells' own bg. */
    int pv_sel_on;
    int pv_ax, pv_ay, pv_bx, pv_by;
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
/* Which entry of the view the row of the LIST PANE shows, or -1 when that row is chrome (the header or the
   footer) or past the end. The painter owns the row layout, so the hit test lives next to it. */
int  fm_row_pos(const FileMgr *fm, int row);
/* Does a wheel gesture over the band move the KEYBOARD into it? Returns the FM_FOCUS_* to apply, or -1 for
   "leave the keyboard where it is". `steps` is the whole-cell scroll the gesture produced (0 must never
   move the keyboard) and `on_hover` is the `fm_focus_on_hover =` setting: 0 (default) = only a CLICK. */
int  fm_wheel_focus(int hit, int steps, int on_hover);
/* Scrolling the PREVIEW pane's body: the wheel over that pane owns pv_scroll, and the painter is the only
   thing that reads it. delta > 0 moves toward the END of the file. Clamped to fm_preview_scroll_max(),
   which is 0 for a preview with nothing to scroll (an image, plain metadata), and reset by
   fm_install_preview(). */
void fm_preview_scroll(FileMgr *fm, int delta);
int  fm_preview_scroll_max(const FileMgr *fm);
/* The same scroll, driven by KEYS while the preview pane owns the keyboard. fm_preview_page_rows() is what
   one page means — the body rows the pane shows at once — so a held Page key walks the file with no line
   shown twice and none skipped. All three go through fm_preview_scroll(), so the clamp has one owner. */
int  fm_preview_page_rows(const FileMgr *fm);
void fm_preview_page(FileMgr *fm, int pages);   /* pages < 0 = toward the top */
void fm_preview_top(FileMgr *fm);
void fm_preview_bottom(FileMgr *fm);

/* ---- a text selection inside the preview pane (the pointer's half lives in main.c) ----
   Coordinates are the PREVIEW PANE's cells, inclusive at both ends, and the pair is normalised here so a
   leftward or upward drag cannot produce an inverted range. Everything the gesture needs is here so a test
   can check it without a display. */
void fm_preview_select(FileMgr *fm, int x0, int y0, int x1, int y1);
void fm_preview_select_clear(FileMgr *fm);
int  fm_preview_selected(const FileMgr *fm, int x, int y);   /* is this pane cell inside the selection? */
/* The word at a pane cell — the SAME rule a session uses (cell_row_word_span in vt.c), so a double click
   means one thing everywhere in the window. *out_x0..*out_x1 inclusive. */
int  fm_preview_word_span(const FileMgr *fm, int x, int y, int *out_x0, int *out_x1);
/* The row a triple click takes. The preview CUTS a long line at its right edge rather than wrapping it, so
   one file line is exactly one pane row: the span is the row's OWN cells — first to last that holds
   something — which skips the pane's two-column gutter (never written) and keeps a line the file itself
   indents. There are no wrap marks to walk, unlike a session grid. A row holding nothing returns the row. */
int  fm_preview_row_span(const FileMgr *fm, int y, int *out_x0, int *out_x1);
/* Copy the selection the way term_selection_text() copies a session's: rows joined with '\n', trailing
   blanks of each row dropped, a wide glyph's continuation cell contributing nothing. One difference: each
   row is read inside its USED range, so a middle row of a multi-row selection does not turn the pane's
   never-written gutter into leading spaces. A gap between two used cells is still a space. */
int  fm_preview_selection_text(const FileMgr *fm, char *out, size_t cap);

/* ---- preview: build (worker thread) then install (main thread) ---- */
FmPreview *fm_preview_build(const char *path, unsigned long seq);
void fm_preview_free(FmPreview *p);
int  fm_install_preview(FileMgr *fm, FmPreview *pv);   /* same stale-answer rule as fm_install_scan */
/* The path that SHOULD be previewed right now (the selected entry, or the current directory when nothing is
   selected). 1 when it differs from what is installed; then *seq gets the id to ask the worker for. */
int  fm_preview_request(FileMgr *fm, unsigned long *seq, char *path, size_t cap);

/* ---- painting ---- */
/* Fill both cell grids from the current state. fg/bg/sel are the CONFIGURED colours, so the file manager
   matches the terminal instead of inventing a palette (`sel` is the terminal's own selection highlight). */
void fm_paint(FileMgr *fm, uint32_t fg, uint32_t bg, uint32_t sel);
/* Where the preview picture goes inside the preview pane, in CELLS, aspect ratio preserved for these cell
 * dimensions: the renderer draws the quad and fm.c decides the box, which keeps a 3000x200 photo from being
 * stretched to the pane. px_w/px_h are the same box in PIXELS — the cell box carries up to one cell of
 * ceil() slack, and drawing that would stretch the picture. */
void fm_image_fit(const FileMgr *fm, int cell_w, int cell_h,
                  int *col, int *row, int *cols, int *rows, int *px_w, int *px_h);

/* ---- small shared formatters (also used by the tests) ---- */
void fm_human_size(uint64_t n, char *out, size_t cap);
void fm_human_time(int64_t t, char *out, size_t cap);
void fm_mode_string(mode_t m, char out[11]);   /* "-rwxr-xr-x" / "drwxr-xr-x" / "lrwxrwxrwx" */
int  fm_lang_for_path(const char *path);       /* FM_LANG_* from a file name */

/* Text preview colouring. State carries from line to line, because a C block comment does not stop at the
 * end of a line. fg has one entry per BYTE of line, so the painter uses a codepoint's lead byte. */
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

/* A COLOR emoji glyph: the single glyph HarfBuzz shapes a whole grapheme cluster into, rasterized from a
   color (CBDT/CBLC or SVG) emoji font. Metrics are UNSCALED bitmap pixels (the strike's native size); the
   renderer scales them at draw time, so zooming never re-rasterizes. Keyed by the codepoint sequence. */
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
                               fallback instead (the renderer sets and clears it) */
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
/* Drop a trailing incomplete UTF-8 sequence left by a BYTE-bounded cut (see src/vt.c). Every byte-cut of text
   that is later drawn calls this — one rule, not four that can drift. */
void utf8_trim_partial(char *s);
int cp_is_emoji(uint32_t cp);        /* a base codepoint that may start an emoji cluster */
int cp_is_cluster_extend(uint32_t cp);  /* ZWJ / variation selector / skin tone / tag char */
int cp_is_wide(uint32_t cp);         /* occupies 2 grid cells (CJK + Emoji_Presentation) */
/* Width of a whole grapheme cluster: 1 when the base alone is wide, or when a modifier inside the cluster
   (U+FE0F emoji presentation, U+20E3 keycap) switches an emoji-capable base to the wide presentation. */
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
    /* No grid origin here: the frame is a list of rectangles (Layout), each with its own origin, so main.c
       passes them per frame instead of setting one global offset. */
    int u_mvp, u_atlas, u_catlas, u_img, u_sel;
    /* One GL texture per image the model currently shows (see KImage). Keyed by KImage.serial, unique per
       session and never reused, so a recycled pointer cannot make the renderer reuse a stale texture.
       Entries for images that are not on screen are dropped. */
    ImgTex *imgtex;
    int nimgtex, imgtex_cap;
    int img_frame;               /* bumped once per frame; ImgTex.seen == img_frame means "used this frame" */
    /* The file manager's preview picture: at most one at a time, so it gets one texture instead of a cache.
       Re-uploaded when FmPreview.uid changes or the pixels were resized. fm_px is the premultiplied copy
       the shader wants; the preview model keeps plain RGBA so its tests can compare decoded bytes. */
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
    uint32_t fg;                 /* the tab bar's text colour, and the base the inactive item / separator
                                    colours are mixed from. Chrome comes from the CONFIG, never from a
                                    cell's pen: an application repaints its own cells, not the bar. */
    uint32_t cursor_color;       /* the `cursor_color =` line: the visible session's cursor colour, and the base
                                    a drawn-but-not-visible session's dimmed cursor is mixed from. It has to
                                    reach the renderer: a configured colour that is stored and read by
                                    nothing is not implemented (this key once was exactly that). */
    uint32_t selection_color;    /* the `selection_color =` line: the selection band (also the shader's
                                    u_sel uniform), the visible session's tab-bar item, and the focus
                                    border. Built-in default is the SEL_BG macro. */
    int cursor_width;            /* the `cursor_width =` line: pixels wide for the bar, tall for the underline. It
                                    reaches the renderer here; render_cursor() clamps it to 1..cell/3. */
    /* bell_on: the `bell =` line as a drawing decision (1 = BELL_VISUAL). A SEPARATE field from the timestamps
       below, so `off` cannot be faked by forgetting to record a ring: a session that rang while the bell is
       off still holds its timestamp, and nothing draws because the renderer passes a duration of 0. */
    int bell_on;
    /* The blink phase of THIS frame: main.c's blink timer flips W.cursor_on and calls redraw(), and the frame
       copies it here. Window state, never Term state — term_init() memsets Term for every new tab. Only the
       visible session's cursor blinks: render_draw() forces 1 for a pane that is drawn but not active. */
    int cursor_on;
    /* The visual bell, per session: `bell_ms[i]` is the CLOCK_MONOTONIC ms session i last rang (0 = never),
       copied from Tab.bell_ms by redraw(), and `now_ms` is this frame's reading of the same clock. The
       renderer asks bell_strength() with them and never reads a clock itself. Indexed by TAB index. */
    long long now_ms;
    long long bell_ms[TAB_MAX];
    /* The address the pointer is over, as the frame sees it: `hover_tab` is the TAB whose pane carries the
       underline (-1 = none), `hover_span` is the run in that session's VIEWPORT rows and columns — the
       space render_draw()'s passes walk. Window state copied per frame, never Term state: it must not
       survive a tab switch, a scroll or a resize it has no business surviving. */
    int hover_tab;
    UrlSpan hover_span;
    void *ecfg;                  /* EGLConfig */
    void *nwin;                  /* EGLNativeWindowType (wl_egl_window) */
    /* Texture uploads fail when the GPU is out of memory. Every upload site logs the failure, and none may draw
       with a texture that has no texels — on the NVIDIA driver that faults inside libnvidia-eglcore. These
       counters back the error lines off to about one per two seconds while a retry loop runs. */
    int atlas_upload_fails, catlas_upload_fails, img_upload_fails, fm_upload_fails;
    int catlas_wait;             /* frames to skip before re-trying a refused color-atlas upload */
} Render;

/* ---------- image decoding (src/img.c) ----------
   The one place bytes become pixels. Both producers share it: src/vt.c (the kitty graphics protocol) and
   src/fm.c (the preview pane). Output is always RGBA8, never premultiplied — the caller premultiplies when
   it knows where the pixels are going. PNG/JPEG/WebP use the system libraries; GIF is decoded in-tree. */

/* Caps every decoder enforces, so neither a hostile escape stream nor a 30 MB photo can make a frame late.
   One definition, because the graphics path and the preview path must refuse the same picture. */
#define IMG_DIM_MAX  4096            /* accepted px per side */
#define IMG_PIX_MAX  (16LL << 20)    /* accepted pixels per image */

/* 1 when these bytes are a format img_decode_mem can read. `kind` (8 bytes, may be NULL) comes back as
   "png" / "jpg" / "gif" / "webp", or "" when nothing matched. Recognition is by the bytes, never by a name
   or by what a protocol's f= field claims. */
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
/* Every CHROME colour of one frame, derived from the configured palette in Render (bg/fg/cursor_color) and
   from nothing else: the selection band, the tab bar's items and separators, the focus border, and both
   cursor colours. An application may repaint its own cells but never the chrome that names its tab (see
   Render.fg). A function rather than locals inside render_draw() so a test can check a configured colour
   where it becomes a vertex colour. Pure — no GL, no font, no Term, no allocation. */
typedef struct Chrome {
    uint32_t sel_bg;                      /* selection band; also the shader's u_sel uniform */
    uint32_t item_off_bg, item_off_fg;    /* a bar item that is not the visible session */
    uint32_t item_on_bg;                  /* the visible session's bar item */
    uint32_t sep;                         /* the bar's separators */
    uint32_t cursor;                      /* the visible session's cursor */
    uint32_t cursor_off;                  /* a drawn-but-not-visible session: cursor mixed toward bg */
    uint32_t border;                      /* the focus border */
    /* The confirm-quit row: the selection highlight for the field and the default ink for the text — the two
       colours the window already uses to mean "this is what the user is being asked about". */
    uint32_t notice_bg, notice_fg;
    /* The window-close box: a lifted paper field so it reads as above the terminal, a border and a Yes label in
       the colour that means "the thing in this window" (selection_color), the No label in the palette's
       red (configured and themeable, not a literal), and button fields matching the terminal's paper. */
    uint32_t dialog_bg, dialog_border, dialog_fg, dialog_yes, dialog_no, dialog_btn_bg;
} Chrome;
void render_chrome(const Render *r, Chrome *out);

/* The colour of a visual-bell flash at `strength` (0..1000, from bell_strength()): the paper mixed
   BELL_MIX_MAX percent toward the ink at full strength, fading to the paper itself at 0. NOT a Chrome
   field: the strength is per pane and per frame, while Chrome is one set of colours per frame. Pure, so
   the colour is checked where it is decided. */
uint32_t render_bell(uint32_t bg, uint32_t fg, int strength);

/* What ONE cursor cell is drawn as. The three parts of a frame (cell backgrounds, selection bands,
   glyphs) each ask a different question about the same cell and they MUST agree: a cursor that
   disagrees with the selection band erases itself. Pure: no GL, no font, no Term, no allocation.
   style    CUR_UNSET (no DECSCUSR seen — a bar is drawn) / CUR_BLOCK / CUR_BAR / CUR_UNDERLINE
   visible  0 draws nothing at all: DEC 25l, a scrolled-back viewport, or the blink phase being off
   band_on  a selection band is painted on this cell. A BLOCK steps aside for it (the ink is tinted
            with the cursor colour instead); a BAR does not, because its quad is emitted after the bands.
   invert   the glyph part swaps the ink with the cell's own background (a block over no band)
   tint     the glyph part paints the ink IN the cursor colour (a block under a band)
   ascent_px, descent_px  the font's metrics for the cell size in use. Only the underline asks them:
            "under the text" is a statement about the baseline, which sits cell_h - ascent_px above
            the cell's bottom edge.
   x,y,w,h  the separate quad's rect in cell-local pixels. x is px from the cell's LEFT edge; y is px
            measured UP from the cell's BOTTOM edge, because the projection maps buffer pixels with y
            growing up. A BAR spans the whole height and cannot show the difference; an underline can. */
typedef struct CursorDraw {
    int block, quad, invert, tint;
    int x, y, w, h;
} CursorDraw;
void render_cursor(int style, int visible, int band_on, int cell_w, int cell_h, int width_px,
                   int ascent_px, int descent_px, CursorDraw *out);

/* One place decides what the HOVER UNDERLINE looks like — the rect of one cell of the address the pointer
   is on. Same reason render_cursor() is a function: the draw loop is in no test binary. The shape is the
   CUR_UNDERLINE shape of render_cursor(), asking the same underline_y(), because two underlines in one cell
   at different heights are two answers to one question. The thickness is NOT `cursor_width` — that key is
   the cursor's — but a fraction of the cell with the same 1 px floor. Pure: no GL, no font, no Term. */
typedef struct {
    int x, y, w, h;      /* px inside the cell; y is measured UP from the cell's bottom edge */
} HoverDraw;
void render_hover(int cell_w, int cell_h, int ascent_px, int descent_px, HoverDraw *out);

int  render_init(Render *r, void *wl_display, void *egl_window, int w, int h, float opacity,
                 uint32_t bg, uint32_t fg, uint32_t cursor_color, uint32_t selection_color,
                 int cursor_width, int bell_on);
/* Draw one frame: the tab bar (if the layout says so), every pane's grid, and the file-manager band if the
   layout has one. Two vertex batches (backgrounds, then bands + glyphs + chrome) with one draw call per
   application picture between them — see the header comment of src/render.c for why the frame cannot be a
   single call. `lay` carries every rectangle; `terms` is indexed by TAB index, so pane p draws
   terms[lay->pane[p].tab] (NULL = empty pane); `fm` may be NULL. Returns 1 when a new buffer was attached,
   0 when nothing was drawn, -1 when the glyph atlas could not be uploaded even after dropping every picture
   texture: nothing can be drawn at all, and drawing with an unbacked texture segfaults in the driver. */
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
