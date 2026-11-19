# Kai-Term — Agent Work Journal

## Project Overview

Kai-Term is a native Linux terminal workspace: a VT terminal emulator, a dual-pane file manager, and a native multiplexer with persistent sessions. It uses its own screen model, PTY handling, Unix-socket IPC and a session daemon; it does not use tmux or GNU Screen.

## Current Status

- Build: `cmake --build build -- -j4` succeeds.
- Tests: all 21 `build/tests/kai-term-*` binaries pass after the changes below.
- Everything through the 2026-11-19 documentation audit is committed and pushed to
  `origin/main`.
- Verified: a view built around a session now takes the session's screen before it is shown, so the black window is gone without a keypress.
- Verified: snapshots carry scrollback history, including the primary screen and history behind an alternate-screen program.
- Not verified: visual inspection on a live Wayland/X11 session; this machine has no X server.

## Current Task

The documentation audit is done: every doc was re-read against the code and brought in
line — stale test counts, the "no person has looked at it" claims that Kaida's live runs
outran, the old socket path, the missing module entries, and the daemon's new behaviour
in ARCHITECTURE. The two dialogs the window puts up — the question asked when a window is closed with
something running, and the Ctrl+Shift+A session list — are drawn inside the terminal
itself now, in the terminal's own font and colours, and both are answered from the
keyboard: the question offers Yes / No / Save Session with No focused and the first
letters answering, and the list is walked like a TUI picker. The warning paragraph
under the question was dropped at Kaida's request; the question and the three buttons
are the whole dialog. The remaining work is verification on a live display and keeping
the daemon fresh after a rebuild.

## Progress Journal

- 2026-11-18: `src/session/sessionbackend.hpp` was reading the attach handshake with its own `FrameReader` while `DaemonClient` had already buffered the snapshot. The view could therefore be built empty. Fixed by using `client_.pop()` / `client_.nextFrame()` and removing the unused `FrameReader`.
- 2026-11-18: `src/terminal/terminalwidget.hpp` already had `feedWhatIsHeld()`; it feeds the backend's held bytes into the model on the GUI thread before the reader starts. This is what removes the black window.
- 2026-11-18: `src/session/snapshot.hpp` now writes scrollback history before the visible grid. Each history row is written at the top with its renditions and pushed into the scrollback by one line feed at the bottom.
- 2026-11-18: `src/session/snapshot.hpp` now carries both buffers for an alternate-screen session: the primary grid/history are painted first, then `ESC[?1049h` saves them, then the alternate grid is painted.
- 2026-11-18: `src/terminal/screen.hpp` gained `primaryHistorySize()`, `primaryHistoryRow()`, `primaryAt()`, `primaryCursorX/Y()` and `primaryScrollTop/Bottom()` so a snapshot can read the buffer hidden behind an alternate screen.
- 2026-11-18: `tests/snapshot.cpp` was updated to expect history to be transferred and replayed, not discarded, and now compares history cell renditions as well as plain text.
- 2026-11-18: `src/session/snapshot.hpp` gained `paintHistoryRow()` so history keeps its colours and attributes instead of being written as plain text.
- 2026-11-18: README now says the attached history comes with the colours and attributes those lines had.
- 2026-11-18: `tests/attach_state.cpp` was updated to fire the hidden-window check instead of leaving it parked.
- 2026-11-18: README now says the scrollback comes with an attach, including the primary buffer behind an alternate screen.
- 2026-11-18: ROADMAP no longer lists scrollback transfer as missing.
- 2026-11-18: RESUME now says attach transfers the scrollback, including the primary buffer behind an alternate screen.
- 2026-11-18: ARCHITECTURE now says a snapshot carries both buffers, primary first and alternate second.
- 2026-11-18: Test comments in `tests/attach_state.cpp`, `tests/widget_session.cpp` and `tests/snapshot.cpp` were updated to match the new history behaviour.
- 2026-11-18: The `modePrelude` comment in `src/session/snapshot.hpp` was corrected: it now leaves the alternate screen and clears the region before painting.
- 2026-11-18: `tests/attach_state.cpp` gained a widget-level check that a re-attached session with more than a screenful brings its scrollback across.
- 2026-11-18: `ISSUES.md` was created with the two fixed issues and the remaining limitations.
- 2026-11-18: `cmake --build build -- -j4` and all test binaries were run; all passed.

- 2026-11-19: `src/terminal/terminalwidget.hpp` gained the overlay: a question with a
  row of buttons, or a list of rows, drawn by the widget in the terminal's own font on
  a panel of the terminal's own background. It takes the keyboard while it is up
  (arrows, Tab, Enter, Esc, j/k/h/l, digits 1-9 in a list, the pointer too) and
  nothing reaches the shell until it is answered. `askInTerminal()` and
  `chooseInTerminal()` are the two shapes; `overlayActive()`, `overlayQuestion()`,
  `overlayChoices()` and `overlaySelected()` let a test see what is on screen.
- 2026-11-19: the close prompt is one of those overlays — **"Quit while btop is
  running?"** with **Yes / No / Save Session**, in that order, No focused so Enter
  cannot end a build by accident, and the first letter of an answer answers it from
  wherever the focus is (Y, N, S). The warning paragraph under the question and
  `closeDetail()` are gone: Kaida asked for the question and the buttons alone.
  `closeOfferDetach()` became `closeOfferSave()`, and the third answer now saves the
  session it used to merely leave behind.
- 2026-11-19: Ctrl+Shift+A lists the sessions inside the terminal as a walked list
  with a key-hint line, replacing the `QMenu` popup that wore the desktop's theme.
- 2026-11-19: fixed a crash that predates this change: a retiring daemon worker moved
  its own live thread handle into the retired list, and collecting that list joined it
  — a thread joining itself, which throws "Resource deadlock avoided". A worker now
  detaches its own handle, and it no longer reaches into the slot table a shutdown may
  have swapped out. This was the intermittent abort in `tests/session.cpp` and
  `tests/app_window.cpp`; 10 back-to-back app-window runs pass.
- 2026-11-19: the daemon execs by `/proc/self/exe`, so the kernel named it `exe` and
  btop's Program column said exactly that about the process holding every kept shell.
  `src/session/cli.hpp` gained `nameSelfForProcessLists()` — `prctl(PR_SET_NAME)` —
  and the `serve` verb calls it before anything else, so the daemon is
  `kai-term-daemon` now. Verified live: a freshly spawned daemon reads back as
  `kai-term-daemon` in `/proc/<pid>/comm`; a daemon started by the old binary keeps
  the old name until it is restarted.
- 2026-11-19: `tests/tools/grabview.cpp` gained `prompt` and `menu` modes, which
  photograph the two dialogs offscreen; the pictures are what stand in for the eyes
  this machine does not have.
- 2026-11-19: the margin around the grid goes to zero while the alternate screen
  holds the window, and the grid grows into the freed room (`activePadding()`,
  `applyGridGeometry()`, `syncGridToScreen()` in `src/terminal/terminalwidget.hpp`,
  with `geometry_padding_` remembering whose screen this is). The margin exists to
  keep a shell's glyphs out of the compositor's rounded corners; a full-screen
  program paints its own frame, and its colours clashing with the terminal's grey-
  black ring — btop's case — was the complaint. `grabview alt` photographs both
  sides of the switch and the widget smoke test checks the inset, the growth and
  the return.
- 2026-11-19: switching through Ctrl+Shift+A while something runs in the current
  shell asks the same question the close button asks, in the same shape: "Load
  session elsewhere while sleep is running?", Yes / No / Save Session, No focused,
  letters answering (`AppWindow::confirmSwitchTo`). Save Session keeps this shell
  under a name of the kept range and switches anyway; a save that fails does not
  quietly become a switch that loses the work the button was pressed to keep.
  Nothing is asked when nothing is running, and nothing is asked about a session
  that is already kept — its work goes on either way.
- 2026-11-19: the command surface Kaida agreed to verb by verb is built. The words come
  straight after `kai-term` — `list`, `new`, `attach`, `detach`, `save`, `send`, `kill`,
  `start`, `shutdown`, `serve` — a number from the listing is the handle (name still
  wins, so a session called "7" means itself), and `attach --window` opens a window
  instead of taking over the terminal. `kai-term session ...` and `kai-term open NAME`
  are retired and say so rather than failing like a typo. A shell the daemon starts
  carries its number in `KAI_SESSION`, set-not-inherited in the child before exec (a
  daemon started from inside a session must not hand that session to every shell it
  later spawns), which is what lets `save` and `detach` take no arguments inside a
  session and lets `list` star the row for the session you are standing in. `new` prints
  the id and the name behind it; `save` answers with the record's own name, not an echo
  of the handle typed. `tests/cli_surface.cpp` (30 checks) runs the real binary in a
  private `$XDG_RUNTIME_DIR` — retired words, ids, the star, argument-free save, the
  fake-parent `KAI_SESSION=99` that a new shell must not inherit. Fixing the surface
  turned up that `attach 999` had never printed its reason: the old code printed the
  error only on the clean-exit path. The attached view's status line and its
  `Ctrl+S to leave` are the last piece of the agreed surface and are next.
- 2026-11-19: documentation audit. README, ARCHITECTURE, RESUME and ROADMAP were
  re-read against the code: test counts (21 binaries; session 126, widget-session 44,
  app-window 78, close-prompt 18 after the detail line went away, widget-panes 24), the
  daemon socket path in ARCHITECTURE, the module layout and file lists (`sessionmenu.hpp`
  was missing), the M3/M4 "nobody has looked at it" claims Kaida's live use outran, the
  done scrollback item still on M4's needed list, and the new rules — dialogs drawn by
  the widget, the margin belonging to the screen's owner, the daemon's caps, races and
  name — written down where the next person will read them.
- 2026-11-19: `tests/app_window.cpp` catches the question on the widget rather than as
  a top-level `QMessageBox`, checks the button order, the focus, the arrow walking and
  the letter answers, and walks the session list to a session. README, ROADMAP, RESUME
  and ISSUES say the same thing the code does.

- 2026-11-18: `src/process/pty.hpp` gained a `cwd` argument and calls `chdir(cwd)` in the child before `execl`, so a shell starts in the requested directory without a typed `cd` command.
- 2026-11-18: `src/session/daemon.hpp` passes `spec.cwd` to `Pty` instead of writing `cd ...` into the shell.
- 2026-11-18: `src/session/cli.hpp` gained `homeDirectory()` and uses it as the default `cwd` for `session new` and `startWindowSession`; `src/app/appwindow.hpp` uses it for the fallback local shell too.
- 2026-11-18: `tests/session.cpp` gained a check that a window's shell starts in the user's home with no `cd` command typed.
- 2026-11-18: README and RESUME were corrected from `daemon-v2.sock` to `daemon-v3.sock`, and README now says a new shell starts in the user's home.

## Next Steps

- **The attached view's status line and `Ctrl+S to leave`.** `kai-term attach 0` hands
  over the whole terminal with no word about which session it is looking at and no way
  out but ending the shell — the one piece of the agreed command surface not built yet.
  The view has to reserve a row, keep the session's grid below it, and answer Ctrl+S by
  sending `Detach` and returning the terminal, because a terminal cannot tell Ctrl+S
  from Ctrl+Shift+S and the byte that arrives from an attached view is Ctrl+S.
- **Themable, configured inside the terminal with a live preview** — agreed with Kaida
  on 2026-11-19. A small TUI drawn by the widget the way the dialogs are, editing
  palette, accent (the dialogs' focused button is fixed blue today) and spacing, with a
  patch of real grid, cursor, selection and a sample dialog repainted as each choice
  moves. Wants the config file first, so a preview can be persisted; see ROADMAP M4.
- Visually verify the two dialogs on a live Wayland/X11 session: the question over a
  real build, and the session list with four or five sessions in it.
- Visually verify the attached window and its coloured scrollback on a live Wayland/X11 session.
- Restart the session daemon after a rebuild: a running daemon keeps the old snapshot code even after the binary on disk is replaced.
- Keep the widget-level history check and the home-directory check in the test suite.

## Project Files

- `src/terminal/screen.hpp` — cell grid, colours, attributes, cursor, scrollback, alternate screen, resize, reflow.
- `src/terminal/terminalwidget.hpp` — Qt widget, rendering, keyboard, mouse, selection, scrollback view, `feedWhatIsHeld()`, and the in-terminal overlay (`askInTerminal()`, `chooseInTerminal()`).
- `src/session/sessionbackend.hpp` — a window's view of a daemon-owned session.
- `src/session/snapshot.hpp` — bytes that rebuild a screen and its history.
- `src/session/daemon.hpp` — session daemon, PTY ownership, attach/resize/snapshot handling.
- `tests/snapshot.cpp` — snapshot round-trip checks, now including history.
- `tests/attach_state.cpp` — attach-state checks, including hidden-window delivery.
- Other source and test files remain as before.

## Key Decisions

- Documentation keeps up with the code in the same session that changed the code.
  Counts, paths, keybinds and claims about what has been verified by a person are
  statements the docs make on the code's behalf, and a stale statement is worse than no
  statement because somebody will trust it. When something changes, grep the docs for
  it before calling the change finished — the 2026-11-19 audit is what catching up
  looks like, and it is not a job anyone should have to do twice.
- The repository keeps up as well: when the tests pass, the work is committed in
  logical commits that say why, and pushed, before a session ends. Uncommitted work is
  risk carried by the next power cut.
- The terminal's dialogs are drawn by the terminal, not by the desktop. A native dialog
  brings its own theme, font and button order and reminds everybody the terminal is a
  rectangle the window manager decorates; an overlay drawn in the grid's own font reads
  as part of the terminal, which is what kitty's question box taught Kaida to expect.
- The close question is one line and three buttons: Yes / No / Save Session, in that
  order, No focused, first letters answering. The paragraph that used to sit under the
  question was dropped at Kaida's request — the buttons say what the answers are.
- "Save Session" really saves: the third answer calls `saveSession()` before letting
  the window go, rather than only leaving the shell behind unsaid.

- The first screen is taken from the backend on the GUI thread, not from a reader thread that posts events to a widget that may not be shown yet. A hidden widget does not process its own queued events.
- A snapshot replaces the screen state: it clears the screen and scrollback first, then rebuilds history and the visible grid. Replaying it twice must not stack histories.
- History rows carry their renditions. A warning printed in red stays red in the scrollback, and bold, underline and reverse survive too.
- A session inside an alternate-screen program has two buffers. The snapshot carries both so the primary screen and its history survive when the program exits.
