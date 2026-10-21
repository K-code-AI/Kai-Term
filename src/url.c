/* Which run of cells a click lands on, what makes that run a URL, what gates the click, how long it waits
   and how the opener is started. main.c owns the pointer, the geometry and the clock and asks questions. */
#include "kai-term.h"

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>     /* strncasecmp for the scheme: RFC 3986 says the scheme is case-insensitive */
#include <unistd.h>

/* ---------- what a run of cells is ---------- */

/* Can this codepoint sit inside a URL run? ASCII only: a space, a never-written cell and the five marks a
   terminal draws AROUND a URL end it. A non-ASCII codepoint CUTS it, so the whole run is refused rather
   than opening the half this rule can spell. */
static int url_cell(uint32_t ch)
{
    if (ch == 0 || ch == ' ' || ch < 0x20 || ch == 0x7f || ch >= 0x80)
        return 0;
    switch (ch) {
    case '"': case '\'': case '<': case '>': case '`':
        return 0;
    default:
        return 1;
    }
}

/* Is this cell one that CUTS a URL rather than ending it? A non-ASCII glyph, or either half of a wide one (the
   continuation holds ch == 0 and only the anchor holds the codepoint, so the pair has to be recognised whole). */
static int url_cut_cell(const Cell *c)
{
    if (c->attr & ATTR_WIDE)
        return 1;
    return c->ch >= 0x80;
}

/* The length of a scheme written at v[i], or 0. Case-insensitive: `HTTPS://EXAMPLE.COM` is the same scheme, and
   the probe measured the grid holding it exactly as printed. */
static int scheme_len(const uint32_t *v, int n, int i)
{
    static const char *s[2] = { "http://", "https://" };
    for (int k = 0; k < 2; k++) {
        int L = (int)strlen(s[k]);
        if (i + L > n)
            continue;
        int ok = 1;
        for (int j = 0; j < L; j++) {
            uint32_t c = v[i + j];
            if (c >= 'A' && c <= 'Z')
                c += 32;
            if (c != (uint32_t)s[k][j]) {
                ok = 0;
                break;
            }
        }
        if (ok)
            return L;
    }
    return 0;
}

/* Sentence punctuation printed after a link. Quote and bracket marks cannot appear here because url_cell()
   already ends a run on them; a trailing slash is kept because `https://x/` and `https://x` are different
   pages. */
static int url_trailing(uint32_t ch)
{
    switch (ch) {
    case '.': case ',': case ':': case ';': case '!': case '?':
        return 1;
    default:
        return 0;
    }
}

/* Does the closer at v[b] close the URL or the sentence around it? `(https://x/a(b)c)` ends with a bracket
   that closed nothing inside the run, `https://x/a(b)` does not; counting the pairs tells them apart. */
static int unbalanced_closer(const uint32_t *v, int a, int b)
{
    char close = (char)v[b];
    char open = close == ')' ? '(' : close == ']' ? '[' : close == '}' ? '{' : 0;
    if (!open)
        return 0;
    int nc = 0, no = 0;
    for (int i = a; i <= b; i++) {
        if (v[i] == (uint32_t)close)
            nc++;
        if (v[i] == (uint32_t)open)
            no++;
    }
    return nc > no;
}

/* The URL inside a joined logical line: extend the run around `at`, find the scheme in it, cut the marks that
   stick to its end. `v` is the codepoints and `cut` marks the cells that would cut a URL in half. */
static int url_run_at(const uint32_t *v, const uint8_t *cut, int n, int at, int *a, int *b)
{
    if (!v || !cut || !a || !b || at < 0 || at >= n)
        return 0;
    *a = *b = at;
    if (!url_cell(v[at]))
        return 0;                              /* the clicked cell is not part of a run at all */
    int lo = at, hi = at;
    while (lo > 0 && url_cell(v[lo - 1]))
        lo--;
    while (hi < n - 1 && url_cell(v[hi + 1]))
        hi++;
    /* Cut, not ended: the next cell is a glyph this rule cannot spell, so the run is half of something. The left
   side needs no guard — the scheme match fixes where the run starts whatever sits before it. */
    if (hi + 1 < n && cut[hi + 1])
        return 0;
    int start = -1, slen = 0;
    for (int i = lo; i <= hi; i++) {
        int L = scheme_len(v, n, i);
        if (L) {
            start = i;
            slen = L;
            break;
        }
    }
    if (start < 0)
        return 0;                              /* a run with no scheme is a word, not a link */
    int end = hi;
    while (end > start + slen && url_trailing(v[end]))
        end--;
    while (end > start + slen && unbalanced_closer(v, start, end))
        end--;
    if (end - start + 1 <= slen)
        return 0;                              /* `http://` with nothing after it opens nothing */
    *a = start;
    *b = end;
    return 1;
}

/* ---------- the grid half ---------- */

int term_url_span(Term *t, int col, int row, UrlSpan *sp)
{
    if (!t || !sp)
        return 0;
    sp->r0 = sp->c0 = sp->r1 = sp->c1 = 0;
    if (row < 0 || row >= t->g.rows || col < 0 || col >= t->g.cols)
        return 0;
    /* The LOGICAL line, walked by the same function a triple click uses — one owner for the direction of the
       wrap marks. A URL the window broke is one run; a URL the program broke with a newline is two. */
    int r0, r1;
    if (!term_line_span(t, row, &r0, &r1))
        return 0;
    int cols = t->g.cols;
    int n = (r1 - r0 + 1) * cols;
    uint32_t *v = malloc(sizeof(uint32_t) * (size_t)n);
    uint8_t *cut = malloc((size_t)n);
    if (!v || !cut) {
        free(v);
        free(cut);
        return 0;                              /* no room: answer "no URL", never half an answer */
    }
    for (int r = r0; r <= r1; r++) {
        const Cell *rc = grid_vprow(&t->g, r);
        for (int c = 0; c < cols; c++) {
            int i = (r - r0) * cols + c;
            v[i] = rc[c].ch;
            cut[i] = (uint8_t)url_cut_cell(&rc[c]);
        }
    }
    int a, b;
    int got = url_run_at(v, cut, n, (row - r0) * cols + col, &a, &b);
    free(v);
    free(cut);
    if (!got)
        return 0;
    sp->r0 = r0 + a / cols;
    sp->c0 = a % cols;
    sp->r1 = r0 + b / cols;
    sp->c1 = b % cols;
    return 1;
}

/* Cells of the span on one row: the first row starts at c0, the last ends at c1, the rows between are whole.
   The spelled-out address and the hover underline both use this, so they cannot disagree about which cells
   belong to the run. Pass the Term's own cols, not a pane's. */
int url_span_row(const UrlSpan *sp, int r, int cols, int *from, int *to)
{
    if (!sp || !from || !to)
        return 0;
    if (r < sp->r0 || r > sp->r1)
        return 0;                            /* this row holds none of the run */
    int a = r == sp->r0 ? sp->c0 : 0;
    int b = r == sp->r1 ? sp->c1 : cols - 1;
    if (a < 0 || b < a || b >= cols)
        return 0;
    *from = a;
    *to = b;
    return 1;
}

int term_url_text(Term *t, const UrlSpan *sp, char *out, size_t cap)
{
    if (!t || !sp || !out || cap == 0)
        return 0;
    out[0] = 0;
    if (sp->r0 < 0 || sp->r1 >= t->g.rows || sp->r1 < sp->r0)
        return 0;
    int cols = t->g.cols;
    size_t len = 0;
    for (int r = sp->r0; r <= sp->r1; r++) {
        int a, b;
        if (!url_span_row(sp, r, cols, &a, &b))
            return 0;
        len += (size_t)(b - a + 1);
    }
    if (len > (size_t)URL_MAX)
        return -1;                             /* not a URL anyone can open */
    if (len + 1 > cap)
        return 0;                              /* does not fit: write NOTHING rather than half a URL */
    size_t n = 0;
    for (int r = sp->r0; r <= sp->r1; r++) {
        const Cell *rc = grid_vprow(&t->g, r);
        int a, b;
        if (!url_span_row(sp, r, cols, &a, &b)) {
            out[0] = 0;
            return 0;
        }
        for (int c = a; c <= b; c++) {
            uint32_t ch = rc[c].ch;
            if (ch == 0 || ch >= 0x80) {       /* the run rule excludes both; refuse rather than guess */
                out[0] = 0;
                return 0;
            }
            out[n++] = (char)ch;
        }
    }
    out[n] = 0;
    if (!url_is_openable(out)) {
        out[0] = 0;
        return 0;
    }
    return (int)n;
}

/* ---------- what to hand the opener, and when ---------- */

int url_is_openable(const char *url)
{
    if (!url || !url[0])
        return 0;
    size_t n = strlen(url);
    if (n > (size_t)URL_MAX)
        return 0;
    /* An argument that starts with a dash is an option, not a document — the one way a URL could change what the
   opener DOES. Unreachable while only http(s) is accepted; it is the lock for the day a second scheme is. */
    if (url[0] == '-')
        return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)url[i];
        if (c < 0x20 || c == 0x7f || c >= 0x80)
            return 0;                          /* no control character and no UTF-8 reaches an opener */
    }
    if (n > 7 && !strncasecmp(url, "http://", 7))
        return 1;
    if (n > 8 && !strncasecmp(url, "https://", 8))
        return 1;
    return 0;                                  /* ftp, mailto, file, magnet: not ours to open */
}

/* Why a plain click does not open, in the order the gates are asked. A value rather than a chain of early
   returns so a refusal can name the gate that fired instead of the last one. */
int url_refusal(const UrlClick *c)
{
    if (!c)
        return URL_REFUSE_NO_ADDRESS;
    if (!c->enabled)
        return URL_REFUSE_OFF;
    if (!c->released)
        return URL_REFUSE_NOT_RELEASE;
    if (!c->single)
        return URL_REFUSE_GESTURE;          /* the second and third click select a word / a line */
    if (!c->no_drag)
        return URL_REFUSE_MOVED;            /* a drag selects text; that rule is older and louder than this one */
    if (c->shift)
        return URL_REFUSE_SHIFT;            /* Shift + click means selection, here as everywhere else */
    if (c->app_owns_click)
        return URL_REFUSE_APP;              /* less / nano / man keep their click-to-place-cursor */
    if (!c->url_found)
        return URL_REFUSE_NO_ADDRESS;
    return URL_REFUSE_NONE;
}

/* The refusal in words, written here so the line the user reads and the line a check pins cannot drift. */
const char *url_refusal_name(int why)
{
    switch (why) {
    case URL_REFUSE_NONE:       return "nothing refused it";
    case URL_REFUSE_OFF:        return "click_opens_url = 0";
    case URL_REFUSE_NOT_RELEASE:return "only a release can open — a press cannot know whether a second click follows";
    case URL_REFUSE_GESTURE:    return "it was the second or third click, which selects a word or a line instead";
    case URL_REFUSE_MOVED:      return "the pointer changed cell between the press and the release, so it is a drag and a drag selects";
    case URL_REFUSE_SHIFT:      return "Shift was held, which means selection";
    case URL_REFUSE_APP:        return "the program owns its own screen and keeps that click";
    case URL_REFUSE_NO_ADDRESS: return "no http(s) address under the pointer";
    default:                    return "unknown";
    }
}

int url_click_opens(const UrlClick *c)
{
    return url_refusal(c) == URL_REFUSE_NONE;
}

/* Should the run under the pointer be underlined? Asked as the CLICK's decision, not as its own list of ifs:
   an underline is a promise that a plain click here opens, so the only way it can lie is to disagree with
   the gate the click obeys. */
int url_hover_shows(const UrlHover *h)
{
    if (!h)
        return 0;
    if (!h->in_pane)
        return 0;                              /* the bar, the band, a margin and the box are not panes */
    UrlClick c = { .enabled = h->enabled,
                   .released = 1,
                   .single = 1,
                   .no_drag = !h->dragging,
                   .shift = h->shift,
                   .app_owns_click = h->app_owns_click,
                   .url_found = h->url_found };
    return url_click_opens(&c);
}

int url_open_due(long long now_ms, long long press_ms, int window_ms)
{
    if (press_ms <= 0)
        return 0;                              /* nothing pending */
    if (window_ms <= 0)
        return 1;                              /* a zero window asks for the click to answer now */
    /* Unreachable with the expression below (a negative difference is never past the window); it stays because a
   monotonic clock really can jump backwards on a resumed machine. */
    if (now_ms < press_ms)
        return 0;                              /* the clock went backwards */
    return now_ms - press_ms > (long long)window_ms;
}

int url_spawn(const char *program, const char *arg)
{
    if (!program || !program[0] || !arg)
        return -1;
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        /* Unblock every signal first: this process blocks SIGCHLD for its signalfd, a signal mask survives exec, and
   xdg-open is a shell script that waits for children. */
        sigset_t all;
        sigemptyset(&all);
        sigprocmask(SIG_SETMASK, &all, NULL);
        /* A new session: the opener is not part of the terminal's foreground group, so Ctrl+C at the prompt
           cannot reach it and it cannot take the terminal's own controlling tty. */
        if (setsid() < 0)
            _exit(127);
        int dev = open("/dev/null", O_RDWR);
        if (dev >= 0) {
            dup2(dev, STDIN_FILENO);
            dup2(dev, STDOUT_FILENO);
            dup2(dev, STDERR_FILENO);
            if (dev > STDERR_FILENO)
                close(dev);
        }
        /* Every descriptor of Kai-Term's own goes next: none of them is close-on-exec, so without this a browser
   would hold the user's ptys open long after their shells were gone. */
        closefrom(STDERR_FILENO + 1);
        char *argv[3] = { (char *)program, (char *)arg, NULL };
        execvp(program, argv);
        _exit(127);                            /* the parent cannot see this; its log says what was ASKED for */
    }
    return 0;
}
