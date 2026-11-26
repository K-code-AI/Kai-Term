# Kai-Term — Known Issues

## Fixed

- **Black screen after re-attaching until a key is pressed.** A view built around a session read the attach handshake with its own `FrameReader`, while `DaemonClient` had already buffered the snapshot. The model could stay empty until the first keystroke. Fixed in `src/session/sessionbackend.hpp` by using `client_.pop()` / `client_.nextFrame()`, and in `src/terminal/terminalwidget.hpp` by feeding the backend's held bytes into the model before the reader starts. Verified by `tests/attach_state.cpp` (`PASS: 43 attach state checks passed`).
- **No scrollback history when re-attaching.** `src/session/snapshot.hpp` now writes the session's history before the visible grid and clears the screen and scrollback first, so replaying a snapshot replaces rather than stacks history. Verified by `tests/snapshot.cpp` (`PASS: 83 screen snapshot checks passed`).
- **Primary screen and history behind an alternate-screen program.** The snapshot now paints the primary grid and history first, enters the alternate screen, and paints the alternate grid. `src/terminal/screen.hpp` exposes the primary buffer through `primaryHistorySize()`, `primaryHistoryRow()`, `primaryAt()`, `primaryCursorX/Y()` and `primaryScrollTop/Bottom()`. Verified by a headless probe and the snapshot tests.

- **Colour and attributes lost in history.** History rows were written as plain text. `src/session/snapshot.hpp` now paints each history row with its renditions, so a red warning stays red and bold, underline and reverse survive. Verified by `tests/snapshot.cpp` and an actual attach probe.
- **Shell started in the daemon's directory, and was moved with a typed `cd`.** `src/process/pty.hpp` now takes a `cwd` and calls `chdir(cwd)` in the child before `execl`; `src/session/daemon.hpp` passes it to `Pty` instead of writing `cd ...`. `src/session/cli.hpp` defaults to the user's home for `session new` and `startWindowSession`, and the fallback local shell uses the same home. Verified by `tests/session.cpp` (`PASS: 126 session daemon checks passed`).

- **Daemon aborted with "Resource deadlock avoided" after enough connections retired.** A retiring worker moved its own still-live thread handle into the retired list, and collecting that list joined it — a thread joining itself, which the runtime rightly refuses. `src/session/daemon.hpp` now detaches a worker's own handle instead of joining it, and a retiring worker no longer reaches into the slot table a shutdown may have already swapped out. This was the intermittent crash in `tests/session.cpp` and `tests/app_window.cpp`.

## Known limitations

- **A view that stops reading can stall the session it is looking at.** The daemon writes a view's frames itself, on the thread that produced them, and a socket whose buffer is full blocks that thread until the view reads — including the fresh screen sent to every view after a resize. A window that stops taking input (stopped in a debugger, a machine swapping hard) therefore holds up the session's other views until its own thread notices. The tests found this by hanging: a check that attaches two views and never reads them stalls the daemon's accept loop, so nothing new can connect at all. Giving each view its own queue, or dropping a stale screen for a view that is not keeping up, is the fix; neither is done.
- The primary screen's cursor shape and visibility are not saved when an alternate screen is active; the current modes are restored, not the pre-alternate ones.
- Only the last 2000 history rows are sent in a snapshot. The model caps history at 1000 rows, so this is not reached in practice.
- No visual verification on a live Wayland/X11 session has been done on this machine.

## Closing places down (found 2026-11-25, in the real window)

- **Fixed: closing a place deleted live places from the arrangement.** A program retired when the arrangement was replaced still ran the "my program ended, close my place" path, and the place id it remembered by then named whoever had taken its place over, so each retired program ate one live place out of the tree. A session could be drawn with four places while its arrangement listed one. Only places still in the tree are sized, so the rest kept the tty size they were born with while the window shrank their cells underneath them — and a shell laying its lines out against a grid that is not the one it is painted on leaves text behind when a wrapped line is erased and paints its redraws beside the text they meant to replace. `src/session/daemon.hpp` closes a place only when the ending program still holds one. Measured before the fix: the shell answered `47 42` for a place drawn 23 rows tall.
- **Fixed: a view whose program had gone kept typing into a neighbour.** Views are filed under the number of the place they are looking at; when places closed and the rest were renumbered, a view whose program was gone was left on its old number, which now belonged to somebody else. `src/session/daemon.hpp` now moves such a view to the place the session is focused on.
- **Fixed: a window was not told which place the daemon moved it to.** Reproduce: make 8 extra places alternating, `exit` in place 9, click place 8, `exit` six times, then type — the text landed in place 1 while place 2 was the place in hand and held the cursor. The daemon moved a view off a closed place silently, and the tab bar carried the session's focus rather than this view's place, so `src/terminal/terminalwidget.hpp` kept `tab_focused_` and the backend it typed through — the window rightly stops trusting the bar's focus once a person has chosen a place by hand. The session now says, to each view, which place *that* view is being shown (`mine` on the tab bar frame, per recipient), and the window adopts it, resizes to its cell, and does not echo the choice back. `tests/session.cpp` checks it end to end: the first place made in a fresh window takes the focus, each place after it does too, a close leaves the view on its neighbour, and what is typed comes back from the program of the place shown.
- **Fixed: closing places threw the view to the first place in the session.** `closePane` focused the first remaining place when the focused one closed, so working down through the places snapped the view back to the far end on the first close. It now hands the focus to the neighbour: a place taken out of the middle passes its spot to whoever moves up into it, a place taken off the end steps the focus back one.
- **Fixed: the tabs view had no way in.** `toggleViewMode()` was still in `src/terminal/terminalwidget.hpp`, still sending `SetViewMode` to the session, with nothing calling it — so one-place-at-a-time could only be reached by writing the arrangement out in words. `Ctrl+K T` calls it again. Making a place while in the tabs view no longer asks which way to divide it, since both answers fill the window; the arrangement still records a side, which is what a later return to side by side lays the places out by, and the tree is untouched by a mode change, so the places come back where they were.

- **The tab bar followed the panes' widths.** It mirrored the arrangement, so squeezing a
  place narrow shrank its tab with it until the number on it was hard to read and its half of
  the bar was hard to hit, while a wide place took the bar over. The bar is now split equally
  between the places whatever their sizes, with a hairline between them side by side and none
  in tab view: its job is to name the places and be picked, and a name is the same size
  whoever owns the room.
## Fixed since

- **Resizing only moved one axis.** `resizeDivider` moved the split a place was made by and
  no further, so a place stacked under another could stretch itself taller but never wider —
  the vertical line that decides its width belongs to a branch higher up the tree. It now
  walks up to the nearest branch dividing the way the arrow points, and takes the axis from
  the arrow: Left and Right mean the nearest vertical line, Up and Down the nearest
  horizontal one.
- **Resizing moved the wrong place.** The request was carried out on the session's idea of
  focus — the last place made — so selecting place 1 and pressing `R` stretched place 3. It
  now uses the place *that window* is looking at, the same mapping "new pane" already used.
- **Resize mode said nothing about itself.** The session now sends, per window, the lines the
  place that window is looking at can be moved by: which axis, where the line sits, how far
  it runs, and whether either end has room. The window draws `◀ ▶` and `▲ ▼` on those lines —
  one set on the border of every place the line touches, centred on that place, and none
  where a divider crosses the middle it would have been centred on — and accepts an arrow
  only along an axis with a line and a direction with room, so the keys and the screen are
  one statement. `Ctrl+Shift+Arrow` and the tab bar are refused while a resize is on the way,
  and the middle of the place being stretched says `Resize Mode`, `Enter - Confirm`,
  `Esc - Cancel`.

## Fixed since

- **A setting for the view a new session opens in.** `default_view_mode` in `~/.config/kai-term/kai-term.conf` (`side-by-side` or `tabs`), with a row in the settings menu for it. It applies to a session the window starts for itself; a session joined by `kai-term attach ID --window` keeps the view it already has, because which way the work was laid out is part of what that session is. `Ctrl+K T` still changes the view of the session in hand either way.

- **A divider stopped answering while there was room to move.** Reported twice: "it doesn't
  let you resize after a certain point it just freeze even when there's plenty of space", and
  "2 pane vertical, pane 1 can get way smaller than pane 2, but they should be the same".
  Reproduce: a 50x200 window, two places side by side, `Ctrl+K R`, then `Right` — the line
  moved once and never again, at 99 columns of 200. In an 85-column window it stopped at 51
  of 84 with 33 columns still free. `resizeDivider` stored the moved line as
  `ratio = moved / shared` and `divide` recovered it with `floor(shared * ratio)`, which in
  binary lands a hair under `moved` for a few percent of (extent, cell) pairs, so the line
  read back one cell short: the next press wrote the fraction it had just written, said yes,
  and moved nothing. The other direction stepped off the bad value, sometimes two cells at a
  press, which is why shrinking kept working and one place could end up at 1 column beside an
  82-column neighbour. `ratioForCell` now stores the middle of the cell the line sits in, so
  the floor lands where it was written; that also carries through the six decimals a preset
  is written in, where a dragged line used to move a cell on reload. Checked in
  `tests/layout.cpp` by walking a divider one press at a time across every width from 3 to
  320, the same sweep over rows and through a nested split-and-stack, and a preset round trip
  at every width: one cell per press, one cell at one end and all but one at the other, and
  the line back in the cell it was left in. Verified in the real window — place 1 walked from
  42 columns to 1 and back to 46 in 45 presses, then on to 83 of 84, then `Esc` put it back.

- **Duplicated resize arrows, and a nested line that would not move.** Reproduce: open
  kai-term, make a vertical place, make a horizontal one, go back to place 1, make a
  horizontal one there, then `Ctrl+K R` — a second pair of `▲ ▼` appeared over the places on
  the far side of the vertical line, where the place in hand has no line and the keys do
  nothing. `rectOfNode` answered for a nested branch by walking the tree *from that branch*
  as though it were the root, so a line inside half the window was described as running
  across all of it, and the window paints its marks from that description. The same answer
  feeds `resizeDivider`, so on a vertical line inside a half it was worse than a spare pair
  of arrows: the line was reported at its parent's column, its travel measured against the
  whole window's shared cells instead of the branch's, and a press stored a ratio that drew
  the line back where it started — success, and nothing moved. `rectOfNode` now walks from
  the root and returns the node's real area. Checked in `tests/layout.cpp` on the
  arrangement above (the line runs across its own half only, and no place beyond the vertical
  line is on its run) and on a vertical line nested two deep (reported at its own column,
  running its own quarter, one cell a press, its neighbour above untouched); seven of those
  checks fail without the fix. The marks themselves were painted offscreen from the real
  widget before and after, and the live window moved the nested line on the third press of
  `Right` where it used to move nothing at all.

- **Two sets of resize arrows a couple of cells apart on one border.** Reproduce: four places
  — the window split left-right, each half also split up-down, the two halves not at the same
  height — select one and press `Ctrl+K R`: the middle line carried `◀ ▶` twice, one pair a
  few rows above the other. That was the rule doing what it was asked to do — a set on the
  border of every place the line touches — with four places touching and the two halves'
  middles only two rows apart, so the two statements read as one mark drawn twice. A set now
  goes on the border of every place the line touches *unless another set is already within
  four cells of it along that border*; a line running over places a window apart still says it
  twice, which is the other thing that was asked for. `TerminalWidget::resizeMarks()` answers
  where the marks go, in cells, so this is counted in `tests/widget_panes.cpp` rather than
  photographed; the pictures are `.scratch/marks_doubled_before.png` and
  `marks_single_after.png`. Note that the spare `▲ ▼` over the far half in the same report was
  the `rectOfNode` fault above, already fixed: the window was still running the daemon from
  before it.

- **Resize marks on borders the place in hand was not standing on.** In a window split four
  ways, standing in the bottom-right place drew `◀ ▶` on the middle line up by the *top* row
  and `▲ ▼` on the left half's divider as well as the right's. The rule was "a set on the
  border of every place the line touches", which describes the line rather than the keys: the
  keys belong to the place in hand. A set now goes on that place's own border, on the edge the
  arrow moves — so the bottom-right place gets one set between itself and the bottom-left, and
  one between itself and the top-right, and nothing anywhere else. What an arrow then does is
  unchanged and unchanged-by-accident: it pushes whoever lies beyond that edge, the whole way
  along the line, because a rectangle cut into four always leaves one cut running the whole
  way — `Left` in the bottom-right place widens it and narrows both left-hand places, while
  `Up` takes from the place above and nobody else. Checked in `tests/widget_panes.cpp` (one
  set per pushable border, on this place's edges, none in the far half, none for a line this
  place is not against, none outside the mode) and `tests/layout.cpp` (what a push moves).

- **The middle of three places could only grow one way.** Three places in a row are made by
  two splits, and the middle one is divided from its left neighbour by a line further up the
  arrangement than the one dividing it from its right. Asking "the nearest line along this
  axis" can name only one of them, so one arrow moved the border the place was not pointing
  at — which is also why an arrow sometimes did not appear: the border you were looking at had
  no line of its own to describe, only a neighbour's. A border is now asked about by itself
  (`edgeAlongAxis`, `moveEdge`), the line standing on that side is found by walking up until
  one matches it exactly, and the lines the room has to pass on its way down are held in the
  cells they stand in so the place gets all of the room instead of sharing it with a sibling.
  The window is told about four borders rather than two lines, and marks each of the place's
  own. A place one cell wide was the case that made the old "within a cell of an edge" test
  wrong: both of its edges sit within a cell of the same line, so the press found the wrong
  line, found it had no room, and refused. Checked in `tests/layout.cpp` (three in a row and
  three stacked, both ways, to both extremes; the third of four places grows by all of it and
  nobody else moves), `tests/session.cpp` (five presses over the wire on the left border: the
  place grows five, its neighbour gives five, the far place keeps its room) and
  `tests/widget_panes.cpp` (a set on each border the place can push).

- **Shift was doing something nothing said.** It reverses the border's travel, which is how a
  place gets shrunk back, and resize mode's note said only Enter and Esc. The note now reads
  Arrows - Grow / Shift - Shrink / Enter - Confirm / Esc - Cancel.

- **Shift said nothing about which way it left the arrows.** It was a hold-to-reverse modifier
  on a mode whose whole language is arrows, so the marks kept pointing outward while the next
  press sent the border inward. `Shift` is now a toggle in resize mode — tap it once and the
  mode stays in shrink — and each border carries one point aimed the way the next press sends
  it, so turning the mode round turns the marks round with it. Holding Shift over one arrow
  still reverses that press without moving the toggle, which meant deciding a tap on release:
  a modifier pressed on its own is caught before resize mode is even looked at, and a chord
  pressed under it must not also re-aim the screen for whoever comes after it. The note shows
  which way it is pointed, and its two direction lines answer a press. Checked in
  `tests/widget_panes.cpp` (a tapped Shift flips the mode and turns both marks round on the
  borders they were on; a chord leaves the mode alone; pressing either line of the note sets
  it).
