/* Several terminal sessions (tabs) in one window. Pure bookkeeping — no Wayland, no EGL, no fork — so the
   tab logic is testable without a window; main.c owns everything that touches the outside world. */
#include "kai-term.h"

void mux_init(Mux *m, int max_tabs)
{
    memset(m, 0, sizeof(*m));
    /* A max of 0 or a negative one is a config mistake, not a request for a terminal that cannot
       open a session: clamp to 1 so the window always keeps the tab it started with. */
    m->max = iclamp_(max_tabs > 0 ? max_tabs : 1, 1, TAB_MAX);
    m->active = -1;
}

int mux_count(const Mux *m) { return m->n; }
int mux_active(const Mux *m) { return m->active; }

Tab *mux_cur(Mux *m)
{
    if (m->active < 0 || m->active >= m->n)
        return NULL;
    return &m->tab[m->active];
}

int mux_touch(Mux *m, int idx)
{
    if (idx < 0 || idx >= m->n || idx == m->active)
        return 0;
    if (m->tab[idx].attention)
        return 0;          /* already marked: the caller has nothing new to show */
    m->tab[idx].attention = 1;
    return 1;
}

int mux_open(Mux *m, int rows, int cols, int scrollback)
{
    if (m->n >= m->max) {
        klog(L_WARN, "mux: not opening another tab: max_tabs = %d reached", m->max);
        return -1;
    }
    int i = m->n++;
    Tab *t = &m->tab[i];
    /* The slot may hold the leftovers of a tab that was closed and shifted away; term_init() memsets
       the whole Term, so nothing of the old one survives. The PTY fields are set to "no process":
       main.c spawns it and keeps the descriptor, mux never does. */
    term_init(&t->term, rows, cols, scrollback);
    t->pty.mfd = -1;
    t->pty.child = 0;
    t->pty.rows = rows;
    t->pty.cols = cols;
    t->attention = 0;
    /* Shell integration belongs to the session, not to the slot: main.c fills these when it spawns the shell.
       A reused slot must not inherit the previous session's request path — a stale path would have the new
       shell read a directory someone else asked for. */
    t->cd_req[0] = 0;
    t->int_dead = 0;
    t->int_gen = 0;        /* matches the fresh Term's shell_int_gen = 0: nothing announced yet */
    /* Same rule for the visual bell: a reused slot must not inherit the previous session's ring, or a new tab
       could come up mid-flash of a BEL that belonged to a shell that is already gone. The click-synthesis stamp
       goes the same way: a stale stamp would mute the new session's first real bell. */
    t->bell_ms = 0;
    t->synth_ms = 0;
    m->active = i;      /* a new tab is the one the user is looking at */
    return i;
}

int mux_close(Mux *m, int idx)
{
    if (idx < 0 || idx >= m->n) {
        klog(L_WARN, "mux: close request for tab %d, which does not exist (%d open)", idx, m->n);
        return m->active;
    }
    term_destroy(&m->tab[idx].term);

    /* Keep the array dense. Tab is POD (Term holds heap pointers, but they move WITH the struct), so
       a struct copy is the whole move; the vacated slot is zeroed so its grid pointers cannot be
       freed a second time. */
    for (int i = idx; i + 1 < m->n; i++)
        m->tab[i] = m->tab[i + 1];
    m->n--;
    memset(&m->tab[m->n], 0, sizeof(Tab));

    /* Term.ps.t points at the Term itself (vt.c's parser reads it through the Parser). term_parse()
       re-arms it on every call, but nothing may read it BEFORE that first parse, so fix it here for
       every tab that just moved. */
    for (int i = idx; i < m->n; i++)
        m->tab[i].term.ps.t = &m->tab[i].term;

    if (m->n == 0) {
        m->active = -1;
        return -1;                       /* the caller quits: a window with no session is pointless */
    }
    if (idx < m->active)
        m->active--;                     /* the tabs after it slid down, so its index did too */
    else if (idx == m->active)
        m->active = imin_(idx, m->n - 1); /* the tab that took its place, or the new last one */
    m->active = iclamp_(m->active, 0, m->n - 1);
    m->tab[m->active].attention = 0;
    return m->active;
}

/* An application changed the default ink/paper (OSC 10/11) in one session. The defaults are process-wide,
   so the other sessions' already-painted cells have to be swept to match. */
void mux_recolor_defaults(Mux *m, uint32_t old_fg, uint32_t old_bg,
                          uint32_t new_fg, uint32_t new_bg)
{
    for (int i = 0; i < m->n; i++)
        term_recolor_defaults(&m->tab[i].term, old_fg, old_bg, new_fg, new_bg);
}

int mux_goto(Mux *m, int idx)
{
    if (m->n == 0) {
        m->active = -1;
        return -1;
    }
    /* Clamped rather than rejected: `key_tab_prefix` + 9 on a two-tab window should land on the last
       tab, and a compositor that repeats a stale index must not be able to lose the visible tab. */
    m->active = iclamp_(idx, 0, m->n - 1);
    m->tab[m->active].attention = 0;
    return m->active;
}

int mux_next(Mux *m)
{
    if (m->n == 0)
        return -1;
    return mux_goto(m, (m->active + 1) % m->n);
}

int mux_prev(Mux *m)
{
    if (m->n == 0)
        return -1;
    return mux_goto(m, (m->active + m->n - 1) % m->n);
}

void mux_title(const Mux *m, char *out, size_t cap)
{
    /* One session: the title stays exactly what it was before multiplexing existed, so a user who
       never opens a second tab sees no change at all. */
    if (m->n <= 1) {
        snprintf(out, cap, "%s", DEF_TITLE);
        return;
    }
    int unread = 0;
    for (int i = 0; i < m->n; i++)
        if (m->tab[i].attention)
            unread++;
    if (unread)
        snprintf(out, cap, "%s [%d/%d] !%d", DEF_TITLE, m->active + 1, m->n, unread);
    else
        snprintf(out, cap, "%s [%d/%d]", DEF_TITLE, m->active + 1, m->n);
}

/* How many sessions have something running that a close would throw away. The indices are 0-based tab
   indices — the CALLER adds 1, because "tab 3" is what the user counts and what mux_goto() takes, and a
   message that got that off by one is worse than no message. */
int mux_running(const Mux *m, int *idx, int max)
{
    if (!m)
        return 0;
    int n = 0;
    for (int i = 0; i < m->n; i++) {
        if (!pty_fg_busy(&m->tab[i].pty))
            continue;
        if (idx && n < max)
            idx[n] = i;
        n++;
    }
    return n;
}

/* Is a bell flash on screen right now? A session the user cannot see has no field to wash out, and keeping
   the window dirty for it would burn frames on a picture that cannot change. */
int mux_bell_live(const Mux *m, const Layout *lay, long long now_ms, int dur_ms)
{
    if (!m || !lay || dur_ms <= 0)
        return 0;
    int n = mux_count(m);
    for (int p = 0; p < lay->npane; p++) {
        int tab = lay->pane[p].tab;
        if (tab < 0 || tab >= n)
            continue;
        if (bell_strength(now_ms, m->tab[tab].bell_ms, dur_ms) > 0)
            return 1;
    }
    return 0;
}

/* "2, 4 and 5" — which sessions are at risk, at most four numbers: that is what still fits an 80-column
   window next to the question and the answer hint. */
static void session_list(const int *idx, int n, char *out, size_t cap)
{
    if (cap == 0)
        return;
    out[0] = 0;
    if (!idx || n <= 0)
        return;
    int shown = n > 4 ? 4 : n;
    size_t o = 0;
    for (int i = 0; i < shown; i++) {
        const char *sep = i == 0 ? "" : (i + 1 == shown ? " and " : ", ");
        int r = snprintf(out + o, o < cap ? cap - o : 0, "%s%d", sep, idx[i] + 1);
        if (r < 0 || o + (size_t)r >= cap)
            break;
        o += (size_t)r;
    }
    if (n > shown) {
        int r = snprintf(out + o, o < cap ? cap - o : 0, ", ...");
        if (r > 0 && o + (size_t)r < cap)
            o += (size_t)r;
    }
}

/* The one-row question's wording lives here rather than in main.c because main.c is in no test binary: a
   string built here is a check, a string inside main.c is only a claim. */
int confirm_message(int what, int n, const int *idx, char *out, size_t cap)
{
    if (!out || cap == 0)
        return 0;
    char list[64];
    list[0] = 0;
    int r;
    if (what == CONFIRM_CLOSE_TAB) {
        if (n <= 0)
            r = snprintf(out, cap, "Close this session?  [Enter] yes  [Esc] no");
        else
            r = snprintf(out, cap, "A command is running here. Close it?  [Enter] yes  [Esc] no");
    } else if (n <= 0) {
        r = snprintf(out, cap, "Quit Kai-Term?  [Enter] yes  [Esc] no");
    } else if (n == 1) {
        r = snprintf(out, cap, "Session %d is still running. Quit Kai-Term?  [Enter] yes  [Esc] no",
                     idx ? idx[0] + 1 : 1);
    } else if (idx) {
        session_list(idx, n, list, sizeof list);
        r = snprintf(out, cap, "%d sessions are still running (%s). Quit?  [Enter] yes  [Esc] no",
                     n, list);
    } else {
        r = snprintf(out, cap, "%d sessions are still running. Quit?  [Enter] yes  [Esc] no", n);
    }
    if (r < 0) {
        out[0] = 0;
        return 0;
    }
    /* snprintf reports what it WOULD have written; the caller gets what it DID write. */
    if ((size_t)r >= cap)
        r = (int)cap - 1;
    return r;
}

/* One entry of the window box's list: the process's own name in quotes when it could be read, the session
   number when it could not. A number says plainly that it is a fallback. */
static void name_phrase(const char *name, int tabidx, char *out, size_t cap)
{
    if (name && name[0])
        snprintf(out, cap, "\"%s\"", name);
    else
        snprintf(out, cap, "session %d", tabidx + 1);
}

/* `"nano"`, `"nano" and "sleep"`, `"a", "b", "c" and 2 more`. At most three names — the box is one
   sentence, not a process listing — and "and" precedes the last name only when the list ends with one. */
static void name_list(const int *idx, const char *const *names, int n, char *out, size_t cap)
{
    if (!cap)
        return;
    out[0] = 0;
    if (n <= 0)
        return;
    int shown = n > 3 ? 3 : n;
    size_t o = 0;
    for (int i = 0; i < shown; i++) {
        char ph[PROC_NAME_MAX + 16];
        name_phrase(names ? names[i] : NULL, idx ? idx[i] : i, ph, sizeof ph);
        const char *sep = (i == 0) ? "" : ((i + 1 == shown && n == shown) ? " and " : ", ");
        int r = snprintf(out + o, o < cap ? cap - o : 0, "%s%s", sep, ph);
        if (r < 0 || o + (size_t)r >= cap)
            break;
        o += (size_t)r;
    }
    if (n > shown) {
        int r = snprintf(out + o, o < cap ? cap - o : 0, " and %d more", n - shown);
        if (r > 0 && o + (size_t)r < cap)
            o += (size_t)r;
    }
}

/* The window box's sentence. It differs from the tab row on purpose: a tab close is reversible, a window
   close is not, so this one names the work and asks in full sentences. */
int confirm_warning(int n, const int *idx, const char *const *names, char *out, size_t cap)
{
    if (!out || cap == 0)
        return 0;

    int r;
    if (n <= 0)
        r = snprintf(out, cap, "Warning ! Are you sure you want to close Kai-Term ?");
    else {
        char list[DIALOG_MAX / 2];
        name_list(idx, names, n, list, sizeof list);
        r = snprintf(out, cap, "Warning ! %s %s still running. Are you sure you want to close %s ?",
                     list, n > 1 ? "are" : "is", n > 1 ? "them" : "it");
    }
    if (r < 0) {
        out[0] = 0;
        return 0;
    }
    if ((size_t)r >= cap)
        r = (int)cap - 1;
    return r;
}
