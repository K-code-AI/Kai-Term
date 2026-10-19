/* Kai-Term — unit tests for VT parser + grid (no display needed). */
#include "kai-term.h"

#include <ctype.h>           /* the config-template name scan (same classes src/config.c uses) */
#include <sys/stat.h>        /* the shell-integration directory: created, 0700, removed again */
#include <unistd.h>          /* access() on a request file that must or must not exist */

static Term T;
static int fails, checks;

#define CHECK(cond, msg) do {                                  \
    checks++;                                                  \
    if (!(cond)) {                                             \
        fails++;                                               \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);  \
    }                                                          \
} while (0)

static void feed(const char *s)
{
    term_parse(&T, (const uint8_t *)s, strlen(s));
}

/* Is there ANY glyph in the viewport? Used to prove a control sequence was consumed rather than printed:
   a parser that abandons a sequence mid-way paints its remaining bytes, and the final byte of every
   mode-ENABLING sequence (\e[?2004h, \e[?1049h, \e[?1h, \e[?25h) is an `h`. */
static int grid_has_glyph(void)
{
    for (int y = 0; y < T.g.rows; y++) {
        Cell *r = grid_vprow(&T.g, y);
        for (int x = 0; x < T.g.cols; x++)
            if (r[x].ch)
                return 1;
    }
    return 0;
}

/* What src/main.c really does with a keystroke: the bytes go to the pty (and term_note_input() tells the
   model they are outstanding), and what the tty echoes back arrives LATER, as a separate term_parse.
   Feeding an echo without noting the input would test a state the real program can never be in. */
static void type(const char *s)
{
    term_note_input(&T, s, strlen(s));
    term_parse(&T, (const uint8_t *)s, strlen(s));
}

/* A key whose echo is not the key itself: Enter sends CR, the tty echoes CRLF. */
static void press(const char *in, const char *echo)
{
    term_note_input(&T, in, strlen(in));
    term_parse(&T, (const uint8_t *)echo, strlen(echo));
}

/* Bytes the terminal itself sends to the application — the file manager's `cd`, the path it types. The
   length comes from strlen(), never from a hand-typed count: `"cd /tmp\r", 9` reads one byte past the
   literal (its NUL), and because that byte is not a CR it RE-OPENS the echo window the CR had just
   closed — which is what made the `cd it ran leaves the line clean` check fail (s76). Same class as the
   truncated-file-literal bug s72 recorded: no hand-typed byte count survives contact with the literal. */
static void send_in(const char *s)
{
    term_note_input(&T, s, strlen(s));
}

/* Fill a dead frame with non-zero pointers so a Term declared on the stack afterwards cannot
   accidentally look zero-initialized (see the term_init guard test at the end of main). */
__attribute__((noinline)) static void dirty_stack(void)
{
    volatile void *junk[512];
    for (int i = 0; i < 512; i++)
        junk[i] = (void *)(long)(0x1000 + 8 * i);
    (void)junk;   /* the writes are the point; silence -Wunused-but-set-variable */
}

static uint32_t cellch(int y, int x) { return grid_vprow(&T.g, y)[x].ch; }
static uint32_t cellfg(int y, int x) { return grid_vprow(&T.g, y)[x].fg; }
static uint32_t cellbg(int y, int x) { return grid_vprow(&T.g, y)[x].bg; }
static uint32_t cellattr(int y, int x) { return grid_vprow(&T.g, y)[x].attr; }
static uint8_t cellnextra(int y, int x) { return grid_vprow(&T.g, y)[x].nextra; }
static uint32_t cellextra(int y, int x, int k) { return grid_vprow(&T.g, y)[x].extra[k]; }

static void row_text(int y, char *out)
{
    for (int x = 0; x < T.g.cols; x++) {
        uint32_t c = cellch(y, x);
        out[x] = c ? (c < 0x80 ? (char)c : '?') : ' ';
    }
    out[T.g.cols] = 0;
}

/* ---------- reflow helpers ---------- */

/* The LOGICAL lines of the content slots: physical rows joined through the soft-wrap marks.
   This is what the user reads as "a line", so it is the thing a width change must preserve.
   Blank lines are skipped (they carry no text to compare). */
static int logical_lines(char out[32][512])
{
    int used = T.g.scroll_off + T.g.rows, n = 0;
    for (int i = 0; i < used && n < 32; ) {
        int len = 0, j = i;
        for (;;) {
            Cell *r = T.g.row[j];
            int end = T.g.cols;
            while (end > 0 && r[end - 1].ch == 0)
                end--;
            for (int x = 0; x < end && len < 500; x++) {
                uint32_t c = r[x].ch;
                out[n][len++] = (c && c < 0x80) ? (char)c : (c ? '?' : ' ');
            }
            if (j + 1 < used && T.g.wrap[j])
                j++;
            else
                break;
        }
        out[n][len] = 0;
        if (len > 0)
            n++;
        i = j + 1;
    }
    return n;
}

/* slots that actually hold something (blank rows are free space or blank lines) */
static int content_rows(void)
{
    int c = 0;
    for (int i = 0; i < T.g.scroll_off + T.g.rows; i++) {
        Cell *r = T.g.row[i];
        int any = 0;
        for (int x = 0; x < T.g.cols; x++)
            if (r[x].ch || (r[x].attr & ATTR_WIDE))
                any = 1;
        if (any)
            c++;
    }
    return c;
}

static int wide_cells(void)      /* ATTR_WIDE continuation cells anywhere in the grid */
{
    int c = 0;
    for (int i = 0; i < T.g.rcap; i++)
        for (int x = 0; x < T.g.cols; x++)
            if (T.g.row[i][x].attr & ATTR_WIDE)
                c++;
    return c;
}

static int wide_orphans(void)    /* continuation cells separated from their anchor */
{
    int o = 0;
    for (int i = 0; i < T.g.rcap; i++) {
        Cell *r = T.g.row[i];
        for (int x = 0; x < T.g.cols; x++)
            if ((r[x].attr & ATTR_WIDE) && (x == 0 || r[x - 1].ch == 0))
                o++;
    }
    return o;
}

/* ---------- layout helpers (the tab bar and the side-by-side panes) ---------- */

/* A LayoutReq for `ntab` sessions. Titles start empty (so a label reads "<n> shell"); individual checks
   overwrite rq->tab[i].title / .attention. */
static void lreq(LayoutReq *rq, int mode, int ww, int wh, int cw, int ch, int pad, int gap,
                 int ntab, int active, int split_max, int bar)
{
    memset(rq, 0, sizeof(*rq));
    rq->mode = mode;
    rq->win_w = ww; rq->win_h = wh;
    rq->cell_w = cw; rq->cell_h = ch;
    rq->pad = pad; rq->gap = gap;
    rq->ntab = ntab; rq->active = active;
    rq->split_max = split_max; rq->bar = bar;
    for (int i = 0; i < ntab && i < TAB_MAX; i++)
        rq->tab[i].tab = i;
}

/* Codepoints, not bytes — a label is only guaranteed to fit its item in COLUMNS, and a column holds one
   codepoint however many bytes it takes. */
static int utf8_len(const char *s)
{
    int n = 0;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 0x80 && c < 0xc0)
            continue;
        n++;
    }
    return n;
}

/* ---------- graphics helpers (the kitty image protocol, src/vt.c) ---------- */

/* base64 for the payloads these tests transmit. The decoder under test is static in src/vt.c, so the
   test carries its own encoder: a bug in one cannot hide a bug in the other. */
static const char B64ALPHA[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64enc(const uint8_t *in, size_t n, char *out)
{
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)in[i] << 16;
        int have = 1;
        if (i + 1 < n) { v |= (unsigned)in[i + 1] << 8; have++; }
        if (i + 2 < n) { v |= (unsigned)in[i + 2]; have++; }
        for (int k = 0; k <= have; k++)
            out[o++] = B64ALPHA[(v >> (18 - 6 * k)) & 0x3f];
        for (int k = have + 1; k < 4; k++)
            out[o++] = '=';
    }
    out[o] = 0;
}

/* One APC chunk in the shape timg 1.6.3 really emits (measured, see AGENT.md s68):
   ESC _ G <control block, keys separated by ','> ; <base64 payload> ESC \ */
static void gfx_feed(Term *t, const char *ctl, const uint8_t *payload, size_t n)
{
    char *b64 = malloc((n / 3 + 1) * 4 + 8);
    b64enc(payload, n, b64);
    char *buf = malloc(16 + strlen(ctl) + strlen(b64) + 8);
    size_t o = 0;
    buf[o++] = 0x1b;
    buf[o++] = '_';
    buf[o++] = 'G';
    memcpy(buf + o, ctl, strlen(ctl));
    o += strlen(ctl);
    if (n) {
        buf[o++] = ';';
        memcpy(buf + o, b64, strlen(b64));
        o += strlen(b64);
    }
    buf[o++] = 0x1b;
    buf[o++] = '\\';
    term_parse(t, (const uint8_t *)buf, o);
    free(buf);
    free(b64);
}

static void fill_rgba(uint8_t *p, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    for (int i = 0; i < w * h; i++) {
        p[i * 4 + 0] = r;
        p[i * 4 + 1] = g;
        p[i * 4 + 2] = b;
        p[i * 4 + 3] = a;
    }
}

static KImage *img_by_id(Term *t, int id)
{
    for (KImage *im = t->images; im; im = im->next)
        if (im->app_id == id)
            return im;
    return NULL;
}

/* ---------- the shipped config templates: which NAMES they list ----------
   Loading a template and comparing every field with config_default() proves the VALUES are right, but it
   cannot see a MISSING line: a template that stopped mentioning `padding` still loads to exactly the
   default, so the check stays green while the file claims to hold every default. This scans the key lines
   themselves, in both directions — every name the parser accepts must be listed, and every name listed must
   be one the parser accepts (a typo in a shipped template would only ever log a warning in the user's copy).
   A line counts as a key line when it starts (after blanks) with a letter and the name is followed by `=`,
   which is the shape src/config.c parses; comment lines, prose and blank lines do not. */
static int tpl_check_names(const char *path, const char *const *names, int n)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("FAIL: template: cannot read %s\n", path);
        return 1;
    }
    int *seen = calloc((size_t)n, sizeof *seen);
    CHECK(seen != NULL, "template: the name-coverage scan has its tally");
    if (!seen) {
        fclose(f);
        return 1;
    }

    int bad = 0;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (!isalpha((unsigned char)*p))
            continue;
        char *end = p;
        while (isalnum((unsigned char)*end) || *end == '_')
            end++;
        char *gap = end;
        while (*gap == ' ' || *gap == '\t')
            gap++;
        if (*gap != '=')
            continue;
        *end = 0;
        int idx = -1;
        for (int i = 0; i < n; i++)
            if (!strcmp(names[i], p)) {
                idx = i;
                break;
            }
        if (idx < 0) {
            printf("FAIL: template: %s lists '%s', which the config parser does not know\n", path, p);
            bad++;
        } else if (seen[idx]++) {
            printf("FAIL: template: %s lists %s more than once\n", path, names[idx]);
            bad++;
        }
    }
    fclose(f);
    for (int i = 0; i < n; i++)
        if (!seen[i]) {
            printf("FAIL: template: %s does not list %s — the file says it holds every default\n",
                   path, names[i]);
            bad++;
        }
    free(seen);
    return bad;
}

/* The names src/config.c compares inside ONE function body. The config surface is a strcmp ladder, so that
   ladder IS the list of accepted names — deriving it here is what keeps the check honest when a setting is
   added: the shipped template must gain the line, not a hand-copied list in this file. A body ends at the
   column-0 `}` (verified: neither ladder contains one before its end). Returns the count, or -1 when the
   signature is not found, so a rename fails loudly instead of quietly checking nothing. */
static int cfg_names_of(const char *src_path, const char *sig, const char *names[], int cap)
{
    FILE *f = fopen(src_path, "r");
    if (!f)
        return -1;
    char line[4096];
    int in = 0, n = 0;
    while (fgets(line, sizeof line, f)) {
        if (!in) {
            if (!strncmp(line, sig, strlen(sig)))
                in = 1;
            continue;
        }
        if (line[0] == '}')
            break;
        const char *marker = "strcmp(key, \"";
        const char *q = strstr(line, marker);
        if (!q)
            continue;
        q += strlen(marker);
        const char *e = strchr(q, '"');
        if (!e || e == q || n >= cap)
            break;
        size_t len = (size_t)(e - q);
        char *buf = malloc(len + 1);
        if (!buf)
            break;
        memcpy(buf, q, len);
        buf[len] = 0;
        names[n++] = buf;
    }
    fclose(f);
    return in ? n : -1;
}

/* the ImgView slot one application id was placed in, or -1 */
static int view_of(const ImgView *view, int n, int id)
{
    for (int i = 0; i < n; i++)
        if (view[i].img->app_id == id)
            return i;
    return -1;
}

/* An 8x8 RGBA PNG (8-bit, non-interlaced) and the same bytes inside a zlib stream: the two payload
   shapes timg 1.6.3 emits (f=100 with and without --compress). Generated with python3/zlib, kept as
   literals so the test needs no image file and no encoder of its own. */
static const uint8_t PNG8[226] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
    0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x08,
    0x08,0x06,0x00,0x00,0x00,0xc4,0x0f,0xbe,0x8b,0x00,0x00,0x00,
    0xa9,0x49,0x44,0x41,0x54,0x78,0x9c,0x0d,0xca,0x21,0x81,0xc4,
    0x50,0x0c,0x45,0xd1,0x80,0x91,0x30,0x60,0x51,0xf0,0x88,0x88,
    0x88,0xe2,0xe0,0x8a,0x78,0x22,0x3e,0x0e,0xfe,0x22,0x22,0xa2,
    0x38,0xb8,0x22,0xea,0xe0,0x6e,0x0f,0x3e,0x66,0x66,0xb8,0x7d,
    0x09,0xfb,0x91,0x16,0xc8,0x0e,0xca,0x4e,0xda,0xc4,0xd8,0xc2,
    0xcc,0xbf,0xb8,0xff,0x08,0x0f,0xd2,0x0f,0xe4,0x27,0xe5,0xa2,
    0x7d,0x31,0xbe,0xdf,0x10,0x3f,0x3c,0x82,0x88,0x83,0x8c,0x13,
    0x85,0xa8,0x58,0x74,0x6c,0x26,0xfa,0x0d,0x19,0x78,0x1e,0x44,
    0x9e,0x64,0x0a,0xe5,0xa2,0x72,0xd3,0xd9,0x4c,0x5e,0x6f,0xd0,
    0x81,0xeb,0x24,0x24,0x52,0x0b,0x69,0x53,0x6a,0x5a,0x17,0xa3,
    0xfb,0x0d,0x75,0xe2,0x25,0xa2,0x16,0x59,0x1b,0x55,0x53,0x75,
    0xd1,0x75,0x33,0xf5,0xbc,0xa1,0x85,0xf7,0x22,0x7a,0x93,0xdd,
    0xa8,0x2f,0xaa,0x6f,0xba,0x1f,0xa6,0x3f,0x6f,0x98,0x85,0xcf,
    0x26,0xa6,0xc9,0xb9,0xd0,0xdc,0xd4,0x3c,0xf4,0x7c,0x98,0xf9,
    0xe3,0x1f,0x1e,0x44,0x94,0x41,0x17,0x18,0x37,0xfe,0x00,0x00,
    0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82,
};
static const uint8_t PNG8_ZLIB[237] = {
    0x78,0xda,0x01,0xe2,0x00,0x1d,0xff,0x89,0x50,0x4e,0x47,0x0d,
    0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,
    0x00,0x00,0x08,0x00,0x00,0x00,0x08,0x08,0x06,0x00,0x00,0x00,
    0xc4,0x0f,0xbe,0x8b,0x00,0x00,0x00,0xa9,0x49,0x44,0x41,0x54,
    0x78,0x9c,0x0d,0xca,0x21,0x81,0xc4,0x50,0x0c,0x45,0xd1,0x80,
    0x91,0x30,0x60,0x51,0xf0,0x88,0x88,0x88,0xe2,0xe0,0x8a,0x78,
    0x22,0x3e,0x0e,0xfe,0x22,0x22,0xa2,0x38,0xb8,0x22,0xea,0xe0,
    0x6e,0x0f,0x3e,0x66,0x66,0xb8,0x7d,0x09,0xfb,0x91,0x16,0xc8,
    0x0e,0xca,0x4e,0xda,0xc4,0xd8,0xc2,0xcc,0xbf,0xb8,0xff,0x08,
    0x0f,0xd2,0x0f,0xe4,0x27,0xe5,0xa2,0x7d,0x31,0xbe,0xdf,0x10,
    0x3f,0x3c,0x82,0x88,0x83,0x8c,0x13,0x85,0xa8,0x58,0x74,0x6c,
    0x26,0xfa,0x0d,0x19,0x78,0x1e,0x44,0x9e,0x64,0x0a,0xe5,0xa2,
    0x72,0xd3,0xd9,0x4c,0x5e,0x6f,0xd0,0x81,0xeb,0x24,0x24,0x52,
    0x0b,0x69,0x53,0x6a,0x5a,0x17,0xa3,0xfb,0x0d,0x75,0xe2,0x25,
    0xa2,0x16,0x59,0x1b,0x55,0x53,0x75,0xd1,0x75,0x33,0xf5,0xbc,
    0xa1,0x85,0xf7,0x22,0x7a,0x93,0xdd,0xa8,0x2f,0xaa,0x6f,0xba,
    0x1f,0xa6,0x3f,0x6f,0x98,0x85,0xcf,0x26,0xa6,0xc9,0xb9,0xd0,
    0xdc,0xd4,0x3c,0xf4,0x7c,0x98,0xf9,0xe3,0x1f,0x1e,0x44,0x94,
    0x41,0x17,0x18,0x37,0xfe,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,
    0x44,0xae,0x42,0x60,0x82,0x96,0x43,0x5d,0x9a,
};

int main(void)
{
    term_init(&T, 10, 20, 100);

    /* --- basic printing --- */
    feed("hello");
    CHECK(cellch(0, 0) == 'h' && cellch(0, 4) == 'o', "print hello");
    CHECK(T.cx == 5 && T.cy == 0, "cursor after hello");
    feed("\r\nworld");
    char row[64];
    row_text(1, row);
    CHECK(!memcmp(row, "world", 5), "second line");

    /* --- CUP + ED + EL --- */
    feed("\x1b[3;5H");
    CHECK(T.cx == 4 && T.cy == 2, "CUP 3;5");
    feed("X");
    CHECK(cellch(2, 4) == 'X', "char at CUP");
    feed("\x1b[2J\x1b[H");
    CHECK(cellch(0, 0) == 0 && cellch(2, 4) == 0, "ED 2 clears");
    feed("abcdef\r\x1b[2K");
    row_text(0, row);
    CHECK(row[0] == ' ', "EL 1 clears line start");

    /* --- SGR colors --- */
    feed("\x1b[2J\x1b[H\x1b[31mR\x1b[0m");
    CHECK(cellfg(0, 0) == term_palette()[1], "SGR 31 -> palette red");
    feed("\x1b[38;2;10;20;30mT\x1b[0m");
    CHECK(cellfg(0, 1) == 0xff0a141eu, "truecolor fg");
    feed("\x1b[48;5;196mU\x1b[0m");
    CHECK(cellbg(0, 2) == term_palette()[196], "256-color bg");
    feed("\x1b[1mB\x1b[0m");
    CHECK(cellattr(0, 3) & ATTR_BOLD, "bold attr");
    CHECK(!(cellattr(0, 4) & ATTR_BOLD), "reset clears bold");

    /* --- scrolling moves the first lines to scrollback ---
     * 12 lines each ending with \r\n: the newlines after line10, line11 and line12 each
     * scroll one line up, so 3 lines end up in scrollback and the bottom viewport row is
     * left blank. */
    feed("\x1b[2J\x1b[H");
    for (int i = 1; i <= 12; i++) {
        char line[32];
        snprintf(line, sizeof(line), "line%02d\r\n", i);
        feed(line);
    }
    CHECK(T.g.scroll_off == 3, "scroll_off == 3 after 12 lines in 10 rows");
    CHECK(T.g.row[0][0].ch == 'l' && T.g.row[0][1].ch == 'i' &&
          T.g.row[0][5].ch == '1', "scrollback row[0] holds line01");
    CHECK(T.g.row[1][0].ch == 'l' && T.g.row[1][5].ch == '2', "scrollback row[1] holds line02");
    CHECK(T.g.row[2][0].ch == 'l' && T.g.row[2][5].ch == '3', "newest scrollback is line03");
    row_text(0, row);
    CHECK(!memcmp(row, "line04", 6), "viewport top is line04");
    CHECK((void *)T.g.row[3] == (void *)grid_vprow(&T.g, 0),
          "viewport starts at row[scroll_off]");

    /* --- scrollback with room: nothing is evicted --- */
    feed("\x1b[2J\x1b[H");
    for (int i = 1; i <= 12; i++) {
        char line[32];
        snprintf(line, sizeof(line), "A%02d\r\n", i);
        feed(line);
    }
    feed("B13\r\nB14");
    /* ED clears the viewport but does not reset scroll_off, so it keeps accumulating:
     * 3 (previous block) + 3 + 1 = 7. */
    CHECK(T.g.scroll_off == 7, "scroll_off == 7 (ED does not reset scrollback)");
    row_text(0, row);
    CHECK(!memcmp(row, "A05", 3), "viewport top A05");
    Cell *newest = T.g.row[T.g.scroll_off - 1];
    CHECK(newest[0].ch == 'A' && newest[1].ch == '0' && newest[2].ch == '4',
          "newest scrollback line is A04");
    row_text(9, row);
    CHECK(!memcmp(row, "B14", 3), "bottom line is the newest");

    /* --- scrollback full: oldest line evicted, its buffer recycled --- */
    term_destroy(&T);
    term_init(&T, 4, 20, 2);
    for (int i = 1; i <= 6; i++) {
        char line[32];
        snprintf(line, sizeof(line), "A%02d\r\n", i);
        feed(line);
    }
    CHECK(T.g.rcap == 6 && T.g.scroll_off == 2, "scrollback full (max 2)");
    row_text(0, row);
    CHECK(!memcmp(row, "A04", 3), "eviction: viewport top is A04");
    CHECK(T.g.row[0][0].ch == 'A' && T.g.row[0][2].ch == '2',
          "oldest kept is A02 (A01 was evicted)");
    CHECK(T.g.row[1][0].ch == 'A' && T.g.row[1][2].ch == '3', "newest scrollback is A03");
    term_destroy(&T);
    term_init(&T, 10, 20, 100);

    /* --- scroll region --- */
    feed("\x1b[2J\x1b[H\x1b[1r");           /* full-screen region */
    feed("\x1b[2;4r");                       /* region rows 2..4 */
    feed("\x1b[2;1H");
    CHECK(T.cy == 1, "cursor at region top after H");
    feed("A\r\nB\r\nC\r\nD");                /* D should scroll within region */
    row_text(1, row);
    CHECK(row[0] == 'B', "region scroll moved A out, B to top of region");
    row_text(3, row);
    CHECK(row[0] == 'D', "D written at region bottom");
    feed("\x1b[1r");

    /* --- scrollback pull-back via scroll down (SD) --- */
    feed("\x1b[2J\x1b[H");
    for (int i = 1; i <= 12; i++) {
        char line[32];
        snprintf(line, sizeof(line), "L%02d\r\n", i);
        feed(line);
    }
    CHECK(T.g.scroll_off == 3, "scroll_off 3 again");
    feed("\x1b[1T");   /* SD: the newest scrollback line returns to the viewport top */
    CHECK(T.g.scroll_off == 2, "SD pulled scrollback back");
    row_text(0, row);
    CHECK(row[0] == 'L' && row[1] == '0' && row[2] == '3', "top line is L03 after SD");
    row_text(9, row);
    CHECK(row[0] == 'L' && row[1] == '1' && row[2] == '2', "SD pushed the blank row out");

    /* --- live_off: the anchor that follows the newest output ---
     * scroll_off is an ABSOLUTE index into row[] (row[0] = oldest retained line), so the live
     * view is at scroll_off == live_off, NOT at scroll_off == 0. Navigation must clamp to
     * [0, live_off] and snap-to-live must restore live_off. These mirror main.c's
     * scrollback_nav()/snap_to_live() math exactly. Fresh grid so row[0] is the first M line. */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    for (int i = 1; i <= 12; i++) {
        char line[32];
        snprintf(line, sizeof(line), "M%02d\r\n", i);
        feed(line);
    }
    CHECK(T.g.live_off == 3, "live_off == 3 after 12 lines in 10 rows");
    CHECK(T.g.scroll_off == T.g.live_off, "a printing terminal stays at the live anchor");
    row_text(8, row);
    CHECK(row[0] == 'M' && row[1] == '1' && row[2] == '2', "live view shows the newest line M12");

    /* nav UP into history (main.c scrollback_nav(+5)) must move toward row[0] */
    T.g.scroll_off = iclamp_(T.g.scroll_off - 5, 0, T.g.live_off);
    CHECK(T.g.scroll_off == 0, "nav up 5 from live_off 3 clamps at 0 (oldest kept)");
    row_text(0, row);
    CHECK(row[0] == 'M' && row[1] == '0' && row[2] == '1', "scrolled back: viewport top is M01");

    /* snap to live must land on live_off, not on 0 (the old bug: it showed the oldest lines) */
    T.g.scroll_off = T.g.live_off;
    row_text(8, row);
    CHECK(row[0] == 'M' && row[1] == '1' && row[2] == '2', "snap-to-live shows the newest line M12");

    /* a whole-viewport scroll while scrolled back snaps the view to live first, so the slots
       below the viewport (which hold the newest lines) are never blanked */
    T.g.scroll_off = 0;
    feed("\x1b[10;1H");            /* cursor to the viewport bottom row */
    feed("\r\n");                  /* forces scroll_up over the whole viewport */
    CHECK(T.g.live_off == 4, "live_off advanced with the scroll");
    CHECK(T.g.scroll_off == T.g.live_off, "scroll_up snapped the scrolled-back view to live");
    row_text(7, row);
    CHECK(row[0] == 'M' && row[1] == '1' && row[2] == '2', "newest line still visible after snap");

    /* a fresh terminal has nothing above the live view: live_off == 0 -> no navigation */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    feed("fresh\r\n");
    CHECK(T.g.live_off == 0 && T.g.scroll_off == 0, "no scrollback accumulated yet");
    CHECK(iclamp_(T.g.scroll_off - 5, 0, T.g.live_off) == 0, "cannot scroll into unwritten rows");

    /* resize must keep the live anchor consistent and keep the cursor on the same content line */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    for (int i = 1; i <= 14; i++) {
        char line[32];
        snprintf(line, sizeof(line), "K%02d\r\n", i);
        feed(line);
    }
    CHECK(T.g.scroll_off == 5 && T.g.live_off == 5, "14 lines -> live_off 5");
    T.g.scroll_off = 1;                       /* pretend the user is scrolled back */
    term_resize(&T, 8, 20);
    CHECK(T.g.scroll_off == T.g.live_off, "resize returns the view to the live anchor");
    CHECK(T.g.live_off == 7 && T.g.rows == 8, "shrink 2 rows advances the anchor by 2");
    CHECK(T.cy == 7, "cursor stays on the same content line after shrink");
    row_text(6, row);
    CHECK(row[0] == 'K' && row[1] == '1' && row[2] == '4', "newest line K14 kept after shrink");
    term_resize(&T, 10, 20);
    CHECK(T.g.live_off == 5 && T.g.scroll_off == 5, "grow 2 rows slides the anchor back up by 2");
    CHECK(T.cy == 9, "cursor stays on the same content line after grow");
    row_text(8, row);
    CHECK(row[0] == 'K' && row[1] == '1' && row[2] == '4', "newest line K14 kept after grow");
    term_destroy(&T);
    term_init(&T, 10, 20, 100);

    /* --- UTF-8 --- */
    feed("\x1b[2J\x1b[H");
    feed("aé€\U0001F600");
    CHECK(cellch(0, 0) == 'a', "ascii a");
    CHECK(cellch(0, 1) == 0xE9, "2-byte e-acute");
    CHECK(cellch(0, 2) == 0x20AC, "3-byte euro");
    CHECK(cellch(0, 3) == 0x1F600, "4-byte emoji");
    CHECK(cellattr(0, 4) & ATTR_WIDE, "emoji reserves a second cell (Emoji_Presentation)");

    /* --- wide char --- */
    feed("\x1b[2J\x1b[H");
    feed("\xE4\xB8\xADX");   /* U+4E2D then X */
    CHECK(cellch(0, 0) == 0x4E2D, "CJK char");
    CHECK(cellattr(0, 1) & ATTR_WIDE, "wide continuation cell");
    CHECK(cellch(0, 2) == 'X', "X after wide char");

    /* --- tabs --- */
    feed("\x1b[2J\x1b[H");
    feed("a\tb");
    CHECK(cellch(0, 8) == 'b', "tab to column 8");

    /* --- ICH / DCH (parameter 1 is the count, cursor must be set first) --- */
    feed("\x1b[2J\x1b[H");
    feed("abcd");
    feed("\x1b[1;2H\x1b[2@");                       /* insert 2 blanks at col 2 */
    row_text(0, row);
    CHECK(row[0] == 'a' && row[1] == ' ' && row[2] == ' ' && row[3] == 'b', "ICH");
    feed("\x1b[2J\x1b[Habcd\x1b[1;2H\x1b[2P");      /* delete 2 chars at col 2 */
    row_text(0, row);
    CHECK(row[0] == 'a' && row[1] == 'd', "DCH");

    /* --- IL / DL (parameter 1 is the count) --- */
    feed("\x1b[2J\x1b[H");
    feed("r1\r\nr2\r\nr3");
    feed("\x1b[2;1H\x1b[1L");                       /* insert 1 blank line at row 2 */
    row_text(0, row);
    CHECK(row[0] == 'r' && row[1] == '1', "IL leaves row 1 alone");
    row_text(1, row);
    CHECK(row[0] == ' ', "IL inserts a blank line at the cursor row");
    row_text(2, row);
    CHECK(row[0] == 'r' && row[1] == '2', "IL moved r2 down one row");
    row_text(3, row);
    CHECK(row[0] == 'r' && row[1] == '3', "IL moved r3 down one row");
    feed("\x1b[2J\x1b[Hr1\r\nr2\r\nr3\x1b[1;1H\x1b[1M");   /* delete the line at row 1 */
    row_text(0, row);
    CHECK(row[0] == 'r' && row[1] == '2', "DL removes the first line");
    row_text(1, row);
    CHECK(row[0] == 'r' && row[1] == '3', "DL moved r3 up one row");

    /* --- cursor save/restore --- */
    feed("\x1b[2J\x1b[H");
    feed("\x1b[5;6H\x1b" "7z\x1b" "8Y");
    CHECK(cellch(4, 5) == 'Y', "ESC 7/8 restore rewrote same cell");
    CHECK(cellch(4, 6) == 0, "nothing written past restored cell");

    /* --- wrap --- */
    feed("\x1b[2J\x1b[H");
    for (int i = 0; i < 25; i++) feed("abcdefghij");   /* 250 chars, 20 cols */
    row_text(0, row);
    CHECK(!memcmp(row, "abcdefghijabcdefghij", 20), "autowrap fills row 1");
    row_text(1, row);
    CHECK(row[0] == 'a' && row[1] == 'b', "autowrap continues at the next line");

    /* --- resize keeps the viewport bottom anchored --- */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    for (int i = 1; i <= 12; i++) {
        char line[32];
        snprintf(line, sizeof(line), "L%02d\r\n", i);
        feed(line);
    }
    feed("tail");                       /* 'tail' sits on the live bottom row */
    term_resize(&T, 5, 10);
    CHECK(T.g.rows == 5 && T.g.cols == 10, "resize dims");
    CHECK(T.g.scroll_off >= 1, "resize pushed content into scrollback");
    row_text(4, row);
    CHECK(row[0] == 't' && row[1] == 'a', "bottom line preserved after shrink");
    term_resize(&T, 10, 20);
    CHECK(T.g.rows == 10 && T.g.cols == 20, "resize back");
    row_text(9, row);
    CHECK(row[0] == 't' && row[1] == 'a', "bottom line still at the bottom after grow");

    /* --- resize with a full scrollback must keep the invariant --- */
    term_destroy(&T);
    term_init(&T, 4, 10, 2);
    feed("1\r\n2\r\n3\r\n4\r\n5\r\n6");
    CHECK(T.g.scroll_off == 2 && T.g.rcap == 6, "small scrollback full");
    term_resize(&T, 6, 10);
    CHECK(T.g.rows == 6 && T.g.scroll_off + T.g.rows <= T.g.rcap,
          "grow: scrollback clamped to rcap");
    CHECK(T.g.scroll_off >= 0 && T.g.row[T.g.scroll_off + 5], "grow: bottom row exists");
    term_destroy(&T);
    term_init(&T, 4, 10, 0);
    feed("1\r\n2\r\n3\r\n4\r\n5\r\n6");
    CHECK(T.g.scroll_off == 0 && T.g.rcap == 4, "no scrollback configured");
    row_text(3, row);
    CHECK(row[0] == '6', "no-scrollback viewport bottom is newest line");
    term_resize(&T, 2, 10);
    CHECK(T.g.scroll_off == 0 && T.g.rows == 2 && T.g.rcap == 4, "shrink without scrollback");
    row_text(1, row);
    CHECK(row[0] == '6', "shrink keeps bottom line (no scrollback)");

    /* --- a height SHRINK must never move the cursor off its own line ---------------------------
       The zoom bug: zooming changes cols AND rows at once. The old shrink always gave
       up the viewport's TOP row, so a shrink larger than the number of rows above the cursor pushed
       the cursor's own line into scrollback; term_resize then clamped cy to 0, the shell repainted
       its prompt at that clamped cursor, and every zoom step left one stale prompt and a blank gap.
       The rule that fixes it: give up the rows BELOW the cursor first. */
    term_destroy(&T);
    term_init(&T, 20, 20, 100);
    feed("\r\nPROMPT-ONE\r\nPROMPT-TWO > ");     /* a two-line prompt, cursor on viewport row 2 */
    CHECK(T.cy == 2, "prompt: the cursor sits on row 2");
    int pslot = T.g.live_off + T.cy;            /* the cursor's absolute row[] slot */
    term_resize(&T, 8, 20);                     /* shrink by 12: far more than the 2 rows above it */
    CHECK(T.g.live_off + T.cy == pslot, "shrink keeps the cursor on its own row[] slot");
    CHECK(T.cy == 2, "shrink keeps the cursor's viewport row");
    CHECK(T.g.live_off == 0, "shrink with blank rows below the cursor pushes nothing into scrollback");
    CHECK(T.g.scroll_off + T.g.rows == 8, "shrink: used == rows, the freed slots stay below");
    row_text(1, row);
    CHECK(!memcmp(row, "PROMPT-ONE", 10), "shrink keeps the prompt's first line visible");
    row_text(2, row);
    CHECK(!memcmp(row, "PROMPT-TWO", 10), "shrink keeps the line the cursor is on");
    term_resize(&T, 20, 20);                    /* zoom back out */
    CHECK(T.g.live_off + T.cy == pslot && T.cy == 2, "grow back keeps the cursor where it was");
    /* three whole zoom cycles (cols AND rows, like zoom_apply) must not duplicate the prompt */
    for (int cyc = 0; cyc < 3; cyc++) {
        term_resize(&T, 6, 16);
        term_resize(&T, 20, 20);
    }
    int copies = 0;
    for (int y = 0; y < T.g.rows; y++) {
        row_text(y, row);
        if (!memcmp(row, "PROMPT", 6))
            copies++;
    }
    CHECK(copies == 2, "three zoom cycles leave exactly the two prompt lines (no leftovers)");
    CHECK(T.g.live_off + T.cy == pslot, "zoom cycles keep the cursor slot");
    CHECK(T.g.scroll_off <= T.g.live_off &&
          T.g.live_off <= imin_(T.g.scrollback_max, T.g.rcap - T.g.rows) &&
          T.g.scroll_off + T.g.rows <= T.g.rcap, "zoom cycles keep the grid invariant");

    /* Rows below the cursor are only spare while they are blank. When the viewport is full of text
       (cursor on the bottom row) the OLD rule still applies: the top line goes to scrollback, the
       newest content stays visible, and the cursor travels with its own line. */
    term_destroy(&T);
    term_init(&T, 10, 10, 100);
    for (int i = 0; i < 6; i++) { char l[8]; snprintf(l, sizeof l, "L%d\r\n", i); feed(l); }
    CHECK(T.cy == 6, "six lines: cursor on row 6, rows 7..9 blank");
    int fslot = T.g.live_off + T.cy;
    term_resize(&T, 3, 10);
    CHECK(T.g.live_off + T.cy == fslot, "full-viewport shrink keeps the cursor slot");
    CHECK(T.g.scroll_off == 4, "full-viewport shrink pushes the top lines into scrollback");
    row_text(0, row);
    CHECK(row[0] == 'L' && row[1] == '4', "full-viewport shrink keeps the NEWEST lines visible");

    /* And when the cursor sits in the middle of a block of text, the rows given up are the ones under
       it — the same trade-off kitty makes, and what keeps the application's cursor anchored. */
    term_destroy(&T);
    term_init(&T, 10, 10, 100);
    for (int i = 0; i < 8; i++) { char l[8]; snprintf(l, sizeof l, "M%d\r\n", i); feed(l); }
    feed("\x1b[3H");                            /* cursor up to row 2, text below it */
    int mslot = T.g.live_off + T.cy;
    term_resize(&T, 5, 10);
    CHECK(T.g.live_off + T.cy == mslot, "mid-text shrink keeps the cursor slot");
    CHECK(T.cy == 2, "mid-text shrink keeps the cursor's viewport row");
    row_text(2, row);
    CHECK(row[0] == 'M' && row[1] == '2', "mid-text shrink keeps the cursor's line");
    row_text(4, row);
    CHECK(row[0] == 'M' && row[1] == '4', "mid-text shrink keeps the rows down to the new bottom");

    /* The alternate screen follows the same rule (grid_resize_plain gets the cursor slot), so a
       full-screen application keeps its cursor anchored across a zoom too. */
    term_destroy(&T);
    term_init(&T, 10, 10, 100);
    feed("\x1b[?1049h");
    feed("V1\r\nV2\r\nV3");
    CHECK(T.alt_screen == 1, "alt screen active");
    int aslot = T.g.live_off + T.cy;
    term_resize(&T, 4, 10);
    CHECK(T.g.live_off + T.cy == aslot, "alt screen shrink keeps the cursor slot");
    CHECK(T.cy == 2, "alt screen shrink keeps the cursor's viewport row");
    row_text(2, row);
    CHECK(row[0] == 'V' && row[1] == '3', "alt screen keeps the cursor's line visible");
    feed("\x1b[?1049l");

    /* --- window padding: one helper decides the grid size AND its pixel origin -----------------
       main.c uses the returned offset for the pointer -> cell mapping and hands it to render.c, so
       these checks are what keep a click, the highlight and the drawn glyph on the same cell. */
    int cols, rows, xoff, yoff;
    term_grid_geometry(936, 1002, 8, 17, 0, &cols, &rows, &xoff, &yoff);
    CHECK(cols == 117 && rows == 58 && xoff == 0 && yoff == 0,
          "padding 0 is the old edge-to-edge layout (window/cell, remainder at bottom/right)");
    term_grid_geometry(936, 1002, 8, 17, 8, &cols, &rows, &xoff, &yoff);
    CHECK(xoff == 8 && yoff == 8, "the margin is applied on the left AND the top");
    CHECK(cols == 115, "16 px of margin costs exactly 2 columns at an 8 px cell");
    CHECK(rows == 58, "the same 16 px costs no row at a 17 px cell — the remainder absorbed it");
    /* The grid must fill the space between the margins, and one more cell must not fit there — that
       is what makes the pointer mapping exact rather than approximate. */
    CHECK(xoff + cols * 8 <= 936 && (cols + 1) * 8 > 936 - 2 * xoff,
          "the grid fills the space between the two margins; one more column would not fit");
    CHECK(yoff + rows * 17 <= 1002 && (rows + 1) * 17 > 1002 - 2 * yoff,
          "and the same vertically, so the leftover pixels stay at the bottom");
    /* The remainder belongs at the bottom/right: the bottom margin is the configured one PLUS the
       leftover pixels, the top margin is exactly what was configured. */
    CHECK(1002 - yoff - rows * 17 >= yoff, "the bottom margin is never smaller than the top one");
    CHECK(936 - xoff - cols * 8 >= xoff, "the right margin is never smaller than the left one");
    /* Every pixel of the GRID area must map to the cell drawn under it: the input side is main.c's
       (p - off)/cell clamped, the output side is render.c's off + cell*cellw span. A mismatch is a
       dead zone (a click that highlights a different glyph than the one under the pointer), which is
       exactly what a padding change could introduce. Pixels in the margin are deliberately clamped to
       the first/last cell, so the sweep covers the grid area only. Counted per geometry, so one check
       per window size rather than one per pixel. */
    struct { int w, h, cw, ch, pad; } geos[] = {
        { 936, 1002, 8, 17, 0 }, { 936, 1002, 8, 17, 8 }, { 300, 200, 9, 19, 4 },
        { 120, 90, 8, 17, 20 }, { 40, 40, 8, 17, 30 }, { 17, 17, 8, 17, 8 },
    };
    for (size_t gi = 0; gi < sizeof geos / sizeof geos[0]; gi++) {
        int gw = geos[gi].w, gh = geos[gi].h, cw = geos[gi].cw, ch = geos[gi].ch;
        term_grid_geometry(gw, gh, cw, ch, geos[gi].pad, &cols, &rows, &xoff, &yoff);
        int bad = 0;
        if (cols < 1 || rows < 1 || xoff < 0 || yoff < 0)
            bad = 1;
        /* the grid must lie inside the window, and it must be MAXIMAL: one more cell may not fit in
           the area left between the two margins */
        if (xoff + cols * cw > gw || yoff + rows * ch > gh)
            bad = 2;
        if ((cols + 1) * cw <= gw - 2 * xoff || (rows + 1) * ch <= gh - 2 * yoff)
            bad = 3;   /* a cell still fits between the margins -> cols/rows are not maximal */
        for (int p = xoff; p < xoff + cols * cw && !bad; p++) {
            int c = iclamp_((p - xoff) / cw, 0, cols - 1);
            if (c < 0 || c >= cols || p < xoff + c * cw || p >= xoff + (c + 1) * cw)
                bad = 4;
        }
        for (int p = yoff; p < yoff + rows * ch && !bad; p++) {
            int r = iclamp_((p - yoff) / ch, 0, rows - 1);
            if (r < 0 || r >= rows || p < yoff + r * ch || p >= yoff + (r + 1) * ch)
                bad = 5;
        }
        CHECK(bad == 0, "every grid pixel maps to the cell drawn under it (padding geometry sweep)");
    }
    /* A margin may never eat the last cell: a tiny window still gets a 1x1 grid and an origin that
       keeps that cell inside the window. */
    term_grid_geometry(40, 40, 8, 17, 30, &cols, &rows, &xoff, &yoff);
    CHECK(cols == 1 && rows == 1, "a window smaller than two margins still gets a 1x1 grid");
    CHECK(xoff + 8 <= 40 && yoff + 17 <= 40,
          "the clamped margin keeps the single cell inside the window");
    term_grid_geometry(1, 1, 0, 0, 0, &cols, &rows, &xoff, &yoff);
    CHECK(cols >= 1 && rows >= 1 && xoff >= 0 && yoff >= 0,
          "a zero cell size / 1x1 window cannot divide by zero or produce a negative origin");
    term_grid_geometry(936, 1002, 8, 17, -5, &cols, &rows, &xoff, &yoff);
    CHECK(xoff == 0 && yoff == 0 && cols == 117 && rows == 58, "a negative margin means no margin");

    /* --- reflow: a width change re-wraps, it must never truncate ---------------------- */
    static char lbuf[32][512];
#define LONG56 "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrst"   /* 56 chars */

    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    feed(LONG56 "\r\n");
    CHECK(T.g.wrap[0] == 1 && T.g.wrap[1] == 1 && T.g.wrap[2] == 0,
          "soft-wrapped rows are marked, the hard-broken one is not");
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], LONG56), "one logical line at 20 cols");
    CHECK(content_rows() == 3, "56 chars at 20 cols take 3 physical rows");

    term_resize(&T, 10, 10);
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], LONG56),
          "narrowing keeps every character (re-broken, not truncated)");
    CHECK(content_rows() == 6, "the same line takes 6 rows at 10 cols");

    term_resize(&T, 10, 20);
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], LONG56), "widening restores the logical line");
    CHECK(content_rows() == 3, "widening restores the original physical wrapping");

    /* the cursor keeps its logical cell, and typing continues the same line */
    term_destroy(&T);
    term_init(&T, 6, 10, 50);
    feed("0123456789ABCDE");
    CHECK(T.cx == 5 && T.cy == 1, "cursor on the wrapped row before the resize");
    term_resize(&T, 6, 20);
    CHECK(T.cx == 15 && T.cy == 0, "cursor keeps its logical column when the line fits again");
    feed("F");
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], "0123456789ABCDEF"),
          "typing after a reflow appends to the same logical line");
    term_resize(&T, 6, 5);
    feed("X");
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], "0123456789ABCDEFX"),
          "after narrowing the cursor still writes at the end of the wrapped line");

    /* a wide glyph is never split from its continuation cell, and no padding hole is left */
    term_destroy(&T);
    term_init(&T, 6, 10, 50);
    feed("abc" "\xe4\xb8\xad" "def");
    CHECK(wide_cells() == 1, "the CJK glyph reserves one continuation cell");
    term_resize(&T, 6, 4);            /* a naive cut at column 4 would split the pair */
    CHECK(wide_orphans() == 0, "no wide continuation cell is orphaned by the re-wrap");
    CHECK(wide_cells() == 1, "the wide pair still occupies exactly one continuation cell");
    term_resize(&T, 6, 10);
    CHECK(wide_cells() == 1 && wide_orphans() == 0, "the round trip keeps the wide pair intact");
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], "abc? def"),
          "the 10 -> 4 -> 10 round trip leaves no padding hole inside the text");

    /* narrowing past the buffer evicts the OLDEST rows and keeps the invariants */
    term_destroy(&T);
    term_init(&T, 4, 20, 4);
    for (int i = 0; i < 6; i++)
        feed("01234567890123456789\r\n");
    feed("NEWEST");
    term_resize(&T, 4, 5);
    int nl = logical_lines(lbuf);
    CHECK(T.g.scroll_off + T.g.rows <= T.g.rcap, "reflow keeps used <= rcap");
    CHECK(T.g.live_off <= T.g.scrollback_max, "reflow keeps live_off <= scrollback_max");
    CHECK(nl > 0 && strstr(lbuf[nl - 1], "NEWEST") != NULL, "the newest line survives eviction");

    /* hard line breaks are never joined, and an erased line stops joining */
    term_destroy(&T);
    term_init(&T, 6, 10, 50);
    feed("ab\r\ncd\r\n");
    term_resize(&T, 6, 20);
    CHECK(logical_lines(lbuf) == 2 && !strcmp(lbuf[0], "ab") && !strcmp(lbuf[1], "cd"),
          "hard-broken lines stay separate lines");
    term_destroy(&T);
    term_init(&T, 6, 10, 50);
    feed("0123456789abcd");           /* wrapped onto row 1 */
    feed("\x1b[1;1H\x1b[0K");         /* cursor to row 0, erase to end of line */
    CHECK(T.g.wrap[0] == 0, "erase-line clears the soft-wrap mark");
    term_resize(&T, 6, 20);
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], "abcd"),
          "an erased wrapped line is not joined back into one line");

    /* a coloured background run is not "trailing whitespace" and must survive */
    term_destroy(&T);
    term_init(&T, 6, 10, 50);
    feed("\x1b[41mXXXXXXXXXX\x1b[0m\r\n");
    uint32_t redb = cellbg(0, 0);
    term_resize(&T, 6, 20);
    int red = 0;
    for (int x = 0; x < T.g.cols; x++)
        if (cellbg(0, x) == redb)
            red++;
    CHECK(red == 10, "a coloured background run survives reflow");

    /* short lines must not multiply when the window narrows */
    term_destroy(&T);
    term_init(&T, 6, 40, 50);
    feed("one\r\ntwo\r\nthree\r\n");
    term_resize(&T, 6, 10);
    CHECK(logical_lines(lbuf) == 3 && !strcmp(lbuf[0], "one") && !strcmp(lbuf[2], "three"),
          "short lines re-wrap to the same three lines");
    CHECK(content_rows() == 3, "short lines do not consume extra rows when narrowing");

    /* scrollback reflows too, not only the viewport */
    term_destroy(&T);
    term_init(&T, 4, 20, 100);
    feed(LONG56 "\r\n");
    feed("second\r\n");
    CHECK(T.g.scroll_off > 0, "the long line pushed content into scrollback");
    term_resize(&T, 4, 10);
    CHECK(logical_lines(lbuf) == 2 && !strcmp(lbuf[0], LONG56) && !strcmp(lbuf[1], "second"),
          "scrollback lines reflow as well");

    /* the ALTERNATE screen is not re-wrapped: full-screen apps repaint themselves */
    term_destroy(&T);
    term_init(&T, 6, 10, 50);
    feed("\x1b[?1049h");
    CHECK(T.alt_screen == 1, "alternate screen active");
    feed("0123456789ABCDE");
    term_resize(&T, 6, 20);
    row_text(0, row);
    CHECK(!memcmp(row, "0123456789", 10), "alt screen row 0 keeps its 10 columns (no re-wrap)");
    row_text(1, row);
    CHECK(row[0] == 'A' && row[5] == ' ', "alt screen row 1 keeps the overflow in place");

    /* ... but the PARKED main screen does reflow, so it is correct when the app exits */
    term_destroy(&T);
    term_init(&T, 6, 20, 50);
    feed(LONG56 "\r\n");
    feed("\x1b[?1049h");
    feed("alt screen");
    term_resize(&T, 6, 10);
    feed("\x1b[?1049l");
    CHECK(T.alt_screen == 0, "left the alternate screen");
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], LONG56),
          "the parked main screen reflowed while the alt screen was active");
#undef LONG56
    feed("\x1b[?1049l");
    term_destroy(&T);
    term_init(&T, 10, 20, 100);

    /* --- OSC title --- */
    feed("\x1b]0;my title\x07");
    CHECK(!strcmp(T.title, "my title"), "OSC title set");

    /* --- DECTCEM --- */
    feed("\x1b[?25l");
    CHECK(T.show_cursor == 0, "hide cursor");
    feed("\x1b[?25h");
    CHECK(T.show_cursor == 1, "show cursor");

    /* --- mouse mode bits (?1000/?1002/?1006) --- */
    feed("\x1b[?1000;1006h");
    CHECK((T.mouse_mode & MOUSE_PRESS) && (T.mouse_mode & MOUSE_SGR) &&
          !(T.mouse_mode & MOUSE_DRAG), "mouse press+sgr set, drag off");
    feed("\x1b[?1002h");
    CHECK(T.mouse_mode & MOUSE_DRAG, "drag tracking set");
    feed("\x1b[?1000;1002;1006l");
    CHECK(T.mouse_mode == 0, "all mouse modes cleared");

    /* --- evdev BTN_* -> SGR button number (session 61: the local mapping had right and middle
       swapped, which made a middle click on the tab bar do nothing and a RIGHT click close a session.
       The kernel order is LEFT 0x110, RIGHT 0x111, MIDDLE 0x112; SGR/X10 want 0/1/2 in that name order.
       The literals are written out here as well as through the enum, so changing the enum without the
       real protocol order fails a check. --- */
    CHECK(mouse_sgr_button(EVDEV_BTN_LEFT) == 0, "BTN_LEFT -> SGR 0");
    CHECK(mouse_sgr_button(EVDEV_BTN_MIDDLE) == 1, "BTN_MIDDLE -> SGR 1");
    CHECK(mouse_sgr_button(EVDEV_BTN_RIGHT) == 2, "BTN_RIGHT -> SGR 2");
    CHECK(EVDEV_BTN_RIGHT == 0x111 && EVDEV_BTN_MIDDLE == 0x112, "BTN_RIGHT is 0x111, BTN_MIDDLE 0x112");
    CHECK(mouse_sgr_button(0x111) == 2 && mouse_sgr_button(0x112) == 1,
          "raw 0x111 is right, raw 0x112 is middle");
    CHECK(mouse_sgr_button(0x113) == 3 && mouse_sgr_button(0) == 3, "side/extra/unknown -> SGR 3");

    /* The wheel code sent to an application that DID ask for the mouse (s96). The wheel is an axis, not a
       button, so the number is a choice — and the old one (4 up / 5 down) is read by ncurses as button 1,
       which made the wheel do nothing in vim, htop, tmux and `nano --mouse`. Measured 2026-10-21
       (.scratch/s96/nanoprobe) on nano --mouse: ESC[<5;20;12M did nothing, ESC[<65;20;12M scrolled the text,
       and in the legacy X10 form 32+65 scrolled while 32+4 did not. */
    CHECK(mouse_wheel_button(1) == 64 && mouse_wheel_button(0) == 65,
          "the wheel is SGR 64 up / 65 down");
    CHECK(mouse_wheel_button(1) != 4 && mouse_wheel_button(0) != 5,
          "and never the old 4/5, which ncurses reads as button 1");

    /* --- DECCKM (ESC[?1h): the form the cursor keys go out in ---
       Measured 2026-10-21 (.scratch/s96/nanoprobe, real programs in a real pty): nano answers either
       form of the cursor keys, less answers ONLY the SS3 one — and less asks for it with ESC[?1h. So a
       terminal that ignores ?1h cannot be scrolled by the arrow keys in less at all. */
    feed("\x1b[?1h");
    CHECK(T.app_cursor == 1, "DECCKM set by ESC[?1h");
    CHECK(!strcmp(term_arrow_seq(&T, 'A'), "\x1bOA") && !strcmp(term_arrow_seq(&T, 'D'), "\x1bOD"),
          "DECCKM on: cursor keys are SS3");
    feed("\x1b[?1l");
    CHECK(T.app_cursor == 0, "DECCKM cleared by ESC[?1l");
    CHECK(!strcmp(term_arrow_seq(&T, 'A'), "\x1b[A") && !strcmp(term_arrow_seq(&T, 'C'), "\x1b[C"),
          "DECCKM off: cursor keys are CSI");
    CHECK(term_arrow_seq(&T, 'Z')[0] == 0, "an unknown cursor key gives an empty string, not garbage");
    /* DECCKM belongs to the screen: entering the alt screen parks it, leaving restores it. */
    feed("\x1b[?1h");
    feed("\x1b[?1049h");
    CHECK(T.app_cursor == 0, "the alt screen starts with DECCKM off, not the main screen's");
    feed("\x1b[?1h");
    feed("\x1b[?1049l");
    CHECK(T.app_cursor == 1, "and the main screen's DECCKM comes back");
    feed("\x1b[?1l");

    /* --- the wheel and a click as keystrokes (s96: nano/less scrolling) ---
       The state that matters is "alternate screen AND no mouse protocol": there the program owns its
       screen, has no terminal scrollback, and reads a mouse report as TYPED TEXT (measured: one report
       put "64;20;5M" into the user's file). So the wheel and a click have to become cursor keys. */
    char kseq[4096];
    term_destroy(&T);
    term_init(&T, 24, 80, 100);
    feed("prompt$ ");
    CHECK(term_wants_key_mouse(&T) == 0, "keymouse: the main screen keeps the wheel for its scrollback");
    CHECK(term_wheel_arrow_keys(&T, 3, kseq, sizeof kseq) == 0,
          "keymouse: nothing is synthesized outside the alternate screen");
    CHECK(term_click_arrow_keys(&T, 5, 5, kseq, sizeof kseq) == 0,
          "keymouse: nor is a click");

    feed("\x1b[?1049h");
    CHECK(T.alt_screen == 1 && term_wants_key_mouse(&T) == 1,
          "keymouse: alt screen + no mouse protocol belongs to the application");
    int kn = term_wheel_arrow_keys(&T, 3, kseq, sizeof kseq);
    CHECK(kn == 9 && !memcmp(kseq, "\x1b[A\x1b[A\x1b[A", 9), "keymouse: wheel up = three Up presses");
    kn = term_wheel_arrow_keys(&T, -2, kseq, sizeof kseq);
    CHECK(kn == 6 && !memcmp(kseq, "\x1b[B\x1b[B", 6), "keymouse: wheel down = two Down presses");
    CHECK(term_wheel_arrow_keys(&T, 0, kseq, sizeof kseq) == 0,
          "keymouse: a gesture that moves nothing sends nothing");
    kn = term_wheel_arrow_keys(&T, 1000000, kseq, sizeof kseq);
    CHECK(kn == 3 * TERM_ARROW_MAX, "keymouse: an absurd gesture is bounded, not unbounded");
    feed("\x1b[?1h");
    kn = term_wheel_arrow_keys(&T, 2, kseq, sizeof kseq);
    CHECK(kn == 6 && !memcmp(kseq, "\x1bOA\x1bOA", 6),
          "keymouse: while DECCKM is on the wheel sends the SS3 form (what less needs)");
    feed("\x1b[?1l");

    /* An application that DID ask for the mouse keeps getting reports, never keys. */
    feed("\x1b[?1000h");
    CHECK(term_wants_key_mouse(&T) == 0, "keymouse: ?1000 means the application wants the mouse");
    CHECK(term_wheel_arrow_keys(&T, 3, kseq, sizeof kseq) == 0 &&
          term_click_arrow_keys(&T, 5, 5, kseq, sizeof kseq) == 0,
          "keymouse: so neither the wheel nor a click is turned into keys");
    feed("\x1b[?1000l");
    feed("\x1b[?1002h");
    CHECK(term_wants_key_mouse(&T) == 0, "keymouse: ?1002 counts too");
    feed("\x1b[?1002l");
    feed("\x1b[?1006h");
    CHECK(term_wants_key_mouse(&T) == 1, "keymouse: ?1006 alone only picks the encoding");
    feed("\x1b[?1006l");

    /* The click: walk the application's own cursor to the clicked cell. The order (vertical first) and
       the end-of-line clamp are both measured behaviour of nano (.scratch/s96/clickprobe.c). */
    feed("\x1b[2J\x1b[H");
    feed("abcdefghij\r\nabc\r\n");
    feed("\x1b[H");                       /* cursor back to (0,0) */
    CHECK(T.cx == 0 && T.cy == 0, "keyclick: the cursor starts at home");
    kn = term_click_arrow_keys(&T, 3, 0, kseq, sizeof kseq);
    CHECK(kn == 9 && !memcmp(kseq, "\x1b[C\x1b[C\x1b[C", 9),
          "keyclick: three columns right is three Right presses, nothing else");
    kn = term_click_arrow_keys(&T, 40, 1, kseq, sizeof kseq);
    CHECK(kn == 12 && !memcmp(kseq, "\x1b[B\x1b[C\x1b[C\x1b[C", 12),
          "keyclick: vertical first, and a click past the end of a 3-column line lands at its end");
    kn = term_click_arrow_keys(&T, 4, 2, kseq, sizeof kseq);
    CHECK(kn == 6 && !memcmp(kseq, "\x1b[B\x1b[B", 6),
          "keyclick: an empty row is clicked at its start, not at column 4");
    CHECK(term_click_arrow_keys(&T, 0, 0, kseq, sizeof kseq) == 0,
          "keyclick: clicking where the cursor already is sends nothing");
    CHECK(term_click_arrow_keys(&T, -1, 0, kseq, sizeof kseq) == 0 &&
          term_click_arrow_keys(&T, 0, T.g.rows, kseq, sizeof kseq) == 0,
          "keyclick: a cell outside the grid sends nothing");
    feed("\x1b[?1h");
    kn = term_click_arrow_keys(&T, 2, 0, kseq, sizeof kseq);
    CHECK(kn == 6 && !memcmp(kseq, "\x1bOC\x1bOC", 6),
          "keyclick: the click follows DECCKM too");
    feed("\x1b[?1l");
    /* A small buffer must be respected to the byte, and the result stays terminated. */
    char small[8];
    kn = term_click_arrow_keys(&T, 70, 20, small, sizeof small);
    CHECK(kn == 6 && small[kn] == 0 && !memcmp(small, "\x1b[B\x1b[B", 6),
          "keyclick: a short buffer gets whole presses, never half an escape sequence");
    CHECK(term_click_arrow_keys(&T, 3, 0, small, 3) == 0, "keyclick: a buffer that cannot hold one press sends nothing");

    /* --- a C0 control byte INSIDE a control sequence: execute it, keep collecting ---
       DEC VT500 / xterm. The parser used to end the sequence at the control byte instead, which did two
       things at once: the mode the sequence was setting was never applied, AND the rest of the sequence was
       printed as text. Measured 2026-10-21 (.scratch/s97/hprobe2) before the fix: ESC [ ? 2004 <TAB> h left
       bracketed_paste at 0 and painted an `h`; ESC [ ? 1049 <TAB> h never entered the alternate screen and
       painted an `h`; ESC [ ? 1 <TAB> h left DECCKM off and painted an `h`. A stray `h` nobody typed is the
       user-visible symptom (report 2026-10-21), because `h` is what every mode-ENABLING sequence ends with. */
    term_destroy(&T);
    term_init(&T, 24, 80, 100);
    feed("\x1b[?2004\x09h");
    CHECK(T.bracketed_paste == 1, "C0 inside CSI (TAB): the sequence still applies its mode");
    CHECK(!grid_has_glyph(), "C0 inside CSI (TAB): nothing of the sequence is printed");
    CHECK(T.cx == 8, "C0 inside CSI (TAB): the TAB itself was executed");

    term_destroy(&T);
    term_init(&T, 24, 80, 100);
    feed("\x1b[?2004\x0ah");
    CHECK(T.bracketed_paste == 1 && T.cy == 1,
          "C0 inside CSI (LF): the LF moved the cursor AND the mode was still applied");
    CHECK(!grid_has_glyph(), "C0 inside CSI (LF): nothing of the sequence is printed");

    term_destroy(&T);
    term_init(&T, 24, 80, 100);
    feed("\x1b[?2004\x0dh");
    CHECK(T.bracketed_paste == 1 && T.cx == 0, "C0 inside CSI (CR): executed, and the mode applied");
    CHECK(!grid_has_glyph(), "C0 inside CSI (CR): nothing printed");

    term_destroy(&T);
    term_init(&T, 24, 80, 100);
    feed("\x1b[?1\x09h");
    CHECK(T.app_cursor == 1 && !grid_has_glyph(), "C0 inside CSI: DECCKM survives a TAB in the sequence");

    term_destroy(&T);
    term_init(&T, 24, 80, 100);
    feed("\x1b[?1049\x09h");
    CHECK(T.alt_screen == 1 && !grid_has_glyph(),
          "C0 inside CSI: the alternate screen is entered even with a TAB in the sequence");

    /* ESC is the exception: it CANCELS the sequence and starts a new escape (xterm does the same), so the
       mode is NOT applied -- but the abandoned bytes must still not be painted. */
    term_destroy(&T);
    term_init(&T, 24, 80, 100);
    feed("\x1b[?2004\x1bh");
    CHECK(T.bracketed_paste == 0, "ESC inside CSI cancels the sequence, so the mode is not set");
    CHECK(!grid_has_glyph(), "ESC inside CSI: the abandoned bytes are never printed");

    /* And the ordinary case a pty read boundary produces -- a sequence split across bursts with NO control
       byte in it -- must keep working. (A probe that hand-counted the length once fed the NUL terminator
       and reported this case as broken; it is not. Always strlen.) */
    term_destroy(&T);
    term_init(&T, 24, 80, 100);
    term_parse(&T, (const uint8_t *)"\x1b[?2004", 7);
    CHECK(T.bracketed_paste == 0, "a CSI sequence left incomplete at the end of a burst stays open");
    term_parse(&T, (const uint8_t *)"h", 1);
    CHECK(T.bracketed_paste == 1 && !grid_has_glyph(),
          "and the next burst finishes it: mode applied, nothing printed");
    feed("\x1b[?2004l");   /* leave the mode off, as the tests below this one expect to find it */
    CHECK(T.bracketed_paste == 0, "C0-inside-CSI checks leave bracketed paste off again");

    /* --- empty/omitted params --- */
    feed("\x1b[2J\x1b[H");
    feed("\x1b[H\x1b[2;H");
    CHECK(T.cx == 0 && T.cy == 1, "CSI 2;H defaults col to 1");

    /* --- alternate screen buffer (?1049): main content + scrollback preserved --- */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    feed("\x1b[2J\x1b[HMAIN");            /* main content on row 0 */
    feed("\x1b[4;3H");                    /* park main cursor at row4 col3 (0-based 3,2) */
    int so_before = T.g.scroll_off;
    CHECK(T.cx == 2 && T.cy == 3, "main cursor parked before alt");
    feed("\x1b[?1049h");                  /* enter alt (saves cursor) */
    CHECK(T.alt_screen == 1, "alt screen active");
    CHECK(T.g.scrollback_max == 0, "alt grid has no scrollback");
    CHECK(T.cx == 0 && T.cy == 0, "alt screen starts at home");
    row_text(0, row);
    CHECK(row[0] == ' ', "alt screen is cleared on entry");
    feed("ALT");
    CHECK(cellch(0, 0) == 'A' && cellch(0, 1) == 'L' && cellch(0, 2) == 'T', "alt content written");
    feed("\x1b[?1049l");                  /* leave alt (restores cursor + main) */
    CHECK(T.alt_screen == 0, "alt screen left");
    row_text(0, row);
    CHECK(!memcmp(row, "MAIN", 4), "main content restored after alt");
    CHECK(T.cx == 2 && T.cy == 3, "main cursor restored after alt");
    CHECK(T.g.scroll_off == so_before, "alt did not pollute main scrollback");

    /* --- alt screen ?47 (switch without saving cursor) --- */
    feed("\x1b[2J\x1b[H");
    feed("\x1b[?47h");
    CHECK(T.alt_screen == 1, "?47 enters alt");
    CHECK(T.cx == 0 && T.cy == 0, "?47 alt starts home");
    feed("\x1b[?47l");
    CHECK(T.alt_screen == 0, "?47 leaves alt");
    CHECK(T.cx == 0 && T.cy == 0, "?47 leaves cursor at 0,0 (no save)");

    /* --- ?1048 cursor save/restore only (no grid switch) --- */
    feed("\x1b[2J\x1b[H\x1b[6;7H");
    CHECK(T.cx == 6 && T.cy == 5, "cursor parked for 1048");
    feed("\x1b[?1048h");
    CHECK(T.alt_screen == 0, "?1048 does not switch grid");
    feed("\x1b[1;1H");
    CHECK(T.cx == 0 && T.cy == 0, "cursor moved after save");
    feed("\x1b[?1048l");
    CHECK(T.cx == 6 && T.cy == 5, "?1048l restores cursor");

    /* --- text selection (term_selected + term_selection_text) --- */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    feed("\x1b[2J\x1b[H");
    feed("hello world\r\nsecond line");   /* row0 "hello world", row1 "second line" */
    char sel[256];
    int n;

    /* Whole-cell selection is expressed with the end frac at 1000 (pointer at the right
       edge of the end cell); a frac of 0 means the pointer is at the cell's left edge. */

    /* single-row selection cols 0..4 on row 0 */
    T.sel_active = 1;
    T.sel_ax = 0; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 4; T.sel_by = 0; T.sel_bf = 1000;
    CHECK(term_selected(&T, 0, 0) && term_selected(&T, 4, 0), "single-row endpoints selected");
    CHECK(!term_selected(&T, 5, 0), "single-row past end not selected");
    CHECK(!term_selected(&T, 0, 1), "other row not selected");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 5 && !memcmp(sel, "hello", 5), "selection text 'hello'");

    /* the covered sub-range (what the highlight band spans) is exact */
    double a, b;
    CHECK(term_selection_cover(&T, 0, 0, &a, &b) && a == 0.0 && b == 1.0, "cover cell0 = [0,1]");
    CHECK(term_selection_cover(&T, 4, 0, &a, &b) && a == 4.0 && b == 5.0, "cover cell4 = [4,5]");
    CHECK(!term_selection_cover(&T, 5, 0, &a, &b), "cover cell5 = none");
    CHECK(!term_selection_cover(&T, 0, 1, &a, &b), "cover other row = none");

    /* multi-row selection: (6,0)..(5,1) -> "world" + "second" */
    T.sel_ax = 6; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 5; T.sel_by = 1; T.sel_bf = 1000;
    CHECK(term_selected(&T, 6, 0) && term_selected(&T, 10, 0), "row0 from anchor to end");
    CHECK(!term_selected(&T, 5, 0), "row0 before anchor not selected");
    CHECK(term_selected(&T, 0, 1) && term_selected(&T, 5, 1), "row1 up to current col");
    CHECK(!term_selected(&T, 6, 1), "row1 past current col not selected");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 12 && !memcmp(sel, "world\nsecond", 12), "multi-row selection text");
    /* middle rows are fully covered; the end row stops at the sub-cell edge */
    CHECK(term_selection_cover(&T, 19, 0, &a, &b) && a == 19.0 && b == 20.0, "row0 last cell covered");
    CHECK(term_selection_cover(&T, 5, 1, &a, &b) && a == 5.0 && b == 6.0, "row1 end cell covered");
    CHECK(!term_selection_cover(&T, 6, 1, &a, &b), "row1 past end not covered");

    /* normalized: anchor after current (4,0)..(0,0) selects cols 0..4 */
    T.sel_ax = 4; T.sel_ay = 0; T.sel_af = 1000;
    T.sel_bx = 0; T.sel_by = 0; T.sel_bf = 0;
    CHECK(term_selected(&T, 0, 0) && term_selected(&T, 4, 0) && !term_selected(&T, 5, 0),
          "anchor-after-current normalized");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 5 && !memcmp(sel, "hello", 5), "normalized selection text");

    /* trailing spaces trimmed: select a wide region on a short line */
    feed("\x1b[2J\x1b[H");
    feed("ab");
    T.sel_ax = 0; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 10; T.sel_by = 0; T.sel_bf = 1000;
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 2 && !memcmp(sel, "ab", 2), "trailing spaces trimmed");

    /* --- sub-cell (per-glyph) selection edges --- */
    feed("\x1b[2J\x1b[H");
    feed("hello world");   /* row0: h e l l o ... at cols 0..4 */

    /* span entirely inside col2 (anchor at its left edge, end 30% in): only col2 touched */
    T.sel_active = 1;
    T.sel_ax = 2; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 2; T.sel_by = 0; T.sel_bf = 300;
    CHECK(term_selected(&T, 2, 0), "sub-cell: col2 selected when span ends inside it");
    CHECK(!term_selected(&T, 3, 0), "sub-cell: col3 not selected (span ends before it)");
    CHECK(!term_selected(&T, 1, 0), "sub-cell: col1 not selected (span starts at col2)");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 1 && sel[0] == 'l', "sub-cell text = the single touched glyph 'l'");

    /* partial edges across three cells: the band is [2,3],[3,4],[4,4.5] */
    T.sel_ax = 2; T.sel_af = 0; T.sel_ay = 0;
    T.sel_bx = 4; T.sel_bf = 500; T.sel_by = 0;
    CHECK(term_selection_cover(&T, 2, 0, &a, &b) && a == 2.0 && b == 3.0, "partial: cell2 full");
    CHECK(term_selection_cover(&T, 3, 0, &a, &b) && a == 3.0 && b == 4.0, "partial: cell3 full");
    CHECK(term_selection_cover(&T, 4, 0, &a, &b) && a == 4.0 && b == 4.5, "partial: cell4 half");
    CHECK(!term_selection_cover(&T, 5, 0, &a, &b), "partial: cell5 untouched");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 3 && !memcmp(sel, "llo", 3), "partial text 'llo' (whole glyphs)");

    /* ending mid-col2 (bf=500) still copies the whole glyph col2, not col3 */
    T.sel_ax = 0; T.sel_af = 0; T.sel_ay = 0;
    T.sel_bx = 2; T.sel_bf = 500; T.sel_by = 0;
    CHECK(term_selected(&T, 2, 0) && !term_selected(&T, 3, 0),
          "mid-cell end includes its glyph, excludes the next");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 3 && !memcmp(sel, "hel", 3), "mid-cell end text 'hel'");

    /* sub-cell anchor 50% into col1: col1 is included, col0 excluded, through col4 */
    T.sel_ax = 1; T.sel_af = 500; T.sel_ay = 0;
    T.sel_bx = 4; T.sel_bf = 1000; T.sel_by = 0;
    CHECK(term_selected(&T, 1, 0) && term_selected(&T, 4, 0), "sub-cell anchor includes its cell");
    CHECK(!term_selected(&T, 0, 0), "sub-cell anchor excludes col0");
    CHECK(term_selection_cover(&T, 1, 0, &a, &b) && a == 1.5 && b == 2.0, "anchor cell band [1.5,2]");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 4 && !memcmp(sel, "ello", 4), "sub-cell anchor text 'ello'");

    /* a click with no movement selects the whole cell under the pointer */
    T.sel_ax = 3; T.sel_ay = 0; T.sel_af = 400;
    T.sel_bx = 3; T.sel_by = 0; T.sel_bf = 400;
    CHECK(term_selection_cover(&T, 3, 0, &a, &b) && a == 3.0 && b == 4.0, "click covers whole cell");
    CHECK(!term_selection_cover(&T, 2, 0, &a, &b) && !term_selection_cover(&T, 4, 0, &a, &b),
          "click covers only its cell");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 1 && sel[0] == 'l', "click copies one glyph");

    /* sel_band_pixels: ABSOLUTE pixel band (renderer must not add the cell origin again).
       cw=10, span 2.3..6.7 on row 0. */
    SelSpan bs = { 0, 2, 300, 0, 6, 700 };
    float bx, bw;
    sel_band_pixels(&bs, 20, 2, 0, 10.0f, &bx, &bw);   /* cell2 covered [2.3,3] */
    CHECK(bx == 23.0f && bw == 7.0f, "band cell2 absolute x=23 w=7");
    sel_band_pixels(&bs, 20, 3, 0, 10.0f, &bx, &bw);   /* full [3,4] */
    CHECK(bx == 30.0f && bw == 10.0f, "band cell3 full x=30 w=10");
    sel_band_pixels(&bs, 20, 6, 0, 10.0f, &bx, &bw);   /* [6,6.7] */
    CHECK(bx == 60.0f && bw == 7.0f, "band cell6 absolute x=60 w=7");
    sel_band_pixels(&bs, 20, 7, 0, 10.0f, &bx, &bw);   /* past end */
    CHECK(bx == 0.0f && bw == 0.0f, "band cell7 none");
    sel_band_pixels(&bs, 20, 1, 0, 10.0f, &bx, &bw);   /* before start */
    CHECK(bx == 0.0f && bw == 0.0f, "band cell1 none");

    /* term_selection_span normalizes by (row,col,frac) */
    T.sel_ax = 4; T.sel_af = 200; T.sel_ay = 1;
    T.sel_bx = 1; T.sel_bf = 700; T.sel_by = 0;
    SelSpan s;
    term_selection_span(&T, &s);
    CHECK(s.r0 == 0 && s.c0 == 1 && s.f0 == 700 && s.r1 == 1 && s.c1 == 4 && s.f1 == 200,
          "span normalized to reading order");

    /* clear */
    term_select_clear(&T);
    CHECK(T.sel_active == 0, "select_clear clears active");
    CHECK(!term_selected(&T, 0, 0), "no cell selected after clear");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 0 && sel[0] == 0, "empty selection text after clear");

    /* --- per-glyph (cell-granular) endpoints — ------------------------------------------------
       main.c's sel_set_endpoints() only ever produces frac 0 (left edge of the endpoint that comes
       first in reading order) and frac 1000 (right edge of the later one), so a range can never end
       inside a glyph. These checks pin what that means for the user: whole cells only, and the cell
       under the pointer stays selected whichever way the drag goes. */
    T.sel_active = 1;
    T.sel_ax = 1; T.sel_ay = 0; T.sel_af = 0;      /* forward drag: anchor col1 .. pointer col3 */
    T.sel_bx = 3; T.sel_by = 0; T.sel_bf = 1000;
    CHECK(term_selected(&T, 1, 0) && term_selected(&T, 2, 0) && term_selected(&T, 3, 0),
          "per-glyph: every cell between the snapped edges is selected");
    CHECK(!term_selected(&T, 0, 0) && !term_selected(&T, 4, 0),
          "per-glyph: no half glyph at either end");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 3 && !memcmp(sel, "ell", 3), "per-glyph text 'ell'");
    SelSpan gs;
    term_selection_span(&T, &gs);
    float gx, gw;
    sel_band_pixels(&gs, 20, 3, 0, 10.0f, &gx, &gw);
    CHECK(gx == 30.0f && gw == 10.0f, "per-glyph: the end cell's band is the whole cell");
    sel_band_pixels(&gs, 20, 4, 0, 10.0f, &gx, &gw);
    CHECK(gx == 0.0f && gw == 0.0f, "per-glyph: the cell after the end gets no band");
    /* backwards drag: the two fracs swap (anchor right edge 1000, end left edge 0). Without the swap
       term_selection_span's reordering would cut the anchor cell out of a leftward selection. */
    T.sel_ax = 3; T.sel_ay = 0; T.sel_af = 1000;
    T.sel_bx = 1; T.sel_by = 0; T.sel_bf = 0;
    CHECK(term_selected(&T, 1, 0) && term_selected(&T, 3, 0) && !term_selected(&T, 0, 0),
          "per-glyph: dragging left keeps the anchor cell selected");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 3 && !memcmp(sel, "ell", 3), "per-glyph: a leftward drag copies the same 3 glyphs");
    /* press without a drag: exactly one whole cell */
    T.sel_ax = 2; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 2; T.sel_by = 0; T.sel_bf = 1000;
    CHECK(term_selected(&T, 2, 0) && !term_selected(&T, 1, 0) && !term_selected(&T, 3, 0),
          "per-glyph: a press with no drag selects exactly one cell");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 1 && sel[0] == 'l', "per-glyph: a press with no drag copies one glyph");

    /* --- no highlight over empty space — ------------------------------------------------------
       term_cell_highlightable() decides whether the band is PAINTED over a covered cell; the copied
       text is deliberately unaffected (blank cells are still spaces, trailing ones trimmed). */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    feed("\x1b[2J\x1b[H");
    feed("ab cd\r\ne");                     /* row0: a b ' ' c d then blanks; row1: e then blanks */
    CHECK(term_cell_highlightable(&T, 0, 0) && term_cell_highlightable(&T, 4, 0),
          "empty: cells holding a glyph are highlighted");
    CHECK(term_cell_highlightable(&T, 2, 0), "empty: a real space IS highlighted");
    CHECK(!term_cell_highlightable(&T, 5, 0), "empty: a never-written cell gets no band");
    CHECK(!term_cell_highlightable(&T, 1, 1), "empty: past the end of the next line = no band");
    CHECK(!term_cell_highlightable(&T, -1, 0) && !term_cell_highlightable(&T, 20, 0) &&
          !term_cell_highlightable(&T, 0, 10), "empty: out-of-range cells are never highlighted");
    feed("\x1b[H\x1b[J");
    feed("\xe4\xb8\xadX");                  /* wide CJK at col0, continuation col1, 'X' col2 */
    CHECK(cellch(0, 0) == 0x4E2D && (cellattr(0, 1) & ATTR_WIDE), "wide char reserves its pair");
    CHECK(term_cell_highlightable(&T, 1, 0),
          "empty: a wide glyph's continuation cell keeps its band (else half the glyph loses it)");
    T.sel_active = 1;
    T.sel_ax = 0; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 19; T.sel_by = 0; T.sel_bf = 1000;   /* drag far past the end of the line */
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 4 && !memcmp(sel, "\xe4\xb8\xadX", 4), "empty: the copied text stops at the last glyph");

    /* --- emoji grapheme clusters + wide emoji (color emoji support) --- */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);

    CHECK(cp_is_wide(0x4E2D) && cp_is_wide(0x1F600) && cp_is_wide(0x1F468) &&
          cp_is_wide(0x1F1E6) && cp_is_wide(0xFF21),
          "cp_is_wide: CJK, emoji, regional indicator, fullwidth");
    CHECK(!cp_is_wide('A') && !cp_is_wide(0x20AC) && !cp_is_wide(0x2764) &&
          !cp_is_wide(0x1F3F3) && !cp_is_wide(0xFE0F),
          "cp_is_wide: text, text-presentation symbols and selectors stay narrow");
    CHECK(cp_is_emoji(0x1F600) && cp_is_emoji(0x2764) && cp_is_emoji(0x1F468), "cp_is_emoji");
    CHECK(!cp_is_emoji('A') && !cp_is_emoji(0x20AC), "plain text is not an emoji base");
    CHECK(cp_is_cluster_extend(0x200D) && cp_is_cluster_extend(0xFE0F) &&
          cp_is_cluster_extend(0x1F3FB) && cp_is_cluster_extend(0x20E3) &&
          cp_is_cluster_extend(0xE0061), "cp_is_cluster_extend: ZWJ/VS/skin tone/keycap/tag");
    CHECK(!cp_is_cluster_extend(0x1F600) && !cp_is_cluster_extend('A'), "a base is not an extend");

    /* cluster_is_wide: the MODIFIER decides the width, not just the base codepoint */
    {
        const uint32_t h1[] = { 0x2764 };
        const uint32_t h2[] = { 0x2764, 0xFE0F };
        const uint32_t k1[] = { '1', 0x20E3 };
        const uint32_t k2[] = { '1', 0xFE0F, 0x20E3 };
        const uint32_t t1[] = { 'a', 0xFE0F };
        const uint32_t r1[] = { 0x1F1FA, 0x1F1F8 };
        const uint32_t e1[] = { 0x1F600 };
        CHECK(!cluster_is_wide(h1, 1) && cluster_is_wide(h2, 2), "VS16 widens a text heart");
        CHECK(cluster_is_wide(k1, 2) && cluster_is_wide(k2, 3), "keycaps are wide");
        CHECK(!cluster_is_wide(t1, 2), "a non-emoji base + VS16 stays narrow");
        CHECK(cluster_is_wide(r1, 2) && cluster_is_wide(e1, 1), "RI pair / Emoji_Presentation base");
        CHECK(!cluster_is_wide(NULL, 0), "empty cluster is narrow");
    }

    /* thumbs up + skin tone: ONE anchor cell, one continuation cell, cursor advances 2 */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F44D\U0001F3FD");
    CHECK(cellch(0, 0) == 0x1F44D, "thumbs-up anchor cell");
    CHECK(cellnextra(0, 0) == 1 && cellextra(0, 0, 0) == 0x1F3FD, "skin tone folds into the anchor");
    CHECK(cellattr(0, 1) & ATTR_WIDE, "wide emoji reserves a second cell");
    CHECK(cellch(0, 1) == 0, "continuation cell holds no codepoint");
    CHECK(T.cx == 2, "cursor advances 2 for a wide emoji cluster");

    /* ZWJ family: 7 codepoints -> one anchor cell with 6 continuations, 2 cells total */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466X");
    CHECK(cellch(0, 0) == 0x1F468, "family anchor is the first codepoint");
    CHECK(cellnextra(0, 0) == 6, "family cluster keeps 6 continuation codepoints");
    CHECK(cellextra(0, 0, 0) == 0x200D && cellextra(0, 0, 1) == 0x1F469 &&
          cellextra(0, 0, 5) == 0x1F466, "family continuation order");
    CHECK(cellch(0, 2) == 'X', "family occupies exactly two cells");

    /* rainbow flag: base + VS16 + ZWJ + emoji. U+1F3F3 is text-presentation on its own, but the
       VS16 inside the cluster switches it to emoji presentation, so the cluster is WIDE. */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F3F3\uFE0F\u200D\U0001F308Y");
    CHECK(cellch(0, 0) == 0x1F3F3 && cellnextra(0, 0) == 3, "rainbow-flag cluster");
    CHECK(cellextra(0, 0, 0) == 0xFE0F && cellextra(0, 0, 1) == 0x200D &&
          cellextra(0, 0, 2) == 0x1F308, "flag continuation order");
    CHECK(cellattr(0, 1) & ATTR_WIDE, "VS16 widens the flag cluster");
    CHECK(cellch(0, 2) == 'Y', "a VS16 flag takes two cells");

    /* keycap: digit + VS16 + combining enclosing keycap -> emoji presentation, 2 cells */
    feed("\x1b[2J\x1b[H");
    feed("1\uFE0F\u20E3Z");
    CHECK(cellch(0, 0) == '1' && cellnextra(0, 0) == 2, "keycap folds onto a text base");
    CHECK(cellattr(0, 1) & ATTR_WIDE, "keycap is emoji presentation -> wide");
    CHECK(cellch(0, 1) == 0 && cellch(0, 2) == 'Z', "keycap takes two cells");

    /* regional indicator PAIR = ONE flag cluster. The second RI folds into the first so the whole
       pair reaches HarfBuzz: the color font keys its flag glyph on the pair (one RI alone shapes
       to the RI LETTER glyph), and the pair must reserve 2 cells, not 4. */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F1FA\U0001F1F8Q");
    CHECK(cellch(0, 0) == 0x1F1FA, "flag anchor is the first regional indicator");
    CHECK(cellnextra(0, 0) == 1 && cellextra(0, 0, 0) == 0x1F1F8, "second RI folds into the anchor");
    CHECK(cellattr(0, 1) & ATTR_WIDE, "a flag reserves two cells");
    CHECK(cellch(0, 2) == 'Q', "the RI pair consumed exactly two cells");
    CHECK(T.cx == 3, "cursor after flag + Q");

    /* a THIRD regional indicator starts a new cluster (Unicode pairs RIs up) */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F1FA\U0001F1F8\U0001F1EF");
    CHECK(cellch(0, 2) == 0x1F1EF && cellnextra(0, 2) == 0, "third RI starts its own cluster");

    /* U+2764 alone is a text dingbat (1 cell); U+2764 U+FE0F is the red emoji (2 cells). The
       anchor is written narrow and UPGRADED to a wide pair when the VS16 arrives. */
    feed("\x1b[2J\x1b[H");
    feed("\u2764\uFE0FW");
    CHECK(cellch(0, 0) == 0x2764 && cellnextra(0, 0) == 1, "heart + VS16 cluster");
    CHECK((cellattr(0, 1) & ATTR_WIDE) && cellch(0, 1) == 0, "VS16 widens the anchor in place");
    CHECK(cellch(0, 2) == 'W', "text after a widened emoji lands two cells on");
    CHECK(T.cx == 3, "cursor steps over the continuation cell it just created");

    /* the upgrade must NOT happen for a base that is not emoji-capable */
    feed("\x1b[2J\x1b[H");
    feed("a\uFE0Fb");
    CHECK(cellch(0, 0) == 'a' && cellnextra(0, 0) == 1, "VS16 still folds onto a text base");
    CHECK(!(cellattr(0, 1) & ATTR_WIDE) && cellch(0, 1) == 'b', "a text base stays one cell");

    /* at the right margin there is no second cell to reserve: the emoji keeps its single cell */
    feed("\x1b[2J\x1b[H\x1b[1;20H");
    feed("\u2764\uFE0F");
    CHECK(cellch(0, 19) == 0x2764 && cellnextra(0, 19) == 1, "heart at the last column");
    CHECK(!(cellattr(0, 19) & ATTR_WIDE), "no room to widen at the right margin");

    /* more continuations than CLUSTER_MAX: the surplus starts its own cell */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F44D");
    for (int i = 0; i < CLUSTER_MAX + 2; i++)
        feed("\U0001F3FB");
    CHECK(cellnextra(0, 0) == CLUSTER_MAX, "cluster capped at CLUSTER_MAX");
    CHECK(cellch(0, 2) == 0x1F3FB, "overflowing modifier gets its own cell (after the wide pair)");

    /* the cluster anchor is dropped by a cursor jump or a newline (cl_cy is a viewport coord) */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F600");                       /* wide emoji at col 0 */
    feed("\x1b[1;3H");                        /* CUP to row 1, col 3 */
    feed("\u200D");
    CHECK(cellch(0, 2) == 0x200D, "ZWJ after CUP gets its own cell");
    feed("\x1b[2J\x1b[H");
    feed("\U0001F600\r\n\u200D");
    CHECK(cellch(1, 0) == 0x200D, "ZWJ after CRLF gets its own cell");
    CHECK(cellnextra(0, 0) == 0, "newline leaves no cluster anchor");

    /* erase clears the cluster too (a stale nextra would resurrect a deleted emoji) */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F44D\U0001F3FD");
    feed("\x1b[1;1H\x1b[2X");
    CHECK(cellch(0, 0) == 0 && cellnextra(0, 0) == 0, "erase chars clears the cluster");

    /* selection copies the WHOLE cluster, and a wide continuation cell adds no stray space */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F44D\U0001F3FDx");
    T.sel_active = 1;
    T.sel_ax = 0; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 19; T.sel_by = 0; T.sel_bf = 1000;
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 9 && !memcmp(sel, "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBDx", 9),
          "selection copies the whole emoji cluster");

    feed("\x1b[2J\x1b[H");
    feed("\xE4\xB8\xADX");                     /* U+4E2D then X */
    T.sel_ax = 0; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 19; T.sel_by = 0; T.sel_bf = 1000;
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 4 && !memcmp(sel, "\xE4\xB8\xADX", 4), "wide continuation cell adds no space");

    /* a cluster widened by VS16 copies as its full sequence, continuation cell included */
    feed("\x1b[2J\x1b[H");
    feed("\u2764\uFE0Fx");                     /* U+2764 U+FE0F then x */
    T.sel_ax = 0; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 19; T.sel_by = 0; T.sel_bf = 1000;
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 7 && !memcmp(sel, "\xE2\x9D\xA4\xEF\xB8\x8Fx", 7),
          "selection copies a VS16-widened cluster");

    /* --- CSI 3 J: erase saved lines (scrollback), gated by term_set_clear_scrollback --- */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    char ln[32];
    for (int i = 0; i < 30; i++) {
        int m = snprintf(ln, sizeof(ln), "L%02d\r\n", i);
        term_parse(&T, (const uint8_t *)ln, (size_t)m);
    }
    CHECK(T.g.live_off == 21 && T.g.scroll_off == 21, "30 lines in a 10-row grid -> live_off 21");
    uint32_t topline = cellch(0, 0), lastline = cellch(8, 0);
    CHECK(topline == 'L' && cellch(0, 1) == '2', "viewport top is line 20");
    CHECK(lastline == 'L' && cellch(9, 0) == 0, "the trailing newline left a blank bottom row");
    feed("\x1b[3J");
    CHECK(T.g.scroll_off == 0 && T.g.live_off == 0, "ED 3 resets both anchors");
    CHECK(cellch(0, 0) == topline && cellch(8, 0) == lastline && cellch(9, 0) == 0,
          "ED 3 leaves the viewport alone");
    CHECK(T.g.row[T.g.rows][0].ch == 0, "the slots below the viewport are blanked");
    {
        int bad = 0;
        for (int s1 = 0; s1 < T.g.rcap; s1++) {
            if (!T.g.row[s1])
                bad++;
            for (int s2 = s1 + 1; s2 < T.g.rcap; s2++)
                if (T.g.row[s1] == T.g.row[s2])
                    bad++;
        }
        CHECK(bad == 0, "ED 3 keeps row[] a valid permutation");
    }
    /* the history is really gone: nothing is retained above the viewport any more */
    for (int i = 0; i < 5; i++) {
        int m = snprintf(ln, sizeof(ln), "M%02d\r\n", i);
        term_parse(&T, (const uint8_t *)ln, (size_t)m);
    }
    CHECK(T.g.live_off == 5, "scrollback rebuilds from zero after ED 3");
    /* the setting off = keep the history */
    term_set_clear_scrollback(0);
    feed("\x1b[3J");
    CHECK(T.g.live_off == 5 && T.g.scroll_off == 5, "clear_scrollback=0 keeps the history");
    term_set_clear_scrollback(1);
    feed("\x1b[3J");
    CHECK(T.g.live_off == 0 && T.g.scroll_off == 0, "re-enabled ED 3 clears again");
    /* `clear` sends ESC[2J then ESC[3J: screen AND history go away */
    feed("\x1b[2J\x1b[3J");
    CHECK(cellch(0, 0) == 0 && T.g.live_off == 0, "2J+3J (clear) empties screen and history");

    /* --- bracketed paste mode (DEC ?2004) + the bytes a paste is sent as --- */
    CHECK(T.bracketed_paste == 0, "bracketed paste off by default");
    feed("\x1b[?2004h");
    CHECK(T.bracketed_paste == 1, "?2004h enables bracketed paste");
    feed("\x1b[?2004l");
    CHECK(T.bracketed_paste == 0, "?2004l disables it again");
    /* the mode must not disturb the neighbours it sits next to in csi_dispatch */
    feed("\x1b[?2004;1000h");
    CHECK(T.bracketed_paste == 1 && (T.mouse_mode & MOUSE_PRESS), "?2004;1000h sets both");
    feed("\x1b[?2004;1000l");
    CHECK(T.bracketed_paste == 0 && !(T.mouse_mode & MOUSE_PRESS), "?2004;1000l clears both");
    /* ESC c rebuilds the Term: an application that has not re-enabled the mode gets raw paste */
    feed("\x1b[?2004h");
    feed("\x1b" "c");   /* the split string keeps the hex escape from swallowing the 'c' */
    CHECK(T.bracketed_paste == 0, "RIS resets the mode to off");
    {
        char pb[64];
        const char *txt = "echo \"foo\"\r\necho \"bar\"\r\n";
        size_t tlen = strlen(txt);
        int wlen = paste_wrap(0, txt, tlen, pb, sizeof(pb));
        CHECK(wlen == (int)tlen && !memcmp(pb, txt, tlen), "paste_wrap off = raw bytes");
        wlen = paste_wrap(1, txt, tlen, pb, sizeof(pb));
        CHECK(wlen == (int)tlen + 12, "paste_wrap on adds the two 6-byte delimiters");
        CHECK(!memcmp(pb, "\x1b[200~", 6), "paste starts with ESC[200~");
        CHECK(!memcmp(pb + 6, txt, tlen), "the whole block stays inside the brackets");
        CHECK(!memcmp(pb + 6 + tlen, "\x1b[201~", 6), "paste ends with ESC[201~");
        /* exact fit is accepted, one byte short is refused (never a truncated paste) */
        CHECK(paste_wrap(1, txt, tlen, pb, tlen + 12) == (int)tlen + 12, "exact fit ok");
        CHECK(paste_wrap(1, txt, tlen, pb, tlen + 11) == -1, "too small = -1");
        CHECK(paste_wrap(0, txt, tlen, pb, tlen) == (int)tlen, "raw paste needs no headroom");
        CHECK(paste_wrap(1, "", 0, pb, sizeof(pb)) == 12 &&
              !memcmp(pb, "\x1b[200~\x1b[201~", 12), "empty paste still gets brackets");
        CHECK(paste_wrap(1, "x", 1, pb, 11) == -1, "brackets alone do not fit in 11 bytes");
    }

    /* --- clipboard bytes a source serves WRONG (reported by the user as "a stray h when I paste",
       with the source being Firefox). Measured in .scratch/s98/nulprobe: the UTF-16LE bytes for
       "http://127.0.0.1:8080" written verbatim into a bracketed paste leave bash's line holding a
       single `h` (readline's bracketed reader stops at the first NUL) and zsh's line holding
       `h^@t^@t^@p^@...` (zle paints every NUL as ^@) — the user's two reports, reproduced exactly.
       A NUL byte must never reach the pty. --- */
    {
        char u16[64], out[256];
        size_t u16len = 0;
        const char *url = "http://127.0.0.1:8080";
        for (const char *p = url; *p; p++) { u16[u16len++] = *p; u16[u16len++] = 0; }

        int rn = paste_repair(u16, u16len, out, sizeof(out));
        CHECK(rn == (int)strlen(url) && !memcmp(out, url, strlen(url)),
              "paste_repair: UTF-16LE with no BOM decodes to the text the user meant to paste");
        CHECK(rn > 0 && memchr(out, 0, (size_t)rn) == NULL,
              "paste_repair: no NUL survives on its way to the pty");

        /* the same block with a BOM in front */
        char bom[80];
        size_t blen = 0;
        bom[blen++] = (char)0xFF; bom[blen++] = (char)0xFE;
        memcpy(bom + blen, u16, u16len); blen += u16len;
        n = paste_repair(bom, blen, out, sizeof(out));
        CHECK(n == (int)strlen(url) && !memcmp(out, url, strlen(url)),
              "paste_repair: a UTF-16LE BOM is consumed, not pasted as a character");

        /* big-endian, with and without the BOM */
        char be16[64];
        size_t belen = 0;
        for (const char *p = url; *p; p++) { be16[belen++] = 0; be16[belen++] = *p; }
        n = paste_repair(be16, belen, out, sizeof(out));
        CHECK(n == (int)strlen(url) && !memcmp(out, url, strlen(url)),
              "paste_repair: UTF-16BE (NULs at even offsets) decodes too");
        char bebom[80];
        size_t bblen = 0;
        bebom[bblen++] = (char)0xFE; bebom[bblen++] = (char)0xFF;
        memcpy(bebom + bblen, be16, belen); bblen += belen;
        n = paste_repair(bebom, bblen, out, sizeof(out));
        CHECK(n == (int)strlen(url) && !memcmp(out, url, strlen(url)),
              "paste_repair: a UTF-16BE BOM is consumed too");

        /* non-ASCII and astral: the surrogate pair for U+1F600 must come out as one 4-byte char.
           Carried with a BOM, which is what makes a short block unambiguous. */
        char smile[8] = { (char)0xFF, (char)0xFE, (char)0x3D, (char)0xD8, (char)0x00, (char)0xDE };
        n = paste_repair(smile, 6, out, sizeof(out));
        CHECK(n == 4 && (unsigned char)out[0] == 0xF0 && (unsigned char)out[1] == 0x9F &&
              (unsigned char)out[2] == 0x98 && (unsigned char)out[3] == 0x80,
              "paste_repair: a UTF-16 surrogate pair becomes one UTF-8 code point");
        char lone[6] = { (char)0xFF, (char)0xFE, (char)0x3D, (char)0xD8, 'a', 0 };  /* high, then no low */
        n = paste_repair(lone, 6, out, sizeof(out));
        CHECK(n == 4 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBF &&
              (unsigned char)out[2] == 0xBD && out[3] == 'a',
              "paste_repair: a lone surrogate becomes one replacement char, and the next unit survives");

        /* a block too sparse to be sure about is NEVER decoded — it is only NUL-stripped */
        char sparse[6] = { (char)0x3D, (char)0xD8, (char)0x00, (char)0xDE, 'x', 0 };
        n = paste_repair(sparse, 6, out, sizeof(out));
        CHECK(n == 4 && memchr(out, 0, (size_t)n) == NULL,
              "paste_repair: sparse NULs are stripped, never guessed at as UTF-16");

        /* the ordinary case must be untouched, byte for byte */
        const char *clean = "ls -la\r\n";
        n = paste_repair(clean, strlen(clean), out, sizeof(out));
        CHECK(n == (int)strlen(clean) && !memcmp(out, clean, strlen(clean)),
              "paste_repair: a block with no NUL is copied through unchanged");

        /* a stray NUL in ordinary text is stripped, NOT decoded as UTF-16 */
        const char *stray = "echo hello there and keep going";
        char polluted[64];
        size_t plen = strlen(stray);
        memcpy(polluted, stray, plen);
        polluted[3] = 0;                       /* one NUL at an even offset */
        n = paste_repair(polluted, plen, out, sizeof(out));
        CHECK(n == (int)plen - 1 && memchr(out, 0, (size_t)n) == NULL &&
              !memcmp(out, "ech hello there and keep going", plen - 1),
              "paste_repair: one stray NUL is stripped, not read as UTF-16");

        /* short output buffer is reported, never a truncated paste */
        CHECK(paste_repair(u16, u16len, out, 4) == -1, "paste_repair: too small = -1");
        CHECK(paste_repair(clean, strlen(clean), out, strlen(clean) - 1) == -1,
              "paste_repair: even a copy needs room for the whole block");
        /* the caller's sizing rule: len*3/2 + 8 always suffices */
        CHECK(paste_repair(u16, u16len, out, u16len * 3 / 2 + 8) == (int)strlen(url),
              "paste_repair: the caller's cap formula is enough for a full decode");
    }

    /* --- which mime type to ask a clipboard source for. The old rule kept the LAST advertised type,
       so a source enumerating text/plain before text/html got HTML asked for. --- */
    CHECK(clipboard_mime_rank("text/plain;charset=utf-8") > clipboard_mime_rank("text/plain"),
          "mime rank: an explicit utf-8 beats a bare text/plain");
    CHECK(clipboard_mime_rank("text/plain") > clipboard_mime_rank("text/html"),
          "mime rank: text/plain beats text/html, whatever order the source advertised them in");
    CHECK(clipboard_mime_rank("text/plain;charset=utf-8") > clipboard_mime_rank("UTF8_STRING"),
          "mime rank: utf-8 explicit beats the bare UTF8_STRING atom");
    CHECK(clipboard_mime_rank("text/html") == 1 && clipboard_mime_rank("text/rtf") == 1,
          "mime rank: any other text/* is still accepted as a last resort");
    CHECK(clipboard_mime_rank("image/png") == 0 && clipboard_mime_rank("application/json") == 0 &&
          clipboard_mime_rank(NULL) == 0, "mime rank: non-text is not text");
    CHECK(clipboard_mime_rank("text/plain;charset=iso-8859-1") == 2 &&
          clipboard_mime_rank("text/plain;dnd=1") == 2, "mime rank: text/plain variants rank below the exact names");

    /* term_init frees the grid it rebuilds (the RIS leak fix), but it must not free a grid that never
       existed: on a stack Term holding garbage that was a free() of a bogus pointer. */
    {
        Term lt;
        dirty_stack();
        term_init(&lt, 6, 12, 20);
        CHECK(lt.g.valid == GRID_VALID && lt.g.rows == 6 && lt.g.cols == 12, "term_init on a dirty stack Term");
        term_parse(&lt, (const uint8_t *)"ab\r\ncd", 5);
        CHECK(grid_vprow(&lt.g, 1)[0].ch == 'c', "the fresh term works");
        term_init(&lt, 6, 12, 20);          /* RIS path: the old buffers ARE freed here */
        CHECK(lt.g.valid == GRID_VALID && lt.g.row[0] && lt.g.row[0][0].ch == 0, "re-init clears and stays valid");
        term_destroy(&lt);
        CHECK(lt.g.valid == 0, "term_destroy clears the valid flag");
        term_init(&lt, 4, 8, 4);        /* destroy left it zeroed: re-init must not free anything */
        CHECK(lt.g.valid == GRID_VALID && lt.g.rows == 4, "init after destroy is safe");
        term_destroy(&lt);
    }

    /* --- config: the app_id is configurable ---------------------------------------------------
       config.c is linked into this binary, so the file format itself is testable here. The expected
       default is DEF_APP_ID (kai-term.h), the same macro config_default() and main.c use. */
    {
        Config c;
        config_default(&c);
        CHECK(!strcmp(c.app_id, DEF_APP_ID) && !strcmp(c.app_id, "Kai-Term"),
              "config: app_id defaults to Kai-Term");
        const char *path = "/tmp/kterm_test_config.conf";
        FILE *f = fopen(path, "w");
        CHECK(f != NULL, "config: temp config file writable");
        if (f) {
            fputs("app_id = com.example.myterm   # trailing comment\n", f);
            fclose(f);
            config_load(&c, path);
            CHECK(!strcmp(c.app_id, "com.example.myterm"), "config: app_id read from the file");
            f = fopen(path, "w");
            fputs("app_id =\n", f);
            fclose(f);
            config_load(&c, path);
            CHECK(!strcmp(c.app_id, DEF_APP_ID) && !strcmp(c.app_id, "Kai-Term"),
                  "config: an empty app_id falls back to Kai-Term");
            f = fopen(path, "w");
            fputs("app_id = io.github.kaida.kterm\n", f);
            fclose(f);
            config_load(&c, path);
            CHECK(!strcmp(c.app_id, "io.github.kaida.kterm"), "config: dotted app_id survives intact");
            remove(path);
        }
    }

    /* --- config: the copy_on_release toggle ---------------------------------------------------
       Default OFF: releasing a selection drag must not touch the clipboard (Ctrl+Shift+C does).
       One cfg_bool() parser now backs both this and clear_scrollback, so both are checked. */
    {
        Config c;
        const char *path = "/tmp/kterm_test_config2.conf";
        config_default(&c);
        CHECK(c.copy_on_release == 0, "config: copy_on_release defaults to off");
        CHECK(c.clear_scrollback == 1, "config: clear_scrollback defaults to on");
        FILE *f = fopen(path, "w");
        CHECK(f != NULL, "config: temp config file writable (toggles)");
        if (f) {
            fclose(f);
            f = fopen(path, "w");
            fputs("copy_on_release = 1\nclear_scrollback = 0\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.copy_on_release == 1, "config: copy_on_release = 1 turns it on");
            CHECK(c.clear_scrollback == 0, "config: clear_scrollback = 0 turns it off");
            f = fopen(path, "w");
            fputs("copy_on_release = FALSE\nclear_scrollback = NO\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.copy_on_release == 0, "config: copy_on_release = FALSE is off (case-insensitive)");
            CHECK(c.clear_scrollback == 0, "config: clear_scrollback = NO is off (case-insensitive)");
            f = fopen(path, "w");
            fputs("copy_on_release = off\nclear_scrollback = true\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.copy_on_release == 0, "config: copy_on_release = off is off");
            CHECK(c.clear_scrollback == 1, "config: clear_scrollback = true is on");
            remove(path);
        }
    }

    /* --- config: shell_integration ------------------------------------------------------------
       On by default: a recognised shell is started with Kai-Term's integration script, which is the only way
       the file manager can move the shell without touching the user's half-typed line. Off must mean "start
       the shell exactly as before", so this is the switch a user pulls if they never want Kai-Term to touch
       how their shell starts. */
    {
        Config c;
        const char *path = "/tmp/kterm_test_config_shellint.conf";
        config_default(&c);
        CHECK(c.shell_integration == 1, "config: shell integration is on by default");
        FILE *f = fopen(path, "w");
        CHECK(f != NULL, "config: temp config file writable (shell_integration)");
        if (f) {
            fputs("shell_integration = 0\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.shell_integration == 0, "config: shell_integration = 0 turns it off");
            f = fopen(path, "w");
            fputs("shell_integration = off\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.shell_integration == 0, "config: shell_integration = off turns it off too");
            f = fopen(path, "w");
            fputs("shell_integration = yes\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.shell_integration == 1, "config: shell_integration = yes turns it on");
            remove(path);
        }
    }

    /* --- config: the key-repeat settings ------------------------------------------------------
       A Wayland compositor never sends repeat events, so Kai-Term generates them itself and both
       numbers are its own: repeat_delay (ms before the first repeat, 0 = off) and repeat_rate
       (repeats per second afterwards). Both are clamped so a typo cannot stall or flood the PTY. */
    {
        Config c;
        const char *path = "/tmp/kterm_test_config3.conf";
        config_default(&c);
        CHECK(c.repeat_delay == 300, "config: repeat_delay defaults to 300 ms");
        CHECK(c.repeat_rate == 30, "config: repeat_rate defaults to 30/s");
        FILE *f = fopen(path, "w");
        CHECK(f != NULL, "config: temp config file writable (key repeat)");
        if (f) {
            fclose(f);
            f = fopen(path, "w");
            fputs("repeat_delay = 500\nrepeat_rate = 12\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.repeat_delay == 500, "config: repeat_delay read from the file");
            CHECK(c.repeat_rate == 12, "config: repeat_rate read from the file");
            f = fopen(path, "w");
            fputs("repeat_delay = 0\n", f);   /* the escape hatch: no Kai-Term-generated repeat */
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.repeat_delay == 0, "config: repeat_delay = 0 turns the repeat off");
            CHECK(c.repeat_rate == 30, "config: repeat_rate is untouched by another key's line");
            f = fopen(path, "w");
            fputs("repeat_delay = 99999\nrepeat_rate = 0\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.repeat_delay == 5000, "config: repeat_delay is clamped to 5000 ms");
            CHECK(c.repeat_rate == 1, "config: repeat_rate is clamped to at least 1/s");
            f = fopen(path, "w");
            fputs("repeat_delay = -5\nrepeat_rate = 5000\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.repeat_delay == 0, "config: a negative repeat_delay clamps to 0 (off)");
            CHECK(c.repeat_rate == 200, "config: repeat_rate is clamped to at most 200/s");
            remove(path);
        }
    }

    /* ---------- config: the search order, and the binding file ----------------------------------
       Two things are pinned here, and neither needs a display:
         1. WHERE a file comes from — $XDG_CONFIG_HOME/kai-term, then ~/.config/kai-term, then
            /etc/kai-term, first one found wins, and nothing found at all means the built-in defaults.
         2. That the search is not just a path printer: config_load(NULL) / config_load_binds(NULL)
            read what it found, each file searched on its own.
       HOME and XDG_CONFIG_HOME are pointed at a scratch tree for the duration of the block and put back
       exactly as they were (XDG_CONFIG_HOME included being ABSENT if it was absent), because every later
       check in this binary reads the real environment. The tree is built and removed with mkdir/rmdir,
       never through a shell, so no path here depends on quoting; and every path is spelled out from
       `base`, which keeps each snprintf provably inside its buffer. */
    {
        char base[128];
        snprintf(base, sizeof base, "/tmp/kterm_cfg_%d", (int)getpid());
        char xdg_root[256], d_xdg[256], f_xdg[256];
        char d_home[256], f_home[256], f_home_bind[256];
        snprintf(xdg_root, sizeof xdg_root, "%s/xdg", base);
        snprintf(d_xdg, sizeof d_xdg, "%s/xdg/kai-term", base);
        snprintf(f_xdg, sizeof f_xdg, "%s/xdg/kai-term/kai-term.conf", base);
        snprintf(d_home, sizeof d_home, "%s/.config/kai-term", base);
        snprintf(f_home, sizeof f_home, "%s/.config/kai-term/kai-term.conf", base);
        snprintf(f_home_bind, sizeof f_home_bind, "%s/.config/kai-term/kai-term.bind", base);

        char save_xdg[4096], save_home[4096];
        int had_xdg = getenv("XDG_CONFIG_HOME") != NULL;
        snprintf(save_xdg, sizeof save_xdg, "%s", getenv("XDG_CONFIG_HOME") ? getenv("XDG_CONFIG_HOME") : "");
        snprintf(save_home, sizeof save_home, "%s", getenv("HOME") ? getenv("HOME") : "");

        mkdir(base, 0700);
        mkdir(xdg_root, 0700);
        mkdir(d_xdg, 0700);
        snprintf(d_home, sizeof d_home, "%s/.config", base);      mkdir(d_home, 0700);
        snprintf(d_home, sizeof d_home, "%s/.config/kai-term", base); mkdir(d_home, 0700);

        setenv("XDG_CONFIG_HOME", xdg_root, 1);   /* the CONFIG ROOT: <root>/kai-term/<file> */
        setenv("HOME", base, 1);                  /* so ~/.config/kai-term IS the scratch one */

        char found[4096];
        struct stat sst;
        int etc_has_it = stat("/etc/kai-term/kai-term.conf", &sst) == 0 && S_ISREG(sst.st_mode);
        config_find("kai-term.conf", found, sizeof found);
        CHECK(found[0] == 0 || !strcmp(found, "/etc/kai-term/kai-term.conf"),
              "cfgsearch: with nothing in XDG or HOME the only answer is /etc/kai-term or nothing");
        CHECK(etc_has_it || found[0] == 0,
              "cfgsearch: that answer exists — /etc/kai-term is asked last, and never invented");

        FILE *sf = fopen(f_home, "w");
        CHECK(sf != NULL, "cfgsearch: the scratch HOME config is writable");
        if (sf) { fputs("app_id = from-home\n", sf); fclose(sf); }
        config_find("kai-term.conf", found, sizeof found);
        CHECK(!strcmp(found, f_home), "cfgsearch: with nothing in XDG, ~/.config/kai-term is used");

        sf = fopen(f_xdg, "w");
        CHECK(sf != NULL, "cfgsearch: the scratch XDG config is writable");
        if (sf) { fputs("app_id = from-xdg\n", sf); fclose(sf); }
        config_find("kai-term.conf", found, sizeof found);
        CHECK(!strcmp(found, f_xdg), "cfgsearch: XDG wins when both hold the file");

        /* An empty XDG_CONFIG_HOME counts as "not set", and a DIRECTORY named kai-term.conf is not a
           config file — both must fall through to the next location rather than resolve to something
           that cannot be read as settings. */
        setenv("XDG_CONFIG_HOME", "", 1);
        config_find("kai-term.conf", found, sizeof found);
        CHECK(!strcmp(found, f_home), "cfgsearch: an empty XDG_CONFIG_HOME means 'not set'");
        setenv("XDG_CONFIG_HOME", xdg_root, 1);
        remove(f_xdg);
        mkdir(f_xdg, 0700);
        config_find("kai-term.conf", found, sizeof found);
        CHECK(!strcmp(found, f_home), "cfgsearch: a directory named kai-term.conf is skipped");
        rmdir(f_xdg);

        /* And the search feeds the loader: config_load(NULL) reads the file it found. */
        Config c;
        config_default(&c);
        config_load(&c, NULL);
        CHECK(!strcmp(c.app_id, "from-home"), "cfgsearch: config_load(NULL) reads the searched file");

        /* The binding file is searched INDEPENDENTLY, so a user with a kai-term.conf in ~/.config and no
           kai-term.bind there still gets the system-wide bindings instead of the built-in ones. */
        sf = fopen(f_home_bind, "w");
        CHECK(sf != NULL, "cfgsearch: the scratch bind file is writable");
        if (sf) { fputs("key_new_tab = Alt+N\n", sf); fclose(sf); }
        config_find("kai-term.bind", found, sizeof found);
        CHECK(!strcmp(found, f_home_bind), "cfgsearch: kai-term.bind is searched the same way");
        config_default(&c);
        config_load(&c, NULL);
        config_load_binds(&c, NULL);
        CHECK(!strcmp(c.app_id, "from-home"), "cfgsearch: the settings file still applied");
        CHECK(c.key_new_tab.set && c.key_new_tab.sym == 'n' && c.key_new_tab.mods == KB_ALT,
              "cfgsearch: and the searched bind file applied its binding");

        /* Tear the tree down: every file before its directory, so each rmdir succeeds. */
        if (had_xdg)
            setenv("XDG_CONFIG_HOME", save_xdg, 1);
        else
            unsetenv("XDG_CONFIG_HOME");
        setenv("HOME", save_home, 1);
        remove(f_home);
        remove(f_home_bind);
        rmdir(d_xdg);
        rmdir(d_home);
        rmdir(xdg_root);
        snprintf(d_home, sizeof d_home, "%s/.config", base);
        rmdir(d_home);
        rmdir(base);
    }

    /* --- config: which NAME lives in which file, and which file wins ---------------------------
       The split is by name, not by "the last file read wins for everything": a binding in kai-term.conf
       keeps working (files written before the split must not silently lose their bindings), the bind
       file is read last so it wins for the same name, and a SETTING written in the bind file is refused
       out loud instead of being ignored as an unknown key. */
    {
        Config c;
        const char *cf = "/tmp/kterm_test_split.conf";
        const char *bf = "/tmp/kterm_test_split.bind";
        FILE *f = fopen(cf, "w");
        CHECK(f != NULL, "config split: the temp files are writable");
        if (f) {
            fclose(f);

            /* A binding read from its own file. */
            f = fopen(cf, "w"); fputs("font_size = 20\n", f); fclose(f);
            f = fopen(bf, "w"); fputs("key_new_tab = Alt+N\nfm_key_up = k\n", f); fclose(f);
            config_default(&c);
            config_load(&c, cf);
            config_load_binds(&c, bf);
            CHECK(c.font_size == 20, "config split: the settings file still sets a setting");
            CHECK(c.key_new_tab.set && c.key_new_tab.sym == 'n' && c.key_new_tab.mods == KB_ALT,
                  "config split: key_new_tab comes from kai-term.bind");
            CHECK(c.fm_key.up.set && c.fm_key.up.sym == 'k' && c.fm_key.up.mods == 0,
                  "config split: a bare file-manager key is legal in kai-term.bind");

            /* Both files name the same binding: the bind file is read last and wins. */
            f = fopen(cf, "w"); fputs("key_new_tab = Ctrl+Alt+J\n", f); fclose(f);
            f = fopen(bf, "w"); fputs("key_new_tab = Alt+N\n", f); fclose(f);
            config_default(&c);
            config_load(&c, cf);
            config_load_binds(&c, bf);
            CHECK(c.key_new_tab.set && c.key_new_tab.sym == 'n' && c.key_new_tab.mods == KB_ALT,
                  "config split: kai-term.bind wins over the same name in kai-term.conf");

            /* A binding left in kai-term.conf is still honoured — that is the compatibility half. */
            f = fopen(cf, "w"); fputs("key_close_tab = Alt+W\nfm_key_path = none\n", f); fclose(f);
            f = fopen(bf, "w"); fputs("# no bindings here\n", f); fclose(f);
            config_default(&c);
            config_load(&c, cf);
            config_load_binds(&c, bf);
            CHECK(c.key_close_tab.set && c.key_close_tab.sym == 'w' && c.key_close_tab.mods == KB_ALT,
                  "config split: a binding still written in kai-term.conf is applied");
            CHECK(!c.fm_key.path.set, "config split: and fm_key_path = none still unbinds it there");

            /* A SETTING in the bind file is refused — and refused as "wrong file", not as an unknown
               name, so the message cannot send the user hunting for a typo. */
            f = fopen(cf, "w"); fputs("font_size = 22\n", f); fclose(f);
            f = fopen(bf, "w"); fputs("font_size = 99\nmax_tabs = 3\n", f); fclose(f);
            config_default(&c);
            config_load(&c, cf);
            config_load_binds(&c, bf);
            CHECK(c.font_size == 22, "config split: a setting in kai-term.bind does not apply");
            CHECK(c.max_tabs == 8, "config split: nor does any other setting there");

            /* `none` / empty / 0 unbind from the bind file too. */
            f = fopen(bf, "w"); fputs("key_tab_prefix = none\nfm_key_quit =\nkey_file_manager = 0\n", f);
            fclose(f);
            config_default(&c);
            config_load_binds(&c, bf);
            CHECK(c.tab_prefix_mods == 0, "config split: key_tab_prefix = none disables the digit jump");
            CHECK(!c.fm_key.quit.set, "config split: an empty value unbinds a file-manager key");
            CHECK(!c.key_file_manager.set, "config split: = 0 unbinds a window-level shortcut");

            remove(cf);
            remove(bf);
        }
    }

    /* --- config: where a '#' starts a comment -------------------------------------------------
       `background = #1c1c22` is the form README documents and the form the shipped template writes, and
       the old rule ("whitespace before the '#' is enough") cut that line at the '#', so the colour was
       never applied and nothing was even logged. A '#' is a comment when only whitespace precedes it, or
       when whitespace precedes AND follows it; anything else is part of the value. */
    {
        Config c;
        const char *path = "/tmp/kterm_test_hash.conf";
        FILE *f = fopen(path, "w");
        CHECK(f != NULL, "config hash: the temp file is writable");
        if (f) {
            fclose(f);
            f = fopen(path, "w");
            fputs("background = #1c1c22\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.bg == 0xff1c1c22u, "config hash: 'background = #1c1c22' is a colour, not a comment");

            f = fopen(path, "w");
            fputs("foreground = #ff0000 # the ink\nworking_dir = /tmp/my#project\napp_id = hash-x # note\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.fg == 0xffff0000u, "config hash: a note after a colour is still stripped");
            CHECK(!strcmp(c.working_dir, "/tmp/my#project"),
                  "config hash: a '#' inside a value survives (no space before it)");
            CHECK(!strcmp(c.app_id, "hash-x"), "config hash: 'value # note' is still stripped");

            f = fopen(path, "w");
            fputs("#whole line\n   # indented, no space after the hash\n\tcursor_color = #00ff00\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(!strcmp(c.app_id, DEF_APP_ID), "config hash: a '#' with only whitespace before it comments the line");
            CHECK(c.cursor_color == 0xff00ff00u, "config hash: a tab-indented setting is still read");

            f = fopen(path, "w");
            fputs("background =#0000ff\n", f);      /* no space after '=' at all */
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.bg == 0xff0000ffu, "config hash: 'key =#value' is a colour too");
            remove(path);
        }
    }

    /* --- config: the shipped templates ARE the defaults ---------------------------------------
       config/kai-term.conf and config/kai-term.bind list every name with the value config_default()
       gives it. That is only true as long as somebody checks it, and the honest check is to load both
       files and compare every field of the result with a fresh default Config. Run from the repo root
       (what `make test` does); from anywhere else the files are not there and the block reports a skip
       rather than passing on nothing. */
    {
        const char *tpl_conf = "config/kai-term.conf";
        const char *tpl_bind = "config/kai-term.bind";
        FILE *fa = fopen(tpl_conf, "r");
        FILE *fb = fopen(tpl_bind, "r");
        if (fa) fclose(fa);
        if (fb) fclose(fb);
        if (!fa || !fb) {
            printf("SKIP: config templates not found at %s / %s (run from the repo root)\n",
                   tpl_conf, tpl_bind);
        } else {
            Config d, t;
            config_default(&d);
            config_default(&t);
            config_load(&t, tpl_conf);
            config_load_binds(&t, tpl_bind);
            CHECK(!strcmp(t.font, d.font), "template: font matches config_default()");
            CHECK(t.font_size == d.font_size, "template: font_size matches");
            CHECK(t.cols == d.cols && t.rows == d.rows, "template: cols/rows match");
            CHECK(t.scrollback == d.scrollback, "template: scrollback matches");
            CHECK(t.padding == d.padding, "template: padding matches");
            CHECK(t.opacity == d.opacity, "template: opacity matches");
            CHECK(t.fg == d.fg, "template: foreground matches");
            CHECK(t.bg == d.bg, "template: background matches");
            CHECK(t.cursor_color == d.cursor_color, "template: cursor_color matches");
            CHECK(!strcmp(t.shell, d.shell), "template: shell matches");
            CHECK(t.shell_integration == d.shell_integration, "template: shell_integration matches");
            CHECK(!strcmp(t.working_dir, d.working_dir), "template: working_dir matches");
            CHECK(!strcmp(t.app_id, d.app_id), "template: app_id matches");
            CHECK(t.clear_scrollback == d.clear_scrollback, "template: clear_scrollback matches");
            CHECK(t.copy_on_release == d.copy_on_release, "template: copy_on_release matches");
            CHECK(t.wheel_sends_keys == d.wheel_sends_keys,
                  "template: wheel_sends_keys matches");
            CHECK(t.click_moves_cursor == d.click_moves_cursor,
                  "template: click_moves_cursor matches");
            CHECK(t.repeat_delay == d.repeat_delay && t.repeat_rate == d.repeat_rate,
                  "template: the key-repeat timings match");
            CHECK(t.max_tabs == d.max_tabs, "template: max_tabs matches");
            CHECK(t.tab_bar == d.tab_bar, "template: tab_bar matches");
            CHECK(t.split_max == d.split_max, "template: split_max matches");
            CHECK(t.fm_on == d.fm_on && t.fm_size == d.fm_size, "template: file_manager/fm_size match");
            CHECK(t.fm_preview == d.fm_preview && t.fm_show_hidden == d.fm_show_hidden,
                  "template: fm_preview/fm_show_hidden match");
            CHECK(t.fm_follow == d.fm_follow, "template: fm_follow matches");
            CHECK(t.loglevel == d.loglevel, "template: log matches");
            CHECK(t.tab_prefix_mods == d.tab_prefix_mods, "template: key_tab_prefix matches");
            const KeyBind pairs[][2] = {
                { t.key_new_tab, d.key_new_tab },
                { t.key_close_tab, d.key_close_tab },
                { t.key_next_tab, d.key_next_tab },
                { t.key_prev_tab, d.key_prev_tab },
                { t.key_toggle_layout, d.key_toggle_layout },
                { t.key_file_manager, d.key_file_manager },
                { t.key_fm_focus, d.key_fm_focus },
                { t.key_fm_release, d.key_fm_release },
                { t.fm_key.up, d.fm_key.up },
                { t.fm_key.down, d.fm_key.down },
                { t.fm_key.pageup, d.fm_key.pageup },
                { t.fm_key.pagedown, d.fm_key.pagedown },
                { t.fm_key.home, d.fm_key.home },
                { t.fm_key.end, d.fm_key.end },
                { t.fm_key.left, d.fm_key.left },
                { t.fm_key.right, d.fm_key.right },
                { t.fm_key.open, d.fm_key.open },
                { t.fm_key.path, d.fm_key.path },
                { t.fm_key.parent, d.fm_key.parent },
                { t.fm_key.refresh, d.fm_key.refresh },
                { t.fm_key.preview, d.fm_key.preview },
                { t.fm_key.hidden, d.fm_key.hidden },
                { t.fm_key.quit, d.fm_key.quit },
            };
            static const char *pair_names[] = {
                "key_new_tab", "key_close_tab", "key_next_tab", "key_prev_tab", "key_toggle_layout",
                "key_file_manager", "key_fm_focus", "key_fm_release", "fm_key_up", "fm_key_down",
                "fm_key_pageup", "fm_key_pagedown", "fm_key_home", "fm_key_end", "fm_key_left",
                "fm_key_right", "fm_key_open", "fm_key_path", "fm_key_parent", "fm_key_refresh",
                "fm_key_preview", "fm_key_hidden", "fm_key_quit",
            };
            int bad = 0;
            for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++)
                if (pairs[i][0].sym != pairs[i][1].sym || pairs[i][0].mods != pairs[i][1].mods ||
                    pairs[i][0].set != pairs[i][1].set) {
                    printf("FAIL: template: %s is %x/%x/%d in the file, %x/%x/%d in the code\n",
                           pair_names[i], pairs[i][0].sym, pairs[i][0].mods, pairs[i][0].set,
                           pairs[i][1].sym, pairs[i][1].mods, pairs[i][1].set);
                    bad++;
                }
            CHECK(bad == 0, "template: every binding in kai-term.bind matches config_default()");

            /* --- and the NAMES themselves, in both directions -----------------------------
               The field-by-field comparison above cannot see a deleted line: a template that stopped
               mentioning `padding` still loads to exactly the default, so it stays green while the file
               claims to hold every default. The accepted names are read out of the two strcmp ladders in
               src/config.c, so this fails when a name is added to the parser and not to the template —
               the direction a hand-maintained list in this file could never catch. Verified against a
               plain set difference first (.scratch/s99/names.sh): 29 settings + 24 bindings, empty diff
               in every direction. */
            const char *set_names[64], *bind_names[64];
            int ns = cfg_names_of("src/config.c", "static void apply_setting(", set_names, 64);
            int nb = cfg_names_of("src/config.c", "static int apply_bind_key(", bind_names, 64);
            CHECK(ns > 0, "template: the settings names were read from src/config.c (the ladder moved?)");
            CHECK(nb > 0, "template: the binding names were read from src/config.c (the ladder moved?)");
            CHECK(ns == 29 && nb == 24,
                  "template: the parser accepts 29 settings and 24 bindings — re-diff, do not copy");
            int nbad = 0;
            if (ns > 0)
                nbad += tpl_check_names(tpl_conf, (const char *const *)set_names, ns);
            if (nb > 0)
                nbad += tpl_check_names(tpl_bind, (const char *const *)bind_names, nb);
            CHECK(nbad == 0,
                  "template: kai-term.conf and kai-term.bind list exactly the names the parser accepts");
            for (int i = 0; i < ns; i++)
                free((char *)set_names[i]);
            for (int i = 0; i < nb; i++)
                free((char *)bind_names[i]);
        }
    }

    /* ---------- a viewport grown past half of rcap (the row[]-overrun segfault) ----------
       rcap is fixed at (rows at init + scrollback), so growing the viewport shrinks the scrollback
       that fits: the bound on scroll_off/live_off is rcap - rows, NOT scrollback_max. Feeding more
       lines than that used to make scroll_up blank row[rcap] — a heap-buffer-overflow ASan found in
       a real window (`seq 1 5000`: 30 configured rows, a 62-row window, 2000 scrollback). */
    {
        term_destroy(&T);
        term_init(&T, 4, 20, 20);            /* rcap = 24, scrollback_max = 20 */
        term_resize(&T, 12, 20);             /* usable scrollback is now 24 - 12 = 12 */
        CHECK(T.g.rcap == 24 && T.g.rows == 12, "grow: rcap stays fixed while the viewport grows");
        CHECK(T.g.scrollback_max == 20 && T.g.rcap - T.g.rows == 12,
              "grow: the usable scrollback is smaller than scrollback_max");
        for (int i = 1; i <= 60; i++) {
            char l[32];
            snprintf(l, sizeof l, "G%02d\r\n", i);
            feed(l);
        }
        feed("NEWEST");
        CHECK(T.g.scroll_off + T.g.rows <= T.g.rcap, "grow: scrolling keeps used <= rcap");
        CHECK(T.g.live_off <= T.g.rcap - T.g.rows, "grow: live_off stays inside the grid");
        CHECK(T.g.live_off == T.g.scroll_off, "grow: printing keeps the view at the live anchor");
        char linebuf[64];
        row_text(T.g.rows - 1, linebuf);
        CHECK(strstr(linebuf, "NEWEST") != NULL, "grow: the newest line is on screen");
        CHECK(content_rows() == T.g.rcap, "grow: every slot holds content once the scrollback fills");
        feed("\x1b[1T");                     /* scroll down: must stay inside the grid */
        CHECK(T.g.scroll_off + T.g.rows <= T.g.rcap, "grow: scroll-down stays inside the grid");
        term_resize(&T, 4, 20);              /* shrink again, then overflow once more */
        for (int i = 1; i <= 40; i++) {
            char l[32];
            snprintf(l, sizeof l, "H%02d\r\n", i);
            feed(l);
        }
        feed("LAST");
        CHECK(T.g.scroll_off + T.g.rows <= T.g.rcap, "shrink after grow: used <= rcap");
        CHECK(T.g.live_off <= T.g.scrollback_max && T.g.live_off <= T.g.rcap - T.g.rows,
              "shrink after grow: live_off within both bounds");
        row_text(T.g.rows - 1, linebuf);
        CHECK(strstr(linebuf, "LAST") != NULL, "shrink after grow: the newest line is on screen");
    }

    /* --- multiplexing: the tab bookkeeping (src/mux.c) ----------------------------------------
       One window hosts several sessions. Only the bookkeeping is testable here (the PTYs and the
       window belong to main.c), and that is exactly where the bugs live: an active index pointing
       past the end, a grid left behind or freed twice, a shifted Tab keeping a pointer into its old
       home, or a limit that can be exceeded. */
    {
        Mux MX;
        char ttl[128];

        mux_init(&MX, 8);
        CHECK(MX.max == 8, "mux: max_tabs kept");
        mux_init(&MX, 0);
        CHECK(MX.max == 1, "mux: max_tabs 0 clamps to 1 (a terminal must keep its first tab)");
        mux_init(&MX, TAB_MAX + 500);
        CHECK(MX.max == TAB_MAX, "mux: max_tabs clamps to TAB_MAX");
        CHECK(mux_count(&MX) == 0 && mux_active(&MX) == -1 && mux_cur(&MX) == NULL,
              "mux: an empty mux has no tab");
        mux_title(&MX, ttl, sizeof ttl);
        CHECK(!strcmp(ttl, DEF_TITLE), "mux: an empty mux still titles the window Kai-Term");
        CHECK(mux_next(&MX) == -1 && mux_prev(&MX) == -1 && mux_goto(&MX, 0) == -1,
              "mux: next/prev/goto on an empty mux do nothing");

        mux_init(&MX, 3);
        int t0 = mux_open(&MX, 6, 20, 50);
        CHECK(t0 == 0 && mux_count(&MX) == 1 && mux_active(&MX) == 0,
              "mux: the first tab is index 0 and the visible one");
        CHECK(mux_cur(&MX) == &MX.tab[0], "mux: mux_cur names the visible tab");
        CHECK(MX.tab[0].term.g.rows == 6 && MX.tab[0].term.g.cols == 20,
              "mux: a new tab gets the requested grid");
        CHECK(MX.tab[0].pty.mfd == -1 && MX.tab[0].pty.child == 0,
              "mux: a new tab has no PTY yet (main.c owns spawning it)");
        mux_title(&MX, ttl, sizeof ttl);
        CHECK(!strcmp(ttl, DEF_TITLE), "mux: one tab keeps the pre-multiplexing title exactly");

        int t1 = mux_open(&MX, 6, 20, 50);
        int t2 = mux_open(&MX, 6, 20, 50);
        CHECK(t1 == 1 && t2 == 2 && mux_count(&MX) == 3, "mux: tabs stay dense 0..n-1");
        CHECK(mux_active(&MX) == 2, "mux: opening a tab makes it the visible one");
        CHECK(mux_open(&MX, 6, 20, 50) == -1 && mux_count(&MX) == 3,
              "mux: max_tabs stops a fourth tab");
        mux_title(&MX, ttl, sizeof ttl);
        CHECK(!strcmp(ttl, "Kai-Term [3/3]"), "mux: the title says which tab of how many");

        /* Mark every tab: a distinct glyph in cell (0,0) and a distinct OSC title. The title also
           arms Term.ps.t (osc_end() reads the parser's back-pointer), which is what the shift on
           close must repair. One tab is put on the alt screen so mux_close() has to free BOTH of
           its grids (ASan checks that nothing leaks or is freed twice). */
        for (int i = 0; i < 3; i++) {
            char mark = (char)('A' + i);
            term_parse(&MX.tab[i].term, (const uint8_t *)&mark, 1);
            char seq[32];
            snprintf(seq, sizeof seq, "\x1b]0;T%d\x07", i);
            term_parse(&MX.tab[i].term, (const uint8_t *)seq, strlen(seq));
        }
        const char *alt = "\x1b[?1049h";
        term_parse(&MX.tab[2].term, (const uint8_t *)alt, strlen(alt));
        CHECK(MX.tab[2].term.alt_screen == 1, "mux: tab 2 is on the alternate screen");
        CHECK(grid_vprow(&MX.tab[2].term.g, 0)[0].ch == 0, "mux: the alt screen starts blank");

        CHECK(mux_goto(&MX, 0) == 0, "mux: goto 0");
        mux_title(&MX, ttl, sizeof ttl);
        CHECK(!strcmp(ttl, "Kai-Term [1/3]"), "mux: the title follows the visible tab");
        CHECK(mux_goto(&MX, 99) == 2 && mux_goto(&MX, -7) == 0,
              "mux: goto clamps instead of rejecting an out-of-range index");
        CHECK(mux_next(&MX) == 1 && mux_next(&MX) == 2 && mux_next(&MX) == 0,
              "mux: next wraps around");
        CHECK(mux_prev(&MX) == 2, "mux: prev wraps around backwards");

        CHECK(mux_touch(&MX, 1) == 1 && mux_touch(&MX, 1) == 0,
              "mux: touch marks a hidden tab once, then reports nothing new");
        CHECK(mux_touch(&MX, mux_active(&MX)) == 0, "mux: the visible tab never marks itself");
        mux_touch(&MX, 0);          /* the other hidden tab */
        mux_title(&MX, ttl, sizeof ttl);
        CHECK(!strcmp(ttl, "Kai-Term [3/3] !2"), "mux: unread output on hidden tabs is reported");
        CHECK(mux_goto(&MX, 1) == 1, "mux: goto 1");
        mux_title(&MX, ttl, sizeof ttl);
        CHECK(!strcmp(ttl, "Kai-Term [2/3] !1"), "mux: looking at a tab clears its unread mark");

        /* Close the MIDDLE tab while it is visible: the later one must slide down with its content,
           its parser back-pointer must point at its new home, and the vacated slot must be dead. */
        CHECK(mux_close(&MX, 1) == 1 && mux_count(&MX) == 2,
              "mux: closing the visible middle tab keeps two tabs and stays in bounds");
        CHECK(grid_vprow(&MX.tab[1].term.g, 0)[0].ch == 0 &&
              !strcmp(MX.tab[1].term.title, "T2"),
              "mux: the tab after a closed one slides down with its content intact");
        CHECK(MX.tab[1].term.ps.t == &MX.tab[1].term,
              "mux: a moved tab's parser points at its new home (ps.t re-armed)");
        CHECK(MX.tab[2].term.g.valid != GRID_VALID && MX.tab[2].term.g.row == NULL,
              "mux: the vacated slot is zeroed, so its grids cannot be freed twice");
        CHECK(mux_close(&MX, 1) == 0 && mux_count(&MX) == 1 && mux_active(&MX) == 0,
              "mux: closing the last-indexed visible tab steps back to the one before it");
        mux_title(&MX, ttl, sizeof ttl);
        CHECK(!strcmp(ttl, DEF_TITLE), "mux: back to one tab, back to the plain title");
        CHECK(mux_close(&MX, 7) == mux_active(&MX) && mux_count(&MX) == 1,
              "mux: closing a tab that does not exist changes nothing");
        CHECK(mux_close(&MX, 0) == -1 && mux_count(&MX) == 0 && mux_active(&MX) == -1 &&
              mux_cur(&MX) == NULL, "mux: closing the last tab reports -1 so main.c can quit");

        CHECK(mux_open(&MX, 4, 10, 20) == 0, "mux: a slot is reusable after every tab closed");
        CHECK(grid_vprow(&MX.tab[0].term.g, 0)[0].ch == 0 && MX.tab[0].term.title[0] == 0,
              "mux: a reused slot starts empty, not as the old tab");
        mux_close(&MX, 0);
    }

    /* --- config: the key-binding specs ("Ctrl+Shift+T") ----------------------------------------
       Every tab action is rebindable from the config file, so the spec parser is user-facing. The
       two rules that protect the user from themselves: a binding with NO modifier is refused (it
       would take a plain typing key away from the shell), and a spec that does not parse keeps the
       binding already in place rather than silently disabling the action. */
    {
        KeyBind KB;
        CHECK(keybind_parse("Ctrl+Shift+T", &KB) && KB.set == 1 &&
              KB.mods == (KB_CTRL | KB_SHIFT) && KB.sym == 't',
              "keybind: Ctrl+Shift+T parses to ctrl+shift+t");
        CHECK(keybind_parse("ctrl+shift+t", &KB) && KB.sym == 't',
              "keybind: the spec is case-insensitive (it names the key, not the Shift state)");
        CHECK(keybind_parse(" Ctrl + Shift + T ", &KB) && KB.mods == (KB_CTRL | KB_SHIFT) &&
              KB.sym == 't', "keybind: spaces around the tokens are ignored");
        CHECK(keybind_parse("Control+Shift+Right", &KB) && KB.sym == KB_KEY_Right &&
              KB.sym == 0xff53, "keybind: a named key uses the X keysym number xkb returns");
        CHECK(keybind_parse("Ctrl+Shift+F12", &KB) && KB.sym == KB_KEY_F12, "keybind: F12");
        CHECK(keybind_parse("Alt+1", &KB) && KB.sym == '1' && KB.mods == KB_ALT, "keybind: a digit key");
        CHECK(keybind_parse("Mod1+Mod4+Up", &KB) && KB.mods == (KB_ALT | KB_SUPER) &&
              KB.sym == KB_KEY_Up, "keybind: xkb modifier names are accepted");
        CHECK(!keybind_parse("T", &KB) && KB.set == 0,
              "keybind: a binding with no modifier is refused (it would swallow typing)");
        CHECK(!keybind_parse("Ctrl+Bogus", &KB) && KB.set == 0, "keybind: an unknown key name is refused");
        CHECK(!keybind_parse("Ctrl+Shift+T+U", &KB), "keybind: two keys in one spec is refused");
        CHECK(!keybind_parse("Ctrl", &KB), "keybind: modifiers with no key is refused");
        CHECK(!keybind_parse("", &KB), "keybind: an empty spec is refused");

        /* The file manager's keys go through the same parser with the no-modifier rule lifted: they are
           only matched while the file manager owns the keyboard, so a bare `Up` cannot steal the shell's
           arrow keys. Everything else the strict parser checks still applies. */
        CHECK(keybind_parse_plain("Up", &KB) && KB.set == 1 && KB.mods == 0 && KB.sym == KB_KEY_Up,
              "keybind plain: a bare Up is usable (and carries no modifier)");
        CHECK(keybind_parse_plain("r", &KB) && KB.sym == 'r' && KB.mods == 0,
              "keybind plain: a bare letter is usable");
        CHECK(keybind_parse_plain(".", &KB) && KB.sym == '.', "keybind plain: punctuation is usable");
        CHECK(keybind_parse_plain("Ctrl+Shift+Up", &KB) && KB.mods == (KB_CTRL | KB_SHIFT),
              "keybind plain: modifiers are still accepted");
        CHECK(!keybind_parse_plain("Ctrl+Bogus", &KB) && KB.set == 0,
              "keybind plain: an unknown key name is still refused");
        CHECK(!keybind_parse_plain("Ctrl+Shift+T+U", &KB),
              "keybind plain: two keys in one spec is still refused");
        CHECK(!keybind_parse_plain("Ctrl", &KB), "keybind plain: modifiers with no key is still refused");
        CHECK(!keybind_parse_plain("", &KB), "keybind plain: an empty spec is still refused");

        unsigned MM = 7;
        CHECK(keymods_parse("Ctrl+Shift", &MM) && MM == (KB_CTRL | KB_SHIFT), "keymods: Ctrl+Shift");
        MM = 7;
        CHECK(keymods_parse("mod1+mod4", &MM) && MM == (KB_ALT | KB_SUPER), "keymods: xkb names work");
        MM = 7;
        CHECK(!keymods_parse("Ctrl+T", &MM) && MM == 0, "keymods: a key in a modifier-only spec is refused");
        MM = 7;
        CHECK(!keymods_parse("", &MM) && MM == 0, "keymods: an empty spec is refused");
    }

    /* --- config: the multiplexing settings ---------------------------------------------------
       max_tabs plus the five bindings. A binding the file does not mention keeps its default, and a
       bad spec must not disable the action the user was trying to change. */
    {
        Config c;
        const char *path = "/tmp/kterm_test_config4.conf";
        config_default(&c);
        CHECK(c.max_tabs == 8, "config: max_tabs defaults to 8");
        CHECK(c.key_new_tab.set && c.key_new_tab.sym == 't' &&
              c.key_new_tab.mods == (KB_CTRL | KB_SHIFT), "config: the default new-tab binding");
        CHECK(c.key_close_tab.set && c.key_close_tab.sym == 'w' &&
              c.key_close_tab.mods == (KB_CTRL | KB_SHIFT), "config: the default close-tab binding");
        CHECK(c.key_next_tab.set && c.key_next_tab.sym == KB_KEY_Right,
              "config: the default next-tab binding");
        CHECK(c.key_prev_tab.set && c.key_prev_tab.sym == KB_KEY_Left,
              "config: the default prev-tab binding");
        CHECK(c.tab_prefix_mods == (KB_CTRL | KB_SHIFT), "config: the default tab prefix is Ctrl+Shift");
        FILE *f = fopen(path, "w");
        CHECK(f != NULL, "config: temp config file writable (key bindings)");
        if (f) {
            fclose(f);
            f = fopen(path, "w");
            fputs("key_new_tab = Alt+N\nkey_close_tab = Ctrl+Alt+W\n"
                  "key_next_tab = Super+Right\nkey_tab_prefix = Alt\nmax_tabs = 3\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.key_new_tab.set && c.key_new_tab.sym == 'n' && c.key_new_tab.mods == KB_ALT,
                  "config: key_new_tab = Alt+N rebinds it");
            CHECK(c.key_close_tab.mods == (KB_CTRL | KB_ALT), "config: key_close_tab = Ctrl+Alt+W");
            CHECK(c.key_next_tab.mods == KB_SUPER, "config: key_next_tab = Super+Right");
            CHECK(c.tab_prefix_mods == KB_ALT, "config: key_tab_prefix = Alt");
            CHECK(c.max_tabs == 3, "config: max_tabs read from the file");
            CHECK(c.key_prev_tab.set && c.key_prev_tab.sym == KB_KEY_Left,
                  "config: a binding the file does not mention keeps its default");

            f = fopen(path, "w");
            fputs("key_new_tab = none\nkey_tab_prefix = off\nmax_tabs = 0\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.key_new_tab.set == 0, "config: key_new_tab = none unbinds the action");
            CHECK(c.tab_prefix_mods == 0, "config: key_tab_prefix = off disables the digit shortcut");
            CHECK(c.max_tabs == 1, "config: max_tabs = 0 clamps to 1");

            f = fopen(path, "w");
            fputs("key_new_tab =\nkey_close_tab = Ctrl+Shift+Nope\nmax_tabs = 99999\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.key_new_tab.set == 0, "config: an empty value unbinds too");
            CHECK(c.key_close_tab.set && c.key_close_tab.sym == 'w',
                  "config: an unparseable binding keeps the default instead of disabling the action");
            CHECK(c.max_tabs == TAB_MAX, "config: max_tabs clamps to TAB_MAX");
            remove(path);
        }
    }

    /* --- config: how the sessions are shown (tab_bar / split_max / key_toggle_layout) ---------
       These three decide what the user SEES of the multiplexing, so each value has to be readable and a
       bad one must never turn the bar off by accident. */
    {
        Config c;
        const char *path = "/tmp/kterm_test_config5.conf";
        config_default(&c);
        CHECK(c.tab_bar == BAR_AUTO, "config: tab_bar defaults to auto (bar from the second session)");
        CHECK(c.split_max == 4, "config: split_max defaults to 4");
        CHECK(c.key_toggle_layout.set && c.key_toggle_layout.sym == 's' &&
              c.key_toggle_layout.mods == (KB_CTRL | KB_SHIFT),
              "config: the default layout toggle is Ctrl+Shift+S");
        FILE *f = fopen(path, "w");
        CHECK(f != NULL, "config: temp config file writable (layout settings)");
        if (f) {
            fputs("tab_bar = always\nsplit_max = 2\nkey_toggle_layout = Alt+Return\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.tab_bar == BAR_ALWAYS, "config: tab_bar = always");
            CHECK(c.split_max == 2, "config: split_max = 2");
            CHECK(c.key_toggle_layout.set && c.key_toggle_layout.sym == KB_KEY_Return &&
                  c.key_toggle_layout.mods == KB_ALT, "config: key_toggle_layout = Alt+Return");

            f = fopen(path, "w");
            fputs("tab_bar = off\nsplit_max = 0\nkey_toggle_layout = none\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.tab_bar == BAR_OFF, "config: tab_bar = off hides the bar completely");
            CHECK(c.split_max == 1, "config: split_max = 0 clamps to 1 (a split view needs a pane)");
            CHECK(c.key_toggle_layout.set == 0, "config: key_toggle_layout = none unbinds the toggle");

            f = fopen(path, "w");
            fputs("tab_bar = sometimes\nsplit_max = 99999\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.tab_bar == BAR_AUTO, "config: an unknown tab_bar value keeps the current one");
            CHECK(c.split_max == TAB_MAX, "config: split_max clamps to TAB_MAX");
            remove(path);
        }
    }

    /* --- config: the file manager (file_manager / fm_size / fm_* bindings) ---------------------
       Off by default, because the feature must not change what a user who never touches it sees. The
       fourteen fm_key_* bindings are the ones allowed to be bare keys (keybind_parse_plain). */
    {
        Config c;
        const char *path = "/tmp/kterm_test_config6.conf";
        config_default(&c);
        CHECK(c.fm_on == 0, "config: the file manager is off by default");
        CHECK(c.fm_size == 75, "config: the band takes 75% of the window's height by default");
        CHECK(c.fm_preview == 1, "config: the preview pane is on by default");
        CHECK(c.fm_show_hidden == 0, "config: dotfiles are hidden by default");
        CHECK(c.fm_follow == 1, "config: the shell follows the browsed directory by default");
        CHECK(c.key_file_manager.set && c.key_file_manager.sym == 'f' &&
              c.key_file_manager.mods == (KB_CTRL | KB_SHIFT),
              "config: the default file-manager toggle is Ctrl+Shift+F");
        CHECK(c.key_fm_focus.set && c.key_fm_focus.sym == KB_KEY_Up &&
              c.key_fm_focus.mods == (KB_CTRL | KB_SHIFT),
              "config: Ctrl+Shift+Up focuses the file manager by default");
        CHECK(c.key_fm_release.set && c.key_fm_release.sym == KB_KEY_Down &&
              c.key_fm_release.mods == (KB_CTRL | KB_SHIFT),
              "config: Ctrl+Shift+Down gives the keyboard back to the terminal");
        CHECK(c.fm_key.up.set && c.fm_key.up.sym == KB_KEY_Up && c.fm_key.up.mods == 0,
              "config: the default browser key is a bare Up");
        CHECK(c.fm_key.left.set && c.fm_key.left.sym == KB_KEY_Left && c.fm_key.left.mods == 0,
              "config: Left goes to the parent directory by default");
        CHECK(c.fm_key.right.set && c.fm_key.right.sym == KB_KEY_Right && c.fm_key.right.mods == 0,
              "config: Right enters the highlighted directory by default");
        CHECK(c.fm_key.open.set && c.fm_key.open.sym == KB_KEY_Return,
              "config: Return opens a directory (and, since s95, nothing at all on a file)");
        CHECK(c.fm_key.path.set && c.fm_key.path.sym == 'p' && c.fm_key.path.mods == 0,
              "config: p types the selected path at the prompt (s95 — this is the only band key that "
              "touches the shell's line)");
        CHECK(c.fm_key.preview.set && c.fm_key.preview.sym == 'v' && c.fm_key.preview.mods == 0,
              "config: v toggles the preview pane — it moved off p when p became the path key");
        CHECK(c.fm_key.parent.set && c.fm_key.parent.sym == KB_KEY_BackSpace,
              "config: Backspace goes to the parent directory");
        CHECK(c.fm_key.quit.set && c.fm_key.quit.sym == KB_KEY_Escape,
              "config: Escape closes the file manager");
        CHECK(c.fm_key.hidden.set && c.fm_key.hidden.sym == '.', "config: dotfiles toggle on .");

        FILE *f = fopen(path, "w");
        CHECK(f != NULL, "config: temp config file writable (file manager settings)");
        if (f) {
            fputs("file_manager = on\nfm_size = 40\nfm_preview = off\nfm_show_hidden = yes\n"
                  "fm_follow = 0\nkey_file_manager = Alt+F\nkey_fm_focus = none\n"
                  "fm_key_up = k\nfm_key_down = j\nfm_key_open = Enter\n"
                  "fm_key_left = h\nfm_key_right = l\nfm_key_path = g\nfm_key_preview = p\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.fm_on == 1, "config: file_manager = on starts the feature shown");
            CHECK(c.fm_size == 40, "config: fm_size read from the file");
            CHECK(c.fm_preview == 0, "config: fm_preview = off hides the preview pane");
            CHECK(c.fm_show_hidden == 1, "config: fm_show_hidden = yes lists dotfiles");
            CHECK(c.fm_follow == 0, "config: fm_follow = 0 leaves the shell alone");
            CHECK(c.key_file_manager.set && c.key_file_manager.sym == 'f' &&
                  c.key_file_manager.mods == KB_ALT, "config: key_file_manager = Alt+F rebinds it");
            CHECK(c.key_fm_focus.set == 0, "config: key_fm_focus = none unbinds the focus switch");
            CHECK(c.fm_key.up.set && c.fm_key.up.sym == 'k' && c.fm_key.up.mods == 0,
                  "config: fm_key_up = k rebinds the browser's up key");
            CHECK(c.fm_key.down.sym == 'j', "config: fm_key_down = j");
            CHECK(c.fm_key.open.sym == KB_KEY_Return, "config: fm_key_open = Enter (same keysym as Return)");
            CHECK(c.fm_key.left.set && c.fm_key.left.sym == 'h' && c.fm_key.left.mods == 0,
                  "config: fm_key_left = h rebinds the parent-directory key to a bare h");
            CHECK(c.fm_key.right.set && c.fm_key.right.sym == 'l' && c.fm_key.right.mods == 0,
                  "config: fm_key_right = l rebinds the enter-directory key to a bare l");
            CHECK(c.fm_key.path.set && c.fm_key.path.sym == 'g' && c.fm_key.path.mods == 0,
                  "config: fm_key_path = g rebinds the key that types the path");
            CHECK(c.fm_key.preview.set && c.fm_key.preview.sym == 'p' && c.fm_key.preview.mods == 0,
                  "config: fm_key_preview = p gives the old p back to the preview toggle");
            CHECK(c.fm_key.refresh.set && c.fm_key.refresh.sym == 'r',
                  "config: a fm_key the file does not mention keeps its default");

            f = fopen(path, "w");
            fputs("fm_size = 0\nfm_key_up = none\nfm_key_left = none\n"
                  "fm_key_path = none\n"
                  "fm_key_open = Ctrl+Shift+Nope\nfm_key_right = Ctrl+Shift+Nope\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.fm_size == FM_SIZE_MIN, "config: fm_size = 0 clamps to the layout minimum");
            CHECK(c.fm_key.up.set == 0, "config: fm_key_up = none unbinds that action");
            CHECK(c.fm_key.left.set == 0, "config: fm_key_left = none leaves BackSpace as the only way up");
            CHECK(c.fm_key.path.set == 0,
                  "config: fm_key_path = none leaves NO key that can write at the prompt");
            CHECK(c.fm_key.open.set && c.fm_key.open.sym == KB_KEY_Return,
                  "config: an unparseable fm_key keeps the default instead of disabling the action");
            CHECK(c.fm_key.right.set && c.fm_key.right.sym == KB_KEY_Right,
                  "config: an unparseable fm_key_right keeps Right, it does not disable entering");

            f = fopen(path, "w");
            fputs("fm_size = 99999\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.fm_size == FM_SIZE_MAX, "config: fm_size clamps to the layout maximum");
            remove(path);
        }
    }

    /* --- layout: the tab bar and the side-by-side panes ----------------------------------------
       The bar, the click targets and the panes are all decided by one pure function, so every rule that
       the user can see (or trip over) is checkable here: the single-session window must not change shape,
       a click must name the tab it is drawn on, a pane's grid must fit its rect, and the pointer mapping
       (col = (ptr_x - pane.x)/cell_w) must stay exact because the leftover pixels stay at the right. */
    {
        LayoutReq rq;
        Layout L;

        /* One session, bar off: exactly the geometry the terminal had before multiplexing. */
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, 8, 8, 1, 0, 4, BAR_OFF);
        layout_compute(&rq, &L);
        int gcols, grows, gxoff, gyoff;
        term_grid_geometry(936, 1002, 8, 17, 8, &gcols, &grows, &gxoff, &gyoff);
        CHECK(!L.bar_on && L.nitem == 0 && L.npane == 1,
              "layout: one session with tab_bar off draws one pane and no bar");
        CHECK(L.pane[0].x == gxoff && L.pane[0].y == gyoff &&
              L.pane[0].cols == gcols && L.pane[0].rows == grows,
              "layout: one pane with the bar off is exactly the old single-grid geometry");
        CHECK(L.pane[0].w == 936 - 16 && L.pane[0].h == 1002 - 16,
              "layout: the pane is the window minus the margin on both sides");
        CHECK(L.pane[0].cols * 8 <= L.pane[0].w && L.pane[0].rows * 17 <= L.pane[0].h,
              "layout: a pane's grid never grows past its rect");

        /* The bar appears by itself at the second session (BAR_AUTO). */
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, 8, 8, 2, 1, 4, BAR_AUTO);
        layout_compute(&rq, &L);
        CHECK(L.bar_on && L.nitem == 2 && L.npane == 1,
              "layout: BAR_AUTO shows the bar from the second session on");
        CHECK(L.bar_y == 8 && L.bar_h == 17 && L.bar_x == 8 && L.bar_w == 920,
              "layout: the bar is one cell row inside the top margin");
        CHECK(L.pane[0].y == 8 + 17 + 8 && L.pane[0].h == 1002 - (8 + 17 + 8) - 8,
              "layout: the grid starts below the bar + gap and keeps its bottom margin");
        CHECK(L.pane[0].rows == (1002 - 33 - 8) / 17, "layout: the bar costs exactly one text row");
        CHECK(L.item[0].x == 8 && L.item[1].x == 8 + 460 && L.item[0].w == 460,
              "layout: bar items are equal width, leftover px at the right");
        CHECK(L.item[0].tab == 0 && L.item[1].tab == 1, "layout: an item names its session by index");
        CHECK(L.item[1].active == 1 && L.item[0].active == 0,
              "layout: the item of the visible session is the highlighted one");
        CHECK(layout_hit_bar(&L, 100, 12) == 0 && layout_hit_bar(&L, 500, 12) == 1,
              "layout: a click in the bar names the item it lands on (what makes tabs clickable)");
        CHECK(layout_hit_bar(&L, 100, 100) == -1 && layout_hit_bar(&L, 4, 12) == -1,
              "layout: a click below the bar or in the margin is not in the bar");
        CHECK(layout_hit_pane(&L, 100, 100) == 0 && layout_hit_pane(&L, 100, 12) == -1,
              "layout: the bar is not part of any pane");

        /* Labels: number, unread mark, OSC title, and a cut that fits the item. */
        CHECK(!strcmp(L.item[0].label, "1 shell"),
              "layout: an unnamed session is labelled by its number + shell");
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, 8, 8, 2, 1, 4, BAR_AUTO);
        rq.tab[0].attention = 1;
        rq.tab[1].title = "vim";
        layout_compute(&rq, &L);
        CHECK(!strcmp(L.item[0].label, "1 *shell"),
              "layout: unread output on a hidden session marks its item");
        CHECK(!strcmp(L.item[1].label, "2 vim"), "layout: a session's OSC title is its label");

        lreq(&rq, LAYOUT_TABS, 80, 400, 8, 17, 8, 8, 2, 0, 4, BAR_AUTO);   /* items 32 px = 4 columns */
        rq.tab[0].title = "a\xc3\xa9" "b";                                  /* "aéb" (\x.. is greedy) */
        layout_compute(&rq, &L);
        CHECK(!strcmp(L.item[0].label, "1 a\xc3\xa9"),
              "layout: a label is cut to the item width at a UTF-8 codepoint edge, never mid-sequence");
        for (int i = 0; i < L.nitem; i++)
            if (utf8_len(L.item[i].label) * 8 > L.item[i].w) {
                CHECK(0, "layout: a label never needs more columns than its item has");
                break;
            }

        /* Side-by-side: equal panes, the gap between them, each grid fitted to its own rect. */
        lreq(&rq, LAYOUT_SPLIT, 936, 1002, 8, 17, 8, 8, 3, 0, 3, BAR_AUTO);
        layout_compute(&rq, &L);
        CHECK(L.npane == 3 && L.nitem == 3, "layout: split mode draws one pane per session");
        CHECK(L.pane[0].x == 8 && L.pane[0].w == (920 - 2 * 8) / 3,
              "layout: split panes share the width equally");
        CHECK(L.pane[1].x == 8 + 301 + 8 && L.pane[2].x == 8 + 2 * (301 + 8),
              "layout: the gap separates the panes");
        CHECK(L.pane[0].cols == 301 / 8 && L.pane[0].rows == (1002 - 33 - 8) / 17,
              "layout: every pane gets the grid that fits its own rect");
        int overlap = 0;
        for (int i = 0; i + 1 < L.npane; i++)
            if (L.pane[i].x + L.pane[i].w > L.pane[i + 1].x ||
                L.pane[i].y != L.pane[i + 1].y || L.pane[i].h != L.pane[i + 1].h)
                overlap = 1;
        CHECK(!overlap, "layout: panes never overlap and share one top edge and height");
        CHECK(layout_hit_pane(&L, L.pane[1].x + 5, 200) == 1,
              "layout: a click in a pane names that pane (click to focus)");
        CHECK(layout_hit_pane(&L, L.pane[0].x + L.pane[0].w + 2, 200) == -1,
              "layout: a click in the gap between panes belongs to no pane");
        CHECK(layout_pane_of_tab(&L, 2) == 2 && layout_pane_of_tab(&L, 5) == -1,
              "layout: pane_of_tab finds a drawn session and rejects one that is not drawn");

        /* More sessions than split_max: the shown window slides to contain the active one. */
        lreq(&rq, LAYOUT_SPLIT, 936, 1002, 8, 17, 8, 8, 6, 0, 3, BAR_AUTO);
        layout_compute(&rq, &L);
        CHECK(L.npane == 3 && L.pane[0].tab == 0 && L.pane[2].tab == 2,
              "layout: split_max caps the panes, starting at the first session");
        CHECK(layout_pane_of_tab(&L, 5) == -1 && layout_pane_of_tab(&L, 0) == 0,
              "layout: a session outside the visible pane window has no pane");
        rq.active = 5;
        layout_compute(&rq, &L);
        CHECK(L.pane[0].tab == 3 && L.pane[2].tab == 5,
              "layout: the pane window slides to the end so the last session is reachable");
        rq.active = 3;
        layout_compute(&rq, &L);
        CHECK(L.pane[0].tab == 2 && L.pane[2].tab == 4, "layout: the pane window centres on the active one");
        CHECK(L.nitem == 6, "layout: the bar still lists every session, drawn in a pane or not");

        /* Tab mode: the single pane shows the visible session, and only it has a pane. */
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, 8, 8, 3, 2, 4, BAR_AUTO);
        layout_compute(&rq, &L);
        CHECK(L.npane == 1 && L.pane[0].tab == 2, "layout: tab mode draws the visible session only");
        CHECK(layout_pane_of_tab(&L, 2) == 0 && layout_pane_of_tab(&L, 0) == -1,
              "layout: the hidden sessions of tab mode have no pane (their output marks the bar instead)");

        /* Degenerate windows: the terminal wins over the chrome, and a grid is never empty. */
        lreq(&rq, LAYOUT_TABS, 30, 30, 8, 17, 20, 8, 2, 0, 4, BAR_AUTO);
        layout_compute(&rq, &L);
        CHECK(!L.bar_on, "layout: a window too short for bar + one row drops the bar, not the text");
        CHECK(L.npane == 1 && L.pane[0].cols >= 1 && L.pane[0].rows >= 1,
              "layout: even a tiny window keeps a usable grid (the margin is capped)");
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, 8, 8, 0, -1, 4, BAR_ALWAYS);
        layout_compute(&rq, &L);
        CHECK(L.npane == 0 && L.nitem == 0 && !L.bar_on,
              "layout: no sessions means no pane and no bar, even with tab_bar = always");
        lreq(&rq, LAYOUT_SPLIT, 100, 100, 8, 17, 0, 0, 2, 0, 2, BAR_AUTO);
        layout_compute(&rq, &L);
        CHECK(L.pane[0].x == 0 && L.pane[1].x == 50 && L.pane[0].cols == 6 && L.pane[0].rows == 4,
              "layout: padding 0 keeps the old edge-to-edge layout with adjacent panes");
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, -5, -5, 2, 0, 4, BAR_ALWAYS);
        layout_compute(&rq, &L);
        CHECK(L.bar_on && L.pane[0].x == 0 && L.pane[0].y == 17,
              "layout: a negative padding or gap is treated as 0");

        /* layout_grid_size(): the grid size EVERY session of a layout gets, drawn or not. src/main.c
           resizes all of its sessions to this, which is what makes a tab switch cost nothing (no resize,
           no SIGWINCH, no reflow on every switch) — so it has to be one size for the whole layout. */
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, 8, 8, 3, 2, 4, BAR_AUTO);
        layout_compute(&rq, &L);
        int lcols, lrows;
        layout_grid_size(&L, &lcols, &lrows);
        CHECK(lcols == L.pane[0].cols && lrows == L.pane[0].rows,
              "layout: grid_size names the grid of this layout's panes");
        int unequal = 0;
        for (int i = 0; i < L.npane; i++)
            if (L.pane[i].cols != lcols || L.pane[i].rows != lrows)
                unequal = 1;
        CHECK(!unequal, "layout: every pane of one layout has the same grid, so switching never resizes");
        lreq(&rq, LAYOUT_SPLIT, 936, 1002, 8, 17, 8, 8, 6, 5, 3, BAR_AUTO);
        layout_compute(&rq, &L);
        layout_grid_size(&L, &lcols, &lrows);
        CHECK(L.npane == 3 && lcols == L.pane[2].cols && lrows == L.pane[2].rows,
              "layout: a session outside the visible pane window still gets the size its pane will have");
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, 8, 8, 0, -1, 4, BAR_ALWAYS);
        layout_compute(&rq, &L);
        layout_grid_size(&L, &lcols, &lrows);
        CHECK(lcols == 0 && lrows == 0,
              "layout: no pane means no grid size, so a caller must not resize to it");

        /* layout_initial_size(): the window size src/main.c asks the compositor for at startup has to be
           big enough that layout_compute() gives back EXACTLY the configured cols x rows. The bar costs one
           cell ROW plus the gap — cell_h, not cell_w — and the round trip below is the only thing that can
           catch that typo, because the two halves live in one module the tests link while main.c is in none
           of them. A non-square cell (8x17) is what makes the difference visible. */
        int iw, ih;
        layout_initial_size(80, 30, 8, 17, 8, 8, BAR_ALWAYS, 0, 0, &iw, &ih);
        CHECK(iw == 80 * 8 + 16 && ih == 30 * 17 + 16 + 17 + 8,
              "layout: the startup window is the grid + the margin + one bar ROW and its gap");
        lreq(&rq, LAYOUT_TABS, iw, ih, 8, 17, 8, 8, 1, 0, 4, BAR_ALWAYS);
        layout_compute(&rq, &L);
        layout_grid_size(&L, &lcols, &lrows);
        CHECK(lcols == 80 && lrows == 30,
              "layout: a tab_bar = always startup really gets the configured cols x rows back");
        layout_initial_size(80, 30, 8, 17, 8, 8, BAR_AUTO, 0, 0, &iw, &ih);
        CHECK(iw == 80 * 8 + 16 && ih == 30 * 17 + 16,
              "layout: with tab_bar = auto the single starting session needs no bar row");
        lreq(&rq, LAYOUT_TABS, iw, ih, 8, 17, 8, 8, 1, 0, 4, BAR_AUTO);
        layout_compute(&rq, &L);
        layout_grid_size(&L, &lcols, &lrows);
        CHECK(lcols == 80 && lrows == 30,
              "layout: a tab_bar = auto startup gets the configured cols x rows back too");
        layout_initial_size(80, 30, 8, 8, 0, 0, BAR_ALWAYS, 0, 0, &iw, &ih);
        CHECK(iw == 640 && ih == 248,
              "layout: padding 0 with a square cell gives the edge-to-edge startup size");

        /* ---------- the file-manager band (Layout.fm) ----------
           936x1002 px window, 8x17 cells, padding 8, gap 8, one session, bar auto (off with one tab):
           the grid area is 920 x 986 px = 115 x 58 whole cells. The band asks for 75% of that height
           (the default), i.e. 739 px; the pixels that do not fill a whole cell stay at the BOTTOM of the
           band, exactly as they stay at the bottom of a grid. */
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, 8, 8, 1, 0, 4, BAR_AUTO);
        rq.fm_on = 1; rq.fm_preview_on = 1; rq.fm_size = 75;
        layout_compute(&rq, &L);
        CHECK(L.fm.on == 1 && L.fm.prev_on == 1, "fm layout: the band appears when it is asked for");
        CHECK(L.fm.list_y == 8 && L.fm.list_h == 986 * 75 / 100,
              "fm layout: the band takes fm_size PERCENT of the grid area, from its top");
        CHECK(L.fm.list_x == 8 && L.fm.list_w == (920 - 8) / 2,
              "fm layout: the browser takes the left half of the band");
        CHECK(L.fm.prev_x == 8 + L.fm.list_w + 8 && L.fm.prev_w == 920 - 8 - L.fm.list_w &&
              L.fm.prev_y == 8 && L.fm.prev_h == 986 * 75 / 100,
              "fm layout: the preview takes the right half, one gap between, same height");
        CHECK(L.fm.list_w + L.fm.prev_w + 8 == 920,
              "fm layout: the two panes and the gap fill the band exactly (remainder on the right)");
        CHECK(L.fm.list_cols == L.fm.list_w / 8 && L.fm.list_rows == (986 * 75 / 100) / 17 &&
              L.fm.prev_cols == L.fm.prev_w / 8 && L.fm.prev_rows == (986 * 75 / 100) / 17,
              "fm layout: each pane's cols x rows come from term_grid_geometry on its own rect");
        CHECK(L.pane[0].y == 8 + (986 * 75 / 100) + 8 && L.pane[0].h == 986 - (986 * 75 / 100) - 8,
              "fm layout: the terminal pane is what is left BELOW the band, gap between");
        CHECK(L.pane[0].x == 8 && L.pane[0].w == 920,
              "fm layout: the terminal pane keeps the full width - the band only costs height");
        layout_grid_size(&L, &lcols, &lrows);
        CHECK(lcols == L.pane[0].cols && lrows == L.pane[0].rows &&
              lrows == (986 - (986 * 75 / 100) - 8) / 17 && lcols == 115,
              "fm layout: every session is sized to the TERMINAL pane, not to the window");
        CHECK(layout_hit_fm(&L, L.fm.list_x + 1, L.fm.list_y + 1) == FM_HIT_LIST,
              "fm layout: a click in the browser pane is the browser");
        CHECK(layout_hit_fm(&L, L.fm.prev_x + 1, L.fm.prev_y + 1) == FM_HIT_PREVIEW,
              "fm layout: a click in the preview pane is the preview");
        CHECK(layout_hit_fm(&L, L.fm.list_x + L.fm.list_w + 1, L.fm.list_y + 1) == FM_HIT_NONE,
              "fm layout: the gap between the two panes belongs to neither");
        CHECK(layout_hit_fm(&L, L.fm.list_x + 1, L.fm.list_y + L.fm.list_h + 1) == FM_HIT_NONE,
              "fm layout: the gap between the band and the terminal pane belongs to neither");
        CHECK(layout_hit_fm(&L, L.fm.list_x + 1, L.pane[0].y + 1) == FM_HIT_NONE &&
              layout_hit_pane(&L, L.fm.list_x + 1, L.pane[0].y + 1) == 0,
              "fm layout: below the band is the terminal again, and only the terminal");
        CHECK(layout_hit_fm(&L, 100, 4) == FM_HIT_NONE,
              "fm layout: the top margin is not part of the band");

        /* The preview toggled away: the browser owns the whole band. */
        rq.fm_preview_on = 0;
        layout_compute(&rq, &L);
        CHECK(L.fm.on == 1 && L.fm.prev_on == 0 && L.fm.list_w == 920 && L.fm.prev_w == 0,
              "fm layout: with the preview off the browser takes the whole band");
        CHECK(layout_hit_fm(&L, 900, 100) == FM_HIT_LIST,
              "fm layout: and the whole band hit-tests as the browser");

        /* A share the window cannot give: it is clamped twice — to FM_SIZE_MAX as a request, then to
           whatever leaves the terminal its FM_TERM_MIN_ROWS. The shell is never starved. */
        rq.fm_preview_on = 1;
        rq.fm_size = 100000;
        layout_compute(&rq, &L);
        CHECK(L.fm.on == 1 && L.fm.list_h == 986 - 8 - FM_TERM_MIN_ROWS * 17 &&
              L.pane[0].h == FM_TERM_MIN_ROWS * 17,
              "fm layout: an oversized share shrinks so the terminal keeps its minimum rows");
        rq.fm_size = FM_SIZE_MAX;
        layout_compute(&rq, &L);
        CHECK(L.fm.on == 1 && L.fm.list_h == 986 - 8 - FM_TERM_MIN_ROWS * 17,
              "fm layout: even fm_size = FM_SIZE_MAX leaves the terminal its floor");
        rq.fm_size = FM_SIZE_MIN;
        layout_compute(&rq, &L);
        CHECK(L.fm.on == 1 && L.fm.list_h == 986 * FM_SIZE_MIN / 100 &&
              L.fm.list_rows == (986 * FM_SIZE_MIN / 100) / 17,
              "fm layout: the smallest allowed share is still drawn, at its own size");

        /* The whole point of a percent instead of a row count: the proportion survives a resize and a
           zoom. A taller window gives the band more pixels; a bigger font gives it the same pixels and
           therefore fewer rows. */
        rq.fm_size = 75;
        rq.win_h = 1502;                        /* grid area 1486 px instead of 986 */
        layout_compute(&rq, &L);
        CHECK(L.fm.on == 1 && L.fm.list_h == 1486 * 75 / 100,
              "fm layout: a taller window gives the band a taller share of the same percent");
        rq.win_h = 1002;
        rq.cell_h = 34;                         /* zoomed in */
        layout_compute(&rq, &L);
        CHECK(L.fm.on == 1 && L.fm.list_h == 986 * 75 / 100 &&
              L.fm.list_rows == (986 * 75 / 100) / 34,
              "fm layout: zooming keeps the same pixel share and hands back fewer rows");
        rq.cell_h = 17;

        /* A window too short for both: the band is dropped, exactly like the tab bar is. */
        rq.fm_size = 75;
        rq.win_h = 120;                       /* grid area 104 px: no room for 3+3 rows and a gap */
        layout_compute(&rq, &L);
        CHECK(L.fm.on == 0 && L.fm.list_h == 0,
              "fm layout: a window too short for band AND terminal drops the band (the terminal wins)");
        CHECK(L.pane[0].y == 8 && L.pane[0].h == 104 && L.pane[0].rows == 6,
              "fm layout: and the terminal pane gets the whole grid area back");
        CHECK(layout_hit_fm(&L, 100, 20) == FM_HIT_NONE,
              "fm layout: with the band off no point ever hit-tests as the file manager");

        /* The startup round trip with the band on. The band is a PERCENT, so the startup height has to be
           SOLVED, not added to: the terminal's rows*ch plus the gap must fit in the (100-pct)% the grid
           area keeps, i.e. grid_h = ceil((rows*ch + gap) * 100 / (100 - pct)). 30 rows at 17 px plus an
           8 px gap is 518 px out of 25%, so the grid area is 2072 px and the window 2088 px. */
        layout_initial_size(80, 30, 8, 17, 8, 8, BAR_AUTO, 1, 75, &iw, &ih);
        CHECK(iw == 80 * 8 + 16 && ih == 2072 + 16,
              "fm layout: the startup window is solved from the percent, not by adding band rows");
        lreq(&rq, LAYOUT_TABS, iw, ih, 8, 17, 8, 8, 1, 0, 4, BAR_AUTO);
        rq.fm_on = 1; rq.fm_preview_on = 1; rq.fm_size = 75;
        layout_compute(&rq, &L);
        layout_grid_size(&L, &lcols, &lrows);
        CHECK(lcols == 80 && lrows == 30,
              "layout: a file-manager startup gets the configured cols x rows back for the terminal pane");
        CHECK(L.fm.on == 1 && L.fm.list_h == 2072 * 75 / 100,
              "fm layout: and the band really did take its share of that window");
        /* The round trip must hold for every combination main.c can ask for: a bar that costs a row, a
           different percent, and no padding or gap at all. */
        layout_initial_size(80, 30, 8, 17, 8, 8, BAR_ALWAYS, 1, 40, &iw, &ih);
        lreq(&rq, LAYOUT_TABS, iw, ih, 8, 17, 8, 8, 1, 0, 4, BAR_ALWAYS);
        rq.fm_on = 1; rq.fm_preview_on = 1; rq.fm_size = 40;
        layout_compute(&rq, &L);
        layout_grid_size(&L, &lcols, &lrows);
        CHECK(lcols == 80 && lrows == 30 && L.bar_on == 1 && L.fm.on == 1,
              "layout: a bar + band startup at fm_size = 40 gets its cols x rows back too");
        layout_initial_size(80, 30, 8, 8, 0, 0, BAR_AUTO, 1, 50, &iw, &ih);
        lreq(&rq, LAYOUT_TABS, iw, ih, 8, 8, 0, 0, 1, 0, 4, BAR_AUTO);
        rq.fm_on = 1; rq.fm_preview_on = 1; rq.fm_size = 50;
        layout_compute(&rq, &L);
        layout_grid_size(&L, &lcols, &lrows);
        CHECK(lcols == 80 && lrows == 30,
              "layout: with no padding and no gap the band round trip still holds");
    }

    /* ---------- the shell's input line: term_at_prompt() (src/vt.c) ----------
       The file manager writes at the shell's prompt (`cd <dir>`, a selected path), and it must not do
       that on top of a command the user is halfway through. Kai-Term cannot tell a prompt from a prompt
       plus typed text by looking at the BYTES (the tty echoes typing, and that echo arrives as ordinary
       output), so what the model remembers is the cursor row as the application last printed it, and the
       echo window around what the terminal itself sent is what keeps that memory honest. These checks
       follow a realistic shell: prompt, echo, readline's own redraws (\r, \b \b, \x1b[K), Enter. */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    CHECK(term_at_prompt(&T) == 0, "prompt: before anything is printed nothing is known");
    feed("prompt$ ");
    CHECK(term_at_prompt(&T) == 1, "prompt: a prompt the shell just printed is untouched");
    type("ls");                             /* the tty echoes exactly these bytes back */
    CHECK(term_at_prompt(&T) == 0, "prompt: typed text makes the line dirty");
    type("\b \b\b \b");                     /* readline's erase: back, space, back — twice */
    CHECK(term_at_prompt(&T) == 1, "prompt: erasing everything makes it clean again (space == blank)");
    press("\r", "\r\n");                    /* Enter submits the line */
    feed("file1  file2\r\nprompt$ ");       /* the command's output, then the next prompt */
    CHECK(term_at_prompt(&T) == 1, "prompt: the next prompt after Enter is untouched");
    type("\x1b[A");                         /* Up: readline redraws a recalled command on the line */
    feed("\r\x1b[Kls -l");
    CHECK(term_at_prompt(&T) == 0, "prompt: a history recall is not an empty line");
    type("\x1b[B");                         /* Down: readline redraws the empty prompt */
    feed("\r\x1b[Kprompt$ ");
    CHECK(term_at_prompt(&T) == 1, "prompt: arrowing back down to an empty prompt is");
    type("\x1b[D");                         /* Left: the cursor moves, the text does not */
    CHECK(term_at_prompt(&T) == 0,
          "prompt: a cursor moved off the prompt position does not count as empty");
    type("\x1b[C");
    CHECK(term_at_prompt(&T) == 1, "prompt: and moving it back does");
    /* The anchor rule this depends on, stated: prompt_cx/prompt_cy are where the cursor sits when the line is
       EMPTY, and a burst that only moves the cursor must not redefine them. Before the input whitelist a
       movement key set input_pending and so kept the snapshot still by accident; now the rule is explicit and
       these checks say what happens if it is dropped. (CSI G moves the cursor WITHIN the row, which is the
       case here; CSI H would also move it to another row, and SS3 `ESC O H` / `ESC [ 1 ~` are not cursor keys
       in this parser at all — measured 2026-10-20, so a check must not lean on them.) */
    type("\x1b[1G");                        /* column 1: the row is untouched, the cursor is inside it */
    CHECK(T.cx == 0 && T.prompt_cx == 8 && T.prompt_cy == T.cy,
          "prompt: moving the cursor does not move the remembered cursor anchor");
    CHECK(term_at_prompt(&T) == 0, "prompt: and a cursor at column 1 of that prompt is not at the prompt");
    type("\x1b[9G");                        /* CSI G: back to the column the empty prompt ends at */
    CHECK(T.cx == 8 && term_at_prompt(&T) == 1, "prompt: and putting the cursor back restores it");
    /* A prompt that really IS redrawn still re-takes the anchor, cursor included — otherwise every later
       check on this row would compare against a stale column. */
    press("\r", "\r\n");
    feed("longer-prompt-here ");
    CHECK(T.prompt_cx == 19 && term_at_prompt(&T) == 1,
          "prompt: a genuinely new prompt re-takes the anchor at its own cursor column");
    type("\x1b[1G");
    CHECK(T.prompt_cx == 19 && term_at_prompt(&T) == 0,
          "prompt: and Home on the new prompt keeps the new anchor, not the old one");
    type("\x1b[20G");
    CHECK(term_at_prompt(&T) == 1, "prompt: the cursor back at the end of the new prompt is at the prompt");
    feed("\r\x1b[Kprompt$ ");
    CHECK(T.prompt_cx == 8 && term_at_prompt(&T) == 1,
          "prompt: a redrawn shorter prompt re-takes the anchor too");
    type("x");
    feed("\r\x1b[K\x1b[31mprompt$ \x1b[0m"); /* the same text redrawn in another colour */
    CHECK(term_at_prompt(&T) == 0, "prompt: the same text in another colour counts as changed");
    feed("\r\x1b[Kprompt$ ");
    CHECK(term_at_prompt(&T) == 1, "prompt: redrawn exactly as remembered, it counts as unchanged");

    /* The user's report, 2026-10-20: BackSpace on an EMPTY prompt killed the file manager's `cd` follow for
       the rest of the session. Measured (`.scratch/s89/bstoprobe`, real shells in a real Kai-Term pty): bash
       and sh answer such a key with nothing but a bell (0x07) and fish answers NOT AT ALL, so no burst was
       ever seen to prove the line empty and the echo window stayed shut. A movement or erase key cannot put
       text on a line that is already provably the bare prompt, so it does not open the window. */
    type("\x7f");                         /* BackSpace on an empty line */
    CHECK(T.input_pending == 0 && term_at_prompt(&T) == 1,
          "prompt: BackSpace on an EMPTY prompt leaves the line known (the user's report, 2026-10-20)");
    feed("\a");                           /* exactly what bash and sh send back: a bell, nothing else */
    CHECK(term_at_prompt(&T) == 1, "prompt: and that bell reply changes nothing");
    type("\x1b[3~");                      /* Delete */
    CHECK(T.input_pending == 0 && term_at_prompt(&T) == 1, "prompt: Delete on an empty prompt likewise");
    type("\x01");                         /* Ctrl+A: beginning of line */
    CHECK(T.input_pending == 0, "prompt: Ctrl+A is movement, not typing");
    feed("\r\x1b[Kprompt$ ");             /* readline repaints the line after it */
    CHECK(term_at_prompt(&T) == 1, "prompt: and the repaint after it leaves the line known");
    type("\x1b[1;5D");                    /* Ctrl+Left: the same movement key, in its modified form */
    CHECK(T.input_pending == 0, "prompt: a movement key with modifiers is still just movement");
    feed("\r\x1b[Kprompt$ ");
    CHECK(term_at_prompt(&T) == 1, "prompt: and the line is known again after that repaint");

    /* The whitelist stops exactly where a key could put characters on a line the application may not be
       showing — that is the s77 hazard (an application that does not echo at all), and it must stay shut. */
    type("\t");                           /* completion inserts text, or inserts nothing and shows nothing */
    CHECK(T.input_pending == 1,
          "prompt: Tab still opens the echo window — completion can insert text the row never shows");
    press("\r", "\r\n");
    feed("prompt$ ");
    type("\x19");                         /* Ctrl+Y yanks the kill buffer back onto the line */
    CHECK(T.input_pending == 1, "prompt: Ctrl+Y still opens it (yank inserts)");
    press("\r", "\r\n");
    feed("prompt$ ");
    type("\x12");                         /* Ctrl+R: history search, which redraws the line */
    CHECK(T.input_pending == 1, "prompt: Ctrl+R still opens it (search recalls a command)");
    press("\r", "\r\n");
    feed("prompt$ ");
    type("\x1b[");                        /* a truncated sequence: assume the worst */
    CHECK(T.input_pending == 1, "prompt: a truncated escape sequence opens it too");
    press("\r", "\r\n");
    feed("prompt$ ");
    type("rm");                           /* text first: the window is open, and a later erase key must not
                                             launder it — the row comparison alone would say "empty" */
    CHECK(T.input_pending == 1 && term_at_prompt(&T) == 0, "prompt: typed text opens it as before");
    type("\x7f");
    CHECK(T.input_pending == 1 && term_at_prompt(&T) == 0,
          "prompt: BackSpace AFTER typed text does not close the window the typing opened");

    /* A query answer is output, not typing: DA1/DSR/CPR go out through the pty but must not open an
       echo window, or every probe would stop the prompt being remembered. The line has to be submitted
       first: the checks above left an echo window open (the last thing typed was `rm`, and the erase key after
       it may not close one), and asserting `input_pending == 0` on top of that would test a state the program
       cannot be in. */
    press("\r", "\r\n");                    /* Enter submits — that is what closes the window */
    feed("prompt$ ");
    CHECK(term_at_prompt(&T) == 1 && T.input_pending == 0,
          "prompt: submitting the line closes the echo window");
    T.resp_len = 0;
    feed("\x1b[5n");
    CHECK(T.resp_len > 0 && T.input_pending == 0,
          "prompt: answering DSR is output — it does not open an echo window");
    CHECK(term_at_prompt(&T) == 1, "prompt: and a probe leaves the prompt known");

    /* What the file manager types is input too: a second injection has to be refused until the line is
       submitted, and a cd it ran has to leave the line clean for the next one. */
    send_in("'/tmp/x'");
    feed("'/tmp/x'");
    CHECK(term_at_prompt(&T) == 0, "prompt: a path the file manager typed makes the line dirty");
    send_in("cd /tmp\r");
    feed("\r\nprompt$ ");
    CHECK(term_at_prompt(&T) == 1, "prompt: and the cd it ran leaves the line clean again");

    /* An application is NOT obliged to echo. Measured 2026-10-19 against a real pty (`.scratch/s77/livegate`,
       `.scratch/s77/echochk`): interactive bash in Kai-Term's pty returned nothing at all for typed
       characters, so the row never changed and the line read as untouched — the file manager would have
       written `cd <dir>` onto a half-typed command and run the result. The echo window is what catches
       that: bytes sent and never submitted keep the line dirty whatever the application shows, and only
       the application putting the remembered prompt back on screen (or CR/LF/Ctrl+C) clears it. */
    send_in("rm -rf");                     /* nothing comes back: this application does not echo */
    CHECK(term_at_prompt(&T) == 0,
          "prompt: typed text with NO echo at all still makes the line dirty");
    feed("\r\x1b[K");                      /* the line is repainted empty — that is not the remembered prompt */
    CHECK(term_at_prompt(&T) == 0,
          "prompt: an empty line is not the remembered prompt either");
    feed("prompt$ ");                      /* the remembered prompt, cell for cell, at the same place */
    CHECK(term_at_prompt(&T) == 1 && T.input_pending == 0,
          "prompt: the remembered prompt redrawn closes the echo window");
    send_in("rm -rf");
    CHECK(term_at_prompt(&T) == 0, "prompt: and typed text makes it dirty again");
    send_in("\x03");                       /* Ctrl+C discards the line */
    CHECK(term_at_prompt(&T) == 1, "prompt: Ctrl+C discards the line and re-arms the gate");

    /* The alternate screen is never "at a prompt" while it is up, and it must not disturb the snapshot:
       the main grid is parked and comes back byte-for-byte, so the row the snapshot describes is the row
       that reappears. Re-taking the snapshot on leave (what s76 did, because the end-of-burst snapshot used
       to be taken from the ALTERNATE grid while the app was up) recorded the cursor where the user had left
       it, which made a half-typed command look like an EMPTY prompt — s79's clear-the-line rule would then
       have erased it. The snapshot now always describes the main grid. */
    type("\x1b[A");                         /* an arrow key: opens an echo window no CR will ever close */
    CHECK(T.input_pending == 1, "prompt: an arrow key leaves an echo window open");
    feed("\x1b[?1049h");
    CHECK(term_at_prompt(&T) == 0, "prompt: the alternate screen is not a prompt");
    feed("\x1b[2J\r\x1b[Hvim repaints the whole screen\x1b[?1049l");
    CHECK(T.prompt_valid == 1,
          "prompt: the snapshot survives the full-screen application — the main grid is parked and "
          "restored byte-for-byte, so it still describes the row now on screen");
    CHECK(term_at_prompt(&T) == 0,
          "prompt: a line that was dirty when the application started is STILL dirty when it exits — "
          "re-taking the snapshot on leave made a half-typed command look empty");
    type("\x1b[B");                         /* the echo that puts the cursor back on the prompt row */
    CHECK(T.input_pending == 0 && term_at_prompt(&T) == 1,
          "prompt: and the echo that restores the remembered row closes the window again");

    /* Scrolled back into history = the user is not looking at the prompt. The grid is 10 rows, so this
       needs output that actually reaches the bottom margin — two newlines do not scroll anything. The
       command that produced the output is submitted first, which is also what closes the echo window the
       arrow key above left open. */
    send_in("ls -l\r");
    for (int i = 0; i < 12; i++)
        feed("file0i\r\n");
    feed("prompt$ ");
    CHECK(T.g.live_off > 1, "prompt: the session has history to scroll into");
    CHECK(term_at_prompt(&T) == 1, "prompt: the prompt after that output is known again");
    T.g.scroll_off = T.g.live_off - 1;
    CHECK(term_at_prompt(&T) == 0, "prompt: scrolled back is not the prompt");
    T.g.scroll_off = T.g.live_off;
    CHECK(term_at_prompt(&T) == 1, "prompt: back at the live bottom it is");

    /* A resize moves and re-wraps rows; the clean/dirty answer has to survive it. */
    term_resize(&T, 12, 20);
    CHECK(term_at_prompt(&T) == 1, "prompt: a resize while clean stays clean");
    type("rm -rf ");
    CHECK(term_at_prompt(&T) == 0, "prompt: typed text is still dirty across a resize");
    term_resize(&T, 10, 20);
    CHECK(term_at_prompt(&T) == 0, "prompt: and a resize does not lose it");
    press("\r", "\r\n");
    feed("prompt$ ");
    CHECK(term_at_prompt(&T) == 1, "prompt: submitting the line ends the echo window");

    /* Two rows holding exactly the same text: the row content alone cannot say which one the prompt is,
       so the snapshot's own row (prompt_cy) is compared too. The cursor moves while an echo is
       outstanding, which is the only way a move does NOT re-take the reference. */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    feed("prompt$ \r\nprompt$ ");
    CHECK(term_at_prompt(&T) == 1, "prompt: the row the cursor sits on is the prompt");
    type("\x1b[A");                         /* up: identical text, different row */
    CHECK(T.cy == 0 && term_at_prompt(&T) == 0,
          "prompt: a cursor on an identical row above the prompt is not at the prompt");
    type("\x1b[B");
    CHECK(term_at_prompt(&T) == 1, "prompt: and back down on the prompt row it is");

    /* A reset (ESC c) frees what the old screen owned — the snapshot included — and the fresh terminal
       answers about its own blank screen instead of comparing against freed memory (LeakSanitizer and
       ASan both watch this in `make asan`). Written as two escapes on purpose: "\x1bc" is ONE hex escape
       (0x1bc, out of range for a char), not ESC followed by 'c'. */
    feed("\x1b" "c");
    CHECK(term_at_prompt(&T) == 1, "prompt: after a reset the fresh screen is described, not freed memory");
    CHECK(T.cx == 0 && T.cy == 0,
          "prompt: and it really was a reset — the cursor went home (the old \"\\x1bc\" byte would "
          "have printed a character and left it at column 1)");

    /* ---------- the same line WITH text on it: term_prompt_line() (src/vt.c) ----------
       The user's rule (s79): the prompt must ALWAYS end up in the directory the browser is looking at, so
       a navigation may NOT be refused because the line already holds text — Kai-Term clears the line, sends
       the `cd`, and puts the text back on the new prompt. That is only safe if the model can say EXACTLY
       what is on the line and where the line ends, which is what these checks pin. */
    term_destroy(&T);
    term_init(&T, 10, 40, 100);            /* wide enough that the checks below never wrap a line */
    char pline[FM_LINE_MAX];
    feed("prompt$ ");
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 1 && pline[0] == 0,
          "line: an untouched prompt is describable and holds nothing");
    type("rm -rf su");
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 1 && !strcmp(pline, "rm -rf su"),
          "line: the typed text is what comes back, so it can be put back afterwards");
    CHECK(term_at_prompt(&T) == 0,
          "line: that same line is still NOT the untouched prompt the old gate insisted on");
    type("\x1b[D");                        /* Left: the cursor moves, the text does not */
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 0,
          "line: a cursor parked mid-line is refused — erasing from there would leave the right half "
          "of the command behind for the cd to run on");
    type("\x1b[C");
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 1 && !strcmp(pline, "rm -rf su"),
          "line: describable again once the cursor is back at the end of the text");

    /* A shell that DECORATES the line: fish paints a grey autosuggestion immediately right of the cursor
       and puts the cursor back where it was (measured live 2026-10-19 with .scratch/s84/pdiag — typing
       `git com` draws `mit -m "Initial commit"` on the row, `ls -la /tmp` draws a `/`). Those cells are
       not part of the command line, and at the cell level they are indistinguishable from the right half of
       a command the user parked the cursor in front of — so the strict description must keep refusing
       (a caller about to erase the line cannot tell the two apart) while the question "did the text I typed
       land?" must still be answered yes (that caller erased nothing; its own text is by definition left of
       the cursor). This is why FMCD_RETYPING uses term_prompt_line_is() and FMCD_ERASING does not. */
    feed("\x1b[90mmit\x1b[0m\x1b[3D");     /* ghost painted, cursor moved back to the end of the typing */
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 0,
          "ghost: text right of the cursor makes the strict description refuse, ghost or not");
    CHECK(term_at_prompt(&T) == 0, "ghost: and the line is still not an untouched prompt");
    CHECK(term_prompt_line_is(&T, "rm -rf su") == 1,
          "ghost: term_prompt_line_is() still sees exactly what was typed — the ghost is right of the "
          "cursor, typed text never is");
    CHECK(term_prompt_line_is(&T, "rm -rf sumit") == 0,
          "ghost: the ghost text is never read as part of the typed text");
    CHECK(term_prompt_line_is(&T, "rm -rf") == 0,
          "ghost: a prefix of what is on the line is not a match either");
    type("\x1b[D");                        /* Left: a REAL character is now right of the cursor */
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 0 && term_prompt_line_is(&T, "rm -rf su") == 0,
          "ghost: a cursor parked mid-line still refuses both — the difference between a ghost and the "
          "right half of a command is not knowable, so the erasing caller refuses both");
    type("\x1b[C");

    /* ---------- the same line, asked the way the PATH KEY asks it: term_prompt_line_typed() (s95) ----------
       The path key inserts at the cursor and erases nothing, so the right-of-cursor half of the strict question
       is not a safety check for it — it is only a refusal. Measured 2026-10-20 (.scratch/s95/pathgate.c, real
       fish/bash/zsh in a real pty, the old machine and the new one in ONE binary): bash and zsh already typed
       the path after `cat `, and the ONLY refusals were the cases where cells right of the cursor hold
       something — fish's autosuggestion, or a cursor the user had moved back. These checks pin the difference
       between the two predicates, so the choice cannot drift back without a check going red. */
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 0,
          "path gate: (the setup) the strict predicate refuses this line — a ghost sits right of the cursor");
    CHECK(term_prompt_line_typed(&T, pline, sizeof pline) == 1 && !strcmp(pline, "rm -rf su"),
          "path gate: the non-strict predicate describes that same line, ghost ignored — this is the gate "
          "fm_send_path() uses");
    type("\x1b[D");                        /* Left: now a REAL character is right of the cursor */
    CHECK(term_prompt_line_typed(&T, pline, sizeof pline) == 1 && !strcmp(pline, "rm -rf s"),
          "path gate: a cursor parked mid-line is described up to the cursor (one character, `u`, is right of "
          "it) — the path is inserted where the user is typing, which is exactly what they asked for (s95)");
    type("\x1b[C");

    /* input_pending is deliberately NOT a reason to refuse here: the row comparison is the evidence, and
       the path Kai-Term typed itself leaves a perfectly describable line behind. */
    send_in("'/tmp/x'");
    CHECK(T.input_pending == 1 && term_at_prompt(&T) == 0,
          "line: bytes are outstanding, so the old gate refuses");
    feed("'/tmp/x'");
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 1 && !strcmp(pline, "rm -rf su'/tmp/x'"),
          "line: with an echo window open the text is still exactly describable");

    /* readline redrawing the whole line in another colour: the prompt prefix no longer matches the
       snapshot, so "everything between the prompt and the cursor" would include prompt characters. */
    feed("\r\x1b[K\x1b[31mprompt$ \x1b[0mrm -rf su'/tmp/x'");
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 0,
          "line: a prompt redrawn in another colour is refused, not half-captured");
    CHECK(term_prompt_line_typed(&T, pline, sizeof pline) == 0,
          "path gate: a redrawn prompt is refused by the loose predicate too — choosing the looser question "
          "does not make an undescribable line describable");

    /* A line that WRAPS onto the next row is not describable either: the snapshot holds one row, and the
       text the user typed now lives in two of them. */
    feed("\x1b" "c");
    term_resize(&T, 10, 20);
    feed("prompt$ ");
    type("a very long command that will not fit");
    CHECK(T.cy != T.prompt_cy,
          "line: the setup really did wrap the typed text onto another row");
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 0,
          "line: a wrapped input line is refused rather than half-read");
    CHECK(term_prompt_line_typed(&T, pline, sizeof pline) == 0,
          "path gate: a wrapped input line is refused by the path key as well");
    term_resize(&T, 10, 40);

    /* A buffer too small is a refusal, never a truncated command: losing half of what the user typed is
       worse than not following the browser. */
    feed("\x1b" "c");
    feed("prompt$ ");
    type("ls -l");
    char tiny[4];
    tiny[0] = 'Z'; tiny[1] = 0;
    CHECK(term_prompt_line(&T, tiny, sizeof tiny) == 0 && tiny[0] == 'Z',
          "line: too little room answers 0 and leaves the caller's buffer alone");
    CHECK(term_prompt_line(&T, pline, FM_LINE_MAX) == 1 && !strcmp(pline, "ls -l"),
          "line: and the same line fits in a real buffer");

    /* A wide glyph owns two cells but contributes ONE codepoint: the continuation cell is ATTR_WIDE with
       ch == 0, and re-typing must reproduce the character, not a space. */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    feed("p ");
    type("\xf0\x9f\x98\x80");              /* U+1F600: wide, one anchor cell + one continuation */
    CHECK(cellch(0, 2) == 0x1f600 && (cellattr(0, 3) & ATTR_WIDE) && cellch(0, 3) == 0,
          "line: the setup really did leave an ATTR_WIDE continuation cell with no codepoint in it");
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 1 && !strcmp(pline, "\xf0\x9f\x98\x80"),
          "line: a wide glyph contributes one codepoint, not its continuation cell");

    /* The alternate screen is owned by a full-screen application, not by a prompt — and it leaves the
       snapshot of the main grid alone, so the line is describable again the moment it is back. */
    feed("\x1b[?1049h");
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 0,
          "line: the alternate screen has no prompt line to describe");
    CHECK(term_prompt_line_typed(&T, pline, sizeof pline) == 0,
          "path gate: the path key never writes while a full-screen application owns the line");
    feed("\x1b[2J\r\x1b[Hthe application paints its own screen\x1b[?1049l");
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 1 && !strcmp(pline, "\xf0\x9f\x98\x80"),
          "line: back on the main screen the same line is describable again, text and all");

    /* Scrolled back into history is not the line the user is looking at. Two newlines scroll nothing in a
       10-row grid, so this first needs output that actually reaches the bottom margin.
       The prompt is printed by the shell (feed) and the emoji is TYPED (type): one feed("p \xf0\x9f\x98\x80")
       would write the whole row as application output, and term_parse() legitimately re-takes the snapshot at
       the end of such a burst, so the describable typed text really is "" then. That setup was the reason
       these two checks failed (measured 2026-10-19 with .scratch/s82/promptprobe) — the model was right. */
    press("\r", "\r\n");
    for (int i = 0; i < 12; i++)
        feed("filler\r\n");
    feed("p ");
    type("\xf0\x9f\x98\x80");
    CHECK(T.g.live_off > 0, "line: the grid has history to scroll into");
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 1 && !strcmp(pline, "\xf0\x9f\x98\x80"),
          "line: at the live bottom the new prompt line is describable");
    T.g.scroll_off = T.g.live_off - 1;
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 0,
          "line: scrolled back into history is not the input line");
    T.g.scroll_off = T.g.live_off;
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 1 && !strcmp(pline, "\xf0\x9f\x98\x80"),
          "line: and back at the live bottom it is again");

    /* The rule that broken setup tripped over, pinned so the next reader does not "fix" the model over it:
       a row the APPLICATION wrote whole IS the prompt, so it is describable and holds no typed text. */
    press("\r", "\r\n");
    for (int i = 0; i < 12; i++)
        feed("filler\r\n");
    feed("p \xf0\x9f\x98\x80");
    CHECK(T.input_pending == 0 && T.prompt_cx == T.cx && T.prompt_cy == T.cy,
          "line: an output burst with nothing outstanding re-takes the snapshot at its own end");
    CHECK(term_prompt_line(&T, pline, sizeof pline) == 1 && pline[0] == 0,
          "line: so that row is describable and the typed text on it is empty — correctly");

    /* ---------- the clear -> `cd` -> re-populate sequence, as the model sees it ----------
       src/main.c is in no test binary, so the machine itself is covered by the live key-injection harness,
       not here. What IS testable — and what the machine is built entirely out of — is the pair of questions
       it asks the model at every step. These replay its exact writes (Ctrl+U, `cd <dir>\r`, the captured
       text with no CR) against the real parser, so a change to the snapshot rules that would break the
       feature fails a check instead of breaking it silently. */
    term_destroy(&T);
    term_init(&T, 10, 40, 100);
    char cap[FM_LINE_MAX];
    feed("prompt$ ");
    type("git com");                       /* the user is mid-command when they navigate the browser */
    CHECK(term_prompt_line(&T, cap, sizeof cap) == 1 && !strcmp(cap, "git com"),
          "seq: the text to put back is captured BEFORE anything is erased");
    CHECK(term_at_prompt(&T) == 0,
          "seq: and the line is not empty, so the cd cannot be written yet");
    send_in("\025");                        /* fm_send_cd(): Ctrl+U, erase backwards to the start */
    CHECK(T.input_pending == 1 && term_at_prompt(&T) == 0,
          "seq: Ctrl+U opens an echo window — the erase is not believed until the shell shows it");
    feed("\r\x1b[Kprompt$ ");               /* readline/fish erasing the line, on the SAME row */
    CHECK(T.input_pending == 0,
          "seq: the burst that puts the remembered prompt back CLOSES the window — that is the proof");
    CHECK(term_at_prompt(&T) == 1,
          "seq: the erase is proven, so the machine may now write the cd");
    CHECK(term_prompt_line(&T, cap, sizeof cap) == 1 && cap[0] == 0,
          "seq: and the line really is empty now");
    send_in("cd /tmp/x\r");                 /* fm_cd_write() */
    CHECK(T.input_pending == 0,
          "seq: the CR in the cd submits the line, so it leaves no echo window open behind it");
    feed("cd /tmp/x\r\nnewprompt$ ");       /* the tty echo, then the new prompt */
    CHECK(term_at_prompt(&T) == 1,
          "seq: after the shell's next burst the new prompt is the known line again");
    CHECK(term_prompt_line(&T, cap, sizeof cap) == 1 && cap[0] == 0,
          "seq: the new prompt holds nothing, so the re-type starts from a clean line");
    send_in("git com");                     /* FMCD_RETYPING: the captured text, and NO CR */
    CHECK(term_at_prompt(&T) == 0,
          "seq: while the re-typed text is outstanding the line is not empty again");
    feed("git com");
    CHECK(term_prompt_line(&T, cap, sizeof cap) == 1 && !strcmp(cap, "git com"),
          "seq: the finished line reads exactly as it did before — the user's text is back, nothing ran");

    /* THE BUG THESE PIN (user report, 2026-10-19): a SECOND navigation on top of the text Kai-Term had just
       re-typed did not erase it — the prompt ended up `Some text cd '/'` and fish ran it. Cause, measured
       with real fish (`.scratch/s86/follow2`): the burst right after the re-type painted NOTHING on the
       input row (fish repainting prompt chrome — OSC 7, OSC 0, DECSET), so the row still equalled the
       snapshot, and the end-of-burst rule read that STEADY STATE as "the application put the prompt back,
       the line is provably empty". It closed the echo window over bytes that had not been echoed yet, and
       every later burst then RE-TOOK the snapshot — including the one carrying the echo of Kai-Term's own
       text — so a row holding `git com` began answering term_at_prompt() as EMPTY and the next navigation
       took the fast path. A match that was already a match is not evidence. */
    feed("\x1b]7;file://host/dir\x07\x1b[?2004h\x1b[?2031h");   /* chrome only: the row is untouched */
    CHECK(T.input_pending == 1,
          "seq: a burst that paints NOTHING on the input row does not close the echo window");
    CHECK(T.prompt_cx < T.cx && term_prompt_line(&T, cap, sizeof cap) == 1 && !strcmp(cap, "git com"),
          "seq: and the snapshot is not re-taken over the echo — the typed text is still describable");
    CHECK(term_at_prompt(&T) == 0,
          "seq: so the line still reads as NOT empty, which is what stops the next navigation appending");
    send_in("\025");                        /* the SECOND navigation erases what was just re-typed */
    CHECK(T.input_pending == 1,
          "seq: and that second erase opens an echo window of its own");
    feed("\r\x1b[Knewprompt$ ");
    CHECK(T.input_pending == 0 && term_at_prompt(&T) == 1,
          "seq: the burst that repaints the remembered prompt proves it — the follow is idempotent");

    /* fish decorates the line: an autosuggestion is painted in grey immediately right of the cursor and is
       NOT part of the command line. Measured with real fish 2026-10-19 — `git com` gets
       `mit -m "Initial commit"` drawn there, `ls -la /tmp` gets a `/`. The user's ruling (2026-10-19) is to
       follow the browser through it, which is safe because the write is gated on the erase PROOF, not on
       the capture. */
    term_destroy(&T);
    term_init(&T, 10, 40, 100);
    feed("prompt$ ");
    type("git com");
    feed("\x1b[90mmit\x1b[0m\x1b[3D");       /* the ghost, then the cursor back where the user left it */
    CHECK(term_prompt_line(&T, cap, sizeof cap) == 0,
          "ghost: the strict description still refuses a ghosted row — at cell level that ghost is "
          "indistinguishable from the right half of a command the cursor was parked in front of");
    CHECK(term_prompt_line_typed(&T, cap, sizeof cap) == 1 && !strcmp(cap, "git com"),
          "ghost: the capture for the erase takes the typed text and ignores the ghost");
    CHECK(term_prompt_line_is(&T, "git com") == 1,
          "ghost: and the re-type confirmation is not fooled by it either");
    send_in("\025");
    feed("\r\x1b[Kprompt$ ");               /* fish's Ctrl+U kills the buffer; the ghost goes with it */
    CHECK(T.input_pending == 0 && term_at_prompt(&T) == 1,
          "ghost: the erase is still PROVEN before any cd may be written — that is what makes following "
          "a ghost safe");

    /* THE PRICE of it, pinned so nobody pretends otherwise: a cursor parked in the middle of a command.
       Ctrl+U erases backwards from the cursor, so the right half survives, the whole-row proof fails, and
       the machine drops the sequence (logging what it had captured) instead of writing. Measured against
       real fish the same way (`.scratch/s86/follow2`, `m.log`). */
    term_destroy(&T);
    term_init(&T, 10, 40, 100);
    feed("prompt$ ");
    type("git commit -m x");
    type("\x1b[D");                          /* Left: the cursor is now between the space and the x */
    CHECK(term_prompt_line_typed(&T, cap, sizeof cap) == 1 && !strcmp(cap, "git commit -m "),
          "mid-line: the capture sees only the text left of the cursor");
    send_in("\025");
    feed("\r\x1b[Kprompt$ x");              /* the right half is all that is left on the line */
    CHECK(T.input_pending == 1 && term_at_prompt(&T) == 0,
          "mid-line: the whole-row proof fails, so the machine drops the sequence — a cd is never written "
          "in front of the half that survived");

    /* The flow STARTS by opening the band, and the band costs the terminal its rows (measured live:
       154x61 -> 154x49). A resize used to throw the description of a DIRTY line away, which made the whole
       sequence impossible — so a resize that re-broke nothing must keep it, and only that one. */
    term_destroy(&T);
    term_init(&T, 10, 40, 100);
    feed("prompt$ ");
    type("git com");
    CHECK(term_prompt_line(&T, cap, sizeof cap) == 1 && !strcmp(cap, "git com"),
          "seq: the line is describable before the band opens");
    term_resize(&T, 6, 40);                 /* same width, fewer rows: exactly what opening the band does */
    CHECK(T.prompt_valid == 1 && T.prompt_cy == T.cy,
          "seq: a height-only resize moves the snapshot with its row instead of discarding it");
    CHECK(term_prompt_line(&T, cap, sizeof cap) == 1 && !strcmp(cap, "git com"),
          "seq: and the typed text is still exactly describable after it");
    term_resize(&T, 6, 30);                 /* a width change re-wraps lines: nothing honest survives */
    CHECK(term_prompt_line(&T, cap, sizeof cap) == 0,
          "seq: a WIDTH change still invalidates the description of a dirty line — refusing is correct");
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    feed("prompt$ ");
    type("a long command that wraps onto row two");
    CHECK(T.cy != T.prompt_cy, "seq: the setup wrapped the typed text onto another row");
    term_resize(&T, 6, 20);
    CHECK(term_prompt_line(&T, cap, sizeof cap) == 0,
          "seq: a wrapped input line stays refused across a resize — the cursor was not on the snapshot row");

    /* THE SAFETY PROPERTY, headless: if the erase does not happen, the `cd` is never sent. A line editor
       that binds Ctrl+U to something else, or an application that shows nothing, leaves the echo window
       open and the row unmatched, so every gate the machine waits behind stays false. */
    term_destroy(&T);
    term_init(&T, 10, 40, 100);
    feed("prompt$ ");
    type("git com");
    send_in("\025");
    feed("\r\x1b[Kprompt$ git ");           /* a PARTIAL erase (delete-word), not erase-to-start */
    CHECK(T.input_pending == 1 && term_at_prompt(&T) == 0,
          "seq: a partial erase keeps the window open and the line not empty — no cd is ever sent");
    CHECK(term_prompt_line(&T, cap, sizeof cap) == 1 && !strcmp(cap, "git "),
          "seq: the half-erased line IS describable, which is exactly why the cd is gated on "
          "term_at_prompt() and never on term_prompt_line()");
    feed("\r\x1b[Kprompt$ ");               /* and a shell that never answers at all */
    CHECK(T.input_pending == 0 && term_at_prompt(&T) == 1,
          "seq: the prompt redrawn is what finally proves the line empty (the machine's deadline "
          "covers the case where it never is)");

    /* The one thing that DOES break the proof, pinned so it is documented rather than discovered: the
       snapshot names a ROW. If the shell prints a line of its own between the erase and its next prompt
       (a background job finishing, a SIGCHLD notice), the redrawn prompt sits on another row, the model
       cannot prove the line is empty, and the machine times out and drops the sequence — logging what was
       on the line. Failing that way is the safe direction; it is not a licence to guess. */
    term_destroy(&T);
    term_init(&T, 10, 40, 100);
    feed("prompt$ ");
    type("git com");
    send_in("\025");
    feed("[1] done\r\n\r\x1b[Kprompt$ ");   /* a message, then the prompt redrawn on the NEW row */
    CHECK(T.g.scroll_off == T.g.live_off && T.cy != T.prompt_cy,
          "seq: the setup moved the prompt to another row (the snapshot still names the old one)");
    CHECK(T.input_pending == 1 && term_at_prompt(&T) == 0,
          "seq: so the erase is NOT proven and the machine will drop the sequence rather than send a cd");

    /* ---------- graphics: the kitty image protocol (src/vt.c) ----------
       Headless on purpose: the decoded PIXELS live in the model, so the parser, the chunk assembly,
       the placement math and the lifetime rules are all testable without a display — render.c only
       turns KImage.rgba into a GL texture. Cell px 8x16 is what src/main.c pushes from the font
       metrics, and the protocol sizes pictures in PIXELS, so every span below depends on it. */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    term_set_cell_px(&T, 8, 16);
    ImgView view[16];
    int nv;

    /* --- a=T transmits, displays, and moves the cursor below the picture --- */
    uint8_t px4[16];
    fill_rgba(px4, 2, 2, 0xff, 0x80, 0x40, 0xff);
    gfx_feed(&T, "a=T,i=1,q=2,f=32,s=2,r=2,m=0", px4, sizeof px4);
    CHECK(term_image_count(&T) == 1, "graphics: a=T creates one image");
    KImage *im = img_by_id(&T, 1);
    CHECK(im && im->w == 2 && im->h == 2, "graphics: raw f=32 with s=/r= gives the pixel size");
    CHECK(im && im->shown, "graphics: a=T marks the image as displayed");
    CHECK(im && im->rgba[0] == 0xff && im->rgba[1] == 0x80 && im->rgba[3] == 0xff,
          "graphics: an opaque texel survives premultiplication unchanged");
    CHECK(T.cy == 1, "graphics: a=T moves the cursor past the picture's rows");
    nv = term_visible_images(&T, view, 16);
    CHECK(nv == 1 && view[0].crow == 0 && view[0].ccol == 0 &&
          view[0].crows == 1 && view[0].ccols == 1,
          "graphics: a 2x2 px picture covers one cell of an 8x16 grid");

    /* --- premultiplication, the rule the no-blending shader depends on ---
       The +255 rounding s68 used made (255*255 + 255)/255 == 256, and the uint8_t cast turned a fully
       opaque WHITE pixel BLACK. The s68 probe missed it because its sample pixel was never white. */
    uint8_t opaque_white[4] = { 0xff, 0xff, 0xff, 0xff };
    gfx_feed(&T, "a=T,i=2,q=2,f=32,s=1,r=1,m=0", opaque_white, 4);
    im = img_by_id(&T, 2);
    CHECK(im && im->rgba[0] == 0xff && im->rgba[1] == 0xff && im->rgba[2] == 0xff &&
          im->rgba[3] == 0xff, "graphics: a premultiplied opaque white texel stays white");
    uint8_t half_red[4] = { 0xff, 0x00, 0x00, 0x80 };
    gfx_feed(&T, "a=T,i=3,q=2,f=32,s=1,r=1,m=0", half_red, 4);
    im = img_by_id(&T, 3);
    CHECK(im && im->rgba[0] == 0x80 && im->rgba[3] == 0x80,
          "graphics: texels are premultiplied by their own alpha");

    /* --- chunked transmission: only the first chunk carries the control block (timg's shape) --- */
    uint8_t px64[64];
    fill_rgba(px64, 4, 4, 0x10, 0x20, 0x30, 0xff);
    gfx_feed(&T, "a=T,i=4,q=2,f=32,s=4,r=4,m=1", px64, 32);
    CHECK(term_image_count(&T) == 3, "graphics: an m=1 chunk is not an image yet");
    gfx_feed(&T, "q=2,m=0", px64 + 32, 32);          /* continuation: no a=, no i= */
    CHECK(term_image_count(&T) == 4, "graphics: the closing chunk completes the image");
    im = img_by_id(&T, 4);
    CHECK(im && im->w == 4 && im->h == 4, "graphics: the chunks assembled into one payload");
    CHECK(im && im->rgba[60] == 0x10 && im->rgba[63] == 0xff,
          "graphics: the continuation chunk's bytes are in the payload");

    /* --- a=t stores without displaying; a=p places it later --- */
    gfx_feed(&T, "a=t,i=5,q=2,f=32,s=2,r=2,m=0", px4, sizeof px4);
    CHECK(term_image_count(&T) == 5, "graphics: a=t stores the image");
    CHECK(term_visible_images(&T, view, 16) == 4, "graphics: an unplaced picture is never drawn");
    feed("\x1b[5;3H");
    T.resp_len = 0;
    gfx_feed(&T, "a=p,i=5,q=1", NULL, 0);
    CHECK(T.cy == 4 && T.cx == 2, "graphics: a=p places without moving the cursor");
    nv = term_visible_images(&T, view, 16);
    int vi = view_of(view, nv, 5);
    CHECK(nv == 5 && vi >= 0 && view[vi].crow == 4 && view[vi].ccol == 2,
          "graphics: a=p places the stored picture at the cursor");
    CHECK(strstr(T.resp, "\x1b_G:OK;i=5") != NULL, "graphics: q=1 answers OK");

    /* --- re-transmitting to an id replaces that picture instead of stacking another one ---
       Measured with timg 1.6.3 -V (see AGENT.md s70): a four-frame gif alternates between TWO ids, so
       three of its four frames are transmissions to an id that already exists. Treating every
       transmission as new leaves a trail of pictures behind an animation. */
    int n_before = term_image_count(&T);
    gfx_feed(&T, "a=T,i=11,q=2,f=32,s=1,r=1,m=0", opaque_white, 4);
    CHECK(term_image_count(&T) == n_before + 1,
          "graphics: the first transmission to a fresh id adds a picture");
    im = img_by_id(&T, 11);
    int ver = im ? im->version : 0;
    int cy_before = T.cy;
    gfx_feed(&T, "a=T,i=11,q=2,f=32,s=1,r=1,m=0", half_red, 4);
    CHECK(term_image_count(&T) == n_before + 1,
          "graphics: a=T to an id that exists does not add a second picture");
    im = img_by_id(&T, 11);
    CHECK(im && im->version == ver + 1,
          "graphics: the replacement bumps the version the renderer watches");
    CHECK(im && im->rgba[0] == 0x80 && im->rgba[3] == 0x80,
          "graphics: the id now holds the new pixels, not the old ones");
    CHECK(T.cy == cy_before + 1,
          "graphics: a=T still moves the cursor past the picture it re-placed");
    int64_t doc_row_after = im ? im->doc_row : -1;
    int cy_after = T.cy;
    gfx_feed(&T, "a=t,i=11,q=2,f=32,s=1,r=1,m=0", opaque_white, 4);
    im = img_by_id(&T, 11);
    CHECK(term_image_count(&T) == n_before + 1 && im && im->doc_row == doc_row_after &&
          T.cy == cy_after,
          "graphics: a=t replaces the data without moving the picture or the cursor");

    /* --- PNG is the format timg really sends (f=100), raw or zlib-wrapped --- */
    feed("\x1b[2J\x1b[H");
    gfx_feed(&T, "a=T,i=6,q=2,f=100,m=0", PNG8, sizeof PNG8);
    im = img_by_id(&T, 6);
    CHECK(im && im->w == 8 && im->h == 8, "graphics: a PNG payload decodes to its own size");
    CHECK(im && im->rgba[4] == 30 && im->rgba[5] == 0 && im->rgba[6] == 20 && im->rgba[7] == 0xff,
          "graphics: the PNG's second pixel comes through");
    gfx_feed(&T, "a=T,i=7,q=2,f=100,m=0", PNG8_ZLIB, sizeof PNG8_ZLIB);
    im = img_by_id(&T, 7);
    CHECK(im && im->w == 8 && im->h == 8, "graphics: a zlib-wrapped payload is inflated first");

    /* --- an explicit c= with no r= derives the height from the picture's aspect ratio --- */
    feed("\x1b[2J\x1b[H");
    gfx_feed(&T, "a=T,i=8,q=2,f=100,c=4,m=0", PNG8, sizeof PNG8);
    nv = term_visible_images(&T, view, 16);
    vi = view_of(view, nv, 8);
    CHECK(vi >= 0 && view[vi].ccols == 4 && view[vi].crows == 2,
          "graphics: c=4 of an 8x8 px picture in 8x16 cells spans 4x2 cells");
    CHECK(T.cy == 2, "graphics: the cursor moves past the derived height, not the pixel height");

    /* --- z= decides the draw order, and a negative z= must parse --- */
    feed("\x1b[2J\x1b[H");
    gfx_feed(&T, "a=T,i=9,q=2,f=32,s=2,r=2,z=5,m=0", px4, sizeof px4);
    feed("\r\n");
    gfx_feed(&T, "a=T,i=10,q=2,f=32,s=2,r=2,z=-5,m=0", px4, sizeof px4);
    nv = term_visible_images(&T, view, 16);
    int z_hi = view_of(view, nv, 9), z_lo = view_of(view, nv, 10);
    CHECK(z_hi >= 0 && z_lo >= 0 && z_lo < z_hi, "graphics: z=-5 is drawn before z=5");

    /* --- a=d deletes --- */
    int n_alive = term_image_count(&T);
    gfx_feed(&T, "a=d,i=9", NULL, 0);
    CHECK(img_by_id(&T, 9) == NULL, "graphics: a=d,i= deletes that image");
    CHECK(term_image_count(&T) == n_alive - 1, "graphics: exactly one image went away");
    gfx_feed(&T, "a=d,d=A", NULL, 0);
    CHECK(term_image_count(&T) == 0, "graphics: a=d,d=A deletes every image");

    /* --- a cursor left at the right margin places the picture at the start of the next row ---
       Real `timg -p k` output hit exactly this (s69): its own text had filled a line, so the cursor
       was in the pending-wrap state, and the picture was anchored at col == cols — decoded fine and
       never drawn. */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    term_set_cell_px(&T, 8, 16);
    feed("01234567890123456789");           /* exactly 20 columns */
    CHECK(T.cx == 20, "graphics: the cursor is in the pending-wrap state after a full line");
    gfx_feed(&T, "a=T,i=1,q=2,f=32,s=2,r=2,m=0", px4, sizeof px4);
    nv = term_visible_images(&T, view, 16);
    CHECK(nv == 1 && view[0].crow == 1 && view[0].ccol == 0,
          "graphics: a picture placed at the right margin starts the next row");

    /* --- a picture scrolls with its text, leaves the screen, and comes back from history --- */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    term_set_cell_px(&T, 8, 16);
    for (int i = 0; i < 5; i++)
        feed("text\r\n");
    gfx_feed(&T, "a=T,i=1,q=2,f=32,s=2,r=2,m=0", px4, sizeof px4);   /* anchored to row 5 */
    for (int i = 0; i < 40; i++)
        feed("more\r\n");
    CHECK(term_image_count(&T) == 1, "graphics: a scrolled-out picture is kept, not dropped");
    CHECK(term_visible_images(&T, view, 16) == 0, "graphics: a picture above the viewport is not drawn");
    T.g.scroll_off = 0;                       /* the user scrolls to the oldest line */
    nv = term_visible_images(&T, view, 16);
    CHECK(nv == 1 && view[0].crow == 5, "graphics: it comes back at its own history row");
    for (int i = 0; i < 200; i++)
        feed("more\r\n");
    CHECK(term_image_count(&T) == 0, "graphics: a picture whose rows were evicted is freed");

    /* --- CSI 3 J erases the history the picture was anchored in --- */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    term_set_cell_px(&T, 8, 16);
    for (int i = 0; i < 5; i++)
        feed("text\r\n");
    gfx_feed(&T, "a=T,i=1,q=2,f=32,s=2,r=2,m=0", px4, sizeof px4);
    for (int i = 0; i < 40; i++)
        feed("more\r\n");
    feed("\x1b[3J");
    CHECK(term_image_count(&T) == 0, "graphics: CSI 3 J drops pictures anchored in the erased history");

    /* --- reflow keeps a picture attached to the text it was printed next to --- */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    term_set_cell_px(&T, 8, 16);
    for (int i = 0; i < 5; i++) {
        char line[32];
        snprintf(line, sizeof line, "%d_________________\r\n", i);   /* exactly 20 columns */
        feed(line);
    }
    gfx_feed(&T, "a=T,i=1,q=2,f=32,s=2,r=2,m=0", px4, sizeof px4);   /* anchored to row 5 */
    term_resize(&T, 10, 10);                  /* every line now wraps to two rows */
    int last_text = -1;
    for (int y = 0; y < T.g.rows; y++) {
        row_text(y, row);
        int blank = 1;
        for (int x = 0; x < T.g.cols; x++)
            if (row[x] != ' ') { blank = 0; break; }
        if (!blank)
            last_text = y;
    }
    nv = term_visible_images(&T, view, 16);
    CHECK(nv == 1 && view[0].crow == last_text + 1,
          "graphics: after a re-wrap the picture still sits on the row after its line");

    /* --- DCS is discarded: sixel output must not be painted into the grid as text --- */
    feed("\x1b[2J\x1b[H");
    feed("\x1bPq0;1;2;1;0,0,0.FF.FF\x1b\\");
    row_text(0, row);
    CHECK(row[0] == ' ', "graphics: a DCS (sixel) sequence never reaches the grid");
    feed("A");
    CHECK(cellch(0, 0) == 'A', "graphics: the parser is back on the ground after a DCS");
    feed("\x1b_XYZ garbage\x1b\\B");
    CHECK(cellch(0, 1) == 'B' && cellch(0, 0) == 'A', "graphics: a non-graphics APC is discarded too");

    /* --- the pixel-geometry queries a tool falls back to when it cannot read the PTY winsize --- */
    T.resp_len = 0;
    feed("\x1b[16t");
    CHECK(strstr(T.resp, "\x1b[6;16;8t") != NULL, "graphics: CSI 16 t answers the cell size in px");
    T.resp_len = 0;
    feed("\x1b[14t");
    CHECK(strstr(T.resp, "\x1b[4;160;80t") != NULL, "graphics: CSI 14 t answers the text area in px");
    term_set_cell_px(&T, 0, 0);
    T.resp_len = 0;
    feed("\x1b[16t");
    CHECK(T.resp_len == 0, "graphics: no cell size known means no answer, like any other terminal");

    /* --- device attributes / status report: what an application reads to decide what to send --- */
    T.resp_len = 0;
    feed("\x1b[c");
    CHECK(strcmp(T.resp, "\x1b[?64;1;2;6;7;12;22c") == 0,
          "graphics: DA1 answers with no graphics capability");
    CHECK(strstr(T.resp, ";4;") == NULL && strstr(T.resp, ";16;") == NULL &&
          strstr(T.resp, ";18;") == NULL,
          "graphics: DA1 must not claim sixel/ReGIS — measured, `;4` makes timg 1.6.3 emit sixel, which "
          "the DCS discard then throws away, so the picture vanishes instead of falling back to blocks");
    T.resp_len = 0;
    feed("\x1b[>c");
    CHECK(strcmp(T.resp, "\x1b[>0;0;0c") == 0, "graphics: DA2 (ESC[>c) is answered");
    T.resp_len = 0;
    feed("\x1b[5n");
    CHECK(strcmp(T.resp, "\x1b[0n") == 0, "graphics: CSI 5 n (DSR) is answered with ESC[0n");
    T.resp_len = 0;
    feed("\x1b[6n");
    CHECK(T.resp_len > 0 && strstr(T.resp, "R") != NULL, "graphics: CSI 6 n still reports the cursor");

    /* Left on purpose: the term_destroy below must free the images and the APC buffers, and the same
       sources are built as bin/test_vt_asan and run under LeakSanitizer by `make asan`. */
    term_set_cell_px(&T, 8, 16);
    gfx_feed(&T, "a=T,i=2,q=2,f=32,s=2,r=2,m=0", px4, sizeof px4);
    CHECK(term_image_count(&T) == 2 && img_by_id(&T, 2) != NULL,
          "graphics: two pictures are alive for term_destroy to free (the reflow one and this one)");

    term_destroy(&T);

    /* ---------- shell integration: Kai-Term's APC channel + src/shellint.c ----------
       The file manager can ask the shell to change its OWN directory instead of Kai-Term typing `cd` at the
       prompt. Kai-Term only prefers that path once THIS session's shell announced itself, and only believes
       it worked when the shell confirms the directory it entered. These checks pin the model half; the
       end-to-end half (a real fish and a real bash in a real pty) is the harness in .scratch/s90/. */
    term_init(&T, 10, 20, 100);
    CHECK(T.shell_int == SHELLINT_NONE && T.shell_cd_ack[0] == 0,
          "integration: nothing is known about the shell until it announces itself");
    feed("\x1b_K;kai-term;integration;fish\x1b\\");
    CHECK(T.shell_int == SHELLINT_FISH, "integration: fish's announcement is believed");
    feed("\x1b_K;kai-term;cd;/etc\x1b\\");
    CHECK(strcmp(T.shell_cd_ack, "/etc") == 0, "integration: the cd ack is kept for main.c to compare");
    term_shell_cd_ack_clear(&T);
    CHECK(T.shell_cd_ack[0] == 0,
          "integration: the ack is cleared before a new request, or an old ack answers for it");
    feed("\x1b_K;kai-term;cd;/etc\x1b\\");   /* put it back: the three checks below need an ack to exist */
    feed("\x1b_K;kai-term;integration;bash\x1b\\");
    CHECK(T.shell_int == SHELLINT_BASH, "integration: a second announcement replaces the kind");
    feed("\x1b_K;kai-term;integration;zsh\x1b\\");
    CHECK(T.shell_int == SHELLINT_ZSH, "integration: zsh is a known kind");
    feed("\x1b_K;kai-term;integration;tcsh\x1b\\");
    CHECK(T.shell_int == SHELLINT_ZSH,
          "integration: an unknown kind changes nothing — the typed path is still what runs");
    feed("\x1b_K;kai-term;nothing\x1b\\");
    CHECK(T.shell_int == SHELLINT_ZSH && strcmp(T.shell_cd_ack, "/etc") == 0,
          "integration: an unknown message is ignored, not guessed at");
    feed("\x1b" "_Knot-for-us;kai-term;cd;/tmp\x1b\\");
    CHECK(strcmp(T.shell_cd_ack, "/etc") == 0,
          "integration: another program's APC that starts with 'K' is not read as ours");
    feed("\x1b_Gi=1,a=T,q=2,f=32,s=4,r=1,m=0;\x1b\\");
    CHECK(T.shell_int == SHELLINT_ZSH,
          "integration: the graphics APC (kind G) still goes to the graphics path");
    /* A reset memsets Term. The announcement happens ONCE, at shell startup, so it must survive — losing it
       would silently drop the feature for the rest of the session after any program sent ESC c. The prompt
       snapshot right next to it must NOT survive, so the two are checked together, with a real snapshot
       taken first (checking prompt_valid on a session that never printed a prompt would prove nothing). */
    feed("prompt$ ");
    CHECK(T.prompt_valid == 1 && T.shell_int == SHELLINT_ZSH,
          "integration: a prompt is snapshotted while the announcement is live");
    feed("\x1b" "c");        /* split so the C compiler does not read \x1bc as one hex escape */
    CHECK(T.shell_int == SHELLINT_ZSH && strcmp(T.shell_cd_ack, "/etc") == 0,
          "integration: a reset (ESC c) keeps the announcement, unlike the prompt snapshot");
    /* What "invalidates" means here: the reset throws the remembered `prompt$ ` row away, and the
       end-of-burst snapshot then legitimately re-takes the BLANK row the reset left behind. So the anchor
       moving to column 0 of a cleared screen is the evidence, not prompt_valid == 0 — which would be wrong
       to expect, since a blank row is a perfectly describable prompt. */
    CHECK(T.prompt_valid == 1 && T.prompt_cx == 0 && T.cx == 0,
          "integration: and a reset replaces the remembered prompt with the blank row it left");
    /* An announcement is an EVENT, not only a kind. The user can put another shell in front of us at the
       prompt (the scripts wrap a bare `fish`/`bash`/`zsh`) and the outer one announces again when that one
       exits; main.c has to see it happen, because it is what revives an integration the session had given up
       on. The kind alone cannot say it: fish -> a nested fish -> back to fish announces three times, one kind. */
    CHECK(T.shell_int_gen > 0, "integration: an announcement is countable, not just a kind");
    int gen_before = T.shell_int_gen;
    feed("\x1b_K;kai-term;integration;fish\x1b\\");
    CHECK(T.shell_int == SHELLINT_FISH && T.shell_int_gen == gen_before + 1,
          "integration: the same shell announcing again is still a new announcement");
    gen_before = T.shell_int_gen;
    feed("\x1b_K;kai-term;integration;tcsh\x1b\\");
    CHECK(T.shell_int_gen == gen_before,
          "integration: an unknown kind is not an announcement — nothing is revived for an unsupported shell");
    feed("\x1b" "c");
    CHECK(T.shell_int_gen == gen_before && T.shell_int == SHELLINT_FISH,
          "integration: a reset keeps the announcement count, so main.c's own copy stays comparable");
    term_destroy(&T);

    /* The pure parts of src/shellint.c: which shell, which sequence, which command line. */
    CHECK(shellint_kind_for("/usr/bin/fish") == SHELLINT_FISH &&
          shellint_kind_for("bash") == SHELLINT_BASH &&
          shellint_kind_for("-zsh") == SHELLINT_ZSH,
          "integration: a shell path (and a login shell's leading dash) maps to its kind");
    CHECK(shellint_kind_for("/bin/sh") == SHELLINT_NONE && shellint_kind_for("") == SHELLINT_NONE &&
          shellint_kind_for(NULL) == SHELLINT_NONE,
          "integration: sh, an empty path and a NULL path get no integration — they keep the typed path");
    {
        size_t flen = 0, blen = 0, zl = 0;
        const char *fseq = shellint_seq(SHELLINT_FISH, &flen);
        const char *bseq = shellint_seq(SHELLINT_BASH, &blen);
        const char *zseq = shellint_seq(SHELLINT_ZSH, &zl);
        /* Measured (AGENT.md s91): C-x C-] fires in fish 4.6 AND bash 5.3, is inert in both when unbound, and
           is unbound by default in both. One sequence, so a nested shell that never announced can never be
           handed a byte that damages its line (F14 inserts `~` in bash; C-x q inserts `q` in fish). */
        CHECK(fseq && flen == 2 && memcmp(fseq, "\x18\x1d", 2) == 0,
              "integration: fish is asked with C-x C-] (measured: one key to fish, inert when unbound)");
        CHECK(bseq && blen == 2 && memcmp(bseq, "\x18\x1d", 2) == 0,
              "integration: bash is asked with the SAME bytes (measured: readline runs the handler, inserts nothing)");
        CHECK(zseq && zl == 2 && memcmp(zseq, fseq, 2) == 0,
              "integration: zsh gets the same bytes (they fire there too — measured, AGENT.md s94)");
        CHECK(shellint_seq(SHELLINT_NONE, &zl) == NULL && zl == 0,
              "integration: an unsupported shell has no sequence at all");
        /* The rule that rules out C-\ (0x1c), which otherwise passes every shell test: `stty -a` on a pty
           reports it as VQUIT, so a stray write would SIGQUIT whatever owns the terminal. Same for every
           other control character the line discipline claims. */
        static const char *special = "\x03\x1c\x7f\x15\x04\x11\x13\x1a\x12\x17\x16\x0f";
        int clean = 1;
        for (size_t i = 0; i < flen; i++)
            for (const char *q = special; *q; q++)
                if (fseq[i] == *q)
                    clean = 0;
        CHECK(clean, "integration: the sequence holds no tty special character (no SIGQUIT, no ^Z, no ^D)");
    }
    {
        char dir[FM_PATH_MAX], req[FM_PATH_MAX + 64], cmd[FM_PATH_MAX * 2];
        CHECK(shellint_prepare(dir, sizeof dir) == 0 && dir[0] == '/',
              "integration: the script directory is created and absolute");
        {
            struct stat st;
            char p[FM_PATH_MAX + 64];
            snprintf(p, sizeof p, "%s/fish.fish", dir);
            CHECK(stat(p, &st) == 0 && st.st_size > 0, "integration: the fish script is on disk");
            snprintf(p, sizeof p, "%s/bash.bash", dir);
            CHECK(stat(p, &st) == 0 && st.st_size > 0, "integration: the bash script is on disk");
            snprintf(p, sizeof p, "%s/zsh/.zshrc", dir);
            CHECK(stat(p, &st) == 0 && st.st_size > 0, "integration: the zsh script is on disk");
            /* The script and shellint_seq() must agree, or Kai-Term sends bytes nobody listens for. Read the
               files back rather than trusting the constants: this is the only check that catches the two
               halves drifting apart. */
            static char ftxt[16384], btxt[16384], ztxt[16384];
            struct { const char *rel; char *txt; size_t cap; } scripts[3] = {
                { "fish.fish",  ftxt, sizeof ftxt },
                { "bash.bash",  btxt, sizeof btxt },
                { "zsh/.zshrc", ztxt, sizeof ztxt },
            };
            int scripts_fit = 1;
            for (int si = 0; si < 3; si++) {
                snprintf(p, sizeof p, "%s/%s", dir, scripts[si].rel);
                FILE *sf = fopen(p, "r");
                size_t rd = sf ? fread(scripts[si].txt, 1, scripts[si].cap - 1, sf) : 0;
                if (sf)
                    fclose(sf);
                scripts[si].txt[rd] = 0;
                if (rd == 0 || rd >= scripts[si].cap - 1)
                    scripts_fit = 0;      /* empty or truncated: the content checks below would be lies */
            }
            CHECK(scripts_fit, "integration: every script was read back whole (a truncated read pins nothing)");
            char *txt = ftxt;             /* the two pre-existing checks keep their shape */
            size_t rd = strlen(ftxt);
            CHECK(rd > 0 && strstr(txt, "bind (printf '\\030\\035')") != NULL,
                  "integration: the fish script binds the bytes shellint_seq() sends");
            txt = btxt;
            rd = strlen(btxt);
            CHECK(rd > 0 && strstr(txt, "bind -x '\"\\C-x\\C-]\"") != NULL,
                  "integration: the bash script binds the bytes shellint_seq() sends");
            CHECK(rd > 0 && strstr(txt, "if [ -f \"$HOME/.bashrc\" ]") != NULL,
                  "integration: the bash script loads the user's own ~/.bashrc first");
            /* The two things the user asked for on 2026-10-20, pinned in the text that actually ships. No
               other check sees these: they live in a string the C code never runs, and losing one silently
               loses the feature (the .scratch/s92 gate proves they work, but only while it is run). */
            CHECK(strstr(ftxt, "commandline -f repaint") != NULL,
                  "integration: the fish script repaints the prompt after the cd (measured: it then names the new directory)");
            CHECK(strstr(ftxt, "functions -q bash") != NULL && strstr(ftxt, "kai_term_nested bash") != NULL,
                  "integration: the fish script wraps a bare `bash` — and only when the user has no such function");
            CHECK(strstr(ftxt, "--on-event fish_postexec") != NULL,
                  "integration: the fish script re-announces after every command (that is what revives a dead one)");
            CHECK(strstr(btxt, "kai_term_nested bash") != NULL && strstr(btxt, "kai_term_announce") != NULL,
                  "integration: the bash script wraps the shells and re-announces too");
            CHECK(strstr(btxt, "PROMPT_COMMAND=") != NULL,
                  "integration: the bash heartbeat hooks PROMPT_COMMAND — bash's only per-prompt hook");
            CHECK(strstr(btxt, "commandline -f repaint") == NULL,
                  "integration: and bash does NOT pretend to repaint (readline cannot; measured, AGENT.md s92)");
            CHECK(strstr(ztxt, "zle reset-prompt") != NULL && strstr(ztxt, "precmd_functions+=") != NULL,
                  "integration: the zsh script repaints and re-announces (verified in zsh 5.9, AGENT.md s94)");
            CHECK(strstr(ftxt, "$KAI_TERM_INT_DIR") != NULL && strstr(btxt, "$KAI_TERM_INT_DIR") != NULL &&
                  strstr(ztxt, "$KAI_TERM_INT_DIR") != NULL,
                  "integration: every script can name the others (the nested-shell wrappers pass these paths)");
            CHECK(stat(dir, &st) == 0 && (st.st_mode & 0777) == 0700,
                  "integration: the directory is private (0700) — it holds a path the shell will cd into");
        }
        shellint_request_path(dir, 3, req, sizeof req);
        CHECK(strstr(req, "cd-request.3") != NULL,
              "integration: every session gets its own request file (one band, N shells)");
        /* main.c numbers sessions with a counter, not the tab index (a freed index is reused, and two live
           sessions sharing a request file is how one shell ends up in another session's directory). So the id
           grows past TAB_MAX in a long session and cleanup must still find the file — it scans the directory
           instead of enumerating indices. */
        {
            char big[FM_PATH_MAX + 64];
            shellint_request_path(dir, 5000, big, sizeof big);
            CHECK(shellint_write_request(big, "/tmp/far") == 0 && access(big, F_OK) == 0,
                  "integration: a request file with an id past TAB_MAX can be written");
        }
        CHECK(shellint_write_request(req, "/tmp/some dir/with spaces") == 0,
              "integration: a request path is written even with spaces");
        {
            FILE *f = fopen(req, "r");
            char got[512] = "";
            if (f) {
                size_t rd = fread(got, 1, sizeof(got) - 1, f);
                got[rd] = 0;
                fclose(f);
            }
            CHECK(strcmp(got, "/tmp/some dir/with spaces") == 0,
                  "integration: what the shell reads is exactly what was asked for, no temp suffix left");
        }
        CHECK(shellint_argv(SHELLINT_FISH, dir, req, cmd, sizeof cmd) == 0 &&
              strstr(cmd, "fish -C 'source") && strstr(cmd, req),
              "integration: fish is started with -C, which loads the script AFTER the user's config");
        CHECK(shellint_argv(SHELLINT_BASH, dir, req, cmd, sizeof cmd) == 0 &&
              strstr(cmd, "bash --rcfile"),
              "integration: bash is started with --rcfile (our file sources ~/.bashrc first)");
        CHECK(shellint_argv(SHELLINT_ZSH, dir, req, cmd, sizeof cmd) == 0 &&
              strstr(cmd, "ZDOTDIR=") && strstr(cmd, "zsh"),
              "integration: zsh is started with ZDOTDIR at our directory");
        /* The nested-shell wrappers run `command bash --rcfile $KAI_TERM_INT_DIR/bash.bash`, so every kind
           must be handed the directory in its environment — a wrapper that cannot find the other scripts
           starts a shell with NO integration, silently (measured that way in .scratch/s92: no error, no
           announcement, just a fallback). */
        int intdir_ok = 1;
        for (int k = SHELLINT_FISH; k <= SHELLINT_ZSH; k++) {
            if (shellint_argv(k, dir, req, cmd, sizeof cmd) != 0)
                intdir_ok = 0;
            else if (!strstr(cmd, "KAI_TERM_INT_DIR='") || !strstr(cmd, dir))
                intdir_ok = 0;
        }
        CHECK(intdir_ok, "integration: every integrated shell is told where the scripts live");
        CHECK(shellint_argv(SHELLINT_NONE, dir, req, cmd, sizeof cmd) != 0,
              "integration: no command line for an unsupported shell — main.c starts it unchanged");
        shellint_cleanup(dir);
        {
            struct stat st;
            char p[FM_PATH_MAX + 64];
            snprintf(p, sizeof p, "%s/fish.fish", dir);
            CHECK(stat(p, &st) != 0 && stat(dir, &st) != 0,
                  "integration: cleanup removes the scripts AND the request file this test wrote, so "
                  "nothing is left behind (a stale request file would name a dead session's directory)");
            snprintf(p, sizeof p, "%s/cd-request.5000", dir);
            CHECK(stat(p, &st) != 0,
                  "integration: cleanup scans the directory, so a request file with any id is removed");
        }
    }

    /* ---------- the configured default ink and paper (s101) ----------
       The user's report: "Some border seems to have appeared recently ... It's probably the padding to prevent
       cutting glyph corner, But would it be possible for it to be the exact same color as the background ? they
       should use the same config line to set their background color." Measured cause: glClear paints the
       Config.padding margin and the whole-cell remainder with Config.bg, while every blank, erased, scrolled-in
       and reflowed cell was filled with the compile-time DEF_BG. With `background = 1c1c22` in their file the two
       numbers differ, and the difference is a frame around the grid. These checks pin the ONE runtime value that
       now owns both jobs — the colour a cell is painted with, and the sentinel that says a cell was never
       written. */
    {
        Config d;
        config_default(&d);
        CHECK(d.fg == DEF_FG && d.bg == DEF_BG,
              "default colours: config_default() still starts at DEF_FG/DEF_BG — the constants stay the built-in "
              "default the shipped template's value check compares against");

        term_set_default_colors(0xff123456u, 0xff1c1c22u);
        CHECK(term_default_fg() == 0xff123456u && term_default_bg() == 0xff1c1c22u,
              "default colours: the setter is what the blank/erase/scroll/reflow paths read");

        term_init(&T, 6, 40, 20);
        CHECK(T.pen.fg == 0xff123456u && T.pen.bg == 0xff1c1c22u,
              "default colours: a fresh Term's pen starts at the configured pair, so main.c no longer has to "
              "patch the pen after opening a session");
        CHECK(cellbg(0, 0) == 0xff1c1c22u && cellfg(0, 0) == 0xff123456u,
              "default colours: a never-written cell carries the configured background, so the grid interior is "
              "the same colour as the padding margin glClear paints with Config.bg");

        feed("\x1b[41mX");
        CHECK(cellbg(0, 0) != 0xff1c1c22u,
              "default colours: SGR 41 still paints a palette background — the configured value is the DEFAULT, "
              "not an override of what an application asks for");
        feed("\x1b[49mY");
        CHECK(T.pen.bg == 0xff1c1c22u && cellbg(0, 1) == 0xff1c1c22u,
              "default colours: SGR 49 returns to the CONFIGURED background, not to DEF_BG — every shell hits "
              "that path on its first reset, which is how the whole grid got repainted the built-in colour");
        feed("\x1b[31mA\x1b[39mB");
        CHECK(T.pen.fg == 0xff123456u && cellfg(0, 3) == 0xff123456u,
              "default colours: SGR 39 returns to the configured foreground — the fg half is invisible on a blank "
              "cell today, so it is fixed for the same reason and not by accident");

        feed("\x1b[c");
        CHECK(T.pen.fg == 0xff123456u && T.pen.bg == 0xff1c1c22u,
              "default colours: ESC c / RIS rebuilds the pen from the configured pair — the reason this is "
              "process-wide state and not a Term field (term_init memsets the struct)");

        term_init(&T, 6, 40, 20);
        feed("abc\x1b[2K");
        CHECK(cellch(0, 0) == 0 && cellbg(0, 0) == 0xff1c1c22u,
              "default colours: an erased cell is blank AND carries the configured background");
        feed("\x1b[H\x1b[42m\x1b[J");
        CHECK(cellbg(0, 0) != 0xff1c1c22u,
              "default colours: an erase with a palette background in the pen keeps that colour — the configured "
              "default must not swallow an application's own erase");

        term_init(&T, 6, 40, 20);
        term_resize(&T, 12, 60);      /* what zooming out does to the grid: more rows AND more columns */
        CHECK(cellbg(11, 59) == 0xff1c1c22u && cellfg(11, 59) == 0xff123456u,
              "default colours: the rows and columns a smaller font reveals are blanked with the configured pair "
              "by vt.c itself, so main.c's zoom path no longer re-fills them");

        term_init(&T, 6, 40, 20);
        feed("abc");
        term_resize(&T, 6, 2);        /* reflow asks cell_pristine() how long that logical line is */
        CHECK(T.cy == 0 && T.cx == 1,
              "default colours: a cell painted with the configured background is STILL the pristine sentinel — "
              "reflow measures the line as 3 cells, not 40, so it produces 2 physical rows and the cursor lands "
              "after the `c` (a 40-cell reading would give 20 rows and put the cursor at column 0)");
        char rb[512];
        term_resize(&T, 6, 40);
        feed("d");
        row_text(0, rb);
        CHECK(rb[0] == 'a' && rb[1] == 'b' && rb[2] == 'c' && rb[3] == 'd' && rb[4] == ' ',
              "default colours: a narrow/wide round trip leaves no literal gap inside the text");

        term_set_default_colors(DEF_FG, DEF_BG);
        CHECK(term_default_fg() == DEF_FG && term_default_bg() == DEF_BG,
              "default colours: the setter restores, so this block cannot change what the rest of the suite sees");
    }

    /* ---------- the palette is process-wide RUNTIME state (the shape themes need) ----------
       Entries 0-15 are the ANSI colours a theme file may set. The point of the whole block is the second
       check: term_init() memsets the Term, so a palette stored in the Term is refilled from the built-in
       base16 by every new tab and by every `ESC c` — a theme would then apply to the first session only. */
    {
        const uint32_t *pal = term_palette();
        CHECK(pal[0] == 0xff000000u && pal[1] == 0xffcc5555u && pal[15] == 0xffffffffu,
              "palette: the built-in ANSI 0-15 are the xterm base colours");
        CHECK(pal[16] == 0xff000000u && pal[232] == 0xff080808u && pal[255] == 0xffeeeeeeu,
              "palette: the computed parts are the cube's first entry and the grey ramp's ends");

        /* The 6x6x6 cube, at the entries an application actually uses. This pinned a real defect: the level
           formula was 55 + 41 * level, which is 260 at level 5, and 260 << 16 leaves 0x04 in the channel —
           91 of the 216 entries came out near-black (measured before the fix: 196 = ff040000,
           226 = ff050400, 231 = ff050504). Alpha stayed 0xff, so no alpha check could ever have seen it. */
        CHECK(pal[196] == 0xffff0000u && pal[46] == 0xff00ff00u && pal[21] == 0xff0000ffu,
              "palette: 196/46/21 are full red/green/blue, the cube's top level is 255");
        CHECK(pal[226] == 0xffffff00u && pal[231] == 0xffffffffu && pal[233] == 0xff121212u,
              "palette: 226 is yellow, 231 is white, and the ramp steps by 10 (233 = 0x12)");
        /* The class rather than one instance: the six levels of each channel must increase and end at 255.
           The wrap made them DROP at level 5, which is what a check like this catches for every channel.
           Entry for (r,g,b) is 16 + 36r + 6g + b, so the red levels sit at stride 36, green at 6, blue at 1. */
        for (int sh = 0; sh < 24; sh += 8) {
            int stride = (sh == 16) ? 36 : (sh == 8) ? 6 : 1;
            int prev = -1, drops = 0, top = 0;
            for (int L = 0; L < 6; L++) {
                int v = (int)((term_palette()[16 + stride * L] >> sh) & 0xff);
                if (v <= prev)
                    drops++;
                prev = v;
                top = v;
            }
            CHECK(drops == 0 && top == 255,
                  "palette: each channel's six cube levels increase and end at 255 (no wrap)");
        }

        /* A theme sets an entry; the parser must then use it. */
        term_set_palette_entry(1, 0xffe07f70u);          /* the user's "13th Floor" red */
        term_init(&T, 6, 40, 20);
        feed("\x1b[31mR\x1b[0m");
        CHECK(cellfg(0, 0) == 0xffe07f70u,
              "palette: a changed entry is what SGR 31 paints — the palette is read at paint time, not baked in");

        /* THE check for this refactor: a new tab (term_init) must not put the built-in red back. */
        term_init(&T, 6, 40, 20);
        feed("\x1b[31mR\x1b[0m");
        CHECK(cellfg(0, 0) == 0xffe07f70u,
              "palette: term_init() does NOT refill the palette — a second tab keeps the theme (on the old "
              "per-Term palette this is the built-in 0xffcc5555)");

        /* Nor does a full reset: `reset` at the prompt sends ESC c. */
        feed("\x1bc");
        feed("\x1b[31mR\x1b[0m");
        CHECK(cellfg(0, 0) == 0xffe07f70u,
              "palette: ESC c / RIS rebuilds the pen but does NOT wipe the palette");

        /* A cell keeps the colour it was painted with: a Cell stores a resolved ARGB, so changing an entry
           later cannot repaint what is already on screen. Pinned because it is a documented consequence. */
        term_init(&T, 6, 40, 20);
        feed("\x1b[31mR\x1b[0m");
        uint32_t painted = cellfg(0, 0);
        term_set_palette_entry(1, 0xff00ff00u);
        CHECK(painted == 0xffe07f70u && cellfg(0, 0) == 0xffe07f70u,
              "palette: an already-painted cell keeps its colour when an entry changes afterwards");

        /* The setter is general (0-255); restricting a theme to 0-15 is the theme layer's job, not this one's. */
        term_set_palette_entry(200, 0xff010203u);
        term_init(&T, 6, 40, 20);
        feed("\x1b[38;5;200mX\x1b[0m");
        CHECK(cellfg(0, 0) == 0xff010203u, "palette: an entry above 15 can be set too (the theme layer restricts, not this)");

        /* Out-of-range indices are ignored, and must not touch a real entry. */
        uint32_t before255 = term_palette()[255];
        term_set_palette_entry(-1, 0xff111111u);
        term_set_palette_entry(256, 0xff111111u);
        term_set_palette_entry(99999, 0xff111111u);
        CHECK(term_palette()[255] == before255,
              "palette: an out-of-range index changes nothing (no clamp, no write past the end)");

        /* OSC 104's meaning: back to the built-ins. */
        term_palette_reset();
        CHECK(term_palette()[1] == 0xffcc5555u && term_palette()[200] != 0xff010203u
              && term_palette()[255] == 0xffeeeeeeu,
              "palette: term_palette_reset() restores every entry, the cube included");
        term_init(&T, 6, 40, 20);
        feed("\x1b[31mR\x1b[0m");
        CHECK(cellfg(0, 0) == 0xffcc5555u,
              "palette: after a reset SGR 31 is the built-in red again — the block leaves no state behind");
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
