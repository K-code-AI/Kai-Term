/* Kai-Term — unit tests for the renderer's pure colour decisions (src/render.c).
 *
 * No display, no EGL, no GL context, no font: the only thing under test is render_chrome(), the one
 * function that turns the configured palette into the colours a frame's chrome is drawn with. It reads a
 * Render struct and nothing else, so a test can call it with a struct filled by hand. That seam exists for
 * one bug (ISSUES.md, s102): `cursor_color =` was defaulted, parsed, stored and asserted on by the config
 * tests, and reached no pixel, because the renderer took the colour from the CURSOR_COL macro. Every check
 * that read Config stayed green. So the checks here sit at the CONSUMER — the value that is about to
 * become a vertex colour.
 *
 * What a headless test cannot reach, stated plainly: the call inside main() that hands Config's value to
 * render_init(). main() is in no linked test (the same limitation tests/cli_test.sh exists for), so that
 * wiring is pinned by the two source scans at the bottom — text checks, weaker than running it, and they
 * fail loudly if the renderer ever reads the macro again or the argument is dropped.
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
    /* The one link a headless test cannot execute: main() handing the configured value to the renderer and
       to the band's painter. The call string includes the '&' so a COMMENT that merely names the function
       (main.c has two) cannot start the scan. */
    CHECK(src_call_passes_arg("src/main.c", "render_init(&", "conf.cursor_color") == 1,
          "src/main.c passes conf.cursor_color to render_init()");
    CHECK(src_call_passes_arg("src/main.c", "render_init(&", "conf.selection_color") == 1,
          "src/main.c passes conf.selection_color to render_init()");
    CHECK(src_call_passes_arg("src/main.c", "fm_paint(&", "conf.selection_color") == 1,
          "src/main.c passes conf.selection_color to fm_paint()");
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
    group_source_wiring();
    group_scanner_selfcheck();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
