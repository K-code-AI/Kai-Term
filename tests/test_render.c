/* Kai-Term — unit tests for the renderer's pure decisions (src/render.c).
 *
 * No display, no EGL, no GL context, no font: what is under test is the pair of functions in render.c that
 * decide something without touching a context — render_chrome(), which turns the configured palette into
 * the colours a frame's chrome is drawn with, and render_cursor(), which turns a shape request, a blink
 * phase, a selection band and a cell size into what one cursor cell is painted as. Both read plain structs
 * and nothing else, so a test can call them with a struct filled by hand. That seam exists for one bug
 * (ISSUES.md, s102): `cursor_color =` was defaulted, parsed, stored and asserted on by the config tests,
 * and reached no pixel, because the renderer took the colour from the CURSOR_COL macro. Every check that
 * read Config stayed green. So the checks here sit at the CONSUMER — the value that is about to become a
 * vertex colour, and the shape that is about to become a quad.
 *
 * What a headless test cannot reach, stated plainly: the call inside main() that hands Config's value to
 * render_init(), and the draw loop that turns these decisions into vertices. main() and render_draw() are
 * in no linked test (the same limitation tests/cli_test.sh exists for), so that wiring is pinned by the
 * source scans in group_source_wiring() — text checks, weaker than running it, and they fail loudly if the
 * renderer ever reads the macro again or an argument is dropped.
 */
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

/* The same blend src/render.c and src/fm.c each keep a private copy of. Copied here on purpose: a check
   that called the code under test to predict the code under test would pin nothing. */
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

/* A Render the way render_init() fills the colour fields (see src/render.c: r->bg, r->fg,
   r->cursor_color, r->opacity) — everything else in the struct is EGL/GL state this test never touches. */
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

    /* 2. A configured value is what the visible session's cursor is drawn with. This is the bug: on the
       code that had the bug this returns CURSOR_COL no matter what the file said. */
    c.cursor_color = 0xffff0000u;
    render_from_config(&r, &c);
    render_chrome(&r, &ch);
    CHECK(ch.cursor == 0xffff0000u, "cursor_color = ff0000: the ACTIVE pane's cursor is drawn with it");

    /* 3. The dimmed cursor of a session that is drawn but not the visible one is derived from the
       configured value, not from the macro. */
    CHECK(ch.cursor_off == mix_ref(0xffff0000u, c.bg, 55),
          "cursor_color = ff0000: the INACTIVE session's cursor is that colour mixed with the background");

    /* 4. Two different configured colours give two different dimmed colours. If the base were the macro,
       both would be identical — the shape the bug had. */
    c.cursor_color = 0xff00ff00u;
    render_from_config(&r, &c);
    Chrome g2;
    render_chrome(&r, &g2);
    CHECK(g2.cursor != ch.cursor && g2.cursor_off != ch.cursor_off,
          "two different cursor_color values change BOTH the active and the inactive cursor");

    /* 5. The dimming follows the configured background too (the `background =` line, s101). Compared with
       g2 — same cursor colour, only bg differs — so the difference can only come from the background. */
    Config d = c;
    d.bg = 0xff4a0710u;
    render_from_config(&r, &d);
    Chrome g3;
    render_chrome(&r, &g3);
    CHECK(g3.cursor_off != g2.cursor_off, "a different background changes the dimmed cursor");
    CHECK(g3.cursor == g2.cursor, "a different background does NOT change the active cursor colour");

    /* 6. Chrome colours stay OPAQUE in Chrome: the window alpha is applied per quad in render_draw(), so a
       configured colour can never be silently halved on the way here. Compared with g2 — same colours,
       only opacity differs. */
    Config e = c;
    e.opacity = 0.3f;
    render_from_config(&r, &e);
    Chrome g4;
    render_chrome(&r, &g4);
    CHECK(((g4.cursor >> 24) & 0xff) == 0xff, "the chrome cursor colour keeps alpha 0xff");
    CHECK(g4.cursor == g2.cursor && g4.cursor_off == g2.cursor_off,
          "window opacity does not change the derived chrome colours");

    /* 7. The highlight colour is the configured one, in all three places that mean "this is selected" —
       the selection band, the visible session's bar item and the focus border. The built-in default is
       still SEL_BG, so a user who configured nothing sees what they always saw. */
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

    /* 8. The confirm-quit row: the highlight for its field, the default ink for its text. It is chrome, so
       the SAME configured colour owns every "this is the thing in this window" signal — the band, the active
       bar item, the focus border and the question — and it is checked HERE, at the consumer, which is the
       place the dead `cursor_color =` bug hid (parsed, stored, tested at the parser, read by nothing). */
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

    /* 9. The confirm-quit DIALOG — a window close's question, drawn OVER the terminal. Six colours, all
       derived here and read by the draw pass (the source scans below pin the reading). The field is a LIFTED
       paper: with no GL blending, "in front of" only exists if the colour differs from what is behind it, so
       a field the exact colour of the cells it covers is a box nobody can see. The border and the Yes label
       take the one configured colour that already owns every "this is the thing in this window" signal; the
       No label takes the palette's RED, so a theme that restyles red restyles the button. Values copied from
       .scratch/s123/colprobe2. */
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
    /* The red follows the palette, which is the whole reason it comes from term_palette() and not from a
       constant: an application that repaints the palette with OSC 4 repaints the button, exactly as it
       repaints a cell. Restored afterwards — the palette is process-wide state, so leaving it changed would
       make a later check read a colour this one invented. */
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

/* ---------- the two source scans that cover what no headless test can run ---------- */

/* Count code tokens equal to `word` in a source file; -1 when the file cannot be opened, so a moved
   file fails loudly instead of quietly checking nothing. Two rules make the count mean what the check
   claims:
   - whole tokens only: a plain strstr for "ch" would find "chrome", and a substring match would find
     CURSOR_COL inside a longer name;
   - comments are skipped (block and line), because a comment may legitimately NAME a macro while the
     code never reads it — which is exactly what these checks are about. */
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

/* Count a LITERAL substring in the code (comments skipped the same way src_word_count skips them).
   Where this is used instead of src_word_count: a word count also counts a token that merely appears in a
   comment, so a check like "main.c feeds the row to the layout" passes when the assignment is deleted and the
   comment naming it is left behind. Naming the assignment (`rq.notice_on`) instead of the field makes the
   check about the code. */
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

/* ---------- the cursor's shape: one pure decision, the whole truth table ----------
   The other half of the s102 lesson. The SHAPE rule used to be implicit in the draw loop (the cursor was
   always the cell's own background), and the draw loop is in no test binary — exactly where a colour rule
   was uncheckable. render_cursor() is the same seam: no GL, no font, no Term, no allocation, so every
   combination of (what the application asked for, is it visible, is a selection band on this cell, how big
   is the cell, how wide did the user ask for) can be pinned here. What it decides matters because THREE
   passes of a frame ask it the same question about the same cell and must agree — the comment inside
   render_draw() ("must match pass 2 exactly") exists because a cursor that disagrees with the band erases
   itself. */
static void group_cursor_shape(void)
{
    CursorDraw d;
    const int cw = 9, ch = 18;          /* a plausible cell at font_size 14 */

    /* 1. The default is the bar the user asked for: a separate quad, not the cell's background. */
    render_cursor(CUR_UNSET, 1, 0, cw, ch, 2, &d);
    CHECK(d.quad == 1 && d.block == 0 && d.invert == 0 && d.tint == 0,
          "no DECSCUSR seen: a separate quad, and NOT the cell's background");
    CHECK(d.x == 0 && d.y == 0 && d.w == 2 && d.h == ch,
          "a 2 px bar at the LEFT edge of the cell, the full height of it");
    render_cursor(CUR_BAR, 1, 0, cw, ch, 2, &d);
    CHECK(d.quad == 1 && d.x == 0 && d.y == 0 && d.w == 2 && d.h == ch,
          "an application asking for a bar explicitly gets exactly the default");

    /* 2. Not visible (DEC 25l, scrolled back, or the blink phase off) draws NOTHING, in every shape. */
    const int styles[] = { CUR_UNSET, CUR_BLOCK, CUR_BAR, CUR_UNDERLINE };
    for (unsigned i = 0; i < sizeof styles / sizeof styles[0]; i++) {
        render_cursor(styles[i], 0, 0, cw, ch, 2, &d);
        CHECK(d.block == 0 && d.quad == 0 && d.invert == 0 && d.tint == 0 &&
              d.x == 0 && d.y == 0 && d.w == 0 && d.h == 0,
              "an invisible cursor emits no block, no quad and no ink change, whatever the shape");
    }

    /* 3. The width clamp. Wider than a third of a cell is a block wearing a hat, and 0 px would be no
       cursor at all — so the clamp has two ends and the lower one matters at a small font size, where a
       cell can be narrower than 3 px and cell/3 would be 0. */
    render_cursor(CUR_BAR, 1, 0, cw, ch, 40, &d);
    CHECK(d.w == cw / 3 && d.h == ch, "a 40 px bar in a 9 px cell is cut to a third of the cell (3 px)");
    render_cursor(CUR_BAR, 1, 0, cw, ch, 1, &d);
    CHECK(d.w == 1, "a 1 px bar stays 1 px");
    render_cursor(CUR_BAR, 1, 0, 2, ch, 2, &d);
    CHECK(d.w == 1, "in a 2 px-wide cell the bar is 1 px — never 0, which would draw nothing");
    render_cursor(CUR_BAR, 1, 0, 1, ch, 2, &d);
    CHECK(d.w == 1, "and in a 1 px cell it is still 1 px, so the cursor cannot vanish");

    /* 4. The underline DECSCUSR asks for: full cell width, at the bottom, as thick as the bar. */
    render_cursor(CUR_UNDERLINE, 1, 0, cw, ch, 2, &d);
    CHECK(d.quad == 1 && d.x == 0 && d.w == cw && d.h == 2 && d.y == ch - 2,
          "an underline is the full width of the cell, 2 px tall, on its bottom edge");
    render_cursor(CUR_UNDERLINE, 1, 0, cw, ch, 40, &d);
    CHECK(d.h == ch / 3 && d.y == ch - ch / 3,
          "a 40 px underline is cut to a third of the cell's HEIGHT (6 px), still on the bottom edge");
    render_cursor(CUR_UNDERLINE, 1, 0, cw, 2, 2, &d);
    CHECK(d.h == 1 && d.y == 1, "in a 2 px-tall cell the underline is 1 px — never 0");

    /* 5. A block is today's behaviour, unchanged: it IS the cell's background (pass 1), so it steps aside
       where a selection band is painted and the ink is tinted with the cursor colour instead. */
    render_cursor(CUR_BLOCK, 1, 0, cw, ch, 2, &d);
    CHECK(d.block == 1 && d.invert == 1 && d.tint == 0 && d.quad == 0 && d.w == 0,
          "a block over no band: pass 1 paints the cell background and pass 3 inverts the ink");
    render_cursor(CUR_BLOCK, 1, 1, cw, ch, 2, &d);
    CHECK(d.block == 0 && d.invert == 0 && d.tint == 1 && d.quad == 0,
          "a block under a band steps aside for it and tints the ink — the rule that must not change");

    /* 6. A bar or an underline does NOT step aside: its quad is emitted in batch B AFTER the bands, so it
       survives where a selection ends on the cursor cell. That is the whole reason it is a separate quad. */
    render_cursor(CUR_BAR, 1, 1, cw, ch, 2, &d);
    CHECK(d.quad == 1 && d.block == 0 && d.tint == 0 && d.invert == 0,
          "a bar under a band still emits its quad and leaves the ink alone");
    render_cursor(CUR_UNDERLINE, 1, 1, cw, ch, 2, &d);
    CHECK(d.quad == 1 && d.block == 0 && d.tint == 0 && d.invert == 0,
          "and so does an underline");

    /* 7. The invariants, over every combination — the ones a frame depends on and that a hand-written
       table of examples would miss.
       (a) a cell is never painted as BOTH a block and a quad (the cursor would be drawn twice),
       (b) invert only ever comes with block (inverting ink with nothing behind it is a blank cell),
       (c) a VISIBLE cursor always does something: a block, a quad, or a tint of the ink — so the three
           passes cannot conspire to erase it,
       (d) the quad's rect is always inside the cell, at every cell size. */
    const int cells[][2] = { { 1, 1 }, { 2, 2 }, { 3, 3 }, { 1, 18 }, { 20, 4 }, { 9, 18 }, { 40, 30 } };
    for (unsigned s = 0; s < sizeof styles / sizeof styles[0]; s++)
        for (int vis = 0; vis <= 1; vis++)
            for (int band = 0; band <= 1; band++)
                for (unsigned c2 = 0; c2 < sizeof cells / sizeof cells[0]; c2++) {
                    int w_px = 2 + (int)c2;      /* every configured width from 2 to 8 */
                    render_cursor(styles[s], vis, band, cells[c2][0], cells[c2][1], w_px, &d);
                    CHECK(!(d.block && d.quad),
                          "invariant: a cell is never both a block and a separate quad");
                    CHECK(!d.invert || d.block,
                          "invariant: ink is only inverted where a block was actually painted");
                    CHECK(!vis || d.block || d.quad || d.tint,
                          "invariant: a visible cursor always paints something in one of the three passes");
                    CHECK(d.x >= 0 && d.y >= 0 && d.x + d.w <= cells[c2][0] &&
                          d.y + d.h <= cells[c2][1],
                          "invariant: the cursor's rect never leaves its own cell");
                }
}

/* The wiring a headless test cannot execute is pinned by the source scans in the next group. */
static void group_source_wiring(void)
{
    /* The renderer must not draw the cursor from the macro any more. On the buggy src/render.c this count
       is 3 and the check fails; the macro is legitimate only as the DEFAULT (kai-term.h, src/config.c). */
    int in_render = src_word_count("src/render.c", "CURSOR_COL");
    CHECK(in_render == 0, "src/render.c never draws the cursor from the CURSOR_COL macro");
    /* Same for the highlight, in the renderer AND in the file manager's painter: both must use the value
       they were handed. On the pre-change tree both counts are non-zero. */
    CHECK(src_word_count("src/render.c", "SEL_BG") == 0,
          "src/render.c never paints the highlight from the SEL_BG macro");
    CHECK(src_word_count("src/fm.c", "SEL_BG") == 0,
          "src/fm.c never paints the band's selected row from the SEL_BG macro");
    /* Not vacuous: the same scan finds a macro the renderer DOES still use. */
    CHECK(src_word_count("src/render.c", "BAR_LABEL_MAX") > 0,
          "the source scan really finds a macro in src/render.c (BAR_LABEL_MAX) — the zeros above mean something");
    /* The question row's two colours: derived in render_chrome() and READ by the draw loop. Counts, not
       existence — a field that is filled in but never consumed is exactly the dead `cursor_color =` shape,
       and the draw loop is in no test binary. Measured on the tree that draws the row: notice_bg three
       times (the derivation, the field, the text's background), notice_fg twice. */
    CHECK(src_word_count("src/render.c", "notice_bg") == 3
          && src_word_count("src/render.c", "notice_fg") == 2,
          "src/render.c both derives AND consumes the question row's two colours (re-measure, do not copy)");
    /* And that BOTH passes (the field and its text) are gated on the layout's own flag, plus the staging
       budget that sizes the buffer they write into: three reads of notice_on. A token scan cannot see a
       disabled branch, so without this a draw pass could be switched off (`if (0)`) and every count above
       would still hold — measured, the mutant c_no_text_pass fails here and nowhere else. */
    CHECK(src_word_count("src/render.c", "notice_on") == 3,
          "src/render.c gates the field pass, the text pass and the staging budget on Layout.notice_on");

    /* ---- confirm-quit: the main.c wiring (step 4, stage D) ----
       The gate, the modal keys and the layout input live in main(), which is in no test binary, so the
       DECISIONS are checked in src/pty.c, src/mux.c and src/layout.c and only the WIRING can be checked here.
       Counts, not existence: a gate that is defined but never called is exactly the shape this feature would
       have if stage D had been skipped, and a gate called from one close path but not the other is the shape
       that loses work in the path nobody thought about. Counts measured on the tree that has the wiring —
       re-measure, do not copy. What no count can say: the SIGCHLD path that closes a tab whose process has
       ALREADY exited must stay ungated (there is nothing to protect), which is a reading of main.c, and the
       row's look, which is the user's to confirm. */
    CHECK(src_word_count("src/main.c", "confirm_gate_window") == 2,
          "src/main.c defines the window gate AND calls it (toplevel_close)");
    CHECK(src_word_count("src/main.c", "confirm_gate_tab") == 3,
          "src/main.c gates BOTH user-initiated tab closes (the key binding and the bar's middle click)");
    CHECK(src_word_count("src/main.c", "confirm_answer") == 4,
          "src/main.c defines the answer and reaches it from BOTH the modal key handler and the box's "
          "buttons (re-measured s123: the box's click is the fourth site)");
    CHECK(src_word_count("src/main.c", "confirm_clear") == 4,
          "the question is taken down on both answers AND when the tab it asked about closes on its own");
    /* The layout input and the visibility rule, as SUBSTRINGS: a word count would also count the comments
       that name these fields, which is how "the row is fed to the layout" could stay green after the
       assignment was deleted. */
    CHECK(src_sub_count("src/main.c", "rq.notice_on") == 1
          && src_sub_count("src/main.c", "rq.notice_text") == 1,
          "src/main.c feeds the question row to the layout — without it the row is never drawn");
    CHECK(src_sub_count("src/main.c", "g_layout.notice_on") == 1,
          "src/main.c checks the row really is on screen before it lets the question block a close");

    /* ---- confirm-quit: the WINDOW box's main.c wiring (s123) ----
       A window close asks with a centred box and two clickable buttons; a tab close keeps the row. The
       DECISIONS (the sentence, the name, the geometry, the hit test) are all checked in src/mux.c,
       src/pty.c and src/layout.c — what these scans pin is that main.c actually asks for the box, reads
       back whether it really got drawn, and gives the pointer to it. Counts measured on the tree that has
       the wiring; re-measure, do not copy. */
    CHECK(src_sub_count("src/main.c", "rq.dialog_on") == 1
          && src_sub_count("src/main.c", "rq.dialog_text") == 1,
          "src/main.c feeds the box to the layout — without it the box is never drawn");
    CHECK(src_sub_count("src/main.c", "g_layout.dialog.on") == 2,
          "src/main.c reads the box back twice: the visibility rule that stops a close, and the pointer gate");
    CHECK(src_word_count("src/main.c", "layout_hit_dialog") == 1,
          "src/main.c hit-tests the buttons — a box nobody can click is only half a box");
    CHECK(src_word_count("src/main.c", "pty_fg_name") == 1,
          "src/main.c reads the process name — the box says \"nano\", not \"a command\"");
    CHECK(src_word_count("src/main.c", "confirm_warning") == 1
          && src_word_count("src/main.c", "confirm_message") == 1,
          "src/main.c builds the window sentence in mux.c and the tab sentence in mux.c — neither is "
          "invented in a file no test links");
    /* The row and the box are chosen by the KIND of question, never both for one: a window close that also
       drew the row would cost a grid row it was supposed to avoid. */
    CHECK(src_sub_count("src/main.c", "confirm_kind == CONFIRM_CLOSE_TAB") == 2
          && src_sub_count("src/main.c", "confirm_kind == CONFIRM_QUIT") == 1,
          "src/main.c gives the row to a tab close and the box to a window close (re-measure, do not copy)");

    /* The box's six colours: derived in render_chrome() AND read by the draw pass. A field that is filled in
       but never consumed is exactly the dead `cursor_color =` shape, and the draw pass is in no test binary.
       Counts measured on the tree that draws the box. */
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
    /* Both the staging budget and the draw pass are gated on the layout's own flag: a token scan cannot see a
       disabled branch, so without this a draw pass could be switched off (`if (0)`) and every count above
       would still hold. Measured: the budget and the pass, two reads. */
    CHECK(src_sub_count("src/render.c", "dialog.on") == 2,
          "src/render.c gates BOTH the staging budget and the draw pass on Layout.dialog.on");

    /* ---- middle-click paste: main.c asks ONE decision, and gates every branch on it (s125) ----
       The precedence (box > bar > band > application-that-asked > pane) is ptr_press_action()'s and is checked
       as a table in tests/test_vt.c. What these scans pin is that main.c actually consults it, and that each
       branch of its chain is gated on the answer rather than on a re-derived condition — otherwise the table
      and the event handler can drift apart and every headless check stays green while the click does something
       else. Counts measured on the wired tree; re-measure, do not copy. */
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
    /* The one link a headless test cannot execute: main() handing the configured value to the renderer and
       to the band's painter. The call string includes the '&' so a COMMENT that merely names the function
       (main.c has two) cannot start the scan. */
    CHECK(src_call_passes_arg("src/main.c", "render_init(&", "conf.cursor_color") == 1,
          "src/main.c passes conf.cursor_color to render_init()");
    CHECK(src_call_passes_arg("src/main.c", "render_init(&", "conf.selection_color") == 1,
          "src/main.c passes conf.selection_color to render_init()");
    CHECK(src_call_passes_arg("src/main.c", "fm_paint(&", "conf.selection_color") == 1,
          "src/main.c passes conf.selection_color to fm_paint()");
    /* The cursor's two numbers, same reason: a key that stops in Config is not implemented (s102). */
    CHECK(src_call_passes_arg("src/main.c", "render_init(&", "conf.cursor_width") == 1,
          "src/main.c passes conf.cursor_width to render_init()");
    /* The blink phase reaches the renderer as a COPY of window state, not as a Term field: on the tree
       before the blink existed this line is absent and the count is 0. The scan's second argument is any
       code string, so this pins the assignment itself (main.c has no comment holding it). */
    CHECK(src_call_passes_arg("src/main.c", "rend.cursor_on = W.cursor_on", "W.cursor_on") == 1,
          "src/main.c copies the blink phase into Render.cursor_on every frame");
    /* The three passes of render_draw() must ask the SAME question about the cursor cell, or a bar
       painted in one pass and not the next erases itself (the "must match pass 2" comment). The scanner
       skips comments, so 4 is the definition plus the three call sites — re-diff, do not copy. */
    CHECK(src_word_count("src/render.c", "render_cursor") == 4,
          "src/render.c calls render_cursor() from all three passes (definition + 3 call sites)");
    CHECK(src_word_count("src/render.c", "cursor_on") == 3,
          "and consults the blink phase in all three of them — re-diff, do not copy");
    CHECK(src_word_count("src/config.c", "CURSOR_COL") >= 1,
          "CURSOR_COL survives as the built-in DEFAULT in src/config.c");
}

/* The scanner itself, pinned — against a file this test writes, NOT against this file. The first version
   scanned tests/test_render.c itself and came out RED for a reason worth keeping: the CHECK lines that name
   a probe token are themselves code containing that token, so the scan counted its own argument (3 for the
   code token — declaration, (void) cast, CHECK argument — and 1 for the comment token, against the expected
   1 and 0). A probe file this test creates has exactly the occurrences the checks claim, and it can also pin
   the rules a self-scan could not: a line comment and a block comment spanning several lines. Every mention
   of a probe token below is split by string concatenation, so this source never holds the whole word and can
   never count itself. */
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
    group_source_wiring();
    group_scanner_selfcheck();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
