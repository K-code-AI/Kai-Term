/* Kai-Term — Wayland frontend, event loop, main. */
#include "kai-term.h"
#include "xdg-shell-client-protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>
#include <xkbcommon/xkbcommon.h>

#define FRAME_INTERVAL_NS 8000000   /* 8 ms redraw batching window */
#define SCROLL_LINES_PER_DETENT 3   /* scrollback lines moved per mouse-wheel notch */

/* The KB_KEY_* numbers in kai-term.h are hand-written X keysym numbers, and a binding only works if
   they are exactly what xkb_state_key_get_one_sym() reports. Checked at compile time: a wrong number
   would make a config binding silently dead, which no runtime test here would notice. */
_Static_assert(KB_KEY_BackSpace == XKB_KEY_BackSpace, "KB_KEY_BackSpace must match xkbcommon");
_Static_assert(KB_KEY_Tab == XKB_KEY_Tab, "KB_KEY_Tab must match xkbcommon");
_Static_assert(KB_KEY_Return == XKB_KEY_Return, "KB_KEY_Return must match xkbcommon");
_Static_assert(KB_KEY_Escape == XKB_KEY_Escape, "KB_KEY_Escape must match xkbcommon");
_Static_assert(KB_KEY_Space == XKB_KEY_space, "KB_KEY_Space must match xkbcommon");
_Static_assert(KB_KEY_Prior == XKB_KEY_Page_Up, "KB_KEY_Prior must match xkbcommon");
_Static_assert(KB_KEY_Next == XKB_KEY_Page_Down, "KB_KEY_Next must match xkbcommon");
_Static_assert(KB_KEY_Home == XKB_KEY_Home, "KB_KEY_Home must match xkbcommon");
_Static_assert(KB_KEY_End == XKB_KEY_End, "KB_KEY_End must match xkbcommon");
_Static_assert(KB_KEY_Insert == XKB_KEY_Insert, "KB_KEY_Insert must match xkbcommon");
_Static_assert(KB_KEY_Delete == XKB_KEY_Delete, "KB_KEY_Delete must match xkbcommon");
_Static_assert(KB_KEY_Left == XKB_KEY_Left, "KB_KEY_Left must match xkbcommon");
_Static_assert(KB_KEY_Up == XKB_KEY_Up, "KB_KEY_Up must match xkbcommon");
_Static_assert(KB_KEY_Right == XKB_KEY_Right, "KB_KEY_Right must match xkbcommon");
_Static_assert(KB_KEY_Down == XKB_KEY_Down, "KB_KEY_Down must match xkbcommon");
_Static_assert(KB_KEY_F1 == XKB_KEY_F1, "KB_KEY_F1 must match xkbcommon");
_Static_assert(KB_KEY_F12 == XKB_KEY_F12, "KB_KEY_F12 must match xkbcommon");

typedef struct Wayland {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_egl_window *egl_window;

    struct wl_data_device_manager *data_mgr;
    struct wl_data_device *data_dev;

    struct xkb_context *xkb_ctx;
    struct xkb_keymap *keymap;
    struct xkb_state *xkb_state;

    struct wl_cursor_theme *cursor_theme;
    struct wl_cursor_image *cursor_image;
    struct wl_surface *cursor_surface;

    int comp_ver;               /* bound wl_compositor interface version */
    int pw, ph;                 /* pending configure size (px) */
    int cur_w, cur_h;           /* applied size (px) */
    /* The visible session's grid origin inside the window, in px (layout_apply()'s pane origin, padding and
       tab bar included) — the pointer is mapped through it so a click names the cell drawn under it. */
    int gx, gy;
    int first_done;
    /* The one outstanding wl_surface_frame callback, NULL when none is in flight. It replaces the old
       `frame_armed` flag and `swapped` counter: the callback OBJECT is the state, so it cannot drift
       out of sync with what was actually committed (see the invariant above request_frame). */
    struct wl_callback *frame_cb;
    int swapped_once;           /* at least one successful swap (the first configure must draw) */
    int resized;                /* a configure changed the pixel size and no buffer of that size has
                                   been attached yet: the compositor is waiting for one, so the frame
                                   path MUST answer with a swap even when nothing else is dirty */
    int dirty;
    int running;
    int fatal;                  /* the renderer gave up (no video memory): main() exits 1, not 0 */
    int is_fullscreen, is_maximized, is_activated;   /* xdg_toplevel configure states (debug only) */
    /* The blink phase of the visible session's cursor: 1 = drawn this frame. Window state on purpose —
       term_init() memsets Term for every new tab, so a phase in the model would survive a tab switch, a
       zoom and a resize it has no business surviving. Steady whenever the blink is not armed. */
    int cursor_on;
    /* 1 while a visual-bell flash is on screen for a DRAWN session. Window state, like cursor_on. It exists so
       the tick that keeps the window dirty while a flash fades asks for EXACTLY ONE more frame when the last
       flash ends: at that moment bell_strength() answers 0 and nothing else would ever redraw again. */
    int bell_live;
    int32_t ptr_x, ptr_y;       /* last pointer position in surface coords (px) */
    /* 1 while the pointer is on this surface. wl_pointer.leave does NOT clear ptr_x/ptr_y — the last position
       stays — so without this a pointer that left the window would keep naming the cell it last touched, and the
       hover underline would sit on screen after the pointer is gone. */
    int ptr_in;
    /* The pane/cell the last pointer event named, kept only to skip a frame when a motion changed nothing:
       a compositor sends one motion per pixel. -1 = over no pane. */
    int hover_tab, hover_col, hover_row;
    int ptr_btn;                /* held button mapped to SGR index (0/1/2), or -1 */
    int selecting;              /* 1 while a left-button text selection drag is in progress */
    int pv_selecting;           /* 1 while a drag extends a selection inside the file manager's preview
                                   pane. Kept apart from `selecting`: the two drags name cells of different
                                   grids and only one can be live. */
    /* The multi-click gesture: click_count is which press in a row this is (1 = fresh, from
       term_click_count()), click_word suppresses the cursor keys synthesised on release. */
    int click_count;
    uint32_t click_ms;
    int click_col, click_row;
    int click_word;
    /* A click on a web address waits out the double-click window before opening, so a second click can take
       the word instead. url_tick() re-asks at the deadline and drops the open if the cell's answer has
       changed. */
    long long url_press_ms;         /* 0 = nothing pending */
    int url_tab;                    /* the tab index the click was in */
    int url_col, url_row;           /* the cell it was in */
    char url_text[URL_MAX + 1];     /* what was under that cell at the click */
    double scroll_accum;        /* pending scrollback scroll in lines (+ = up/history) */
    double wheel_accum;         /* The same carry, for a wheel gesture turned into application cursor keys.
                                   Kept apart from scroll_accum so leaving the alternate screen mid-gesture
                                   cannot hand scrollback a remainder it never asked for. */
    double frame_axis_val;      /* axis value (px) accumulated within the current scroll frame */
    int    frame_discrete;      /* discrete wheel steps accumulated within the current frame */
    int    frame_has_axis;      /* an axis event occurred in the current frame */
    int    frame_discrete_seen; /* an axis_discrete event occurred in the current frame */
} Wayland;

static Wayland W;
/* Multiplexing: every session this window hosts, plus which one is visible. The two accessors below read
   mux.active on every call, so there is no second copy of that state to drift. */
static Mux mux;
static Font font;
static Render rend;
static Config conf;
static const char *opt_cmd = NULL;   /* -e/--exec: argv storage lives until exit, so const is fine */
static const char *g_cmd = NULL;     /* what every session runs (opt_cmd, config shell, or NULL) */
static char g_wdir[4096];            /* resolved startup directory, reused by every new tab */
/* Shell integration's private per-instance directory holding its shell scripts: made at startup, removed on
   the way out. Empty means running without integration. */
static char g_intdir[FM_PATH_MAX];
/* Session ids for the request files. A counter, deliberately NOT the tab index: mux_close() shifts the array
   down and a new tab reuses the freed index, so two live sessions could otherwise share one request file and
   one of them would cd into a directory the other asked for. */
static int g_int_seq;
static int base_font_size;      /* configured size, restored by Ctrl+0 */

/* How this window's pixels are shared: the tab-bar strip, one rect per session, and the grid size every
   session gets. src/layout.c computes it, render_draw() draws exactly it, the pointer hit tests use it. */
static Layout g_layout;
static int layout_mode = LAYOUT_TABS;   /* LAYOUT_TABS (one session fills the window) / LAYOUT_SPLIT */
/* The confirm-quit question. confirm_kind doubles as "is a question up?", so no question kind may be 0:
   CONFIRM_QUIT = 0 would read as "nothing asked", draw nothing and swallow the close. */
static int  confirm_kind = CONFIRM_NONE;
static int  confirm_tab = -1;
/* One buffer for both questions. DIALOG_MAX is the larger of the two limits, so neither the row's text
   nor the box's sentence can ever be cut by the buffer it travels in. */
static char confirm_text[DIALOG_MAX];
/* px between panes, and between the tab bar and the grid. Deliberately not a config key: Config.padding
   already sets the margin, and a gap of 0 would let one pane's focus border draw over its neighbour. */
#define PANE_GAP 8

/* The in-window file manager: only the wiring lives here — which region owns the keyboard, the worker
   thread, its result pipe, and the `cd <path>` the shell gets. The model is src/fm.c. */
static FileMgr g_fm;

/* The worker's request slots hold only the newest request of each kind: one superseded before the thread
   reached it is overwritten, so the disk is not even read for it. */
enum { FMQ_SCAN = 0, FMQ_PREVIEW = 1, FMQ_KINDS = 2 };
typedef struct {
    int used;
    char path[FM_PATH_MAX];
    unsigned long seq;
} FmReq;
static FmReq fm_req[FMQ_KINDS];
static int fm_quit;                 /* the worker must leave its cond_wait and exit */
static int fm_worker_up;
/* The directory the shell is believed to be in (fm_follow) — the one fact every follow decision is made
   from. One per window; tab_switch() clears it so a newly visible shell is re-synced on the next
   navigation. */
static char fm_last_cd[FM_PATH_MAX];

/* The clear -> `cd` -> re-populate sequence, one step per parsed pty burst: the erase must be PROVEN before
   the `cd`, or a line editor that binds Ctrl+U elsewhere leaves half a command for the `cd` to run on. */
enum { FMCD_IDLE = 0, FMCD_INTEGRATION, FMCD_ERASING, FMCD_CDING, FMCD_RETYPING };
static const char *const fm_cd_state_name[] = { "idle", "integration", "erasing", "cd", "re-typing" };
static int fm_cd_state = FMCD_IDLE;
static int fm_cd_tab = -1;              /* the session the sequence belongs to */
static char fm_cd_line[FM_LINE_MAX];    /* what the user had typed, captured before the erase */
static char fm_cd_dir[FM_PATH_MAX];     /* the directory the browser is in NOW (a newer navigation
                                           re-targets it, so the shell always ends up where the user is
                                           looking, even if they moved again mid-sequence) */
static int fm_cd_docrow = -1;           /* the DOCUMENT row the last `cd` was typed on, or -1 when no `cd`
                                           was sent. The new prompt is believed only once the cursor has
                                           left that row. */
static uint64_t fm_cd_deadline;         /* CLOCK_MONOTONIC ms; a stuck sequence is dropped, not retried */
static uint64_t fm_cd_last_burst;       /* CLOCK_MONOTONIC ms of the last output burst of the owning
                                           session — CDING counts its quiet period from this */
/* A shell that never redraws its prompt, or a line editor that does not bind Ctrl+U to erase-to-start,
   would otherwise leave this waiting forever. Two seconds is far longer than a local shell needs to
   repaint and short enough that a stall is never felt. */
#define FMCD_TIMEOUT_MS 2000
/* How long the session must have STOPPED printing before CDING believes the new prompt is drawn. A burst
   boundary is not the prompt: a shell answers a `cd` with the echo, a continuation row and prompt chrome,
   each in its own burst. Stopping at the first would remember a blank row as "the prompt". */
#define FMCD_QUIET_MS 150
/* How long to wait for an integrated shell to confirm the directory it entered. The ack comes back within
   tens of milliseconds, so 400 ms is far above it yet short enough that a shell which never answers — a
   nested shell, a script the user overrode — falls back to the typed path inside one keystroke. */
#define FM_INT_ACK_MS 400
static void fm_cd_abort(const char *why);

static uint64_t mono_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000L);
}

/* One click selects, a second on the same entry within CLICK_DCLICK_MS opens. Button `time` is ms on the
   keyboard's clock; unsigned arithmetic makes the 49-day wrap read as a long gap. */
static uint32_t fm_click_ms;
static int fm_click_pos = -1;     /* view position of that click, -1 = no pending first click */
/* The preview pane's click state is deliberately separate from the list's: that pair compares a view
   POSITION (a second click on the same ENTRY opens it), this one compares a pane CELL (a second click
   selects the WORD, a third the ROW). A list row number is not comparable with a preview column. */
static uint32_t fm_pv_click_ms;
static int fm_pv_click_x = -1, fm_pv_click_y = -1, fm_pv_click_count;
static int fm_pv_anchor_x = -1, fm_pv_anchor_y = -1;   /* where the drag started */
static int fm_pv_drag_x = -1, fm_pv_drag_y = -1;       /* the last cell it was extended to */
static pthread_t fm_thr;
static pthread_mutex_t fm_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fm_cv = PTHREAD_COND_INITIALIZER;
static int fm_pipe[2] = { -1, -1 }; /* [0] read end (in epoll), [1] written by the worker */

/* One answer through the pipe. Both ends are the same process, so the pointer is just a value; the write
   is one 24-byte struct, far under PIPE_BUF, so it is atomic and the reader never sees half an answer. */
typedef struct {
    int kind;                       /* FMQ_SCAN / FMQ_PREVIEW */
    unsigned long seq;
    unsigned long ptr;              /* FmScan * or FmPreview *, ownership moves to the reader */
} FmResult;

static Term *cur_term(void)
{
    Tab *t = mux_cur(&mux);
    return t ? &t->term : NULL;
}

static Pty *cur_pty(void)
{
    Tab *t = mux_cur(&mux);
    return t ? &t->pty : NULL;
}

/* clipboard (Wayland data-device) state */
static char *clip_text;         /* selection text offered to the clipboard (heap) */
static size_t clip_len;
static struct wl_data_source *clip_source;
static struct wl_data_offer *sel_offer;    /* current clipboard selection offer (may be NULL) */
static char sel_offer_mime[128];           /* a text mime offered by sel_offer */
static int sel_offer_text;                 /* 1 when sel_offer offers a text mime */
static char last_offer_mime[128];          /* the BEST text mime recorded from the most recent data_offer */
static int last_offer_text;                /* its clipboard_mime_rank() (0 = no text type seen) */
static int paste_fd[2] = { -1, -1 };       /* pipe: [0] read end (in epoll), [1] given to receive */
/* The whole paste is buffered before it reaches the PTY: the ESC[200~ / ESC[201~ delimiters have to
   surround the ENTIRE block, so the text cannot be streamed through as it arrives. */
static char *paste_buf;
static size_t paste_len, paste_cap;
static int paste_overflow;                 /* paste bigger than PASTE_BUF_MAX: send it raw */
#define PASTE_BUF_MAX (8u << 20)           /* 8 MiB of clipboard text */
static int g_ep = -1;                      /* main epoll fd (for adding the paste pipe) */

/* Wayland delivers one press and one release per physical keypress, so the client generates its own repeats
   — timed from Kai-Term's config, so the feel is the same on every compositor. */
static int krfd = -1;
static int rep_key = -1;        /* Wayland keycode being repeated, -1 = none */
static uint32_t rep_serial;     /* serial of that key's press (clipboard actions need one) */

/* ---------------- helpers ---------------- */

static const struct wl_callback_listener frame_listener;
static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t format, int fd, uint32_t size);
static void selection_copy(uint32_t serial);
static int clipboard_paste(const char *why);
static void update_title(void);
/* The preview pane's cell under the pointer, defined with the other pointer mappers further down but needed
   by the band's click handler above them. */
static int pv_cell(int *x, int *y);
/* The hover underline's three helpers, defined with the other pointer mappers further down. hover_update() runs in
   every redraw() (the run under the pointer is a fact about the frame, not about the motion event that happened to
   precede it), and hover_motion() runs in the pointer listener to decide whether a motion deserves a frame. */
static int hover_cell(int *tab, int *col, int *row);
static void hover_motion(void);
static void hover_update(void);
/* Is Shift down right now? Defined with the pointer listener that asks it; hover_update() needs it too, because an
   underline promises a plain click and Shift + click means selection. */
static int ptr_shift_held(void);
/* Declared here because the file manager's `cd` and typed path (further up) go through it: it is the one
   place that tells the model "these bytes are INPUT, expect an echo" (term_note_input, src/vt.c). */
static void input_write(Term *t, Pty *p, const void *buf, size_t len);

static void wl_flush(void)
{
    while (wl_display_flush(W.display) < 0 && errno == EAGAIN) {
        struct pollfd pfd = { .fd = wl_display_get_fd(W.display), .events = POLLOUT };
        poll(&pfd, 1, 100);
    }
}

/* ---- frame pacing ----
   A frame callback is armed only by a commit that attaches a NEW buffer: armed on an empty commit
   it can stay pending forever, and every draw path here waits on there being no callback in flight. */
static void request_frame(void)
{
    if (W.frame_cb || !W.surface)
        return;
    W.frame_cb = wl_surface_frame(W.surface);
    wl_callback_add_listener(W.frame_cb, &frame_listener, NULL);
}

/* Recompute the layout and give every session the grid of a pane in it, drawn or not: a tab switch then
   costs no resize, no SIGWINCH storm and no reflow. W.gx/W.gy become the visible session's pane origin. */
static void layout_apply(void)
{
    if (!W.egl_window || mux_count(&mux) == 0)
        return;

    LayoutReq rq;
    memset(&rq, 0, sizeof rq);
    rq.mode = layout_mode;
    rq.win_w = W.cur_w;
    rq.win_h = W.cur_h;
    rq.cell_w = font.cell_w;
    rq.cell_h = font.cell_h;
    rq.pad = conf.padding;
    rq.gap = PANE_GAP;
    rq.ntab = mux_count(&mux);
    rq.active = mux_active(&mux);
    rq.split_max = conf.split_max;
    rq.bar = conf.tab_bar;
    rq.fm_on = g_fm.on;
    rq.fm_preview_on = g_fm.preview_on;
    rq.fm_size = conf.fm_size;
    /* The question, whichever form it takes. The ROW (a tab close) costs the panes one row — the price of
       drawing chrome in the grid area rather than over it, so the terminal stays readable above it. The
       BOX (a window close) costs nothing: it is centred over the grid, the pane rect unchanged. */
    rq.notice_on = (confirm_kind == CONFIRM_CLOSE_TAB) ? 1 : 0;
    rq.notice_text = confirm_text;
    rq.dialog_on = (confirm_kind == CONFIRM_QUIT) ? 1 : 0;
    rq.dialog_text = confirm_text;
    for (int i = 0; i < rq.ntab; i++) {
        rq.tab[i].tab = i;
        rq.tab[i].attention = mux.tab[i].attention;
        rq.tab[i].title = mux.tab[i].term.title;   /* OSC 0/2; empty until the shell sets one */
    }
    layout_compute(&rq, &g_layout);

    int cols, rows;
    layout_grid_size(&g_layout, &cols, &rows);
    if (cols < 1 || rows < 1)
        return;

    for (int i = 0; i < mux_count(&mux); i++) {
        Tab *tb = &mux.tab[i];
        /* The image protocol sizes pictures in PIXELS while the grid is in cells, and an application learns the
           cell size from the PTY's winsize (timg selects its kitty graphics mode only when that geometry
           is non-zero). This is the one place where the font metrics and every session are in scope. */
        term_set_cell_px(&tb->term, font.cell_w, font.cell_h);
        pty_set_cells(&tb->pty, font.cell_w, font.cell_h);
    }

    for (int i = 0; i < mux_count(&mux); i++) {
        Tab *tb = &mux.tab[i];
        if (cols == tb->term.g.cols && rows == tb->term.g.rows)
            continue;
        term_resize(&tb->term, rows, cols);
        pty_setsize(&tb->pty, rows, cols);   /* SIGWINCH, so every shell redraws at its pane width */
        const PaneGeom *pg = &g_layout.pane[0];
        klog(L_DEBUG, "grid resized to %dx%d (window %dx%d px, cell %dx%d, padding %d, "
             "unused %d x %d px, tab %d/%d, %s view, %d pane%s)",
             cols, rows, W.cur_w, W.cur_h, font.cell_w, font.cell_h, conf.padding,
             pg->w - cols * font.cell_w, pg->h - rows * font.cell_h,
             i + 1, mux_count(&mux), layout_mode == LAYOUT_SPLIT ? "side-by-side" : "tab",
             g_layout.npane, g_layout.npane == 1 ? "" : "s");
    }

    int pi = layout_pane_of_tab(&g_layout, mux_active(&mux));
    if (pi >= 0) {
        W.gx = g_layout.pane[pi].x;
        W.gy = g_layout.pane[pi].y;
    }

    /* The band's two panes are Cell grids sized by the layout like a session grid; this is the only place
       that knows their rects, so the only place allowed to resize them. Off, they are 0x0. */
    fm_set_size(&g_fm, g_layout.fm.list_cols, g_layout.fm.list_rows,
                g_layout.fm.prev_cols, g_layout.fm.prev_rows);

    /* One line per SHAPE change rather than per frame: it is the client-side evidence for every
       "the bar or the panes look wrong" report, and it is what tests/cli_test.sh greps for. */
    static int last_bar = -1, last_npane = -1, last_mode = -1;
    if (last_bar != g_layout.bar_on || last_npane != g_layout.npane || last_mode != layout_mode) {
        last_bar = g_layout.bar_on;
        last_npane = g_layout.npane;
        last_mode = layout_mode;
        klog(L_INFO, "layout: %s view, %d pane%s, tab bar %s, grid %dx%d per session",
             layout_mode == LAYOUT_SPLIT ? "side-by-side" : "tab",
             g_layout.npane, g_layout.npane == 1 ? "" : "s", g_layout.bar_on ? "on" : "off",
             cols, rows);
    }
}

/* Move the viewport through scrollback. delta > 0 scrolls UP toward row[0] (the oldest retained line), < 0
   toward the live bottom; clamped to 0..live_off. Returns the movement applied, 0 at an end. */
static int scrollback_nav(Term *t, int delta)
{
    int live = t->g.live_off;
    if (live <= 0)
        return 0;   /* nothing above the live view yet */
    int old = t->g.scroll_off;
    int so = iclamp_(old - delta, 0, live);
    int applied = old - so;
    if (applied) {
        t->g.scroll_off = so;
        W.selecting = 0;
        term_select_clear(t);   /* selection is viewport-relative; drop it when scrolling */
        W.dirty = 1;
    }
    return applied;
}

/* A smooth scroll in (possibly fractional) lines; delta_lines > 0 is up into history. The fraction is
   carried in W.scroll_accum so pixel-wheel motion still adds up to whole lines. */
static void scroll_apply(double delta_lines)
{
    W.scroll_accum += delta_lines;
    int whole = (int)W.scroll_accum;   /* truncate toward zero */
    if (whole == 0)
        return;
    int applied = scrollback_nav(cur_term(), whole);
    W.scroll_accum -= whole;
    if (applied != whole)
        W.scroll_accum = 0;   /* hit the top/bottom: drop any pending remainder */
}

/* Return the view to the live bottom, and drop any pending fractional scroll so it cannot linger. Per
   session: scrolling in one tab must not move another tab's view. */
static void snap_to_live(Term *t)
{
    W.scroll_accum = 0;
    if (t->g.scroll_off != t->g.live_off) {
        t->g.scroll_off = t->g.live_off;
        W.dirty = 1;
    }
}

/* Zoom: re-rasterize the font at a new pixel size (window px unchanged, so cols/rows change).
   layout_apply() gives every session a new grid, so existing text is left intact and nothing here re-
   paints. */
static void zoom_apply(int newpx)
{
    newpx = iclamp_(newpx, 6, 256);
    if (newpx == conf.font_size)
        return;
    conf.font_size = newpx;
    if (font_resize(&font, newpx) < 0)
        return;
    layout_apply();   /* new cell px -> new pane rects -> new grid for every session */
    W.dirty = 1;
    klog(L_DEBUG, "zoom: font %dpx, grid %dx%d", conf.font_size, cur_term()->g.cols,
         cur_term()->g.rows);
}

static int swap_fail_streak;    /* consecutive failed swaps; only used to back off the log line */

static void redraw(void)
{
    if (!W.egl_window || !rend.prog || !cur_term())
        return;
    W.dirty = 0;
    /* The bar labels, their unread marks and the pane rects ARE the picture, so the layout is recomputed
       here too (it resizes nothing when the grids already match). */
    layout_apply();
    /* The buffer this configure attaches supersedes the pending one, so destroy its callback: libwayland
       discards events of a destroyed object, so an old done cannot clear the new callback's state. */
    if (W.frame_cb) {
        wl_callback_destroy(W.frame_cb);
        W.frame_cb = NULL;
    }
    /* `terms` is indexed by TAB index, exactly like PaneGeom.tab, so the renderer pairs a pane with its own
       session and cannot be off by one on a pane index. */
    Term *terms[TAB_MAX];
    for (int i = 0; i < mux_count(&mux); i++)
        terms[i] = &mux.tab[i].term;
    /* Paint the band from the model into its Cell grids: after layout_apply() sized them, before the frame
       is built, and only when fm->dirty says the model changed. */
    if (g_fm.on && g_fm.dirty)
        fm_paint(&g_fm, conf.fg, conf.bg, conf.selection_color);
    /* The blink phase of this frame. Copied, not read from W inside the renderer: the renderer takes its
       window state through Render (opacity, colours) and never reaches into main.c's globals. */
    rend.cursor_on = W.cursor_on;
    /* The visual bell of this frame: the same rule, and the reason the renderer reads no clock. Indexed by TAB
       index like `terms` above, so a pane pairs with its own session's timestamp and in side-by-side view only
       the pane whose program rang washes out. A session that is not in the layout has no entry to read. */
    rend.now_ms = (long long)mono_ms();
    for (int i = 0; i < TAB_MAX; i++)
        rend.bell_ms[i] = i < mux_count(&mux) ? mux.tab[i].bell_ms : 0;
    /* The hover underline of this frame, asked the same way: main.c owns the pointer and the layout, the renderer
       is handed the run. After layout_apply() (the pane rects are what map the pointer) and before render_draw(). */
    hover_update();
    int drew = render_draw(&rend, &font, &g_layout, terms, &g_fm);
    if (drew < 0) {
        /* The renderer could not put the glyph atlas in GPU memory even after dropping every picture texture, so
           there is nothing this frame — or any later one — can draw. The alternative is drawing with a
           texture that has no texels, which faults inside libnvidia-eglcore. Close the window normally. */
        klog(L_ERROR, "kai-term: no video memory for the glyph atlas — exiting");
        W.fatal = 1;
        W.running = 0;
        return;
    }
    if (!drew) {
        /* No buffer was attached, so nothing may be armed on the commit that follows (see frame pacing).
           Keep the frame dirty so the 8 ms timer retries, and do not print one error line per retry. */
        if ((swap_fail_streak++ % 64) == 0)
            klog(L_WARN, "swap failed (%d in a row), retrying on the next frame", swap_fail_streak);
        W.dirty = 1;
        return;
    }
    swap_fail_streak = 0;
    if (W.resized)
        klog(L_DEBUG, "configure answered: buffer %dx%d attached (grid %dx%d)", W.cur_w, W.cur_h,
             cur_term()->g.cols, cur_term()->g.rows);
    W.resized = 0;                    /* the buffer just attached has the configured size */
    W.swapped_once = 1;
    request_frame();                  /* arm BEFORE the commit: it belongs to this frame */
    wl_surface_commit(W.surface);     /* present this frame; the armed cb fires on it */
    wl_display_flush(W.display);
}

/* Applies a pending configure. Returns 1 when the pixel size actually changed — the configure handler
   needs to know, because a size change makes the buffer still attached the WRONG size. */
static int apply_pending_resize(void)
{
    if (!W.egl_window || W.pw <= 0 || W.ph <= 0)
        return 0;
    if (W.pw == W.cur_w && W.ph == W.cur_h)
        return 0;
    W.cur_w = W.pw;
    W.cur_h = W.ph;
    wl_egl_window_resize(W.egl_window, W.cur_w, W.cur_h, 0, 0);
    render_resize(&rend, W.cur_w, W.cur_h);
    layout_apply();
    W.dirty = 1;
    W.resized = 1;      /* cleared by redraw() once a buffer of THIS size has been attached */
    /* Do not arm a frame callback or swap here: with no buffer committed the callback would never be
       delivered and every draw path would stay blocked. The frame path answers this configure. */
    return 1;
}

/* ---------------- multiplexing: the sessions of this window ---------------- */

/* The tab state in the window title: the renderer draws cells, not chrome, so the title is the only
   place a user sees which session they are in, how many there are, and whether a hidden one has output. */
static void update_title(void)
{
    if (!W.toplevel)
        return;
    char ttl[128];
    mux_title(&mux, ttl, sizeof ttl);
    xdg_toplevel_set_title(W.toplevel, ttl);
}

/* Fold the ASCII letters to lowercase: a binding names the KEY, and Shift is a modifier of its own, so
   "Ctrl+Shift+T" is written with a T but the press arrives as 'T'. config.c folds it the same way. */
static xkb_keysym_t norm_sym(xkb_keysym_t s)
{
    if (s >= XKB_KEY_A && s <= XKB_KEY_Z)
        return s - XKB_KEY_A + XKB_KEY_a;
    return s;
}

/* Does this press match a binding? The modifier set must match EXACTLY (reduced to the four KB_*
   bits), so Ctrl+Shift+T does not fire for Ctrl+Alt+Shift+T, and Caps/Num lock never count. */
static int bind_match(const KeyBind *kb, xkb_keysym_t sym, unsigned mods)
{
    if (!kb->set || kb->mods != mods)
        return 0;
    return kb->sym == norm_sym(sym);
}

/* Which session owns this pty master fd? epoll reports only the fd. Linear scan: at most max_tabs
   entries (TAB_MAX is 64), and this runs once per readable fd, not per byte read. */
static int tab_for_fd(int fd)
{
    if (fd < 0)
        return -1;
    for (int i = 0; i < mux_count(&mux); i++)
        if (mux.tab[i].pty.mfd == fd)
            return i;
    return -1;
}

static int tab_for_child(pid_t pid)
{
    if (pid <= 0)
        return -1;
    for (int i = 0; i < mux_count(&mux); i++)
        if (mux.tab[i].pty.child == pid)
            return i;
    return -1;
}

/* Start a session's shell and put its master in epoll. With shell_integration on a recognised shell is
   started through shellint_argv(); everything else is spawned exactly as before. */
static int tab_spawn(Tab *tb)
{
    /* The cell size goes into the Pty BEFORE the spawn: pty_spawn's first TIOCSWINSZ is the geometry the child
       sees at startup, and a tool that asks for pixel geometry immediately (`kai-term -e 'timg foo.png'`)
       would otherwise fall back to text blocks. pty_set_cells() is unusable here — no master fd yet. */
    tb->pty.cell_w = font.cell_w > 0 ? font.cell_w : 0;
    tb->pty.cell_h = font.cell_h > 0 ? font.cell_h : 0;

    const char *cmd = g_cmd;
    tb->int_dead = 0;
    tb->int_gen = 0;          /* the fresh Term has seen no announcement either */
    if (conf.shell_integration && g_intdir[0] && !(opt_cmd && opt_cmd[0])) {
        /* `-e COMMAND` is not a shell we can integrate with: it is whatever the user asked to run, and
           wrapping it in a shell that loads our script would change what they asked for. */
        const char *sh = conf.shell[0] ? conf.shell : getenv("SHELL");
        int kind = shellint_kind_for(sh);
        if (kind != SHELLINT_NONE) {
            char line[FM_PATH_MAX * 2 + 256];
            int id = g_int_seq++;
            shellint_request_path(g_intdir, id, tb->cd_req, sizeof tb->cd_req);
            if (shellint_argv(kind, g_intdir, tb->cd_req, line, sizeof line) == 0) {
                cmd = line;      /* lives on the stack until pty_spawn() is done with it (the child gets a
                                    copy across fork, and the parent only reads it before exec) */
                klog(L_INFO, "shell integration: session %d runs %s with the Kai-Term script (request file %s)",
                     id, shellint_name(kind), tb->cd_req);
            } else {
                tb->cd_req[0] = 0;
                klog(L_WARN, "shell integration: no command line for %s — this session keeps the typed path",
                     shellint_name(kind));
            }
        }
    }
    if (pty_spawn(&tb->pty, cmd, tb->term.g.rows, tb->term.g.cols, g_wdir) < 0)
        return -1;
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = tb->pty.mfd };
    if (g_ep >= 0 && epoll_ctl(g_ep, EPOLL_CTL_ADD, tb->pty.mfd, &ev) != 0) {
        klog(L_WARN, "mux: cannot watch the new tab's pty: %s", strerror(errno));
        close(tb->pty.mfd);
        tb->pty.mfd = -1;
        if (tb->pty.child > 0)
            kill(tb->pty.child, SIGHUP);
        tb->pty.child = 0;   /* the SIGCHLD path reaps it; do not kill a recycled pid */
        return -1;
    }
    return 0;
}

/* Open another session in this window: the same command and startup directory as the first, and the grid
   size this layout gives every session (layout_grid_size), so the new pane needs no resize. */
static void tab_open(void)
{
    int cols, rows;
    layout_grid_size(&g_layout, &cols, &rows);
    if (cols < 1 || rows < 1) {          /* no layout yet (startup): fall back to the configured grid */
        Tab *ref = mux_cur(&mux);
        cols = ref ? ref->term.g.cols : conf.cols;
        rows = ref ? ref->term.g.rows : conf.rows;
    }
    int idx = mux_open(&mux, rows, cols, conf.scrollback);
    if (idx < 0)
        return;                       /* mux_open already logged the max_tabs refusal */
    Tab *tb = &mux.tab[idx];
    layout_apply();                   /* the bar gains an item, and every session gets its pane's grid */
    if (tab_spawn(tb) < 0) {
        mux_close(&mux, idx);
        layout_apply();
        update_title();
        W.dirty = 1;
        return;
    }
    klog(L_INFO, "mux: tab %d/%d opened (pid %d, pty fd %d, grid %dx%d)",
         idx + 1, mux_count(&mux), (int)tb->pty.child, tb->pty.mfd, tb->term.g.cols,
         tb->term.g.rows);
    update_title();
    W.dirty = 1;
}

/* ---------------- confirm-quit: asking before a close throws work away ----------------
   The gate asks pty_fg_busy(): a job backgrounded with `&` leaves the shell in the foreground group,
   so it reads idle. This protects a foreground command, not a daemon. */

/* Take the question down. layout_apply() runs because the row costs a grid row: without it the panes stay
   one row short after the question is gone. */
static void confirm_clear(void)
{
    if (confirm_kind == CONFIRM_NONE)
        return;
    confirm_kind = CONFIRM_NONE;
    confirm_tab = -1;
    confirm_text[0] = 0;
    layout_apply();
    W.dirty = 1;
}

/* Put the question up. 1 = it is on screen, so the caller must not close; 0 = it does not fit and the
   caller must close rather than swallow the request. layout_compute() owns that decision. */
static int confirm_show(int what, int tabidx)
{
    int idx[TAB_MAX];
    char namebuf[TAB_MAX][PROC_NAME_MAX];
    const char *names[TAB_MAX];
    int n = 0;
    if (what == CONFIRM_CLOSE_TAB) {
        if (tabidx >= 0 && tabidx < mux_count(&mux) && pty_fg_busy(&mux.tab[tabidx].pty)) {
            idx[0] = tabidx;
            n = 1;
        }
    } else {
        n = mux_running(&mux, idx, TAB_MAX);
        for (int i = 0; i < n; i++) {
            names[i] = pty_fg_name(&mux.tab[idx[i]].pty, namebuf[i], sizeof namebuf[i])
                     ? namebuf[i] : NULL;
        }
    }
    confirm_kind = what;
    confirm_tab = (what == CONFIRM_CLOSE_TAB) ? tabidx : -1;
    if (what == CONFIRM_CLOSE_TAB)
        confirm_message(what, n, n ? idx : NULL, confirm_text, sizeof confirm_text);
    else
        confirm_warning(n, n ? idx : NULL, n ? names : NULL, confirm_text, sizeof confirm_text);
    layout_apply();          /* the row costs a grid row, so every session's grid changes with it */
    W.dirty = 1;
    int visible = (what == CONFIRM_CLOSE_TAB) ? g_layout.notice_on : g_layout.dialog.on;
    if (!visible) {
        confirm_kind = CONFIRM_NONE;
        confirm_tab = -1;
        confirm_text[0] = 0;
        layout_apply();
        klog(L_WARN, "confirm: the question does not fit in this window — closing without asking");
        return 0;
    }
    klog(L_INFO, "confirm: asking before %s (%d session(s) running): %s",
         what == CONFIRM_CLOSE_TAB ? "closing a tab" : "quitting", n, confirm_text);
    return 1;
}

/* 1 = the question is now up and the caller must NOT close. `never` never asks; `running` (the default)
   asks only when something is in a foreground group; `always` asks at every close. A second close request
   while a question is already up is ignored rather than stacked: the row on screen is already the answer. */
static int confirm_gate_window(void)
{
    if (confirm_kind != CONFIRM_NONE) {
        klog(L_DEBUG, "confirm: a close was requested while the question is up — ignored");
        return 1;
    }
    if (conf.confirm_quit == CONFIRM_NEVER)
        return 0;
    int n = mux_running(&mux, NULL, 0);
    if (conf.confirm_quit == CONFIRM_RUNNING && n == 0) {
        klog(L_DEBUG, "confirm: nothing is running in any session — closing without asking");
        return 0;
    }
    return confirm_show(CONFIRM_QUIT, -1);
}

static int confirm_gate_tab(int idx)
{
    if (confirm_kind != CONFIRM_NONE) {
        klog(L_DEBUG, "confirm: a tab close was requested while the question is up — ignored");
        return 1;
    }
    if (idx < 0 || idx >= mux_count(&mux))
        return 0;                       /* tab_close() logs the bounds case itself */
    if (conf.confirm_quit == CONFIRM_NEVER)
        return 0;
    if (conf.confirm_quit == CONFIRM_RUNNING && !pty_fg_busy(&mux.tab[idx].pty)) {
        klog(L_DEBUG, "confirm: session %d has nothing running — closing without asking", idx + 1);
        return 0;
    }
    return confirm_show(CONFIRM_CLOSE_TAB, idx);
}

/* Close one session. The Pty is captured BEFORE mux_close(): the tab after it slides into this slot.
   Closing the master hangs up the shell, the explicit SIGHUP covers one that survives, and the SIGCHLD
   path reaps the child. */
static void tab_close(int idx)
{
    if (idx < 0 || idx >= mux_count(&mux))
        return;
    /* mux_close() shifts every tab after idx down, so a follow belongs to no session once idx or any LOWER
       index closes: fm_cd_tab would keep its old number and name a different shell — the machine would
       erase that shell's line, `cd` it and type the other session's text into it. */
    if (fm_cd_tab >= 0 && idx <= fm_cd_tab)
        fm_cd_abort("a session closed");
    /* A question about a TAB belongs to that index, and mux_close() shifts every later tab down: if the
       session it asked about already exited, answering YES would close a different shell. */
    if (confirm_kind == CONFIRM_CLOSE_TAB && idx <= confirm_tab)
        confirm_clear();
    Pty p = mux.tab[idx].pty;
    if (g_ep >= 0 && p.mfd >= 0)
        epoll_ctl(g_ep, EPOLL_CTL_DEL, p.mfd, NULL);
    if (p.mfd >= 0)
        close(p.mfd);
    if (p.child > 0)
        kill(p.child, SIGHUP);
    int act = mux_close(&mux, idx);
    klog(L_INFO, "mux: tab %d closed, %d left (pid %d)", idx + 1, mux_count(&mux), (int)p.child);
    if (act < 0) {
        klog(L_INFO, "mux: the last session is gone, exiting");
        W.running = 0;
        return;
    }
    layout_apply();      /* one bar item fewer, and the remaining panes get the whole width back */
    update_title();
    W.dirty = 1;
}

/* The answer, logged either way: "asked and nothing was running" is a real outcome, and a user asked for no
   visible reason needs one line to point at. YES for a window closes every session named in it. */
static void confirm_answer(int yes)
{
    int what = confirm_kind, tab = confirm_tab;
    if (what == CONFIRM_NONE)
        return;
    klog(L_INFO, "confirm: %s — %s", yes ? "YES" : "NO",
         yes ? (what == CONFIRM_CLOSE_TAB ? "closing that session" : "quitting, every session with it")
             : "nothing is closed");
    if (what == CONFIRM_CLOSE_TAB) {
        confirm_clear();
        if (yes)
            tab_close(tab);
    } else {
        confirm_clear();
        if (yes)
            W.running = 0;
    }
}

/* Show another session. Only what is drawn and what the keyboard talks to changes: every shell keeps
   running, and each tab keeps its own grid, scroll position, selection and alt screen. */
static void tab_switch(int idx)
{
    if (idx < 0 || idx >= mux_count(&mux))
        return;
    /* mux_goto() IS the switch. The active index is the only state that says which session the keyboard talks to,
       which pane the focus border is drawn around and which pane's origin the pointer is mapped through, so
       a function called "switch" that only re-applies the layout switches nothing. */
    int act = mux_goto(&mux, idx);
    W.selecting = 0;      /* a drag belongs to the tab it started in */
    /* The newly visible session's shell may be anywhere. Forgetting where the file manager last sent a
       `cd` means the next navigation re-states the directory to this shell instead of silently doing
       nothing because some OTHER session was already told. */
    fm_last_cd[0] = 0;
    /* A follow in flight belongs to the session that started it. Its captured text must not be typed into
       the newly visible shell, and its `cd` must not be sent there either. */
    fm_cd_abort("the visible session changed");
    klog(L_INFO, "mux: tab %d/%d visible", act + 1, mux_count(&mux));
    /* Which session is visible is part of the LAYOUT: it is the item the bar highlights, the pane the
       focus border is drawn around, the session the pane window slides to in split view, and the one whose
       pane origin the pointer is mapped through (W.gx/W.gy). */
    layout_apply();
    update_title();
    W.dirty = 1;
}

/* Flip between "one session fills the window" (tab view) and "the sessions sit side by side" (split
   view). Nothing else changes — the same sessions, shells, scrollback and selection. */
static void layout_toggle(void)
{
    layout_mode = (layout_mode == LAYOUT_SPLIT) ? LAYOUT_TABS : LAYOUT_SPLIT;
    layout_apply();
    W.dirty = 1;
}

/* ---------------- the in-window file manager: the wiring ---------------- */

/* The worker thread runs exactly two functions, which touch nothing but malloc, the filesystem and their own
   argument, so it never reads or writes FileMgr. The main thread keeps ownership of the model and installs
   every answer, which is why the request-id rule in fm_install_*() is the only synchronisation needed. */
static void *fm_worker(void *unused)
{
    (void)unused;
    for (;;) {
        int kind = -1;
        char path[FM_PATH_MAX];
        unsigned long seq = 0;

        pthread_mutex_lock(&fm_mtx);
        while (!fm_quit) {
            for (int i = 0; i < FMQ_KINDS; i++)
                if (fm_req[i].used) {
                    kind = i;
                    break;
                }
            if (kind >= 0)
                break;
            pthread_cond_wait(&fm_cv, &fm_mtx);
        }
        if (kind >= 0) {
            snprintf(path, sizeof path, "%s", fm_req[kind].path);
            seq = fm_req[kind].seq;
            fm_req[kind].used = 0;
        }
        pthread_mutex_unlock(&fm_mtx);
        if (kind < 0)
            return NULL;              /* fm_quit: the main thread is shutting down */

        FmResult res;
        res.kind = kind;
        res.seq = seq;
        res.ptr = 0;
        if (kind == FMQ_SCAN)
            res.ptr = (unsigned long)(uintptr_t)fm_scan_build(path, seq);
        else
            res.ptr = (unsigned long)(uintptr_t)fm_preview_build(path, seq);
        if (!res.ptr)
            continue;                 /* the builder already logged why it has nothing */

        ssize_t w;
        do {
            w = write(fm_pipe[1], &res, sizeof res);
        } while (w < 0 && errno == EINTR);
        if (w != (ssize_t)sizeof res) {
            /* nowhere to hand the answer to: free it here rather than leak it */
            if (kind == FMQ_SCAN)
                fm_scan_free((FmScan *)(uintptr_t)res.ptr);
            else
                fm_preview_free((FmPreview *)(uintptr_t)res.ptr);
        }
    }
}

/* Start the worker the first time the file manager is opened — never before, so a session that does not
   use the feature creates no thread and no descriptor. */
static int fm_worker_start(void)
{
    if (fm_worker_up)
        return 1;
    if (pipe(fm_pipe) < 0) {
        klog(L_WARN, "fm: pipe: %s — the file manager cannot load anything", strerror(errno));
        fm_pipe[0] = fm_pipe[1] = -1;
        return 0;
    }
    fcntl(fm_pipe[0], F_SETFL, O_NONBLOCK);
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = fm_pipe[0] };
    if (g_ep >= 0 && epoll_ctl(g_ep, EPOLL_CTL_ADD, fm_pipe[0], &ev) != 0) {
        klog(L_WARN, "fm: cannot watch the worker's result pipe: %s", strerror(errno));
        close(fm_pipe[0]);
        close(fm_pipe[1]);
        fm_pipe[0] = fm_pipe[1] = -1;
        return 0;
    }
    if (pthread_create(&fm_thr, NULL, fm_worker, NULL) != 0) {
        klog(L_WARN, "fm: pthread_create failed — the file manager cannot load anything");
        if (g_ep >= 0)
            epoll_ctl(g_ep, EPOLL_CTL_DEL, fm_pipe[0], NULL);
        close(fm_pipe[0]);
        close(fm_pipe[1]);
        fm_pipe[0] = fm_pipe[1] = -1;
        return 0;
    }
    fm_worker_up = 1;
    klog(L_DEBUG, "fm: worker started (result fd %d)", fm_pipe[0]);
    return 1;
}

/* Hand the worker a request. The path is copied into the slot: the worker must never read FileMgr, and
   the path may be a stack buffer of the caller. */
static int fm_ask(int kind, const char *path, unsigned long seq)
{
    if (!fm_worker_up || fm_pipe[1] < 0)
        return 0;
    pthread_mutex_lock(&fm_mtx);
    snprintf(fm_req[kind].path, sizeof fm_req[kind].path, "%s", path);
    fm_req[kind].seq = seq;
    fm_req[kind].used = 1;
    pthread_cond_signal(&fm_cv);
    pthread_mutex_unlock(&fm_mtx);
    return 1;
}

/* Ask for whatever the model says should be previewed now. fm_preview_request() compares it with what is
   installed, so moving the selection onto the entry already previewed costs no disk access. */
static void fm_ask_preview(void)
{
    if (!g_fm.preview_on)
        return;
    unsigned long seq = 0;
    char path[FM_PATH_MAX];
    if (fm_preview_request(&g_fm, &seq, path, sizeof path))
        fm_ask(FMQ_PREVIEW, path, seq);
}

/* Take every answer the worker has written. Runs from the event loop when the read end is readable, and
   once more at shutdown to free anything that arrived after the last drain. */
static void fm_drain_results(void)
{
    if (fm_pipe[0] < 0)
        return;
    for (;;) {
        FmResult res;
        ssize_t r = read(fm_pipe[0], &res, sizeof res);
        if (r == (ssize_t)sizeof res) {
            if (res.kind == FMQ_SCAN) {
                /* fm_install_scan frees the answer either way: it takes ownership, and drops it (after
                   freeing) when it answers a request the user already replaced. */
                if (fm_install_scan(&g_fm, (FmScan *)(uintptr_t)res.ptr))
                    fm_ask_preview();   /* the selection may have moved when the listing landed */
            } else {
                /* A new preview replaces every cell, so a drag's range now names another file's text. This
                   flag must be cleared with the model's selection, or the next motion re-creates it from
                   the old anchor. */
                if (fm_install_preview(&g_fm, (FmPreview *)(uintptr_t)res.ptr))
                    W.pv_selecting = 0;
            }
            W.dirty = 1;
            continue;
        }
        if (r < 0 && errno == EINTR)
            continue;
        break;                        /* EAGAIN: everything queued has been taken */
    }
}

/* Quote one path for the shell: single quotes, with an embedded quote written the shell's way as '\''. A
   quoted path cannot glob or be read as a leading-dash option. Returns the bytes written. */
static size_t shell_quote(char *out, size_t cap, const char *path)
{
    if (cap < 3)
        return 0;
    size_t n = 0;
    out[n++] = '\'';
    for (const char *s = path; *s && n + 5 < cap; s++) {
        if (*s == '\'') {
            out[n++] = '\''; out[n++] = '\\'; out[n++] = '\''; out[n++] = '\'';
        } else {
            out[n++] = *s;
        }
    }
    out[n++] = '\'';
    out[n] = 0;
    return n;
}

/* Hand the shell the directory the browser moved into, so the prompt lands where the user is looking.
   Never sent to a session on the alternate screen: vim, htop and less would receive it as typing. */

/* The cursor's row in the grid's document numbering (slot 0 = row_base, cursor slot = live_off + cy). A
   viewport row says nothing across a scroll: the grid rotates under it. */
static int term_docrow(Term *t)
{
    return t->g.row_base + t->g.live_off + t->cy;
}

/* Shared by the fast path and the deferred machine so the quoting and the log line cannot drift apart.
   Moves fm_last_cd, so a `cd` is never sent for a directory the shell is already in. */
static void fm_cd_write(Term *t, Pty *p, const char *dir)
{
    char cmd[FM_PATH_MAX + 16];
    size_t n = 0;
    cmd[n++] = 'c'; cmd[n++] = 'd'; cmd[n++] = ' ';
    n += shell_quote(cmd + n, sizeof cmd - n, dir);
    cmd[n++] = '\r';
    cmd[n] = 0;
    input_write(t, p, cmd, n);
    snprintf(fm_last_cd, sizeof fm_last_cd, "%s", dir);
    fm_cd_docrow = term_docrow(t);      /* the row the command was typed on: the prompt must be on another */
    /* Logged AFTER the write, and naming the directory the model actually holds rather than the one some
       caller asked for: a line that printed its own argument could claim a cd the alt-screen gate (or a
       dead pty) refused. This is the only client-side evidence the follow-the-directory rule has. */
    klog(L_DEBUG, "fm: told the shell to follow: cd %s", dir);
}

/* Ask the shell to change its OWN directory, leaving the user's line alone: the path travels in the request
   file, so the sequence carries no data a stray byte could run. 1 = sent, 0 = typed path. */
static int fm_int_try(Tab *tb, Term *t, Pty *p, const char *dir)
{
    if (!conf.shell_integration || !g_intdir[0] || !tb || !tb->cd_req[0])
        return 0;
    /* A shell that announces is a shell that listens, and the user can put a different one in front of us at any
       moment: a nested shell announces when it starts and the outer one announces again when it exits.
       Either way Term.shell_int_gen moves, which REVIVES an integration this session had given up on. */
    if (t->shell_int_gen != tb->int_gen) {
        if (tb->int_dead)
            klog(L_DEBUG, "fm: a shell announced again — the integration is used for this session again");
        tb->int_dead = 0;
        tb->int_gen = t->shell_int_gen;
    }
    /* Two ways integration is dead: the shell never announced, or it announced and stopped answering — what
       a nested shell looks like. The second is remembered; the sequence is inert, so being wrong costs a
       delay, not damage. */
    if (t->shell_int == SHELLINT_NONE || tb->int_dead)
        return 0;
    size_t len = 0;
    const char *seq = shellint_seq(t->shell_int, &len);
    if (!seq || !len)
        return 0;
    if (shellint_write_request(tb->cd_req, dir) != 0) {
        tb->int_dead = 1;          /* the channel itself is broken; stop using it for this session */
        klog(L_WARN, "fm: shell integration could not be asked for %s — using the typed path", dir);
        return 0;
    }
    /* Not input_write()/term_note_input(): those open the echo window, for bytes that may echo on the
       user's line. These are never echoed — the ack is the evidence here. */
    term_shell_cd_ack_clear(t);
    pty_write(p, seq, len);
    fm_cd_state = FMCD_INTEGRATION;
    fm_cd_tab = mux_active(&mux);
    snprintf(fm_cd_dir, sizeof fm_cd_dir, "%s", dir);
    fm_cd_line[0] = 0;                  /* nothing was captured — there is nothing to put back */
    fm_cd_docrow = -1;
    fm_cd_deadline = mono_ms() + FM_INT_ACK_MS;
    klog(L_DEBUG, "fm: shell integration (%s) asked to cd into %s", shellint_name(t->shell_int), dir);
    return 1;
}

static void fm_send_cd(const char *dir)
{
    Term *t = cur_term();
    Pty *p = cur_pty();
    if (!t || !p || t->alt_screen || !conf.fm_follow)
        return;
    if (!dir[0])
        return;
    /* A sequence is already in flight for this session. Do NOT abort it — the erase may already have
       happened, and dropping it then would throw the user's text away. Re-target it instead. */
    if (fm_cd_state != FMCD_IDLE) {
        if (fm_cd_tab == mux_active(&mux)) {
            snprintf(fm_cd_dir, sizeof fm_cd_dir, "%s", dir);
            fm_cd_deadline = mono_ms() + FMCD_TIMEOUT_MS;
            klog(L_DEBUG, "fm: the follow already in flight now targets %s", dir);
        }
        return;
    }
    if (!strcmp(fm_last_cd, dir))
        return;
    /* Integration first: an announced shell is asked to move, so the user's half-typed line is never
       captured, erased or re-typed. Everything else falls through to the typed machine. */
    if (fm_int_try(mux_cur(&mux), t, p, dir))
        return;
    /* Fast path: the line is provably empty, so nothing has to be erased first. */
    if (term_at_prompt(t)) {
        fm_cd_write(t, p, dir);
        return;
    }
    /* Capture the typed text. term_prompt_line_typed() ignores the shell's ghost text right of the cursor;
       the next step proves the WHOLE row equals the remembered prompt, so a cursor parked mid-line fails
       it. */
    if (!term_prompt_line_typed(t, fm_cd_line, sizeof fm_cd_line)) {
        fm_cd_line[0] = 0;
        klog(L_DEBUG, "fm: not telling the shell to follow — its input line is not one I can describe");
        return;
    }
    snprintf(fm_cd_dir, sizeof fm_cd_dir, "%s", dir);
    fm_cd_docrow = -1;                  /* no `cd` sent yet, so no row requirement yet */
    fm_cd_tab = mux_active(&mux);
    fm_cd_state = FMCD_ERASING;
    fm_cd_deadline = mono_ms() + FMCD_TIMEOUT_MS;
    /* Ctrl+U erases to the start of the line in fish, readline and zsh's emacs mode. It needs no character
       count, which backspace-per-cell would: a tab and a wide glyph each cover several cells. */
    input_write(t, p, "\025", 1);
    klog(L_DEBUG, "fm: clearing the shell's input line to follow the browser into %s", dir);
}

/* Stop the sequence and leave the terminal exactly as it is. Never retype captured text from here: text
   typed into a line we cannot describe is precisely the mangling this machine exists to avoid. */
static void fm_cd_abort(const char *why)
{
    if (fm_cd_state == FMCD_IDLE)
        return;
    /* If the captured text never made it back onto the line, say so and print it — losing half of what the
       user typed is the worst failure this feature has, and the log is the only way to get it back. When
       the line still holds it, the erase never took effect and nothing was lost. */
    int lost = (fm_cd_state == FMCD_ERASING || fm_cd_state == FMCD_CDING) && fm_cd_line[0];
    if (lost && fm_cd_tab >= 0 && fm_cd_tab < mux_count(&mux)) {
        char now[FM_LINE_MAX];
        if (term_prompt_line(&mux.tab[fm_cd_tab].term, now, sizeof now) == 1 && !strcmp(now, fm_cd_line))
            lost = 0;
    }
    if (lost)
        klog(L_WARN, "fm: dropped the follow while %s (%s) — the shell's line does NOT hold what you had "
                     "typed; it was: %s", fm_cd_state_name[fm_cd_state], why, fm_cd_line);
    else
        klog(L_DEBUG, "fm: dropped the follow while %s (%s)", fm_cd_state_name[fm_cd_state], why);
    fm_cd_state = FMCD_IDLE;
    fm_cd_tab = -1;
    fm_cd_line[0] = 0;
    fm_cd_dir[0] = 0;
    fm_cd_docrow = -1;
}

/* The integrated shell never confirmed the directory, so this session stops being treated as integrated (a
   nested shell has the environment but not the binding) and falls back to the typed machine. */
static void fm_int_failed(void)
{
    int ti = fm_cd_tab;
    char dir[FM_PATH_MAX];
    snprintf(dir, sizeof dir, "%s", fm_cd_dir);
    const char *kind = "the shell";
    if (ti >= 0 && ti < mux_count(&mux)) {
        kind = shellint_name(mux.tab[ti].term.shell_int);
        mux.tab[ti].int_dead = 1;
    }
    klog(L_DEBUG, "fm: no integration ack from %s — using the typed path", kind);
    fm_cd_state = FMCD_IDLE;
    fm_cd_tab = -1;
    fm_cd_dir[0] = 0;
    fm_send_cd(dir);
}

/* Advance the sequence after one burst of that session's output: the only evidence an erase or a `cd`
   landed is what the shell printed next. Only real output (burst=1) restarts CDING's quiet period. */
static void fm_cd_advance(int ti, int burst)
{
    if (fm_cd_state == FMCD_IDLE)
        return;
    if (ti != fm_cd_tab || ti < 0 || ti >= mux_count(&mux))
        return;
    Term *t = &mux.tab[ti].term;
    Pty *p = &mux.tab[ti].pty;
    if (burst)
        fm_cd_last_burst = mono_ms();

    if (mono_ms() >= fm_cd_deadline) {
        if (fm_cd_state == FMCD_INTEGRATION)
            fm_int_failed();
        else
            fm_cd_abort("the wait ran out");
        return;
    }
    if (t->alt_screen) {
        fm_cd_abort("the session entered the alternate screen");
        return;
    }
    switch (fm_cd_state) {
    case FMCD_INTEGRATION:
        /* The shell's own ack naming the directory it entered is the ONLY evidence: a prompt redraw proves
           nothing and the request produced no echo. The deadline turns silence into the typed path. */
        if (!t->shell_cd_ack[0] || strcmp(t->shell_cd_ack, fm_cd_dir))
            return;
        snprintf(fm_last_cd, sizeof fm_last_cd, "%s", fm_cd_dir);
        fm_cd_state = FMCD_IDLE;
        fm_cd_tab = -1;
        fm_cd_dir[0] = 0;
        klog(L_DEBUG, "fm: the shell's integration changed directory to %s — the line was not touched",
             fm_last_cd);
        return;
    case FMCD_ERASING:
        /* term_at_prompt() here is not a convenience: it is the PROOF that the line is empty again — the
           row matches the remembered prompt and no bytes of ours are outstanding. Until then the `cd`
           is not sent, whatever the line editor did with Ctrl+U. */
        if (!term_at_prompt(t))
            return;
        fm_cd_state = FMCD_CDING;
        fm_cd_deadline = mono_ms() + FMCD_TIMEOUT_MS;
        fm_cd_write(t, p, fm_cd_dir);
        return;
    case FMCD_CDING:
        if (!term_at_prompt(t))
            return;                       /* the new prompt has not been drawn yet */
        if (fm_cd_docrow >= 0 && term_docrow(t) == fm_cd_docrow)
            /* Still the row the `cd` was typed on. Our own command ended with CR, so no echo window was
               left and its echo re-took the prompt snapshot — term_at_prompt() is true here on a row that
               is not a prompt. The shell's next prompt sits on a later DOCUMENT row. */
            return;
        /* Off the `cd` row is necessary but not sufficient — see FMCD_QUIET_MS. Wait for the session to go
           quiet, which is the evidence that the shell has finished drawing its prompt. */
        if (mono_ms() - fm_cd_last_burst < FMCD_QUIET_MS)
            return;
        if (strcmp(fm_last_cd, fm_cd_dir)) {
            /* The browser moved again while that `cd` was in flight, or back where the shell already was,
               and the line is provably empty. State the newest directory before typing anything back, or a
               second pass repeats the line. */
            fm_cd_deadline = mono_ms() + FMCD_TIMEOUT_MS;
            fm_cd_write(t, p, fm_cd_dir);
            return;
        }
        if (!fm_cd_line[0]) {             /* nothing had been typed: there is nothing to put back */
            fm_cd_state = FMCD_IDLE;
            fm_cd_tab = -1;
            klog(L_DEBUG, "fm: the shell followed the browser (the line was empty, nothing to restore)");
            return;
        }
        fm_cd_state = FMCD_RETYPING;
        fm_cd_deadline = mono_ms() + FMCD_TIMEOUT_MS;
        input_write(t, p, fm_cd_line, strlen(fm_cd_line));   /* no CR: nothing is ever run here */
        klog(L_DEBUG, "fm: re-typing what was on the line: %s", fm_cd_line);
        return;
    case FMCD_RETYPING:
        /* NOT term_prompt_line(): that demands the cells right of the cursor equal the snapshot, which
           fish's ghost text breaks. term_prompt_line_is() asks what this state needs — the prompt plus
           exactly fm_cd_line, ending at the cursor. */
        if (!term_prompt_line_is(t, fm_cd_line))
            return;                       /* the echo has not landed (or differs): keep waiting */
        fm_cd_state = FMCD_IDLE;
        fm_cd_tab = -1;
        klog(L_DEBUG, "fm: the prompt now holds the browser's directory and what you had typed");
        fm_cd_line[0] = 0;
        /* A navigation that arrived during the re-type could not send its `cd` then — the line already
           held text again. Run the sequence once more now that the line is describable. */
        if (strcmp(fm_last_cd, fm_cd_dir))
            fm_send_cd(fm_cd_dir);
        return;
    default:
        fm_cd_abort("an impossible state");
        return;
    }
}

/* Checked from the frame timer: a shell that never answers produces no output to advance on, and the END
   of output (FMCD_QUIET_MS) is CDING's evidence — no burst is coming to trigger it. */
static void fm_cd_tick(void)
{
    if (fm_cd_state == FMCD_IDLE)
        return;
    if (mono_ms() >= fm_cd_deadline) {
        if (fm_cd_state == FMCD_INTEGRATION)
            fm_int_failed();
        else
            fm_cd_abort("the wait ran out");
        return;
    }
    if (fm_cd_state == FMCD_CDING && mono_ms() - fm_cd_last_burst >= FMCD_QUIET_MS)
        fm_cd_advance(fm_cd_tab, 0);
}


/* The model changed: re-apply the geometry (layout_apply() is what calls fm_set_size() with the band's
   rects) and ask for a redraw. The grids themselves are painted in redraw(), just before the frame. */
static void fm_touch(void)
{
    layout_apply();
    W.dirty = 1;
}

static void fm_open(void)
{
    if (g_fm.on)
        return;
    if (!fm_worker_start())
        return;               /* the reason is already in the log; an empty band would only confuse */
    g_fm.on = 1;
    g_fm.focus = FM_FOCUS_LIST;
    unsigned long seq = 0;
    fm_refresh(&g_fm, &seq);  /* opening always re-reads: files may have appeared while it was closed */
    fm_ask(FMQ_SCAN, g_fm.dir, seq);
    fm_ask_preview();
    fm_touch();
    klog(L_DEBUG, "fm: opened on %s", g_fm.dir);
}

/* Closing leaves the terminal as it was: the band's rects go to 0x0 and the model keeps its listing. The
   worker stays alive but idle — killing it would race with an answer already in the pipe. */
static void fm_close(void)
{
    if (!g_fm.on)
        return;
    g_fm.on = 0;
    g_fm.focus = FM_FOCUS_TERM;
    /* A preview-pane text selection dies with the pane it names: the band repaints from scratch when it
       reopens, the listing may have changed while it was hidden, and a drag that was still in progress
       would otherwise keep extending a range in a pane that is no longer drawn. */
    fm_preview_select_clear(&g_fm);
    W.pv_selecting = 0;
    fm_touch();
    klog(L_DEBUG, "fm: closed — the terminal is the whole window again");
}

static void fm_toggle(void)
{
    if (g_fm.on)
        fm_close();
    else
        fm_open();
}

/* Which region the keyboard drives. The border render.c draws is what makes a switch visible, so a
   switch that changes nothing on screen is not allowed — focusing a toggled-away preview pane does
   nothing. */
static void fm_focus_set(int f)
{
    if (!g_fm.on)
        return;
    if (f == FM_FOCUS_PREVIEW && !g_fm.preview_on)
        return;
    if (g_fm.focus == f)
        return;
    g_fm.focus = f;
    W.dirty = 1;
    klog(L_DEBUG, "fm: keyboard -> %s",
         f == FM_FOCUS_TERM ? "terminal" : (f == FM_FOCUS_PREVIEW ? "preview" : "browser"));
}

/* One navigation: ask the worker for the listing, tell the shell, re-ask for the preview, re-apply the
   geometry. `seq` MUST already hold the id the model issued — `fm_nav(fm_go_sel(fm, &seq), seq, ...)`
   compiles, warns about nothing and is broken: C evaluates arguments in unspecified order. */
static void fm_nav(int changed, unsigned long seq, const char *what)
{
    if (!changed)
        return;
    fm_ask(FMQ_SCAN, g_fm.dir, seq);
    fm_send_cd(g_fm.dir);
    fm_ask_preview();
    fm_touch();
    klog(L_DEBUG, "fm: %s -> %s", what, g_fm.dir);
}

static void fm_reread(void)
{
    unsigned long seq = 0;
    int changed = fm_refresh(&g_fm, &seq);   /* fills seq — see fm_nav */
    fm_nav(changed, seq, "refresh");
}

static void fm_preview_toggle(void)
{
    g_fm.preview_on = g_fm.preview_on ? 0 : 1;
    if (!g_fm.preview_on && g_fm.focus == FM_FOCUS_PREVIEW)
        g_fm.focus = FM_FOCUS_LIST;
    fm_touch();          /* the half-split changes: the browser takes the whole band back */
    fm_ask_preview();
}

/* Types the selected entry's path and NOTHING else — no newline, so Kai-Term never runs a file it was
   only asked to name. The gate is the NON-STRICT predicate: this inserts and erases nothing. */
static void fm_send_path(void)
{
    Term *t = cur_term();
    Pty *p = cur_pty();
    if (!t || !p)
        return;
    if (fm_cd_state != FMCD_IDLE) {
        klog(L_DEBUG, "fm: not typing the path — the shell's line is being moved to the browser's "
                      "directory");
        return;
    }
    char probe[FM_LINE_MAX];
    if (!term_prompt_line_typed(t, probe, sizeof probe)) {
        klog(L_DEBUG, "fm: not typing the path — the input line is not one I can describe");
        return;
    }
    char path[FM_PATH_MAX];
    if (!fm_entry_path(&g_fm, fm_sel(&g_fm), path, sizeof path))
        return;
    char cmd[FM_PATH_MAX + 8];
    size_t n = shell_quote(cmd, sizeof cmd, path);
    if (!n)
        return;
    input_write(t, p, cmd, n);
    klog(L_DEBUG, "fm: typed the selected path at the prompt: %s", path);
}

/* The selection moved (key, click or wheel): the preview has to follow it. fm_select() already marked
   the model dirty, so the frame path repaints; only the preview request is issued here. */
static void fm_sel_changed(void)
{
    fm_ask_preview();
    W.dirty = 1;
}

/* Open the selected entry: descend into a directory, and on anything else do NOTHING — typing a file's
   path is fm_key_path's job. One function so the `open` key and a double-click cannot drift apart. */
static void fm_open_sel(void)
{
    unsigned long seq = 0;
    const FmEntry *e = fm_sel_entry(&g_fm);
    /* fm_go_sel() resolves a symlink to a directory with stat() and descends; a dangling link or a plain
       file returns 0 and nothing moves. The result is taken in its own statement because fm_nav's `seq`
       argument must be read AFTER fm_go_sel filled it (see the fm_nav comment). */
    if (e && (e->kind == FMK_DIR || e->kind == FMK_SYMLINK)) {
        int changed = fm_go_sel(&g_fm, &seq);
        fm_nav(changed, seq, "open");
    }
}

/* Matched ONLY while the band owns the keyboard — that gate is what makes bare `Up`, `r` or `Escape` safe.
   Three states: -1 not a band key (reaches the PTY), 0 handled and must NOT auto-repeat, 1 handled and
   repeats. 0 once meant "not handled", so Return typed a path AND a newline the shell ran. */
static int fm_keys(xkb_keysym_t base, unsigned mods)
{
    if (!g_fm.on || g_fm.focus == FM_FOCUS_TERM)
        return -1;
    const FmKeys *k = &conf.fm_key;
    unsigned long seq = 0;

    /* The four scroll keys move the PREVIEWED TEXT, with the browser's bindings: one action per key, the
       focus deciding which body it scrolls. `Up`/`Down` still move the LISTING. */
    if (g_fm.focus == FM_FOCUS_PREVIEW) {
        if (bind_match(&k->pageup, base, mods))   { fm_preview_page(&g_fm, -1); W.dirty = 1; return 1; }
        if (bind_match(&k->pagedown, base, mods)) { fm_preview_page(&g_fm, +1); W.dirty = 1; return 1; }
        if (bind_match(&k->home, base, mods))     { fm_preview_top(&g_fm); W.dirty = 1; return 1; }
        if (bind_match(&k->end, base, mods))      { fm_preview_bottom(&g_fm); W.dirty = 1; return 1; }
    }

    if (bind_match(&k->up, base, mods))       { fm_move(&g_fm, -1); fm_sel_changed(); return 1; }
    if (bind_match(&k->down, base, mods))     { fm_move(&g_fm, +1); fm_sel_changed(); return 1; }
    if (bind_match(&k->pageup, base, mods))   { fm_page(&g_fm, -1); fm_sel_changed(); return 1; }
    if (bind_match(&k->pagedown, base, mods)) { fm_page(&g_fm, +1); fm_sel_changed(); return 1; }
    if (bind_match(&k->home, base, mods))     { fm_home(&g_fm); fm_sel_changed(); return 1; }
    if (bind_match(&k->end, base, mods))      { fm_end(&g_fm); fm_sel_changed(); return 1; }
    /* Left is "up one level", Right is "into this one". Right and `open` do NOTHING to a file, so holding
       either through a listing cannot write at the prompt. */
    if (bind_match(&k->left, base, mods)) {
        int changed = fm_parent(&g_fm, &seq);   /* fills seq — see fm_nav */
        fm_nav(changed, seq, "parent");
        return 0;
    }
    if (bind_match(&k->right, base, mods)) {
        const FmEntry *e = fm_sel_entry(&g_fm);
        if (e && (e->kind == FMK_DIR || e->kind == FMK_SYMLINK)) {
            int changed = fm_go_sel(&g_fm, &seq);   /* fills seq — see fm_nav */
            fm_nav(changed, seq, "open");
        }
        return 0;
    }
    if (bind_match(&k->open, base, mods))     { fm_open_sel(); return 0; }
    /* The only band key that touches the shell's line: it types the selected entry's quoted path at the
       cursor and stops there — no CR, so nothing is ever run. A one-shot (return 0): holding it must not
       type the same path over and over. */
    if (bind_match(&k->path, base, mods))     { fm_send_path(); return 0; }
    if (bind_match(&k->parent, base, mods)) {
        int changed = fm_parent(&g_fm, &seq);   /* fills seq — see fm_nav */
        fm_nav(changed, seq, "parent");
        return 0;
    }
    if (bind_match(&k->refresh, base, mods))  { fm_reread(); return 0; }
    if (bind_match(&k->preview, base, mods))  { fm_preview_toggle(); return 0; }
    if (bind_match(&k->hidden, base, mods))   { fm_set_hidden(&g_fm, !g_fm.show_hidden);
                                                fm_sel_changed(); return 0; }
    if (bind_match(&k->quit, base, mods))     { fm_close(); return 0; }
    return -1;
}

/* A click inside the band selects in whichever pane it landed and takes the keyboard; a second on the SAME
   entry within FM_DCLICK_MS opens it. Row -> entry uses the painter's own metrics. */
static void fm_pointer_select(int hit, int btn, uint32_t time)
{
    if (hit == FM_HIT_PREVIEW) {
        fm_focus_set(FM_FOCUS_PREVIEW);
        fm_click_pos = -1;      /* a click here is never the second half of a LIST double-click */
        /* Only the left button selects text: a right or middle click in this pane just moves the
           keyboard. */
        if (btn != 0)
            return;
        int x, y;
        if (!pv_cell(&x, &y))
            return;
        /* One selection at a time in the window: the session's highlight and the pane's cannot both be
           live, because Ctrl+Shift+C has exactly one answer to give. Dropping the session's here is the
           same thing a press in the terminal pane already does to a previous selection. */
        if (cur_term())
            term_select_clear(cur_term());
        /* The same gesture as the terminal pane, counted by term_click_count against the same
           CLICK_DCLICK_MS: second click selects the WORD, third the ROW. conf.click_selects_word
           switches both at once. */
        int same = (x == fm_pv_click_x && y == fm_pv_click_y);
        fm_pv_click_count = conf.click_selects_word
                          ? term_click_count(time, fm_pv_click_ms, same, fm_pv_click_count)
                          : 1;
        fm_pv_click_ms = time;
        fm_pv_click_x = x;
        fm_pv_click_y = y;
        int x0 = x, x1 = x;
        const char *how = "click";
        if (fm_pv_click_count >= 3) {
            fm_pv_click_count = 3;      /* a fourth click stays a row, it does not grow */
            if (fm_preview_row_span(&g_fm, y, &x0, &x1))
                how = "triple click";
        } else if (fm_pv_click_count == 2) {
            if (fm_preview_word_span(&g_fm, x, y, &x0, &x1))
                how = "double click";
        }
        fm_preview_select(&g_fm, x0, y, x1, y);
        W.pv_selecting = 1;
        fm_pv_anchor_x = x;
        fm_pv_anchor_y = y;
        fm_pv_drag_x = x;
        fm_pv_drag_y = y;
        /* Log what the OWNER of the state holds, not what was asked for (Key Decisions: a log line that
           prints its own argument can lie). fm_preview_select() normalises the pair, so the printed range is
           the one the painter bands and the copy reads. */
        klog(L_DEBUG, "fm: preview %s at pane cell (%d,%d) selects %d,%d -> %d,%d",
             how, x, y, g_fm.pv_ax, g_fm.pv_ay, g_fm.pv_bx, g_fm.pv_by);
        return;
    }
    g_fm.focus = FM_FOCUS_LIST;
    int ch = font.cell_h > 0 ? font.cell_h : 1;
    int row = (W.ptr_y - g_layout.fm.list_y) / ch;
    int pos = fm_row_pos(&g_fm, row);
    if (pos < 0) {          /* the header, the footer, or empty space below the listing */
        fm_click_pos = -1;
        W.dirty = 1;
        return;
    }
    int again = (btn == 0 && fm_click_pos >= 0 && pos == fm_click_pos &&
                 time - fm_click_ms <= CLICK_DCLICK_MS);
    fm_click_ms = time;
    fm_click_pos = pos;
    fm_select(&g_fm, pos);
    fm_sel_changed();
    const FmEntry *e = fm_sel_entry(&g_fm);
    klog(L_DEBUG, "fm: click row %d selects %s", row, e ? e->name : "?");
    if (!again)
        return;
    fm_click_pos = -1;      /* consumed: a third click needs a fresh pair, it does not fire again */
    klog(L_DEBUG, "fm: double-click opens %s", e ? e->name : "?");
    fm_open_sel();
}

/* The wheel over the band walks the listing or scrolls the preview's body; 1 = the wheel belonged to the
   file manager. It does NOT take the keyboard by default — a pane is focused by CLICKING it. */
static int fm_wheel(void)
{
    if (!g_fm.on)
        return 0;
    int hit = layout_hit_fm(&g_layout, W.ptr_x, W.ptr_y);
    if (hit == FM_HIT_NONE)
        return 0;
    int steps = 0;
    if (W.frame_discrete_seen)
        steps = W.frame_discrete;                       /* wheel down = later entries */
    else if (W.frame_has_axis)
        steps = (int)W.frame_axis_val / imax_(font.cell_h, 1);
    int f = fm_wheel_focus(hit, steps, conf.fm_focus_on_hover);
    if (f >= 0)
        fm_focus_set(f);
    if (!steps)
        return 1;
    if (hit == FM_HIT_PREVIEW) {
        /* No selection to walk here, so the wheel scrolls the preview's BODY — a file's text or a
           directory's entries. fm_preview_scroll() clamps, so a picture does not move. */
        fm_preview_scroll(&g_fm, steps);
        W.dirty = 1;
        return 1;
    }
    fm_move(&g_fm, steps);
    fm_sel_changed();
    return 1;
}

/* Stop the worker and free everything the feature owns. Draining the pipe AFTER the join is what makes
   this leak-free: the worker cannot write another answer once it is joined. */
static void fm_shutdown(void)
{
    if (fm_worker_up) {
        pthread_mutex_lock(&fm_mtx);
        fm_quit = 1;
        pthread_cond_broadcast(&fm_cv);
        pthread_mutex_unlock(&fm_mtx);
        pthread_join(fm_thr, NULL);
        fm_worker_up = 0;
    }
    if (fm_pipe[0] >= 0) {
        fm_drain_results();
        if (g_ep >= 0)
            epoll_ctl(g_ep, EPOLL_CTL_DEL, fm_pipe[0], NULL);
        close(fm_pipe[0]);
        fm_pipe[0] = -1;
    }
    if (fm_pipe[1] >= 0) {
        close(fm_pipe[1]);
        fm_pipe[1] = -1;
    }
    fm_destroy(&g_fm);
}

/* ---------------- wayland listeners ---------------- */

static void frame_done(void *data, struct wl_callback *cb, uint32_t t);
static const struct wl_callback_listener frame_listener = { .done = frame_done };

static void frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    (void)data; (void)time;
    if (W.frame_cb == cb)
        W.frame_cb = NULL;
    wl_callback_destroy(cb);
    /* The compositor has shown the buffer whose callback this was, so a swap now is least likely to wait on
       a release — the preferred moment to draw. `resized` is in the condition because a size-only configure
       leaves nothing dirty while the compositor waits for a buffer of the new size. */
    if (W.dirty || W.resized)
        redraw();
}

static void wm_base_ping(void *d, struct xdg_wm_base *wb, uint32_t serial)
{
    (void)d;
    xdg_wm_base_pong(wb, serial);
    wl_display_flush(W.display);
}
static const struct xdg_wm_base_listener wm_base_listener = { .ping = wm_base_ping };

static void xdg_surface_configure(void *d, struct xdg_surface *xs, uint32_t serial)
{
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    apply_pending_resize();
    if (!W.first_done) {
        W.first_done = 1;
        W.dirty = 1;
    }
    /* The FIRST configure must attach a buffer right here (a window is not mapped until a commit carries
       one, so waiting for a callback never maps and never gets one); every LATER configure gets a commit
       only — a buffer while another is in flight made the driver fail the swap with EGL_BAD_ALLOC. */
    if (!W.swapped_once)
        redraw();
    else
        wl_surface_commit(W.surface);
}
static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void toplevel_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h,
                              struct wl_array *states)
{
    (void)d; (void)t;
    if (w > 0 && h > 0)
        W.pw = w, W.ph = h;   /* apply after commit via apply_pending_resize */
    /* No behaviour depends on the toplevel states, but they are the only client-side evidence that the
       compositor put the window into fullscreen/maximized, so a change is logged under -d: without it a
       "the screen froze in fullscreen" report cannot be followed from the client side at all. */
    uint32_t *st;
    int fs = 0, mx = 0, act = 0;
    wl_array_for_each(st, states) {
        if (*st == XDG_TOPLEVEL_STATE_FULLSCREEN)
            fs = 1;
        else if (*st == XDG_TOPLEVEL_STATE_MAXIMIZED)
            mx = 1;
        else if (*st == XDG_TOPLEVEL_STATE_ACTIVATED)
            act = 1;
    }
    if (fs != W.is_fullscreen || mx != W.is_maximized || act != W.is_activated) {
        W.is_fullscreen = fs;
        W.is_maximized = mx;
        W.is_activated = act;
        klog(L_DEBUG, "toplevel state: fullscreen %d maximized %d activated %d (configure %dx%d px)",
             fs, mx, act, w, h);
    }
}

static void toplevel_close(void *d, struct xdg_toplevel *t)
{
    (void)d; (void)t;
    /* The window's close button is the close the user cannot undo, so it is the one the gate exists for.
       Before this it set W.running = 0 straight away, and every session in the window lost whatever it was
       running with no warning. */
    if (confirm_gate_window())
        return;
    klog(L_INFO, "toplevel close: quitting without asking (confirm_quit = %s)",
         conf.confirm_quit == CONFIRM_NEVER ? "never" : "running, nothing is running");
    W.running = 0;
}
static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

/* Every byte Kai-Term sends to the application AS INPUT goes through here, so the model knows an echo is
   coming — what tells a bare prompt from a prompt plus typed text. */
static void input_write(Term *t, Pty *p, const void *buf, size_t len)
{
    if (!p)
        return;
    if (t)
        term_note_input(t, buf, len);
    pty_write(p, buf, len);
}

static void send_str(const char *s, size_t n)
{
    /* The user typed at the prompt. A follow sequence in flight would otherwise erase that text or type
       the captured text on top of it, so it stops here — the terminal is the user's again. */
    if (fm_cd_state != FMCD_IDLE)
        fm_cd_abort("you typed at the prompt");
    input_write(cur_term(), cur_pty(), s, n);
}

/* Translate one key press and act on it. Returns 1 when the press should keep going while the key is
   held (a keystroke to the PTY, or scrollback navigation) and 0 for terminal-local one-shots. */
static int send_key(uint32_t key, uint32_t serial)
{
    if (!W.keymap || !W.xkb_state)
        return 0;
    xkb_keycode_t kc = key + 8;

    xkb_keysym_t base = xkb_state_key_get_one_sym(W.xkb_state, kc);
    xkb_mod_mask_t eff = xkb_state_serialize_mods(W.xkb_state, XKB_STATE_MODS_EFFECTIVE);
    xkb_mod_mask_t ctrl = xkb_keymap_mod_get_mask(W.keymap, "Control");
    xkb_mod_mask_t shift = xkb_keymap_mod_get_mask(W.keymap, "Shift");
    int shift_held = shift && (eff & shift);

    /* Multiplexing. Matched FIRST, before the built-in Ctrl+Shift+C/V: the tab actions are the ones the user can
       rebind, so on a shared chord the configured binding wins. Every one returns 0, so a held key never
       opens, closes or switches tab after tab (see the repeat comment at the top of the file). */
    unsigned kbmods = 0;
    if (ctrl && (eff & ctrl))
        kbmods |= KB_CTRL;
    if (shift && (eff & shift))
        kbmods |= KB_SHIFT;
    xkb_mod_mask_t alt = xkb_keymap_mod_get_mask(W.keymap, "Alt");
    xkb_mod_mask_t superm = xkb_keymap_mod_get_mask(W.keymap, "Super");
    if (alt && (eff & alt))
        kbmods |= KB_ALT;
    if (superm && (eff & superm))
        kbmods |= KB_SUPER;
    /* Modal: matched before every binding and the PTY paths, so nothing else can act while the user
       answers. Enter/y confirms, Esc/n cancels — the same answers the box's buttons carry. */
    if (confirm_kind != CONFIRM_NONE) {
        if (base == XKB_KEY_Return || base == XKB_KEY_KP_Enter ||
            base == XKB_KEY_y || base == XKB_KEY_Y)
            confirm_answer(1);
        else if (base == XKB_KEY_Escape || base == XKB_KEY_n || base == XKB_KEY_N)
            confirm_answer(0);
        else
            klog(L_DEBUG, "confirm: key swallowed — the question has the keyboard");
        return 0;
    }
    if (bind_match(&conf.key_new_tab, base, kbmods)) { tab_open(); return 0; }
    if (bind_match(&conf.key_close_tab, base, kbmods)) {
        int t = mux_active(&mux);
        if (!confirm_gate_tab(t))
            tab_close(t);
        return 0;
    }
    if (bind_match(&conf.key_next_tab, base, kbmods)) { tab_switch(mux_next(&mux)); return 0; }
    if (bind_match(&conf.key_prev_tab, base, kbmods)) { tab_switch(mux_prev(&mux)); return 0; }
    if (bind_match(&conf.key_toggle_layout, base, kbmods)) { layout_toggle(); return 0; }
    /* The three window-level bindings work from anywhere: showing the band and moving the keyboard
       between its regions is not something the band can do for itself. The band's own keys are matched
       only while it owns the keyboard. */
    if (bind_match(&conf.key_file_manager, base, kbmods)) { fm_toggle(); return 0; }
    if (bind_match(&conf.key_fm_focus, base, kbmods))     { fm_focus_set(FM_FOCUS_LIST); return 0; }
    if (bind_match(&conf.key_fm_release, base, kbmods))   { fm_focus_set(FM_FOCUS_TERM); return 0; }
    /* key_tab_prefix + a digit jumps to that tab, the one case the KeyBind fields cannot express. The
       digit comes from the LEVEL-0 keysym, not `base`: on a US layout Ctrl+Shift turns 1 into "!". */
    if (conf.tab_prefix_mods && kbmods == conf.tab_prefix_mods) {
        xkb_layout_index_t grp = xkb_state_serialize_layout(W.xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
        const xkb_keysym_t *lev = NULL;
        int nlev = xkb_keymap_key_get_syms_by_level(W.keymap, kc, grp, 0, &lev);
        xkb_keysym_t dig = (nlev > 0 && lev) ? lev[0] : base;
        if (dig >= XKB_KEY_1 && dig <= XKB_KEY_9) {
            tab_switch(mux_goto(&mux, (int)(dig - XKB_KEY_1)));   /* mux_goto clamps to the tabs that exist */
            return 0;
        }
    }

    /* Shift+PageUp/Down/Home/End navigate scrollback instead of sending escape codes. */
    if (shift_held) {
        Term *t = cur_term();
        switch (base) {
        case XKB_KEY_Page_Up:   scrollback_nav(t, t->g.rows); return 1;
        case XKB_KEY_Page_Down: scrollback_nav(t, -t->g.rows); return 1;
        case XKB_KEY_Home:      scrollback_nav(t, t->g.rcap); return 1;
        case XKB_KEY_End:       scrollback_nav(t, -t->g.rcap); return 1;
        default: break;
        }
    }
    /* Ctrl+Shift+C copies the current selection; Ctrl+Shift+V pastes the clipboard.
       Handled before snap_to_live so a copy can read a scrolled-back selection. */
    if (ctrl && (eff & ctrl) && shift_held) {
        /* One-shot: repeating either would re-offer the selection or paste the clipboard over and
           over, so these never arm the repeat. */
        if (base == XKB_KEY_C) { selection_copy(serial); return 0; }
        if (base == XKB_KEY_V) { clipboard_paste("Ctrl+Shift+V"); return 0; }
    }
    /* The band's own keys, matched only while it owns the keyboard (see fm_keys). Before the PTY paths,
       so a key the file manager uses never reaches the shell and never snaps the terminal's view. A
       handled key returns 0 or 1 here (one-shot / repeatable); only -1 means "not the band's". */
    int fmk = fm_keys(base, kbmods);
    if (fmk >= 0)
        return fmk;
    /* The band owns the keyboard, so nothing else may reach the shell: a key that is not a band binding
       falls through to the PTY and runs in a terminal the user is not looking at. Chrome matched above
       still works. */
    if (g_fm.on && g_fm.focus != FM_FOCUS_TERM) {
        klog(L_DEBUG, "fm: key swallowed — the file manager has the keyboard");
        return 0;
    }
    /* any other key sent to the PTY returns the view to the live bottom */
    snap_to_live(cur_term());

    if (ctrl && (eff & ctrl)) {
        /* Ctrl+letter / Ctrl+common punctuation -> control codes */
        if (base >= XKB_KEY_a && base <= XKB_KEY_z) {
            char c = (char)(base - XKB_KEY_a + 1);
            send_str(&c, 1);
            return 1;
        }
        switch (base) {
        case XKB_KEY_bracketleft:  send_str("\x1b", 1); return 1;
        case XKB_KEY_bracketright: send_str("\x1d", 1); return 1;
        case XKB_KEY_backslash:    send_str("\x1c", 1); return 1;
        case XKB_KEY_3:            send_str("\x1b", 1); return 1;   /* Ctrl+3 = ESC */
        case XKB_KEY_space:        send_str("\x00", 1); return 1;
        /* zoom is a one-shot: a held Ctrl+- would otherwise fly past every size */
        case XKB_KEY_plus: case XKB_KEY_equal: case XKB_KEY_KP_Add:
            zoom_apply(conf.font_size + 2); return 0;
        case XKB_KEY_minus: case XKB_KEY_KP_Subtract:
            zoom_apply(conf.font_size - 2); return 0;
        case XKB_KEY_0: case XKB_KEY_KP_0:
            zoom_apply(base_font_size); return 0;
        default: break;
        }
    }

    switch (base) {
    case XKB_KEY_Return:       send_str("\r", 1); return 1;
    case XKB_KEY_KP_Enter:     send_str("\r", 1); return 1;
    case XKB_KEY_BackSpace:    send_str("\x7f", 1); return 1;
    case XKB_KEY_Tab:          send_str("\t", 1); return 1;
    case XKB_KEY_ISO_Left_Tab: send_str("\x1b[Z", 3); return 1;
    case XKB_KEY_Escape:       send_str("\x1b", 1); return 1;
    case XKB_KEY_Up:           send_str(term_arrow_seq(cur_term(), 'A'), 3); return 1;
    case XKB_KEY_Down:         send_str(term_arrow_seq(cur_term(), 'B'), 3); return 1;
    case XKB_KEY_Right:        send_str(term_arrow_seq(cur_term(), 'C'), 3); return 1;
    case XKB_KEY_Left:         send_str(term_arrow_seq(cur_term(), 'D'), 3); return 1;
    case XKB_KEY_Home:         send_str("\x1b[H", 3); return 1;
    case XKB_KEY_End:          send_str("\x1b[F", 3); return 1;
    case XKB_KEY_Page_Up:      send_str("\x1b[5~", 4); return 1;
    case XKB_KEY_Page_Down:    send_str("\x1b[6~", 4); return 1;
    case XKB_KEY_Insert:       send_str("\x1b[2~", 4); return 1;
    case XKB_KEY_Delete:       send_str("\x1b[3~", 4); return 1;
    case XKB_KEY_F1:  send_str("\x1bOP", 4); return 1;
    case XKB_KEY_F2:  send_str("\x1bOQ", 4); return 1;
    case XKB_KEY_F3:  send_str("\x1bOR", 4); return 1;
    case XKB_KEY_F4:  send_str("\x1bOS", 4); return 1;
    case XKB_KEY_F5:  send_str("\x1b[15~", 6); return 1;
    case XKB_KEY_F6:  send_str("\x1b[17~", 6); return 1;
    case XKB_KEY_F7:  send_str("\x1b[18~", 6); return 1;
    case XKB_KEY_F8:  send_str("\x1b[19~", 6); return 1;
    case XKB_KEY_F9:  send_str("\x1b[20~", 6); return 1;
    case XKB_KEY_F10: send_str("\x1b[21~", 6); return 1;
    case XKB_KEY_F11: send_str("\x1b[23~", 6); return 1;
    case XKB_KEY_F12: send_str("\x1b[24~", 6); return 1;
    default: break;
    }
    if (base >= 0x10000000)   /* dead keys and other non-text keysyms */
        return 0;
    if (base == XKB_KEY_NoSymbol)
        return 0;
    char buf[64];
    int n = xkb_state_key_get_utf8(W.xkb_state, kc, buf, sizeof(buf));
    if (n <= 0)
        return 0;
    send_str(buf, (size_t)n);
    return 1;
}

/* Stop repeating and forget the held key. Any expiry that already fired is drained here, so a release
   that raced with the timer cannot deliver one extra keystroke. */
static void repeat_stop(void)
{
    rep_key = -1;
    if (krfd < 0)
        return;
    struct itimerspec its = { 0 };
    timerfd_settime(krfd, 0, &its, NULL);
    uint64_t exp;
    while (read(krfd, &exp, sizeof(exp)) == (ssize_t)sizeof(exp))
        ;
}

/* A repeatable press: (re)start the delay window. One-shot — repeat_fire() switches to the rate. */
static void repeat_start(uint32_t key, uint32_t serial)
{
    if (krfd < 0 || conf.repeat_delay <= 0)
        return;
    rep_key = (int)key;
    rep_serial = serial;
    struct itimerspec its = { 0 };
    its.it_value.tv_sec = conf.repeat_delay / 1000;
    its.it_value.tv_nsec = (long)(conf.repeat_delay % 1000) * 1000000L;
    timerfd_settime(krfd, 0, &its, NULL);
}

/* The delay elapsed: re-send the held key, then keep re-sending it every 1/repeat_rate seconds. The
   key is re-TRANSLATED on every repeat (send_key reads the current xkb state), so pressing Shift while
   holding 'a' starts producing 'A', exactly as on any other terminal. */
static void repeat_fire(void)
{
    uint64_t exp;
    while (read(krfd, &exp, sizeof(exp)) == (ssize_t)sizeof(exp))
        ;
    if (rep_key < 0)
        return;
    int rate = conf.repeat_rate > 0 ? conf.repeat_rate : 30;
    long ns = 1000000000L / rate;          /* rate 1 -> exactly one second */
    struct itimerspec its = { 0 };
    its.it_interval.tv_sec = ns / 1000000000L;    /* tv_nsec must stay < 1e9 */
    its.it_interval.tv_nsec = ns % 1000000000L;
    its.it_value = its.it_interval;
    timerfd_settime(krfd, 0, &its, NULL);
    send_key((uint32_t)rep_key, rep_serial);
}

static void kb_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t time,
                   uint32_t key, uint32_t state_wl)
{
    (void)d; (void)k; (void)time;
    if (!W.keymap || !W.xkb_state)
        return;
    if (state_wl == WL_KEYBOARD_KEY_STATE_PRESSED) {
        /* Only the most recent repeatable press repeats, which is what the X server does too. A press
           that is NOT repeatable deliberately leaves an existing repeat running: holding BackSpace and
           tapping a modifier must not interrupt the BackSpace. */
        if (send_key(key, serial))
            repeat_start(key, serial);
        return;
    }
    /* Released the key that was repeating: stop. Releasing some OTHER key leaves the repeat alone.
       Kai-Term tracks a single key, so releasing the newest key also ends the repeat of an older key
       that is still held — a deliberate simplification (see the krfd block at the top of the file). */
    if (state_wl == WL_KEYBOARD_KEY_STATE_RELEASED && rep_key == (int)key)
        repeat_stop();
}

/* ---- mouse reporting helpers (DEC ?1000/?1002/?1006) ---- */

/* The evdev BTN_* -> SGR button-number mapping lives in vt.c (mouse_sgr_button) because that is where
   the mouse-reporting state is and because main.c is linked into no test binary. Session 61 found the
   local version of it had BTN_RIGHT and BTN_MIDDLE swapped. */

static void mouse_cell(int *col, int *row)
{
    int cw = font.cell_w > 0 ? font.cell_w : 1;
    int ch = font.cell_h > 0 ? font.cell_h : 1;
    *col = iclamp_((W.ptr_x - W.gx) / cw, 0, cur_term()->g.cols - 1) + 1;   /* 1-based */
    *row = iclamp_((W.ptr_y - W.gy) / ch, 0, cur_term()->g.rows - 1) + 1;
}

/* 0-based viewport cell under the pointer. Selection is CELL granular, so an endpoint can never land in the
   middle of a glyph. This mapping is exact only because render.c draws the grid at the SAME origin this
   computes; a pointer in the margin or in the whole-cell remainder clamps to the first/last row/column. */
static void sel_cell(int *col, int *row)
{
    int cw = font.cell_w > 0 ? font.cell_w : 1;
    int ch = font.cell_h > 0 ? font.cell_h : 1;
    *col = iclamp_((W.ptr_x - W.gx) / cw, 0, cur_term()->g.cols - 1);
    *row = iclamp_((W.ptr_y - W.gy) / ch, 0, cur_term()->g.rows - 1);
}

/* The preview pane's cell under the pointer, in that pane's own coordinates. Exact for the same reason
   sel_cell() is — same cell box, remainder clamped to the last row/column. 0 = no preview pane. */
static int pv_cell(int *x, int *y)
{
    if (!g_fm.on || !g_fm.preview_on || !g_fm.prev.cell || g_fm.prev.cols <= 0 || g_fm.prev.rows <= 0)
        return 0;
    if (!g_layout.fm.on || !g_layout.fm.prev_on)
        return 0;
    int cw = font.cell_w > 0 ? font.cell_w : 1;
    int ch = font.cell_h > 0 ? font.cell_h : 1;
    *x = iclamp_((W.ptr_x - g_layout.fm.prev_x) / cw, 0, g_fm.prev.cols - 1);
    *y = iclamp_((W.ptr_y - g_layout.fm.prev_y) / ch, 0, g_fm.prev.rows - 1);
    return 1;
}

/* The pane the pointer is over and the cell of that pane's grid it names: in side-by-side view the
   pointer can be over a session the keyboard is not talking to. 0 = over no grid at all. */
static int hover_cell(int *tab, int *col, int *row)
{
    if (!W.ptr_in)
        return 0;
    int pi = layout_hit_pane(&g_layout, W.ptr_x, W.ptr_y);
    if (pi < 0)
        return 0;
    const PaneGeom *pg = &g_layout.pane[pi];
    if (pg->cols <= 0 || pg->rows <= 0)
        return 0;                            /* a pane with no grid has no cell to name */
    int cw = font.cell_w > 0 ? font.cell_w : 1;
    int ch = font.cell_h > 0 ? font.cell_h : 1;
    *tab = pg->tab;
    *col = iclamp_((W.ptr_x - pg->x) / cw, 0, pg->cols - 1);
    *row = iclamp_((W.ptr_y - pg->y) / ch, 0, pg->rows - 1);
    return 1;
}

/* A pointer event just happened: if the CELL under it changed, the frame has to be redrawn. Only the
   cheap half of the question is asked here (a pane index and a cell, no scan); the run itself is
   recomputed in hover_update(). */
static void hover_motion(void)
{
    int tab = -1, col = 0, row = 0;
    if (!hover_cell(&tab, &col, &row))
        tab = -1;                            /* over no pane: one state, whatever the stale coords say */
    if (tab == W.hover_tab && col == W.hover_col && row == W.hover_row)
        return;
    W.hover_tab = tab;
    W.hover_col = col;
    W.hover_row = row;
    W.dirty = 1;
}

/* The address under the pointer, copied into Render for the frame about to be built. Recomputed every
   frame, not remembered from the motion event: a scroll, new output or a resize changes what is under
   the pointer with no pointer event. */
static void hover_update(void)
{
    rend.hover_tab = -1;
    rend.hover_span.r0 = rend.hover_span.c0 = rend.hover_span.r1 = rend.hover_span.c1 = 0;

    int tab = -1, col = 0, row = 0;
    int got = hover_cell(&tab, &col, &row);
    /* The confirm-quit box owns the pointer while it is up (ptr_press_action() answers PTR_DIALOG for every
       press), so a click behind it answers the question and opens nothing — no underline. */
    if (got && g_layout.dialog.on)
        got = 0;
    if (!got || tab < 0 || tab >= mux_count(&mux))
        return;

    Term *t = &mux.tab[tab].term;
    UrlSpan sp;
    sp.r0 = sp.c0 = sp.r1 = sp.c1 = 0;
    int found = 0;
    if (conf.click_opens_url && term_url_span(t, col, row, &sp)) {
        char url[URL_MAX + 1];
        url[0] = 0;
        /* The SAME two questions the click asks, so the underline appears exactly on a run that would be opened:
           0 from term_url_text() means it refused (a character it cannot spell, a run that does not fit) and -1
           means it is longer than anything to hand an opener. Both answer "no underline". */
        if (term_url_text(t, &sp, url, sizeof url) > 0)
            found = 1;
    }
    UrlHover hv = { .enabled = conf.click_opens_url,
                    .in_pane = 1,
                    .dragging = W.selecting || W.pv_selecting,
                    .shift = ptr_shift_held(),
                    .app_owns_click = term_wants_key_mouse(t),
                    .url_found = found };
    if (!url_hover_shows(&hv))
        return;
    rend.hover_tab = tab;
    rend.hover_span = sp;
}

/* Set both selection endpoints. An endpoint is always a cell EDGE, never a point inside a glyph; the one
   first in reading order gets the LEFT edge, so dragging left swaps the endpoints and their fracs with
   them. */
static void sel_set_endpoints(int ax, int ay, int bx, int by)
{
    int forward = (by > ay) || (by == ay && bx >= ax);
    cur_term()->sel_ax = ax;
    cur_term()->sel_ay = ay;
    cur_term()->sel_bx = bx;
    cur_term()->sel_by = by;
    cur_term()->sel_af = forward ? 0 : 1000;
    cur_term()->sel_bf = forward ? 1000 : 0;
}

/* code: SGR button number (0..3), or +32 for a motion event. action: 'M' press/motion,
   'm' release. SGR (1006) sends \e[<code;col;rowM/m; legacy X10 sends \e[M with each
   component offset by 32 (so a motion code of btn+32 becomes 64+btn, as X10 expects). */
static void mouse_send(int code, int col, int row, char action)
{
    char seq[40];
    int n;
    if (cur_term()->mouse_mode & MOUSE_SGR)
        n = snprintf(seq, sizeof(seq), "\x1b[<%d;%d;%d%c", code, col, row, action);
    else
        n = snprintf(seq, sizeof(seq), "\x1b[M%c%c%c", 32 + code, 32 + col, 32 + row);
    if (n > 0)
        pty_write(cur_pty(), seq, (size_t)n);
}

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t serial,
                      struct wl_surface *surf, wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)surf;
    /* surface_x/surface_y are wl_fixed_t, not pixels: the cell mapping divides by the cell size, so a
       raw fixed-point value (~256x too large) clamps every click to the last row and column. */
    W.ptr_x = wl_fixed_to_int(x);
    W.ptr_y = wl_fixed_to_int(y);
    W.ptr_in = 1;
    hover_motion();
    if (!W.cursor_theme && W.shm) {
        const char *name = getenv("XCURSOR_THEME");
        W.cursor_theme = wl_cursor_theme_load(name ? name : "left_ptr", 24, W.shm);
        if (W.cursor_theme) {
            struct wl_cursor *c = wl_cursor_theme_get_cursor(W.cursor_theme, "left_ptr");
            if (c)
                W.cursor_image = c->images[0];
        }
    }
    if (W.cursor_image && !W.cursor_surface) {
        W.cursor_surface = wl_compositor_create_surface(W.compositor);
    }
    if (W.cursor_image && W.cursor_surface) {
        struct wl_buffer *buf = wl_cursor_image_get_buffer(W.cursor_image);
        if (buf) {
            wl_surface_attach(W.cursor_surface, buf, 0, 0);
            wl_surface_damage(W.cursor_surface, 0, 0,
                              W.cursor_image->width, W.cursor_image->height);
            wl_surface_commit(W.cursor_surface);
            wl_pointer_set_cursor(p, serial, W.cursor_surface,
                                  W.cursor_image->hotspot_x, W.cursor_image->hotspot_y);
        }
    } else {
        wl_pointer_set_cursor(p, serial, NULL, 0, 0);
    }
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *surf)
{
    (void)d; (void)serial; (void)surf;
    wl_pointer_set_cursor(p, 0, NULL, 0, 0);
    /* The pointer is gone but ptr_x/ptr_y keep their last value, so this is the only thing that can stop the
       hover underline from staying on the screen after the hand left the window. */
    W.ptr_in = 0;
    hover_motion();
}

static void ptr_motion(void *d, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)p; (void)time;
    W.ptr_x = wl_fixed_to_int(x);   /* fixed-point -> integer surface px (see ptr_enter) */
    W.ptr_y = wl_fixed_to_int(y);
    /* A drag inside the preview pane extends THAT selection, not the session's: the two grids have
       different origins and column counts, so the cell comes from pv_cell() and the range is in the
       pane's own coordinates. The flag drops as soon as the band or its selection is gone. */
    if (W.pv_selecting) {
        if (!g_fm.on || !g_fm.pv_sel_on) {
            W.pv_selecting = 0;
            return;
        }
        int px, py;
        if (!pv_cell(&px, &py))
            return;
        if (px == fm_pv_drag_x && py == fm_pv_drag_y)
            return;                 /* the pointer moved within the same cell: nothing changed to repaint */
        fm_pv_drag_x = px;
        fm_pv_drag_y = py;
        fm_preview_select(&g_fm, fm_pv_anchor_x, fm_pv_anchor_y, px, py);
        W.dirty = 1;
        return;
    }
    /* text selection drag: extend the selection to the pointer cell */
    if (W.selecting) {
        int col, row;
        sel_cell(&col, &row);
        int before = (row < cur_term()->sel_ay) || (row == cur_term()->sel_ay && col < cur_term()->sel_ax);
        int af = before ? 1000 : 0, bf = before ? 0 : 1000;
        if (col != cur_term()->sel_bx || row != cur_term()->sel_by || af != cur_term()->sel_af || bf != cur_term()->sel_bf) {
            sel_set_endpoints(cur_term()->sel_ax, cur_term()->sel_ay, col, row);
            W.dirty = 1;
            klog(L_DEBUG, "sel drag cell=(%d,%d) frac=%d ptr=(%d,%d) cw=%d ch=%d rows=%d cols=%d",
                 col, row, cur_term()->sel_bf, W.ptr_x, W.ptr_y, font.cell_w, font.cell_h,
                 cur_term()->g.rows, cur_term()->g.cols);
        }
        return;
    }
    /* wl_pointer.motion is opcode 2 and libwayland aborts if a bound interface's listener
       leaves an event slot NULL. When ?1002 drag tracking is on and a button is held, emit
       a motion report at the pointer's cell. */
    if ((cur_term()->mouse_mode & MOUSE_DRAG) && W.ptr_btn >= 0) {
        int col, row;
        mouse_cell(&col, &row);
        mouse_send(W.ptr_btn + 32, col, row, 'M');
    }
    /* The hover underline follows the pointer CELL, not every motion event: a compositor sends one event per
   pixel, and a frame per pixel would be a storm for nothing. The run itself is recomputed in redraw(),
   which keeps the underline honest when the TEXT moves under a pointer that never moved. */
    hover_motion();
}

/* Pointer events carry no modifier state, but the keyboard listener keeps xkb_state current, so the
   terminal can ask. Shift + click is the escape hatch every terminal uses to step around an
   application's mouse handling. */
static int ptr_shift_held(void)
{
    if (!W.keymap || !W.xkb_state)
        return 0;
    xkb_mod_mask_t shift = xkb_keymap_mod_get_mask(W.keymap, "Shift");
    xkb_mod_mask_t eff = xkb_state_serialize_mods(W.xkb_state, XKB_STATE_MODS_EFFECTIVE);
    return shift && (eff & shift) ? 1 : 0;
}

static void ptr_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t time,
                       uint32_t button, uint32_t state)
{
    (void)d; (void)p;
    int btn = mouse_sgr_button(button);   /* 0 left, 1 middle, 2 right, 3 other (vt.c) */
    int pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;

    /* A preview-pane text drag ends wherever the button is RELEASED — over any pane, the tab bar, or
       outside the band — so this comes BEFORE every hit test below: an early return would leave the drag
       live and the next motion would extend a range under a pointer the user already let go of. */
    if (!pressed && W.pv_selecting) {
        W.pv_selecting = 0;
        fm_pv_anchor_x = fm_pv_anchor_y = fm_pv_drag_x = fm_pv_drag_y = -1;
        /* Same rule as the terminal's release: by default the highlight just stays and Ctrl+Shift+C copies
           it; copy_on_release = 1 offers it to the clipboard straight away. */
        if (conf.copy_on_release) {
            selection_copy(serial);
        } else if (g_fm.pv_sel_on) {
            klog(L_DEBUG, "fm: preview selection finished: pane cell %d,%d -> %d,%d "
                          "(copy_on_release off — Ctrl+Shift+C to copy)",
                 g_fm.pv_ax, g_fm.pv_ay, g_fm.pv_bx, g_fm.pv_by);
        }
        W.dirty = 1;
        return;
    }

    /* Which surface owns this press is decided by ptr_press_action() in src/vt.c — the ORDER is what a
       user feels and what a test can flip; this only fills the flags. */
    int bi = layout_hit_bar(&g_layout, W.ptr_x, W.ptr_y);
    int fm_hit = g_fm.on ? layout_hit_fm(&g_layout, W.ptr_x, W.ptr_y) : FM_HIT_NONE;
    Term *pt = cur_term();
    PtrPress pp = { .btn = btn,
                    .pressed = pressed,
                    .dialog_on = g_layout.dialog.on,
                    .hit_bar = bi >= 0,
                    .hit_fm = fm_hit != FM_HIT_NONE,
                    .app_mouse = pt && (pt->mouse_mode & (MOUSE_PRESS | MOUSE_DRAG)) ? 1 : 0,
                    .middle_paste = conf.middle_click_paste };
    int act = ptr_press_action(&pp);

    /* The confirm box is checked before the bar, the band and the panes: a click on Yes answers the
       question and must not also close the tab underneath. Both press and release are swallowed. */
    if (act == PTR_DIALOG) {
        if (pressed) {
            int hi = layout_hit_dialog(&g_layout, W.ptr_x, W.ptr_y);
            if (hi >= 0) {
                int yes = g_layout.dialog.btn[hi].yes;
                klog(L_INFO, "confirm: the %s button was clicked at %d,%d",
                     yes ? "Yes" : "No", W.ptr_x, W.ptr_y);
                confirm_answer(yes);
            } else {
                klog(L_DEBUG, "confirm: a click at %d,%d is not on a button — nothing answered",
                     W.ptr_x, W.ptr_y);
            }
            W.selecting = 0;
        }
        W.dirty = 1;
        return;
    }

    /* The tab bar is chrome, not terminal, and is checked before the mouse-reporting branch: an
       application that grabbed the mouse must not be able to eat the bar that names its tab. */
    if (act == PTR_BAR) {
        int tab = g_layout.item[bi].tab;
        W.selecting = 0;
        if (btn == 1) {                     /* BTN_MIDDLE closes the session it was clicked on */
            /* Gated like the binding: a middle click is where work is lost most often, because it closes the
               session under the pointer with one press. */
            if (confirm_gate_tab(tab))
                klog(L_INFO, "bar: middle click on session %d/%d asks first", tab + 1, mux_count(&mux));
            else {
                klog(L_INFO, "bar: middle click closes session %d/%d", tab + 1, mux_count(&mux));
                tab_close(tab);
            }
        } else if (btn == 0 && tab != mux_active(&mux)) {
            klog(L_DEBUG, "bar: click selects session %d/%d", tab + 1, mux_count(&mux));
            tab_switch(tab);
        }
        if (!cur_term())
            return;                         /* the middle click closed the last session: the app exits */
        W.dirty = 1;
        return;
    }

    /* The file-manager band is chrome like the bar: a click there never reaches the application (not
       even when it grabbed the mouse) and never starts a text selection. Checked before the mouse-
       reporting branch for the bar's reason. */
    if (g_fm.on) {
        if (act == PTR_FM) {
            W.selecting = 0;
            if (pressed)
                fm_pointer_select(fm_hit, btn, time);
            W.dirty = 1;
            return;
        }
        /* A click in a terminal pane gives the keyboard back to the terminal, so the pointer and the
           keyboard never disagree about where the next keystroke goes. */
        if (pressed && layout_hit_pane(&g_layout, W.ptr_x, W.ptr_y) >= 0)
            fm_focus_set(FM_FOCUS_TERM);
    }

    /* A click inside a pane that is NOT the visible session focuses that session first, so the cell the
       pointer maps to is the cell drawn under it (W.gx/W.gy follow the visible session's pane). */
    if (pressed) {
        int pi = layout_hit_pane(&g_layout, W.ptr_x, W.ptr_y);
        if (pi >= 0 && g_layout.pane[pi].tab != mux_active(&mux))
            tab_switch(g_layout.pane[pi].tab);
    }

    /* The switch above can change WHICH session the pointer is over, and that session may be the one that asked
       for the mouse — so the answer is asked again from the session that is active NOW. Reporting a selection
       to a program that asked for the button types the report into its file. */
    pt = cur_term();
    pp.app_mouse = pt && (pt->mouse_mode & (MOUSE_PRESS | MOUSE_DRAG)) ? 1 : 0;
    act = ptr_press_action(&pp);

    if (act == PTR_REPORT) {
        /* an application grabbed the mouse: report the button to the PTY */
        W.ptr_btn = pressed ? btn : -1;
        int col, row;
        mouse_cell(&col, &row);
        mouse_send(btn, col, row, pressed ? 'M' : 'm');
        return;
    }

    /* The middle button in a plain terminal pane, and ONLY there: the decision above already gave the tab bar,
       the band, the confirm-quit box and an application that holds the mouse their own answer. It pastes the
       CLIPBOARD, and only the PRESS pastes — a release would paste the same text twice. */
    if (act == PTR_PASTE) {
        W.selecting = 0;                 /* a paste is not the start of a selection */
        clipboard_paste("middle click");
        W.dirty = 1;
        return;
    }

    /* No application grabbed the mouse: the left button drives text selection. */
    if (act == PTR_SELECT) {   /* BTN_LEFT */
        int col, row;
        sel_cell(&col, &row);
        if (pressed) {
            /* One selection at a time in the window: a press here ends a preview-pane selection, the mirror
               of what a press in the preview pane does to the session's. Without it both highlights are on
               screen and Ctrl+Shift+C has to guess which one the user meant. */
            fm_preview_select_clear(&g_fm);
            /* A pending open belongs to the gesture that just ended, and this press starts a new one. Whatever
               the last release was waiting on is dropped here; the release below sets a new one if it earns it. */
            W.url_press_ms = 0;
            W.url_text[0] = 0;
            /* Which click in a row is this? Same cell within CLICK_DCLICK_MS continues the gesture. A
               second click selects the WORD, a third the whole LOGICAL line (a soft-wrapped line is one
               line). The rule for what a word is lives in vt.c's term_word_span(). */
            int same = (col == W.click_col && row == W.click_row);
            W.click_count = conf.click_selects_word
                          ? term_click_count(time, W.click_ms, same, W.click_count)
                          : 1;
            W.click_ms = time;
            W.click_col = col;
            W.click_row = row;
            W.click_word = 0;
            cur_term()->sel_active = 1;
            if (W.click_count == 2) {
                int c0, c1;
                if (term_word_span(cur_term(), col, row, &c0, &c1)) {
                    sel_set_endpoints(c0, row, c1, row);
                    W.click_word = 1;
                    klog(L_DEBUG, "sel: double click at (%d,%d) selected the word cols %d-%d",
                         col, row, c0, c1);
                }
            } else if (W.click_count >= 3) {
                W.click_count = 3;        /* a fourth click stays a line selection, it does not grow */
                int r0, r1;
                if (term_line_span(cur_term(), row, &r0, &r1)) {
                    sel_set_endpoints(0, r0, cur_term()->g.cols - 1, r1);
                    W.click_word = 1;
                    klog(L_DEBUG, "sel: triple click at (%d,%d) selected the logical line rows %d-%d",
                         col, row, r0, r1);
                }
            } else {
                sel_set_endpoints(col, row, col, row);   /* one whole cell under the pointer */
            }
            W.selecting = 1;              /* a drag from here extends whatever was selected */
            klog(L_DEBUG, "sel press cell=(%d,%d) frac=%d ptr=(%d,%d) cw=%d ch=%d rows=%d cols=%d",
                 col, row, cur_term()->sel_bf, W.ptr_x, W.ptr_y, font.cell_w, font.cell_h,
                 cur_term()->g.rows, cur_term()->g.cols);
        } else {
            W.selecting = 0;
            /* A click that never moved asks the program to put its cursor where they clicked — only for
               a program that owns its own screen and never asked for the mouse, the one state where a
               report cannot be sent. A DRAG still selects, and Shift still means selection. */
            int no_drag = (col == cur_term()->sel_ax && row == cur_term()->sel_ay);
            /* A plain click on a web address. The ORDER is url_click_opens()'s in src/url.c, so "a drag
               still selects" and "Shift means selection" are rows in a table a test can flip; main.c
               only decides that an open is PENDING until the double-click window passes. */
            Term *ut = cur_term();
            UrlSpan usp;
            char url[sizeof W.url_text];
            url[0] = 0;
            UrlClick uc = { .enabled = conf.click_opens_url,
                            .released = 1,
                            .single = W.click_count <= 1,
                            .no_drag = no_drag,
                            .shift = ptr_shift_held(),
                            .app_owns_click = term_wants_key_mouse(ut),
                            .url_found = 0 };
            if (uc.enabled && uc.single && uc.no_drag && !uc.shift && !uc.app_owns_click &&
                term_url_span(ut, col, row, &usp)) {
                int ul = term_url_text(ut, &usp, url, sizeof url);
                if (ul < 0)
                    klog(L_WARN, "url: a run of more than %d characters is not something to open", URL_MAX);
                else if (ul > 0)
                    uc.url_found = 1;
            }
            int why = url_refusal(&uc);
            if (why == URL_REFUSE_NONE) {
                W.url_press_ms = (long long)mono_ms();
                W.url_tab = mux_active(&mux);
                W.url_col = col;
                W.url_row = row;
                snprintf(W.url_text, sizeof W.url_text, "%s", url);
                term_select_clear(ut);   /* a click that opens something leaves no one-cell highlight behind */
                klog(L_DEBUG, "url: the click at (%d,%d) holds %s — waiting out the double-click window",
                     col, row, url);
                W.dirty = 1;
                return;
            }
            /* A refused click logs which of its checks failed — without that, a click on an address that
               did nothing leaves nothing in the log to look at. */
            if (uc.enabled)
                klog(L_DEBUG, "url: the click at (%d,%d) opened nothing — %s",
                     col, row, url_refusal_name(why));
            if (no_drag && !W.click_word && conf.click_moves_cursor && !ptr_shift_held() &&
                term_wants_key_mouse(cur_term())) {
                char seq[3 * TERM_ARROW_MAX + 8];
                int n = term_click_arrow_keys(cur_term(), col, row, seq, sizeof seq);
                if (n > 0) {
                    term_select_clear(cur_term());   /* the cursor moved; leave no stray highlight */
                    send_str(seq, (size_t)n);
                    /* Stamped so a BEL answering these presses is not mistaken for one the program rang
                       on its own. The wheel path is not stamped — there the user asked to scroll. */
                    Tab *st = mux_cur(&mux);
                    if (st)
                        st->synth_ms = (long long)mono_ms();
                    klog(L_DEBUG, "mouse: click at (%d,%d) became %d cursor key press(es) for the "
                                  "application", col, row, n / 3);
                    W.dirty = 1;
                    return;
                }
            }
            /* Releasing the drag used to ALWAYS offer the selection to the clipboard, so any stray
               click-drag overwrote what the user had copied. `copy_on_release = 1` restores that; by
               default the selection stays visible and Ctrl+Shift+C copies it. */
            if (conf.copy_on_release) {
                selection_copy(serial);   /* finalize: offer the selection to the clipboard */
            } else if (cur_term()->sel_active) {
                klog(L_DEBUG, "selection finished: %d,%d -> %d,%d (copy_on_release off — "
                              "Ctrl+Shift+C to copy)",
                     cur_term()->sel_ax, cur_term()->sel_ay, cur_term()->sel_bx, cur_term()->sel_by);
            }
        }
        W.dirty = 1;
    }
}

/* A program that owns its screen and never asked for the mouse has no scrollback to move, and a mouse
   report would reach it as typed text — so the wheel becomes cursor-key presses. */
static int app_wheel(Term *t, double delta_lines)
{
    if (!conf.wheel_sends_keys || !term_wants_key_mouse(t)) {
        W.wheel_accum = 0;
        return 0;
    }
    W.wheel_accum += delta_lines;
    int whole = (int)W.wheel_accum;          /* truncate toward zero, as scroll_apply() does */
    if (whole == 0)
        return 1;
    W.wheel_accum -= whole;
    char seq[3 * TERM_ARROW_MAX + 8];
    int n = term_wheel_arrow_keys(t, whole, seq, sizeof seq);
    if (n > 0) {
        send_str(seq, (size_t)n);
        klog(L_DEBUG, "mouse: the wheel became %d %s for the application",
             n / 3, t->app_cursor ? "SS3 cursor key press(es)" : "cursor key press(es)");
    }
    return 1;
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t time, uint32_t axis,
                     wl_fixed_t value)
{
    (void)d; (void)p; (void)time;
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
        return;
    double v = wl_fixed_to_double(value);
    if (cur_term()->mouse_mode & (MOUSE_PRESS | MOUSE_DRAG)) {
        /* An application grabbed the mouse: report the wheel as button 64 (up) / 65 (down), which is what xterm,
           kitty, neovim and tmux send — and NOT 4/5, which ncurses reads as button 1 and so leaves the
           wheel dead. mouse_wheel_button() (vt.c) owns those numbers so tests/test_vt.c can pin them. */
        int col, row;
        mouse_cell(&col, &row);
        mouse_send(mouse_wheel_button(v < 0), col, row, 'M');
        return;
    }
    /* No application grabbed the mouse: accumulate the continuous (pixel) delta for this frame.
       The frame handler turns it into whole-line scrollback motion; discrete wheel steps
       (ptr_axis_discrete) take precedence when the compositor sends them. */
    W.frame_axis_val += v;
    W.frame_has_axis = 1;
}

static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t source)
{
    (void)d; (void)p; (void)source;   /* must be handled at pointer v5; discrete/value drive scrolling */
}

static void ptr_axis_stop(void *d, struct wl_pointer *p, uint32_t time, uint32_t axis)
{
    (void)d; (void)p; (void)time; (void)axis;   /* momentum end: nothing extra to apply */
}

static void ptr_axis_discrete(void *d, struct wl_pointer *p, uint32_t axis, int32_t discrete)
{
    (void)d; (void)p;
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
        return;
    if (cur_term()->mouse_mode & (MOUSE_PRESS | MOUSE_DRAG))
        return;   /* app-grabbed wheel is already reported via ptr_axis */
    W.frame_discrete += discrete;
    W.frame_discrete_seen = 1;
}

/* wl_pointer.frame groups one scroll gesture's events. Convert the frame's accumulated scroll
   into whole-line scrollback motion (discrete wheel steps preferred, continuous pixel delta as
   fallback), then reset the per-frame accumulators. */
static void ptr_scroll_done(void *d, struct wl_pointer *p)
{
    (void)d; (void)p;
    if (!(cur_term()->mouse_mode & (MOUSE_PRESS | MOUSE_DRAG))) {
        if (!fm_wheel()) {
            double delta_lines = 0.0;   /* + = scroll up (into history) */
            if (W.frame_discrete_seen)
                delta_lines = -(double)W.frame_discrete * SCROLL_LINES_PER_DETENT;
            else if (W.frame_has_axis) {
                int ch = font.cell_h > 0 ? font.cell_h : 1;
                delta_lines = -W.frame_axis_val / (double)ch;
            }
            if (delta_lines != 0.0) {
                /* The application owns this screen and never asked for the mouse: the wheel is its
                   cursor keys, not the terminal's scrollback. */
                if (!app_wheel(cur_term(), delta_lines))
                    scroll_apply(delta_lines);
            }
        }
    }
    W.frame_axis_val = 0;
    W.frame_discrete = 0;
    W.frame_has_axis = 0;
    W.frame_discrete_seen = 0;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = ptr_enter,
    .leave = ptr_leave,
    .motion = ptr_motion,
    .button = ptr_button,
    .axis = ptr_axis,
    .frame = ptr_scroll_done,
    .axis_source = ptr_axis_source,
    .axis_stop = ptr_axis_stop,
    .axis_discrete = ptr_axis_discrete,
};

/* ---------------- clipboard (Wayland data-device) ---------------- */

/* Our data source: when another client asks for the selection, the compositor calls send()
   with a pipe fd; we write the copied text to it. */
static void source_send(void *d, struct wl_data_source *src, const char *mime, int32_t fd)
{
    (void)d; (void)src; (void)mime;
    size_t off = 0;
    while (clip_text && off < clip_len) {
        ssize_t w = write(fd, clip_text + off, clip_len - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += (size_t)w;
    }
    close(fd);
}
static void source_cancelled(void *d, struct wl_data_source *src)
{
    (void)d;
    wl_data_source_destroy(src);
    if (src == clip_source)
        clip_source = NULL;
}
static const struct wl_data_source_listener source_listener = {
    .send = source_send,
    .cancelled = source_cancelled,
};

/* A data offer (from another client's selection): record the best text mime it offers. */
static void offer_offer(void *d, struct wl_data_offer *o, const char *mime)
{
    (void)d; (void)o;
    /* Keep the BEST advertised text type, not the LAST one: which types a source advertises, and in what
       order, is entirely the source's business (Firefox offers four in an order of its own), and the
       type asked for decides the bytes that come back. */
    int r = clipboard_mime_rank(mime);
    if (r > last_offer_text) {
        last_offer_text = r;
        snprintf(last_offer_mime, sizeof(last_offer_mime), "%s", mime);
    }
}
static void offer_actions(void *d, struct wl_data_offer *o, uint32_t a)
{
    (void)d; (void)o; (void)a;
}
static const struct wl_data_offer_listener offer_listener = {
    .offer = offer_offer,
    .source_actions = offer_actions,
};

/* Begin receiving paste data from an offer into a pipe; the read end is added to epoll. */
static void start_paste(struct wl_data_offer *offer, const char *mime)
{
    if (paste_fd[0] >= 0)
        return;   /* already receiving */
    if (pipe(paste_fd) < 0) {
        paste_fd[0] = paste_fd[1] = -1;
        return;
    }
    fcntl(paste_fd[0], F_SETFL, O_NONBLOCK);
    wl_data_offer_receive(offer, mime, paste_fd[1]);
    close(paste_fd[1]);   /* our copy of the write end; the source keeps its own */
    paste_fd[1] = -1;
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = paste_fd[0] };
    if (g_ep >= 0)
        epoll_ctl(g_ep, EPOLL_CTL_ADD, paste_fd[0], &ev);
}

/* Paste idle watchdog. A paste is flushed when the source closes the pipe (EOF), which every real client
   does; if one leaves the write end open the buffered block would sit there forever, so after a quiet
   period we deliver what we have. One-shot: re-armed by the next short read. */
static int pwfd = -1;

static void paste_watch_arm(void)
{
    if (pwfd < 0 || g_ep < 0)
        return;
    struct itimerspec its = { .it_value = { .tv_sec = 0, .tv_nsec = 400000000 } };
    timerfd_settime(pwfd, 0, &its, NULL);
}

static void paste_watch_disarm(void)
{
    if (pwfd < 0)
        return;
    struct itimerspec its = { 0 };
    timerfd_settime(pwfd, 0, &its, NULL);
    uint64_t exp;
    while (read(pwfd, &exp, sizeof(exp)) == (ssize_t)sizeof(exp))
        ;
}

/* ---------- the cursor blink ----------
   The user's `cursor_blink =` is the ceiling: DECSCUSR can only make the cursor LESS flashy, and a
   blink needs the window to have focus. */
static int cbfd = -1;
static int cbfd_half;                 /* the half-period currently armed; 0 = disarmed */
static Term *cbfd_term;               /* the session that half-period was decided for */

static int blink_half_ms(void)
{
    if (conf.cursor_blink <= 0)
        return 0;                     /* `cursor_blink = 0` — steady, and the app cannot override that */
    if (!W.is_activated)
        return 0;                     /* an unfocused window sits still */
    Term *t = cur_term();
    if (!t || !t->show_cursor)
        return 0;                     /* DEC 25l: there is no cursor to blink */
    if (t->cur_blink == CURBLINK_STEADY)
        return 0;                     /* the application asked for a steady cursor */
    return conf.cursor_blink;
}

/* Re-arm only when the wanted state CHANGED: this runs from the frame tick, and a timerfd_settime per
   frame would be 60 syscalls a second. "Changed" includes WHICH session is visible, or a switch would
   leave the new session in the old one's off half. */
static void blink_update(void)
{
    if (cbfd < 0)
        return;
    Term *t = cur_term();
    int half = blink_half_ms();
    if (half == cbfd_half && t == cbfd_term)
        return;
    cbfd_half = half;
    cbfd_term = t;
    struct itimerspec its = { 0 };
    if (half > 0) {
        its.it_interval.tv_sec = half / 1000;
        its.it_interval.tv_nsec = (long)(half % 1000) * 1000000L;
        its.it_value = its.it_interval;
        W.cursor_on = 1;              /* start a fresh blink on the visible side, not the hidden one */
    } else if (!W.cursor_on) {
        W.cursor_on = 1;              /* going steady: the cursor has to come back */
        W.dirty = 1;
    }
    timerfd_settime(cbfd, 0, &its, NULL);
    klog(L_DEBUG, "cursor blink: half-period %d ms (%s)", half, half > 0 ? "blinking" : "steady");
}

/* mux_bell_live() (src/mux.c) owns the rule — it knows only a DRAWN session has a field to wash out. The
   duration is handed in so this and the renderer cannot disagree. */
static int bell_live(void)
{
    return mux_bell_live(&mux, &g_layout, (long long)mono_ms(),
                         conf.bell == BELL_VISUAL ? BELL_FLASH_MS : 0);
}

/* The frame timer's bell tick: while a flash is live the window redraws every frame or the fade freezes,
   and the frame it ends on redraws once more to clear the wash. */
static void bell_tick(void)
{
    int live = bell_live();
    if (live || W.bell_live)
        W.dirty = 1;
    W.bell_live = live;
}

/* The frame timer's URL tick. The open is re-checked at the deadline rather than trusted from the click:
   the tab may have closed, the pane scrolled, or the line repainted since. */
static void url_tick(void)
{
    if (!W.url_press_ms)
        return;
    if (!url_open_due((long long)mono_ms(), W.url_press_ms, URL_OPEN_WAIT_MS))
        return;
    int ti = W.url_tab;
    int col = W.url_col, row = W.url_row;
    char clicked[URL_MAX + 1];
    snprintf(clicked, sizeof clicked, "%s", W.url_text);
    W.url_press_ms = 0;
    W.url_text[0] = 0;
    if (ti < 0 || ti >= mux_count(&mux)) {
        klog(L_DEBUG, "url: the session the click was in is gone — nothing opened");
        return;
    }
    Term *t = &mux.tab[ti].term;
    UrlSpan sp;
    char holds[URL_MAX + 1];
    if (!term_url_span(t, col, row, &sp) ||
        term_url_text(t, &sp, holds, sizeof holds) <= 0 || strcmp(holds, clicked)) {
        klog(L_INFO, "url: the text under the click changed while the click waited — nothing opened");
        return;
    }
    klog(L_INFO, "url: a click asked %s for %s", URL_OPENER_PROGRAM, clicked);
    if (url_spawn(URL_OPENER_PROGRAM, clicked) < 0)
        klog(L_WARN, "url: could not start %s: %s", URL_OPENER_PROGRAM, strerror(errno));
}

static void dd_data_offer(void *d, struct wl_data_device *dd, struct wl_data_offer *offer)
{
    (void)d; (void)dd;
    last_offer_text = 0;
    last_offer_mime[0] = 0;
    wl_data_offer_add_listener(offer, &offer_listener, NULL);
}
static void dd_enter(void *d, struct wl_data_device *dd, uint32_t serial, struct wl_surface *surf,
                     wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *offer)
{
    (void)d; (void)dd; (void)serial; (void)surf; (void)x; (void)y; (void)offer;   /* DnD not supported */
}
static void dd_leave(void *d, struct wl_data_device *dd) { (void)d; (void)dd; }
static void dd_motion(void *d, struct wl_data_device *dd, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)dd; (void)time; (void)x; (void)y;
}
static void dd_drop(void *d, struct wl_data_device *dd)
{
    (void)d; (void)dd;   /* DnD not supported */
}
static void dd_selection(void *d, struct wl_data_device *dd, struct wl_data_offer *offer)
{
    (void)d; (void)dd;
    /* The compositor pushes the current clipboard selection here (there is no request_selection
       for wl_data_device). Destroy the previous selection offer, then adopt the new one. */
    if (sel_offer && sel_offer != offer)
        wl_data_offer_destroy(sel_offer);
    sel_offer = offer;                 /* may be NULL (selection cleared) */
    sel_offer_text = offer ? last_offer_text : 0;
    if (offer)
        snprintf(sel_offer_mime, sizeof(sel_offer_mime), "%s", last_offer_mime);
    else
        sel_offer_mime[0] = 0;
}
static const struct wl_data_device_listener data_device_listener = {
    .data_offer = dd_data_offer,
    .enter = dd_enter,
    .leave = dd_leave,
    .motion = dd_motion,
    .drop = dd_drop,
    .selection = dd_selection,
};

static void maybe_create_data_device(void)
{
    if (W.data_dev || !W.data_mgr || !W.seat)
        return;
    W.data_dev = wl_data_device_manager_get_data_device(W.data_mgr, W.seat);
    if (W.data_dev) {
        wl_data_device_add_listener(W.data_dev, &data_device_listener, NULL);
        klog(L_DEBUG, "clipboard: data device ready");
    }
}

/* Copy the current selection to the clipboard as a text/plain data source. ONE owner per copy: the file
   manager's preview pane when it holds a selection, otherwise the session. Both can be on screen at once,
   so the order of these two branches IS the rule, and the log line names the owner that served the bytes. */
static void selection_copy(uint32_t serial)
{
    if (!W.data_dev)
        return;
    char buf[16400];
    int from_preview = g_fm.on && g_fm.pv_sel_on;
    int n = from_preview ? fm_preview_selection_text(&g_fm, buf, sizeof buf)
                         : (cur_term() ? term_selection_text(cur_term(), buf, sizeof buf) : 0);
    if (n <= 0)
        return;
    free(clip_text);
    clip_text = malloc((size_t)n + 1);
    if (!clip_text) { clip_len = 0; return; }
    memcpy(clip_text, buf, (size_t)n);
    clip_text[n] = 0;
    clip_len = (size_t)n;
    if (clip_source) { wl_data_source_destroy(clip_source); clip_source = NULL; }
    clip_source = wl_data_device_manager_create_data_source(W.data_mgr);
    if (!clip_source)
        return;
    wl_data_source_offer(clip_source, "text/plain");
    wl_data_source_add_listener(clip_source, &source_listener, NULL);
    wl_data_device_set_selection(W.data_dev, clip_source, serial);
    wl_display_flush(W.display);
    klog(L_DEBUG, "selection copied: %d bytes from %s", n,
         from_preview ? "the file manager's preview pane" : "the terminal");
}

/* Ask the clipboard for its text. `why` names the gesture that asked (the middle click or Ctrl+Shift+V),
   because the two paths are otherwise identical, and every early return says why in the log: silently
   doing nothing is how "the middle click is broken" becomes an unanswerable report. */
static int clipboard_paste(const char *why)
{
    if (!W.data_dev) {
        klog(L_DEBUG, "paste: %s — this seat has no clipboard device", why);
        return 0;
    }
    if (paste_fd[0] >= 0) {
        klog(L_DEBUG, "paste: %s — a paste is already in flight", why);
        return 0;
    }
    if (!sel_offer || !sel_offer_text) {
        klog(L_DEBUG, "paste: %s — the clipboard offers no text", why);
        return 0;
    }
    klog(L_INFO, "paste: %s asks the clipboard for its text", why);
    start_paste(sel_offer, sel_offer_mime[0] ? sel_offer_mime : "text/plain");
    return 1;
}

/* Wrapped in bracketed-paste delimiters when the application asked for mode 2004, and each write stays
   under the tty's input buffer so a large paste is never split mid-block. */
static void paste_flush(void)
{
    if (!paste_len)
        return;
    if (fm_cd_state != FMCD_IDLE)
        fm_cd_abort("a paste was inserted at the prompt");
    /* A NUL must never reach the pty: Firefox on Wayland serves UTF-16 code units under a UTF-8 label,
       and bash's bracketed-paste reader stops at the first NUL. */
    if (memchr(paste_buf, 0, paste_len)) {
        size_t cap = paste_len * 3 / 2 + 8;
        char *tmp = malloc(cap);
        int n = tmp ? paste_repair(paste_buf, paste_len, tmp, cap) : -1;
        if (n >= 0) {
            klog(L_DEBUG, "paste: %zu clipboard bytes contained NUL, repaired to %zu",
                 paste_len, (size_t)n);
            free(paste_buf);
            paste_buf = tmp;
            paste_len = (size_t)n;
        } else {
            /* Should not happen (cap is sized to always fit). Never send the NULs: strip them here. */
            free(tmp);
            size_t o = 0;
            for (size_t i = 0; i < paste_len; i++)
                if (paste_buf[i])
                    paste_buf[o++] = paste_buf[i];
            klog(L_WARN, "paste: clipboard block contained NUL bytes; %zu stripped", paste_len - o);
            paste_len = o;
        }
        if (!paste_len)
            return;
    }
    int bracketed = cur_term()->bracketed_paste && !paste_overflow;
    static const char start[] = PASTE_START, end[] = PASTE_END;
    Pty *pp = cur_pty();
    if (bracketed)
        input_write(cur_term(), pp, start, sizeof(start) - 1);
    size_t off = 0;
    while (off < paste_len) {
        size_t chunk = imin_(paste_len - off, (size_t)4096);
        input_write(cur_term(), pp, paste_buf + off, chunk);
        off += chunk;
    }
    if (bracketed)
        input_write(cur_term(), pp, end, sizeof(end) - 1);
    klog(L_DEBUG, "paste: %zu bytes, bracketed %d%s", paste_len, bracketed,
         paste_overflow ? " (overflow: sent raw)" : "");
    paste_len = 0;
    paste_overflow = 0;
}

static void kb_enter(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s,
                     struct wl_array *keys)
{
    (void)d; (void)k; (void)serial; (void)s; (void)keys;
}
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s)
{
    (void)d; (void)k; (void)serial; (void)s;
    /* Focus left the surface. The release of a held key may never be delivered, which would leave a
       repeat running forever against a keyboard the user is no longer using. */
    repeat_stop();
}

static void kb_mod(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t mods_depressed,
                   uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
    (void)d; (void)k; (void)serial;
    if (W.xkb_state)
        xkb_state_update_mask(W.xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static void kb_repeat(void *d, struct wl_keyboard *k, int rate, int delay)
{
    (void)d; (void)k;
    /* Informational only. Kai-Term drives its own repeat from repeat_delay/repeat_rate in the config
       file, so the feel does not depend on what the compositor happens to be configured with; the
       values are logged so the user can match their system settings. */
    klog(L_DEBUG, "compositor repeat_info: rate %d/s delay %d ms (kai-term uses repeat_delay=%d ms, "
         "repeat_rate=%d/s from the config)", rate, delay, conf.repeat_delay, conf.repeat_rate);
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = kb_keymap,
    .enter = kb_enter,
    .leave = kb_leave,
    .key = kb_key,
    .modifiers = kb_mod,
    .repeat_info = kb_repeat,
};

static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t format, int fd, uint32_t size)
{
    (void)d; (void)k; (void)format;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return;
    }
    struct xkb_keymap *km = xkb_keymap_new_from_string(W.xkb_ctx, map,
                                                       XKB_KEYMAP_FORMAT_TEXT_V1,
                                                       XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    close(fd);
    if (!km) {
        klog(L_ERROR, "xkb keymap compile failed");
        return;
    }
    xkb_keymap_unref(W.keymap);
    xkb_state_unref(W.xkb_state);
    W.keymap = km;
    W.xkb_state = xkb_state_new(km);
    /* The state machine was replaced under a possibly-held key: stop the repeat rather than re-send
       an old keycode through a keymap that no longer exists. */
    repeat_stop();
}

static void seat_cap(void *d, struct wl_seat *s, uint32_t cap)
{
    (void)d;
    if ((cap & WL_SEAT_CAPABILITY_POINTER) && !W.pointer) {
        W.pointer = wl_seat_get_pointer(s);
        wl_pointer_add_listener(W.pointer, &pointer_listener, NULL);
        klog(L_DEBUG, "wl_pointer interface version %d (>=5 enables axis_discrete/frame scrolling)",
            wl_proxy_get_version((struct wl_proxy *)W.pointer));
    }
    if ((cap & WL_SEAT_CAPABILITY_KEYBOARD) && !W.keyboard) {
        W.keyboard = wl_seat_get_keyboard(s);
        wl_keyboard_add_listener(W.keyboard, &keyboard_listener, NULL);
    }
    maybe_create_data_device();
}
static void seat_name(void *d, struct wl_seat *s, const char *name) { (void)d; (void)s; (void)name; }
static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_cap,
    .name = seat_name,
};

static void registry_global(void *d, struct wl_registry *reg, uint32_t id,
                            const char *iface, uint32_t version)
{
    (void)d;
    if (!strcmp(iface, wl_compositor_interface.name)) {
        W.comp_ver = imin_(version, 4);
        W.compositor = wl_registry_bind(reg, id, &wl_compositor_interface, W.comp_ver);
    } else if (!strcmp(iface, wl_shm_interface.name)) {
        W.shm = wl_registry_bind(reg, id, &wl_shm_interface, 1);
    } else if (!strcmp(iface, wl_seat_interface.name)) {
        /* Bound at v5 because axis_discrete/axis_source/axis_stop/frame are version-5 events and smooth
           wheel needs them. wl_keyboard has no events above v4, so v5 adds no unhandled keyboard events. */
        W.seat = wl_registry_bind(reg, id, &wl_seat_interface, imin_(version, 5));
        wl_seat_add_listener(W.seat, &seat_listener, NULL);
    } else if (!strcmp(iface, xdg_wm_base_interface.name)) {
        W.wm_base = wl_registry_bind(reg, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(W.wm_base, &wm_base_listener, NULL);
    } else if (!strcmp(iface, wl_data_device_manager_interface.name)) {
        W.data_mgr = wl_registry_bind(reg, id, &wl_data_device_manager_interface, imin_(version, 3));
        maybe_create_data_device();
    }
}
static void registry_global_remove(void *d, struct wl_registry *reg, uint32_t id) { (void)d; (void)reg; (void)id; }
static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

/* ---------------- setup ---------------- */

static int wayland_setup(int init_w, int init_h)
{
    W.display = wl_display_connect(NULL);
    if (!W.display) {
        klog(L_ERROR, "wl_display_connect failed (WAYLAND_DISPLAY set?)");
        return -1;
    }
    W.registry = wl_display_get_registry(W.display);
    wl_registry_add_listener(W.registry, &registry_listener, NULL);
    wl_display_roundtrip(W.display);

    if (!W.compositor || !W.wm_base) {
        klog(L_ERROR, "compositor lacks wl_compositor or xdg_wm_base");
        return -1;
    }

    W.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!W.xkb_ctx) {
        klog(L_ERROR, "xkb_context_new failed");
        return -1;
    }

    W.surface = wl_compositor_create_surface(W.compositor);
    W.xdg_surface = xdg_wm_base_get_xdg_surface(W.wm_base, W.surface);
    xdg_surface_add_listener(W.xdg_surface, &xdg_surface_listener, NULL);
    W.toplevel = xdg_surface_get_toplevel(W.xdg_surface);
    xdg_toplevel_add_listener(W.toplevel, &toplevel_listener, NULL);
    /* app_id is what the compositor matches windows on (niri/Sway rules, taskbar grouping).
       config_default() puts DEF_APP_ID there and refills an empty value, so the fallback here only
       covers a Config that never went through config_default. */
    xdg_toplevel_set_app_id(W.toplevel, conf.app_id[0] ? conf.app_id : DEF_APP_ID);
    klog(L_INFO, "app_id: %s", conf.app_id[0] ? conf.app_id : DEF_APP_ID);
    update_title();   /* DEF_TITLE while there is one session; "Kai-Term [2/3]" once there are several */

    if (W.comp_ver >= 3)
        wl_surface_set_buffer_scale(W.surface, 1);

    W.egl_window = wl_egl_window_create(W.surface, init_w, init_h);
    if (!W.egl_window) {
        klog(L_ERROR, "wl_egl_window_create failed");
        return -1;
    }
    /* conf.fg goes to the renderer because the tab bar's colours are mixed from the CONFIGURED palette: an
       application may repaint its own cells but not the chrome that names its tab (see Render.fg).
       conf.cursor_color goes with it for the same reason — a key that reaches no pixel is not one. */
    if (render_init(&rend, W.display, W.egl_window, init_w, init_h, conf.opacity, conf.bg,
                    conf.fg, conf.cursor_color, conf.selection_color, conf.cursor_width,
                    conf.bell == BELL_VISUAL) < 0) {
        klog(L_ERROR, "render_init failed");
        return -1;
    }

    W.cur_w = W.pw = init_w;
    W.cur_h = W.ph = init_h;

    xdg_toplevel_set_min_size(W.toplevel, 200, 100);
    wl_surface_commit(W.surface);
    wl_display_roundtrip(W.display);
    return 0;
}

static void wayland_destroy(void)
{
    if (paste_fd[0] >= 0) { close(paste_fd[0]); paste_fd[0] = -1; }
    if (paste_buf) { free(paste_buf); paste_buf = NULL; paste_cap = 0; paste_len = 0; }
    if (paste_fd[1] >= 0) { close(paste_fd[1]); paste_fd[1] = -1; }
    if (sel_offer) { wl_data_offer_destroy(sel_offer); sel_offer = NULL; }
    if (clip_source) { wl_data_source_destroy(clip_source); clip_source = NULL; }
    free(clip_text); clip_text = NULL;
    if (W.data_dev) { wl_data_device_destroy(W.data_dev); W.data_dev = NULL; }
    if (W.data_mgr) { wl_data_device_manager_destroy(W.data_mgr); W.data_mgr = NULL; }
    if (W.cursor_theme) wl_cursor_theme_destroy(W.cursor_theme);
    if (W.cursor_surface) wl_surface_destroy(W.cursor_surface);
    if (W.keyboard) wl_keyboard_destroy(W.keyboard);
    if (W.pointer) wl_pointer_destroy(W.pointer);
    if (W.seat) wl_seat_destroy(W.seat);
    if (W.shm) wl_shm_destroy(W.shm);
    xkb_state_unref(W.xkb_state);
    xkb_keymap_unref(W.keymap);
    xkb_context_unref(W.xkb_ctx);
    if (W.toplevel) xdg_toplevel_destroy(W.toplevel);
    if (W.xdg_surface) xdg_surface_destroy(W.xdg_surface);
    if (W.wm_base) xdg_wm_base_destroy(W.wm_base);
    if (W.egl_window) wl_egl_window_destroy(W.egl_window);
    if (W.surface) wl_surface_destroy(W.surface);
    if (W.compositor) wl_compositor_destroy(W.compositor);
    if (W.registry) wl_registry_destroy(W.registry);
    if (W.display) wl_display_disconnect(W.display);
}

/* ---------------- main ---------------- */

/* Flags are per-launch; anything about how the terminal LOOKS belongs in the config files. -c points at
   kai-term.conf and -b at kai-term.bind, each searched for independently. */
static void usage(const char *argv0, FILE *out)
{
    fprintf(out,
            "usage: %s [OPTION]...\n\n"
            "  -e, --exec COMMAND       run COMMAND instead of the default shell\n"
            "  -w, --workingdir DIR     start the shell in DIR (default: $HOME)\n"
            "  -c, --config FILE        read FILE instead of the searched kai-term.conf\n"
            "  -b, --binds FILE         read FILE instead of the searched kai-term.bind\n"
            "      --import-theme FILE  import a theme from another terminal and exit\n"
            "      --force              with --import-theme: overwrite without asking\n"
            "  -d, --debug              enable debug logging\n"
            "  -v, --version            print the version and exit\n"
            "  -h, --help               show this help and exit\n\n"
            "Long options also accept the --name=VALUE form.\n"
            "Settings live in kai-term.conf, key bindings in kai-term.bind; each is read from the first\n"
            "of $XDG_CONFIG_HOME/kai-term, ~/.config/kai-term and /etc/kai-term that holds it, and the\n"
            "built-in defaults cover a file none of those has. Command-line options override everything.\n",
            argv0);
}

/* Matches "--name" or "--name=value"; *valp gets the value of the '=' form, NULL otherwise. */
static int longopt(const char *arg, const char *name, const char **valp)
{
    size_t n = strlen(name);
    if (strncmp(arg, name, n) != 0)
        return 0;
    if (valp)
        *valp = (arg[n] == '=') ? arg + n + 1 : NULL;
    return arg[n] == '\0' || arg[n] == '=';
}

/* Value of an option that takes one: --name=VALUE, else the next argv element. */
static const char *opt_value(const char *inline_val, char **argv, int *i, int argc,
                             const char *flag)
{
    if (inline_val)
        return inline_val;
    if (*i + 1 >= argc) {
        fprintf(stderr, "kai-term: %s needs an argument\n", flag);
        return NULL;
    }
    return argv[++*i];
}

int main(int argc, char **argv)
{
    config_default(&conf);
    const char *opt_config = NULL;
    const char *opt_binds = NULL;
    const char *opt_wdir = NULL;
    const char *opt_import = NULL;
    int opt_force = 0;
    int opt_debug = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *val = NULL;
        if (!strcmp(a, "-h") || longopt(a, "--help", &val)) {
            usage(argv[0], stdout);
            return 0;
        } else if (!strcmp(a, "-v") || longopt(a, "--version", &val)) {
            printf("kai-term 0.1.0 (prototype)\n");
            return 0;
        } else if (!strcmp(a, "-d") || longopt(a, "--debug", &val)) {
            opt_debug = 1;      /* applied AFTER config_load: the CLI must beat `log =` in the config */
        } else if (!strcmp(a, "-e") || longopt(a, "--exec", &val)) {
            const char *v = opt_value(val, argv, &i, argc, a);
            if (!v)
                return 1;
            opt_cmd = v;
        } else if (!strcmp(a, "-w") || longopt(a, "--workingdir", &val)) {
            const char *v = opt_value(val, argv, &i, argc, a);
            if (!v)
                return 1;
            opt_wdir = v;   /* one argv element: spaces/special chars need no extra work */
        } else if (!strcmp(a, "-c") || longopt(a, "--config", &val)) {
            const char *v = opt_value(val, argv, &i, argc, a);
            if (!v)
                return 1;
            opt_config = v;
        } else if (!strcmp(a, "-b") || longopt(a, "--binds", &val)) {
            const char *v = opt_value(val, argv, &i, argc, a);
            if (!v)
                return 1;
            opt_binds = v;
        } else if (longopt(a, "--import-theme", &val)) {
            /* Long form only. `-f` was retired in session 43 and tests/cli_test.sh pins that it is still
               REJECTED, so the new flag must not quietly take its short spelling back. */
            const char *v = opt_value(val, argv, &i, argc, a);
            if (!v)
                return 1;
            opt_import = v;
        } else if (longopt(a, "--force", &val)) {
            opt_force = 1;
        } else {
            /* One generic path for every option Kai-Term does not take, including the flags that were
               dropped in the past: no per-flag explanation, just the message and the option list. */
            fprintf(stderr, "kai-term: unknown option '%s'\n", a);
            usage(argv[0], stderr);
            return 1;
        }
    }

    /* Raise the log level before the config readers run — their own `log =` line is not known yet — then
       settle on the final one (the CLI still beats the file). */
    if (opt_debug)
        kai_term_loglevel = L_DEBUG;

    /* `--import-theme` is a one-shot operation, not a launch: it reads one file, writes one file and
       exits. It runs BEFORE config_load so the settings file's own log lines do not bury the import
       report, and it needs no display. All the translation lives in src/theme.c. */
    if (opt_import) {
        char dest[4096];
        int rc = theme_import(opt_import, dest, sizeof dest, opt_force);
        if (rc == 1) {
            /* Ask — do not silently overwrite and do not silently refuse: "Warning a theme with the same
               name already exist. Overwrite ? [y/N]". No answer to read (closed stdin, a script) is a
               refusal that says how to get the other answer, rather than a guess. */
            fputs("Warning: a theme with the same name already exists. Overwrite? [y/N] ", stdout);
            fflush(stdout);
            char ans[128];
            if (!(fgets(ans, sizeof ans, stdin) && (ans[0] == 'y' || ans[0] == 'Y'))) {
                fputs("kai-term: nothing written. Re-run with --force to overwrite without asking.\n",
                      stderr);
                return 1;
            }
            rc = theme_import(opt_import, dest, sizeof dest, 1);
        }
        if (rc != 0)
            return 1;
        char nm[128];
        theme_name_of_path(dest, nm, sizeof nm);
        printf("imported %s\n  -> %s\nenable it in kai-term.conf:  theme = %s\n", opt_import, dest, nm);
        return 0;
    }

    /* Binds are read second: for a name both files set, kai-term.bind wins. */
    config_load(&conf, opt_config);
    config_load_binds(&conf, opt_binds);
    /* The theme, if the settings file named one, is applied AFTER both files have been read: a colour the user
       wrote in kai-term.conf beats it wherever that line sits, and a colour they never wrote is filled
       in. It must run before term_set_default_colors() and before render_init() clears the window. */
    theme_apply_config(&conf);
    if (opt_debug)
        conf.loglevel = L_DEBUG;
    kai_term_loglevel = conf.loglevel;
    base_font_size = conf.font_size;
    term_set_clear_scrollback(conf.clear_scrollback);
    /* Whether an application may repaint the terminal (OSC 4/10/11/104). Process-wide state in src/vt.c, set
       once here like the ink and paper below; `osc_colors = 0` makes the parser ignore all four. */
    term_set_osc_colors(conf.osc_colors);
    /* Set in src/vt.c before the first Term exists and before render_init(): if the padding margin and
       the blank cells read different colours, the difference draws a frame around the grid. */
    term_set_default_colors(conf.fg, conf.bg);

    /* Where the shell starts: -w DIR beats config working_dir beats $HOME. The path is resolved in the
       PARENT so a bad one warns once instead of failing chdir inside the child. */
    char wdir[4096];
    expand_tilde(opt_wdir && opt_wdir[0] ? opt_wdir : conf.working_dir, wdir, sizeof(wdir));
    if (!wdir[0]) {
        const char *home = getenv("HOME");
        snprintf(wdir, sizeof(wdir), "%s", home && home[0] ? home : ".");
    }
    struct stat wst;
    if (stat(wdir, &wst) != 0 || !S_ISDIR(wst.st_mode)) {
        klog(L_WARN, "working directory '%s' is not a directory, using $HOME", wdir);
        const char *home = getenv("HOME");
        snprintf(wdir, sizeof(wdir), "%s", home && home[0] ? home : ".");
    }
    klog(L_INFO, "shell starts in %s", wdir);

    /* The file manager's model starts in the same directory the shell starts in, so the browser and the
       prompt agree before the user moves either. The worker thread is NOT started here: it comes up the
       first time the band is opened (fm_open), or below when `file_manager = 1` asks for it at startup. */
    fm_init(&g_fm, wdir);
    g_fm.on = conf.fm_on ? 1 : 0;
    /* The shell starts in wdir, so that is where it already is — the first navigation must send a cd,
       a refresh must not. */
    snprintf(fm_last_cd, sizeof fm_last_cd, "%s", g_fm.dir);
    g_fm.preview_on = conf.fm_preview ? 1 : 0;
    g_fm.show_hidden = conf.fm_show_hidden ? 1 : 0;
    g_fm.focus = g_fm.on ? FM_FOCUS_LIST : FM_FOCUS_TERM;

    if (font_init(&font, conf.font, conf.font_size) < 0)
        return 1;

    /* The first session. mux_init applies max_tabs; the tab exists before the window so the initial
       window size can be computed from the configured grid, and its shell is spawned only once the
       grid has the window's real size (after layout_apply below). */
    mux_init(&mux, conf.max_tabs);
    g_cmd = opt_cmd && opt_cmd[0] ? opt_cmd : (conf.shell[0] ? conf.shell : NULL);
    snprintf(g_wdir, sizeof(g_wdir), "%s", wdir);
    /* The integration scripts exist before the first shell starts, because the command line that loads them
       is built per session in tab_spawn(). A failure here is not fatal and not retried: g_intdir stays empty
       and every session is spawned exactly as it was before this feature. */
    if (conf.shell_integration) {
        if (shellint_prepare(g_intdir, sizeof g_intdir) == 0)
            klog(L_INFO, "shell integration: scripts written to %s (fish, bash, zsh)", g_intdir);
        else
            klog(L_WARN, "shell integration: off for this run — the script directory could not be created");
    } else {
        klog(L_INFO, "shell integration: off (shell_integration = 0)");
    }
    if (mux_open(&mux, conf.rows, conf.cols, conf.scrollback) < 0)
        return 1;

    /* Ask for the grid plus the margin on both sides — plus the bar's row and gap when `tab_bar =
       always`, or the grid comes up a row short. layout_initial_size() does that arithmetic. */
    int init_w, init_h;
    layout_initial_size(conf.cols, conf.rows, font.cell_w, font.cell_h, conf.padding, PANE_GAP,
                        conf.tab_bar, conf.fm_on, conf.fm_size, &init_w, &init_h);

    if (wayland_setup(init_w, init_h) < 0)
        return 1;
    layout_apply();        /* the layout, and with it every session's grid, now matches the window */

    /* epoll loop: wayland fd, one pty master per session, timerfd, signalfd(SIGCHLD). It is created
       BEFORE the first shell is spawned because tab_spawn() is what registers the descriptor — the
       same call every later tab uses. */
    int ep = epoll_create1(0);
    g_ep = ep;
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    struct itimerspec its = {
        .it_interval = { .tv_nsec = FRAME_INTERVAL_NS },
        .it_value = { .tv_nsec = FRAME_INTERVAL_NS },
    };
    timerfd_settime(tfd, 0, &its, NULL);

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    int sfd = signalfd(-1, &mask, SFD_NONBLOCK);

    int wfd = wl_display_get_fd(W.display);
    struct epoll_event ev;

    /* one-shot idle timer for a buffered clipboard paste (see paste_watch_arm) */
    pwfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (pwfd >= 0) {
        ev = (struct epoll_event){ .events = EPOLLIN, .data.fd = pwfd };
        epoll_ctl(ep, EPOLL_CTL_ADD, pwfd, &ev);
    }

    /* key repeat: idle here, armed per key press by repeat_start() and re-armed at
       the configured rate by repeat_fire() */
    krfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (krfd >= 0) {
        ev = (struct epoll_event){ .events = EPOLLIN, .data.fd = krfd };
        epoll_ctl(ep, EPOLL_CTL_ADD, krfd, &ev);
    } else {
        klog(L_WARN, "key repeat disabled: timerfd_create: %s", strerror(errno));
    }

    /* the cursor blink: armed by blink_update() from the frame tick, idle until then (a steady cursor
       costs nothing). Same reason krfd is a timerfd: nothing else in the loop wakes on a schedule. */
    cbfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (cbfd >= 0) {
        ev = (struct epoll_event){ .events = EPOLLIN, .data.fd = cbfd };
        epoll_ctl(ep, EPOLL_CTL_ADD, cbfd, &ev);
    } else {
        klog(L_WARN, "cursor blink disabled: timerfd_create: %s", strerror(errno));
    }
    W.cursor_on = 1;                  /* steady until blink_update() decides otherwise */
    rend.cursor_on = 1;
    /* Nothing is hovered at startup, and -1 is what "nothing" means in both places that ask: Render.hover_tab (the
       renderer draws the underline in that pane only) and W.hover_tab (the remembered cell). The zeroed globals
       would otherwise read as "tab 0, cell 0,0", which is a real cell. */
    W.hover_tab = -1;
    rend.hover_tab = -1;

    ev = (struct epoll_event){ .events = EPOLLIN, .data.fd = wfd };
    epoll_ctl(ep, EPOLL_CTL_ADD, wfd, &ev);
    ev = (struct epoll_event){ .events = EPOLLIN, .data.fd = tfd };
    epoll_ctl(ep, EPOLL_CTL_ADD, tfd, &ev);
    ev = (struct epoll_event){ .events = EPOLLIN, .data.fd = sfd };
    epoll_ctl(ep, EPOLL_CTL_ADD, sfd, &ev);

    if (tab_spawn(&mux.tab[0]) < 0)
        return 1;
    klog(L_INFO, "mux: session 1/%d started (pid %d, grid %dx%d, max_tabs %d)",
         mux_count(&mux), (int)mux.tab[0].pty.child, mux.tab[0].term.g.cols,
         mux.tab[0].term.g.rows, mux.max);

    wl_flush();
    W.running = 1;

    /* `file_manager = 1` in the config: the band is up from the first frame, so its first listing is
       requested here — once the worker exists and its result fd is part of the loop below. */
    if (g_fm.on) {
        if (fm_worker_start()) {
            unsigned long seq = 0;
            fm_refresh(&g_fm, &seq);
            fm_ask(FMQ_SCAN, g_fm.dir, seq);
            fm_ask_preview();
        }
        fm_touch();
    }

    static uint8_t rbuf[65536];
    struct epoll_event events[8];

    while (W.running) {
        /* Canonical wayland+epoll pattern: dispatch anything already queued in libwayland's buffer, then prepare
           a read so the later read_events() will not block. Calling read_events() inside a drain loop
           AFTER dispatch blocks on the display fd and starves the timerfd and signalfd. */
        while (wl_display_prepare_read(W.display) != 0) {
            if (wl_display_dispatch_pending(W.display) < 0) {
                W.running = 0;
                break;
            }
        }
        if (!W.running)
            break;
        wl_flush();

        int n = epoll_wait(ep, events, 8, -1);
        if (n < 0) {
            wl_display_cancel_read(W.display);
            if (errno == EINTR)
                continue;
            break;
        }

        int wl_ready = 0;
        for (int i = 0; i < n; i++)
            if (events[i].data.fd == wfd)
                wl_ready = 1;

        /* Finish the wayland read BEFORE any descriptor is handled: while a wl_display_prepare_read() is
           outstanding a swap blocks forever and the terminal freezes (see the frame pacing rules).
           Finishing the read here also means a listener may draw, which is the preferred swap site. */
        if (wl_ready) {
            if (wl_display_read_events(W.display) < 0) {
                W.running = 0;
                break;
            }
        } else {
            /* we prepared a read but epoll woke for a non-wayland fd: release it */
            wl_display_cancel_read(W.display);
        }
        if (wl_display_dispatch_pending(W.display) < 0) {
            W.running = 0;
            break;
        }

        for (int i = 0; i < n && W.running; i++) {
            int fd = events[i].data.fd;
            if (fd == wfd) {
                /* already read and dispatched above */
            } else if (tab_for_fd(fd) >= 0) {
                int ti = tab_for_fd(fd);
                Tab *tb = &mux.tab[ti];
                for (;;) {
                    ssize_t r = read(tb->pty.mfd, rbuf, sizeof(rbuf));
                    if (r > 0) {
                        /* The parser addresses rows relative to the LIVE view, so a scrolled-back view
                           would be overwritten: return the view to the live anchor first. */
                        if (tb->term.g.scroll_off != tb->term.g.live_off)
                            snap_to_live(&tb->term);
                        term_parse(&tb->term, rbuf, (size_t)r);
                        /* The file manager's clear -> `cd` -> re-populate machine advances only on this
                           event: what the shell printed is the only proof that an erase or a `cd` landed. */
                        fm_cd_advance(ti, 1);
                        /* Drain any terminal query responses (CPR, DA) queued by the parser */
                        if (tb->term.resp_len > 0) {
                            pty_write(&tb->pty, tb->term.resp, (size_t)tb->term.resp_len);
                            tb->term.resp_len = 0;
                        }
                        /* An application changed the default ink or paper (OSC 10/11). vt.c did this
                           session's cells; the other sessions, the renderer and conf still need it. */
                        if (tb->term.colors_dirty) {
                            uint32_t of = tb->term.old_def_fg, ob = tb->term.old_def_bg;
                            uint32_t nf = term_default_fg(),   nb = term_default_bg();
                            mux_recolor_defaults(&mux, of, ob, nf, nb);
                            conf.fg = nf;
                            conf.bg = nb;
                            rend.fg = nf;
                            rend.bg = nb;
                            tb->term.colors_dirty = 0;
                            W.dirty = 1;
                            klog(L_INFO, "display: the window's ink and paper are now #%06x / #%06x — "
                                 "an application set them, not kai-term.conf", nf & 0xffffff, nb & 0xffffff);
                        }
                        /* A timestamp is all the renderer needs to fade a flash. The flag is cleared
                           because leaving it set would re-arm the flash on every later read. */
                        if (tb->term.bell_rang) {
                            tb->term.bell_rang = 0;
                            if (bell_is_ours((long long)mono_ms(), tb->synth_ms, BELL_OURS_MS)) {
                                /* The program rang this at Kai-Term's request: a click became cursor-key
                                   presses and this is the answer to them. The BEL is real and the flag is
                                   cleared either way — only the flash is skipped. */
                                klog(L_DEBUG, "bell: session %d/%d rang, but that answers the cursor keys a "
                                     "click became — no flash", ti + 1, mux_count(&mux));
                            } else {
                                tb->bell_ms = (long long)mono_ms();
                                if (layout_pane_of_tab(&g_layout, ti) >= 0)
                                    W.dirty = 1;
                                klog(L_DEBUG, "bell: session %d/%d rang%s", ti + 1, mux_count(&mux),
                                     conf.bell == BELL_VISUAL ? " — its pane flashes" : " (bell = off: nothing drawn)");
                            }
                        }
                        if (ti == mux_active(&mux) || layout_pane_of_tab(&g_layout, ti) >= 0) {
                            /* The visible session, or one drawn NEXT TO it in side-by-side view: the picture
                               changed, so redraw. A pane the user can already see needs no unread mark. */
                            W.dirty = 1;
                        } else if (mux_touch(&mux, ti)) {
                            /* Nothing shows this session: do not touch its scroll position, but mark it
                               in the bar and the title, once, so the user can find the session that just
                               finished something. */
                            update_title();
                            if (g_layout.bar_on)
                                W.dirty = 1;
                        }
                        if (r < (ssize_t)sizeof(rbuf))
                            break;
                    } else if (r == 0 || (r < 0 && errno != EINTR && errno != EAGAIN)) {
                        klog(L_INFO, "mux: the pty of tab %d/%d closed", ti + 1, mux_count(&mux));
                        tab_close(ti);   /* sets W.running = 0 when it was the last one */
                        break;
                    } else {
                        break;
                    }
                }
            } else if (fd == tfd) {
                uint64_t ticks;
                (void)!read(tfd, &ticks, sizeof(ticks));
                /* The blink decision is re-read here because this is the one place that runs on a
                   schedule: a tab switch, a focus change, DEC 25l or an application's DECSCUSR can all
                   change it, and blink_update() only touches the timer when the answer actually moved. */
                blink_update();
                /* A follow sequence whose shell never answers has no output to advance on, so the
                   deadline is checked here too. */
                fm_cd_tick();
                /* A bell flash fades on a schedule, so it is kept alive here too — see bell_tick(). */
                bell_tick();
                /* A click that landed on a web address opens once the double-click window has passed — see
                   url_tick(). No redraw is implied: nothing about it is drawn. */
                url_tick();
                /* Draw when something changed and no frame callback is in flight: with frame_cb == NULL
                   the compositor has already shown the last buffer, so the swap is least likely to
                   block. */
                if ((W.dirty || W.resized) && !W.frame_cb)
                    redraw();
            } else if (fd == sfd) {
                struct signalfd_siginfo si;
                while (read(sfd, &si, sizeof(si)) == (ssize_t)sizeof(si)) {
                    if (si.ssi_signo != SIGCHLD)
                        continue;
                    /* Several shells may be running and the one that exited is not necessarily the
                       visible one, so reap by pid and match the pid to a tab. tab_close() drops that
                       tab's descriptor; the application only exits when the last tab is gone. */
                    int st;
                    pid_t done;
                    while ((done = waitpid(-1, &st, WNOHANG)) > 0) {
                        int ti = tab_for_child(done);
                        if (ti >= 0) {
                            klog(L_INFO, "mux: the shell of tab %d/%d exited (pid %d)",
                                 ti + 1, mux_count(&mux), (int)done);
                            mux.tab[ti].pty.child = 0;  /* reaped: tab_close must not kill it again */
                            tab_close(ti);
                        }
                        if (!W.running)
                            break;
                    }
                    if (!W.running)
                        break;
                }
            } else if (paste_fd[0] >= 0 && fd == paste_fd[0]) {
                /* Accumulate the clipboard text; it is written to the PTY as ONE bracketed block
                   once the source closes the pipe (see paste_flush). */
                char pbuf[4096];
                int done = 0;
                while (!done) {
                    ssize_t r = read(paste_fd[0], pbuf, sizeof(pbuf));
                    if (r > 0) {
                        if (paste_len + (size_t)r > (size_t)PASTE_BUF_MAX) {
                            paste_overflow = 1;
                            klog(L_WARN, "paste too large (> %zu bytes): sending it unwrapped",
                                 (size_t)PASTE_BUF_MAX);
                        } else if (paste_len + (size_t)r > paste_cap) {
                            size_t want = paste_cap ? paste_cap : 8192;
                            while (want < paste_len + (size_t)r)
                                want *= 2;
                            char *nb = realloc(paste_buf, want);
                            if (!nb) {
                                klog(L_WARN, "paste: out of memory, paste dropped");
                                paste_len = 0;
                                done = 1;
                                break;
                            }
                            paste_buf = nb;
                            paste_cap = want;
                        }
                        if (!paste_overflow) {
                            memcpy(paste_buf + paste_len, pbuf, (size_t)r);
                            paste_len += (size_t)r;
                        }
                        if (r < (ssize_t)sizeof(pbuf))
                            paste_watch_arm();   /* drained for now; the source may still write */
                    } else if (r == 0) {
                        done = 1;          /* EOF: the whole selection has arrived */
                    } else {
                        if (errno == EINTR)
                            continue;
                        if (errno == EAGAIN)
                            break;         /* wait for more via epoll */
                        klog(L_WARN, "paste: read: %s", strerror(errno));
                        done = 1;
                    }
                }
                if (done) {
                    epoll_ctl(ep, EPOLL_CTL_DEL, paste_fd[0], NULL);
                    close(paste_fd[0]); paste_fd[0] = -1;
                    paste_watch_disarm();
                    paste_flush();
                }
            } else if (fm_pipe[0] >= 0 && fd == fm_pipe[0]) {
                /* a listing or a preview finished on the worker thread: install it (an answer about a
                   directory or a file the user already left is dropped by fm_install_*()) */
                fm_drain_results();
            } else if (pwfd >= 0 && fd == pwfd) {
                /* the source went quiet without closing the pipe: deliver what we have */
                uint64_t exp;
                while (read(pwfd, &exp, sizeof(exp)) == (ssize_t)sizeof(exp))
                    ;
                if (paste_fd[0] >= 0) {
                    epoll_ctl(ep, EPOLL_CTL_DEL, paste_fd[0], NULL);
                    close(paste_fd[0]); paste_fd[0] = -1;
                }
                paste_flush();
            } else if (krfd >= 0 && fd == krfd) {
                /* the hold outlasted repeat_delay: re-send the held key (see repeat_start) */
                repeat_fire();
            } else if (cbfd >= 0 && fd == cbfd) {
                /* the half-period elapsed: flip the phase and let the normal frame path draw it. The tick
                   never draws or swaps by itself — see the block at the definition of cbfd. */
                uint64_t exp;
                while (read(cbfd, &exp, sizeof(exp)) == (ssize_t)sizeof(exp))
                    ;
                W.cursor_on = !W.cursor_on;
                W.dirty = 1;
            }
        }
    }

    render_destroy(&rend);
    wayland_destroy();
    /* Every session: close its master (which hangs up the shell), stop the child if it is still running,
       free its grids. The running loop reaps shells through the signalfd; on the way out a WNOHANG
       waitpid is enough and keeps a stubborn child from stalling the exit. */
    for (int i = 0; i < mux_count(&mux); i++) {
        Tab *tb = &mux.tab[i];
        if (tb->pty.mfd >= 0) {
            close(tb->pty.mfd);
            tb->pty.mfd = -1;
        }
        if (tb->pty.child > 0) {
            kill(tb->pty.child, SIGHUP);
            waitpid(tb->pty.child, NULL, WNOHANG);
        }
        term_destroy(&tb->term);
    }
    font_destroy(&font);
    fm_shutdown();
    /* The scripts and every request file go with the process: a leftover request file would let a later
       shell cd into a directory someone asked for in a session that no longer exists. */
    shellint_cleanup(g_intdir);
    close(tfd);
    close(sfd);
    if (pwfd >= 0)
        close(pwfd);
    if (krfd >= 0)
        close(krfd);
    if (cbfd >= 0)
        close(cbfd);
    close(ep);
    klog(L_INFO, "kai-term exiting%s", W.fatal ? " — renderer failure" : "");
    return W.fatal ? 1 : 0;
}
