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
