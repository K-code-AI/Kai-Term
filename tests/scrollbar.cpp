// Headless check of the scroll indicator's geometry: where the thumb sits for a
// given scroll position, how tall it is, and that dragging it back reproduces
// the same position. The widget's painting and mouse wiring are exercised in
// the offscreen widget test; the arithmetic lives here because it is the part
// that can be wrong by a pixel and mislead someone about where they are.

#include "../src/terminal/scrollbar.hpp"

#include <iostream>
#include <string>

namespace {

int checks = 0;
int failures = 0;

void expect(const char* what, bool ok) {
    ++checks;
    if (ok)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << "\n";
}

void expectNum(const char* what, long got, long want) {
    ++checks;
    if (got == want)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << ": want [" << want << "] got [" << got << "]\n";
}

const QSize kArea(800, 600);

} // namespace

int main() {
    // ---- Nothing to show when there is nowhere to go ---------------------
    {
        const kai::ScrollBar bar = kai::scrollBarGeometry(kArea, 0, 24, 0);
        expect("no history means no indicator", !bar.visible);
        expect("and a click in its place selects text", !kai::trackContains(bar, QPoint(790, 300)));
        expect("an empty area shows nothing",
               !kai::scrollBarGeometry(QSize(0, 0), 100, 24, 0).visible);
        expect("no visible lines shows nothing",
               !kai::scrollBarGeometry(kArea, 100, 0, 0).visible);
    }

    // ---- The thumb says where the window is ------------------------------
    {
        const kai::ScrollBar at_bottom = kai::scrollBarGeometry(kArea, 200, 24, 0);
        expect("the indicator appears once there is history", at_bottom.visible);
        expectNum("at the live bottom the thumb sits at the bottom",
                  at_bottom.thumb.bottom(), at_bottom.track.bottom());

        const kai::ScrollBar at_top = kai::scrollBarGeometry(kArea, 200, 24, 200);
        expectNum("at the oldest line the thumb sits at the top", at_top.thumb.top(),
                  at_top.track.top());

        const kai::ScrollBar middle = kai::scrollBarGeometry(kArea, 200, 24, 100);
        expect("halfway back puts the thumb in between",
               middle.thumb.top() > at_top.thumb.top() &&
                   middle.thumb.top() < at_bottom.thumb.top());

        int previous_top = at_bottom.thumb.top();
        bool monotonic = true;
        for (int offset = 0; offset <= 200; offset += 10) {
            const int top = kai::scrollBarGeometry(kArea, 200, 24, offset).thumb.top();
            if (top > previous_top)
                monotonic = false;
            previous_top = top;
        }
        expect("scrolling back only ever moves the thumb up", monotonic);
    }

    // ---- The thumb says how big the window is ----------------------------
    {
        // 200 lines of history, 24 of them on screen, in a 600 px track: the
        // viewport is 24/224 of the whole, so the thumb is about 64 px.
        const kai::ScrollBar bar = kai::scrollBarGeometry(kArea, 200, 24, 0);
        expectNum("the thumb is the viewport's share of the track", bar.thumb.height(), 64);
        expectNum("the track fills the height minus its margin", bar.track.height(), 594);

        // A scrollback 100 times deeper than the window would make the thumb
        // unreadably thin; it stops at the floor instead.
        const kai::ScrollBar deep = kai::scrollBarGeometry(kArea, 20000, 24, 0);
        expectNum("a deep scrollback keeps the thumb grabbable", deep.thumb.height(),
                  kai::kScrollBarMinThumb);

        // Almost nothing above the window: the thumb is nearly the whole track,
        // and there is exactly one step of travel left.
        const kai::ScrollBar shallow = kai::scrollBarGeometry(kArea, 1, 24, 0);
        expectNum("a shallow scrollback leaves the thumb nearly the full track",
                  shallow.thumb.height(), 570);
        expectNum("sitting at the bottom of the track", shallow.thumb.bottom(),
                  shallow.track.bottom());
        expectNum("one line of history is one step of travel",
                  kai::scrollBarGeometry(kArea, 1, 24, 1).thumb.top(), shallow.track.top());
    }

    // ---- Dragging the thumb reproduces the position ----------------------
    {
        const int history = 300;
        const int visible = 24;
        bool exact = true;
        int worst = 0;
        for (int offset = 0; offset <= history; ++offset) {
            const kai::ScrollBar bar = kai::scrollBarGeometry(kArea, history, visible, offset);
            const int back = kai::scrollOffsetForThumbY(bar.thumb.top(), bar, history, visible);
            worst = std::max(worst, std::abs(back - offset));
            if (back != offset)
                exact = false;
        }
        expect("grabbing the thumb lands on the same line every time", exact);
        expectNum("with no rounding error at all", worst, 0);

        // Dragging past either end must not run off the scrollback.
        const kai::ScrollBar bar = kai::scrollBarGeometry(kArea, history, visible, 0);
        expectNum("dragging above the top goes to the oldest line",
                  kai::scrollOffsetForThumbY(-500, bar, history, visible), history);
        expectNum("dragging below the bottom returns to the live bottom",
                  kai::scrollOffsetForThumbY(5000, bar, history, visible), 0);
        expectNum("with no indicator there is nothing to drag",
                  kai::scrollOffsetForThumbY(300, kai::scrollBarGeometry(kArea, 0, 24, 0), 0, 24),
                  0);
    }

    // ---- Hit testing ------------------------------------------------------
    {
        const kai::ScrollBar bar = kai::scrollBarGeometry(kArea, 200, 24, 100);
        expect("the thumb is a hit", kai::thumbContains(bar, bar.thumb.center()));
        expect("the track above it is a hit too",
               kai::trackContains(bar, QPoint(bar.track.center().x(), bar.track.top())));
        expect("above the track is not",
               !kai::trackContains(bar, QPoint(bar.track.center().x(), bar.track.top() - 2)));
        expect("beside the track is not",
               !kai::trackContains(bar, QPoint(bar.track.left() - 2, bar.track.center().y())));
        expect("the track outside the thumb pages rather than drags",
               !kai::thumbContains(bar, QPoint(bar.track.center().x(), bar.track.top())));
    }

    if (failures == 0) {
        std::cout << "PASS: " << checks << " scroll indicator geometry checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " scroll indicator checks failed\n";
    return 1;
}
