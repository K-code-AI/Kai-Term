# Kai-Term — Agent Work Journal

## Project Overview

Kai-Term is a native Linux terminal workspace: a VT terminal emulator, a dual-pane file manager, and a native multiplexer with persistent sessions. It uses its own screen model, PTY handling, Unix-socket IPC and a session daemon; it does not use tmux or GNU Screen.

## Current Status

- Build: `cmake --build build -- -j4` succeeds.
- Tests: all 29 `build/tests/kai-term-*` binaries pass after the changes below (two clean
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
- Verified offscreen: a menu fits a window far smaller than it wants to be — panel, rows,
  bars and the two words all inside it, none of them painted over the others — and the
  mixer's sample sentence is painted in the colour being mixed (`tests/app_window.cpp`,
  the small-panel block; `build/kai-term-panelview` writes the pictures at four window
  sizes).
- Seen on the live window: the preview's ground switch — the sentence on the window's own
  background, and the word at the end of the strip walking it to black and white, by
  pointer and by arrow (Kaida, 2026-11-20: "The ground now works I saw it").
- Verified offscreen: one thing lit per panel and it is the thing the keys are on — the
  value in the colour list and the settings menu, the colour band in the mixer, the word
  once you walk to it — the mixer titled with the colour it is editing, and the rows a
  couple of pixels apart (`tests/app_window.cpp`; `build/kai-term-panelview` for the
  pictures).
- Verified offscreen: a panel is as large as what it holds — the line of keys is small print
  that breaks over the panel's width, the panel is narrower than that sentence, its corner is
  cut, its title and its last row have air of their own, and nothing is painted outside its
  frame (`tests/app_window.cpp`, the panel-size block; each of the ten rules was turned off
  and the check that owns it was seen to fail).
- Verified by running the real binary: `--help` one row per argument, `<command> --help`
  and `help <command>` one page each, an unknown dash-word answered with the page and exit
  2 instead of a window, and `-c FILE` taking its colours from the `theme.conf` beside it
  (`tests/cli_surface.cpp`, `tests/config.cpp`).
- Verified: `theme` in `kai-term.conf` picks which colours to wear — left out or
  `Default.conf` for `theme.conf`, a bare name for `<config>/themes/<name>`, a slash for
  that path, `KAI_TERM_THEME` above all of it — and only the one file is read
  (`tests/config.cpp`).
- Verified offscreen: the settings menu's last row is that choice, and stepping it wears
  the theme before anything is written, Esc puts the colours back, saving writes the name
  into `kai-term.conf`, and the next window opens wearing it (`tests/app_window.cpp`).
- Verified offscreen: the picker's `theme` line — the name without its `.conf`, shut until
  `e` opens it, a caret the arrows walk, Enter confirming and Esc putting the word back —
  saves under a new name as a new file holding every colour, and asks in the panel before
  writing over a theme that exists (`tests/app_window.cpp`, and `picker-name`, `picker-edit`
  and `picker-overwrite` from `build/kai-term-panelview`).
- Verified offscreen, and found by it: an overlay opens with the keys on its first row. The
  picker had been keeping whichever word the last visit ended on, so the second visit
  opened with the keys already on `save`; the settings menu had the same hole.
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

A finding that was open since 2026-11-19 is now **fixed**: a fork-exec loop of session
daemons — ~577 `kai-term serve` at load 47, reached again on 2026-11-20 by a test tool that
opened a window — cannot happen any more. A launched process carries `KAI_TERM_DAEMON=1`
and refuses to start a daemon when it carries it, so a launch is one generation deep no
matter what `/proc/self/exe` turns out to be; a launch lock makes "is anyone home?" and
"then start one" one gesture instead of a race; and the daemon writes its pid beside its
socket so whoever is locked out names the process instead of hunting it. `kai-term new`
inside a session still works because that marker is cleared from a shell's environment on
the way in. The suite no longer needs a watchdog against it; the fork cap stays because a
test that hangs should still cost a test and not a machine.

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
- 2026-11-20: **the colour mixer (Enter on a colour), and the panels stopped being
  translucent.** `src/config/colormixer.hpp` is the third Qt-free model: one colour held as
  RGB and as HSL, with the last edit deciding which is authoritative and the wheel kept in
  tenths of a degree because a whole degree is worth up to six units of a byte at the
  saturated end (measured: 248,10,11 is 359.6°; 359° is 248,4,0). At tenths, the round trip
  is exact for all 16.7 million colours, so `hsl10ToRgb(mixer.hsl10()) == mixer.colour()`
  holds at every moment — the bars and the wheel are two readings of one thing. The widget
  paints bars as *what pushing them does* (left end that channel at zero, right end at
  full) and drags them with the pointer. Kaida also asked that Enter open the mixer, that
  saving be the word at the bottom rather than a key ("to save we go down to the save
  button, no need for a direct key, but we keep Esc for quick cancel"), that the panels be
  opaque whatever the transparency says ("oppacity shouldn't affect the menu for better
  readability"), and that the titles sit higher and start with a capital.
- 2026-11-20: **a window holds the theme; a session does not.** `applyTheme` set the
  screen's ink and ground only for a window that had started its own shell, so an attached
  window left them to whatever the snapshot re-sent — and a snapshot re-sends them on every
  resize. Any colour the theme file had not *named* therefore came back from the session,
  which is what "theme is lost when attaching a session" was. It applies them either way
  now, and an attached view locks the whole theme, not only the named lines. **Honest note:
  the headless harness could not reproduce the loss** (a session's painted colours did not
  reach the attached view in the test, whatever the code did), so the checks added pin down
  the rule rather than the old failure, and the real confirmation is Kaida's on niri.
- 2026-11-20: **a session spawn was eating the theme, and it was one line.** Starting a
  fresh shell in a window (`saveCurrentSession` → `showSession`) does `screen_ = Screen(...)`
  and a new screen starts with built-in colours and **no locks**, so the theme went out with
  the old screen and the new shell's OSC 10/11 walked in. `showSession` now dresses the new
  screen itself: theme defaults, theme palette, the scrollback and cursor style the window was
  configured with, and the colour locks — which is why the widget keeps its own copy of the
  locks (`colour_locks_`) instead of leaving them only in the screen. Locks are now the whole
  theme for every window, not the named lines for owned windows and the whole theme for
  attached ones; a program colouring its own cells is untouched by that. The test types
  `printf '\033]11;#ff0000\007'` into a real shell and then spawns a fresh one, and it fails
  without the fix (3 checks).
- 2026-11-20: **one thing lit at a time; bars that show where they go.** The row highlight
  followed the *model's* row, so with the keys on `save` the last row stayed lit too — two
  answers on one panel (Kaida). It now requires `focus == 0`; guarded by a check that samples
  a pixel of the row band before and after walking onto `save`, which fails without the fix.
  The mixer's bars are painted from `channelAt(mixer, channel, fraction)` — the channel at a
  fraction with the others at the *precise* wheel — with 7 stops for hue (the wheel laid
  flat) and 3 for lightness (black → colour → white), plus rounded ends and a knob.
- 2026-11-20: **the walking ring had a join in it, and the join was wrong.** `overlayWalk`
  moved `focus` and left the row to the model's `step()`, but the two disagree at the
  boundary: walking down off the last row leaves the model where it was and puts the keys on
  `save`, so stepping *up* from there moved the model one more time and landed on the
  second-to-last row. The walk is now one line of stops (`pos = (pos + dir) % (rows + 2)`)
  that names the row instead of stepping towards it, and both models grew nothing because
  `setRow(int)` already existed. Left/Right change a setting's value on every row except a
  colour's digits — with `previewSetting()`, because a change you cannot see is not a change
  (the first version moved the model and the test caught it). The bottom line reads
  "Arrows to Navigate - Enter to Select - Escape to Cancel" in all three panels; rows are
  centred in the panel rather than hung off its left edge (the panel is as wide as the hint,
  which left the right side empty); the mixer's preview sentence picks black or white when
  the mixed colour is too close to the ink to read; and the colour list is hidden while the
  mixer is up, because two panels stacked read as one crowded panel.

- 2026-11-20: **the preview is the sentence, the arrows reach every row, and a menu fits
  its window.** Three things out of one screenshot. (1) The mixer's preview was a strip
  flooded with the mixed colour and the sample sentence written over it in black or white,
  which painted the menu that colour twice over and the sentence not at all (Kaida: "the
  Quick brown fox line didn't get paint in red when it should be the only thing getting
  painted red on this menu, since it's the preview"). The sentence is painted *in* the
  colour now, on the panel's own ground; when that ground is within a fifth of the scale of
  the colour — mixing the window's own background, which is what the screenshot showed — the
  ground steps away from the sentence rather than the sentence away from the colour. (2)
  Left/Right change the value on every mixer row, the `hex` row included: `nudgeNibble()`
  steps the pair under the cursor, so the row is adjustable rather than only typeable, and
  the cursor a typed number wants moved to Shift+Left/Right. The colour *list* keeps the
  plain arrows for its six digits, per the same message. (3) The panels were laid out from
  their own content with no idea of the window's size — at 430x320 the mixer ran off both
  edges with its hint under its rows and its words on top of them. `panelFont()` hands a
  panel a smaller drawing font until it fits (floor 8px; the terminal's own font is never
  touched), a list shrinks only to a dozen rows and then scrolls as it always did, the hint
  elides instead of overflowing, and `paintOverlayButtons` takes the panel's font — it had
  been measuring itself against the *window's*, which is how its words landed on the last
  row. `tests/tools/panelview.cpp` photographs all three panels at 960x540, 620x460, 430x320
  and 330x220, because pictures are how this machine sees. The new checks read the painted
  pixels — the sentence is the pixel furthest from the strip's own ground — and ask whether
  the last row sits above the two words; the pixel ones fail without the fix, and the
  row-and-word one fails with the fallback font switched off.

- 2026-11-20: **the command line answers a mistake with the list of what you can type.**
  IDEA.md asked for `-c /path/to/config.conf`, for an invalid argument to bring up the
  help, and for that help rewritten with one argument per row and a per-command page
  (`kai-term list --help`). The old help was two texts — prose in `main.cpp`, a list in
  `runSessionCommand` — and they had already started to disagree, so there is now one
  table of commands in `cli.hpp` and both pages are drawn from it; `--help`, `help`,
  `help <command>` and `<command> --help` are four ways to ask for the same two pages.
  `-c FILE` reads the settings from FILE and the colours from the `theme.conf` beside it,
  because one argument should decide one thing; a directory is accepted for it too, since
  path completion hands over a directory as often as a file, and `setConfigDir` now clears
  a file named earlier rather than leaving half of the previous answer standing (the first
  `-c` test caught exactly that).
  The rule that changes behaviour for a person is the small one: a dash-word the program
  does not know used to fall through to *"not a session verb, so a window"*, which is how
  `kai-term --confg-dir x` came to open a working-looking terminal wearing the default
  settings. It prints the page and exits 2 now. That also meant `help` had to become a
  routed word — `kai-term help attach` opened a real window during this change, which is
  the second time this week that a test written before the code it tests has caught
  something the author had not thought about. Qt's own options are unaffected in the way
  anyone uses them from here: `QT_QPA_PLATFORM` and its neighbours come through the
  environment, and `QT_QPA_PLATFORM` is what the whole suite runs on.
  Fifteen checks in `tests/cli_surface.cpp` and seven in `tests/config.cpp`, each seen to
  fail with its belt off: with the two guards removed the typo'd option opens a window and
  is killed for taking too long (`code 200`), and `list --help` errors instead of
  answering.

- 2026-11-20: **light what the keys are on, and let the rows breathe.** IDEA.md asked for
  three things on the panels: the mixer titled with the colour it is editing, a couple of
  pixels more air between the elements, and "the highlight shouldn't color the whole row,
  it should look cleaner, and that on all settings page". The rule that fell out of the
  third one is *light the thing the keys are standing on*: in the colour list and the
  settings menu that is the value — the six digits, the number — and in the mixer it is the
  colour band, because there the band and the number are the same value seen two ways and
  the band is the one you push (Kaida, on what to light instead: "highlight the value being
  editied except on the colour mixer where the whole color band should be highlighted
  instead of the numerical value"). The band is painted *behind* the ramp rather than over
  it, so lighting a band never paints over one of its colours; where a row has no band —
  the hex row — the digits get it, as everywhere else. A row that *is* the answer keeps its
  whole-line band: the question overlay and the session list have no value to light, and
  there "this line" is the true thing to say.
  Two things the first cut got wrong and the pictures caught. A band sized to the column's
  widest value gave `font size = 14` a nine-cell bar running into its arrows, which is the
  same lie as a full-row band and uglier; it is measured against the row's own text now.
  And the band was being painted after the text that it sits behind, because measuring the
  text was the reason to compute it early — so `14` disappeared under its own highlight.
  `kPanelRowGap` is in pixels, not cells, so the air does not shrink away on the very small
  panel that most needs it; `panelFont` takes the room as `extra_h` so a panel that has to
  shrink accounts for what it is adding, and the list's `visible` row count divides by
  `ch + gap` like everything else that measures a row.
  `OverlayHit` now records `highlight`, the one lit rect, because "only one thing is lit"
  is a rule worth checking and a test that has to work out where a highlight was drawn is a
  test that disagrees with the panel about what a row is. Six checks, each confirmed to
  fail with its belt off: painting the row again loses the value checks in the menu and the
  list, dropping the band in the mixer and the name from the title lose theirs, and setting
  the gap to zero loses the air.

- 2026-11-20: **the preview's ground is a choice, not a guess.** The ground that evening
  was decided *for* you: the panel's own ground unless the colour came within a fifth of
  the scale of it, in which case the ground stepped toward white or black. Kaida tried it
  and asked for something honest instead — the ground is the window's own background at
  its own opacity, and a switch on the side of the strip alternates `background` / `black`
  / `white` ("The ground should just be of the current background color (with the opacity
  of the backgroud) and have a switch on the side that alternate between Black / White /
  Current Background colour"). The auto-blend is gone, and with it the case where the menu
  quietly decided a colour was unreadable and moved the ground out from under it: mixing
  the background now shows a sentence in the colour of the thing it is written on, which
  looks like a bug and is the truest thing the panel can say about that colour. The switch
  is a place the keys can be — walking up off the first channel stops on the sentence and
  Left/Right walk the three grounds, because a choice reachable only by pointer is a
  choice the keyboard does not have (Kaida: "left right when the cursor is on the preview
  zone seems nice"). Typing a digit there changes nothing, because it would be a number
  nobody is standing on. The choice lives on the widget rather than in the mixer, so it
  outlives one mixing: a ground picked because the window's own was unreadable was picked
  for the window. The checks read the strip's modal colour as its ground and the furthest
  pixel as its ink, inside the border and left of the switch — the accent border and the
  word itself are both colours that are not the sentence, and a reader that did not know
  that mistook them for it.

- 2026-11-20: **a daemon is one process, and nothing starts a second one.** The storm was
  open since 2026-11-19 and came back this evening through a route nobody had thought of:
  `startDaemon` execs `/proc/self/exe` and trusts the program that comes back to read the
  word `serve`. A helper built from these same sources — a tool that opens a window and
  takes no notice of its arguments — re-ran *itself* instead, asked for a daemon, and
  started another copy; five hundred processes and load 270 later the shape of the fault
  was clear, which was that nothing in the launch path knew it was the launch path. Three
  belts, all of them checked by a test that fails when the belt is switched off. (1) A
  process launched to be the daemon carries `KAI_TERM_DAEMON=1`, set before the exec so it
  is carried whatever the binary turns out to be, and `startDaemon` refuses to start a
  daemon when it is set — one generation deep, always. `serve` says the same about itself,
  so a daemon started by hand is under the same rule. It is cleared from a shell's
  environment on the way in, because a shell that believed it was the daemon would answer
  "the daemon does not start another daemon" to someone standing in a shell whose daemon
  had just died — the one moment it is needed. (2) A `flock` beside the socket, held across
  the whole of "is anyone home? then start one", so six commands at one moment make one
  daemon and five waiters rather than one winner and five losers whose only contribution
  was processes; a launcher that cannot get the lock says so instead of forking. (3) The
  daemon writes its pid beside its socket and removes it with the socket, and whoever is
  locked out says "already running, at pid N" — the hunt `ps`-and-grep through a crowd
  calling itself `kai-term serve` is what made the last storm expensive to clean up. The
  pid is only read when a listener is actually answering, so the file a killed daemon
  leaves behind cannot point at a process number the kernel has since handed to something
  else. `tests/cli_surface.cpp` counts daemons by *runtime directory and marker* rather
  than by process name, so it can say "one" and mean this test's one.

- 2026-11-20: **a theme is a file you can have more than one of.** IDEA.md's third idea, and
  the last thing in the config that assumed there was exactly one set of colours. The rule
  Kaida wrote is short — "We add a 'Theme = Default.conf' to our config and a sub-directory
  'themes' in ~/.config/kai-term where they get sourced from by default unless a full path
  for the theme is given" — and the whole of it is honoured by three sentences in
  `resolveThemePath`: empty or `Default.conf` means `theme.conf`, which is what has always
  held the colours and what the picker writes, so nothing written before this needs
  converting; a bare name means `<config>/themes/<name>`; anything with a slash is taken
  for the path it is. One file is read per launch — a named theme *replaces* `theme.conf`
  rather than layering over it, because two files both claiming to say the background is one
  colour is one argument about the same pixel — and it is still read last. `KAI_TERM_THEME`
  outranks the lot: the environment is the thing you are holding right now. A `-c` directory
  brings its own `themes/` with it, for the same reason everything else follows that flag.

  Wearing one is a choice row at the end of Ctrl+Shift+K, and the honest version of a preview
  had to be paid for: the code that turns a `Config` into colours and locks was inside
  `adoptConfig`, which re-reads from disk, and a theme you have not saved is not on disk.
  `applyConfigColours` is that code with a name now, so the menu wears a theme before writing
  anything and puts the old one back on Esc.

  Making a new one is the picker's job, and the field is where the argument lived. Kaida's
  shape: "the picker's name field is always visible — saving under the loaded name asks to
  overwrite, a new name writes a new theme", then, on seeing it: "let's not show the .conf
  and keep it implied. also we should require pressing e to edit the field and in edit mode
  the arrow can control the cursor and enter will confirm and get us out of edit mode". So the
  line reads `theme  midnight`, shut until `e` opens it — a letter typed while walking the
  colour list cannot rename the theme you were about to save — and open it takes a caret the
  arrows walk, Backspace taking the letter before it. Enter confirms and leaves the keys where
  they were: confirming a name is not saving, and the word for saving is one step down, where
  it has always been. Esc inside the field puts the word back and leaves the panel up, because
  throwing away the colours somebody just chose along with the letters they did not finish is
  a bigger undo than anyone asked for. A pointer landing on the field opens it the way `e`
  does, because a box you can point at but not type into is a label with a border round it.

  Saving asks once, in the panel, when the name is already a file: the title becomes
  `Overwrite midnight.conf?` and the two words become `overwrite` and `keep it`, same places,
  same walking — a question that opens somewhere else is a question about a panel you have
  just stopped looking at. A name nothing is using writes without a word, and takes every
  colour the window is wearing rather than only the ones that moved, because a new file
  starts empty; then `theme = dusk.conf` goes into `kai-term.conf`, which is where the
  decision lives rather than in the colours themselves. The widget now hands back every
  colour the panel holds and the caller diffs, which is one list instead of two.

  Two bugs the checks found on their own. The first was the field's `.conf`: the widget showed
  what the config holds, so `themeFileName` was appending an extension to a name that already
  had one — the display name came out of `pickerDisplayName` with the full name kept behind
  it, which is the same lesson the hex cursor learned: what is shown and what is stored are
  two different things and the panel has to know both. The second was older than this feature:
  `picker_focus_` was never reset when the picker opened, so a visit that ended with the keys
  on `save` opened the *next* visit with the keys already on `save`. The settings menu had the
  same hole. Both start on the panel's own first row now, and the check that caught it was a
  walk that arrived at the wrong place — the reason to write "the keys are here" as an
  assertion rather than assuming it.

- 2026-11-20: **a panel was as wide as its instructions.** IDEA.md said the settings windows
  were too large and should be more rounded, and when asked where the size was going Kaida
  named it: "the tooltips are the culprit, they take too much space and make the windows
  larger, tooltips should have a reduced size". It was true in the exact sense. The line of
  keys along the bottom was set in the panel's own font, drawn on one unbreakable line, and
  the panel's width was `max(table, hint)` — and a sentence about arrow keys is longer than
  any table of colours, so the hint was the widest thing in every panel and every panel was
  measured from it. Rounding was four pixels on a panel several hundred wide, which is not a
  rounded thing, it is a rectangle with soft edges.

  So: small print (`kPanelHintScale`), broken over as many lines as the panel's width needs,
  and the panel as wide as its table. The type starts below the terminal's own
  (`kPanelFontScale`) because a grid is not a menu — the same sentence `panelFont` already
  said when it shrank a panel to fit, and it was simply never applied until the panel had
  room left over. The corner is `panelRadius`, a curve you can see and one that grows with
  the panel's own type so a small panel is not all curve.

  The bottom of a panel — words, breath, hint — is now one function. Before it was arithmetic
  repeated in three panels, each reserving *one row* for a hint whose height nobody had
  measured, which was fine while the hint was exactly one row tall and is a way for a
  two-line hint to be painted straight through the words. Same reason the picker's list and
  the name line and the words had each been anchored from a different edge.

  Then: "It could be a little bit bigger, it feels cramped now." The first cut had
  eight-tenths type and a row of padding, and taking the sentence out from under a panel is
  not the same as taking the air out of it — the padding was never what made them large.
  Nine-tenths and a row and a half. A change like that is not arguable from a screenshot
  description, which is why `build/kai-term-panelview` photographs the panels at four window
  sizes and every round of this went: change, photograph, look, adjust.

  Then the two gaps: "a bit more space between the title and the actual settings, and a bit
  more space between the settings and the save / cancel buttons". Both are constants now, in
  *rows* rather than pixels — `kTitleGapRows` and `kPanelBottomAir` — because a gap written in
  pixels is one size of gap on a big panel and no gap at all on a small one, and `kTitleGap`
  had been six pixels since before panels resized themselves.

  And the `(e to edit)` note, which Kaida had not mentioned and was worse than cramped: "the
  (e to edit) tool tip render outside our frame and it's not beatiful at all". It was drawn
  after the theme name in a value column ten cells wide. The note is twelve cells long. It
  has *never* fitted, on any theme name, at any size — it left the panel and hung over the
  terminal every time the keys were on the field, and the photographs taken of that field
  yesterday had shown it and been read as a panel with a slightly long line. The fix is not to
  widen the panel for a note that only appears on one row: the way to open the field is said
  along the bottom with every other key the panel says. Which introduced the last problem of
  the day — the panel's height depends on the hint, so walking on to the name would have
  grown the panel and moved the row you were looking at. The height is reserved for the
  longest of the panel's candidate lines and the one it is saying is centred in that, so the
  words change and the panel does not.

  Fifteen checks. Each of the ten rules was then switched off one at a time and the check that
  owns it was seen to fail — including the one that first fooled me: `kPanelFontScale = 1.0`
  changed nothing, because the hint's size is *relative* to the rows, so the knob that turns
  off "small print" is `kPanelHintScale`. A probe that fails to fail is the other half of the
  test, and it found a wrong probe twice today.

- 2026-11-21: **two ideas from IDEA.md, and a probe that would not fail.** The first was
  Kaida's: "Instead of having a keybind for the Theme Editor Ctrl+shift+P, the Theme editor
  should be an item in the setting menu under Theme. It would make things more coherent."
  Asked plainly, the answer came back "ctrl+shift+p, does indeed go away" — so the key is
  gone rather than kept-and-duplicated, and the way in is a row.

  A `SettingRow` now has `opens`. That is the whole of the model change, and it is what lets
  one row do both jobs it already had: the arrows still step between the themes on disk and
  preview each, Enter opens the editor. `SettingKind` was not the place for it — a row that
  opens something is still a `Choice` row — and the hint line along the bottom changes on
  that row, because the keys standing on a row that behaves differently should not be
  described as if they were not.

  The real work was what "the setting keeps your change until you save or cancel them, even
  when in a sub-menu" costs. Closing the menu and reopening it is the easy reading of that
  sentence and it is wrong: the font size you had dialled in would be gone. So the menu
  *steps aside* — `settings_suspended_`, one bool, checked at every place the menu paints,
  takes a key, takes a click or takes a wheel, and the same object with the same row and the
  same unsaved values comes back. One panel on screen at a time, and nothing forgotten.

  Then the part nobody asked for and the probe found. The menu's undo baseline — what Esc
  puts back — was captured in locals at open. A save inside the editor is not a preview, so
  leaving the menu afterwards rolled back a decision somebody had already written to a file,
  and left the window wearing an older theme than the settings file named. The baseline is a
  member now (`MenuBaseline`) and the editor moves the parts it committed. Same reasoning
  made `setRowAnswers` un-remember the theme row as a change: the editor already wrote the
  name, so pressing `save` in the menu afterwards must not write the older one back over it.

  Probe R — deleting those three lines — **did not fail anything**, and the reason is worth
  keeping: the test saved a theme under the name it already had, so the menu's cancel
  re-read the same file and got the saved colours by accident. Only a *renamed* save can
  show the bug, because only then does the file the menu re-read differ from the file the
  editor wrote. Third time this month that a probe failing to fail has been the finding.

  The second idea was "change the theme config to not require an = between the name and the
  value, so that kitty themes can be used as is". The parser had already made the hard
  decision years ago — a value beginning with `#` is a colour, not a comment — so the rest
  was one `else`: no `=` on the line means the first word is the key. `=` still wins when a
  line has one, so both spellings live in one file. Kaida also asked for the selection
  colours by name, "yes add selection background to our config too" and "and selection
  foreground": the first is what `selection` always meant, so it keeps reading as the older
  name the way `opacity` does, and the second was a light grey compiled into the painting,
  on a theme whose selection colour could already be pale. `use color0-color15 in the
  config, but keep their name in the UI` needed no work at all — the palette had been
  written `color0` and read `black` since it was built — which is worth writing down too:
  two of the three asks were already true, and the only reason to check is that the third
  was not.

  Tests: 290 checks became 327 in the window test, 96 became 105 in the config test. Opening
  the editor is a walk now, so there is one helper that does the walk and it starts by
  leaving whatever panels are up, because a test that ends inside the editor leaves the menu
  waiting underneath. That is also the answer to "how do I get back to the terminal now":
  two Escapes. The photographer's habit of naming a picture for a size it was not taken at —
  four of them said 960x540 while sitting at 330x220 — is fixed too, since a photograph that
  lies about its size is how a panel looks fine and is not.

- 2026-11-21: **five ideas, and a list that stopped hiding.** IDEA.md arrived with two more
  after the pair shipped that morning: *"when there is enough space show all color in the
  theme editor instead of having to scroll. prioritize showing everything without scrolling,
  and when scrolling is needed can we show a little indicator to say that there is more to be
  seen if the user scrolls"*, and *"The preview pane in the Theme editor should read `0123
  #$%& Color Preview` instead of `The quick brown fox ...`"* — then three more while that was
  in flight: *"the scrolling indicator … pointing down should be closer to the color a bit
  more up. the one pointing up is fine but we would need the title \"Colours\" to be a bit
  more up and also changed for \"Theme Editor\""*, *"the Enter key should allow the Editing of
  the Theme name field, replacing and removing the e key … Also make the Field accept Capital
  letters as it does not currently"*, and *"The color should be grouped togheter by 'color'
  … Black, Bright Black, Red, Bright Red … Foreground background cursor … can stay at the top
  but in this order: Background, Foreground, Accent, Cursor, Selection Background, Selection
  Foreground."*

  The caption first, because it was a bug wearing a wording ticket: the sample sentence was a
  pangram, long enough that the centred draw clipped both ends, so the strip read as a
  sentence with pieces missing off it. A caption inside a swatch has one job, which is to be
  the colour, and `mixerSampleFits()` now records while painting whether all of it fitted —
  the check that would have caught the clipping the day it was written.

  Then the list. The font had been asked to fit a dozen rows and the rest scrolled, so a
  window with room for twenty-two rows of slightly smaller type was given twelve of slightly
  larger type and a scroll. Asking for all of them was easy; asking *whether they fit* was
  not. The first version measured the rows and nothing else, declared it fitted at 900x560,
  and then scrolled anyway — because the panel is not a stack of rows: it is the rows plus the
  title, the air, the two words and the line of keys, and the line of keys is the tallest thing
  at the bottom and gets taller the narrower the panel is. The honest question needed the
  bottom measured first, which meant a reference panel in the scrolling size, which is also
  the size the fallback wants anyway. Below `kPanelListFitMinPixels` it still scrolls, because
  eight-pixel type you cannot read is not better than a scroll you can.

  The mark is drawn as a polygon, not a glyph: a panel's font is whatever the terminal's font
  is, and a character it does not carry draws an empty box. It lives in air the panel already
  leaves — title to rows, rows to words — so appearing never costs a row, which is the same
  trap the hint fell into a day earlier: anything that comes and goes while you walk has to
  fit in space that was already reserved. It is not centred in that air either. Centred, it
  read as a thing in the middle of a gap that had nothing to do with either end of the list;
  it sits nearer the rows it belongs to now, which is what Kaida asked for and, once seen,
  obvious.

  The `e` was the second key in this program that meant *open the thing you are standing on*,
  and Enter already meant that everywhere else, including two rows above in the same panel.
  Removing it also removed a rule from the manual. Capitals were a plain bug: a capital is the
  same key with Shift on it, the field asked for `plain`, and so `MyTheme` came out as
  `y heme` — the same reason a capital `F` typed at a colour row vanished, which nobody had
  reported because nobody tries to type hex in capitals. A name is a file name, so it keeps
  whatever case you typed, extension and all.

  Reordering the rows looked like ten lines and cost twenty-one checks, which is the sound a
  test suite makes when it has been walking by number instead of by name. Most of the ripple
  was honest — a block that mixes the ink now has to say so and step down one — and one was
  not: a four-step walk up that had been reaching the name field *from the background row*,
  which only existed because an earlier line had stepped down to it. `selection background`
  took the place of `selection` on the screen too, because a row called `selection` next to
  one called `selection foreground` leaves you guessing which half is which; the file still
  takes both spellings.

  Tests: 327 checks became 345. Every rule here was switched off and seen to fail its own
  check, and two of them needed a second attempt. The lower scroll mark passed with the mark
  disabled, because the probe had only disabled the upper one — two checks is not one rule,
  and disabling one half proves nothing about the other. The capital hex check passed with
  capitals refused, because the theme it ran against already had an `f` in the high nibble of
  the channel it was watching: a check that cannot tell the difference is a check that agrees
  with whatever happens, so the row is now driven to black first and the `f` has nowhere to
  come from but the keystroke.

  And one lesson about editing. A three-line change was applied by line number, and the lines
  were not the three lines assumed: the lambda's `sendEvent` was overwritten by a second copy
  of the line above it, which compiles, runs, and delivers no keystrokes at all. The whole
  theme block then failed at its first check and the failure looked like a behaviour change.
  It is worth remembering that a block dying at check one is rarely the feature — and that
  text you can search for uniquely is worth more than a position you have to count to.

- 2026-11-21: **the tooltip that changed the shape of the thing it described, and a cursor
  the session kept taking back.** Two more from IDEA.md.

  *"In the theme editor The tooltips should stay the same size as every other field even on
  the name field. The name field should just change the \"Select\" word to \"Edit\" instead
  of adding a whole new line in the beginning of the tooltip."* The name row's line had
  grown a clause at the front — `Enter edits the theme name - ` bolted onto the usual
  sentence — because when Enter stopped meaning *save* and started meaning *open*, the panel
  had to say so somewhere, and the bottom line is where keys get said. The fix is not to
  explain it better; it is that `Select` was the wrong word on that row all along. Standing
  on the name, the line is now the panel's usual one with one word swapped, and standing
  *inside* it, the same three parts with the verbs changed: `Arrows to Move - Enter to
  Confirm - Escape to Put Back`. A field whose keys take more lines than any other field's
  is a panel that changes shape under the keys, which is the same fault the hint line was
  fixed for a day earlier, arriving again by a different route.

  *"Cursor style is lost upon a fresh instance of kai-term / when loading a session. It needs
  to always apply the cursor style supplied by the config file at all times. just like the
  theme, the Windows hold the cursor style."* Kaida named the answer in the complaint. The
  colours had already been through this: the file is read, the window wears it, and then the
  session talks — `OSC 10/11` from a prompt that wants its own background, and a snapshot
  that replays every byte it remembered. The cursor's turn was `DECSCUSR`: the file said
  `bar`, the window obliged, and a moment later the session's own `CSI 2 q`, or the one a
  restored snapshot replayed, moved it and nothing brought it back. So the shape is now a
  lock, set beside the colour locks in the same place a config is worn: named in the file it
  answers every `DECSCUSR` including `CSI 0 q`, which the standard spells "user default" and
  which now genuinely means *the person's default*; left unnamed there is no lock and a
  program picks its shape exactly as it always did. The settings menu's row is a choice
  somebody is making, so it locks too, and leaving the menu without saving hands the lock
  back to the file — otherwise a cancelled preview would have gone on refusing the session's
  shapes for the rest of the window's life, which is a bug that would have taken a fortnight
  to name.

  *"Make sure other settings are also kept after being applied to the config file"* was an
  audit, and the audit's answer is that the cursor was the only one still leaking: font,
  padding, scrollback and the rest are decisions about the window, and a session has no
  channel to argue with them. The colours were closed off when the theme authority landed;
  the shape was the last thing a session could write over.

  Tests: 345 checks became 350 in the window test, 118 became 120 in the widget one — and
  the first version of the window check did not fail when the feature was removed, because
  it read `cursorStyle()`, which is the window's *default* shape, and the default had always
  been set correctly. The bug was in the shape on screen. The check now runs a `printf`
  inside the session the way Kaida would, and looks at what the window is showing; its
  partner runs the same escape in a window whose file names no shape and insists the cursor
  moves. A check about a lock has to be seen to open it too, or a welded door passes for a
  locked one. The tooltip's line-count check needed the same treatment: at a wide window the
  old long sentence and the new short one both fit in two lines, so the check was repeated at
  a width where the old one needed three.

- 2026-11-21: **a lock written on the thing that gets replaced, and a cursor that blinks on
  a number.** Two things: a new setting, and the same bug back again one line further down.

  *"can we add cursor blink, with customizable blink speed ( 0 being no blinking)"* — the
  parenthetical is the interesting half. `0` is not a switch that happens to be off, it is
  the answer *stay lit*, and the window then starts no timer at all: a terminal that wakes
  twice a second to paint nothing is a laptop that runs down. So the file takes a number of
  milliseconds per half-blink, the settings menu walks it, and the DECSCUSR blinking shapes
  (`1 q`, `3 q`, `5 q`) still only choose a *shape* — how much movement somebody wants
  behind the text they are reading is their decision, not a program's. Two decisions came
  with it. The default is steady, because the blink has never been on here and a visible
  change arriving with an unrelated build is not mine to make; and the row got a `step` of
  fifty, because a range in the thousands walked one at a press is five thousand presses
  between a slow blink and no blink — the first row in that menu whose own scale needed a
  step, and the reason `SettingRow` now has one. The blink also taught the suite something:
  a window that repaints twice a second makes every pixel comparison in it a race, so the
  shape checks now read the cursor's cell at known phases rather than hoping.

  *"we are still loosing cursor style on a new session / when attaching a session. Fresh
  start still hold the right one from config."* The asymmetry in that sentence was the whole
  diagnosis, and it should have been recognisable: on 2026-11-20 the colours went missing on
  a session spawn for exactly this reason — `showSession` builds a new `Screen`, the theme
  went out with the old one, and the fix was to keep the locks in the window and dress each
  new screen with them. Yesterday's cursor fix was written against the symptom I could see,
  the snapshot replaying a `DECSCUSR`, and so the lock went onto the screen and nowhere else.
  A fresh start wore the file's shape; the first session after it handed the shape back to
  the shell, because the screen holding the lock had been thrown away and rebuilt. The shape
  is now kept in the window beside `colour_locks_` and re-applied in the same breath as the
  colours are.

  What the checks say is worth writing down, because three of them cover one rule and each
  one fails a different way of being wrong. *"the fresh session arrives wearing the shape the
  file named"* and *"moving onto a session that keeps asking for a bar keeps the shape"* both
  fail with the new-screen lock removed. But *"an attached window wears the cursor shape its
  own file names"* — the check that reads most like the complaint — passed with the fix
  taken out, because a window that starts attached locks its screen after construction and
  never goes through the replacement. It was not a wrong check; it was a check of the rule
  rather than of the fix, and on its own it would have let the bug through the door Kaida
  actually walked in through. Its partner, a window whose file names no shape attached to the
  same session, exists so that the refusal is known to be a refusal: the session asks for a
  bar every fifth of a second and gets one there, and does not get one next door.

  Two smaller things from the same afternoon. A probe that is killed partway through leaves
  the feature it was testing switched off in the tree — the blink's `start` call was missing
  from a file I was about to keep editing, found only because I grepped for it before
  believing the source rather than reading what I expected to be there. And the config's
  complaint count: adding a fourth refused line to a fixture broke *"all three said their
  piece"*, which is a check doing its job and a reminder that fixtures are shared.

## Next Steps

- **A persistent status line in the attached view**, if it is wanted: which session,
  what is running in it. The escape hatch is built (Ctrl+S leaves, shell kept, entry
  notice says so). A line that survives a full-screen app cannot be painted on a raw
  byte relay — an app that addresses its cursor absolutely will overwrite it — so a
  real one means the view keeps a Screen model and repaints it a row down, the way the
  Qt widget does. That is a renderer for the CLI, a milestone of its own, and the
  tmux_e2e test is proof of how much care terminal repainting wants; do not fake it.
- **The daemon's start path is hardened — done 2026-11-20.** A process started in order
  to be the daemon carries `KAI_TERM_DAEMON=1` and refuses to start a daemon while it
  carries it, so a launch is one generation deep whatever `/proc/self/exe` turns out to
  be — including a test tool built from these sources that reads no arguments and opens a
  window instead of serving. A `flock` beside the socket makes "is anyone home?" and "then
  start one" one gesture, so a loaded machine gets one daemon and five commands that
  waited for it. The daemon leaves its pid beside the socket and whoever is locked out
  names it. Checked in `tests/cli_surface.cpp`, each check confirmed to fail with its belt
  switched off. Keep running the suite under a fork cap (`ulimit -u`) — a hung test should
  cost a test, not a machine — and remember that a daemon left by a killed build keeps
  running old code: `kai-term shutdown` after a rebuild. A stray one is `pkill -x
  kai-term-daemon`; the older ones exec'd `/proc/self/exe` and answered to `exe`.
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
- `src/config/colormixer.hpp` — the colour mixer's Qt-free model: one colour as RGB and as HSL in tenths, bars as fractions, clamped and wrapped so no channel leaves its range (no Qt).
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

- A file a program writes and a file it reads have to agree on one shape, or a save quietly
  produces a file the reader disagrees with. Theme files are written `key value` — the shape
  a kitty theme arrives in — while `kai-term.conf` keeps `key = value`, because that file is
  mostly somebody's own notes. The writer recognised a setting only where the line contained
  `=`, so saving over a theme written without one matched no line, marked nothing as written,
  and appended all the colours again in the very shape it had stopped writing: a theme holding
  each of its colours twice, with whichever copy the reader reached last winning. Both shapes
  are read, because a file somebody already wrote is not wrong. One shape is written per
  file, and which one is the caller's word.
  (Kaida, 2026-11-21: "Overwriting a theme should also Overwrite it for real instead of
  appending to it. to avoid conflict like this", and on which shape is right, "only the
  =less layout should be used is what i meant, we a re deprecating the value = xx layout".)
- A test that seeds its fixture in the shape it is checking for proves nothing. Every
  window test wrote its theme file with `key = value` by hand, so the old writer read them
  all perfectly and the duplication it caused on a real file — one the writer itself had
  produced — was invisible for as long as the feature existed. The check that catches it is
  a round trip: let the writer make the file, then save over what it made.
