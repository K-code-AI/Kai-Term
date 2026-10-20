/* Kai-Term — VT/ANSI parser, grid, scrollback. */
#include "kai-term.h"

#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <strings.h>     /* strncasecmp: the X11 `rgb:` prefix is case-insensitive */
#include <zlib.h>

int kai_term_loglevel = L_WARN;

/* forward declarations for terminal ops used before definition */
static void scroll_up(Term *t, int top, int bot);
static void scroll_down(Term *t, int top, int bot);
static void linefeed(Term *t);
static void putch(Term *t, uint32_t cp);
static void cluster_invalidate(Term *t);
static void pen_default(Term *t);
static void alt_screen_enter(Term *t, int save_cursor);
static void alt_screen_leave(Term *t, int restore_cursor);
/* defined in the graphics section below, used by term_init/term_destroy */
static void term_free_images(Term *t);
static void kitty_tx_free(Parser *ps);

void klog(int level, const char *fmt, ...)
{
    static const char *pfx[] = { "error", "warn", "info", "debug" };
    if (level > kai_term_loglevel)
        return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "kai-term %s: ", pfx[level]);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

int utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* ---------------- codepoint predicates ---------------- */

/* Codepoints that can START an emoji grapheme cluster (the anchor of a ZWJ / modifier
   sequence) or take part in one after a ZWJ. Deliberately generous: a codepoint that is not
   really an emoji only costs one HarfBuzz shaping attempt (cached) before the mono fallback. */
int cp_is_emoji(uint32_t cp)
{
    return (cp >= 0x2190 && cp <= 0x2BFF) ||    /* arrows, misc symbols, dingbats, symbols-A */
           (cp >= 0x1F000 && cp <= 0x1FAFF);    /* emoji planes (pictographs, emoticons, ...) */
}

/* Codepoints that never occupy a cell of their own: they modify the emoji (or keycap) written
   just before them. ZWJ additionally opens the cluster for the NEXT emoji base codepoint. */
int cp_is_cluster_extend(uint32_t cp)
{
    return cp == 0x200D ||                      /* zero width joiner */
           (cp >= 0xFE00 && cp <= 0xFE0F) ||    /* variation selectors (VS15/VS16) */
           (cp >= 0x1F3FB && cp <= 0x1F3FF) ||  /* emoji modifier skin tones */
           cp == 0x20E3 ||                      /* combining enclosing keycap */
           (cp >= 0xE0020 && cp <= 0xE007F);    /* tag chars (subdivision flags) */
}

/* Regional indicator letters (U+1F1E6..U+1F1FF). Two of them form ONE flag grapheme cluster
   (U+1F1FA U+1F1F8 = US flag), which is what the color font's flag glyph is keyed on. */
static int cp_is_ri(uint32_t cp)
{
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

/* Two-cell (wide) codepoints: East Asian Wide/Fullwidth plus the Emoji_Presentation ranges,
   so a color emoji reserves two grid slots instead of being squished into one. */
int cp_is_wide(uint32_t cp)
{
    if (cp >= 0x1100 &&
        (cp <= 0x115F || (cp >= 0x2E80 && cp <= 0xA4CF) ||
         (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
         (cp >= 0xFFE0 && cp <= 0xFFE6)))
        return 1;
    /* Emoji_Presentation = Yes (Unicode 15.1) + regional indicators. Coarse on purpose: a
       handful of text-presentation symbols in these blocks become wide, which is what most
       terminals do anyway and never breaks the grid. */
    static const uint32_t emo[][2] = {
        { 0x1F004, 0x1F004 }, { 0x1F0CF, 0x1F0CF }, { 0x1F18E, 0x1F18E },
        { 0x1F191, 0x1F19A }, { 0x1F1E6, 0x1F1FF }, { 0x1F200, 0x1F202 },
        { 0x1F210, 0x1F23B }, { 0x1F240, 0x1F248 }, { 0x1F250, 0x1F251 },
        { 0x1F260, 0x1F265 }, { 0x1F300, 0x1F320 }, { 0x1F32D, 0x1F335 },
        { 0x1F337, 0x1F37C }, { 0x1F37E, 0x1F393 }, { 0x1F3A0, 0x1F3CA },
        { 0x1F3CF, 0x1F3D3 }, { 0x1F3E0, 0x1F3F0 }, { 0x1F3F4, 0x1F3F4 },
        { 0x1F3F8, 0x1F43E }, { 0x1F440, 0x1F440 }, { 0x1F442, 0x1F4FC },
        { 0x1F4FF, 0x1F53D }, { 0x1F54B, 0x1F54E }, { 0x1F550, 0x1F567 },
        { 0x1F57A, 0x1F57A }, { 0x1F595, 0x1F596 }, { 0x1F5A4, 0x1F5A4 },
        { 0x1F5FB, 0x1F64F }, { 0x1F680, 0x1F6C5 }, { 0x1F6CC, 0x1F6CC },
        { 0x1F6D0, 0x1F6D2 }, { 0x1F6D5, 0x1F6D7 }, { 0x1F6DC, 0x1F6DF },
        { 0x1F6EB, 0x1F6EC }, { 0x1F6F4, 0x1F6FC }, { 0x1F7E0, 0x1F7EB },
        { 0x1F7F0, 0x1F7F0 }, { 0x1F90C, 0x1F93A }, { 0x1F93C, 0x1F945 },
        { 0x1F947, 0x1F9FF }, { 0x1FA70, 0x1FA7C }, { 0x1FA80, 0x1FA88 },
        { 0x1FA90, 0x1FABD }, { 0x1FABF, 0x1FACF }, { 0x1FAD0, 0x1FADB },
    };
    if (cp < 0x1F004 || cp > 0x1FADB)
        return 0;
    for (unsigned i = 0; i < sizeof(emo) / sizeof(emo[0]); i++)
        if (cp >= emo[i][0] && cp <= emo[i][1])
            return 1;
    return 0;
}

/* Width of a WHOLE grapheme cluster, not just of its base codepoint. A cluster is wide when
   the base is wide by itself (Emoji_Presentation, CJK, a lone regional indicator) OR when a
   modifier in the cluster switches the base to EMOJI PRESENTATION:
     - U+FE0F (VS16) on an emoji-capable base: U+2764 FE0F is the red emoji heart, U+2764 alone
       is a plain text dingbat. Same for U+263A FE0F, U+1F3F3 FE0F 200D 1F308 (rainbow flag) ...
     - U+20E3 (combining enclosing keycap) on a digit / '#' / '*': the keycap 1\u20E3 is an emoji.
   Without this the base's narrow width is used and a 136x128 color bitmap gets shrunk into a
   single 8px cell instead of reserving the two cells a wide emoji needs. */
int cluster_is_wide(const uint32_t *seq, int n)
{
    if (n <= 0)
        return 0;
    if (cp_is_wide(seq[0]))
        return 1;
    int vs16 = 0, keycap = 0;
    for (int i = 1; i < n; i++) {
        if (seq[i] == 0xFE0F)
            vs16 = 1;
        if (seq[i] == 0x20E3)
            keycap = 1;
    }
    if (!vs16 && !keycap)
        return 0;
    if (cp_is_emoji(seq[0]))
        return 1;
    if (keycap && ((seq[0] >= '0' && seq[0] <= '9') || seq[0] == '#' || seq[0] == '*'))
        return 1;
    return 0;
}

/* The ink and the paper of a cell nothing has painted: DEF_FG/DEF_BG until main.c hands over the
   configured `foreground =` / `background =` values, before the first Term is created. Process-wide file-scope
   state rather than a Term field for the same reason as kai_term_loglevel and term_set_clear_scrollback —
   term_init() memsets the whole Term, and the blank/erase/scroll/reflow paths below have no Config pointer.

   ONE value does two jobs here and they must stay one value:
     - it is the colour pass 1 of render_draw() paints for every never-written cell (the grid's interior);
     - it is the sentinel cell_pristine() compares a cell against to decide it was NEVER written, which is what
       reflow's row_used() measures and what the "no selection band over never-written space" rule asks.
   Give those two jobs different numbers and a blank cell stops being pristine while still looking blank, so
   reflow counts trailing blanks as content and selection bands appear over nothing. And give this value a
   different number from Config.bg — which is what glClear paints the Config.padding margin and the whole-cell
   remainder with — and the difference draws a visible frame around the grid (the s100/s101 user report). */
static uint32_t g_def_fg = DEF_FG, g_def_bg = DEF_BG;

/* The 256-colour palette, process-wide for the same reason as g_def_fg/g_def_bg and the bell of this
   comment: term_init() memsets the whole Term, so palette state kept in a Term is destroyed by every new
   tab and by every `ESC c`. A theme in a file would then apply to the first session only. It is also what
   lets one line change every session: entries 0-15 are the themeable ANSI colours, 16-231 the 6x6x6 cube
   and 232-255 the grey ramp, and those 240 entries are computed here rather than read from anywhere —
   they are identical in every terminal and no theme in the wild touches them.
   Filled on first use, not by a required init call: a caller that forgets gets the built-in palette, not
   zeros, which is the failure that cannot silently change a colour. */
static uint32_t g_palette[256];
static int g_palette_ready;

/* The built-in colour of ONE entry, computed rather than looked up in a second table: OSC 104;N has to put a
   single entry back, and a second copy of the palette is a copy that can drift from the first. */
static uint32_t palette_builtin_entry(int i)
{
    /* xterm 16 base colors, then 6x6x6 cube, then grayscale ramp.
       The cube's six levels are 0, 95, 135, 175, 215, 255 — the set almost every terminal and almost every
       256-colour chart uses, so 196 is red, 46 is green, 226 is yellow and 231 is white. It is written as a
       table rather than a formula on purpose: the formula this function used to have, 55 + 41 * level, gives
       260 at level 5, and 260 << 16 puts 0x04 in the red byte — 91 of the 216 entries came out near-black
       (measured: 196 was ff040000, 226 was ff050400, 231 was ff050504). A table cannot overflow. */
    static const uint32_t base16[16] = {
        0xff000000, 0xffcc5555, 0xff55cc55, 0xffcccc55,
        0xff5555cc, 0xffcc55cc, 0xff55cccc, 0xffcccccc,
        0xff777777, 0xffff5555, 0xff55ff55, 0xffffff55,
        0xff5555ff, 0xffff55ff, 0xff55ffff, 0xffffffff,
    };
    static const int cube6[6] = { 0, 95, 135, 175, 215, 255 };
    if (i < 0 || i > 255)
        return 0xff000000u;
    if (i < 16)
        return base16[i];
    if (i < 232) {
        int j = i - 16;
        int r = cube6[j / 36], g = cube6[(j / 6) % 6], b = cube6[j % 6];
        return 0xff000000u | (uint32_t)(r << 16 | g << 8 | b);
    }
    int v = 8 + (i - 232) * 10;
    return 0xff000000u | (uint32_t)(v << 16 | v << 8 | v);
}

static void palette_fill_builtin(void)
{
    for (int i = 0; i < 256; i++)
        g_palette[i] = palette_builtin_entry(i);
    g_palette_ready = 1;
}

const uint32_t *term_palette(void)
{
    if (!g_palette_ready)
        palette_fill_builtin();
    return g_palette;
}

/* One entry. Out-of-range indices are ignored rather than clamped: a caller that asks for entry 999 has a
   bug, and painting entry 255 with it would hide that bug as a wrong colour. */
void term_set_palette_entry(int idx, uint32_t argb)
{
    if (idx < 0 || idx > 255)
        return;
    term_palette();   /* fill the built-ins first, so setting one entry cannot zero the other 255 */
    g_palette[idx] = argb;
}

/* Back to the built-in palette: what OSC 104 asks for. NOT called by term_init() or by `ESC c` — a new tab
   or a `reset` at the prompt must not wipe a theme the user put in a file (see the comment above). */
void term_palette_reset(void)
{
    palette_fill_builtin();
}

/* One entry back to its built-in colour — `OSC 104;N` resets N alone and leaves the rest of a theme alone.
   Fills the built-in table first so a caller that never touched the palette cannot get zeros. */
void term_palette_entry_reset(int idx)
{
    if (idx < 0 || idx > 255)
        return;
    term_palette();
    g_palette[idx] = palette_builtin_entry(idx);
}

/* Whether an application may repaint the terminal's OWN colours (OSC 4 / 10 / 11 / 104). Process-wide like the
   palette and the two defaults below: it is a property of the window, not of a session. The user asked for the
   capability AND an off switch (s102 question 2) — with `osc_colors = 0` nothing running inside the terminal can
   change a colour, which is the security-relevant half of the feature. */
static int g_osc_colors = 1;

void term_set_osc_colors(int on) { g_osc_colors = on ? 1 : 0; }
int  term_osc_colors(void)       { return g_osc_colors; }

void term_set_default_colors(uint32_t fg, uint32_t bg)
{
    g_def_fg = fg;
    g_def_bg = bg;
}

uint32_t term_default_fg(void) { return g_def_fg; }
uint32_t term_default_bg(void) { return g_def_bg; }

/* Re-point everything one session has ALREADY painted at a new default ink/paper (OSC 10/11).
   This pass is necessary because a Cell stores a RESOLVED colour, not a palette index (see Cell): moving
   g_def_fg/g_def_bg alone would change only FUTURE output, so the screen the user is looking at keeps the old
   colours and the terminal reads as half-themed — the exact complaint a live colour change invites.
   The rule is deliberately dumb: any cell whose fg/bg equals the old default becomes the new default. That one
   pass is also what keeps the s101 invariant alive. A pristine blank cell has fg==old_fg && bg==old_bg, so it
   becomes fg==new_fg && bg==new_bg and cell_pristine() still recognises it. Sweep only the cells that are NOT
   pristine and a blank cell stops being pristine while still looking blank: row_used() counts trailing blanks as
   content, reflow joins lines it must not, and a selection band appears over nothing. A cell an application
   painted with the SAME rgb as the old default by explicit SGR is swept too — Kai-Term stores no "this pen is the
   default" flag, and adding that state to Pen to save that one cell is not worth it.
   Idempotent: running it twice with the same pair changes nothing the second time, which is what lets main.c
   sweep every session without needing to exclude the one that already swept itself. */
static void recolor_grid(Grid *g, uint32_t of, uint32_t ob, uint32_t nf, uint32_t nb)
{
    if (!g->row)
        return;                                  /* no grid parked here (Term.alt when not in the alt screen) */
    for (int i = 0; i < g->rcap; i++) {
        if (!g->row[i])
            continue;
        for (int x = 0; x < g->cols; x++) {
            Cell *c = &g->row[i][x];
            if (c->fg == of) c->fg = nf;
            if (c->bg == ob) c->bg = nb;
        }
    }
}

void term_recolor_defaults(Term *t, uint32_t old_fg, uint32_t old_bg,
                           uint32_t new_fg, uint32_t new_bg)
{
    if (old_fg == new_fg && old_bg == new_bg)
        return;
    recolor_grid(&t->g, old_fg, old_bg, new_fg, new_bg);
    /* The MAIN grid parked while an application owns the screen. It comes back byte-for-byte on exit, so it
       must be swept now or the user leaves vim into a half-recoloured shell. */
    recolor_grid(&t->alt, old_fg, old_bg, new_fg, new_bg);
    if (t->pen.fg == old_fg) t->pen.fg = new_fg;
    if (t->pen.bg == old_bg) t->pen.bg = new_bg;
    /* The prompt snapshot is compared cell-for-cell against the live grid and prompt_cell_same() compares
       fg/bg, so a snapshot left in the old colours would report "the prompt changed" forever and the shell
       follow would stop working for the rest of the session. */
    for (int x = 0; t->prompt_row && x < t->prompt_cols; x++) {
        Cell *c = &t->prompt_row[x];
        if (c->fg == old_fg) c->fg = new_fg;
        if (c->bg == old_bg) c->bg = new_bg;
    }
}

/* ---------------- grid ---------------- */

static Cell *row_new_cols(int cols)
{
    Cell *r = calloc((size_t)cols, sizeof(Cell));
    if (!r)
        abort();
    for (int x = 0; x < cols; x++) {
        r[x].fg = g_def_fg;
        r[x].bg = g_def_bg;
    }
    return r;
}

static Cell *row_new(Grid *g)
{
    return row_new_cols(g->cols);
}

static void row_fillblank(Grid *g, Cell *r, uint32_t bg)
{
    for (int x = 0; x < g->cols; x++) {
        r[x].ch = 0;
        r[x].attr = 0;
        r[x].nextra = 0;   /* a blank cell holds no emoji cluster continuation */
        r[x].fg = g_def_fg;
        r[x].bg = bg;
    }
}

static void row_resize(Grid *g, Cell **rp, int old_cols)
{
    Cell *r = realloc(*rp, (size_t)g->cols * sizeof(Cell));
    if (!r)
        abort();
    *rp = r;
    /* realloc does NOT zero newly grown columns; clear them so they are blank cells
       (otherwise stale heap bytes become garbage ch values -> garbage glyph quads). */
    for (int x = old_cols; x < g->cols; x++) {
        r[x].ch = 0;
        r[x].attr = 0;
        r[x].nextra = 0;
        r[x].fg = g_def_fg;
        r[x].bg = g_def_bg;
    }
    for (int x = 0; x < g->cols; x++)
        if (r[x].bg == 0)
            r[x].bg = g_def_bg;
}

/* Rotate a contiguous range of row[] slots. Both are permutations of the range,
 * so no buffer can ever end up referenced by two slots (which would double-free
 * in grid_destroy). The soft-wrap marks rotate WITH the rows (w may be NULL only
 * for a grid without scrollback bookkeeping, i.e. never in practice): a mark belongs
 * to the line, not to the slot. */
static void rot_left(Cell **a, uint8_t *w, int n)   /* slot i <- old slot i+1; old slot 0 -> slot n-1 */
{
    Cell *first = a[0];
    memmove(a, a + 1, (size_t)(n - 1) * sizeof(Cell *));
    a[n - 1] = first;
    if (!w)
        return;
    uint8_t wf = w[0];
    memmove(w, w + 1, (size_t)(n - 1));
    w[n - 1] = wf;
}

static void rot_right(Cell **a, uint8_t *w, int n)  /* slot i <- old slot i-1; old slot n-1 -> slot 0 */
{
    Cell *last = a[n - 1];
    memmove(a + 1, a, (size_t)(n - 1) * sizeof(Cell *));
    a[0] = last;
    if (!w)
        return;
    uint8_t wl = w[n - 1];
    memmove(w + 1, w, (size_t)(n - 1));
    w[0] = wl;
}

/* Blank a slot's row AND forget its soft-wrap mark: a blank line never continues into the next. */
static void grid_blank_slot(Grid *g, int i, uint32_t bg)
{
    row_fillblank(g, g->row[i], bg);
    g->wrap[i] = 0;
}

/* How much scrollback a viewport of the CURRENT height can actually hold. rcap is fixed for the grid's
   lifetime (rows_at_init + scrollback_max), so a window that becomes TALLER than the initial row count
   eats into the scrollback: the usable limit is rcap - rows, not scrollback_max. Every bound on
   scroll_off/live_off must use this. Using scrollback_max after the viewport grew is what made
   scroll_up blank row[rcap] (heap-buffer-overflow, found by an ASan build of kai-term with `seq 1 5000`:
   30 configured rows -> a 62-row window -> so saturates at 1968 while scrollback_max still says 2000). */
static int sb_limit(const Grid *g)
{
    return imin_(g->scrollback_max, g->rcap - g->rows);
}

void grid_init(Grid *g, int rows, int cols, int scrollback)
{
    g->rows = rows;
    g->cols = cols;
    g->scrollback_max = scrollback;
    g->scroll_off = 0;
    g->live_off = 0;
    g->row_base = 0;
    g->rcap = rows + scrollback;
    g->valid = GRID_VALID;
    g->row = calloc((size_t)g->rcap, sizeof(Cell *));
    if (!g->row)
        abort();
    g->wrap = calloc((size_t)g->rcap, sizeof(uint8_t));
    if (!g->wrap)
        abort();
    for (int i = 0; i < g->rcap; i++)
        g->row[i] = row_new(g);
}

/* Grid layout invariant:
 *   row[0 .. scroll_off-1]                    scrollback (oldest first)
 *   row[scroll_off .. scroll_off+rows-1]      viewport
 *   row[scroll_off+rows .. rcap-1]            free slots (stale buffers, content
 *                                             irrelevant but must be cleared before
 *                                             they re-enter the viewport)
 * with 0 <= scroll_off <= live_off <= sb_limit(g) == min(scrollback_max, rcap - rows),
 * rcap == rows_at_init + scrollback_max (fixed for the grid's lifetime) and
 * scroll_off + rows <= rcap. The sb_limit form matters: growing the viewport shrinks the
 * scrollback that fits, so scrollback_max alone is NOT a safe bound after a grow.
 *
 * live_off is the anchor that follows the newest output: scroll_up/scroll_down advance it
 * together with scroll_off, so a terminal that is simply printing always has
 * scroll_off == live_off and the viewport shows the newest lines. User scrollback navigation
 * moves scroll_off DOWN below live_off (toward the oldest lines); the slots that leave the
 * viewport at the bottom then hold the newest lines rather than being free, which is why any
 * whole-viewport scroll (and a resize) first returns scroll_off to live_off.
 *
 * grid_init allocates all rcap row buffers and they are never freed or reallocated
 * (except column resize); every content-moving operation is a rotation of a
 * contiguous slot range, so row[] stays a permutation of those buffers and each is
 * freed exactly once by grid_destroy.
 */
Cell *grid_vprow(Grid *g, int y)
{
    return g->row[g->scroll_off + y];
}

/* ---------------- reflow (column-count change) ----------------
 *
 * grid_resize used to realloc every row buffer to the new width: narrowing silently DELETED the
 * cells past the new width and widening left blanks, so text never came back. Reflow instead joins
 * the physical rows back into the logical lines the application wrote (using the Grid.wrap marks)
 * and re-breaks them at the new width, so a width change is loss-free and reversible.
 *
 * Rules:
 *  - the content is slots [0, used), used == scroll_off + rows; the caller has snapped scroll_off to
 *    live_off, so everything above the viewport is scrollback and everything below is free space;
 *  - a logical line is a maximal run of slots whose mark is set (row i continues into row i+1);
 *  - a wide glyph is an anchor cell followed by an ATTR_WIDE continuation cell: a chunk boundary is
 *    pulled back so the pair is never split (the whole glyph moves to the next line);
 *  - re-breaking can need MORE slots than the grid has (narrowing). The OLDEST rows are evicted,
 *    exactly like ordinary scrollback overflow, so the newest content always survives;
 *  - every produced row buffer is freshly allocated at the new width and all the old ones are freed,
 *    so row[] stays a permutation of exactly rcap buffers (tests/dbg.c asserts that invariant).
 */
/* A cell that was never written to and carries no colour of its own. */
static int cell_pristine(const Cell *c)
{
    return c->ch == 0 && c->nextra == 0 && c->attr == 0 && c->fg == g_def_fg && c->bg == g_def_bg;
}

/* How many leading cells of a row take part in its logical line: the trailing never-written blanks
   do not. Dropping them when rows are joined back together matters twice — it stops a 5-character
   line from becoming two physical rows in a narrow window, and it removes the padding a wide glyph
   leaves at the end of a row when an earlier reflow moved the whole pair to the next line (without
   this, re-widening turned that padding into a literal blank cell in the middle of the text). */
static int row_used(const Cell *r, int cols)
{
    int end = cols;
    while (end > 0 && cell_pristine(&r[end - 1]))
        end--;
    return end;
}

static void grid_reflow(Grid *g, int new_cols, int *slot, int *col)
{
    int old_cols = g->cols;
    int used = g->scroll_off + g->rows;
    if (used > g->rcap)
        used = g->rcap;

    /* Output is built oldest-first. At most rcap rows survive (older ones are evicted), and the kept
       window is compacted back to the front once head slots are dead, so a fixed 2*rcap array is
       enough even when narrowing multiplies the line count many times over. */
    int cap = 2 * g->rcap + 2;
    Cell **out = malloc((size_t)cap * sizeof(Cell *));
    uint8_t *ow = malloc((size_t)cap);
    int *ulen = malloc((size_t)g->rcap * sizeof(int));
    if (!out || !ow || !ulen)
        abort();
    int n = 0, head = 0;

    for (int i = 0; i < used; i++)
        ulen[i] = row_used(g->row[i], old_cols);

    /* Locate the tracked cell (the cursor) in logical coordinates: which logical line, and which
       column of the joined line. The join uses the same ulen values the chunker walks below, so the
       two agree by construction. */
    int cur_logical = -1, cur_lcol = -1;
    if (slot && *slot >= 0 && *slot < used) {
        int L = 0, base = 0;
        for (int i = 0; i < *slot; i++) {
            base += ulen[i];
            if (i + 1 < used && !g->wrap[i]) {
                L++;
                base = 0;
            }
        }
        cur_logical = L;
        cur_lcol = base + imin_(*col, ulen[*slot]);
    }
    int out_row = -1, out_col = 0;      /* where that cell lands in the new grid */

    int L = 0;
    for (int i = 0; i < used; ) {
        int j = i;
        while (j + 1 < used && g->wrap[j])
            j++;
        int total = 0;
        for (int k = i; k <= j; k++)
            total += ulen[k];

        if (total == 0) {
            /* a completely blank logical line: it still needs exactly one blank row */
            if (cur_logical == L && out_row < 0) {
                out_row = n;
                out_col = imin_(cur_lcol, new_cols - 1);
            }
        }

        int last = -1, last_x = 0, pos = 0;
        int sk = i, sc = 0;                     /* source cell: slot sk, column sc */
        do {
            Cell *nr = row_new_cols(new_cols);
            int cstart = pos, x = 0;
            while (x < new_cols && pos < total) {
                /* Wide pair guard: if this cell would be the chunk's LAST one and the cell after it
                   is that cell's ATTR_WIDE continuation, stop here instead — the whole glyph moves to
                   the next line rather than being cut in half. (x > 0 keeps progress guaranteed at
                   new_cols == 1, where a wide glyph cannot fit anyway.) */
                if (x > 0 && x + 1 >= new_cols && pos + 1 < total) {
                    int nk = sk, nc = sc + 1;
                    while (nk < j && nc >= ulen[nk]) { nk++; nc = 0; }
                    if (g->row[nk][nc].attr & ATTR_WIDE)
                        break;
                }
                nr[x++] = g->row[sk][sc];
                pos++;
                sc++;
                while (sk < j && sc >= ulen[sk]) { sk++; sc = 0; }
            }
            for (int k = x; k < new_cols; k++) {  /* calloc blanked them; only the pen is set */
                nr[k].fg = g_def_fg;
                nr[k].bg = g_def_bg;
            }
            if (cur_logical == L && out_row < 0 && cstart <= cur_lcol && cur_lcol < pos) {
                out_row = n;
                out_col = cur_lcol - cstart;
            }
            last = n;
            last_x = x;
            out[n] = nr;
            ow[n] = (pos < total) ? 1 : 0;     /* every chunk but the last continues */
            n++;
            /* eviction: keep only the newest rcap rows (the oldest lines fall off the top). This must
               run for EVERY produced row, blank rows included — otherwise `kept` can exceed rcap and
               the commit below would drop buffers instead of installing them (a leak). */
            if (n - head >= g->rcap) {
                free(out[head]);
                head++;
                if (head >= g->rcap) {      /* compact so the fixed array never overflows */
                    memmove(out, out + head, (size_t)(n - head) * sizeof(Cell *));
                    memmove(ow, ow + head, (size_t)(n - head));
                    if (out_row >= 0)
                        out_row -= head;
                    if (last >= 0)
                        last -= head;
                    n -= head;
                    head = 0;
                }
            }
        } while (pos < total);
        if (cur_logical == L && out_row < 0 && last >= 0) {
            /* the tracked cell sat in trimmed blank space: put it just past the surviving text */
            out_row = last;
            out_col = last_x;
        }
        L++;
        i = j + 1;
    }
    free(ulen);

    /* commit: the old buffers are gone, the new ones fill row[0..kept), the rest are fresh blanks */
    for (int i = 0; i < g->rcap; i++)
        free(g->row[i]);
    g->cols = new_cols;
    int kept = n - head;
    if (kept > g->rcap) {          /* eviction makes this impossible; never leak if it somehow happens */
        for (int i = g->rcap; i < kept; i++)
            free(out[head + i]);
        kept = g->rcap;
    }
    for (int i = 0; i < g->rcap; i++) {
        if (i < kept) {
            g->row[i] = out[head + i];
            g->wrap[i] = ow[head + i];
        } else {
            g->row[i] = row_new(g);
            g->wrap[i] = 0;
        }
    }
    free(out);
    free(ow);

    /* the newest content stays anchored at the bottom of the viewport */
    g->live_off = iclamp_(kept - g->rows, 0, sb_limit(g));
    g->scroll_off = g->live_off;
    /* Re-number the document rows: the newest row keeps its absolute number, so slot kept-1 must
       still be document row row_base + used - 1. Narrowing makes `kept` grow (row_base falls),
       widening merges rows (row_base rises), and evicting the oldest lines is what keeps the
       newest-row identity fixed. Images anchored to a document row therefore keep their distance
       from the newest text across a re-wrap instead of jumping to an arbitrary row. */
    g->row_base += (int64_t)used - kept;

    if (slot) {
        if (out_row >= 0) {
            *slot = out_row;              /* index into the NEW row[] (kept rows start at 0) */
            *col = iclamp_(out_col, 0, new_cols - 1);
        } else {
            *slot = iclamp_(kept - 1, 0, g->rcap - 1);   /* its line was evicted */
            *col = 0;
        }
    }

    if (kept > used)
        klog(L_DEBUG, "reflow: %d -> %d rows at %d cols (%d evicted)", used, kept, new_cols, kept - used);
}

/* Viewport height change: the rows math, shared by the reflowing and the plain resize.
 * cur_slot is the absolute row[] slot of the tracked cell (the cursor), or -1 when the caller has
 * no cursor to preserve. It decides WHICH end of the viewport loses rows on a shrink, and that is
 * not a detail: getting it wrong moves the cursor off the line it sits on, the application then
 * repaints there, and the old copy stays on screen (the zoom leftover-lines bug). */
static void grid_resize_rows(Grid *g, int rows, int cur_slot)
{
    /* Resize math below assumes the slots below the viewport are free, which is only true
       at the live anchor. A resize while scrolled back therefore returns the view to the
       live bottom first (the scrollback content itself is preserved). */
    g->scroll_off = g->live_off;
    if (rows < g->rows) {
        /* Shrink: drop rows from the BOTTOM of the viewport while the cursor still has rows below
         * it, because everything under the cursor is spare — at a shell prompt it is blank. The
         * cursor keeps its slot and its viewport row, so the shell's SIGWINCH repaint lands exactly
         * on its own prompt instead of below it.
         * Once the cursor is on the viewport's bottom row there is nothing spare under it, so the
         * viewport's TOP row is given up instead: it becomes the newest scrollback line (the newest
         * content is what must stay visible), the window slides down one slot, and the cursor's
         * viewport row drops with it, which keeps it on the same content line. When scrollback is
         * full the oldest line is evicted by left-rotating the used region; the evicted buffer
         * lands in the last used slot, where it is cleared and that slot becomes free. */
        while (g->rows > rows) {
            int used = g->scroll_off + g->rows;
            int cy = cur_slot >= 0 ? cur_slot - g->scroll_off : -1;   /* cursor's viewport row */
            if (cy >= 0 && cy < g->rows - 1) {
                /* the viewport's bottom row leaves the screen; its slot becomes free */
                grid_blank_slot(g, used - 1, g_def_bg);
            } else if (g->scroll_off < sb_limit(g)) {
                g->scroll_off++;
                g->live_off = g->scroll_off;
            } else {
                rot_left(g->row, g->wrap, used);
                grid_blank_slot(g, used - 1, g_def_bg);
                g->row_base++;        /* oldest document row evicted (see KImage) */
                g->live_off = g->scroll_off;
            }
            g->rows--;
        }
    } else {
        /* Grow: anchor the live bottom line. A taller viewport fits more scrollback
         * above it, so the live anchor slides UP by the number of added rows (never
         * below 0). If the anchor can supply the extra rows the window slides up in
         * place and the revealed slots already hold the right content; otherwise all
         * scrollback is revealed and the remaining new rows come from free slots at
         * the bottom, which must be cleared. rcap is fixed, so the viewport can never
         * exceed it. */
        if (rows > g->rcap)
            rows = g->rcap;
        int add = rows - g->rows;
        int used = g->scroll_off + g->rows;
        g->live_off = iclamp_(g->live_off - add, 0, imin_(g->scrollback_max, g->rcap - rows));
        if (g->live_off + rows <= g->rcap) {
            g->scroll_off = g->live_off;
            for (int i = used; i < g->scroll_off + rows; i++)
                grid_blank_slot(g, i, g_def_bg);
        } else {
            g->scroll_off = g->rcap - rows;
            for (int i = used; i < g->rcap; i++)
                grid_blank_slot(g, i, g_def_bg);
        }
        g->live_off = iclamp_(g->scroll_off, 0, sb_limit(g));
        g->rows = rows;
    }
}

void grid_resize(Grid *g, int rows, int cols, int *slot, int *col)
{
    rows = iclamp_(rows, 1, 4096);
    cols = iclamp_(cols, 1, 4096);
    /* The reflow needs the live anchor (everything below the viewport must be free space), so a
       width change while scrolled back snaps the view to the live bottom first. Selection is
       viewport-relative and is cleared on any resize (term_resize). */
    g->scroll_off = g->live_off;
    if (cols != g->cols)
        grid_reflow(g, cols, slot, col);
    if (rows == g->rows)
        return;
    /* grid_reflow has already moved *slot to the cursor's slot in the re-wrapped grid, so the row
       math sees the cursor where it actually is now. */
    grid_resize_rows(g, rows, slot ? *slot : -1);
}

void grid_resize_plain(Grid *g, int rows, int cols, int cur_slot)
{
    rows = iclamp_(rows, 1, 4096);
    cols = iclamp_(cols, 1, 4096);
    if (cols != g->cols) {
        /* No reflow: rows are truncated or grown in place (see the header for why the alternate
           screen must not be re-wrapped). Marks of truncated rows are dropped: the surviving part
           of a wrapped line ends at the new margin, so claiming it continues would invent content. */
        int old_cols = g->cols;
        g->cols = cols;
        for (int i = 0; i < g->rcap; i++) {
            row_resize(g, &g->row[i], old_cols);
            if (cols < old_cols)
                g->wrap[i] = 0;
        }
    }
    if (rows == g->rows)
        return;
    grid_resize_rows(g, rows, cur_slot);
}

void grid_destroy(Grid *g)
{
    if (g->valid != GRID_VALID || !g->row)
        return;
    for (int i = 0; i < g->rcap; i++)
        free(g->row[i]);
    free(g->row);
    free(g->wrap);
    memset(g, 0, sizeof(*g));   /* also clears valid: the grid is gone */
}

/* ---------------- mouse button mapping ----------------
   wl_pointer.button delivers the kernel's BTN_* codes, and the terminal protocols number the buttons
   differently: SGR (1006) and legacy X10 both want 0 = left, 1 = middle, 2 = right, while the BTN_*
   values run LEFT 0x110, RIGHT 0x111, MIDDLE 0x112. Switching on the raw codes in their definition
   order therefore reports a right click as the middle button and vice versa — which is exactly the bug
   this function exists to prevent (it made a middle click on the tab bar do nothing and a RIGHT click
   close the session). Kept here, next to the mouse_mode state it belongs to, so tests/test_vt.c can pin
   it: main.c is linked into no test binary. */
int mouse_sgr_button(uint32_t evdev_code)
{
    switch (evdev_code) {
    case EVDEV_BTN_LEFT:   return 0;
    case EVDEV_BTN_MIDDLE: return 1;
    case EVDEV_BTN_RIGHT:  return 2;
    default:               return 3;   /* BTN_SIDE / BTN_EXTRA / anything unknown */
    }
}

/* The wheel code an application that asked for the mouse expects. See the header: 4/5 (which Kai-Term sent
   until s96) is read by ncurses as button 1, so the wheel did nothing in vim, htop, tmux and `nano --mouse`;
   64/65 is what every other terminal sends and what measurably scrolls those programs. */
int mouse_wheel_button(int up)
{
    return up ? 64 : 65;
}

/* ---------------- the wheel and a click, delivered as keystrokes ----------------
   A program that takes over the whole screen (nano, less, man) and never asks for the mouse cannot be
   scrolled by scrollback — its grid has no scrollback (alt_screen_enter() builds it with
   scrollback_max = 0) — and cannot be reported to, because a program with no mouse protocol reads a
   mouse report as TYPED TEXT: measured 2026-10-21 (.scratch/s96/nanoprobe), one ESC[<64;20;5M put
   "64;20;5M" into the user's file and set the title to "Modified". What does move them is the cursor
   keys, which is what kitty and VTE send in this state and what these three functions work out.
   They live in vt.c, next to the state they read, because main.c is linked into no test binary. */
int term_wants_key_mouse(const Term *t)
{
    return t && t->alt_screen && !(t->mouse_mode & (MOUSE_PRESS | MOUSE_DRAG));
}

/* Append one cursor-key press in the form THIS application asked for. `which` is the final byte either
   way — A up, B down, C right, D left. */
static size_t arrow_append(char *out, size_t cap, size_t o, const Term *t, char which)
{
    const char *seq = term_arrow_seq(t, which);
    size_t n = strlen(seq);
    if (!n || o + n + 1 > cap)      /* never fill the caller's buffer right up to its last byte */
        return o;
    memcpy(out + o, seq, n);
    return o + n;
}

/* The three bytes the application in THIS session expects for one cursor key: SS3 (ESC O x) while DECCKM
   is on, CSI (ESC [ x) otherwise. One function because main.c's real arrow keys and the synthesized ones
   must never disagree about which form the program asked for. */
const char *term_arrow_seq(const Term *t, char which)
{
    int app = t && t->app_cursor;
    switch (which) {
    case 'A': return app ? "\x1bOA" : "\x1b[A";
    case 'B': return app ? "\x1bOB" : "\x1b[B";
    case 'C': return app ? "\x1bOC" : "\x1b[C";
    case 'D': return app ? "\x1bOD" : "\x1b[D";
    default:  return "";
    }
}

int term_wheel_arrow_keys(const Term *t, int lines, char *out, size_t cap)
{
    if (!out || cap < 4 || lines == 0 || !term_wants_key_mouse(t))
        return 0;
    long n = lines;
    if (n > TERM_ARROW_MAX)
        n = TERM_ARROW_MAX;
    if (n < -TERM_ARROW_MAX)
        n = -TERM_ARROW_MAX;
    /* lines > 0 means "toward the top of the text", which is the Up key. */
    char which = n > 0 ? 'A' : 'B';
    if (n < 0)
        n = -n;
    size_t o = 0;
    for (long i = 0; i < n; i++)
        o = arrow_append(out, cap, o, t, which);
    out[o] = 0;
    return (int)o;
}

/* Viewport row `row`, read without handing a const Grid to grid_vprow(). Same slot it returns. */
static const Cell *const_vprow(const Term *t, int row)
{
    return t->g.row[t->g.scroll_off + row];
}

/* The last column of viewport row `row` that holds a glyph, or -1 when the row is empty. A space counts
   as empty: nano paints its text rows out to the right margin, so "the last cell that is not blank" is
   the width of the pane and not the end of the line, and a click in the empty space right of a line must
   land at the end of THAT line, not 40 presses further right. A wide glyph's continuation cell is blank,
   so the anchor is what counts. */
static int row_last_col(const Term *t, int row)
{
    const Cell *r = const_vprow(t, row);
    int last = -1;
    for (int x = 0; x < t->g.cols; x++)
        if (r[x].ch && r[x].ch != ' ')
            last = x;
    return last;
}

int term_click_arrow_keys(const Term *t, int col, int row, char *out, size_t cap)
{
    if (!out || cap < 4 || !term_wants_key_mouse(t))
        return 0;
    if (col < 0 || row < 0 || col >= t->g.cols || row >= t->g.rows)
        return 0;

    /* A click right of the line's text means "put me at the end of that line": sending more Right
       presses than the line holds walks into the NEXT line (measured on nano — a click at column 60 of a
       58-column line landed on the row below). Clamping to the last glyph + 1 is the position a program
       leaves its cursor at when the line ends. */
    int last = row_last_col(t, row);
    int want_col = col > last + 1 ? last + 1 : col;
    if (want_col < 0)
        want_col = 0;

    int dy = row - t->cy;
    int dx = want_col - t->cx;
    if (!dy && !dx)
        return 0;
    dy = iclamp_(dy, -TERM_ARROW_MAX, TERM_ARROW_MAX);
    dx = iclamp_(dx, -TERM_ARROW_MAX, TERM_ARROW_MAX);

    /* Vertical first, then horizontal: measured on nano, Up/Down keep the column the cursor is heading
       for, so correcting the column afterwards lands on the clicked cell. The other order lets a
       horizontal move cross a line boundary and land a row away. */
    size_t o = 0;
    for (int i = 0; i < (dy > 0 ? dy : -dy); i++)
        o = arrow_append(out, cap, o, t, dy > 0 ? 'B' : 'A');
    for (int i = 0; i < (dx > 0 ? dx : -dx); i++)
        o = arrow_append(out, cap, o, t, dx > 0 ? 'C' : 'D');
    out[o] = 0;
    return (int)o;
}

/* ---------------- terminal core ---------------- */

static void pen_default(Term *t)
{
    t->pen.fg = g_def_fg;
    t->pen.bg = g_def_bg;
    t->pen.attr = 0;
}

void term_init(Term *t, int rows, int cols, int scrollback)
{
    /* Was there a live Term here to clean up at all? The answer has to be taken BEFORE anything is
       destroyed, because grid_destroy() memsets the Grid and so clears `valid`: the old code re-read
       t->g.valid for the third block, which made that block DEAD — a reset (ESC c) freed the grid but
       leaked the image list, the APC buffer, the kitty chunk assembly and the prompt snapshot. Found by
       LeakSanitizer the day the snapshot became a heap field (s75). The GRID_VALID test is not
       decoration either: term_init also runs on a never-initialized Term, where stack garbage is
       routinely non-zero (measured 32511), and freeing a garbage pointer is a crash. */
    int was_valid = (t->g.valid == GRID_VALID);
    /* If called as a reset (ESC c / RIS) while the alt screen is active, first
       discard the alt grid and restore the main grid so neither is leaked. */
    if (was_valid && t->alt_screen) {
        grid_destroy(&t->g);            /* the alt grid */
        t->g = t->alt;                  /* restore main grid */
        memset(&t->alt, 0, sizeof(t->alt));
        t->alt_screen = 0;
    }
    if (was_valid)
        grid_destroy(&t->g);            /* free old main grid buffers (fixes RIS leak) */
    if (was_valid) {
        /* A real reset: the screen and everything anchored to it goes. */
        term_free_images(t);
        free(t->ps.apc);
        t->ps.apc = NULL;
        kitty_tx_free(&t->ps);
        free(t->prompt_row);        /* the prompt snapshot belongs to the old grid */
        t->prompt_row = NULL;
        t->prompt_cols = 0;
        t->prompt_valid = 0;
    }
    memset(t, 0, sizeof(*t));
    grid_init(&t->g, rows, cols, scrollback);
    t->top = 0;
    t->bot = rows - 1;
    t->show_cursor = 1;
    pen_default(t);
    /* The palette is NOT touched here: it is process-wide state (see g_palette), because term_init() runs
       for every new tab and would otherwise throw away a theme the user configured. */
}

void term_grid_geometry(int win_w, int win_h, int cell_w, int cell_h, int pad,
                        int *cols, int *rows, int *xoff, int *yoff)
{
    /* Defensive: a caller with a not-yet-initialized font must not divide by 0, and a compositor
       can configure a tiny window. */
    if (cell_w < 1) cell_w = 1;
    if (cell_h < 1) cell_h = 1;
    if (win_w < 1)  win_w = 1;
    if (win_h < 1)  win_h = 1;
    if (pad < 0)    pad = 0;

    /* A margin may never eat the last whole cell: at most (window - one cell)/2 per side. Without
       this clamp a 40 px window with padding = 30 would ask for a grid of 0 columns. */
    int mx = imin_(pad, imax_((win_w - cell_w) / 2, 0));
    int my = imin_(pad, imax_((win_h - cell_h) / 2, 0));

    /* The offset is the margin itself; the pixels that do not fill a whole cell are left at the
       right/bottom on purpose. That is the invariant the pointer mapping relies on
       (col = (ptr_x - xoff) / cell_w clamped to cols-1) and the one render.c documents: moving the
       remainder to the top/left would make floor((ptr_y - yoff)/cell_h) name the row BELOW the
       pointer for most of each row's height. */
    if (xoff) *xoff = mx;
    if (yoff) *yoff = my;
    if (cols) *cols = imax_(1, (win_w - 2 * mx) / cell_w);
    if (rows) *rows = imax_(1, (win_h - 2 * my) / cell_h);
}

/* ---------- the shell's input line ----------
   The file manager writes at the shell's prompt (`cd <dir>`, or the path the user picked). Doing that on
   top of a half-typed command mangles the command and then RUNS the mangled result, so main.c asks
   term_at_prompt() first. There is no way to know where a prompt ENDS: the tty echoes what the user
   types, and that echo arrives through the same pty fd as any other output, so "a prompt" and "a prompt
   plus typed text" are the same kind of bytes as far as the terminal is concerned. What CAN be told
   apart is WHO caused the output: main.c calls term_note_input() for every byte it sends, so output that
   arrives while those bytes are outstanding is an echo or a readline redraw, and output that arrives
   while none are is the application's own.

   So: at the end of every output burst, re-take the snapshot (the whole cursor row plus the cursor
   column) UNLESS input_pending is set. Typing therefore never moves the snapshot — it only makes the row
   stop matching it. That catches typed text, a history recall and a paste. It also un-catches them
   correctly: backspacing everything away, or arrowing back down to an empty prompt, redraws the line
   exactly as the shell printed it, and the match comes back. Submitting the line (CR/LF in what we sent)
   clears input_pending, so the shell's next output — its output plus the next prompt — re-takes it.

   The row comparison alone is NOT enough, because an application is not obliged to echo: measured 2026-10-19
   (`.scratch/s77/livegate`, `.scratch/s77/echochk`), interactive bash in a Kai-Term pty echoed nothing at
   all for the characters typed, so the row never changed and the line read as untouched. term_at_prompt()
   therefore also refuses while input_pending is set — Kai-Term knows it has sent bytes that have not been
   submitted, whether or not the application shows them. The window closes in exactly two ways: the payload
   that was sent contains CR/LF/Ctrl+C (submitted or discarded), or an output burst leaves the row matching
   the remembered prompt again, which is the application PROVING the line is empty (readline's `\b \b`
   erase, an arrow-key redraw). An application that shows nothing keeps the window shut, which is the safe
   direction to be wrong in.

   OPENING it, though, has to mean something. Measured 2026-10-20 (`.scratch/s89/bstoprobe`, real shells in a
   real Kai-Term pty): BackSpace on an EMPTY prompt makes bash and sh answer with nothing but a bell (0x07)
   and makes fish answer NOT AT ALL, so no burst ever proved the line empty — the window stayed shut for the
   rest of the session and the file manager's `cd` follow died until the user pressed Enter (the user's
   report, 2026-10-20). So a payload that can only MOVE the cursor or DELETE from the line does not open the
   window when the line is already provably the bare prompt: it cannot turn an empty prompt into a prompt
   holding text, and the row comparison re-checks the rest on every call, so a key that DOES change the row
   or move the cursor still makes term_at_prompt() answer "no". Everything else — text, Tab, Up/Down, the
   history and search bindings, an unknown or truncated sequence — opens it exactly as before, because any of
   those can put characters on a line the application may not be showing. That is what keeps the s86 hole
   shut: the echo of text Kai-Term typed itself may still be on its way. */

/* Can this payload NOT put a character on the shell's input line? A whitelist, so the answer is "it can" for
   anything not recognised: text bytes, UTF-8, a bracketed paste, Tab (completion), Up/Down (history recall),
   Ctrl+R/S (history search), Ctrl+T (transpose), Ctrl+V (quoted insert), Ctrl+Y (yank), Ctrl+_ (readline's
   undo), Insert, PageUp/PageDown (fish binds them to history search), F-keys (user-bindable) and any
   truncated or unrecognised escape sequence all count as able to insert. What does NOT: cursor movement
   (Left/Right/Home/End, in both CSI and SS3 form, with or without modifiers, plus Ctrl+A/B/E/F), deletion
   (BackSpace, Ctrl+H, Delete, Ctrl+D, Ctrl+K/U/W), Ctrl+L (repaint), and a bare Escape. CR/LF/Ctrl+C are
   listed because the caller treats them as submit/discard — they neither open nor close anything here. */
static int payload_cannot_insert(const unsigned char *p, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        unsigned char c = p[i];
        if (c == 0x7f || c == 0x08 || c == 0x01 || c == 0x02 || c == 0x04 || c == 0x05 || c == 0x06 ||
            c == 0x0b || c == 0x0c || c == 0x15 || c == 0x17 ||
            c == '\r' || c == '\n' || c == 0x03)
            continue;
        if (c != 0x1b)
            return 0;                                   /* a character the application inserts */
        if (i + 1 >= len)
            continue;                                   /* the Escape key on its own */
        unsigned char intro = p[i + 1];
        if (intro != '[' && intro != 'O')
            return 0;                                   /* Alt+key, or a sequence we do not know */
        size_t j = i + 2;
        while (j < len && p[j] >= 0x30 && p[j] <= 0x3f)   /* parameter bytes */
            j++;
        while (j < len && p[j] >= 0x20 && p[j] <= 0x2f)   /* intermediate bytes */
            j++;
        if (j >= len || p[j] < 0x40 || p[j] > 0x7e)
            return 0;                                   /* truncated: assume it inserts */
        unsigned char final = p[j];
        if (final == 'D' || final == 'C' || final == 'H' || final == 'F') {
            i = j;
            continue;                                   /* Left / Right / Home / End */
        }
        if (final == '~' && j == i + 3 && p[i + 2] == '3') {
            i = j;
            continue;                                   /* ESC [ 3 ~ = Delete, and nothing else */
        }
        return 0;                                       /* Up, Down, F-keys, Insert, PageUp/Down, ... */
    }
    return 1;
}

void term_note_input(Term *t, const void *buf, size_t len)
{
    if (!t)
        return;
    const unsigned char *p = (const unsigned char *)buf;
    /* Read once, before the loop can change it: "the window is shut AND the row is provably the remembered
       prompt". A movement or erase key arriving in that state leaves nothing that could show up later. */
    int clean_before = term_at_prompt(t);
    int harmless = payload_cannot_insert(p, len);
    for (size_t i = 0; i < len; i++) {
        /* CR or LF submits the line; Ctrl+C discards it. Both end the echo window: what the
           application prints next is output, not an echo of what is on the line. */
        if (p[i] == '\r' || p[i] == '\n' || p[i] == 0x03)
            t->input_pending = 0;
        else if (harmless && clean_before)
            ;                  /* movement/erase on a provably-empty line: nothing to wait for */
        else
            t->input_pending = 1;
    }
}

/* Two cells count as the same for the prompt comparison. A cell never written (ch == 0) and a cell
   holding a space are the same thing on a prompt line: readline erases characters with `\b \b`, which
   leaves literal spaces where the typed text was, and a line cleared that way must read as empty again —
   otherwise backspacing a mistake away would leave the prompt "dirty" forever. Colours, attributes and
   cluster data still have to match, so a line redrawn with different attributes is treated as changed
   (the safe answer: refuse to write there). */
static int prompt_cell_same(const Cell *a, const Cell *b)
{
    uint32_t ac = (a->ch == 0 || a->ch == ' ') ? 0 : a->ch;
    uint32_t bc = (b->ch == 0 || b->ch == ' ') ? 0 : b->ch;
    if (ac != bc || a->fg != b->fg || a->bg != b->bg ||
        a->attr != b->attr || a->nextra != b->nextra)
        return 0;
    for (int i = 0; i < a->nextra; i++)
        if (a->extra[i] != b->extra[i])
            return 0;
    return 1;
}

/* Remember the row the cursor is on. The parser's cy is a live-view row and main.c snaps the view to
   live_off before every parse, which is the same row grid_vprow() names. */
static int prompt_row_same_cells(Term *t);     /* defined below with prompt_row_matches() */
static void prompt_snapshot(Term *t)
{
    int cols = t->g.cols;
    if (t->g.valid != GRID_VALID || cols < 1) {
        t->prompt_valid = 0;
        return;
    }
    if (t->prompt_cols < cols) {
        Cell *grown = realloc(t->prompt_row, (size_t)cols * sizeof(Cell));
        if (!grown) {
            /* Out of memory: say "unknown", which term_at_prompt() answers as "not at a prompt". The
               old buffer is freed so a resize that fails cannot leak the grid it could not grow. */
            free(t->prompt_row);
            t->prompt_row = NULL;
            t->prompt_cols = 0;
            t->prompt_valid = 0;
            return;
        }
        t->prompt_row = grown;
        t->prompt_cols = cols;
    }
    const Cell *row = grid_vprow(&t->g, t->cy);
    if (!row) {
        t->prompt_valid = 0;
        return;
    }
    /* The cursor part of the snapshot means "where the cursor sits when the line is EMPTY" — it is the
       anchor term_prompt_line() measures typed text from and the position term_at_prompt() insists on. So a
       burst that changed the row NOT AT ALL and only moved the cursor must not redefine it: taking the
       snapshot there would make the cursor equal its own anchor again, and a cursor parked INSIDE the
       remembered prompt text would then answer "safe to write here" (and the line would be described from
       the wrong column). Before the s89 input whitelist that case was covered by accident — a movement key
       set input_pending, which kept the snapshot still — and the check at tests/test_vt.c:2245 is what says
       so. The row itself is still re-compared cell for cell, so a burst that really rewrote the prompt (a
       different prompt, a repaint in other colours, a shell that moved its own prompt row) re-takes the
       whole snapshot, anchor included, exactly as before. */
    if (prompt_row_same_cells(t))
        return;
    for (int x = 0; x < cols; x++)
        t->prompt_row[x] = row[x];
    t->prompt_cx = t->cx;
    t->prompt_cy = t->cy;
    t->prompt_valid = 1;
}

/* Is the row under the cursor cell-for-cell the remembered prompt, WITHOUT any word about where the cursor
   is? This is the half that means "the text is the prompt"; prompt_row_matches() adds the cursor half. */
static int prompt_row_same_cells(Term *t)
{
    if (!t || t->g.valid != GRID_VALID || t->alt_screen)
        return 0;                       /* a full-screen app owns the screen, not the prompt */
    if (!t->prompt_valid || !t->prompt_row || t->prompt_cols < t->g.cols)
        return 0;                       /* unknown: refuse to write at a line we cannot describe */
    if (t->cy < 0 || t->cy >= t->g.rows || t->cy != t->prompt_cy)
        return 0;                       /* the prompt row is not the row under the cursor */
    const Cell *row = grid_vprow(&t->g, t->cy);
    if (!row)
        return 0;
    for (int x = 0; x < t->g.cols; x++)
        if (!prompt_cell_same(&row[x], &t->prompt_row[x]))
            return 0;
    return 1;
}

/* Does the row under the cursor still look exactly like the prompt that was last remembered, AND is the
   cursor back where that prompt left it? This says nothing about whether bytes are outstanding — that is
   term_at_prompt()'s job. */
static int prompt_row_matches(Term *t)
{
    if (!prompt_row_same_cells(t))
        return 0;
    if (t->g.scroll_off != t->g.live_off)
        return 0;                       /* the user is looking at history, not at the prompt */
    if (t->cx != t->prompt_cx)
        return 0;                       /* the cursor is inside the prompt text, or past it */
    return 1;
}

int term_at_prompt(Term *t)
{
    /* Two independent reasons to keep off the line: it does not look like the prompt any more, or bytes
       Kai-Term sent have not been submitted (the application may have consumed them without showing
       anything). Both answer "no" when the truth is unknown. */
    if (t->input_pending)
        return 0;
    return prompt_row_matches(t);
}

/* The shared body of term_prompt_line() and term_prompt_line_is(). One implementation so the two cannot
   drift apart on what "the prompt, then typed text ending at the cursor" means; the only thing they
   disagree on is whether the cells RIGHT of the cursor have to match the snapshot, which is what
   strict_right decides (see the two wrappers' comments — one is about to erase the line, the other only
   wants to know whether text it typed itself has landed). */
static int prompt_line_text(Term *t, char *out, size_t cap, int strict_right)
{
    if (!out || cap < 1)
        return 0;
    if (!t || t->g.valid != GRID_VALID || t->alt_screen)
        return 0;                       /* a full-screen app owns the screen, not the prompt */
    if (!t->prompt_valid || !t->prompt_row || t->prompt_cols < t->g.cols)
        return 0;                       /* unknown: we cannot describe the line, so do not touch it */
    if (t->g.scroll_off != t->g.live_off)
        return 0;                       /* the user is looking at history, not at the prompt */
    if (t->cy < 0 || t->cy >= t->g.rows || t->cy != t->prompt_cy)
        return 0;                       /* the line moved, or the cursor left it */
    if (t->cx < 0 || t->cx >= t->g.cols || t->prompt_cx < 0 || t->prompt_cx > t->cx)
        return 0;                       /* cursor left of where the prompt ended: it is mid-line */
    const Cell *row = grid_vprow(&t->g, t->cy);
    if (!row)
        return 0;
    /* The prompt itself must still be what the shell printed. If readline redrew it with different
       attributes the snapshot no longer describes the line, and "everything before the cursor that is
       not the prompt" would include prompt characters. */
    for (int x = 0; x < t->prompt_cx; x++)
        if (!prompt_cell_same(&row[x], &t->prompt_row[x]))
            return 0;
    /* And the cursor must sit at the END of the line's content. Erasing from mid-line (the user pressed
       Left) would leave the text right of the cursor in place, and the `cd` would then be typed in front
       of it and RUN on it — the one thing this feature must never do. That is a requirement on a caller
       that is about to WRITE at this line; a caller that is only checking its own typing did not erase
       anything, and a shell's ghost text right of the cursor (fish's autosuggestion) would otherwise hide
       an answer that is plainly yes. */
    if (strict_right)
        for (int x = t->cx; x < t->g.cols; x++)
            if (!prompt_cell_same(&row[x], &t->prompt_row[x]))
                return 0;

    /* What is between the end of the prompt and the cursor is what the user typed. Wide glyphs own two
       cells (the second is an ATTR_WIDE continuation holding nothing), and a cluster's extra codepoints
       belong to the anchor cell, so re-typing the bytes reproduces the line.
       Built in a local buffer and copied only at the end: a caller that gets 0 must find its buffer
       untouched, and a half-written command in it would be worse than the refusal. */
    char tmp[FM_LINE_MAX];
    size_t n = 0;
    for (int x = t->prompt_cx; x < t->cx; x++) {
        const Cell *c = &row[x];
        if (c->attr & ATTR_WIDE)
            continue;
        uint32_t cps[CLUSTER_MAX + 1];
        int ncp = 0;
        cps[ncp++] = c->ch ? c->ch : ' ';   /* never-written cell == space, as in term_selection_text() */
        for (int k = 0; k < c->nextra && ncp < (int)(sizeof cps / sizeof cps[0]); k++)
            cps[ncp++] = c->extra[k];
        for (int k = 0; k < ncp; k++) {
            char b8[4];
            int m = utf8_encode(cps[k], b8);
            if (m <= 0)
                continue;
            if (n + (size_t)m >= sizeof tmp)
                return 0;   /* refuse rather than hand back half of the user's command */
            memcpy(tmp + n, b8, (size_t)m);
            n += (size_t)m;
        }
    }
    if (n + 1 >= cap)
        return 0;                          /* the caller's buffer is too small — say so, do not truncate */
    tmp[n] = 0;
    memcpy(out, tmp, n + 1);
    return 1;
}

int term_prompt_line(Term *t, char *out, size_t cap)
{
    return prompt_line_text(t, out, cap, 1);
}

int term_prompt_line_typed(Term *t, char *out, size_t cap)
{
    return prompt_line_text(t, out, cap, 0);
}

int term_prompt_line_is(Term *t, const char *text)
{
    if (!text)
        return 0;
    char got[FM_LINE_MAX];
    if (!prompt_line_text(t, got, sizeof got, 0))
        return 0;
    return strcmp(got, text) == 0;
}

void term_resize(Term *t, int rows, int cols)
{
    /* A resize moves and re-wraps rows, so a snapshot taken before it names cells that no longer exist.
       The clean/dirty answer must survive the resize all the same — a user who has typed nothing should
       still be able to use the file manager after dragging the window — so the state is read BEFORE the
       rows move and re-taken AFTER them if it was clean. */
    int was_clean = term_at_prompt(t);
    int old_live = t->g.live_off;
    int old_cy = t->cy;
    int old_cols = t->g.cols;
    /* A DIRTY line is the one the file manager now has to DESCRIBE (term_prompt_line) before it erases
       and re-types it, and opening the file-manager band resizes the grid — measured 2026-10-19: the band
       costs the terminal its rows (154x61 -> 154x49), so the very first step of the user's flow destroyed
       the description it needed three steps later. Keeping it is only honest when the resize did not touch
       the line: the width is unchanged (so grid_resize() re-broke no line), the cursor was ON the snapshot
       row (a wrapped input line is a different matter and stays refused), and the row that ends up under
       the cursor is compared cell for cell with the row that was there before. "Expected to be the same"
       is not evidence — a snapshot naming the wrong row is worse than no snapshot, because the file
       manager would then erase and re-type on top of something else. */
    Cell *saved = NULL;
    if (!was_clean && t->prompt_valid && t->prompt_row && cols == old_cols &&
        old_cy == t->prompt_cy && old_cols > 0) {
        const Cell *row = grid_vprow(&t->g, old_cy);
        if (row) {
            saved = malloc((size_t)old_cols * sizeof(Cell));
            if (saved)
                memcpy(saved, row, (size_t)old_cols * sizeof(Cell));
            else
                t->prompt_valid = 0;      /* cannot verify: answer "unknown", which means "do not touch" */
        }
    }
    /* The cursor travels through the reflow as an absolute row[] slot + column: grid_resize maps it
       to the same cell in the new grid (its logical line and column are preserved, and if the line
       was evicted by narrowing it lands on the newest surviving line). Without reflow the slot does
       not move, so this reduces to the old "keep the cursor on the same content line" rule. */
    int slot = old_live + old_cy, col = t->cx;
    if (t->alt_screen) {
        /* While the alternate screen is active t->g IS that screen and t->alt holds the parked MAIN
           grid — the opposite of what the names suggest. A full-screen application gets SIGWINCH and
           repaints itself, so the alt screen must NOT be re-wrapped (that would scramble vim/htop
           output); the parked main screen, which the user returns to when the app exits, is reflowed
           like any other width change. */
        grid_resize_plain(&t->g, rows, cols, slot);
        grid_resize(&t->alt, rows, cols, NULL, NULL);
    } else {
        grid_resize(&t->g, rows, cols, &slot, &col);
    }
    t->top = 0;
    t->bot = t->g.rows - 1;
    t->cx = iclamp_(col, 0, t->g.cols - 1);
    /* The parser's cursor is a LIVE-view coordinate (main.c snaps the view to live_off before
       every parse), and a resize always leaves the view at the live anchor, so its viewport row is
       its slot minus the anchor. (Clamping cy alone left the cursor stranded mid-screen after a
       window grow.) */
    t->cy = iclamp_(slot - t->g.live_off, 0, t->g.rows - 1);
    t->wrap_next = 0;
    cluster_invalidate(t);
    term_select_clear(t);   /* selection is viewport-relative; drop it on resize */
    if (was_clean) {
        prompt_snapshot(t);
    } else if (saved) {
        const Cell *row = grid_vprow(&t->g, t->cy);
        int same = row != NULL;
        if (row)
            for (int x = 0; same && x < old_cols; x++)
                if (!prompt_cell_same(&row[x], &saved[x]))
                    same = 0;
        if (same)
            t->prompt_cy = t->cy;   /* the line survived the resize unchanged: the snapshot still fits it */
        else
            t->prompt_valid = 0;    /* it moved, was evicted or was rewritten: unknown */
        free(saved);
    } else {
        t->prompt_valid = 0;        /* a width change re-wraps the line: no honest description survives */
    }
}

void term_select_clear(Term *t)
{
    t->sel_active = 0;
    t->sel_af = t->sel_bf = 0;
}

/* Normalize the selection into a reading-order span (r0,c0,f0)..(r1,c1,f1). The f values
   are thousandths of a cell width (0..1000). The anchor may be after the current cell, so
   ordering is by (row, col, frac). */
void term_selection_span(Term *t, SelSpan *s)
{
    s->r0 = t->sel_ay; s->c0 = t->sel_ax; s->f0 = t->sel_af;
    s->r1 = t->sel_by; s->c1 = t->sel_bx; s->f1 = t->sel_bf;
    if (s->r0 > s->r1 ||
        (s->r0 == s->r1 && (s->c0 > s->c1 || (s->c0 == s->c1 && s->f0 > s->f1)))) {
        int tr = s->r0, tc = s->c0, tf = s->f0;
        s->r0 = s->r1; s->c0 = s->c1; s->f0 = s->f1;
        s->r1 = tr;    s->c1 = tc;    s->f1 = tf;
    }
}

/* Covered sub-range of cell (col,row) in cell-fraction units, clamped to [col,col+1].
   Returns 1 when the cell is covered (then *b > *a), 0 otherwise.
   Rows strictly between the span rows are fully covered; the top/bottom rows start/end at
   the sub-cell edges. A click with no movement (start == end) covers the whole cell under
   the pointer, so a single click still selects a glyph. */
int sel_span_cover(const SelSpan *s, int cols, int col, int row, double *a, double *b)
{
    *a = 0.0;
    *b = 0.0;

    if (s->r0 == s->r1 && s->c0 == s->c1 && s->f0 == s->f1) {
        if (row != s->r0 || col != s->c0)
            return 0;
        *a = (double)col;
        *b = (double)col + 1.0;
        return 1;
    }

    double lo, hi;
    if (row < s->r0 || row > s->r1) {
        lo = 1.0;
        hi = 0.0;                       /* outside the span's rows: empty */
    } else {
        lo = (row == s->r0) ? (s->c0 + s->f0 / 1000.0) : 0.0;
        hi = (row == s->r1) ? (s->c1 + s->f1 / 1000.0) : (double)cols;
    }

    double x0 = ((double)col > lo) ? (double)col : lo;
    double x1 = ((double)col + 1.0 < hi) ? (double)col + 1.0 : hi;
    if (x1 <= x0)
        return 0;
    *a = x0;
    *b = x1;
    return 1;
}

/* Band geometry in buffer pixels: the band is ABSOLUTE (a*cw), not relative to the cell
   origin. The renderer must not add the cell origin again — doing so shifts every band one
   cell width to the right, where the next cell's background quad paints over it and only the
   left-most column's highlight survives. */
void sel_band_pixels(const SelSpan *s, int cols, int col, int row, float cw,
                     float *x, float *w)
{
    double a, b;
    *x = 0.0f;
    *w = 0.0f;
    if (!sel_span_cover(s, cols, col, row, &a, &b))
        return;
    *x = (float)a * cw;
    *w = (float)(b - a) * cw;
}

int term_selection_cover(Term *t, int col, int row, double *a, double *b)
{
    if (!t->sel_active) {
        *a = 0.0;
        *b = 0.0;
        return 0;
    }
    SelSpan s;
    term_selection_span(t, &s);
    return sel_span_cover(&s, t->g.cols, col, row, a, b);
}

/* Is glyph (col,row) part of the selection? True when the selection covers part of its
   cell, so the pointer entering a cell selects that glyph and a span ending before a cell
   drops it. */
int term_selected(Term *t, int col, int row)
{
    double a, b;
    return term_selection_cover(t, col, row, &a, &b);
}

/* Should the highlight band actually be PAINTED over cell (col,row)? The caller has already decided
   the cell is inside the selection; this answers "is there anything here to put a highlight on?".
   Rule:
     - ch == 0            -> nothing was ever written here: NO band, so a selection that runs past the
                             end of a line stops at the last real glyph instead of painting the whole
                             remainder of the window.
     - ch == ' '          -> a real space produced by an application: band IS painted (the user asked
                             for it explicitly, and it is what shows a multi-line selection is
                             still connected across the gap).
     - wide continuation  -> ch == 0 but ATTR_WIDE set: this cell belongs to the glyph in the previous
                             column, so it keeps its band, otherwise the right half of a CJK/emoji
                             glyph would lose its highlight.
   The copied text is unaffected: term_selection_text() maps ch == 0 to a space and trims trailing
   spaces per line either way. */
int term_cell_highlightable(Term *t, int col, int row)
{
    if (row < 0 || row >= t->g.rows || col < 0 || col >= t->g.cols)
        return 0;
    const Cell *c = grid_vprow(&t->g, row) + col;
    if (c->attr & ATTR_WIDE)
        return 1;
    return c->ch != 0;
}

/* ---------- what a double / triple click selects (the model's half of step 3) ----------
   main.c detects the gesture (it owns the pointer and the clock); these answer "what would that
   gesture select", in the model, where a test can check it without a display or a mouse. */

/* Is this cell part of a word? The user's rule, verbatim: "a word is everything no separated by a
   space". So the separators are exactly two: a never-written cell (ch == 0) and a real space
   (U+0020). A wide-glyph cell — anchor OR continuation — is always part of a word, which is also what
   keeps a CJK/emoji glyph from ever being split: the continuation belongs to the glyph in the previous
   column, so the run simply covers both. */
static int word_cell(const Cell *c)
{
    if (c->attr & ATTR_WIDE)
        return 1;
    return c->ch != 0 && c->ch != ' ';
}

/* Does this glyph stick to the end of a word instead of belonging to it? The user's rule: "we don't
   take the trailing ',' or '.'", and asked which punctuation to strip, the answer was "any of it".
   ASCII ispunct() covers everything an ASCII word can end with. The explicit list adds the marks a
   terminal actually draws — curly quotes, en/em dashes, ellipsis, full-width CJK punctuation — because
   they stick to a word exactly like a comma does. Anything NOT listed stays part of the word: this
   project has no Unicode punctuation table, and guessing at the rest of Unicode would cut words that
   should stay whole. A word that keeps a mark is a smaller mistake than a word cut in half. */
static int trailing_punct(uint32_t ch)
{
    if (ch < 0x80)
        return ispunct((int)ch) != 0;
    switch (ch) {
    case 0x2018: case 0x2019: case 0x201C: case 0x201D:   /* ‘ ’ “ ” */
    case 0x2013: case 0x2014: case 0x2026:                /* en dash, em dash, ellipsis */
    case 0x3001: case 0x3002:                             /* 、 。 */
    case 0xFF01: case 0xFF0C: case 0xFF0E: case 0xFF1A:   /* ！ ， 。 ： */
    case 0xFF1B: case 0xFF1F:                             /* ； ？ */
        return 1;
    default:
        return 0;
    }
}

/* The word at (col,row): the maximal run of word cells around the clicked cell, then the trailing
   punctuation cut off it. Leading punctuation STAYS (the user's answer: "we don't take the trailing",
   and a path like `/etc/passwd` or a `(paren` keeps its first character). Returns 0 only when
   (col,row) is not a cell at all; a click on a blank returns the single clicked cell, because "the
   double click did nothing" is worse than selecting the blank. */
int term_word_span(Term *t, int col, int row, int *out_c0, int *out_c1)
{
    if (!out_c0 || !out_c1)
        return 0;
    *out_c0 = *out_c1 = col;
    if (row < 0 || row >= t->g.rows || col < 0 || col >= t->g.cols)
        return 0;
    return cell_row_word_span(grid_vprow(&t->g, row), t->g.cols, col, out_c0, out_c1);
}

/* The same rule on ONE row of cells given directly — the seam the file manager's preview pane uses,
   whose pane is a flat Cell array with the same ATTR_WIDE convention as a session grid. One copy of
   the rule on purpose: a second `trailing_punct()` is the drift the project's one-colour-parser rule
   exists to prevent, and the checks in tests/test_vt.c and tests/test_fm.c then pin the same thing. */
int cell_row_word_span(const Cell *rc, int cols, int col, int *out_c0, int *out_c1)
{
    if (!rc || !out_c0 || !out_c1 || cols <= 0 || col < 0 || col >= cols)
        return 0;
    *out_c0 = *out_c1 = col;
    if (!word_cell(&rc[col]))
        return 1;                        /* a blank: the one cell under the pointer */
    int a = col, b = col;
    while (a > 0 && word_cell(&rc[a - 1]))
        a--;
    while (b < cols - 1 && word_cell(&rc[b + 1]))
        b++;
    /* Cut trailing punctuation a GLYPH at a time: a full-width mark is a wide pair, and dropping its
     * anchor while leaving its continuation would put half a glyph outside the selection. Never strip
     * the word down to nothing — a lone `,` double-clicked stays selected. */
    while (b > a) {
        int gstart = b;
        if (rc[b].ch == 0 && (rc[b].attr & ATTR_WIDE) && b > a)
            gstart = b - 1;              /* the pair's anchor holds the codepoint */
        if (!trailing_punct(rc[gstart].ch))
            break;
        if (gstart <= a)
            break;
        b = gstart - 1;
    }
    *out_c0 = a;
    *out_c1 = b;
    return 1;
}

/* The logical line the clicked row belongs to: a soft-wrapped row is ONE line the application never
   saw broken, so a triple click must take every physical row it spans. `wrap[i]` is the mark for the
   line in row SLOT i (the same index row[] uses, hence scroll_off + r) and means "this row was broken
   by the right margin and continues on the next" — measured with .scratch/s113/wrapprobe before this
   was written, because getting the direction of that flag backwards would extend every triple click
   one row in the wrong direction. A hard break (the application's own \n) leaves wrap == 0, so the
   line stops there. */
int term_line_span(Term *t, int row, int *out_r0, int *out_r1)
{
    if (!out_r0 || !out_r1)
        return 0;
    *out_r0 = *out_r1 = row;
    if (row < 0 || row >= t->g.rows)
        return 0;
    int a = row, b = row;
    while (a > 0 && t->g.wrap[t->g.scroll_off + a - 1])
        a--;
    while (b < t->g.rows - 1 && t->g.wrap[t->g.scroll_off + b])
        b++;
    *out_r0 = a;
    *out_r1 = b;
    return 1;
}

/* How many clicks in a row this press is: 1 for a fresh press, the previous count + 1 when the same
   cell was pressed again within CLICK_DCLICK_MS. Kept out of main.c for the same reason the other
   "what does this gesture mean" helpers live here (mouse_sgr_button, term_wants_key_mouse): the
   window's clock and the cell are inputs, and the answer is a rule a test can pin. The subtraction is
   unsigned, so a compositor clock that wraps (Wayland time is 32-bit ms, ~49 days) still compares
   correctly — the same arithmetic src/main.c's file-manager double click already relies on. */
int term_click_count(uint32_t time_ms, uint32_t prev_time_ms, int same_cell, int prev_count)
{
    if (!same_cell || prev_count <= 0)
        return 1;
    if ((uint32_t)(time_ms - prev_time_ms) > (uint32_t)CLICK_DCLICK_MS)
        return 1;
    return prev_count + 1;
}

/* Copy the selected text into out (NUL-terminated), returning its length. Each line has
   trailing spaces trimmed; lines are joined with '\n'. A glyph is copied when the span
   covers its cell (exactly the same rule as the highlight). */
int term_selection_text(Term *t, char *out, size_t cap)
{
    if (!t->sel_active) {
        if (cap)
            out[0] = 0;
        return 0;
    }
    SelSpan s;
    term_selection_span(t, &s);
    if (s.r0 < 0) { s.r0 = 0; s.c0 = 0; s.f0 = 0; }
    if (s.r1 > t->g.rows - 1) { s.r1 = t->g.rows - 1; s.c1 = t->g.cols - 1; s.f1 = 1000; }

    size_t n = 0;
    /* A cell can hold a whole emoji grapheme cluster: the anchor codepoint plus up to
       CLUSTER_MAX continuation codepoints, 4 UTF-8 bytes each, so the per-line scratch buffer
       is sized for the worst case rather than the old 4-bytes-per-cell assumption. */
    size_t linecap = (size_t)t->g.cols * 4u * (size_t)(CLUSTER_MAX + 1) + 1u;
    char *line = malloc(linecap);
    if (!line) {
        if (cap)
            out[0] = 0;
        return 0;
    }
    for (int y = s.r0; y <= s.r1; y++) {
        const Cell *r = grid_vprow(&t->g, y);
        int ln = 0;
        for (int x = 0; x < t->g.cols; x++) {
            double a, b;
            if (!sel_span_cover(&s, t->g.cols, x, y, &a, &b))
                continue;
            const Cell *c = &r[x];
            if (c->attr & ATTR_WIDE)
                continue;              /* continuation of a wide glyph: already copied */
            uint32_t ch = c->ch;
            if (ch == 0) {
                if (ln + 1 < (int)linecap)
                    line[ln++] = ' ';
                continue;
            }
            /* copy the whole cluster (base + ZWJ / variation selector / skin tone / joined
               emoji) so a pasted emoji is still the emoji, not its pieces */
            uint32_t seq[CLUSTER_MAX + 1];
            int nseq = 1;
            seq[0] = ch;
            for (int k = 0; k < (int)c->nextra && nseq <= CLUSTER_MAX; k++)
                seq[nseq++] = c->extra[k];
            for (int k = 0; k < nseq; k++) {
                char b8[4];
                int m = utf8_encode(seq[k], b8);
                if (ln + m < (int)linecap) {
                    memcpy(line + ln, b8, (size_t)m);
                    ln += m;
                }
            }
        }
        while (ln > 0 && line[ln - 1] == ' ')
            ln--;
        if ((int)(n + (size_t)ln) + 1 >= (int)cap) {
            free(line);
            break;
        }
        memcpy(out + n, line, (size_t)ln);
        n += (size_t)ln;
        if (y < s.r1) {
            if ((int)(n + 1) >= (int)cap) {
                free(line);
                break;
            }
            out[n++] = '\n';
        }
    }
    free(line);
    out[n] = 0;
    return (int)n;
}

void term_destroy(Term *t)
{
    if (t->g.valid == GRID_VALID) {
        term_free_images(t);
        free(t->ps.apc);
        t->ps.apc = NULL;
        kitty_tx_free(&t->ps);
        free(t->prompt_row);
        t->prompt_row = NULL;
        t->prompt_cols = 0;
        t->prompt_valid = 0;
    }
    if (t->alt_screen) {
        grid_destroy(&t->g);            /* alt grid */
        grid_destroy(&t->alt);          /* saved main grid */
    } else {
        grid_destroy(&t->g);
    }
}

/* Alternate screen buffer (xterm ?1049 / ?47 / ?1047). The active grid stays t->g so
   every existing t->g reference keeps working; the MAIN grid is parked in t->alt while
   the alt screen is active. The alt grid has no scrollback (scrollback_max 0), so its
   content scrolls in place and is discarded on exit. */
static void alt_screen_enter(Term *t, int save_cursor)
{
    if (t->alt_screen)
        return;
    if (save_cursor) {
        t->alt_saved_cx = t->cx;
        t->alt_saved_cy = t->cy;
    } else {
        t->alt_saved_cx = 0;
        t->alt_saved_cy = 0;
    }
    t->alt = t->g;                                        /* park main grid */
    grid_init(&t->g, t->alt.rows, t->alt.cols, 0);       /* fresh alt grid, no scrollback */
    /* DECCKM belongs to the screen, not to the terminal: xterm saves and restores it with the buffer,
       and an application that never touches ?1h must not inherit the mode the program on the other
       screen left behind. (Every ncurses program here sets it explicitly on entry anyway — nano and less
       both send ESC[?1h right after ?1049h, measured.) */
    t->alt_saved_app_cursor = t->app_cursor;
    t->app_cursor = 0;
    for (int y = 0; y < t->g.rows; y++)
        row_fillblank(&t->g, t->g.row[y], t->pen.bg);
    t->top = 0;
    t->bot = t->g.rows - 1;
    t->cx = 0;
    t->cy = 0;
    t->wrap_next = 0;
    t->alt_screen = 1;
    /* The prompt snapshot is deliberately LEFT ALONE: it describes the MAIN grid, which is being parked
       and will come back byte-for-byte. prompt_row_matches() already refuses while alt_screen is up, so
       keeping it costs nothing and losing it would cost the shell follow for the rest of the session. */
}

static void alt_screen_leave(Term *t, int restore_cursor)
{
    if (!t->alt_screen)
        return;
    grid_destroy(&t->g);                /* discard alt grid */
    t->g = t->alt;                      /* restore main grid */
    memset(&t->alt, 0, sizeof(t->alt));
    t->alt_screen = 0;
    t->app_cursor = t->alt_saved_app_cursor;
    t->alt_saved_app_cursor = 0;
    t->top = 0;
    t->bot = t->g.rows - 1;
    t->cx = restore_cursor ? t->alt_saved_cx : 0;
    t->cy = restore_cursor ? t->alt_saved_cy : 0;
    t->cx = iclamp_(t->cx, 0, t->g.cols - 1);
    t->cy = iclamp_(t->cy, 0, t->g.rows - 1);
    t->wrap_next = 0;
    /* Back from the alternate screen the snapshot is STILL the one taken before the application ran, and
       that is exactly right: the main grid and the cursor are restored byte-for-byte, so the row the
       snapshot describes is the row now on screen. Re-taking it HERE (s76's fix) was wrong in a way only
       the s79 requirement exposed — it recorded the cursor where the user had left it, which made a
       half-typed command look like an EMPTY prompt, and the file manager would then have erased that
       command. s76's real problem was different and is fixed at its source instead: the end-of-burst
       snapshot used to be taken from the ALTERNATE grid while a full-screen app was up, which is why
       invalidating on leave seemed necessary. It is now skipped while the alternate screen is active, so
       the snapshot never describes the wrong grid and never needs to be thrown away. */
}

static void linefeed(Term *t)
{
    cluster_invalidate(t);
    if (t->cy == t->bot)
        scroll_up(t, t->top, t->bot);
    else if (t->cy < t->g.rows - 1)
        t->cy++;
    if (t->newline_mode)
        t->cx = 0;
}

/* Fold cp into the emoji grapheme cluster whose anchor cell was written just before the
   cursor. Returns 1 when cp was consumed: it gets NO cell of its own and the cursor does not
   move, so 👍🏽 stays one cell and 👨\u200d👩\u200d👧\u200d👦 stays one cell pair (2 slots).
   Without this each ZWJ / variation selector / skin tone lands in its own cell and the
   sequence renders as broken separate glyphs. The anchor must still be the cell immediately
   before the cursor on the same row, which is what makes the state safe to keep across a
   write burst and drop as soon as anything else moves the cursor or the rows. */
static int put_cluster_join(Term *t, uint32_t cp)
{
    if (!t->cl_valid)
        return 0;
    if (t->cy != t->cl_cy || t->cx != t->cl_cx + t->cl_w)
        return 0;
    Cell *a = &grid_vprow(&t->g, t->cl_cy)[t->cl_cx];
    if (a->ch == 0 || a->nextra >= CLUSTER_MAX)
        return 0;

    int join_base = t->cl_join && cp_is_emoji(cp);   /* emoji after a ZWJ */
    /* A regional indicator also joins an RI anchor: the PAIR is one flag cluster and the color
       font keys its flag glyph on the pair (shaping one RI alone yields the letter glyph). Only
       the second one joins -- nextra == 0 means the anchor is still a bare RI -- so a third RI
       starts a new cluster, as Unicode requires. */
    int join_ri = cp_is_ri(cp) && cp_is_ri(a->ch) && a->nextra == 0;
    if (!join_base && !join_ri && !cp_is_cluster_extend(cp))
        return 0;

    a->extra[a->nextra++] = cp;
    if (cp == 0x200D)
        t->cl_join = 1;          /* the next emoji base joins this same anchor */
    else if (join_base || join_ri)
        t->cl_join = 0;

    /* The modifier may have just switched the cluster to emoji presentation, which is WIDE
       (U+2764 is 1 cell, U+2764 U+FE0F is 2). The anchor was reserved narrow when its base was
       written, so upgrade it now: the cell right after the anchor becomes the continuation cell
       and the cursor steps over it, exactly as if the wide base had been written that way.
       Only possible while the cursor still sits directly after a 1-cell anchor and the next
       column exists; otherwise the emoji stays in its single cell (shrunk to fit). */
    if (t->cl_w == 1 && t->cx == t->cl_cx + 1 && t->cl_cx + 1 < t->g.cols) {
        uint32_t seq[CLUSTER_MAX + 1];
        int nseq = 1;
        seq[0] = a->ch;
        for (int k = 0; k < (int)a->nextra && nseq <= CLUSTER_MAX; k++)
            seq[nseq++] = a->extra[k];
        if (cluster_is_wide(seq, nseq)) {
            Cell *nxt = &grid_vprow(&t->g, t->cl_cy)[t->cl_cx + 1];
            nxt->ch = 0;
            nxt->fg = a->fg;
            nxt->bg = a->bg;
            nxt->attr = ATTR_WIDE;
            nxt->nextra = 0;
            t->cx++;
            t->cl_w = 2;
            if (t->cx >= t->g.cols)
                t->wrap_next = 1;
        }
    }
    return 1;
}

static void putch(Term *t, uint32_t cp)
{
    if (put_cluster_join(t, cp))
        return;

    if (t->wrap_next && t->cx >= t->g.cols) {
        t->cx = 0;
        /* The line the cursor is leaving was broken by the RIGHT MARGIN, not by the application.
           Mark it BEFORE the linefeed: this is the only information that lets a later width change
           join the two physical rows back into one logical line (reflow). linefeed() may scroll, and
           the scroll rotations move the mark together with its row. */
        t->g.wrap[t->g.scroll_off + t->cy] = 1;
        linefeed(t);
    }
    t->wrap_next = 0;
    if (t->cx >= t->g.cols)
        t->cx = t->g.cols - 1;
    int ax = t->cx, ay = t->cy;   /* anchor cell of the new cluster (viewport coords) */
    Cell *c = &grid_vprow(&t->g, t->cy)[t->cx];
    c->ch = cp;
    c->fg = t->pen.fg;
    c->bg = t->pen.bg;
    c->attr = t->pen.attr & ~(uint32_t)ATTR_WIDE;
    c->nextra = 0;               /* a fresh cell starts a fresh cluster */
    int wide = cp_is_wide(cp);
    int consumed = 1;
    if (wide && t->cx + 1 < t->g.cols) {
        c[1].ch = 0;
        c[1].fg = t->pen.fg;
        c[1].bg = t->pen.bg;
        c[1].attr = ATTR_WIDE;
        c[1].nextra = 0;
        consumed = 2;
        t->cx += 2;
    } else {
        t->cx++;
    }
    if (t->cx >= t->g.cols)
        t->wrap_next = 1;

    /* remember the anchor so a following ZWJ / modifier / joined emoji folds into it */
    t->cl_valid = 1;
    t->cl_cx = ax;
    t->cl_cy = ay;
    t->cl_w = consumed;
    t->cl_join = 0;
}

/* Any cursor jump or row movement invalidates the in-progress cluster: cl_cx/cl_cy are
   viewport coordinates and row rotations move the anchor out from under them. */
static void cluster_invalidate(Term *t)
{
    t->cl_valid = 0;
    t->cl_join = 0;
}

static void scroll_up(Term *t, int top, int bot)
{
    Grid *g = &t->g;
    cluster_invalidate(t);
    int whole = (top == 0 && bot == g->rows - 1);
    /* Whole-viewport scrolling advances the LIVE view. If the user has scrolled back, the
       slots below the viewport hold the newest lines (they are not free), so return the
       window to the live anchor before moving it. Partial-region scrolls never touch
       scrollback and leave the scrolled-back view alone. */
    if (whole && g->scroll_off != g->live_off)
        g->scroll_off = g->live_off;
    int so = g->scroll_off;
    int sbmax = sb_limit(g);        /* rcap - rows, not scrollback_max: see sb_limit() */
    if (whole && sbmax > 0 && so < sbmax) {
        /* Whole viewport, scrollback has room: the window slides down one slot. No
         * content moves - the departing viewport top line already sits in the slot
         * that becomes the newest scrollback line, and the free slot below the
         * window becomes the new blank bottom line. */
        g->scroll_off = so + 1;
        g->live_off = g->scroll_off;
        grid_blank_slot(g, so + g->rows, t->pen.bg);
    } else if (whole && sbmax > 0) {
        /* Whole viewport, scrollback full: left-rotate the used region, evicting the
         * oldest line. Its buffer lands in the last used slot and is cleared there
         * to become the new blank bottom line. */
        int used = so + g->rows;
        rot_left(g->row, g->wrap, used);
        grid_blank_slot(g, used - 1, t->pen.bg);
        g->row_base++;          /* the oldest document row no longer exists (see KImage) */
        g->live_off = g->scroll_off;   /* saturated at sb_limit() */
    } else {
        /* Partial scroll region, or no scrollback configured: rotate inside
         * [top..bot]. The departing top line's buffer lands at the region bottom,
         * where it is cleared to become the new region bottom line. */
        rot_left(g->row + so + top, g->wrap + so + top, bot - top + 1);
        grid_blank_slot(g, so + bot, t->pen.bg);
    }
}

static void scroll_down(Term *t, int top, int bot)
{
    Grid *g = &t->g;
    cluster_invalidate(t);
    int whole = (top == 0 && bot == g->rows - 1);
    if (whole && g->scroll_off != g->live_off)
        g->scroll_off = g->live_off;   /* see scroll_up: the live anchor must be current */
    int so = g->scroll_off;
    if (whole && so > 0) {
        /* Whole viewport with scrollback above it: the window slides up one slot, so
         * the newest scrollback line becomes the viewport top in place and the
         * viewport's bottom line leaves the screen - its slot becomes free and is
         * cleared. Exact inverse of scroll_up's slide branch. */
        g->scroll_off = so - 1;
        g->live_off = g->scroll_off;
        grid_blank_slot(g, so + g->rows - 1, t->pen.bg);
    } else {
        /* Partial region, or nothing above the viewport: rotate inside [top..bot].
         * The region's bottom line leaves the region and its buffer lands at the
         * region top, where it is cleared as the inserted blank line. */
        rot_right(g->row + so + top, g->wrap + so + top, bot - top + 1);
        grid_blank_slot(g, so + top, t->pen.bg);
    }
}

static void erase_cells(Term *t, int y, int x0, int x1)
{
    Cell *r = grid_vprow(&t->g, y);
    for (int x = imax_(x0, 0); x <= imin_(x1, t->g.cols - 1); x++) {
        r[x].ch = 0;
        r[x].attr = 0;
        r[x].nextra = 0;
        r[x].fg = t->pen.fg;
        r[x].bg = t->pen.bg;
    }
}

/* CSI 3 J setting (see kai-term.h). Default ON; main.c applies the configured value. */
static int g_clear_scrollback = 1;

void term_set_clear_scrollback(int on)
{
    g_clear_scrollback = on ? 1 : 0;
}

/* Build the bytes a paste of `text` is sent to the PTY as (see kai-term.h). With bracketed paste mode
   on, the block is delimited by ESC[200~ / ESC[201~: bash/zsh/readline/vim then insert it literally,
   newlines included, instead of letting every CR run a command. The delimiters are written as three
   separate writes by the caller so a huge paste never needs a buffer holding prefix+text+suffix. */
int paste_wrap(int bracketed, const char *text, size_t len, char *out, size_t cap)
{
    size_t need = len;
    if (bracketed)
        need += strlen(PASTE_START) + strlen(PASTE_END);
    if (!out || cap < need)
        return -1;
    size_t o = 0;
    if (bracketed) {
        memcpy(out + o, PASTE_START, strlen(PASTE_START));
        o += strlen(PASTE_START);
    }
    if (text && len)
        memcpy(out + o, text, len);
    o += len;
    if (bracketed) {
        memcpy(out + o, PASTE_END, strlen(PASTE_END));
        o += strlen(PASTE_END);
    }
    return (int)o;
}

/* ---------- clipboard text policy (pure, unit-testable; src/main.c calls these) ----------

   A clipboard source decides, alone, which mime types it advertises and what bytes it serves for
   each. Two independent defects live at that boundary, and both were reported by the user as "a
   stray character when I paste":

   1. WHICH TYPE TO ASK FOR. A source may advertise several text types. The old rule kept the LAST
      one advertised, which is not a preference — it is an accident of the source's enumeration
      order. `clipboard_mime_rank()` makes the choice explicit.

   2. WHAT BYTES COME BACK. Firefox on Wayland serves UTF-16 code units under a UTF-8 label. Written
      to the pty verbatim, the NUL bytes of UTF-16LE arrive as keystrokes: bash's bracketed-paste
      reader stops at the first NUL, so pasting "http://127.0.0.1:8080" leaves a single `h` on the
      line (measured, .scratch/s98/nulprobe), and zsh inserts them and paints every NUL as `^@`
      (`h^@t^@t^@p^@:...`, the user's exact report). A NUL must never reach the pty.

   Both are pure byte functions so the semantics are pinned in tests/test_vt.c without a display. */

/* Preference order for a text mime type: 0 = not text, higher = ask for this one. */
int clipboard_mime_rank(const char *mime)
{
    if (!mime)
        return 0;
    if (!strcmp(mime, "text/plain;charset=utf-8")) return 5;
    if (!strcmp(mime, "UTF8_STRING"))              return 4;
    if (!strcmp(mime, "text/plain"))               return 3;
    if (!strncmp(mime, "text/plain;charset=", 19)) return 2;   /* some other declared charset */
    if (!strncmp(mime, "text/plain", 10))          return 2;   /* text/plain;foo=bar and friends */
    if (!strncmp(mime, "text/", 5))                return 1;   /* text/html, text/rtf: better than nothing */
    return 0;
}

/* Append one code point as UTF-8. Returns bytes written, 0 if cap is short or cp is not encodable. */
static size_t utf8_append(uint32_t cp, char *out, size_t cap)
{
    if (!out || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return 0;
    if (cp < 0x80) {
        if (cap < 1) return 0;
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        if (cap < 2) return 0;
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        if (cap < 3) return 0;
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cap < 4) return 0;
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

int paste_repair(const char *in, size_t len, char *out, size_t cap)
{
    if (!in || !out)
        return -1;
    /* Nothing to do — the overwhelmingly common case — only when the block holds no NUL at all. */
    if (!memchr(in, 0, len)) {
        if (cap < len)
            return -1;
        if (len)
            memcpy(out, in, len);
        return (int)len;
    }

    /* UTF-16 or plain NUL-polluted bytes? A BOM decides outright; without one, the parity of the
       NULs does: UTF-16LE puts a NUL after every ASCII character (odd offsets), UTF-16BE before
       every one of them (even offsets). Anything else is treated as text with stray NULs. */
    int be = 0, le = 0;
    size_t start = 0;
    if (len >= 2 && (unsigned char)in[0] == 0xFF && (unsigned char)in[1] == 0xFE) {
        le = 1; start = 2;
    } else if (len >= 2 && (unsigned char)in[0] == 0xFE && (unsigned char)in[1] == 0xFF) {
        be = 1; start = 2;
    } else {
        size_t nul_odd = 0, nul_even = 0;
        for (size_t i = 0; i < len; i++)
            if (!in[i]) {
                if (i & 1)
                    nul_odd++;
                else
                    nul_even++;
            }
        /* Only a DENSE, one-sided pattern is UTF-16: UTF-16 text carries a NUL in at least every
           other byte. Anything sparser must not be decoded — a lone stray NUL in an otherwise
           ordinary block would turn good text into garbage, which is worse than the NUL we were
           removing. Ambiguous input therefore gets NUL-stripped only. */
        size_t nuls = nul_odd + nul_even;
        if (nuls * 2 >= len) {
            if (nul_odd > nul_even)
                le = 1;
            else if (nul_even > nul_odd)
                be = 1;
        }
    }

    size_t o = 0;
    if (le || be) {
        /* UTF-16 -> UTF-8. Worst case is 2 bytes in -> 3 bytes out, so cap must be >= len/2*3 + 3
           for a full decode; the caller sizes it that way. A short cap is reported, not truncated. */
        for (size_t i = start; i + 1 < len; i += 2) {
            uint32_t hi = le ? ((uint32_t)(unsigned char)in[i] | ((uint32_t)(unsigned char)in[i + 1] << 8))
                             : (((uint32_t)(unsigned char)in[i] << 8) | (uint32_t)(unsigned char)in[i + 1]);
            uint32_t cp = hi;
            if (hi >= 0xD800 && hi <= 0xDBFF) {
                uint32_t lo = 0;
                if (i + 3 < len)
                    lo = le ? ((uint32_t)(unsigned char)in[i + 2] | ((uint32_t)(unsigned char)in[i + 3] << 8))
                            : (((uint32_t)(unsigned char)in[i + 2] << 8) | (uint32_t)(unsigned char)in[i + 3]);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00);
                    i += 2;
                } else {
                    cp = 0xFFFD;
                }
            } else if (hi >= 0xD800 && hi <= 0xDFFF) {
                cp = 0xFFFD;                       /* lone surrogate: one replacement char, not garbage */
            }
            if (!cp)                               /* a NUL code point never reaches the pty */
                continue;
            size_t n = utf8_append(cp, out + o, cap - o);
            if (!n)
                return -1;
            o += n;
        }
        return (int)o;
    }

    /* Not UTF-16: strip the NUL bytes and keep every other byte exactly as it arrived. */
    if (cap < len)
        return -1;
    for (size_t i = 0; i < len; i++)
        if (in[i])
            out[o++] = in[i];
    return (int)o;
}

/* CSI 3 J — "erase saved lines" (the terminfo E3 capability; `clear` emits ESC[2J then ESC[3J).
   It drops the SCROLLBACK and leaves the viewport exactly as it is, so the prompt stays on screen:
   1. the viewport rows move to the top slots of row[] (they keep their buffers),
   2. every remaining row buffer is blanked and sits below the viewport as free space,
   3. scroll_off = live_off = 0, which also snaps a scrolled-back view back to the live bottom.
   row[] must stay a permutation of the allocated row buffers (tests/dbg.c asserts no slot is lost,
   duplicated or NULL), so the rearrangement is done through a scratch pointer array and validated
   before anything is committed. */
static void erase_scrollback(Term *t)
{
    Grid *g = &t->g;
    cluster_invalidate(t);              /* the anchor cell may have just moved rows */
    t->prompt_valid = 0;                /* every row moved; the shell repaints its prompt after clear */
    if (!g_clear_scrollback || g->scrollback_max <= 0)
        return;                         /* nothing retained (e.g. the alt screen) -> nothing to do */
    int so = g->scroll_off;
    if (so == 0) {
        /* no scrollback above the viewport, but rows below it can still hold the newest lines while
           the user is scrolled back; blanking the free slots below is enough */
        for (int y = g->rows; y < g->rcap; y++)
            grid_blank_slot(g, y, t->pen.bg);
        g->live_off = 0;
        return;
    }
    Cell **tmp = malloc((size_t)g->rcap * sizeof(Cell *));
    uint8_t *tmpw = malloc((size_t)g->rcap);
    if (!tmp || !tmpw)
        abort();
    int n = 0;
    for (int y = 0; y < g->rows; y++) {                      /* viewport -> slots 0..rows-1 */
        tmp[n] = g->row[so + y];
        tmpw[n] = g->wrap[so + y];      /* the mark belongs to the line, so it moves with it */
        n++;
    }
    for (int i = 0; i < g->rcap; i++) {                      /* every other buffer, below them */
        if (i < so || i >= so + g->rows) {
            tmp[n] = g->row[i];
            tmpw[n] = g->wrap[i];
            n++;
        }
    }
    if (n != g->rcap) {                                      /* never corrupt the grid on a logic bug */
        free(tmp);
        free(tmpw);
        klog(L_ERROR, "erase_scrollback: row accounting broken (%d/%d)", n, g->rcap);
        return;
    }
    memcpy(g->row, tmp, (size_t)g->rcap * sizeof(Cell *));
    memcpy(g->wrap, tmpw, (size_t)g->rcap);
    free(tmp);
    free(tmpw);
    /* The viewport moved to slots 0..rows-1, i.e. up `so` slots, so the absolute number of slot 0
       moves the other way: the rows that were above the viewport are gone. Anything anchored to
       one of them is dropped by image_prune() at the end of the parse. */
    g->row_base += so;
    for (int y = g->rows; y < g->rcap; y++)
        grid_blank_slot(g, y, t->pen.bg);
    g->scroll_off = 0;
    g->live_off = 0;
    klog(L_DEBUG, "ED 3: dropped %d scrollback rows", so);
}

static void erase_display(Term *t, int mode)
{
    if (mode == 3) {          /* xterm "erase saved lines" (terminfo E3, what `clear` sends) */
        erase_scrollback(t);
        return;
    }
    /* Every row whose cells are erased also loses its soft-wrap mark: after the erase nothing proves
       the line still reaches the margin, and reflow must never join lines that were not wrapped. */
    #define ERASE_ROW(y) do { erase_cells(t, (y), 0, t->g.cols - 1); \
                              t->g.wrap[t->g.scroll_off + (y)] = 0; } while (0)
    if (mode == 0) {
        erase_cells(t, t->cy, t->cx, t->g.cols - 1);
        t->g.wrap[t->g.scroll_off + t->cy] = 0;
        for (int y = t->cy + 1; y < t->g.rows; y++)
            ERASE_ROW(y);
    } else if (mode == 1) {
        for (int y = 0; y < t->cy; y++)
            ERASE_ROW(y);
        erase_cells(t, t->cy, 0, t->cx);
        t->g.wrap[t->g.scroll_off + t->cy] = 0;
    } else {
        for (int y = 0; y < t->g.rows; y++)
            ERASE_ROW(y);
    }
    #undef ERASE_ROW
}

static void erase_line(Term *t, int mode)
{
    if (mode == 0)
        erase_cells(t, t->cy, t->cx, t->g.cols - 1);
    else if (mode == 1)
        erase_cells(t, t->cy, 0, t->cx);
    else
        erase_cells(t, t->cy, 0, t->g.cols - 1);
    /* see erase_display: an erased line no longer provably continues on the next row */
    t->g.wrap[t->g.scroll_off + t->cy] = 0;
}

static void insert_lines(Term *t, int n)
{
    if (t->cy < t->top || t->cy > t->bot)
        return;
    Grid *g = &t->g;
    cluster_invalidate(t);
    int so = g->scroll_off;
    for (int k = 0; k < n; k++) {
        /* Right-rotate [cy..bot]: every line moves down one slot, the region's
         * bottom line leaves the region and its buffer lands at cy, where it is
         * cleared to become the inserted blank line. scroll_off is untouched: IL
         * discards the departing line rather than pushing it into scrollback. */
        rot_right(g->row + so + t->cy, g->wrap + so + t->cy, t->bot - t->cy + 1);
        grid_blank_slot(g, so + t->cy, t->pen.bg);
    }
}

static void delete_lines(Term *t, int n)
{
    if (t->cy < t->top || t->cy > t->bot)
        return;
    Grid *g = &t->g;
    cluster_invalidate(t);
    int so = g->scroll_off;
    for (int k = 0; k < n; k++) {
        /* Left-rotate [cy..bot]: every line moves up one slot, the deleted line's
         * buffer lands at the region bottom, where it is cleared to become the new
         * bottom line. scroll_off is untouched. */
        rot_left(g->row + so + t->cy, g->wrap + so + t->cy, t->bot - t->cy + 1);
        grid_blank_slot(g, so + t->bot, t->pen.bg);
    }
}

/* A blank cell for ICH/DCH shifts. { 0 } zeroes ch/attr/extra/nextra; GCC does not warn on
   the canonical zero initializer, unlike the old { 0, fg, bg, 0 } literal, which left the new
   cluster fields' initializer implicit. */
static Cell blank_cell(Term *t)
{
    Cell c = { 0 };
    c.fg = t->pen.fg;
    c.bg = t->pen.bg;
    return c;
}

static void set_cursor_pos(Term *t, int row, int col)
{
    int top = t->top, bot = t->bot;
    int r = iclamp_(row, top, bot);
    t->cx = iclamp_(col, 0, t->g.cols - 1);
    t->cy = r;
    t->wrap_next = 0;
    cluster_invalidate(t);
}

/* Queue a response string for the PTY; main.c flushes Term.resp after every parse. Kept as a
   function so the graphics code can answer queries too (the macro below used to be the only way). */
static void term_respond(Term *t, const char *fmt, ...)
{
    if (t->resp_len >= (int)sizeof(t->resp) - 64)
        return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(t->resp + t->resp_len, sizeof(t->resp) - (size_t)t->resp_len, fmt, ap);
    va_end(ap);
    if (n > 0)
        t->resp_len += n;
}

static void csi_dispatch(Term *t, int fin)
{
    cluster_invalidate(t);   /* any CSI may move the cursor or the rows */
    /* ps->npar is the INDEX of the last parameter (npar == 0 means exactly one
     * parameter); ps->par[k] is already normalized to 0 for omitted/empty params. */
    int def1 = t->ps.par[0];            /* raw first parameter, VT default NOT substituted (ED/EL, DSR) */
    int p1 = t->ps.par[0] ? iclamp_(t->ps.par[0], 0, 65535) : 1;   /* VT default 1 */
    int p2 = t->ps.par[1] ? iclamp_(t->ps.par[1], 0, 65535) : 1;   /* VT default 1 */

    /* Queue a response string to be written back to the PTY by main.c. */
    #define TERM_RESP(t_, ...) term_respond((t_), __VA_ARGS__)

    if (t->ps.priv) {
        switch (fin) {
        case 'h':
            for (int i = 0; i <= t->ps.npar; i++) {
                int p = t->ps.par[i];
                if (p == 25) t->show_cursor = 1;
                else if (p == 1) t->app_cursor = 1;          /* DECCKM: application cursor keys */
                else if (p == 47 || p == 1047) alt_screen_enter(t, 0);
                else if (p == 1049) alt_screen_enter(t, 1);
                else if (p == 1048) { t->alt_saved_cx = t->cx; t->alt_saved_cy = t->cy; }
                else if (p == 1000) t->mouse_mode |= MOUSE_PRESS;
                else if (p == 1002) t->mouse_mode |= MOUSE_DRAG;
                else if (p == 1006) t->mouse_mode |= MOUSE_SGR;
                else if (p == 2004) t->bracketed_paste = 1;
            }
            break;
        case 'l':
            for (int i = 0; i <= t->ps.npar; i++) {
                int p = t->ps.par[i];
                if (p == 25) t->show_cursor = 0;
                else if (p == 1) t->app_cursor = 0;          /* DECCKM back to normal cursor keys */
                else if (p == 47 || p == 1047) alt_screen_leave(t, 0);
                else if (p == 1049) alt_screen_leave(t, 1);
                else if (p == 1048) {
                    t->cx = iclamp_(t->alt_saved_cx, 0, t->g.cols - 1);
                    t->cy = iclamp_(t->alt_saved_cy, 0, t->g.rows - 1);
                    t->wrap_next = 0;
                }
                else if (p == 1000) t->mouse_mode &= ~MOUSE_PRESS;
                else if (p == 1002) t->mouse_mode &= ~MOUSE_DRAG;
                else if (p == 1006) t->mouse_mode &= ~MOUSE_SGR;
                else if (p == 2004) t->bracketed_paste = 0;
            }
            break;
        case 'c': /* DA1 private query (ESC[?c) */
            TERM_RESP(t, "\x1b[?64;1;2;4;6;7;12;16;18;22c");
            break;
        default: break;   /* unknown private modes ignored */
        }
        return;
    }

    switch (fin) {
    case 'A': t->cy = iclamp_(t->cy - p1, t->top, t->bot); t->wrap_next = 0; break;
    case 'B': t->cy = iclamp_(t->cy + p1, t->top, t->bot); t->wrap_next = 0; break;
    case 'C': t->cx = imin_(t->cx + p1, t->g.cols - 1); t->wrap_next = 0; break;
    case 'D': t->cx = imax_(t->cx - p1, 0); t->wrap_next = 0; break;
    case 'E': t->cy = iclamp_(t->cy + p1, t->top, t->bot); t->cx = 0; t->wrap_next = 0; break;
    case 'F': t->cy = iclamp_(t->cy - p1, t->top, t->bot); t->cx = 0; t->wrap_next = 0; break;
    case 'G': case '`': t->cx = iclamp_(p1 - 1, 0, t->g.cols - 1); t->wrap_next = 0; break;
    case 'd': t->cy = iclamp_(p1 - 1, t->top, t->bot); t->wrap_next = 0; break;
    case 'H': case 'f': set_cursor_pos(t, p1 - 1, p2 - 1); break;
    case 'J': erase_display(t, def1); break;
    case 'K': erase_line(t, def1); break;
    case 'L': insert_lines(t, imax_(p1, 1)); break;
    case 'M': delete_lines(t, imax_(p1, 1)); break;
    case 'P': /* DCH: delete chars */ {
        Cell *r = grid_vprow(&t->g, t->cy);
        int n = imax_(p1, 1);
        for (int x = t->cx; x < t->g.cols; x++) {
            int src = x + n;
            r[x] = src < t->g.cols ? r[src] : blank_cell(t);
        }
        break;
    }
    case '@': /* IL: insert chars */ {
        Cell *r = grid_vprow(&t->g, t->cy);
        int n = imax_(p1, 1);
        for (int x = t->g.cols - 1; x >= t->cx; x--) {
            int src = x - n;
            r[x] = src >= t->cx ? r[src] : blank_cell(t);
        }
        break;
    }
    case 'X': /* erase chars */
        erase_cells(t, t->cy, t->cx, t->cx + imax_(p1, 1) - 1);
        break;
    case 'S': /* SU */ { int bot = t->bot; for (int k = 0; k < imax_(p1, 1); k++) scroll_up(t, t->top, bot); break; }
    case 'T': /* SD */ { for (int k = 0; k < imax_(p1, 1); k++) scroll_down(t, t->top, t->bot); break; }
    case 'r': {
        int top = iclamp_(p1 - 1, 0, t->g.rows - 1);
        /* an omitted second parameter means the bottom row */
        int bot = t->ps.par[1] ? iclamp_(t->ps.par[1] - 1, 0, t->g.rows - 1)
                               : t->g.rows - 1;
        if (top < bot) {
            t->top = top;
            t->bot = bot;
        }
        set_cursor_pos(t, 0, 0);
        break;
    }
    case 's': t->save_cx = t->cx; t->save_cy = t->cy; break;
    case 'u': set_cursor_pos(t, t->save_cy, t->save_cx); break;
    case 't': {
        /* xterm window-geometry queries. `ESC[16t` asks for one CELL in px, `ESC[14t` for the text
           area; an image application uses them when it cannot read the PTY winsize (over ssh, for
           example), which is the same information timg needs to pick a graphics protocol. Answered
           only once main.c has told us the cell size, so a tool falls back the same way it does for
           a terminal that has no answer. */
        if (t->cell_w > 0 && t->cell_h > 0) {
            if (def1 == 16)
                TERM_RESP(t, "\x1b[6;%d;%dt", t->cell_h, t->cell_w);
            else if (def1 == 14)
                TERM_RESP(t, "\x1b[4;%d;%dt", t->g.rows * t->cell_h, t->g.cols * t->cell_w);
        }
        break;
    }
    case 'm': {
        for (int i = 0; i <= t->ps.npar; i++) {
            int p = t->ps.par[i];
            switch (p) {
            case 0: pen_default(t); break;
            case 1: t->pen.attr |= ATTR_BOLD; break;
            case 2: t->pen.attr |= ATTR_DIM; break;
            case 3: t->pen.attr |= ATTR_ITALIC; break;
            case 4: t->pen.attr |= ATTR_UNDERLINE; break;
            case 7: t->pen.attr |= ATTR_INVERSE; break;
            case 8: t->pen.attr |= ATTR_HIDDEN; break;
            case 9: t->pen.attr |= ATTR_STRIKE; break;
            case 21: case 22: t->pen.attr &= ~(uint32_t)(ATTR_BOLD | ATTR_DIM); break;
            case 23: t->pen.attr &= ~(uint32_t)ATTR_ITALIC; break;
            case 24: t->pen.attr &= ~(uint32_t)ATTR_UNDERLINE; break;
            case 27: t->pen.attr &= ~(uint32_t)ATTR_INVERSE; break;
            case 28: t->pen.attr &= ~(uint32_t)ATTR_HIDDEN; break;
            case 29: t->pen.attr &= ~(uint32_t)ATTR_STRIKE; break;
            case 38: {
                if (i + 1 <= t->ps.npar && t->ps.par[i + 1] == 2) {
                    /* 38;2;r;g;b */
                    if (i + 4 <= t->ps.npar) {
                        int r = iclamp_(t->ps.par[i + 2], 0, 255);
                        int g = iclamp_(t->ps.par[i + 3], 0, 255);
                        int b = iclamp_(t->ps.par[i + 4], 0, 255);
                        t->pen.fg = 0xff000000u | (uint32_t)(r << 16 | g << 8 | b);
                        i += 4;
                    }
                } else if (i + 2 <= t->ps.npar && t->ps.par[i + 1] == 5) {
                    /* 38;5;idx */
                    int idx = t->ps.par[i + 2];
                    if (idx >= 0 && idx < 256)
                        t->pen.fg = term_palette()[idx];
                    i += 2;
                } else if (i + 1 <= t->ps.npar) {
                    /* 38;idx (shorthand) */
                    int idx = t->ps.par[i + 1];
                    if (idx >= 0 && idx < 256)
                        t->pen.fg = term_palette()[idx];
                    i += 1;
                }
                break;
            }
            case 48: {
                if (i + 1 <= t->ps.npar && t->ps.par[i + 1] == 2) {
                    /* 48;2;r;g;b */
                    if (i + 4 <= t->ps.npar) {
                        int r = iclamp_(t->ps.par[i + 2], 0, 255);
                        int g = iclamp_(t->ps.par[i + 3], 0, 255);
                        int b = iclamp_(t->ps.par[i + 4], 0, 255);
                        t->pen.bg = 0xff000000u | (uint32_t)(r << 16 | g << 8 | b);
                        i += 4;
                    }
                } else if (i + 2 <= t->ps.npar && t->ps.par[i + 1] == 5) {
                    /* 48;5;idx */
                    int idx = t->ps.par[i + 2];
                    if (idx >= 0 && idx < 256)
                        t->pen.bg = term_palette()[idx];
                    i += 2;
                } else if (i + 1 <= t->ps.npar) {
                    /* 48;idx (shorthand) */
                    int idx = t->ps.par[i + 1];
                    if (idx >= 0 && idx < 256)
                        t->pen.bg = term_palette()[idx];
                    i += 1;
                }
                break;
            }
            case 39: t->pen.fg = g_def_fg; break;
            case 49: t->pen.bg = g_def_bg; break;
            case 90: case 91: case 92: case 93:
            case 94: case 95: case 96: case 97:
                t->pen.fg = term_palette()[p - 90 + 8];
                break;
            case 100: case 101: case 102: case 103:
            case 104: case 105: case 106: case 107:
                t->pen.bg = term_palette()[p - 100 + 8];
                break;
            default:
                if (p >= 30 && p <= 37)
                    t->pen.fg = term_palette()[p - 30];
                else if (p >= 40 && p <= 47)
                    t->pen.bg = term_palette()[p - 40];
                break;
            }
        }
        break;
    }
    case 'h': {
        for (int i = 0; i <= t->ps.npar; i++)
            if (t->ps.par[i] == 20)
                t->newline_mode = 1;
        break;
    }
    case 'l': {
        for (int i = 0; i <= t->ps.npar; i++)
            if (t->ps.par[i] == 20)
                t->newline_mode = 0;
        break;
    }
    case 'n':
        if (def1 == 6) {
            /* Cursor Position Report (CPR) query: ESC[6n -> respond ESC[{row};{col}R */
            TERM_RESP(t, "\x1b[%d;%dR", t->cy + 1, t->cx + 1);
        } else if (def1 == 5) {
            /* DSR status report: ESC[5n -> ESC[0n ("terminal in functioning state"). Applications use
               it as a cheap "is anyone there" probe -- timg sends ESC[>q ESC[5n as its first query and
               sits in a 250 ms timeout until something answers. Not answering is not harmful, it is
               just slow, and answering is what every terminal does. */
            TERM_RESP(t, "\x1b[0n");
        }
        break;
    case 'c':
        if (t->ps.gt) {
            /* DA2 (Secondary Device Attributes): ESC[>c */
            TERM_RESP(t, "\x1b[>0;0;0c");
        } else {
            /* DA1 (Primary Device Attributes): ESC[c or ESC[0c. The list deliberately claims NO
               graphics capability: 4 (sixel), 16 (sixel + ReGIS) and 18 (sixel, wide pixels) are not
               implemented -- the parser discards DCS, so sixel never reaches the screen -- and an
               application that trusts the claim sends a picture that is then thrown away. Measured
               with timg 1.6.3: with `;4` in this answer it switches to sixel and the picture
               disappears; without it it falls back to quarter blocks, which at least draw. */
            TERM_RESP(t, "\x1b[?64;1;2;6;7;12;22c");
        }
        break;
    case 'q':
        /* DECSCUSR — `CSI Ps SP q` sets the cursor shape and its blink. The SPACE is the sequence: the
           parser used to drop intermediate bytes, so `ESC[3 q` reached here indistinguishable from
           `ESC[3q` and nothing acted on it at all (measured 2026-10-19 with
           .scratch/s111/decusrprobe: every form was consumed and changed nothing). Requiring the space is
           what the sequence IS, and it keeps `CSI Ps q` (a different, unimplemented spelling) out.
           xterm's table: 0 (omitted) and 1 = blinking block, 2 = steady block, 3 = blinking bar, 4 = steady
           bar, 5 = blinking underline, 6 = steady underline. Anything else is ignored, not guessed. */
        if (t->ps.inter == ' ') {
            int p = t->ps.par[0];
            int style = 0, blink = 0;
            switch (p) {
            case 0: case 1: style = CUR_BLOCK;     blink = CURBLINK_BLINK;  break;
            case 2:         style = CUR_BLOCK;     blink = CURBLINK_STEADY; break;
            case 3: case 4: style = CUR_BAR;       blink = p == 3 ? CURBLINK_BLINK : CURBLINK_STEADY; break;
            case 5: case 6: style = CUR_UNDERLINE; blink = p == 5 ? CURBLINK_BLINK : CURBLINK_STEADY; break;
            default: break;
            }
            if (style) {
                t->cur_style = style;
                t->cur_blink = blink;
                klog(L_DEBUG, "decscusr: the application asked for shape %d blink %d", style, blink);
            } else {
                klog(L_DEBUG, "decscusr: ESC[%d q ignored — not a shape Kai-Term knows", p);
            }
        }
        break;
    default:
        break;   /* unimplemented CSI ignored */
    }
    #undef TERM_RESP
}

/* ================= graphics: the kitty image protocol =================
 *
 * An application draws an image with one or more APC sequences:
 *
 *     ESC _ G  key=value ; ... ; key=value : <base64 payload>  ESC \
 *
 * What timg 1.6.3 really emits (captured, see AGENT.md s68) is
 *
 *     ESC_Ga=T,i=<id>,q=2,f=100,m=1;<4096 base64 chars>ESC\        first chunk
 *     ESC_Gq=2,m=0;<rest>ESC\                                     continuation: no a=, no i=
 *
 * so `m` is the "more chunks follow" flag here and `f=100` says the payload is a PNG. The
 * protocol overloads `r`: height in px for the raw formats (f=24/32), rows of the grid for the
 * self-describing ones. It never says how many CELLS an image covers in the common case — the
 * terminal is expected to divide the pixel size by its own cell size, which is why main.c pushes
 * the cell size into Term.cell_w/cell_h and the PTY winsize.
 *
 * Lifetime: an image is anchored to an ABSOLUTE grid row (Grid.row_base), so it scrolls with the
 * text it was printed next to, is clipped at the viewport edges and comes back when the user
 * scrolls up into history. image_prune() drops the ones whose rows no longer exist; the byte
 * budget drops the oldest when an application keeps sending pictures.
 */

#define IMAGE_BYTES_MAX (64LL << 20)     /* decoded bytes one session keeps */
#define IMAGE_COUNT_MAX 256              /* images one session keeps */
#define APC_BYTES_MAX   (32u << 20)      /* buffered APC payload (one sequence) */
#define ZINFLATE_MAX    (64u << 20)      /* largest payload a compressed chunk may expand to */

/* ---------- base64 ---------- */
static int b64_val(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Decode n base64 chars into out (room for (n/4+1)*3 bytes). Returns the byte count. Anything
   outside the alphabet ends the stream, which covers padding and any stray whitespace.
   The mask after each emitted byte is not cosmetic: without it `acc` keeps every bit it ever
   collected, and by the fifth character `acc << 6` overflows int (UBSan caught it on the first
   4-chunk payload this file feeds). Kept in unsigned so the shifts are defined anyway. */
static size_t b64_decode(const char *s, size_t n, uint8_t *out)
{
    size_t o = 0;
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        int v = b64_val((unsigned char)s[i]);
        if (v < 0)
            break;
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (uint8_t)((acc >> bits) & 0xff);
            acc &= (1u << bits) - 1u;      /* keep only the bits not yet emitted */
        }
    }
    return o;
}

/* ---------- payload decoding ---------- */

/* zlib-wrapped payload. The protocol's compression flag is ambiguous in the wild (timg uses m= as
   the more-chunks flag), so the payload's own bytes decide whether this is needed. */
static uint8_t *zinflate(const uint8_t *in, size_t n, size_t *outlen)
{
    size_t cap = n * 4 + 1024;
    for (int attempt = 0; attempt < 6; attempt++) {
        uint8_t *buf = malloc(cap);
        if (!buf)
            return NULL;
        z_stream zs;
        memset(&zs, 0, sizeof zs);
        if (inflateInit(&zs) != Z_OK) {
            free(buf);
            return NULL;
        }
        /* The cast away from const is zlib's signature, not a dropped guarantee: `next_in` is `Bytef *` unless
           zlib was built with ZLIB_CONST, and inflate() only ever reads these bytes. This is also the one place
           in our own tree that -Wcast-qual would flag (measured with clang 22) — the flag stays out of WARN for
           the generated xdg-shell bindings, and saying so here keeps it from looking like an oversight. */
        zs.next_in = (Bytef *)in;
        zs.avail_in = (uInt)n;
        zs.next_out = buf;
        zs.avail_out = (uInt)cap;
        int r = inflate(&zs, Z_FINISH);
        size_t got = zs.total_out;
        inflateEnd(&zs);
        if (r == Z_STREAM_END) {
            *outlen = got;
            return buf;
        }
        free(buf);
        if (r != Z_BUF_ERROR || cap >= ZINFLATE_MAX)
            return NULL;
        cap *= 4;
    }
    return NULL;
}

/* f=24 (RGB) / f=32 (RGBA): the raw pixels, whose size the application must have given.
   The protocol is ambiguous about HOW it gives it: the kitty spec says s= is the byte size of the
   uncompressed data and r= its row count (so width = s/(r*bpp)), while an encoder that treats s= as
   the width sends s*r*bpp bytes. Nothing here can tell which one timg et al. use — timg only ever
   emits PNG (measured, see AGENT.md s68) — so the payload length decides: a payload that is exactly
   s*r*bpp bytes cannot be a (s/(r*bpp)) x r image, and one that is s bytes cannot be s columns wide.
   Both readings are accepted rather than guessed at, and the raw path stays the least exercised part
   of this protocol. */
static uint8_t *decode_raw(int fmt, int s_val, int rows, const uint8_t *raw, size_t n, int *w, int *h)
{
    int bpp = fmt == 24 ? 3 : 4;
    int pw = 0, ph = rows;
    long long as_width = (long long)s_val * rows * bpp;      /* s= is the column count */
    if (rows > 0 && as_width > 0 && (long long)n >= as_width) {
        pw = s_val;
    } else if (rows > 0 && s_val % (rows * bpp) == 0 && (long long)n >= (long long)s_val) {
        pw = s_val / (rows * bpp);                           /* s= is the byte count */
    }
    if (pw < 1 || ph < 1 || pw > IMG_DIM_MAX || ph > IMG_DIM_MAX ||
        (long long)pw * ph > IMG_PIX_MAX) {
        klog(L_WARN, "graphics: raw image has no usable size (s=%d, r=%d, %zu bytes)",
             s_val, rows, n);
        return NULL;
    }
    if (n < (size_t)pw * ph * bpp) {
        klog(L_WARN, "graphics: raw image is short (%zu bytes for %dx%d)", n, pw, ph);
        return NULL;
    }
    uint8_t *out = malloc((size_t)pw * ph * 4);
    if (!out)
        return NULL;
    for (long long i = 0; i < (long long)pw * ph; i++) {
        out[i * 4 + 0] = raw[i * bpp + 0];
        out[i * 4 + 1] = raw[i * bpp + 1];
        out[i * 4 + 2] = raw[i * bpp + 2];
        out[i * 4 + 3] = bpp == 4 ? raw[i * bpp + 3] : 255;
    }
    *w = pw;
    *h = ph;
    return out;
}

/* Turn the assembled payload into premultiplied RGBA. The payload's own magic wins over f=,
   because a PNG is self-describing and an application may set f= loosely. The decoders themselves
   live in src/img.c, which the file manager's preview pane uses too — the PNG a terminal application
   transmits and the PNG a preview reads from disk must go through the same code. */
static uint8_t *decode_image(const KittyTx *tx, int *w, int *h)
{
    const uint8_t *raw = tx->buf;
    size_t n = tx->len;
    *w = *h = 0;
    if (!n) {
        klog(L_WARN, "graphics: empty payload");
        return NULL;
    }
    uint8_t *inf = NULL;
    size_t infn = 0;
    if (n >= 2 && raw[0] == 0x78) {
        inf = zinflate(raw, n, &infn);
        if (inf) {
            raw = inf;
            n = infn;
        }
    }
    static const uint8_t png_magic[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    uint8_t *out = NULL;
    if (n >= 8 && !memcmp(raw, png_magic, 8))
        out = img_decode_png(raw, n, w, h);
    else if (tx->fmt == 24 || tx->fmt == 32)
        out = decode_raw(tx->fmt, tx->pw, tx->ph, raw, n, w, h);
    else
        klog(L_WARN, "graphics: unsupported payload (f=%d, %zu bytes)", tx->fmt, n);
    free(inf);
    if (!out)
        return NULL;
    /* The renderer composites texels as c + bg*(1-c.a) with no GL blending — the same rule the
       color-emoji atlas follows — so the texels must already be premultiplied.
       Rounding is +128, NOT +255: (x*255 + 255)/255 == 256 for x == 255, and the cast to uint8_t turned
       a fully opaque WHITE pixel black (found by the test in tests/test_vt.c, missed by the s68 probe
       whose sample pixel was never white). */
    img_premultiply(out, (long long)(*w) * (*h));
    return out;
}

/* ---------- the image list ---------- */

static void image_free(KImage *im)
{
    free(im->rgba);
    free(im);
}

static void term_free_images(Term *t)
{
    KImage *im = t->images;
    while (im) {
        KImage *nx = im->next;
        image_free(im);
        im = nx;
    }
    t->images = NULL;
    t->image_bytes = 0;
}

int term_image_count(const Term *t)
{
    int n = 0;
    for (const KImage *im = t->images; im; im = im->next)
        n++;
    return n;
}

/* Append, keeping the list oldest first: the byte budget evicts from the front, which is also the
   order the user scrolls past. */
static void image_append(Term *t, KImage *im)
{
    KImage **pp = &t->images;
    while (*pp)
        pp = &(*pp)->next;
    *pp = im;
    im->next = NULL;
    t->image_bytes += (long long)im->w * im->h * 4;
}

static KImage *image_find(Term *t, const KittyTx *tx)
{
    int id = tx->use_last ? t->image_last_id : tx->id;
    if (!id)
        return NULL;
    for (KImage *im = t->images; im; im = im->next)
        if (im->app_id == id)
            return im;
    return NULL;
}

/* The span an image covers, in cells. An explicit c=/r= wins; otherwise its pixel size divided by
   the current cell size (ceil, at least one cell). With no cell size yet — a session that has
   never been laid out — fall back to one cell per pixel so the arithmetic stays finite. When only
   ONE of c=/r= was given the other follows the picture's own aspect ratio (the protocol scales the
   picture into the cell area it named), and that has to be decided HERE rather than in the renderer:
   the span is also what a=T moves the cursor past and what image_prune() measures, so a span that
   disagreed with what is drawn would move the prompt to the wrong row. */
static int image_span_rows(const Term *t, const KImage *im)
{
    if (im->rows > 0)
        return im->rows;
    if (im->cols > 0 && t->cell_w > 0 && t->cell_h > 0 && im->w > 0 && im->h > 0) {
        long long num = (long long)im->cols * t->cell_w * im->h;
        long long den = (long long)im->w * t->cell_h;
        return imax_(1, (int)((num + den - 1) / den));
    }
    int ch = t->cell_h > 0 ? t->cell_h : im->h;
    if (ch < 1)
        ch = 1;
    return imax_(1, (im->h + ch - 1) / ch);
}

static int image_span_cols(const Term *t, const KImage *im)
{
    if (im->cols > 0)
        return im->cols;
    if (im->rows > 0 && t->cell_w > 0 && t->cell_h > 0 && im->w > 0 && im->h > 0) {
        long long num = (long long)im->rows * t->cell_h * im->w;
        long long den = (long long)im->h * t->cell_w;
        return imax_(1, (int)((num + den - 1) / den));
    }
    int cw = t->cell_w > 0 ? t->cell_w : im->w;
    if (cw < 1)
        cw = 1;
    return imax_(1, (im->w + cw - 1) / cw);
}

void term_set_cell_px(Term *t, int w, int h)
{
    t->cell_w = w > 0 ? w : 0;
    t->cell_h = h > 0 ? h : 0;
}

/* The images of this session that are at least partly inside the viewport, in the order they must
   be drawn (z ascending, then arrival). crow may be negative and the span may run past the bottom
   — render.c clips the quad, and it needs the UNCLIPPED rect to get the texture coordinates right. */
int term_visible_images(Term *t, ImgView *out, int cap)
{
    Grid *g = &t->g;
    int n = 0;
    for (KImage *im = t->images; im; im = im->next) {
        if (!im->shown || im->alt != (t->alt_screen ? 1 : 0))
            continue;
        int rows = image_span_rows(t, im);
        int cols = image_span_cols(t, im);
        int crow = (int)(im->doc_row - g->row_base - g->scroll_off);
        int ccol = im->col;
        if (crow >= g->rows || crow + rows <= 0)
            continue;                       /* fully above or below the viewport */
        if (ccol >= g->cols || ccol + cols <= 0)
            continue;                       /* fully left or right of the grid */
        if (n >= cap)
            continue;                       /* room only for the first `cap` */
        out[n].img = im;
        out[n].crow = crow;
        out[n].ccol = ccol;
        out[n].crows = rows;
        out[n].ccols = cols;
        n++;
    }
    for (int i = 1; i < n; i++) {     /* insertion sort: n is a handful at most */
        ImgView key = out[i];
        int j = i - 1;
        while (j >= 0 && (out[j].img->z > key.img->z ||
                          (out[j].img->z == key.img->z && out[j].img->serial > key.img->serial))) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return n;
}

/* Drop images whose anchor rows no longer exist in this grid: evicted above the retained window,
   or anchored below it (erase_scrollback moves the viewport to slots 0..rows-1 and blanks the
   rest, so anything anchored there sits in a free slot). */
static void image_prune(Term *t)
{
    Grid *g = &t->g;
    if (!t->images)
        return;
    int64_t first = g->row_base;
    int64_t last = g->row_base + g->scroll_off + g->rows;   /* exclusive */
    KImage **pp = &t->images;
    while (*pp) {
        KImage *im = *pp;
        int rows = image_span_rows(t, im);
        if (im->doc_row + rows <= first || im->doc_row >= last) {
            *pp = im->next;
            t->image_bytes -= (long long)im->w * im->h * 4;
            image_free(im);
            continue;
        }
        pp = &im->next;
    }
}

/* Ceiling for a session that keeps showing images (a video played with timg -V sends a frame per
   step). The oldest go first; the renderer notices the serial disappear and frees its texture. */
static void image_budget(Term *t)
{
    while (t->images &&
           (t->image_bytes > IMAGE_BYTES_MAX || term_image_count(t) > IMAGE_COUNT_MAX)) {
        KImage *oldest = t->images;
        t->images = oldest->next;
        t->image_bytes -= (long long)oldest->w * oldest->h * 4;
        klog(L_DEBUG, "graphics: dropped image %d (%d x %d px) to stay within the budget",
             oldest->serial, oldest->w, oldest->h);
        image_free(oldest);
    }
}

static void image_place(Term *t, KImage *im, int move_cursor)
{
    Grid *g = &t->g;
    /* A cursor left past the right margin (the pending-wrap state putch() enters after the last
       column) is resolved here exactly the way putch() resolves it for the next character: the
       picture starts at the left edge of the next row. Anchoring it at col == cols put it entirely
       outside the grid — measured with `timg -p k` run after its own verbose output had filled a
       line: the image decoded fine and was never drawn. */
    if (t->wrap_next && t->cx >= g->cols) {
        t->cx = 0;
        g->wrap[g->scroll_off + t->cy] = 1;
        linefeed(t);
    }
    if (t->cx >= g->cols)                /* defensive: an anchor must be a real column */
        t->cx = g->cols - 1;
    im->doc_row = g->row_base + g->scroll_off + t->cy;
    im->col = t->cx;
    im->alt = t->alt_screen ? 1 : 0;
    im->shown = 1;
    if (!move_cursor)
        return;
    /* a=T transmits, displays AND moves the cursor below the picture: timg emits the image and
       then a single CR LF, so the terminal is what has to leave the prompt under it. linefeed()
       is used rather than cy += rows because a picture taller than the viewport must scroll the
       screen, exactly like printing that many lines would. */
    int rows = image_span_rows(t, im);
    for (int i = 0; i < rows; i++)
        linefeed(t);
    t->wrap_next = 0;
    cluster_invalidate(t);
}

static void image_delete(Term *t, const KittyTx *tx)
{
    int id = tx->use_last ? t->image_last_id : tx->id;
    int scope = tx->del;
    int64_t cur = t->g.row_base + t->g.scroll_off + t->cy;
    KImage **pp = &t->images;
    int gone = 0;
    while (*pp) {
        KImage *im = *pp;
        int hit = 0;
        if (scope == 'A')
            hit = 1;
        else if (scope == 'I' || scope == 0)
            hit = (id != 0 && im->app_id == id);
        else if (scope == 'R')
            hit = (im->doc_row >= cur);
        else if (scope == 'C')
            hit = (im->doc_row == cur);
        if (hit) {
            *pp = im->next;
            t->image_bytes -= (long long)im->w * im->h * 4;
            image_free(im);
            gone++;
        } else {
            pp = &im->next;
        }
    }
    if (gone)
        klog(L_DEBUG, "graphics: deleted %d image(s)", gone);
}

/* ---------- the control block ---------- */

static void kitty_ctl_reset(KittyTx *tx)
{
    tx->active = 0;
    tx->more = 0;
    tx->len = 0;
    tx->act = 0;
    tx->fmt = 0;
    tx->id = 0;
    tx->pid = 0;
    tx->use_last = 0;
    tx->pw = 0;
    tx->ph = 0;
    tx->r_raw = 0;
    tx->cols = 0;
    tx->rows = 0;
    tx->xoff = 0;
    tx->yoff = 0;
    tx->z = 0;
    tx->q = 0;
    tx->del = 0;
}

/* Parse `k=v;k=v;...` — and, because timg 1.6.3 writes `k=v,k=v,...`, ',' is accepted as a
   separator too. Returns 1 when an action (a=) was named, which is how a continuation chunk is
   recognised: timg's second chunk carries only q= and m=, so everything else must be inherited
   from the chunk that started the transmission. */
static int kitty_ctl_parse(KittyTx *tx, const char *ctl)
{
    int saw_act = 0, ttype = 0;
    const char *p = ctl;
    while (p && *p) {
        const char *end = strpbrk(p, ";,");
        size_t len = end ? (size_t)(end - p) : strlen(p);
        const char *eq = memchr(p, '=', len);
        if (eq && len >= 2) {
            char key = p[0];
            const char *val = eq + 1;
            size_t vlen = len - (size_t)(eq - p) - 1;
            char num[24];
            long v = 0;
            /* a leading '-' belongs to the value: z= is documented to take negative indices */
            size_t dig = (vlen > 0 && val[0] == '-') ? 1 : 0;
            int isnum = vlen > dig && vlen < sizeof num;
            for (size_t i = dig; isnum && i < vlen; i++)
                if (val[i] < '0' || val[i] > '9')
                    isnum = 0;
            if (isnum) {
                memcpy(num, val, vlen);
                num[vlen] = 0;
                v = strtol(num, NULL, 10);
            }
            switch (key) {
            case 'a': tx->act = val[0]; saw_act = 1; break;
            case 'f': tx->fmt = (int)v; break;
            case 'i': tx->id = (int)v; break;
            case 'u': tx->pid = (int)v; break;
            case 'U': tx->use_last = (int)v; break;
            case 'm': tx->more = (int)v; break;
            case 's': tx->pw = (int)v; break;
            case 'r': tx->r_raw = (int)v; break;
            case 'c': tx->cols = (int)v; break;
            case 'x': tx->xoff = (int)v; break;
            case 'y': tx->yoff = (int)v; break;
            case 'z': tx->z = (int)v; break;
            case 'q': tx->q = (int)v; break;
            case 'd': tx->del = val[0]; break;
            case 't': ttype = val[0]; break;
            default: break;      /* w/h/X/Y crop, o/e, v: not implemented, ignored */
            }
        }
        p = end ? end + 1 : NULL;
    }
    /* r= means height in px for the raw formats and rows of the grid for the self-describing
       ones; f= may come after r=, hence deciding it here. */
    if (tx->fmt == 24 || tx->fmt == 32)
        tx->ph = tx->r_raw;
    else
        tx->rows = tx->r_raw;
    if (ttype == 'f' || ttype == 't' || ttype == 'v')
        klog(L_WARN, "graphics: file transmission (t=%c) is not supported", ttype);
    return saw_act;
}

static void kitty_tx_free(Parser *ps)
{
    free(ps->tx.buf);
    ps->tx.buf = NULL;
    ps->tx.cap = 0;
    ps->tx.len = 0;
    ps->tx.active = 0;
}

static void kitty_tx_grow(KittyTx *tx, size_t need)
{
    if (need <= tx->cap)
        return;
    size_t cap = tx->cap ? tx->cap : 4096;
    while (cap < need)
        cap *= 2;
    uint8_t *b = realloc(tx->buf, cap);
    if (!b) {
        klog(L_WARN, "graphics: out of memory for a %zu byte image payload", cap);
        return;
    }
    tx->buf = b;
    tx->cap = cap;
}

/* The transmission is complete: decode it and carry out the action. */
static void kitty_complete(Term *t, KittyTx *tx)
{
    switch (tx->act) {
    case 't': case 'T': case 'f': {
        int w = 0, h = 0;
        uint8_t *rgba = decode_image(tx, &w, &h);
        if (!rgba) {
            if (tx->q == 2)
                term_respond(t, "\x1b_G:EBMP;i=%d\x1b\\", tx->id);
            break;
        }
        /* Does this transmission name an id that is already on the list? The protocol identifies an
           image by that id, so re-transmitting to it REPLACES that image instead of leaving another
           copy behind (measured with timg 1.6.3 -V: a four-frame gif alternates between TWO ids, and
           treating every transmission as new stacks four pictures where two belong). a=f and a=t keep
           the existing placement; a=T re-places at the cursor and moves it, like any new picture. */
        KImage *im = image_find(t, tx);
        if (im) {
            t->image_bytes -= (long long)im->w * im->h * 4;
            free(im->rgba);
            im->rgba = rgba;
            im->w = w;
            im->h = h;
            im->version++;
            t->image_bytes += (long long)w * h * 4;
            if (tx->cols > 0)
                im->cols = tx->cols;
            if (tx->rows > 0)
                im->rows = tx->rows;
            im->z = tx->z;
            if (tx->act == 'T')
                image_place(t, im, 1);
        } else {
            im = calloc(1, sizeof *im);
            if (!im) {
                free(rgba);
                break;
            }
            im->serial = ++t->image_serial;
            im->app_id = tx->id;
            im->rgba = rgba;
            im->w = w;
            im->h = h;
            im->cols = tx->cols;
            im->rows = tx->rows;
            im->px = tx->xoff;
            im->py = tx->yoff;
            im->z = tx->z;
            im->version = 1;
            image_append(t, im);
            if (tx->id)
                t->image_last_id = tx->id;
            if (tx->act == 'T')
                image_place(t, im, 1);
            /* a=t only transmits: the image waits for a later a=p/a=c to be placed. */
        }
        if (tx->q == 1)
            term_respond(t, "\x1b_G:OK;i=%d\x1b\\", im->app_id ? im->app_id : im->serial);
        image_budget(t);
        break;
    }
    case 'p': case 'c': {
        KImage *im = image_find(t, tx);
        if (!im) {
            klog(L_DEBUG, "graphics: no image %d to display", tx->id);
            if (tx->q == 2)
                term_respond(t, "\x1b_G:ENOENT;i=%d\x1b\\", tx->id);
            break;
        }
        if (tx->cols > 0)
            im->cols = tx->cols;
        if (tx->rows > 0)
            im->rows = tx->rows;
        image_place(t, im, 0);
        if (tx->q == 1)
            term_respond(t, "\x1b_G:OK;i=%d\x1b\\", im->app_id);
        break;
    }
    case 'd':
        image_delete(t, tx);
        break;
    default:
        klog(L_DEBUG, "graphics: action a=%c ignored", tx->act ? tx->act : '?');
        break;
    }
}

/* One complete APC payload (the parser already stripped ESC _ G and the terminator). */
static void kitty_apc(Term *t, const char *s)
{
    Parser *ps = &t->ps;
    KittyTx *tx = &ps->tx;

    /* Split control block from payload. The protocol says the payload follows a ':', but timg
       1.6.3 writes `a=T,i=...,f=100,m=0;<base64>` — a ';' — and separates the keys with ','.
       So: walk control elements (`letter=value`, separated by ';' or ','), and the payload is
       whatever follows the last of them, or everything after the first ':' if one comes first.
       Base64 never contains ':', ';' or ',', so the walk cannot run past the payload. */
    const char *ctl_end = s + strlen(s);
    const char *payload = NULL;
    {
        const char *p = s;
        for (;;) {
            const char *co = strchr(p, ':');
            const char *sc = strpbrk(p, ";,");
            if (co && (!sc || co < sc)) {          /* the documented separator */
                ctl_end = co;
                payload = co + 1;
                break;
            }
            const char *elem_end = sc ? sc : p + strlen(p);
            size_t elen = (size_t)(elem_end - p);
            int is_ctrl = elen >= 2 && p[1] == '=' &&
                          ((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z'));
            if (!is_ctrl) {                        /* the payload starts here */
                ctl_end = p;
                payload = p;
                break;
            }
            if (!sc) {                             /* control block ends the string */
                ctl_end = elem_end;
                break;
            }
            p = sc + 1;
        }
    }
    size_t ctln = (size_t)(ctl_end - s);
    if (ctln >= 4096) {
        klog(L_WARN, "graphics: control block too long (%zu bytes), ignored", ctln);
        return;
    }
    char ctl[4096];
    memcpy(ctl, s, ctln);
    ctl[ctln] = 0;
    size_t plen = payload ? strlen(payload) : 0;

    /* Start from the state a previous chunk left behind, so a continuation chunk (which names
       only m=) inherits the format, the id and the size of the chunk that began the transfer. */
    KittyTx nt = *tx;
    nt.more = 0;
    int saw_act = kitty_ctl_parse(&nt, ctl);
    if (saw_act) {
        if (tx->active && tx->len)
            klog(L_DEBUG, "graphics: transmission interrupted, %zu bytes dropped", tx->len);
        kitty_ctl_reset(&nt);
        /* kitty_ctl_reset cleared the fields a fresh parse must re-fill; re-parse into the
           cleared state so a key the new chunk omits really is absent. */
        saw_act = kitty_ctl_parse(&nt, ctl);
    } else if (!tx->active) {
        klog(L_DEBUG, "graphics: APC chunk with no action and nothing in progress, ignored");
        return;
    }
    /* nt carries the same buf/cap pointers tx did (it is a copy), so assigning it is safe. */
    *tx = nt;
    if (saw_act)
        tx->len = 0;
    tx->active = 1;

    if (plen) {
        kitty_tx_grow(tx, tx->len + (plen / 4 + 1) * 3);
        tx->len += b64_decode(payload, plen, tx->buf + tx->len);
    }

    if (tx->more)
        return;
    tx->active = 0;
    kitty_complete(t, tx);
    tx->len = 0;
}

/* ---------- APC / DCS parser states ---------- */

/* Kai-Term's own APC channel: `ESC _ K ; kai-term ; <what> [ ; <text> ] ST`, written by the shell
   integration script (src/shellint.c) to its own stdout, which is this session's pty. Two messages:
   `integration;<kind>` — "this shell loaded the script, the fast path is available" — and `cd;<dir>` —
   "I, the shell, changed my own directory to <dir>". The second is the only evidence Kai-Term gets that a
   `cd` request actually reached a shell that was listening, which is what makes the request safe to prefer
   over typing at the prompt: if it does not arrive, main.c falls back to the typed path. Nothing here is
   trusted beyond "a shell of this kind is listening": the directory in an ack is compared with the one
   Kai-Term asked for, never used as an instruction.
   The two functions below are the whole of that channel — the clear main.c calls before asking, and the
   parser's entry point for what comes back. */

/* The shell's `cd` ack is one slot, so it answers for exactly one request. main.c clears it before sending a
   new request; without that, a navigation back to a directory the shell entered earlier would be "confirmed"
   by the old ack and the follow would believe a request nobody answered. */
void term_shell_cd_ack_clear(Term *t)
{
    t->shell_cd_ack[0] = 0;
}

static void kai_apc(Term *t, const char *p)
{
    /* The kind byte ('K') is consumed by the parser and is NOT part of the payload, so the payload of
       `ESC _ K ; kai-term ; cd ; /etc ST` arrives as `;kai-term;cd;/etc`. */
    if (p[0] == ';')
        p++;
    if (strncmp(p, "kai-term;", 9) != 0)
        return;                       /* some other program's APC that happens to start with 'K' */
    p += 9;
    if (strncmp(p, "integration;", 12) == 0) {
        const char *kind = p + 12;
        int k = SHELLINT_NONE;
        if (!strcmp(kind, "fish"))       k = SHELLINT_FISH;
        else if (!strcmp(kind, "bash"))  k = SHELLINT_BASH;
        else if (!strcmp(kind, "zsh"))   k = SHELLINT_ZSH;
        if (k == SHELLINT_NONE) {
            klog(L_WARN, "shell integration: a shell announced an unknown kind '%s', ignoring it", kind);
            return;
        }
        if (t->shell_int != k)
            klog(L_INFO, "shell integration: %s announced itself", shellint_name(k));
        t->shell_int = k;
        /* Every announcement is an event main.c must be able to see, even when the kind does not change
           (fish -> a nested fish -> back to fish): that is how a session whose integration had stopped
           answering learns that a shell is listening again. See Tab.int_gen. */
        t->shell_int_gen++;
        return;
    }
    if (strncmp(p, "cd;", 3) == 0) {
        snprintf(t->shell_cd_ack, sizeof(t->shell_cd_ack), "%s", p + 3);
        return;
    }
}

static void apc_end(Parser *ps)
{
    Term *t = ps->t;
    if (ps->apc_len < ps->apc_cap)
        ps->apc[ps->apc_len] = 0;
    if (ps->apc_drop) {
        klog(L_WARN, "graphics: APC payload over %u bytes, discarded", APC_BYTES_MAX);
    } else if (ps->apc_kind == 'G' && ps->apc_len) {
        kitty_apc(t, ps->apc);
    } else if (ps->apc_kind == 'K' && ps->apc_len) {
        kai_apc(t, ps->apc);
    }
    ps->apc_len = 0;
    ps->apc_drop = 0;
    ps->apc_kind = 0;
}

static void apc_put(Parser *ps, uint8_t b)
{
    if (ps->apc_drop)
        return;
    if (ps->apc_len + 1 >= ps->apc_cap) {
        if (ps->apc_cap >= APC_BYTES_MAX) {
            ps->apc_drop = 1;
            return;
        }
        size_t cap = ps->apc_cap ? ps->apc_cap : 8192;
        if (cap * 2 > APC_BYTES_MAX)
            cap = APC_BYTES_MAX;
        else
            cap *= 2;
        char *b2 = realloc(ps->apc, cap);
        if (!b2) {
            ps->apc_drop = 1;
            return;
        }
        ps->apc = b2;
        ps->apc_cap = cap;
    }
    ps->apc[ps->apc_len++] = (char)b;
}

/* An OSC colour argument. Kai-Term's own forms (#RRGGBB, 0xRRGGBB, bare RRGGBB, AARRGGBB) go through
   cfg_parse_color so the project keeps ONE colour syntax and a theme file can never accept a form the config
   file rejects. The X11 `rgb:RR/GG/BB` and `rgb:RRRR/GGGG/BBBB` forms are what xterm, kitty and every
   colour-picker tool actually send, so accepting only the hex forms would make the feature useless in practice.
   X11 colour NAMES ("red", "wheat") are deliberately NOT accepted: that is a 700-entry table, and every tool
   that can name a colour can also spell it in hex. A 0x00000000 result is "not a colour" — the same convention
   src/theme.c uses, where it is sound because a 6-digit value always gets alpha 0xff and an 8-digit value with
   alpha 00 is invisible by definition. */
static int osc_color(const char *s, uint32_t *out)
{
    if (!s || !*s)
        return 0;
    if (!strncasecmp(s, "rgb:", 4)) {
        s += 4;
        unsigned v[3] = { 0, 0, 0 };
        int width = 0;                       /* every component must have the SAME digit count (the X11 rule) */
        for (int i = 0; i < 3; i++) {
            char *end;
            unsigned long d = strtoul(s, &end, 16);
            size_t digits = (size_t)(end - s);
            if (end == s || (digits != 2 && digits != 4))
                return 0;
            if (i == 0) width = (int)digits;
            else if ((int)digits != width) return 0;
            /* 2 digits are a byte; 4 digits are a fraction of 0xffff and lose their low byte. No clamp is
               needed and none is wanted: two hex digits are at most 0xff, and 0xffff >> 8 is 0xff, so a
               component CANNOT exceed a byte here. It used to be written (d + 0x80) >> 8 to "round", and
               0xffff + 0x80 is 0x1007f, which >> 8 gives 256 — 256 << 16 lands 0x01 in the green byte, so
               `rgb:ffff/ffff/ffff`, the canonical X11 form of WHITE, painted #010100: near-black. Measured
               (.scratch/s104/rgbround.c). That is the s102 cube bug in a new place — a computed colour that
               overflows its channel — so the arithmetic is deliberately one shift. */
            v[i] = digits == 2 ? (unsigned)d : (unsigned)(d >> 8);
            if (i < 2) {
                if (*end != '/')
                    return 0;
                s = end + 1;
            } else {
                s = end;
            }
        }
        while (isspace((unsigned char)*s))
            s++;
        if (*s)
            return 0;
        *out = 0xff000000u | (v[0] << 16) | (v[1] << 8) | v[2];
        return 1;
    }
    uint32_t v = cfg_parse_color(s, 0x00000000u);
    if (!v)
        return 0;
    *out = v;
    return 1;
}

static void osc_end(Parser *ps)
{
    Term *t = ps->t;
    ps->osc[ps->osc_len] = 0;
    int cmd = -1;
    char *start = ps->osc;
    if (!start[0])
        return;
    char *semi = strchr(start, ';');
    const char *s;                      /* the title text: `start` when there is no numeric prefix */
    if (semi) {
        cmd = atoi(start);
        s = semi + 1;
    } else {
        cmd = atoi(start);
        s = "";
    }
    if (cmd == 0 || cmd == 1 || cmd == 2) {
        snprintf(t->title, sizeof(t->title), "%s", s);
        return;
    }

    /* OSC 4 (set a palette entry), 10/11 (set or query the default ink/paper) and 104 (put entries back) are
       the ONLY colour sequences a program can send, so they are the whole of the "repaint the terminal from
       the inside" capability — which is why they sit behind the user's off switch (s102 question 2: yes to the
       feature, and an off switch because "any program in my terminal can repaint the window"). */
    if (!g_osc_colors) {
        if (cmd == 4 || cmd == 10 || cmd == 11 || cmd == 104)
            klog(L_DEBUG, "osc: %d ignored — osc_colors = 0", cmd);
        return;
    }

    switch (cmd) {
    case 4: {
        /* `OSC 4;N;colour` and the multi form `OSC 4;N;colour;M;colour;...` both occur in the wild. Entries
           0-255 are all settable here, unlike a theme file: the setter is general by design (the theme layer
           is what restricts a file to 0-15), and `osc_colors = 0` is the switch for anyone who does not want
           an application touching the cube. */
        char *p = (char *)s;
        int set = 0;
        while (p && *p) {
            char *iend;
            long idx = strtol(p, &iend, 10);
            if (iend == p || *iend != ';') {
                klog(L_WARN, "osc: OSC 4 '%s' has no entry number", p);
                break;
            }
            char *cstart = iend + 1;
            char *cend = strchr(cstart, ';');
            size_t clen = cend ? (size_t)(cend - cstart) : strlen(cstart);
            char spec[64];
            if (clen >= sizeof spec) {
                klog(L_WARN, "osc: OSC 4;%ld colour argument is too long", idx);
                break;
            }
            memcpy(spec, cstart, clen);
            spec[clen] = 0;
            uint32_t col;
            if (!osc_color(spec, &col)) {
                klog(L_WARN, "osc: OSC 4;%ld '%s' is not a colour", idx, spec);
            } else if (idx < 0 || idx > 255) {
                klog(L_WARN, "osc: OSC 4;%ld is not a palette entry (0-255)", idx);
            } else {
                term_set_palette_entry((int)idx, col);
                set++;
            }
            p = cend ? cend + 1 : NULL;
        }
        klog(L_DEBUG, "osc: OSC 4 set %d palette entr%s", set, set == 1 ? "y" : "ies");
        break;
    }
    case 10:      /* default foreground (ink) */
    case 11: {    /* default background (paper) */
        const char *what = cmd == 10 ? "foreground" : "background";
        if (!strcmp(s, "?")) {
            /* Query. The X11 16-bit form, so a tool that asks can feed the answer straight back. Called
               directly rather than through the TERM_RESP macro: that one is scoped to the CSI section and is
               #undef'd long before this function. */
            uint32_t cur = cmd == 10 ? term_default_fg() : term_default_bg();
            term_respond(t, "\x1b]%d;rgb:%04x/%04x/%04x\x1b\\", cmd,
                         (((cur >> 16) & 0xff) * 0x101), (((cur >> 8) & 0xff) * 0x101),
                         ((cur & 0xff) * 0x101));
            break;
        }
        uint32_t col;
        if (!osc_color(s, &col)) {
            klog(L_WARN, "osc: OSC %d '%s' is not a colour", cmd, s);
            break;
        }
        uint32_t of = term_default_fg(), ob = term_default_bg();
        uint32_t nf = cmd == 10 ? col : of;
        uint32_t nb = cmd == 11 ? col : ob;
        if (nf == of && nb == ob)
            break;
        /* The old values travel with the flag: by the time main.c acts, the defaults have moved, and the
           sweep is a match-and-replace that has to know what the cells on screen were painted with. */
        t->old_def_fg = of;
        t->old_def_bg = ob;
        term_set_default_colors(nf, nb);
        /* This session first, so output later in the SAME burst already gets the new default. main.c then
           sweeps every other session and re-points the renderer's clear colour and chrome. */
        term_recolor_defaults(t, of, ob, nf, nb);
        t->colors_dirty = 1;
        klog(L_INFO, "osc: OSC %d set the default %s to #%06x — the kai-term.conf %s line no longer "
             "describes this window", cmd, what, col & 0xffffff, what);
        break;
    }
    case 104: {
        /* `OSC 104` puts the whole palette back; `OSC 104;N;M;...` puts only those entries back, which is what
           lets a tool borrow two colours and give them back without wiping a theme the user set in a file. */
        if (!s[0]) {
            term_palette_reset();
            klog(L_INFO, "osc: OSC 104 reset the whole palette to the built-in colours");
            break;
        }
        char *p = (char *)s;
        int n = 0;
        while (p && *p) {
            char *end;
            long idx = strtol(p, &end, 10);
            if (end == p)
                break;
            if (idx < 0 || idx > 255)
                klog(L_WARN, "osc: OSC 104;%ld is not a palette entry (0-255)", idx);
            else
                term_palette_entry_reset((int)idx);
            n++;
            p = (*end == ';') ? end + 1 : NULL;
        }
        klog(L_INFO, "osc: OSC 104 put %d palette entr%s back to the built-in colour",
             n, n == 1 ? "y" : "ies");
        break;
    }
    default:
        break;   /* every other OSC: nothing to do, and nothing left to leak into the grid */
    }
}

/* One C0 control byte, executed the way the VT says. Shared by the ground state and by a control
   sequence that a C0 byte arrives inside of (DEC VT500: the control is executed and the sequence keeps
   collecting; ESC is the exception and is handled by the caller, because it cancels the sequence).
   Keeping it in one place is what makes the two states agree -- they used to disagree, and the CSI copy
   of the rule was missing entirely. */
static void exec_c0(Term *t, uint8_t b)
{
    switch (b) {
    case 0x07: break;                                    /* BEL: nothing to do here */
    case 0x08:                                           /* BS */
        if (t->cx > 0)
            t->cx--;
        cluster_invalidate(t);
        t->wrap_next = 0;
        break;
    case 0x09: {                                         /* HT: next tab stop */
        int next = ((t->cx / 8) + 1) * 8;
        t->cx = imin_(next, t->g.cols - 1);
        cluster_invalidate(t);
        t->wrap_next = 0;
        break;
    }
    case 0x0a: case 0x0b: case 0x0c:                     /* LF / VT / FF */
        linefeed(t);
        break;
    case 0x0d:                                           /* CR */
        t->cx = 0;
        cluster_invalidate(t);
        t->wrap_next = 0;
        break;
    default: break;                                      /* every other C0: ignored */
    }
}

void term_parse(Term *t, const uint8_t *buf, size_t len)
{
    Parser *ps = &t->ps;
    ps->t = t;
    /* Was the input line already the remembered prompt BEFORE this burst? The end-of-burst rule below
       needs the difference, not the state: only a burst that CHANGED the line into the prompt is evidence
       that the line became empty. Measured 2026-10-19 (`.scratch/s86/follow2`): after the file manager
       re-typed the user's text, fish's next burst repainted only prompt chrome (OSC 7 / OSC 0 / DECSET /
       `ESC[K` / a cursor move) and touched the input row not at all, so the row still equalled the
       snapshot; the old steady-state test called that "the prompt is back, the line is provably empty",
       closed the echo window over bytes that had not been echoed yet, and every later burst then
       RE-TOOK the snapshot — including the burst carrying the echo of the text Kai-Term itself had typed.
       The snapshot became `prompt + the user's text`, so a prompt holding text answered `term_at_prompt()`
       as EMPTY and the next navigation appended `cd <dir>` to it and ran the result. */
    int prompt_matched_before = prompt_row_matches(t);

    for (size_t i = 0; i < len; i++) {
        uint8_t b = buf[i];

        switch (ps->state) {

        case PS_GROUND:
            if (b == 0x1b) { ps->state = PS_ESC; continue; }
            if (b < 0x20) { exec_c0(t, b); continue; }
            if (b < 0x80) { putch(t, b); continue; }
            if (b >= 0xc2 && b <= 0xdf) { ps->utf8_cp = b & 0x1f; ps->utf8_need = 1; ps->state = PS_UTF8; continue; }
            if (b >= 0xe0 && b <= 0xef) { ps->utf8_cp = b & 0x0f; ps->utf8_need = 2; ps->state = PS_UTF8; continue; }
            if (b >= 0xf0 && b <= 0xf4) { ps->utf8_cp = b & 0x07; ps->utf8_need = 3; ps->state = PS_UTF8; continue; }
            continue;   /* invalid UTF-8 byte */

        case PS_UTF8:
            if ((b & 0xc0) == 0x80) {
                ps->utf8_cp = (ps->utf8_cp << 6) | (b & 0x3f);
                if (--ps->utf8_need == 0) {
                    if (ps->utf8_cp < 0x80 || (ps->utf8_cp >= 0xD800 && ps->utf8_cp <= 0xDFFF) || ps->utf8_cp > 0x10FFFF) {
                        /* overlong / surrogate / out-of-range: drop */
                    } else {
                        putch(t, (uint32_t)ps->utf8_cp);
                    }
                    ps->state = PS_GROUND;
                }
            } else {
                ps->state = PS_GROUND;
                i--;   /* reprocess this byte in ground state */
            }
            continue;

        case PS_ESC:
            ps->state = PS_GROUND;
            switch (b) {
            case '[': ps->state = PS_CSI; ps->npar = 0;
                      ps->priv = 0; ps->bang = 0; ps->gt = 0; ps->inter = 0;
                      for (int k = 0; k < 32; k++) ps->par[k] = -1;  /* -1 = not seen yet */
                      break;
            case ']': ps->state = PS_OSC; ps->osc_len = 0; break;
            /* APC: the kitty graphics protocol lives here (ESC _ G). DCS (ESC P) is discarded
               whole: sixel and friends would otherwise be parsed as text and paint the payload
               into the grid as garbage characters. */
            case '_': ps->state = PS_APC; ps->apc_len = 0; ps->apc_drop = 0;
                      ps->apc_kind = 0; break;
            case 'P': ps->state = PS_DCS; break;
            case 'D': linefeed(t); break;
            case 'M': cluster_invalidate(t);
                      if (t->cy == t->top) scroll_down(t, t->top, t->bot); else if (t->cy > 0) t->cy--; break;
            case 'E': linefeed(t); t->cx = 0; break;
            case '7': t->save_cx = t->cx; t->save_cy = t->cy; break;
            case '8': set_cursor_pos(t, t->save_cy, t->save_cx); break;
            case 'c': {
                /* A reset rebuilds Term from scratch. The shell integration announcement is the one piece
                   of state that must survive it: the script announces ONCE, at shell startup, so dropping it
                   here would silently disable the integration path for the rest of the session after a
                   `reset` (or any program that sends ESC c). Everything else about the shell's line is
                   genuinely invalidated by a reset and is dropped, as it should be. */
                int saved_int = t->shell_int;
                int saved_gen = t->shell_int_gen;
                char saved_ack[FM_PATH_MAX];
                memcpy(saved_ack, t->shell_cd_ack, sizeof(saved_ack));
                term_init(t, t->g.rows, t->g.cols, t->g.scrollback_max);
                t->shell_int = saved_int;
                t->shell_int_gen = saved_gen;   /* main.c's Tab.int_gen is still the one it saw */
                memcpy(t->shell_cd_ack, saved_ack, sizeof(saved_ack));
                break;
            }
            case '(': case ')': case '*': case '+': ps->state = PS_IGNORE; break;
            default: break;
            }
            continue;

        case PS_IGNORE:
            ps->state = PS_GROUND;   /* consume one charset-designator byte */
            continue;

        case PS_CSI:
            if (b >= '0' && b <= '9') {
                if (ps->npar < 32) {
                    if (ps->par[ps->npar] < 0)
                        ps->par[ps->npar] = b - '0';
                    else if (ps->par[ps->npar] < 100000)
                        ps->par[ps->npar] = ps->par[ps->npar] * 10 + (b - '0');
                }
                continue;
            }
            if (b == ';') {
                if (ps->npar < 31) {
                    ps->npar++;
                    ps->par[ps->npar] = -1;   /* param omitted unless digits follow */
                }
                continue;
            }
            if (b == '?') { ps->priv = 1; continue; }
            if (b == '!') { ps->bang = 1; continue; }
            if (b == '>') { ps->gt = 1; continue; }
            if (b >= ' ' && b <= '/') { ps->inter = b; continue; }   /* intermediate bytes: kept, see Parser.inter */
            if (b >= 0x40 && b <= 0x7e) {
                for (int k = 0; k < 32; k++)
                    if (ps->par[k] < 0)
                        ps->par[k] = 0;   /* omitted/empty params default to 0 */
                csi_dispatch(t, b);
                ps->state = PS_GROUND;
                continue;
            }
            /* ESC cancels the sequence and starts a new escape (xterm does the same); the bytes after it
               belong to the new sequence, not to this one. */
            if (b == 0x1b) { ps->state = PS_ESC; continue; }
            /* Any other C0 inside a control sequence is EXECUTED and the sequence KEEPS collecting --
               DEC VT500 / xterm. It used to do `state = PS_GROUND; i--;`, which ended the sequence at the
               control byte, so the mode it was setting was never applied AND its remaining bytes were
               printed as text. Measured 2026-10-21 (.scratch/s97/hprobe2): ESC [ ? 2004 <TAB> h left
               bracketed_paste at 0 and painted an `h` into the grid; ESC [ ? 1049 <TAB> h never entered the
               alternate screen and painted `h`; ESC [ ? 1 <TAB> h left DECCKM off and painted `h`. A final
               byte of `h` is what every mode-ENABLING sequence ends with, which is why the leaked character
               the user sees is an `h`. */
            if (b < 0x20) { exec_c0(t, b); continue; }
            continue;

        case PS_OSC:
            if (b == 0x07) { osc_end(ps); ps->state = PS_GROUND; continue; }
            if (b == 0x1b) {
                /* peek next byte for ST (ESC \) */
                if (i + 1 < len && buf[i + 1] == '\\') {
                    i++;
                    osc_end(ps);
                    ps->state = PS_GROUND;
                } else {
                    osc_end(ps);
                    ps->state = PS_ESC;
                }
                continue;
            }
            if (ps->osc_len < (int)sizeof(ps->osc) - 1)
                ps->osc[ps->osc_len++] = (char)b;
            continue;

        case PS_APC:
            if (b == 0x07) { apc_end(ps); ps->state = PS_GROUND; continue; }
            if (b == 0x1b) {
                if (i + 1 < len && buf[i + 1] == '\\') {
                    i++;
                    apc_end(ps);
                    ps->state = PS_GROUND;
                } else {
                    apc_end(ps);
                    ps->state = PS_ESC;
                }
                continue;
            }
            if (ps->apc_len == 0 && !ps->apc_kind) {
                ps->apc_kind = b;      /* 'G' = graphics; anything else is dropped at apc_end.
                                          The kind byte is NOT part of the payload. */
                continue;
            }
            apc_put(ps, b);
            continue;

        case PS_DCS:
            if (b == 0x07) { ps->state = PS_GROUND; continue; }
            if (b == 0x1b) {
                if (i + 1 < len && buf[i + 1] == '\\') {
                    i++;
                    ps->state = PS_GROUND;
                } else {
                    ps->state = PS_ESC;
                }
                continue;
            }
            continue;                  /* discarded: sixel and every other DCS extension */

        default:
            ps->state = PS_GROUND;
            continue;
        }
    }

    /* Rows may have been evicted, rotated or re-wrapped by what was just parsed; images anchored
       to rows that no longer exist are dropped here, once per burst rather than per escape
       sequence. */
    image_prune(t);

    /* The prompt snapshot, once per burst for the same reason as image_prune() (see the prompt section
       above). While input_pending is set the bytes arriving here are the echo of what the terminal sent,
       or readline redrawing the line the user is typing — remembering THAT as "the prompt" would let the
       file manager write on top of it, which is the whole bug this machinery exists to prevent. And while
       the ALTERNATE screen is up, t->g is a full-screen application's screen, not the shell's: snapshotting
       it is what used to force alt_screen_leave() to throw the snapshot away (and losing it disabled the
       shell follow for the rest of the session, s76). The snapshot always describes the MAIN grid. */
    if (t->alt_screen)
        ;
    else if (!t->input_pending)
        prompt_snapshot(t);
    else if (prompt_matched_before == 0 && prompt_row_matches(t))
        /* The application has CHANGED the row into the remembered prompt — the line is provably empty
           again (readline's erase, an arrow-key redraw, the shell's repaint after Ctrl+U). Close the echo
           window on that evidence only: a burst that leaves the row exactly as it was proves nothing about
           what happened to the bytes we sent, and an application that shows nothing leaves it open. */
        t->input_pending = 0;
}
