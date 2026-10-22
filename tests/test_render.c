/* Unit tests for the renderer's decisions that need no display: render_chrome() (the configured palette
   -> the colours a frame's chrome is drawn with) and render_cursor() / render_hover() (a request -> what
   one cell is painted as). Both read plain structs, so a test can fill one by hand. The wiring in main()
   and the draw loop in render_draw() are in no test binary; group_source_wiring() pins them with source
   scans, which fail if a value stops being read at the pixel end. */
#include "kai-term.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     /* unlink(): the scanner self-pin writes a probe file and removes it */

static int fails, checks;

#define CHECK(cond, msg) do {                                  \
    checks++;                                                  \
    if (!(cond)) {                                             \
        fails++;                                               \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);  \
    }                                                          \
} while (0)

/* The same blend src/render.c and src/fm.c each keep a private copy of. Copied here on purpose: a check that
   called the code under test to predict the code under test would pin nothing. */
static uint32_t mix_ref(uint32_t a, uint32_t b, int k100)
{
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        int av = (int)((a >> sh) & 0xff), bv = (int)((b >> sh) & 0xff);
        int v = av + (bv - av) * k100 / 100;
        out |= (uint32_t)(v < 0 ? 0 : (v > 255 ? 255 : v)) << sh;
    }
    return out;
}

/* A Render the way render_init() fills its colour fields; everything else in the struct is EGL/GL state this
   test never touches. */
static void render_from_config(Render *r, const Config *c)
{
    memset(r, 0, sizeof *r);
    r->vw = 800;
    r->vh = 600;
    r->opacity = fclamp_(c->opacity, 0.0f, 1.0f);
    r->bg = c->bg;
    r->fg = c->fg;
    r->cursor_color = c->cursor_color;
    r->selection_color = c->selection_color;
}

/* ---------- the cursor colour reaches the pixel ---------- */

static void group_cursor_color(void)
{
    Config c;
    config_default(&c);
    Render r;
    Chrome ch;

    /* 1. The built-in default is unchanged: a user who configured nothing gets the cursor they always got. */
    CHECK(c.cursor_color == CURSOR_COL, "default config: cursor_color is still the built-in CURSOR_COL");
    render_from_config(&r, &c);
    render_chrome(&r, &ch);
    CHECK(ch.cursor == CURSOR_COL, "default config: the chrome cursor colour is the built-in default");

    /* 2. A configured value is what the visible session's cursor is drawn with — on the buggy code this returned
       CURSOR_COL no matter what the file said. */
    c.cursor_color = 0xffff0000u;
    render_from_config(&r, &c);
    render_chrome(&r, &ch);
    CHECK(ch.cursor == 0xffff0000u, "cursor_color = ff0000: the ACTIVE pane's cursor is drawn with it");

    /* 3. The dimmed cursor of a session that is drawn but not the visible one is derived from the configured
       value, not from the macro. */
    CHECK(ch.cursor_off == mix_ref(0xffff0000u, c.bg, 55),
          "cursor_color = ff0000: the INACTIVE session's cursor is that colour mixed with the background");

    /* 4. Two different configured colours give two different dimmed colours; a shared base would make them
       identical. */
    c.cursor_color = 0xff00ff00u;
    render_from_config(&r, &c);
    Chrome g2;
    render_chrome(&r, &g2);
    CHECK(g2.cursor != ch.cursor && g2.cursor_off != ch.cursor_off,
          "two different cursor_color values change BOTH the active and the inactive cursor");

    /* 5. The dimming follows the configured background too. Compared with g2 — same cursor colour, only bg
       differs — so the difference can only come from the background. */
    Config d = c;
    d.bg = 0xff4a0710u;
    render_from_config(&r, &d);
    Chrome g3;
    render_chrome(&r, &g3);
    CHECK(g3.cursor_off != g2.cursor_off, "a different background changes the dimmed cursor");
    CHECK(g3.cursor == g2.cursor, "a different background does NOT change the active cursor colour");

    /* 6. Chrome colours stay OPAQUE in Chrome: the window alpha is applied per quad in render_draw(), so a
       configured colour can never be silently halved on the way here. g2 differs only in opacity. */
    Config e = c;
    e.opacity = 0.3f;
    render_from_config(&r, &e);
    Chrome g4;
    render_chrome(&r, &g4);
    CHECK(((g4.cursor >> 24) & 0xff) == 0xff, "the chrome cursor colour keeps alpha 0xff");
    CHECK(g4.cursor == g2.cursor && g4.cursor_off == g2.cursor_off,
          "window opacity does not change the derived chrome colours");

    /* 7. The highlight colour is the configured one, in all three places that mean "this is selected" — the
       selection band, the visible session's bar item and the focus border. */
    CHECK(ch.sel_bg == SEL_BG && ch.item_on_bg == SEL_BG && ch.border == SEL_BG,
          "default config: the selection band, the active bar item and the focus border are SEL_BG");
    c.selection_color = 0xff220088u;
    render_from_config(&r, &c);
    Chrome g5;
    render_chrome(&r, &g5);
    CHECK(g5.sel_bg == 0xff220088u && g5.item_on_bg == 0xff220088u && g5.border == 0xff220088u,
          "selection_color = 220088: the band, the active bar item AND the focus border all use it");
    CHECK(g5.cursor == g2.cursor && g5.cursor_off == g2.cursor_off,
          "a custom selection colour leaves the cursor colours alone (compared with g2: same cursor colour, "
          "only the highlight differs)");
    CHECK(g5.sep == mix_ref(c.fg, c.bg, 65) && g5.item_off_bg == mix_ref(c.bg, c.fg, 10)
          && g5.item_off_fg == mix_ref(c.fg, c.bg, 45),
          "the bar's inactive colours are mixed from the configured fg/bg, not from the highlight");

    /* 8. The confirm-quit row: the highlight for its field, the default ink for its text. One configured colour
       owns every "this is the thing in this window" signal — the band, the active bar item, the focus border
       and the question. */
    CHECK(ch.notice_bg == SEL_BG && ch.notice_fg == c.fg,
          "default config: the question row's field is SEL_BG and its text is the default ink");
    CHECK(g5.notice_bg == 0xff220088u && g5.notice_fg == c.fg,
          "selection_color = 220088: the question row's field is drawn with it too");
    Config ink = c;
    ink.fg = 0xff11ee22u;
    render_from_config(&r, &ink);
    Chrome g6;
    render_chrome(&r, &g6);
    CHECK(g6.notice_fg == 0xff11ee22u && g6.notice_bg == g5.notice_bg,
          "a different ink changes the question's TEXT and not its field (compared with g5)");

    /* 9. The confirm-quit DIALOG, drawn OVER the terminal. With no GL blending "in front of" only exists
       if the colour differs from what is behind, so a field the exact colour of the cells it covers is a
       box nobody can see. Border and Yes take the highlight colour, No the palette's red. */
    CHECK(ch.dialog_bg == mix_ref(c.bg, c.fg, 15) && ch.dialog_bg != c.bg,
          "default config: the box's field is a lifted paper, NOT the paper it is drawn over");
    CHECK(ch.dialog_bg != ch.item_off_bg,
          "the box's field is its own mix, not the bar's inactive item reused");
    CHECK(ch.dialog_border == SEL_BG && ch.dialog_yes == SEL_BG,
          "default config: the box's border and its Yes label take SEL_BG, like the band and the focus border");
    CHECK(ch.dialog_fg == c.fg && ch.dialog_btn_bg == c.bg,
          "default config: the sentence uses the default ink and the button fields the terminal's own paper");
    CHECK(ch.dialog_no == 0xffcc5555u && ch.dialog_no == term_palette()[1],
          "default config: the No label takes the palette's red, entry 1 — not a literal in the renderer");
    CHECK(g5.dialog_border == 0xff220088u && g5.dialog_yes == 0xff220088u &&
          g5.dialog_no == ch.dialog_no,
          "selection_color = 220088: the box's border and Yes move with it, the red does not");
    /* The red follows the palette rather than being a constant, so an application that repaints the palette with
       OSC 4 repaints the button exactly as it repaints a cell. The palette is process-wide state, so it is
       restored: leaving it changed would let a later check read a colour this one invented. */
    term_set_palette_entry(1, 0xff00ff00u);
    Chrome g7;
    render_chrome(&r, &g7);
    CHECK(g7.dialog_no == 0xff00ff00u && g7.dialog_no != ch.dialog_no,
          "a repainted palette repaints the No label — the colour is live state, not a frozen default");
    term_palette_entry_reset(1);          /* one entry back to built-in, the same call OSC 104;1 makes */
    Chrome g8;
    render_chrome(&r, &g8);
    CHECK(g8.dialog_no == ch.dialog_no,
          "restoring the built-in red restores the label (the palette is process-wide: this group must not "
          "leave a colour behind for the checks that follow)");
}

/* ---------- the visual bell's colour --------------------------------------------------------
   render_draw() is in no test binary, so its two decisions about a flash — how far the paper moves
   toward the ink, and the ends of the range — are checked at the function that makes them. */
static void group_bell_color(void)
{
    const uint32_t paper = 0xff101014u, ink = 0xffe5e5e5u;   /* the shipped template's background/foreground */
    CHECK(render_bell(paper, ink, 0) == paper,
          "bell: strength 0 is the paper itself — the last frame of a fade is indistinguishable from no flash");
    CHECK(render_bell(paper, ink, -7) == paper, "bell: a negative strength is the paper too");
    /* The -7 above cannot tell a guard from no guard: 25 * -7 / 1000 is 0 in C, so a mix that skipped the guard
       would still hand back the paper. A strength far below 0 separates them — without the guard the percent
       goes negative and every channel clamps toward BLACK, a colour nobody asked for. */
    CHECK(render_bell(paper, ink, -1000) == paper,
          "bell: a strength far below 0 is the paper as well — a negative mix percent would drag every "
          "channel toward black, not toward the paper");
    CHECK(render_bell(paper, ink, 1000) == mix_ref(paper, ink, BELL_MIX_MAX),
          "bell: at full strength the paper moves exactly BELL_MIX_MAX percent toward the ink");
    CHECK(render_bell(paper, ink, 1000) != paper && render_bell(paper, ink, 1000) != ink,
          "bell: the flash is visible, but it is never the ink itself — the text has to stay readable over it");
    CHECK(render_bell(paper, ink, 5000) == render_bell(paper, ink, 1000),
          "bell: a strength over 1000 is clamped, so no channel can pass the colour it is mixing toward");
    CHECK(render_bell(paper, ink, 500) == mix_ref(paper, ink, BELL_MIX_MAX * 500 / 1000),
          "bell: half strength is half the mix — the fade is one quad changing colour, not more quads");
    int mono = 1;
    int prevb = -1;
    for (int s = 0; s <= 1000; s += 25) {
        int v = (int)((render_bell(paper, ink, s) >> 16) & 0xff);
        if (v < prevb)
            mono = 0;
        prevb = v;
    }
    CHECK(mono, "bell: every step of the fade moves the same way — no flicker, no getting brighter");
    /* A light theme has to flash too: mixing toward a DARK ink is what keeps this from being a feature of dark
       palettes only. */
    CHECK(render_bell(ink, paper, 1000) != ink,
          "bell: a light paper with dark ink still flashes — the mix is toward the ink either way");
    CHECK((render_bell(ink, paper, 1000) >> 16) < (ink >> 16),
          "bell: and on a light paper it moves DOWN toward the ink, not up");
    CHECK((render_bell(paper, ink, 1000) >> 24) == 0xffu,
          "bell: the alpha stays opaque — a transparent window must not get a hole in it");
    CHECK(render_bell(0xff808080u, 0xff808080u, 1000) == 0xff808080u,
          "bell: a theme whose ink equals its paper flashes to that same colour (there is nothing to show, "
          "and inventing a colour would be a guess)");
}

/* ---------- the two source scans that cover what no headless test can run ---------- */

/* Count code tokens equal to `word` in a source file; -1 when the file cannot be opened, so a moved file
   fails loudly. Whole tokens only (a substring match would find CURSOR_COL inside a longer name), and
   comments skipped — a comment may NAME a macro the code never reads. */
static int is_wordch(int c)
{
    return c == '_' || isalnum((unsigned char)c);
}

static int src_word_count(const char *path, const char *word)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    size_t wl = strlen(word);
    int n = 0, in_block = 0;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p) {
            if (in_block) {
                if (p[0] == '*' && p[1] == '/') { in_block = 0; p += 2; continue; }
                p++;
                continue;
            }
            if (p[0] == '/' && p[1] == '*') { in_block = 1; p += 2; continue; }
            if (p[0] == '/' && p[1] == '/') break;
            if (!is_wordch(*p)) { p++; continue; }
            char *s = p;
            while (*p && is_wordch(*p))
                p++;
            if ((size_t)(p - s) == wl && !memcmp(s, word, wl))
                n++;
        }
    }
    fclose(f);
    return n;
}

/* Count a LITERAL substring in the code (comments skipped as src_word_count skips them). A word count also
   counts a token that only appears in a comment, so a check about an assignment has to name the assignment
   (`rq.notice_on`) and not just the field. */
static int src_sub_count(const char *path, const char *needle)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    size_t nl = strlen(needle);
    int n = 0, in_block = 0;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p) {
            if (in_block) {
                if (p[0] == '*' && p[1] == '/') { in_block = 0; p += 2; continue; }
                p++;
                continue;
            }
            if (p[0] == '/' && p[1] == '*') { in_block = 1; p += 2; continue; }
            if (p[0] == '/' && p[1] == '/') break;
            if (!strncmp(p, needle, nl)) { n++; p += nl; continue; }
            p++;
        }
    }
    fclose(f);
    return n;
}

/* Does the render_init() call in src/main.c pass `arg`? Text check, not execution — the honest limit of
   testing a call site that lives in main(). */
static int src_call_passes_arg(const char *path, const char *call, const char *arg)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    char line[4096];
    int in_call = 0, found = 0;
    while (fgets(line, sizeof line, f)) {
        if (!in_call) {
            if (strstr(line, call))
                in_call = 1;
            else
                continue;
        }
        if (strstr(line, arg))
            found = 1;
        if (strstr(line, ");"))
            break;
    }
    fclose(f);
    return found;
}

/* Does the first CODE occurrence of `first` come before the first of `second`? That is how DRAW ORDER is
   pinned: with no GL blending a later quad simply wins, and no headless test can run the draw loop. The
   needles must be code, one occurrence per layer. */
static int src_code_before(const char *path, const char *first, const char *second)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    char buf[262144];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    /* Blank the comments, then look for the two needles in what is left. */
    for (size_t i = 0; i + 1 < n; ) {
        if (buf[i] == '/' && buf[i + 1] == '*') {
            size_t j = i + 2;
            while (j + 1 < n && !(buf[j] == '*' && buf[j + 1] == '/')) {
                if (buf[j] == '\n')
                    j++;
                else
                    buf[j++] = ' ';
            }
            i = j < n ? j + 2 : n;
            continue;
        }
        if (buf[i] == '/' && buf[i + 1] == '/') {
            size_t j = i + 2;
            while (j < n && buf[j] != '\n')
                buf[j++] = ' ';
            i = j;
            continue;
        }
        i++;
    }
    char *p = strstr(buf, first);
    char *q = strstr(buf, second);
    return p && q && p < q ? 1 : 0;
}

/* ---------- the cursor's shape: one pure decision, the whole truth table ----------
   render_cursor() is the seam that replaced the rule inside the draw loop: no GL, no font, no Term, so
   every combination of (shape, visible, selection band, cell size, width) is pinned here. Three layers
   of a frame ask it about the same cell and must agree, or the cursor erases itself. */
static void group_cursor_shape(void)
{
    CursorDraw d;
    const int cw = 9, ch = 18;          /* a plausible cell at font_size 14 */
    const int asc = 13, desc = 5;       /* its font's metrics: the baseline sits 5 px above the cell's bottom */

    /* 1. The default is a bar: a separate quad, not the cell's background. */
    render_cursor(CUR_UNSET, 1, 0, cw, ch, 2, asc, desc, &d);
    CHECK(d.quad == 1 && d.block == 0 && d.invert == 0 && d.tint == 0,
          "no DECSCUSR seen: a separate quad, and NOT the cell's background");
    CHECK(d.x == 0 && d.y == 0 && d.w == 2 && d.h == ch,
          "a 2 px bar at the LEFT edge of the cell, the full height of it");
    render_cursor(CUR_BAR, 1, 0, cw, ch, 2, asc, desc, &d);
    CHECK(d.quad == 1 && d.x == 0 && d.y == 0 && d.w == 2 && d.h == ch,
          "an application asking for a bar explicitly gets exactly the default");

    /* 2. Not visible (DEC 25l, scrolled back, or the blink phase off) draws NOTHING, in every shape. */
    const int styles[] = { CUR_UNSET, CUR_BLOCK, CUR_BAR, CUR_UNDERLINE };
    for (unsigned i = 0; i < sizeof styles / sizeof styles[0]; i++) {
        render_cursor(styles[i], 0, 0, cw, ch, 2, asc, desc, &d);
        CHECK(d.block == 0 && d.quad == 0 && d.invert == 0 && d.tint == 0 &&
              d.x == 0 && d.y == 0 && d.w == 0 && d.h == 0,
              "an invisible cursor emits no block, no quad and no ink change, whatever the shape");
    }

    /* 3. The width clamp. Wider than a third of a cell is a block wearing a hat, and 0 px would be no
       cursor at all — so the clamp has two ends and the lower one matters at a small font size, where a
       cell can be narrower than 3 px and cell/3 would be 0. */
    render_cursor(CUR_BAR, 1, 0, cw, ch, 40, asc, desc, &d);
    CHECK(d.w == cw / 3 && d.h == ch, "a 40 px bar in a 9 px cell is cut to a third of the cell (3 px)");
    render_cursor(CUR_BAR, 1, 0, cw, ch, 1, asc, desc, &d);
    CHECK(d.w == 1, "a 1 px bar stays 1 px");
    render_cursor(CUR_BAR, 1, 0, 2, ch, 2, asc, desc, &d);
    CHECK(d.w == 1, "in a 2 px-wide cell the bar is 1 px — never 0, which would draw nothing");
    render_cursor(CUR_BAR, 1, 0, 1, ch, 2, asc, desc, &d);
    CHECK(d.w == 1, "and in a 1 px cell it is still 1 px, so the cursor cannot vanish");

    /* 4. The underline DECSCUSR asks for: full cell width, UNDER the baseline, as thick as the bar. A
       quad's y is measured UP from the cell's bottom edge, so `y == ch - 2` put the line in the top two
       rows — a strikethrough. "Under the text" is about the baseline (ch - asc). */
    render_cursor(CUR_UNDERLINE, 1, 0, cw, ch, 2, asc, desc, &d);
    CHECK(d.quad == 1 && d.x == 0 && d.w == cw && d.h == 2 && d.y == 0,
          "an underline is the full width of the cell, 2 px tall, at the bottom of the descender band");
    CHECK(d.y + d.h <= ch - asc,
          "an underline never rises above the baseline — the check that the old one could not be, because it "
          "restated the implementation's own (wrong) idea of 'the bottom edge'");
    CHECK(d.y + d.h < ch,
          "and it is not the top of the cell either — the s134 report, pinned by name");
    render_cursor(CUR_UNDERLINE, 1, 0, cw, ch, 40, asc, desc, &d);
    CHECK(d.h == ch / 3 && d.y == 0,
          "a 40 px underline is cut to a third of the cell's HEIGHT (6 px), still at the bottom of the band");
    render_cursor(CUR_UNDERLINE, 1, 0, cw, 2, 2, 2, 0, &d);
    CHECK(d.h == 1 && d.y == 0, "in a 2 px-tall cell the underline is 1 px — never 0, and never outside the cell");

    /* 5. A block is today's behaviour, unchanged: it IS the cell's own background layer, so it steps aside where a
       selection band is painted and the ink is tinted with the cursor colour instead. */
    render_cursor(CUR_BLOCK, 1, 0, cw, ch, 2, asc, desc, &d);
    CHECK(d.block == 1 && d.invert == 1 && d.tint == 0 && d.quad == 0 && d.w == 0,
          "a block over no band: pass 1 paints the cell background and pass 3 inverts the ink");
    render_cursor(CUR_BLOCK, 1, 1, cw, ch, 2, asc, desc, &d);
    CHECK(d.block == 0 && d.invert == 0 && d.tint == 1 && d.quad == 0,
          "a block under a band steps aside for it and tints the ink — the rule that must not change");

    /* 6. A bar or an underline does NOT step aside: its quad is emitted in batch B AFTER the bands, so it survives
       where a selection ends on the cursor cell. That is the whole reason it is a separate quad. */
    render_cursor(CUR_BAR, 1, 1, cw, ch, 2, asc, desc, &d);
    CHECK(d.quad == 1 && d.block == 0 && d.tint == 0 && d.invert == 0,
          "a bar under a band still emits its quad and leaves the ink alone");
    render_cursor(CUR_UNDERLINE, 1, 1, cw, ch, 2, asc, desc, &d);
    CHECK(d.quad == 1 && d.block == 0 && d.tint == 0 && d.invert == 0,
          "and so does an underline");

    /* 7. The invariants, over every combination — the ones a frame depends on and a hand-written table misses:
       (a) a cell is never painted as BOTH a block and a quad (the cursor would be drawn twice),
       (b) invert only ever comes with block (inverting ink with nothing behind it is a blank cell),
       (c) a VISIBLE cursor always does something: a block, a quad, or a tint of the ink,
       (d) the quad's rect is always inside the cell, at every cell size. */
    const int cells[][2] = { { 1, 1 }, { 2, 2 }, { 3, 3 }, { 1, 18 }, { 20, 4 }, { 9, 18 }, { 40, 30 } };
    for (unsigned s = 0; s < sizeof styles / sizeof styles[0]; s++)
        for (int vis = 0; vis <= 1; vis++)
            for (int band = 0; band <= 1; band++)
                for (unsigned c2 = 0; c2 < sizeof cells / sizeof cells[0]; c2++) {
                    int w_px = 2 + (int)c2;      /* every configured width from 2 to 8 */
                    int cwh = cells[c2][1];
                    render_cursor(styles[s], vis, band, cells[c2][0], cwh, w_px,
                                  cwh * 3 / 4, cwh / 4, &d);
                    CHECK(!(d.block && d.quad),
                          "invariant: a cell is never both a block and a separate quad");
                    CHECK(!d.invert || d.block,
                          "invariant: ink is only inverted where a block was actually painted");
                    CHECK(!vis || d.block || d.quad || d.tint,
                          "invariant: a visible cursor always paints something in one of the three passes");
                    CHECK(d.x >= 0 && d.y >= 0 && d.x + d.w <= cells[c2][0] &&
                          d.y + d.h <= cwh,
                          "invariant: the cursor's rect never leaves its own cell");
                }
}

/* ---------- the hover underline's rect: the same seam, one pure decision ----------
   WHETHER a link is underlined is src/url.c's decision (checked in tests/test_vt.c); here it is the rect
   one cell draws — a rule written inside the draw loop is a claim. Four rules, all the kind that break
   by arithmetic and show only as a look: never 0 px tall, never so thick it reads as a block, the full
   cell width so a run reads as ONE address, and the position written against the cell's BASELINE. */
static void group_hover_shape(void)
{
    HoverDraw d;

    /* 1. A plausible cell (9x18, ascent 13, descent 5, so the baseline sits 5 px above the cell's bottom edge):
          a hairline at the bottom of the descender band, which for such a font IS the cell's bottom edge. */
    const int cw = 9, ch = 18, asc = 13, desc = 5;
    render_hover(cw, ch, asc, desc, &d);
    CHECK(d.x == 0 && d.w == cw,
          "hover: the line spans the WHOLE cell width, so neighbouring cells join into one underline");
    CHECK(d.h == 1 && d.y == 0,
          "hover: 1 px at that size, at the bottom of the descender band — the CUR_UNDERLINE position, asked of the "
          "same underline_y() the cursor asks, so the two underlines cannot disagree about where the line is");
    CHECK(d.y + d.h <= ch - asc,
          "hover: the line's top edge never rises above the baseline — 'under the text' is a statement about the "
          "baseline, not about the cell's edges, which is the distinction s133 got wrong");
    CHECK(d.y + d.h < ch,
          "hover: and it is NOT the top row of the cell — the s134 report (\"over the text instead of under it\"), "
          "pinned so the old formula cannot come back");

    /* 2. The invariants, over the installed font at 7 pixel sizes AND over faces this machine has not
       got: leading under the baseline, an ascent filling the cell, a descender deeper than the cell,
       cells 1..36 px tall. THE TABLE IS SORTED BY cell_h: the last row compares with the one before. */
    const int cells[][4] = {   /* cell_w, cell_h, ascent, descent — Liberation Mono at px 10,12,14,16,20,24,32 */
        { 1, 1, 1, 0 }, { 3, 4, 4, 1 },
        { 6, 11, 9, 3 }, { 7, 14, 10, 4 }, { 8, 16, 12, 5 },
        { 10, 18, 14, 5 }, { 10, 18, 18, 0 }, { 10, 18, 15, 20 },
        { 10, 21, 13, 4 },     /* leading under the baseline: cell_h > ascent + descent */
        { 12, 23, 17, 6 }, { 14, 27, 20, 8 }, { 40, 30, 22, 8 },
        { 10, 36, 30, 2 },     /* a hairline face: the descender band is THINNER than the line */
        { 19, 36, 27, 10 }
    };
    int bad_zero = 0, bad_cap = 0, bad_in = 0, bad_base = 0, bad_band = 0, bad_w = 0, thinner = 0;
    int prev_h = -1;
    for (size_t i = 0; i < sizeof cells / sizeof cells[0]; i++) {
        int cwh = cells[i][0], chh = cells[i][1], a = cells[i][2], de = cells[i][3];
        render_hover(cwh, chh, a, de, &d);
        int baseline = imax_(chh - a, 0);
        if (d.h < 1)
            bad_zero++;
        if (d.h > imax_(1, chh / 3))
            bad_cap++;
        if (d.y < 0 || d.y + d.h > chh)
            bad_in++;
        if (baseline >= d.h && d.y + d.h > baseline)
            bad_base++;
        if (baseline - de >= 0 && de >= d.h && d.y != baseline - de)
            bad_band++;
        if (d.x != 0 || d.w != cwh)
            bad_w++;
        if (d.h < prev_h)
            thinner++;
        prev_h = d.h;
    }
    CHECK(bad_zero == 0,
          "hover invariant: never 0 px tall at any cell size — a 0 px quad is an invisible feature, not a thin one");
    CHECK(bad_cap == 0, "hover invariant: never thicker than a third of the cell, the same cap the underline cursor obeys");
    CHECK(bad_in == 0, "hover invariant: the rect is always inside its own cell (0 <= y, and y + h <= cell_h)");
    CHECK(bad_base == 0,
          "hover invariant: whenever the cell has room under the baseline the whole line fits under it — at every "
          "size in the table, including the faces with no descender band and a descender deeper than the cell");
    CHECK(bad_band == 0,
          "hover invariant: when the cell has LEADING under the baseline the line sits at the bottom of the "
          "DESCENDER band, not at the bottom of the cell — a bottom-of-the-cell rule drifts off the text for such a font");
    CHECK(bad_w == 0, "hover invariant: x is 0 and w is the whole cell, at every size");
    CHECK(thinner == 0, "hover invariant: the line never gets thinner as the cell gets taller");

    /* 3. The measured thickness AND the measured place, written out. The first seven rows are Liberation
       Mono at px 10..32 — 1 px up to a 23 px cell, then one more px per 12 px of cell. Each of those
       faces has a descender deeper than the room its cell leaves under the baseline, so y is 0; the last
       row is the leading case, where the line lifts off the cell's bottom edge and follows the text. */
    const int want[][5] = {   /* cell_h, ascent, descent, want h, want y */
        { 11, 9, 3, 1, 0 }, { 14, 10, 4, 1, 0 }, { 16, 12, 5, 1, 0 }, { 18, 14, 5, 1, 0 },
        { 23, 17, 6, 1, 0 }, { 27, 20, 8, 2, 0 }, { 36, 27, 10, 3, 0 },
        { 21, 13, 4, 1, 4 },   /* leading: the line follows the text, 4 px up from the cell's bottom edge */
        { 36, 30, 2, 3, 3 }    /* a band thinner than the line: it hangs from the baseline, not from the band */
    };
    int ladder_ok = 1;
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        render_hover(10, want[i][0], want[i][1], want[i][2], &d);
        if (d.h != want[i][3] || d.y != want[i][4])
            ladder_ok = 0;
    }
    CHECK(ladder_ok,
          "hover: the measured thickness AND place at every installed font size — 1/1/1/1/1/2/3 px, all of them on "
          "the cell's bottom edge, 4 px up from that edge for a font with leading under the baseline, and hanging "
          "from the baseline for a face whose band is thinner than the line");

    /* 3b. The rule is not an opinion. At font_size 14 the line's top edge lands 3 px below the baseline
       (cell_h 16, ascent 12 -> baseline 4; y + h = 1). Liberation Mono's OS/2 table says
       usUnderlinePosition = -435 at upm 2048 = 3 px below the baseline, and a 'p' reaches that far — the
       line goes where the font says, clear of the descender. */
    render_hover(8, 16, 12, 5, &d);
    CHECK(16 - 12 - (d.y + d.h) == 3,
          "hover at font_size 14: the line is 3 px under the baseline — the font's own underline position and the "
          "depth of a 'p', measured rather than guessed");

    /* 4. No cell, no line. A zero or negative cell answers an EMPTY rect: a quad with a negative size is a rectangle
       drawn somewhere else in the window, and the caller's `h <= 0` guard is what skips the pass. */
    render_hover(0, 18, asc, desc, &d);
    CHECK(d.x == 0 && d.y == 0 && d.w == 0 && d.h == 0, "hover: a 0-wide cell draws nothing");
    render_hover(9, 0, asc, desc, &d);
    CHECK(d.w == 0 && d.h == 0, "hover: a 0-tall cell draws nothing");
    render_hover(-5, -40, -1, -1, &d);
    CHECK(d.w == 0 && d.h == 0, "hover: a negative cell draws nothing — never a quad with a negative size");

    /* 5. The thickness does NOT come from `cursor_width`: that key is the cursor's, and a link hint that changed when
       someone restyled their cursor would be a coincidence. render_hover() takes no width argument, and the
       pass that calls it passes the cell box, the font's metrics and nothing else — pinned by the scan below. */
    render_hover(cw, ch, asc, desc, &d);
    CHECK(d.h == 1 && d.y == 0,
          "hover: the rect is a function of the cell and its font alone — nothing else in the renderer can move it");

    /* 6. The two underlines agree. The cursor's underline shape takes its thickness from `cursor_width`, so ask it
       for exactly the hover thickness and the two rects must come out the same rect: one rule, two callers. */
    CursorDraw cd;
    render_cursor(CUR_UNDERLINE, 1, 0, cw, ch, d.h, asc, desc, &cd);
    CHECK(cd.quad && cd.x == d.x && cd.y == d.y && cd.w == d.w && cd.h == d.h,
          "the DECSCUSR underline cursor and the hover underline are the same rect at the same thickness");
}

/* The wiring a headless test cannot execute is pinned by the source scans in the next group. */
static void group_source_wiring(void)
{
    /* The renderer must not draw the cursor from the macro any more; the macro is legitimate only as the DEFAULT
       (kai-term.h, src/config.c). */
    int in_render = src_word_count("src/render.c", "CURSOR_COL");
    CHECK(in_render == 0, "src/render.c never draws the cursor from the CURSOR_COL macro");
    /* Same for the highlight, in the renderer AND in the file manager's painter: both must use the value they were
       handed. */
    CHECK(src_word_count("src/render.c", "SEL_BG") == 0,
          "src/render.c never paints the highlight from the SEL_BG macro");
    CHECK(src_word_count("src/fm.c", "SEL_BG") == 0,
          "src/fm.c never paints the band's selected row from the SEL_BG macro");
    /* Not vacuous: the same scan finds a macro the renderer DOES still use. */
    CHECK(src_word_count("src/render.c", "BAR_LABEL_MAX") > 0,
          "the source scan really finds a macro in src/render.c (BAR_LABEL_MAX) — the zeros above mean something");
    /* The question row's two colours: derived in render_chrome() and READ by the draw loop. Counts, not
       existence — a field filled in but never consumed is the dead-config shape, and the draw loop is in
       no test binary. On the tree that draws the row: notice_bg three times, notice_fg twice. */
    CHECK(src_word_count("src/render.c", "notice_bg") == 3
          && src_word_count("src/render.c", "notice_fg") == 2,
          "src/render.c both derives AND consumes the question row's two colours (re-measure, do not copy)");
    /* And that BOTH passes (the field and its text) are gated on the layout's own flag, plus the staging budget that
       sizes the buffer they write into: three reads of notice_on. A token scan cannot see a disabled branch,
       so without this a draw pass could be switched off (`if (0)`) and every count above would still hold. */
    CHECK(src_word_count("src/render.c", "notice_on") == 3,
          "src/render.c gates the field pass, the text pass and the staging budget on Layout.notice_on");

    /* ---- confirm-quit: the main.c wiring ----
       The DECISIONS are checked in src/pty.c, src/mux.c and src/layout.c; only the WIRING is checked
       here, with counts — a gate defined but never called, or called from one close path only, is what a
       skipped step leaves behind. What no count sees: the SIGCHLD path that closes a tab whose process
       already exited must stay ungated. */
    CHECK(src_word_count("src/main.c", "confirm_gate_window") == 2,
          "src/main.c defines the window gate AND calls it (toplevel_close)");
    CHECK(src_word_count("src/main.c", "confirm_gate_tab") == 3,
          "src/main.c gates BOTH user-initiated tab closes (the key binding and the bar's middle click)");
    CHECK(src_word_count("src/main.c", "confirm_answer") == 4,
          "src/main.c defines the answer and reaches it from BOTH the modal key handler and the box's "
          "buttons (re-measured s123: the box's click is the fourth site)");
    CHECK(src_word_count("src/main.c", "confirm_clear") == 4,
          "the question is taken down on both answers AND when the tab it asked about closes on its own");
    /* The layout input and the visibility rule, as SUBSTRINGS: a word count would also count the comments naming
       these fields, which is how "the row is fed to the layout" could stay green after the assignment went. */
    CHECK(src_sub_count("src/main.c", "rq.notice_on") == 1
          && src_sub_count("src/main.c", "rq.notice_text") == 1,
          "src/main.c feeds the question row to the layout — without it the row is never drawn");
    CHECK(src_sub_count("src/main.c", "g_layout.notice_on") == 1,
          "src/main.c checks the row really is on screen before it lets the question block a close");

    /* ---- confirm-quit: the WINDOW box's main.c wiring ----
       The decisions (sentence, name, geometry, hit test) are checked in src/mux.c, src/pty.c and
       src/layout.c; these scans pin that main.c asks for the box, checks it got drawn, and hands it the
       pointer. */
    CHECK(src_sub_count("src/main.c", "rq.dialog_on") == 1
          && src_sub_count("src/main.c", "rq.dialog_text") == 1,
          "src/main.c feeds the box to the layout — without it the box is never drawn");
    CHECK(src_sub_count("src/main.c", "g_layout.dialog.on") == 3,
          "src/main.c reads the box back three times: the visibility rule that stops a close, the pointer gate, "
          "and the hover rule (a box owns the pointer, so nothing behind it is underlined)");
    CHECK(src_word_count("src/main.c", "layout_hit_dialog") == 1,
          "src/main.c hit-tests the buttons — a box nobody can click is only half a box");
    CHECK(src_word_count("src/main.c", "pty_fg_name") == 1,
          "src/main.c reads the process name — the box says \"nano\", not \"a command\"");
    CHECK(src_word_count("src/main.c", "confirm_warning") == 1
          && src_word_count("src/main.c", "confirm_message") == 1,
          "src/main.c builds the window sentence in mux.c and the tab sentence in mux.c — neither is "
          "invented in a file no test links");
    /* The row and the box are chosen by the KIND of question, never both for one: a window close that also drew the
       row would cost a grid row it was supposed to avoid. */
    CHECK(src_sub_count("src/main.c", "confirm_kind == CONFIRM_CLOSE_TAB") == 2
          && src_sub_count("src/main.c", "confirm_kind == CONFIRM_QUIT") == 1,
          "src/main.c gives the row to a tab close and the box to a window close (re-measure, do not copy)");

    /* The box's six colours: derived in render_chrome() AND read by the draw pass. A field that is filled in but
       never consumed is the dead-config shape, and the draw pass is in no test binary. */
    CHECK(src_word_count("src/render.c", "dialog_bg") == 3
          && src_word_count("src/render.c", "dialog_border") == 2
          && src_word_count("src/render.c", "dialog_fg") == 2
          && src_word_count("src/render.c", "dialog_yes") == 2
          && src_word_count("src/render.c", "dialog_no") == 2
          && src_word_count("src/render.c", "dialog_btn_bg") == 3,
          "src/render.c both derives AND consumes all six of the box's colours (re-measure, do not copy)");
    /* The box is drawn from the palette's red, not from a literal — the same rule the cursor colour and the
       highlight colour are under, and the reason a themed or OSC-repainted red repaints the No button too. */
    CHECK(src_word_count("src/render.c", "term_palette") > 0,
          "src/render.c takes the box's red from the process-wide palette, not from a literal");
    /* Both the staging budget and the draw pass are gated on the layout's own flag: two reads of
       notice_on — a token scan cannot see a branch switched off with `if (0)`. */
    CHECK(src_sub_count("src/render.c", "dialog.on") == 2,
          "src/render.c gates BOTH the staging budget and the draw pass on Layout.dialog.on");

    /* ---- middle-click paste: main.c asks ONE decision, and gates every branch on it ----
       The precedence (box > bar > band > mouse-owning program > pane) is ptr_press_action()'s, checked
       as a table in test_vt.c; these scans pin that main.c consults it and gates its branches on the
       answer. */
    CHECK(src_word_count("src/main.c", "ptr_press_action") == 2,
          "src/main.c asks the decision twice: once for the whole window, once after a click that switched "
          "session (the session it lands on may be the one that asked for the mouse)");
    CHECK(src_sub_count("src/main.c", "act == PTR_") == 6,
          "every branch of ptr_button()'s chain is gated on the action — box, bar, band, report, paste, select");
    CHECK(src_word_count("src/main.c", "PTR_PASTE") == 1 && src_sub_count("src/main.c", "act == PTR_PASTE") == 1,
          "src/main.c has exactly one paste branch, and it is the action that opens it");
    CHECK(src_word_count("src/main.c", "middle_click_paste") == 1,
          "src/main.c feeds conf.middle_click_paste to the decision — a key nothing reads is the dead "
          "cursor_color shape again");
    CHECK(src_word_count("src/main.c", "clipboard_paste") == 4,
          "the paste entry point is shared: declared, defined, and reached from BOTH Ctrl+Shift+V and the "
          "middle click — one paste path, not two that can drift");
    CHECK(src_word_count("src/main.c", "mux_running") == 2 && src_word_count("src/main.c", "pty_fg_busy") == 2,
          "src/main.c asks the measured predicate rather than inventing its own busy test");

    /* ---- the visual bell: the draw loop asks two decisions and makes none of its own ----
       The flash is one quad, so what can be wrong is its timing and its colour, both functions a test
       calls. These scans pin that render_draw() consults them instead of doing its own arithmetic, and
       that the staging budget pays for the pass. */
    CHECK(src_word_count("src/render.c", "bell_strength") == 1,
          "src/render.c asks bell_strength() once — how long a flash lasts is vt.c's decision, not a number "
          "in the draw loop");
    CHECK(src_word_count("src/render.c", "render_bell") == 2,
          "src/render.c defines one bell colour and the draw loop calls it — no literal colour in the pass");
    CHECK(src_word_count("src/render.c", "BELL_FLASH_MS") == 1,
          "the duration comes from the header constant exactly once — a second copy can disagree with the "
          "checks that pin it");
    CHECK(src_sub_count("src/render.c", "+ lay->npane") == 1,
          "the staging budget pays for one quad per drawn pane, so the bell pass cannot overrun the buffer");
    /* ... and that main.c actually feeds it. Without these the renderer would ask bell_strength() with a now_ms of
       0 and a bell_ms of 0 forever: every check above stays green while no bell is ever drawn. */
    CHECK(src_call_passes_arg("src/main.c", "render_init(&", "conf.bell") == 1,
          "src/main.c passes conf.bell to render_init() — `bell = off` has to reach the drawing decision");
    CHECK(src_sub_count("src/render.c", "r->bell_on") == 2,
          "src/render.c stores bell_on once and consults it once, in the pass itself");
    CHECK(src_sub_count("src/main.c", "rend.now_ms") == 1 && src_sub_count("src/main.c", "rend.bell_ms[") == 1,
          "redraw() fills this frame's clock and every session's ring time — the renderer reads no clock itself");
    CHECK(src_sub_count("src/main.c", "term.bell_rang") == 2,
          "the pty-read path reads the BEL event AND clears it — a flag left set would re-arm the flash on "
          "every later read and the pane would never stop washing");
    CHECK(src_word_count("src/main.c", "bell_strength") == 0 && src_word_count("src/main.c", "mux_bell_live") == 1,
          "main.c asks the SAME live decision the renderer reasons about, once, and never writes the fade "
          "arithmetic itself — a second copy could disagree about when a flash is over (the rule moved to "
          "src/mux.c in s127 because a mutant that ignored the DRAWN part survived every check here)");
    CHECK(src_word_count("src/mux.c", "mux_bell_live") == 1 && src_word_count("src/mux.c", "bell_strength") == 1,
          "src/mux.c defines the live decision once and asks bell_strength() once — no second fade formula "
          "anywhere in the tree");
    CHECK(src_sub_count("src/main.c", "layout_pane_of_tab(&g_layout, ti) >= 0") == 2,
          "the BEL hand-off marks the window dirty only for a session the layout DRAWS (the second use is the "
          "picture path). Found by `main_dirty_ignores_drawn` surviving: an unseen session burning a frame every "
          "8 ms is a battery cost with no visible symptom");
    CHECK(src_word_count("src/main.c", "bell_tick") == 2,
          "the bell tick is defined AND called from the frame timer — a fade that nothing drives freezes at "
          "its first frame");
    CHECK(src_sub_count("src/main.c", "live || W.bell_live") == 1,
          "the tick asks for one more frame when the LAST flash ends — without that transition the pane keeps "
          "the last half-faded wash forever. A text pin, honestly labelled: nothing headless can see the frame");
    CHECK(src_word_count("src/mux.c", "bell_ms") == 2,
          "src/mux.c names the ring time twice and only twice: mux_open() clears a reused slot (a new tab cannot "
          "inherit a dead session's bell) and mux_bell_live() reads it");
    /* ---- the bell a click caused: main.c asks one decision, and stamps one moment -------
       A click in `less` becomes Up presses, `less` cannot scroll before the start of its file, and its
       BEL arrives 0-1 ms later. These scans pin that main.c asks bell_is_ours(), stamps BOTH sides of
       the pair, and leaves the WHEEL path unstamped. */
    CHECK(src_word_count("src/main.c", "bell_is_ours") == 1,
          "src/main.c asks the mute decision exactly once, where a BEL becomes a flash — one owner for the rule "
          "that Kai-Term does not flash for its own typing");
    CHECK(src_call_passes_arg("src/main.c", "bell_is_ours", "BELL_OURS_MS") == 1,
          "the call passes the header's window, not a literal — the number is measured (0-1 ms reply) and pinned "
          "by a check, so a second copy could silently disagree with it");
    CHECK(src_word_count("src/main.c", "synth_ms") == 2,
          "the stamp is written once (the click that became keystrokes) and read once (the BEL hand-off) — THREE "
          "would mean the wheel path stamps too, which is the deliberate other half of the rule, and ONE would "
          "mean a mute that nothing sets or nothing consults");
    CHECK(src_word_count("src/vt.c", "bell_is_ours") == 1 && src_word_count("src/vt.c", "BELL_OURS_MS") == 0,
          "src/vt.c defines the decision once and never names the window itself: the length of a mute is the "
          "caller's, exactly like mux_bell_live()'s duration — and the parser still records every BEL either way");
    CHECK(src_word_count("src/mux.c", "synth_ms") == 1,
          "src/mux.c clears the stamp for a reused slot, so a new tab cannot inherit a closed session's mute and "
          "lose its first real bell");
    /* Which session gets the stamp matters: the mute is per session, and the session that was clicked is the one
       whose Tab mux_cur() returns — the same one cur_term() handed the keystrokes to. A mutant that stamps
       mux.tab[0] instead breaks the second scan. */
    CHECK(src_sub_count("src/main.c", "Tab *st = mux_cur(&mux);") == 1
          && src_sub_count("src/main.c", "st->synth_ms") == 1,
          "the stamp is written through the CURRENT session's Tab — the one the click sent its presses to, not a "
          "slot index that would mute whichever tab happens to be first");
    /* ---- a plain click opens a URL: main.c computes the flags, url.c owns every rule ----
       The run rule, the gates, the wait and the spawn are checked as functions in tests/test_vt.c; these
       scans pin that main.c CONSULTS them rather than keeping its own copy. */
    CHECK(src_word_count("src/main.c", "url_refusal") == 1 &&
          src_word_count("src/main.c", "url_refusal_name") == 1 &&
          src_word_count("src/main.c", "url_click_opens") == 0,
          "src/main.c asks the gate exactly once and asks it for a REASON — the order of the flags is "
          "src/url.c's decision, so re-deriving it in the event handler could drift from the table the "
          "checks flip (and main.c must not keep a second predicate that could disagree with the log line)");
    CHECK(src_sub_count("src/main.c", "url_refusal_name(why)") == 1,
          "and the reason is LOGGED — s131 shipped a refusal that logged nothing and the user's \"it's not "
          "clickable\" was undecidable; a decision with no trace is a decision nobody can debug");
    CHECK(src_sub_count("src/main.c", "if (uc.enabled)") == 1,
          "the refusal line is gated on the switch: with click_opens_url = 0 the user asked for no opens, so "
          "there is nothing to explain on every click");
    CHECK(src_sub_count("src/url.c", "url_refusal(c) == URL_REFUSE_NONE") == 1,
          "url_click_opens is defined as `refusal == NONE` in one place — two predicates could disagree with "
          "the reason the log prints");
    CHECK(src_word_count("src/main.c", "click_opens_url") == 3,
          "src/main.c feeds conf.click_opens_url to the click AND to the hover (twice there: the flag, and the "
          "scan guard that keeps the work off every frame when the switch is off) — a key nothing reads is the "
          "dead cursor_color shape again");
    CHECK(src_word_count("src/main.c", "term_url_span") == 3 &&
          src_word_count("src/main.c", "term_url_text") == 3,
          "the scan is asked THREE times: at the click, at the deadline when the session may have closed, scrolled "
          "or repainted the line, and every frame for the hover underline — which is what makes a stale underline "
          "impossible");
    CHECK(src_sub_count("src/main.c", "strcmp(holds, clicked)") == 1,
          "and the deadline compares the text it re-read with the text the click saw — an address the user "
          "never clicked is never opened");
    CHECK(src_word_count("src/main.c", "url_tick") == 2 && src_sub_count("src/main.c", "url_tick();") == 1,
          "the tick is defined AND called from the frame timer — a wait nothing drives never expires, which "
          "is a click that opens nothing at all");
    CHECK(src_word_count("src/main.c", "url_open_due") == 1 &&
          src_word_count("src/main.c", "URL_OPEN_WAIT_MS") == 1,
          "the wait asks the shared decision with the header's window, never a literal — the number has to "
          "stay the one term_click_count() uses");
    CHECK(src_sub_count("src/main.c", "W.url_press_ms = 0") == 2,
          "the pending open is cleared in BOTH places that end a wait: a new press, and the deadline itself "
          "— a stamp left set would open the same address on every frame");
    CHECK(src_word_count("src/main.c", "url_spawn") == 1 &&
          src_sub_count("src/main.c", "url_spawn(URL_OPENER_PROGRAM, clicked)") == 1,
          "exactly ONE spawn site, and it takes the header's opener and the RE-VALIDATED text");
    CHECK(src_word_count("src/main.c", "URL_OPENER_PROGRAM") == 3 &&
          src_word_count("src/url.c", "URL_OPENER_PROGRAM") == 0,
          "the opener's name lives in main.c only (the spawn and its two log lines) — src/url.c starts "
          "whatever it is handed, so there is one place to look for what Kai-Term executes");
    CHECK(src_word_count("src/main.c", "url_is_openable") == 0 &&
          src_word_count("src/url.c", "url_is_openable") == 2,
          "the argument check is not re-implemented at the click: it is defined once in src/url.c and "
          "called by term_url_text(), so no caller can obtain text that failed it");
    CHECK(src_word_count("src/url.c", "term_line_span") == 1,
          "src/url.c walks the logical line with the SAME function a triple click uses — a second walk of "
          "the wrap marks is a second answer about which rows are one line");
    CHECK(src_word_count("src/url.c", "closefrom") == 1 && src_word_count("src/url.c", "setsid") == 1 &&
          src_word_count("src/url.c", "sigprocmask") == 1,
          "the three things the spawn promises are all in it: Kai-Term's descriptors closed, a session of "
          "its own, and the blocked signal mask cleared before the exec");
    CHECK(src_word_count("src/url.c", "system") == 0 && src_word_count("src/url.c", "popen") == 0 &&
          src_word_count("src/url.c", "execl") == 0 && src_word_count("src/url.c", "execlp") == 0 &&
          src_word_count("src/url.c", "execve") == 0,
          "no shell and no second exec path anywhere in the feature — the argument is data, always");
    CHECK(src_sub_count("src/url.c", "char *argv[3]") == 1,
          "the opener is started with exactly one argument and a NULL terminator");
    /* ---- the HOVER underline's wiring: the frame asks, the renderer draws, url.c decides ---- */
    CHECK(src_word_count("src/main.c", "url_hover_shows") == 1 &&
          src_word_count("src/url.c", "url_hover_shows") == 1 &&
          src_word_count("src/render.c", "url_hover_shows") == 0,
          "the hover is decided exactly once, in src/url.c, and the renderer never re-decides it — an underline "
          "the draw loop judged for itself would drift from the gate the click obeys");
    CHECK(src_word_count("src/main.c", "hover_update") == 3 &&
          src_sub_count("src/main.c", "hover_update();") == 1,
          "the run under the pointer is recomputed in ONE place, called from redraw() — not cached on the motion "
          "event, which is what would leave an underline on a run that scrolled, repainted or resized away");
    CHECK(src_word_count("src/main.c", "hover_motion") == 5 &&
          src_sub_count("src/main.c", "hover_motion();") == 3,
          "and the frame is asked for by all three pointer listeners — enter, motion and LEAVE. Without the leave "
          "the underline stays on screen after the pointer is gone, because ptr_x/ptr_y keep their last value");
    CHECK(src_sub_count("src/main.c", "W.ptr_in = 1") == 1 && src_sub_count("src/main.c", "W.ptr_in = 0") == 1,
          "the pointer's presence on the surface is set in exactly the two listeners that can know it");
    /* …and OBED by the mapping that asks for the cell: pinning the two assignments is not pinning the gate that
   reads them. */
    CHECK(src_sub_count("src/main.c", "if (!W.ptr_in)") == 1,
          "the pointer's presence is the FIRST question the cell mapping asks — wl_pointer.leave does not clear "
          "ptr_x/ptr_y, so without this the last cell the pointer was over keeps its underline after the hand leaves "
          "the window");
    CHECK(src_sub_count("src/main.c", "rend.hover_tab = -1") == 2 &&
          src_sub_count("src/main.c", "rend.hover_span = sp") == 1,
          "every frame starts from \"nothing hovered\" and sets the span in exactly one place — a frame that "
          "forgot to clear would keep the previous frame's run");
    CHECK(src_word_count("src/main.c", "hover_cell") == 4,
          "the pointer-to-cell mapping is one function, asked by the frame and by the motion gate — two mappings "
          "would be two answers about which cell the pointer is on (the sel_cell()/render agreement rule)");
    CHECK(src_word_count("src/render.c", "render_hover") == 2 &&
          src_sub_count("src/render.c", "render_hover((int)cw, (int)ch, (int)ascent, (int)descent, &hd)") == 1,
          "the underline's shape is one function asked once per pane per frame, with the CELL BOX and the font's "
          "metrics and nothing else — no cursor_width, so restyling the cursor cannot restyle the link hint, and no "
          "made-up baseline, so the line is placed by the font that is actually drawn");
    /* The position rule: ONE helper answers "where in the cell does a line under the characters go", and
       BOTH underlines ask it (3 = definition + cursor branch + hover rect). The wrong answer is pinned
       absent by its own text — `cell_h - h` put the line across the letters, and only a scan can see it. */
    CHECK(src_word_count("src/render.c", "underline_y") == 3,
          "the two underlines ask ONE position rule (definition + both call sites) — two copies of the expression "
          "is how they came to be wrong in the same direction");
    CHECK(src_sub_count("src/render.c", "cell_h - h") == 0 &&
          src_sub_count("src/render.c", "cell_h - w") == 0,
          "and neither underline computes its y from the cell's TOP edge any more — the s134 defect, pinned absent");
    CHECK(src_sub_count("src/render.c", "float descent = (float)f->descent * f->scale;") == 1 &&
          src_sub_count("src/render.c", "(int)ascent, (int)descent, &cd") == 3,
          "the font's own descent reaches render_draw() and all THREE cursor passes take it — a metric that stops "
          "in Font is not implemented (the s102 lesson, one level down)");
    CHECK(src_word_count("src/render.c", "url_span_row") == 1 &&
          src_word_count("src/url.c", "url_span_row") == 3 &&
          src_word_count("src/main.c", "url_span_row") == 0,
          "which cells of a run sit on which row has ONE owner in src/url.c (definition + the two uses in "
          "term_url_text), asked once by the renderer and by nobody in main.c — the underline and the opened bytes "
          "cannot disagree about the range");
    CHECK(src_sub_count("src/render.c", "lay->pane[p].tab != r->hover_tab") == 1,
          "the underline is drawn in the ONE pane the pointer is over — in side-by-side view the other pane's "
          "text stays untouched");
    CHECK(src_sub_count("src/render.c", "col2f(ink, fg)") == 1,
          "the line takes the CELL's own ink, not a chrome colour: an address printed in blue underlines in blue, "
          "and no new colour key is needed");
    /* The same claim by its own text, because the check above cannot see WHERE `ink` came from: feeding it a chrome
   colour instead of the cell's survives every other check here. */
    CHECK(src_sub_count("src/render.c", "uint32_t ink = c->fg;") == 1 &&
          src_sub_count("src/render.c", "ink = c->bg;") == 1,
          "and the ink is read from the CELL — its own fg, swapped to its bg exactly where the cell is inverted, the "
          "same swap pass 3 makes so the line matches the ink the eye sees");
    CHECK(src_word_count("src/render.c", "URL_MAX") == 1,
          "the frame's quad budget counts the hover pass (at most URL_MAX cells) — a budget that forgets a pass "
          "is a staging-buffer overrun waiting for a long address");
    /* The draw ORDER, which no headless test can run and the frame depends on: with no GL blending the later quad
   simply wins, so the underline must come after the selection bands and the cursor quad (or a highlight eats
   it) and before the glyphs (or it hides a character). Each needle appears once in render_draw(). */
    CHECK(src_code_before("src/render.c", "if (!term_cell_highlightable", "render_hover((int)cw") == 1,
          "the underline is emitted AFTER the selection bands, so a highlight never paints over it");
    CHECK(src_code_before("src/render.c", "if (!cd.quad)", "render_hover((int)cw") == 1,
          "and after the bar/underline cursor quad, so the two lines in one cell cannot disagree by draw order");
    CHECK(src_code_before("src/render.c", "render_hover((int)cw",
                          "emit_cell_glyph(f, c, px, py, cw, ch, ascent, fg, bg, band0, band1, &vp)") == 1,
          "and BEFORE the glyphs, so a character is never hidden behind its own underline");
    /* The one link a headless test cannot execute: main() handing the configured value to the renderer and to the
   band's painter. The call string includes the '&' so a COMMENT that merely names the function (main.c has
   two) cannot start the scan. */
    CHECK(src_call_passes_arg("src/main.c", "render_init(&", "conf.cursor_color") == 1,
          "src/main.c passes conf.cursor_color to render_init()");
    CHECK(src_call_passes_arg("src/main.c", "render_init(&", "conf.selection_color") == 1,
          "src/main.c passes conf.selection_color to render_init()");
    CHECK(src_call_passes_arg("src/main.c", "fm_paint(&", "conf.selection_color") == 1,
          "src/main.c passes conf.selection_color to fm_paint()");
    /* The cursor's two numbers, same reason: a key that stops in Config is not implemented. */
    CHECK(src_call_passes_arg("src/main.c", "render_init(&", "conf.cursor_width") == 1,
          "src/main.c passes conf.cursor_width to render_init()");
    /* The blink phase reaches the renderer as a COPY of window state, not as a Term field. The scan's second
   argument is any code string, so this pins the assignment itself. */
    CHECK(src_call_passes_arg("src/main.c", "rend.cursor_on = W.cursor_on", "W.cursor_on") == 1,
          "src/main.c copies the blink phase into Render.cursor_on every frame");
    /* The three layers of render_draw() that paint a cursor cell must ask the SAME question, or a bar drawn by one
   and not the next erases itself. The scanner skips comments, so 4 is the definition plus the three call
   sites — re-diff, do not copy. */
    CHECK(src_word_count("src/render.c", "render_cursor") == 4,
          "src/render.c calls render_cursor() from all three passes (definition + 3 call sites)");
    CHECK(src_word_count("src/render.c", "cursor_on") == 3,
          "and consults the blink phase in all three of them — re-diff, do not copy");
    CHECK(src_word_count("src/config.c", "CURSOR_COL") >= 1,
          "CURSOR_COL survives as the built-in DEFAULT in src/config.c");
}

/* The scanner itself, pinned against a file this test writes, NOT against this file: a self-scan counts
   its own CHECK lines as occurrences of the token it looks for. Every mention of a probe token below is
   split by string concatenation, so this file never holds the whole word. */
static void group_scanner_selfcheck(void)
{
    char path[128];
    snprintf(path, sizeof path, "/tmp/kai-term-scan-%ld.txt", (long)getpid());
    FILE *w = fopen(path, "w");
    if (!w) {
        CHECK(0, "scanner self-pin: the probe file could not be written");
        return;
    }
    fputs("int SRC_SCAN_" "PROBE_TOKEN = 1;\n", w);                        /* code: 1 */
    fputs("SRC_SCAN_" "PROBE_TOKEN = SRC_SCAN_" "PROBE_TOKEN + 1;\n", w);  /* code: 2 more */
    fputs("/* SRC_SCAN_" "COMMENT_TOKEN */\n", w);                         /* block comment */
    fputs("/* SRC_SCAN_" "COMMENT_TOKEN spans\n   two lines */\n", w);     /* block comment, 2 lines */
    fputs("// SRC_SCAN_" "COMMENT_TOKEN\n", w);                            /* line comment */
    fclose(w);

    CHECK(src_word_count(path, "SRC_SCAN_" "PROBE_TOKEN") == 3,
          "the source scan counts every code use of a token (3 in the probe file)");
    CHECK(src_word_count(path, "SRC_SCAN_" "COMMENT_TOKEN") == 0,
          "the source scan ignores a token that appears only in comments (block, multi-line and //)");
    CHECK(src_word_count(path, "NO_SUCH_TOKEN_IN_THE_FILE") == 0,
          "the source scan reports 0 for a token that is simply absent");
    CHECK(src_word_count("tests/no_such_file_here.c", "CURSOR_COL") == -1,
          "the source scan fails loudly (-1) when the file cannot be opened, so a moved file cannot pass");
    unlink(path);
}

int main(void)
{
    group_cursor_color();
    group_cursor_shape();
    group_hover_shape();
    group_bell_color();
    group_source_wiring();
    group_scanner_selfcheck();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
