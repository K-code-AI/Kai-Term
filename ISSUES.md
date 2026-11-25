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
