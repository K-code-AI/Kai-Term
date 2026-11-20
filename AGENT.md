# Kai-Term — Agent Work Journal

## Project Overview

Kai-Term is a native Linux terminal workspace: a VT terminal emulator, a dual-pane file manager, and a native multiplexer with persistent sessions. It uses its own screen model, PTY handling, Unix-socket IPC and a session daemon; it does not use tmux or GNU Screen.

## Current Status

- Build: `cmake --build build -- -j4` succeeds.
- Tests: all 28 `build/tests/kai-term-*` binaries pass after the changes below (two clean
  back-to-back runs on an idle machine, run under a fork cap and a watchdog — see Next
  Steps on the daemon fork-storm).
- Everything through the 2026-11-19 documentation audit is committed and pushed to
  `origin/main`. The 2026-11-19 config split, colour picker, body settings, the
  full-screen resize-loop fix, the config-wins colour authority, the palette-colours-after-
  resize fix, the no-resize-on-save fix, and the two transparency settings are done here and
  committed at the end of this session.
- Verified: a view built around a session now takes the session's screen before it is shown, so the black window is gone without a keypress.
- Verified: snapshots carry scrollback history, including the primary screen and history behind an alternate-screen program.
- Verified: a full-screen program (btop, tmux) no longer hangs when there is scrollback to
  scroll back to — the resize loop that reflowed the whole history on every frame is broken
  by feeding a restored screen to the model in one piece (`tests/snapshot_resize.cpp`).
- Verified: a colour named in the config now wins over the session on every window — fresh,
  new, and attached — so fish's startup `OSC 10/11` and a session's own colours no longer
  wash the theme back to defaults (`tests/colour_authority.cpp`, and the attach +
  OSC-override cases in `tests/app_window.cpp`).
- Verified: a config palette colour survives a resize. A resize re-sends the whole screen,
  and the re-sent cells now carry the palette index they were asked for, so each view
  re-resolves them through its own locks instead of receiving the session's RGB
  (`tests/app_window.cpp`, the token-wears-the-locked-colour-after-resize case).
- Verified: saving colours in the picker does not resize the window (`tests/app_window.cpp`,
  which moves a window off its configured grid, saves, and requires the grid to stay).
- Verified offscreen: the settings menu opens on Ctrl+Shift+K, previews a font change on
  the live window, puts it back on Esc and writes the file on Enter; its rows clamp to
  their ranges under 200 clicks of `+`; and its mouse clicks — a row, an arrow, `save` —
  land where they were painted (`tests/app_window.cpp`, the settings-menu block).
- Verified offscreen: the two transparency settings reach the window's alpha, the picker
  previews them as they are typed, and they save to `kai-term.conf` as fractions
  (`tests/app_window.cpp`, the transparency block). Not verified on a live compositor —
  whether the desktop actually shows through is a thing only a real screen can confirm.
- Not verified: visual inspection on a live Wayland/X11 session; this machine has no X server.

## Current Task

The 2026-11-19 work: the settings split into two files (`kai-term.conf` for behaviour,
`theme.conf` for colours) with `font_size`, `cols`, `rows` and the two transparency
alphas now honoured at
startup; the theme picker grew to edit all sixteen palette colours as well as the five
chrome colours; and a full-screen program hanging once there was scrollback was traced to a
resize loop and fixed. The picker's cursor sat one glyph left of the digit it edited (the
`#` of `#rrggbb` occupies the first cell), and the colour writer rendered nibbles 10-15 as
`'a'+v` instead of `'a'+(v-10)`, so a saved colour with a letter in it came back unreadable
— both fixed, with tests that use letters, not just digits.

The latest piece (Kaida, 2026-11-19, after the above): a saved theme was being washed back
to defaults on a fresh window, a new session, and an attached old session, because fish and
prompt tools write `OSC 10/11` at startup and a session's snapshot carries its own colours.
Now **a colour named in the config is a lock in the `Screen`** that outranks `OSC 4/10/11`
and holds on every window including an attached one; a colour the config left unnamed stays
the session's to recolour. The picker preview moves that lock too (so it stays live and
beat the session), and re-reads the config after a save so the locked set matches the file.
`--config-dir DIR` was added as the one way to run against a different set of settings.

A separate, unfixed finding is recorded under Next Steps: a fork-exec loop of session
daemons was observed again on 2026-11-19, this time while running the test suite repeatedly
(not only throwaway probes), and it reached ~577 `kai-term serve` processes at load 47 before
being frozen and killed. It does not happen in normal single-window use, but it is reachable
and dangerous, and the suite is now run under a fork cap and a watchdog until the daemon's
stale-socket start path is hardened so a daemon can never start another daemon.

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
- 2026-11-19: there is a config file. `src/config/config.hpp`, key=value like
  Old-Term's, no Qt, tested headless: `~/.config/kai-term/config` (`KAI_TERM_CONFIG`
  to point elsewhere), `#` comments, colours `#rrggbb` and nothing as colourful as a
  name. An unknown key names itself with its line and the rest of the file still
  applies — a setting skipped in silence is an afternoon of wondering — and the
  environment beats the file everywhere, because the environment is what you are
  holding right now. It carries `confirm_quit` and the first five colours, which reach
  the window as a `Theme`; the rule underneath them is the recolour rule again: a theme
  decides the next screen, cells keep what they were written with, and a window
  attached to a session takes the window chrome but never the session's own colours —
  those crossed a process boundary to be here. One bug found by the test that was
  written for it: stripping `#` comments before parsing ate every colour value, since
  both begin with `#`; a value that starts with one is a colour, and says so now.
  TOML, which a doc once promised, is off the pending list — key=value matched the
  words people already type, and a parser you can read in one sitting is worth a
  dependency you do not have.
- 2026-11-19: the attached view got its way out. Ctrl+S on its own sends `Detach`
  (no discard, so the daemon keeps the shell) and returns the terminal with status 0;
  before the session's screen takes over, a one-line notice names the session and the
  key. A terminal cannot tell Ctrl+S from Ctrl+Shift+S, so in a view with no window the
  plain byte is the leave key — raw mode already turned flow control off, so nothing is
  stolen from programs, and only a lone Ctrl+S leaves so a paste carrying the byte is
  sent whole. `tests/cli_surface.cpp` drives it over a real pty (forkpty, write 0x13,
  expect exit 0 and the session still listed) — 35 checks, run three times for flakiness.
  The persistent status line is deliberately left undone: a raw byte relay cannot hold a
  row against a full-screen app that addresses its cursor absolutely, so a real one
  wants the view to emulate and repaint, which is its own milestone, not paint on a relay.
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
- 2026-11-19: the settings split into two files at Kaida's direction — behaviour in
  `~/.config/kai-term/kai-term.conf`, colours in `~/.config/kai-term/theme.conf`, both
  overridable by `KAI_TERM_CONFIG` / `KAI_TERM_THEME`. The reader is lenient about which
  file holds which key and reads the theme last so a colour in either file wins; a future
  `/etc/kai-term/` system-default layer is deferred to the installer work.
- 2026-11-19: the theme picker grew from five chrome colours to twenty-one rows — the
  five plus all sixteen palette colours (`color0`..`color15`, labelled by name so a person
  picks "bright magenta", not an index). The palette reaches the screen through a new
  authority stack in `Screen::paletteColor`: OSC 4 session colours beat the theme palette,
  which beats the built-in table; a theme change repaints future writes, and the in-row
  swatch is the live feedback.
- 2026-11-19: the picker's cursor sat one glyph left of the digit it was editing, because
  the `#` of `#rrggbb` occupies the first cell — the underline is at `label_w + 1 + digit`,
  not `label_w + digit`. A test that only round-trips digit-only colours could never have
  caught the next bug either: `colourText` rendered nibbles 10-15 as `'a'+v` instead of
  `'a'+(v-10)`, so a saved colour containing a letter came back unreadable — the file
  looked written correctly but loaded as garbage. Fixed, with tests that use letters.
- 2026-11-19: `font_size`, `cols`, `rows` and `opacity` are honoured at startup now, read
  from the real `kai-term.conf` before the window is built so the PTY is born at the
  configured size and the font is set before the window is sized to the grid.
- 2026-11-19: **a full-screen program (btop, tmux) hung whenever there was scrollback, and
  ran fine when there was not.** The cause was a resize loop, not a parser bug. When a
  session changes size the daemon re-sends the whole screen as a snapshot; that snapshot
  begins with `ESC[?1049l` (to paint the primary buffer) and re-enters the alternate screen
  at the end. With scrollback the snapshot is large and the widget's reader split it across
  several reads; a boundary landing between the leave and the re-enter showed the window a
  transient primary buffer, so it flipped its padding, resized the PTY, and the session
  answered with another snapshot — an endless loop that reflowed the entire scrollback on
  every turn, saturating the GUI thread. A small snapshot (no scrollback) lands in one read
  and the transient is never seen, which is exactly why it only hung with history. The fix
  is in `src/terminal/terminalwidget.hpp`: the reader now drains the whole buffered run
  into one event, so a snapshot reaches the model in a single `feed` and its transient
  alternate-screen leave is invisible to `syncGridToScreen`. `tests/snapshot_resize.cpp`
  drives a large alternate-screen snapshot through the reader with a tape backend and
  asserts the window neither leaves the alternate screen nor resizes when it is applied;
  it fails on the old code and passes on the fixed code.
- 2026-11-19: a fork-exec storm of `kai-term serve` daemons (500+, load 47) was seen while
  stress-testing with throwaway probes that shared one hardcoded private runtime directory
  across many rapid runs. It did not reproduce under the test suite or normal use, and was
  cleared by freezing and killing the group; but tests can leave a daemon running on the
  default runtime dir, and the daemon's stale-socket start path (`ensureDaemon` →
  `startDaemon`) is the thing to harden. Recorded under Next Steps.
- 2026-11-19: **a saved theme was being washed back to defaults.** Kaida reported the
  config colours not applying "when starting fresh, when starting a new session, and even
  when attaching an old session that was previously themed." The config was being read all
  along — an isolated probe proved it — but the theme was applied *first* and then washed
  out: fish, starship and prompt tools write `OSC 10/11` at startup, a session's snapshot
  carries its own colours, and the attach path (`adoptConfig(false)`) deliberately skipped
  screen colours. The fix makes **a colour the config named a LOCK in the `Screen`**
  (`lockDefaults`, `lockPalette`, `colourLocks`/`setColourLocks`) that outranks `OSC 4/10/11`
  and the built-ins on every window including an attached one; a colour the config left
  unnamed stays unlocked so a program that legitimately recolours is still heard. The
  palette authority is now config-lock > session `OSC 4` > theme > built-in.
  `tests/colour_authority.cpp` checks the rule headless; `tests/app_window.cpp` checks a
  session painting itself magenta over a locked theme (with a control that reads the raw
  snapshot to prove the session really did change) and that an attached window wears the
  theme; and an empty-config case checks an unnamed colour still follows the session.
- 2026-11-19: the picker preview had to move the lock too. Locking the config colours made
  `applyTheme`'s `setDefaults` a no-op against a locked field, which would have frozen the
  preview on the saved value; `previewPicker` now locks the whole previewed theme and hands
  the saved lock set back untouched on a cancel, and a save re-reads the config so the
  locked set becomes exactly the colours the file now names.
- 2026-11-19: `--config-dir DIR` added to `main.cpp` (stripped before Qt sees argv) and to
  `config.hpp` (`setConfigDir`/`configDirOverride`, consulted by both file paths after the
  per-file env override). It is the escape hatch Kaida asked for: the saved theme supersedes
  everything by default, and pointing at another directory is the one way to override that.
- 2026-11-19: the daemon fork-storm recurred — ~577 `kai-term serve` at load 47 — this time
  while running the test suite repeatedly, not only throwaway probes. Cleared by freezing
  (a frozen process cannot fork) then killing; the `comm` of a `serve` is `exe` because it
  execs `/proc/self/exe`, so `pkill -x exe` is the fast, correct target. The suite is now
  run under `ulimit -u` plus a watchdog; hardening the daemon so it can never start another
  daemon is now a required Next Step, not a maybe.

- 2026-11-18: `src/process/pty.hpp` gained a `cwd` argument and calls `chdir(cwd)` in the child before `execl`, so a shell starts in the requested directory without a typed `cd` command.
- 2026-11-18: `src/session/daemon.hpp` passes `spec.cwd` to `Pty` instead of writing `cd ...` into the shell.
- 2026-11-18: `src/session/cli.hpp` gained `homeDirectory()` and uses it as the default `cwd` for `session new` and `startWindowSession`; `src/app/appwindow.hpp` uses it for the fallback local shell too.
- 2026-11-18: `tests/session.cpp` gained a check that a window's shell starts in the user's home with no `cd` command typed.
- 2026-11-18: README and RESUME were corrected from `daemon-v2.sock` to `daemon-v3.sock`, and README now says a new shell starts in the user's home.
- 2026-11-19: **a config colour now reaches the cells that are WRITTEN, not only the
  accessors.** Four separate leaks took it out, found one at a time by measuring a written
  cell (`screen().at(r,c).fg`) instead of trusting `defaultForeground()` — every earlier
  test read the accessors, which is why they all passed while the bug was live. The SGR
  write path (`0`, `39`, `49`, `clearCell`) used the raw defaults; `RIS` wrote the built-in
  `220,220,220` into the current rendition; a session's `OSC 10/11` dragged the current
  rendition to the raw value it wrote instead of the effective default; and a snapshot sent
  each cell's resolved RGB as an explicit `38;2;…` triple, which no lock can touch, so a
  resize — which re-sends the whole screen — reset all sixteen palette colours to the
  session's. The last one is fixed in the model rather than at the symptom: a `Cell`
  carries the palette index it was asked for (`fg_role`/`bg_role`, `kRgbColour` for an exact
  colour), and a snapshot sends `38;5;n` for those and RGB only for a truecolour. Roles are
  also what makes a re-sent screen agree with the view's own live output.
  `tests/app_window.cpp` drives the real daemon path and reads the ink of the printed
  token — taking the *last* match on screen, because a token typed at a prompt is on the
  screen twice.
- 2026-11-19: **saving colours no longer resizes the window.** `adoptConfig` re-reads the
  file after a save to refresh the locks, and re-reading applied `cols`/`rows`/`font_size`
  a second time, snapping the window back to the configured grid. It takes `apply_body`
  now and the picker save passes `false`; a test resizes a window away from its config
  grid, saves a colour, and requires the grid to stay put.
- 2026-11-19: **real transparency, and why `setWindowOpacity` was doing nothing.** On this
  desktop (Wayland, niri) `QWidget::setWindowOpacity` asks an X11 window manager for
  `_NET_WM_WINDOW_OPACITY`; Wayland has no such request, so the value was stored,
  `windowOpacity()` reported it back — and the window stayed solid. The offscreen test
  platform reported 0.8 and passed, which is what hid it. Transparency is per-pixel now:
  `background_opacity` and `foreground_opacity` (the older `opacity` still reads, as the
  ground), `WA_TranslucentBackground` whenever either is below 1, and every painted colour
  through the widget's `ground()`/`ink()`, so the desktop comes through the ground while
  the text stays as solid as chosen. Both are picker rows too, typed `1..100` (100 = 1.0),
  written as a fraction to `kai-term.conf` — not to `theme.conf`, where a number nothing
  reads would have gone.
- 2026-11-19: **the settings menu (Ctrl+Shift+K), and the rule that an invalid value
  cannot be reached.** `src/config/settingspicker.hpp` is the Qt-free model — rows that
  know their range or their short list of answers — with the widget painting them and the
  window as the preview, the same shape as the colour picker. Clamping happens in every
  mutator rather than at save time, because the preview is live: a padding of 4000 pixels
  would spend the window's pixels long before a file was written. Ranges are the config
  parser's own, so nothing the menu can reach is a line startup would refuse, and
  `tests/settings_picker.cpp` hammers every row with every digit, every step and both
  arrows and asserts the value stays inside its range the whole time. Three settings came
  with it: `scrollback` (`Screen::setHistoryLimit`, honoured in the **daemon** as well as
  the window — the daemon holds the history a snapshot is built from, so a view asking
  for more than the daemon keeps would silently get less), `padding`, and `cursor_style`,
  which is `Screen`'s *default* shape: DECSCUSR still lets a program pick its own, and
  `CSI 0 q` / `CSI 9 q` come back to the person's answer instead of a constant.
- 2026-11-19: **both overlay panels take the mouse.** A press answers to the geometry the
  painting **last used** — the paint records its row rects, arrows and buttons and the
  press reads that record, because laying a click out a second time is a second opinion
  about where the pixels went and the two drift into clicks landing on the wrong line.
  The wheel walks the list rather than scrolling the terminal behind it. Percentages are
  drawn three digits wide (`100`, `075`, `005`) at Kaida's request: "it's quite hard to
  have the cursor in the right spot when it keep change from a 3 digit to 2 digit to 1
  digit value … keep it at 100% 075% 005% so the cursor is always accurate" — a value
  that changes width moves the digits out from under a cursor that stayed put.
- 2026-11-20: **the two words at the bottom of an overlay are keyboard stops now.** Tab
  walks list → `save` → `leave`, the arrows step between the two words, Up goes back to the
  rows, and Enter answers whichever one the keys are on. Enter and Esc always did their
  work, but nothing on screen could be moved to, which left them decoration for anyone with
  their hands on the keys. The focused word is filled the way a focused row is, and any
  accepted edit — typed digit, `+`, walking the rows, clicking a row — takes the keys back
  to the list. They also moved a row clear of the hint under them ("a bit too close to the
  control indication written under them").
- 2026-11-20: **transparency left the colour menu.** `background_opacity` and
  `foreground_opacity` are settings, not colours, and the settings menu has both; the
  colour picker is 21 colour rows again, `FieldKind` and the percent row editing are gone
  from `themepicker.hpp` with it, and the picker's save no longer splits two kinds of row
  across two files. The shared numeric helpers (`percentText`, `fractionText`,
  `percentFromFraction`, `fractionFromPercent`) stay where both panels and the file reader
  can use them.
- 2026-11-20: **the overlay's keyboard stops are one ring, and the second word is
  `cancel`.** The first cut put Tab between the list and the two words; Kaida turned it
  down — "no tab to reach save and leave, they should be reachable when going down. the
  interfaces must be Cohesive" — so walking down past the last row now lands on `save`,
  then `cancel`, then the first row, and Up runs it backwards. One gesture moves the keys
  everywhere, which is also why Tab is simply Down here as it is in the rows. `tests/app_window.cpp`
  asserts the ring directly through a cursor accessor: inferring where the keys are from
  which setting happened to move is how a test agrees with a wrong implementation.
  (That accessor earned its keep immediately: clicking a row's centre hit the `-` arrow
  painted left of centre, which is correct behaviour and a test that had been guessing.)

## Next Steps

- **A persistent status line in the attached view**, if it is wanted: which session,
  what is running in it. The escape hatch is built (Ctrl+S leaves, shell kept, entry
  notice says so). A line that survives a full-screen app cannot be painted on a raw
  byte relay — an app that addresses its cursor absolutely will overwrite it — so a
  real one means the view keeps a Screen model and repaints it a row down, the way the
  Qt widget does. That is a renderer for the CLI, a milestone of its own, and the
  tmux_e2e test is proof of how much care terminal repainting wants; do not fake it.
- **Harden the daemon's start path against a stale socket — now a required fix.** The
  `kai-term serve` fork-exec storm recurred on 2026-11-19 while running the test suite
  repeatedly (~577 daemons, load 47), so it is reachable, not theoretical. The concrete
  invariant to enforce: **a process that is a daemon must never start another daemon** — set
  an environment marker when `startDaemon` execs `serve` and have `ensureDaemon`/`startDaemon`
  refuse when it is set, which caps the storm at one generation whatever makes a `serve` want
  to connect. Also: `startDaemon` should not let a launch pile up when the socket is stale or
  races itself, and a test that spawns a detached daemon should shut it down so runs do not
  accumulate them. Until this lands, run the suite under a fork cap (`ulimit -u`) and a
  watchdog; the storm is cleared with `pkill -STOP -x exe` then `pkill -9 -x exe` (a `serve`'s
  `comm` is `exe` because it execs `/proc/self/exe`).
- Visually verify the two dialogs on a live Wayland/X11 session: the question over a
  real build, and the session list with four or five sessions in it.
- Visually verify the attached window and its coloured scrollback on a live Wayland/X11 session.
- Visually verify the full-screen fix on a live Wayland/X11 session: `seq 1 500` then `btop`
  and `tmux` in the same window, which is the exact thing that used to hang.
- Restart the session daemon after a rebuild: a running daemon keeps the old snapshot code even after the binary on disk is replaced.
- Keep the widget-level history check, the home-directory check and the new snapshot-resize
  regression test in the suite.

## Project Files

- `src/terminal/screen.hpp` — cell grid, colours, attributes, cursor, scrollback, alternate screen, resize, reflow, and the colour authority stack: config lock > session `OSC 4/10/11` > theme > built-in (`paletteColor`, `setThemePalette`, `lockDefaults`, `lockPalette`). A cell also carries the palette index its colour was asked for (`fg_role`/`bg_role`, `kRgbColour` when no index answers), which is what lets a snapshot send `38;5;n` and keep a lock outrank a re-sent screen.
- `src/terminal/terminalwidget.hpp` — Qt widget, rendering, keyboard, mouse, selection, scrollback view, `feedWhatIsHeld()`, the in-terminal overlay (`askInTerminal()`, `chooseInTerminal()`), the theme picker (which moves the colour lock on preview), the two transparency alphas (`setTransparency`, and every paint going through `ground()`/`ink()` on a translucent surface), the two overlay panels (colour picker and settings menu) with their shared click hit-testing against painted geometry, and the reader that hands a whole buffered run to the model at once.
- `src/config/config.hpp` — the two key=value files (`kai-term.conf`, `theme.conf`): their path rules incl. `--config-dir` (`setConfigDir`), colours, palette, body settings (font, grid, the two transparencies), complaints (no Qt).
- `src/config/settingspicker.hpp` — the settings menu's Qt-free state machine (Ctrl+Shift+K): rows with ranges or choice lists, digit walking, clamping so no keystroke or click can reach an invalid value, and which rows changed (no Qt).
- `src/config/themepicker.hpp` — the picker's Qt-free state machine: rows, digit stepping, hex editing and percentage editing (`FieldKind`, `percentFromFraction`/`fractionText`), and which fields changed.
- `src/session/sessionbackend.hpp` — a window's view of a daemon-owned session.
- `src/session/snapshot.hpp` — bytes that rebuild a screen and its history.
- `src/session/daemon.hpp` — session daemon, PTY ownership, attach/resize/snapshot handling.
- `tests/snapshot.cpp` — snapshot round-trip checks, now including history.
- `tests/colour_authority.cpp` — the colour authority itself, headless: a config lock outranks `OSC 4/10/11` and survives the session handing its colours back (`OSC 104`); an unnamed colour still follows the session.
- `tests/snapshot_resize.cpp` — a large alternate-screen snapshot applied through the reader must not resize the window or drop the program (the full-screen hang regression test).
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
- A restored screen must reach the model in one piece. A snapshot that leaves and re-enters the alternate screen is only safe if the window never sees the gap between the two, because the window gives a full-screen program the whole window and a transient primary buffer would make it resize — and a resize makes the daemon send another snapshot, which is a loop that reflows the whole scrollback every turn. The reader therefore hands the whole buffered run over as a single event. A local shell has nothing buffered between reads, so this is only about snapshots.
- Behaviour and colours live in separate files — `kai-term.conf` and `theme.conf` — because they are edited for different reasons and the colour file is what the picker rewrites on every keystroke. The reader is lenient about which key is in which file and reads the theme last, so a person editing either by hand gets the answer they meant.
- A colour the config names is a LOCK and is the top authority, above a session's own `OSC 4/10/11` and the built-ins, on every window including one attached to a running session. The person chose it; the program did not. A colour the config left unnamed is not locked and stays the session's to recolour, so a program that legitimately changes a colour is still heard. The lock lives in the `Screen`, not the widget, so the authority is one rule in the model, and it is deliberately untouched by a program's colour writes — only the config and the picker move it. (Kaida, 2026-11-19: "the theme saved in the user config should supersede everything when existing.")
- The one way to run against a different set of settings is `--config-dir DIR`, read for both files from `DIR` directly. It beats the default directory but loses to an explicit `KAI_TERM_CONFIG`/`KAI_TERM_THEME` for the single file it names, so tests and one-off overrides keep working.
