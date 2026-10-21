/* How one window's pixels are split between the tab bar, the file-manager band and the session panes.
   Pure geometry — no Wayland, no EGL, no file descriptors — so it is testable without a window. */
#include "kai-term.h"

/* Cut a string to at most `max` UTF-8 codepoints, never inside a multi-byte sequence. */
static void clip_utf8(char *s, int max)
{
    int n = 0;
    for (char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 0x80 && c < 0xc0)
            continue;
        if (n >= max) {
            *p = 0;
            return;
        }
        n++;
    }
}

/* Drop a trailing incomplete UTF-8 sequence left by a byte-bounded copy (snprintf can split one). */

/* "<n> <title>", with a '*' before the title when the session is hidden and has output the user has not
   seen yet. An unnamed session reads "shell" — every shell sets its own title (OSC 0/2) as soon as it
   prints one, so that is only the startup state. */
static void bar_label(char *out, int tab, int attention, const char *title, int maxcols)
{
    snprintf(out, BAR_LABEL_MAX, "%d %s%s", tab + 1, attention ? "*" : "",
             (title && title[0]) ? title : "shell");
    clip_utf8(out, maxcols > 1 ? maxcols : 1);
    utf8_trim_partial(out);        /* the 40-byte buffer can cut a title mid-sequence (see above) */
}

/* Columns a run of UTF-8 occupies. A lead byte starts a codepoint and a continuation byte belongs to the
   one before it — the same rule clip_utf8() uses, and the reason the box is as wide as the text that will
   actually be drawn in it rather than as wide as its byte count. */
static int utf8_cols(const char *s, int nbytes)
{
    int n = 0;
    for (int i = 0; i < nbytes; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!(c >= 0x80 && c < 0xc0))
            n++;
    }
    return n;
}

/* Bytes of the next word — everything up to the next space or the end of the string. */
static int word_bytes(const char *s)
{
    int n = 0;
    while (s[n] && s[n] != ' ')
        n++;
    return n;
}

/* Bytes of `s` whose codepoints fit in `maxcols` columns, never splitting a codepoint. */
static int cols_bytes(const char *s, int nbytes, int maxcols)
{
    int nb = 0, cols = 0;
    while (nb < nbytes) {
        unsigned char c = (unsigned char)s[nb];
        int clen = (c >= 0xf0) ? 4 : (c >= 0xe0) ? 3 : (c >= 0xc0) ? 2 : 1;
        if (nb + clen > nbytes)
            clen = nbytes - nb;      /* a malformed tail: take what is there */
        if (cols + 1 > maxcols)
            break;
        nb += clen;
        cols++;
    }
    return nb;
}

/* Wrap a sentence into at most DIALOG_LINES lines of `maxcols` columns, breaking at spaces; a word wider
   than the line is hard-broken, and DIALOG_LINE_MAX bounds BYTES, which a name can exhaust first. */
static void wrap_sentence(const char *text, int maxcols, Dialog *d)
{
    d->nline = 0;
    if (maxcols < 1)
        maxcols = 1;
    if (!text || !text[0]) {
        d->line[0][0] = 0;
        d->nline = 1;
        return;
    }

    const char *p = text;
    while (*p == ' ')
        p++;
    while (*p && d->nline < DIALOG_LINES) {
        char *dst = d->line[d->nline];
        size_t o = 0;
        int cols = 0;
        int cut = 0;          /* bytes of dst that end on a COMPLETED word */

        while (*p) {
            int wb = word_bytes(p);
            int wc = utf8_cols(p, wb);
            int sep = cols ? 1 : 0;
            if (cols + sep + wc > maxcols || (int)o + sep + wb >= DIALOG_LINE_MAX) {
                if (cols) {                 /* the line is full: end it at the last word boundary */
                    o = (size_t)cut;
                    break;
                }
                /* One word wider than the line: take what fits, broken at a codepoint edge. */
                int take = cols_bytes(p, wb, maxcols);
                if (take > DIALOG_LINE_MAX - 1)
                    take = DIALOG_LINE_MAX - 1;
                memcpy(dst, p, (size_t)take);
                dst[take] = 0;
                utf8_trim_partial(dst);        /* the BYTE bound can land inside a codepoint */
                take = (int)strlen(dst);
                o = (size_t)take;
                /* The pointer steps over exactly the bytes this line KEPT. Stepping over the trimmed ones loses a
   character and starts the next line on its leftover continuation byte. */
                p += take;
                break;
            }
            if (sep)
                dst[o++] = ' ';
            memcpy(dst + o, p, (size_t)wb);
            o += (size_t)wb;
            cols += sep + wc;
            p += wb;
            while (*p == ' ')
                p++;
            cut = (int)o;
        }
        dst[o] = 0;
        utf8_trim_partial(dst);
        d->nline++;
    }

    if (*p && d->nline >= DIALOG_LINES) {
        /* The sentence did not fit. Shorten the last line and say so, rather than hide the rest. */
        char *last = d->line[DIALOG_LINES - 1];
        int nb = (int)strlen(last);
        while (nb > 0 && last[nb - 1] == ' ')
            nb--;
        int budget = imax_(maxcols - 4, 1);        /* " ..." costs four columns */
        if (utf8_cols(last, nb) > budget)
            nb = cols_bytes(last, nb, budget);
        while (nb > 0 && last[nb - 1] == ' ')
            nb--;
        if (nb > DIALOG_LINE_MAX - 5)             /* room for " ..." and the NUL */
            nb = DIALOG_LINE_MAX - 5;
        last[nb] = 0;
        utf8_trim_partial(last);
        snprintf(last + strlen(last), DIALOG_LINE_MAX - strlen(last), " ...");
    }

    if (!d->nline) {
        d->line[0][0] = 0;
        d->nline = 1;
    }
}

void layout_compute(const LayoutReq *rq, Layout *out)
{
    memset(out, 0, sizeof(*out));

    int cw = imax_(rq->cell_w, 1), ch = imax_(rq->cell_h, 1);
    int ww = imax_(rq->win_w, 1), wh = imax_(rq->win_h, 1);
    int pad = imax_(rq->pad, 0);
    int gap = imax_(rq->gap, 0);
    int ntab = iclamp_(rq->ntab, 0, TAB_MAX);
    int active = (rq->active >= 0 && rq->active < ntab) ? rq->active : (ntab ? 0 : -1);
    int smax = iclamp_(rq->split_max < 1 ? 1 : rq->split_max, 1, TAB_MAX);
    out->mode = (rq->mode == LAYOUT_SPLIT) ? LAYOUT_SPLIT : LAYOUT_TABS;
    out->active_tab = active;   /* the renderer highlights its item and borders its pane */

    /* A margin may never eat the last whole cell — the same cap term_grid_geometry() applies, done here
       because the bar has to fit inside what is left. */
    int padx = imin_(pad, imax_((ww - cw) / 2, 0));
    int pady = imin_(pad, imax_((wh - ch) / 2, 0));

    int bar_on = 0;
    if (ntab > 0 && rq->bar != BAR_OFF)
        bar_on = (rq->bar == BAR_ALWAYS) ? 1 : (ntab >= 2 ? 1 : 0);

    int grid_x = padx, grid_y = pady;
    int grid_w = ww - 2 * padx, grid_h = wh - 2 * pady;
    if (bar_on) {
        grid_y = pady + ch + gap;
        grid_h = wh - grid_y - pady;
        if (grid_h < ch)
            bar_on = 0;      /* no room for the bar AND one text row: keep the text row */
    }
    if (!bar_on) {
        grid_y = pady;
        grid_h = wh - 2 * pady;
    }
    if (grid_w < cw)
        grid_w = cw;
    if (grid_h < ch)
        grid_h = ch;

    out->bar_on = bar_on;
    if (bar_on) {
        out->bar_x = grid_x;
        out->bar_y = pady;
        out->bar_w = grid_w;
        out->bar_h = ch;
        out->nitem = ntab;
        /* Equal-width items; the pixels left over after whole items stay at the right, like the
           whole-cell remainder of a grid. */
        int item_w = imax_(grid_w / ntab, 1);
        int maxcols = imax_(item_w / cw, 1);
        for (int i = 0; i < ntab; i++) {
            BarItem *it = &out->item[i];
            it->x = grid_x + i * item_w;
            it->w = item_w;
            it->tab = rq->tab[i].tab;
            it->attention = rq->tab[i].attention ? 1 : 0;
            it->active = (it->tab == active) ? 1 : 0;
            bar_label(it->label, it->tab, it->attention, rq->tab[i].title, maxcols);
        }
    }

    /* The confirm-quit question costs one row at the bottom of the grid area, reserved before the band so the
   band's percent applies to what is left. Too short for that row and the question is dropped. */
    int notice_on = rq->notice_on ? 1 : 0;
    if (notice_on && grid_h - ch < ch)
        notice_on = 0;
    out->notice_on = notice_on;
    if (notice_on) {
        out->notice_x = grid_x;
        out->notice_y = grid_y + grid_h - ch;      /* the last row of the UNSHRUNKEN grid area */
        out->notice_w = grid_w;
        out->notice_h = ch;
        grid_h -= ch;
        /* The text is cut HERE, to the columns this row really has, so the renderer draws what geometry decided. */
        snprintf(out->notice, sizeof out->notice, "%s",
                 (rq->notice_text && rq->notice_text[0]) ? rq->notice_text : "");
        clip_utf8(out->notice, imax_(grid_w / cw, 1));
        utf8_trim_partial(out->notice);
    }

    /* The band takes fm_size percent of the grid area's height and the sessions keep the rest. It stays an
   ordinary PaneGeom, so every hit test works with the band on and off. */
    int pane_y = grid_y, pane_h = grid_h;
    if (rq->fm_on) {
        int pct = iclamp_(rq->fm_size, FM_SIZE_MIN, FM_SIZE_MAX);
        int band_h = (int)((long long)grid_h * pct / 100);
        /* The shell keeps at least FM_TERM_MIN_ROWS rows. If that leaves less than a usable band, the
           band is dropped for this frame rather than starving the terminal — the bar's rule again. */
        int room = grid_h - gap - FM_TERM_MIN_ROWS * ch;
        if (band_h > room)
            band_h = room;
        if (band_h >= FM_ROWS_MIN * ch && grid_w >= 2 * cw) {
            FmGeom *fg = &out->fm;
            fg->on = 1;
            fg->list_x = grid_x;
            fg->list_y = grid_y;
            fg->list_h = band_h;
            /* A preview narrower than a couple of cells is worse than no preview, so the whole band
               becomes the browser. The remainder of the half-split stays on the RIGHT of the band,
               like every other whole-cell remainder. */
            int prev_on = rq->fm_preview_on && grid_w >= 3 * cw + gap;
            fg->list_w = prev_on ? (grid_w - gap) / 2 : grid_w;
            fg->prev_on = prev_on;
            int xo, yo;
            term_grid_geometry(fg->list_w, fg->list_h, cw, ch, 0,
                               &fg->list_cols, &fg->list_rows, &xo, &yo);
            if (prev_on) {
                fg->prev_w = grid_w - gap - fg->list_w;
                fg->prev_x = grid_x + fg->list_w + gap;
                fg->prev_y = grid_y;
                fg->prev_h = band_h;
                term_grid_geometry(fg->prev_w, fg->prev_h, cw, ch, 0,
                                   &fg->prev_cols, &fg->prev_rows, &xo, &yo);
            }
            pane_y = grid_y + band_h + gap;
            pane_h = grid_h - band_h - gap;
        }
    }

    /* Which sessions get a pane, and how wide each pane is. */
    int npane = 0, start = 0, pane_w = grid_w;
    if (ntab > 0) {
        if (out->mode == LAYOUT_TABS) {
            npane = 1;
            start = imax_(active, 0);
            pane_w = grid_w;
        } else {
            npane = imin_(ntab, smax);
            /* Slide the shown window so it contains the active session: centred on it when possible,
               clamped at both ends so the first/last sessions really are reachable. */
            start = imin_(imax_(active - npane / 2, 0), imax_(ntab - npane, 0));
            pane_w = imax_((grid_w - (npane - 1) * gap) / npane, 1);
        }
    }
    out->npane = npane;
    for (int i = 0; i < npane; i++) {
        PaneGeom *pg = &out->pane[i];
        pg->x = grid_x + i * (pane_w + gap);
        pg->y = pane_y;
        pg->w = pane_w;
        pg->h = pane_h;
        pg->tab = start + i;
        /* No inner margin inside a pane: `gap` already separates the panes, and Config.padding is the
           window's own margin. xoff/yoff are therefore 0 and pane.x/pane.y ARE the grid origin, which is
           what main.c stores for the pointer mapping of that session. */
        int xoff, yoff;
        term_grid_geometry(pg->w, pg->h, cw, ch, 0, &pg->cols, &pg->rows, &xoff, &yoff);
    }

    /* The confirm-quit dialog: a centred box drawn OVER the terminal, costing no grid row. It hugs its text —
   wrapped at the widest possible, then shrunk to the widest line produced — and is dropped if it will
   not fit, in which case the window closes without asking. */
    if (rq->dialog_on) {
        int pad_h = cw;                    /* one cell of padding, each side */
        int pad_v = imax_(ch / 2, 1);      /* half a cell above and below the text */
        int max_text_cols = imax_((grid_w - 2 * pad_h) / cw, 1);
        Dialog *dg = &out->dialog;
        wrap_sentence(rq->dialog_text, max_text_cols, dg);

        int widest = 0;                    /* the widest line the wrap PRODUCED, in columns */
        for (int i = 0; i < dg->nline; i++) {
            int c = utf8_cols(dg->line[i], (int)strlen(dg->line[i]));
            if (c > widest)
                widest = c;
        }

        /* A button is its label plus one cell of padding each side, and the pair is two cells apart. The
           labels are short on purpose: they are the two answers, not a description of them, and the sentence
           above already says what is being answered for. */
        static const char *LBL_YES = "Yes";
        static const char *LBL_NO  = "No";
        int yes_cols = (int)strlen(LBL_YES) + 2;
        int no_cols  = (int)strlen(LBL_NO) + 2;
        int btn_gap  = 2 * cw;
        int need_w   = (yes_cols + no_cols) * cw + btn_gap + 2 * pad_h;

        int box_w = imin_(imax_((widest + 2) * cw, need_w), grid_w);
        int box_h = dg->nline * ch + 2 * pad_v + ch;
        if (box_w >= need_w && box_h <= grid_h) {
            dg->on = 1;
            dg->w  = box_w;
            dg->h  = box_h;
            dg->x  = grid_x + (grid_w - box_w) / 2;    /* centred in the grid area, not in the window: the */
            dg->y  = grid_y + (grid_h - box_h) / 2;    /* bar and the margin are not the user's work area. */
            dg->text_x = dg->x + pad_h;                /* each line is one cell row down from here, so the */
            dg->text_y = dg->y + pad_v;                /* renderer needs no padding of its own to know. */

            int by = dg->y + pad_v + dg->nline * ch + pad_v;
            int no_x  = dg->x + dg->w - pad_h - no_cols * cw;
            int yes_x = no_x - btn_gap - yes_cols * cw;
            dg->nbtn = 2;
            DialogBtn *by_yes = &dg->btn[0];
            by_yes->yes = 1;                    /* the ANSWER, not a name: a hit test cannot break the day */
            by_yes->x = yes_x;                  /* the labels change, and the box is still answered right. */
            by_yes->y = by;
            by_yes->w = yes_cols * cw;
            by_yes->h = ch;
            by_yes->label_x = yes_x + (yes_cols - (int)strlen(LBL_YES)) * cw / 2;
            snprintf(by_yes->label, sizeof by_yes->label, "%s", LBL_YES);
            DialogBtn *by_no = &dg->btn[1];
            by_no->yes = 0;
            by_no->x = no_x;
            by_no->y = by;
            by_no->w = no_cols * cw;
            by_no->h = ch;
            by_no->label_x = no_x + (no_cols - (int)strlen(LBL_NO)) * cw / 2;
            snprintf(by_no->label, sizeof by_no->label, "%s", LBL_NO);
        } else {
            /* Left as memset gave it: on == 0, nbtn == 0, no geometry. A half-drawn box is worse than none. */
            dg->nline = 0;
        }
    }
}

/* Which dialog button owns this pointer position, or -1 anywhere else — including inside the box but off
   both buttons, which leaves the question up. */
int layout_hit_dialog(const Layout *l, int px, int py)
{
    if (!l->dialog.on)
        return -1;
    for (int i = 0; i < l->dialog.nbtn; i++) {
        const DialogBtn *b = &l->dialog.btn[i];
        if (px >= b->x && px < b->x + b->w && py >= b->y && py < b->y + b->h)
            return i;
    }
    return -1;
}

int layout_hit_bar(const Layout *l, int px, int py)
{
    if (!l->bar_on || py < l->bar_y || py >= l->bar_y + l->bar_h)
        return -1;
    for (int i = 0; i < l->nitem; i++)
        if (px >= l->item[i].x && px < l->item[i].x + l->item[i].w)
            return i;
    return -1;      /* the remainder at the right end of the strip belongs to no tab */
}

int layout_hit_pane(const Layout *l, int px, int py)
{
    for (int i = 0; i < l->npane; i++) {
        const PaneGeom *pg = &l->pane[i];
        if (px >= pg->x && px < pg->x + pg->w && py >= pg->y && py < pg->y + pg->h)
            return i;
    }
    return -1;      /* margin, gap, or below the last row */
}

int layout_hit_fm(const Layout *l, int px, int py)
{
    const FmGeom *fg = &l->fm;
    if (!fg->on)
        return FM_HIT_NONE;
    if (px >= fg->list_x && px < fg->list_x + fg->list_w &&
        py >= fg->list_y && py < fg->list_y + fg->list_h)
        return FM_HIT_LIST;
    if (fg->prev_on && px >= fg->prev_x && px < fg->prev_x + fg->prev_w &&
        py >= fg->prev_y && py < fg->prev_y + fg->prev_h)
        return FM_HIT_PREVIEW;
    return FM_HIT_NONE;      /* the gap between the two panes, or below the band */
}

int layout_pane_of_tab(const Layout *l, int tab)
{
    for (int i = 0; i < l->npane; i++)
        if (l->pane[i].tab == tab)
            return i;
    return -1;
}

void layout_grid_size(const Layout *l, int *cols, int *rows)
{
    if (l->npane <= 0) {          /* no session is drawn: there is no grid to size */
        *cols = 0;
        *rows = 0;
        return;
    }
    /* Every pane of one layout has the same rect height, and in split mode the same width (the width is
       divided equally, the remainder going to the right of the LAST pane), so pane[0] describes the grid
       size of every session this window holds — including the ones not drawn right now. */
    *cols = l->pane[0].cols;
    *rows = l->pane[0].rows;
}

/* The startup window size: layout_compute() inverted. The bar costs a cell ROW plus the gap; the band is a
   percent, so grid_h = ceil((rows*ch + gap) * 100 / (100 - pct)). */
void layout_initial_size(int cols, int rows, int cell_w, int cell_h, int pad, int gap, int bar,
                         int fm_on, int fm_size, int *win_w, int *win_h)
{
    int cw = imax_(cell_w, 1), ch = imax_(cell_h, 1);
    int p  = imax_(pad, 0), g = imax_(gap, 0);

    *win_w = imax_(cols, 1) * cw + 2 * p;
    *win_h = imax_(rows, 1) * ch + 2 * p;
    /* BAR_AUTO shows nothing with the single starting session, so it needs no extra height here. */
    if (bar == BAR_ALWAYS)
        *win_h += ch + g;
    if (fm_on) {
        int pct = iclamp_(fm_size, FM_SIZE_MIN, FM_SIZE_MAX);
        int need = imax_(rows, 1) * ch + g;      /* what the (100-pct)% remainder has to hold */
        int grid_h = (need * 100 + (100 - pct) - 1) / (100 - pct);
        *win_h = grid_h + 2 * p;
        if (bar == BAR_ALWAYS)
            *win_h += ch + g;
    }
}
