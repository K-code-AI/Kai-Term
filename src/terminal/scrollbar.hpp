#pragma once

// Geometry for the scroll-position indicator, kept free of widget code so the
// mapping between "lines scrolled back" and "pixels along the track" can be
// checked without a display. The indicator is drawn over the terminal rather
// than beside it, so it never moves a single character of the grid; it exists
// to answer one question — where in the scrollback am I — and to be grabbable.

#include <QPoint>
#include <QRect>
#include <QSize>

#include <algorithm>
#include <cmath>

namespace kai {

inline constexpr int kScrollBarThickness = 10;
inline constexpr int kScrollBarMargin = 3;
inline constexpr int kScrollBarRadius = 3;
// A thumb that shrank with the scrollback would become impossible to hit in a
// long log, so it stops shrinking here.
inline constexpr int kScrollBarMinThumb = 24;

struct ScrollBar {
    bool visible = false;
    QRect track; // the strip the thumb slides along
    QRect thumb; // the part a person grabs
};

// `history_lines` are the lines held above the window, `visible_lines` are the
// ones in it, and `scroll_offset` counts how far back from the live bottom the
// window currently sits.
inline ScrollBar scrollBarGeometry(QSize area, int history_lines, int visible_lines,
                                   int scroll_offset,
                                   int thickness = kScrollBarThickness,
                                   int margin = kScrollBarMargin) {
    ScrollBar bar;
    if (history_lines <= 0 || visible_lines <= 0 || area.height() <= 2 * margin)
        return bar; // nothing to scroll to, so nothing to show

    const int track_height = area.height() - 2 * margin;
    bar.visible = true;
    bar.track = QRect(area.width() - thickness - margin, margin, thickness, track_height);

    const int total = history_lines + visible_lines;
    int thumb_height = static_cast<int>(
        std::lround(static_cast<double>(track_height) * visible_lines / total));
    thumb_height = std::clamp(thumb_height, std::min(kScrollBarMinThumb, track_height),
                              track_height);
    // The thumb travels the track as the viewport travels the scrollback: at the
    // live bottom it sits at the bottom, at the oldest line it sits at the top.
    const int travel = track_height - thumb_height;
    const double progress =
        1.0 - static_cast<double>(std::clamp(scroll_offset, 0, history_lines)) /
                  static_cast<double>(history_lines);
    const int top = bar.track.top() + static_cast<int>(std::lround(travel * progress));
    bar.thumb = QRect(bar.track.left(), top, bar.track.width(), thumb_height);
    return bar;
}

inline bool thumbContains(const ScrollBar& bar, const QPoint& point) {
    return bar.visible && bar.thumb.contains(point);
}

inline bool trackContains(const ScrollBar& bar, const QPoint& point) {
    return bar.visible && bar.track.contains(point);
}

// The scroll offset that puts the top of the thumb at `thumb_top`, which is how
// a drag turns pointer movement back into lines. Round-tripping with
// scrollBarGeometry is exact for whole lines: the indicator is a position, not
// a source of precision.
inline int scrollOffsetForThumbY(int thumb_top, const ScrollBar& bar, int history_lines,
                                 int visible_lines) {
    if (!bar.visible || history_lines <= 0)
        return 0;
    const int travel = bar.track.height() - bar.thumb.height();
    if (travel <= 0)
        return history_lines / 2;
    const double progress = std::clamp(
        static_cast<double>(thumb_top - bar.track.top()) / static_cast<double>(travel), 0.0,
        1.0);
    static_cast<void>(visible_lines);
    return static_cast<int>(std::lround(history_lines * (1.0 - progress)));
}

// Where the thumb would be if it were dragged so its top reached `thumb_top`,
// used while dragging so the indicator follows the pointer even when the
// offset is clamped at either end.
inline ScrollBar scrollBarWithThumbTop(const ScrollBar& bar, int thumb_top) {
    if (!bar.visible)
        return bar;
    ScrollBar next = bar;
    const int top = std::clamp(thumb_top, bar.track.top(),
                               bar.track.bottom() + 1 - bar.thumb.height());
    next.thumb = QRect(bar.thumb.left(), top, bar.thumb.width(), bar.thumb.height());
    return next;
}

} // namespace kai
