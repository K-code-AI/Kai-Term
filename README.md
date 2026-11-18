# Kai-Term

Kai-Term is a native Linux terminal workspace: a terminal emulator, a dual-pane file manager, and a native multiplexer with persistent sessions. It does not use tmux, GNU Screen, or another external multiplexer.

## Current status

M1 (the native terminal emulator), M2 (input reliability: text selection,
clipboard, paste, and mouse reporting) and M3 (native session infrastructure) are
implemented and tested, and M4 (the native multiplexer) has its first pieces: every
window's shell is held by the session daemon, **Ctrl+Shift+S** keeps one and starts a
fresh shell in the same window, **Ctrl+Shift+A** brings a kept one back, and closing a
window with something running asks rather than ends. M1 and M2 have been used at a live
Wayland session. Of M3 and M4, the keep-this-shell keybind has been used at a live
Wayland session — on niri, by Kaida — and it works. Everything else in them is still
checked
headless or offscreen only, so what the session menu looks like, what the close prompt
reads like when you have a build running, and what it feels like to attach to a session
from another terminal are still somebody else's to say. Since the last milestone closed, the parts an audit found thin have been
filled in: scrollback reflows when
the window changes width, the view says where it is in the history, a selection
dragged to an edge scrolls, the shell's process tree is taken down with it, and the
escape-sequence coverage applications actually send (window titles, OSC 52 clipboard,
working directory, colour queries, window-size queries) is answered instead of
dropped. The milestone list is in [ROADMAP.md](ROADMAP.md).

The terminal emulator does **not** depend on `libvterm`; Kai-Term uses its own VT parser and screen model. Rendering uses a monospace font with cell spacing derived from the font advance.

Implemented:

- Qt 6 Widgets terminal window.
- Native VT/escape-sequence parser.
- PTY creation with `forkpty()`.
- Configurable shell selection through `KAI_SHELL` or `SHELL`.
- Shell input and output.
- Basic terminal resize through `TIOCSWINSZ`.
- Colors, attributes, cursor movement, erasing, and Unicode text.
- Scrollback buffer retained in memory: the mouse wheel scrolls history three rows
  per notch, and Shift+PageUp, Shift+PageDown, Shift+Home and Shift+End scroll by a
  screen, to the oldest line, and back to the live bottom. Typing any key returns
  the view to the bottom. While the view is anywhere but at the bottom a narrow
  indicator along the right edge shows how much history there is and where this
  window sits in it; at the bottom it is gone, because "following the shell" is
  worth more screen than it is worth a bar.
- A program that takes the whole screen (vim, less, btop, tmux) gets the whole screen.
  While one is drawn there is no scrollback to offer and no scroll bar: the history it
  would show belongs to the shell underneath, and the program has its own idea of what is
  above its top row. The wheel, Shift+PageUp, dragging the thumb and dragging a selection
  to an edge all do nothing, and giving the screen back brings the shell's history with it
  — it was kept all along, it was only not what was showing.
- Scrollback **reflows**. Retained lines are stored as logical lines, so changing
  the window width re-wraps them instead of leaving a ragged column of text that
  stops where the old width did. The cursor keeps its place in the window, and the
  line it was on survives the rewrap.
- A selection dragged to the top or bottom edge of the window scrolls on its own,
  so a drag can reach further than the window is tall. Whatever scrolls out of the
  window during a downward drag is gone from the selection — the same trade every
  terminal makes here.
- Key encoding lives in `src/terminal/keyencode.hpp` as a pure function, so the
  exact bytes sent to the shell are asserted in tests rather than inferred.
- Enter sends carriage return (`0x0D`) and Backspace sends `0x7F`, matching xterm
  and the line discipline; both are checked against a real PTY in
  `tests/input_pty.cpp`.
- DECCKM application cursor keys (`ESC[?1h`), so vim, less and fish's Vi mode get
  `ESC O A`-style sequences instead of `ESC [ A`.
- Arrow keys are sent to fish as ANSI cursor bindings (`ESC[A]`, `ESC[B]`, `ESC[C]`, `ESC[D]`); fish moves its own command-line cursor and updates autosuggestions. A visible cursor marker follows the screen cursor.
- Rendering has a small padding buffer around the glyph grid.
- Alternate screen buffers (`ESC[?1049h` / `ESC[?1049l`) are supported; `ESC[3J` clears the screen and scrollback. Lines that scroll off the alternate screen are discarded instead of entering the shell's history, and resizing while an alternate-screen application is running preserves the primary screen and its scrollback.
- Scroll regions (`ESC[...r]`) and scroll-content-up/down (`ESC[...S]` / `ESC[...T]`) are supported for tmux-style layouts, including a region given on one side only (`ESC[3r`) and a bottom bound past the last row. Lines SU pushes off the top of a whole screen go into the scrollback, as a newline's would.
- OSC sequences that applications send and expect an answer from: window and icon title (`OSC 0/1/2`, with `OSC 23;1` and the `ESC[22 t` / `ESC[23 t` title stack), the working directory (`OSC 7`, percent-decoded), the clipboard (`OSC 52`, both set and query — a program inside the terminal can copy text out, and can read the clipboard when it asks), the default foreground and background (`OSC 10/11`) and palette entries (`OSC 4`, reset by `OSC 104`).
- XTWINOPS (`ESC[...t`) answers the questions programs block waiting on: text area in pixels and in characters, cell size in pixels, and the window title. The kitty keyboard query (`ESC[?u`) is answered with "no", which is what lets a program finish starting up.
- `?2026` synchronized output: an application drawing a frame as one block is shown
  at most one repaint per 100 ms inside that block, and the finished frame as soon
  as the block ends. It is pacing rather than an off-screen frame — a repaint shows
  what had arrived by then — but it is what removes half-drawn screens.
- SGR attributes are drawn, not just stored: bold, dim, italic, underline, reverse,
  hidden, strike and overline (`53`, off with `55`). Underline, strike and overline
  each draw their own line; they used to share one position variable, so a struck
  word that also carried an underline silently lost the underline.
- DECSCUSR (`ESC[<n> q`) sets the cursor shape: block, underline or vertical bar. Kai-Term's own default stays the underline it has always drawn, so nothing changes for an application that never asks; the blinking variants select their steady shape, since nothing here animates yet. A full reset (`ESC c`) hands the shape back along with the colour defaults and any palette entries an application changed.
- Colour specifications in both spellings: `38;5;196` and `38:5:196`, `38;2;r;g;b` and `38:2::r:g:b`. An index outside the palette leaves the colour alone rather than inventing one.
- Primary Device Attribute queries (`ESC[c]` and `ESC[0c]`) are answered.
- Answers fish’s Primary Device Attribute query (`ESC[0c`) so fish startup does not print the 10-second warning.
- Ignores DCS payloads and handles `ESC =` at chunk boundaries without printing stray `[K` text.
- Shell exit closes the Qt terminal window/app.
- Mouse text selection: drag to select, double-click for a word, triple-click for
  a line, Shift+click to move the end of a selection without dragging across the
  whole window. Selecting while scrolled up selects the retained lines, not the
  live rows underneath. Holding Shift while clicking takes the mouse back from an
  application that grabbed it, the xterm convention, which is how you copy out of
  vim.
- **Ctrl+Shift+C** copies the selection; nothing else does. A finished drag leaves
  the clipboard alone, so selecting a path does not throw away what you were about
  to paste. The highlight stays on the text it was made on: it moves with the text
  when output arrives or when you scroll, and it is dropped once its lines leave
  the window rather than left pointing at different characters.
- Middle click pastes. Newlines in pasted text become carriage returns, so pasting
  a script cannot run commands that were never pressed. When the application asks
  for it (`ESC[?2004h`), the paste is wrapped in the bracketed-paste markers
  `ESC[200~` and `ESC[201~`.
- Mouse reporting for applications: click (`ESC[?1000h`), drag (`ESC[?1002h`) and
  any-event (`ESC[?1003h`) tracking, wheel as buttons 64 and 65, Shift/Meta/Ctrl
  bits, and the classic three-byte, UTF-8 (`ESC[?1005h`), urxvt (`ESC[?1015h`), SGR
  (`ESC[?1006h`) and hexadecimal SGR (`ESC[?1016h`) coordinate forms. The encoding is
  a pure function in `src/terminal/mouseencode.hpp`, asserted on exact bytes. UTF-8
  coordinates are the same `ESC[M` framing with each coordinate written as UTF-8 of
  its value plus 32, so a click in a window 300 columns wide arrives intact instead of
  clamping at 223; SGR outranks it and hexadecimal SGR outranks both, because that is
  the order applications expect when they ask for more than one.
  `kai-term-mouse-app` also runs a real application (btop) inside the widget and
  confirms the reports are understood: btop asks for `?1002;1006`, and its own
  scroll counter moves when the wheel is turned. That test exists because the SGR
  form was being written as `CSI < col ; row ; button M` instead of xterm's
  `CSI < button ; col ; row M` — unreadable to anything parsing it, and invisible
  to byte-level tests written from the same wrong idea.
- Text extraction is a pure function too (`src/terminal/text.hpp`): the text of a
  row, of the whole screen, or of a selection, with the padding that terminal rows
  carry trimmed off and double-width glyphs counted once.
- Clipboard support is Qt's. Ctrl+Shift+C is the only thing that copies: it sets
  the clipboard and, where the platform has one, PRIMARY. A finished drag copies
  nothing anywhere, because selecting a path should not throw away what you were
  about to paste. The one exception is the terminal doing what a program inside it
  asked: OSC 52 with an empty payload reads the clipboard, and a payload copies to
  the clipboard or to PRIMARY depending on the target it names. Middle click reads
  PRIMARY first and falls back to the clipboard, so a selection in one terminal
  still pastes into another. On platforms without a separate selection only the
  clipboard exists.
- The shell is taken down with the window. `forkpty()` makes it a session leader,
  so the teardown signals the session rather than a process group: a background
  `sleep 300 &` left behind by an exited shell goes with it, and one that traps
  HUP and TERM is killed outright after a short grace period. The exit status and
  signal are reported, and a shell named by `KAI_SHELL` that cannot be run says so
  in the window instead of silently falling back to `/bin/sh`.
- Session daemon, multiplexer, and file manager are not implemented yet.

Two limits worth knowing before they surprise you. Cells store a resolved colour,
not a palette index, so an OSC 4 or OSC 10 change recolours text as it is written
from then on and leaves what is already on screen alone. And OSC 8 hyperlinks are
consumed without being stored: the text renders, but there is nothing to follow.

The widget has been exercised without a display server (`QT_QPA_PLATFORM=offscreen`):
the real widget is constructed, painted into an image, given synthetic key, wheel
and mouse events, and its output is read back from the shell. Mouse reports and
pastes are read back as bytes: the test puts the terminal's own tty into raw mode,
hands it to `cat` and compares the file against the bytes the encoder should have
produced, so the assertion is about what a program inside the terminal receives.
That proves the code paths run, that scrolling and selecting change what is
painted, and that the right bytes reach the shell.

Two things the tests cannot prove, and no test here claims them:

- It is **not** a visual inspection of colours, fonts or spacing.
- Qt's offscreen platform has no real desktop clipboard: it has a clipboard and no
  X11-style PRIMARY selection, so the tests exercise our side of the call only.

What a person has confirmed at a live Wayland session (niri, 2026-11-17): the
selection reads as a selection, selected text can be pasted into other
applications, and the general appearance is acceptable. That session also found
the btop bug described above — btop ignored every click, wheel turn and selection —
which the headless suite had missed and which `kai-term-mouse-app` now covers.
Interactive applications other than btop (vim, less, htop, tmux) have still not
been driven with the mouse by anyone.

## Build requirements

Verified on this machine:

- C++20 compiler and CMake 4.3.4.
- Qt 6 Widgets, version 6.11.2.
- Qt platform plugin binaries for both X11 and Wayland are installed.
- POSIX `forkpty()` / PTY support.

No `libvterm` dependency is used. No external terminal emulator is used.

## Build

```sh
cd Kai-Term
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This builds one program, `build/kai-term`, and twenty test binaries under
`build/tests/`. One program matters: the daemon is `kai-term session serve`, not a second
executable to install, version and get out of step with the terminal it serves. The tests
are kept out of the way because someone who has just built the terminal should not have to
work out which of twenty-one files is the terminal.

The tests are listed under [Tests](#tests); `cmake --build build` builds all of them, and
`cmake --build build --target kai-term` builds only the application.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

or individually:

```sh
./build/tests/kai-term-parser
./build/tests/kai-term-pty
./build/tests/kai-term-tmux-e2e
./build/tests/kai-term-input
./build/tests/kai-term-input-pty
./build/tests/kai-term-scrollback
./build/tests/kai-term-mouse
./build/tests/kai-term-selection
./build/tests/kai-term-scrollbar
./build/tests/kai-term-process
./build/tests/kai-term-mouse-app
./build/tests/kai-term-widget-smoke
./build/tests/kai-term-ipc
./build/tests/kai-term-sockets
./build/tests/kai-term-snapshot
./build/tests/kai-term-session
./build/tests/kai-term-widget-session
./build/tests/kai-term-widget-panes
./build/tests/kai-term-app-window
./build/tests/kai-term-close-prompt
./tests/version.sh ./build/kai-term
```

Verified results on this machine:

- `ctest` — `100% tests passed, 0 tests failed out of 20` (51 s).
- `kai-term-parser` prints `PASS`.
- `kai-term-pty` prints `PASS: PTY output contains HELLO and resize 6 40`.
- `kai-term-tmux-e2e` prints `PASS: tmux E2E matches tmux ground truth at all stages`.
- `kai-term-input` prints `PASS: 66 key encodings match the expected PTY bytes`.
- `kai-term-input-pty` prints `PASS: 47 input sequences round-tripped through a real PTY unchanged`
  (35 key sequences and 12 mouse reports, classic and extended forms).
- `kai-term-scrollback` prints `PASS: 86 scrollback, cursor-mode and mouse-mode checks passed`.
- `kai-term-scrollbar` prints `PASS: 25 scroll indicator geometry checks passed`.
- `kai-term-process` prints `PASS: 37 process lifecycle checks passed`, including that
  a command name which cannot be drawn is never drawn.
- `kai-term-mouse` prints `PASS: 40 mouse encodings match the expected PTY bytes`.
- `kai-term-selection` prints `PASS: 119 selection and text-extraction checks passed`.
- `kai-term-mouse-app` prints `PASS: 8 mouse checks passed against a running btop (no visual
  inspection performed)`. When btop is not installed it prints that the checks were not
  exercised instead of pretending to pass.
- `kai-term-widget-smoke` prints `PASS: 112 offscreen widget checks passed (no visual inspection
  performed) [this platform has no separate X11-style selection: only the clipboard copy was
  exercised]`. The bracketed note is the test being honest about the platform it ran on.
- `kai-term-ipc` prints `PASS: 43 protocol framing and field checks passed`.
- `kai-term-sockets` prints `PASS: 44 unix socket checks passed`.
- `kai-term-snapshot` prints `PASS: 64 screen snapshot checks passed`. A snapshot fed
  into a fresh screen model reproduces it cell for cell, which is the property that makes
  "attach and be shown the screen" true.
- `kai-term-session` prints `PASS: 109 session daemon checks passed`, over a real pty and
  a real socket with no display: a session outlives the view that started it, a second
  view is shown the screen, two views see the same output, a view hanging up disturbs
  nothing, terminal queries are answered, an unsaved session survives a window that
  vanished without saying anything and dies when one says it is closing, a kept session
  is kept under a name and a taken name is refused, what is running out front is named,
  and stopping leaves no socket and no shell.
- `kai-term-widget-session` prints `PASS: 41 window-showing-a-session checks passed`,
  including a window switched from one shell to another: the screen it shows is the one
  it is looking at, typing after the switch reaches the new shell and not the old one, a
  kept session outlives the window moving on and an unsaved one does not.
- `kai-term-app-window` prints `PASS: 43 application window checks passed`: a window
  gets a shell held by the daemon without asking for one, Ctrl+Shift+S keeps it and
  starts a fresh one in the same window, a kept session comes back into the window that
  asks for it, and the program the close prompt would name is the one that is running —
  and, offscreen, that closing such a window really does put the question up, that the
  question is dismissed as a "no" leaves the window standing, and that `KAI_CONFIRM_QUIT`
  reaches the window that reads it.
- `kai-term-attach-state` prints `PASS: 15 attach state checks passed` — what a second
  view of a session arrives knowing. The glyphs were never the problem on their own: a
  snapshot without the modes is a picture of a terminal, and the window behaves as though
  nothing inside it ever asked for the mouse, the alternate screen, the arrow-key form or
  a quoted paste.
- `kai-term-close-prompt` prints `PASS: 24 close prompt checks passed` — when closing
  asks at all under each policy, and whether the words say what is actually being lost.
- `wayland-close` runs the real binary under sway with no display at all and asks the
  compositor to close the window, which is the `xdg_toplevel.close` a desktop keybinding
  sends. It checks that the window stops and asks rather than exiting, and that a window
  with nothing in it still closes without a word. It is registered only where sway is
  installed, and it brings its own runtime directory: its own daemon, its own compositor
  config, and nothing left in either.
- `kai-term-widget-panes` prints `PASS: 24 pane-scoped selection checks passed`, with
  real mouse events across a pane boundary.
- `tests/version.sh` prints `Kai-Term 0.0.0`.

`kai-term-input-pty` needs a working `/bin/sh`, and the widget tests need Qt's
offscreen platform plugin; all of them set what they need themselves, open no window, and
clean up the socket directory they made.

Launching `./build/kai-term` automatically looks for a Wayland display under `$XDG_RUNTIME_DIR` when neither `WAYLAND_DISPLAY` nor a usable `DISPLAY` is set, and selects Qt's Wayland platform plugin. On X11, Qt's X11 backend is used.

To skip the Qt window in automated runs:

```sh
KAI_TERM_TEST_NO_WINDOW=1 ./build/kai-term
```

## Usage

```sh
./build/kai-term
./build/kai-term --version
./build/kai-term --help
```

The shell is chosen from `KAI_SHELL`, then `SHELL`, then `/bin/sh`.

Sessions — shells that stay alive when the window closes:

```sh
./build/kai-term session new -s work --cwd ~/src   # starts the daemon if it is not running
./build/kai-term session list
./build/kai-term open work                         # a window onto it
./build/kai-term session attach -s work            # or this terminal
./build/kai-term session send -s work --text 'make' --enter
./build/kai-term session detach -s work            # send the views away, keep the shell
./build/kai-term session kill -s work              # end the shell
./build/kai-term session shutdown                  # stop the daemon and everything
```

The socket is `$XDG_RUNTIME_DIR/kai-term/daemon-v2.sock` — the number is the protocol it
speaks, so a daemon left running by an older build is not the one a new build talks to.
That matters because the ways an old daemon gets it wrong are invisible: it has no `save`,
so the keybind that keeps a shell would say `unknown command`, and it says nothing about
whether a session was kept, so a listing would hide shells and a menu come up empty. A new
build starts its own daemon and says, once, that an older one is still running with shells
it cannot reach and which process to stop; it never stops that daemon by itself, because a
daemon holding somebody's shells is not ours to tidy away. `kai-term session serve` runs
the daemon in the foreground instead of the background — that is the way to see why a
daemon would not start. `kai-term` is the only program built; the daemon is the same
binary started a different way. What is on the
screen when attaching comes from the daemon's own model, so a session started this
morning shows this morning; the scrollback above the visible screen is not transferred
yet.

### From inside a window

An ordinary window needs nothing starting first: it asks the daemon for its shell, and
the daemon is started if it is not there. If it cannot be started the window says so
once in the corner and runs a shell of its own, which works the same except that
nothing can be kept.

- **Ctrl+Shift+S** keeps the shell this window is showing and starts a fresh one in the
  same window. The window stays, because what ended was the shell, not the window. The
  kept session gets a name (`s1`, `s2`, …) and the corner says which.
- **Ctrl+Shift+A** opens a menu of the kept sessions with what is happening in each —
  `s1   sleep`, `s2   at a prompt` — and clicking one brings it back into this window.
  The session already being shown is not in the list.

- Plain **Ctrl+S** is left alone and goes to the shell, where it stops and starts
  output the way it always has. That is why the key above has Shift in it: a terminal
  cannot tell Ctrl+S from Ctrl+Shift+S — a control letter arrives as one byte and Shift
  is not in it — so claiming the letter would take it away from every program in the
  window. Qt can tell them apart, which is why the window can have the letter at all.

Keys held free for what is coming, so that a later feature does not quietly take one
that has become somebody else's: **Ctrl+Shift+F** for a file manager, **Ctrl+Shift+N**
for a new tab, **Ctrl+Shift+H** to switch between side-by-side panes and a tab view.
- Closing a window with something running asks **"Quit while `make` is running?"** with
  **Yes**, **No** and **Detach session before closing terminal**. A shell sitting at a
  prompt is not asked about, because there is nothing out there to lose.

- A session that was kept is asked about too, when something is running in it. Keeping a
  session says what happens to the *shell*; it is not permission for the window to drop
  whatever is running inside it on the way out. The second line says which of the two it
  is — for a kept one, that the work goes on running either way and closing takes only the
  window — and Detach is left off, because on a kept session it would be a second way to
  press the first button.

- **`KAI_CONFIRM_QUIT`** says how often to ask: `running` (the default) asks whenever
  something is running in the window, `always` asks at every close, and `never` lets the
  window go down without a word. The three words are the
  ones Old-Term's `confirm_quit` takes, so somebody who knows one knows the other; they
  are read from the environment until there is a config file to keep them in.

- If a window closes without asking when you expected it to, run it as
  `KAI_DEBUG_CLOSE=1 ./build/kai-term` and close it with something running. It writes to
  stderr and to `${XDG_RUNTIME_DIR:-/tmp}/kai-term-close.log` what the window thought was
  running, whether the session was kept, which policy was in force, and whether that
  merited asking — then, on a second line, what the daemon had to say about every shell
  it holds. The file is there because the window being asked about is usually the one
  that was started from the terminal the line would have gone to. `kept=yes` means it was
  right to say nothing; `running=''` while something plainly is means the window and the
  daemon are not describing the same shell, and the second line says which; no line at
  all means the desktop's close never reached the window.

A window's own shell is a session like any other, but unsaved: it is not listed as one
of yours, it does not stop the window closing, and closing the window on purpose ends
it. Losing that window *without* meaning to — a crash, a `kill -9` — leaves the shell
behind, listed by `session list` as **left behind**, so the work is still there.

Nothing here has been looked at by a person: the keybinds, the menu and the close
prompt are tested offscreen, which checks what they decide and what they would show, not
what they look like.

## Configuration

Configuration handling is not implemented yet. TOML configuration is planned.

## Layout

- `README.md` — current status and usage.
- `ARCHITECTURE.md` — subsystem design and IPC plan.
- `DEPENDENCIES.md` — verified dependencies and licenses.
- `ROADMAP.md` — milestone status.
- `LICENSE` — GPL-3.0 license text.
- `RESUME.md` — handoff state.
- `src/app/main.cpp` — application entry point.
- `src/app/appwindow.hpp` — the window a person runs: the two keybinds, the menu, the close prompt.
- `src/app/closeprompt.hpp` — when closing has to ask, and what it says (Qt-free).
- `src/platform/platform.hpp` / `platform.cpp` — Qt platform-plugin selection.
- `src/terminal/screen.hpp` — native screen model.
- `src/terminal/keyencode.hpp` — Qt key event to PTY bytes, as a pure function.
- `src/terminal/mouseencode.hpp` — mouse event to PTY bytes, as a pure function.
- `src/terminal/selection.hpp` — selection geometry and word expansion.
- `src/terminal/text.hpp` — screen, row and selection text extraction.
- `src/terminal/vtparser.cpp` — native VT parser.
- `src/terminal/base64.hpp` — the decoding OSC 52 needs, with no toolkit in it.
- `src/terminal/terminalwidget.hpp` — Qt terminal widget, including the scrollback view.
- `src/terminal/backend.hpp` — what the widget talks to: a pty, or a session elsewhere.
- `src/process/pty.hpp` — PTY and shell lifecycle.
- `src/process/job.hpp` — what is running in the foreground of a terminal.
- `src/ipc/protocol.hpp` — frames, escaped fields, record lists.
- `src/ipc/socket.hpp` — socket paths, binding, stale sockets, peer credentials.
- `src/session/snapshot.hpp` — a screen as escape sequences, for a view joining a session.
- `src/session/daemon.hpp` — the session daemon: pty ownership, views, commands.
- `src/session/sessionbackend.hpp` — a session across the socket, as a terminal backend.
- `src/session/cli.hpp` — the `session` verbs.
- `tests/parser.cpp` — parser test.
- `tests/pty.cpp` — PTY test.
- `tests/tmux_e2e.cpp` — tmux ground-truth comparison.
- `tests/input.cpp` — key encoding byte assertions.
- `tests/input_pty.cpp` — key sequences replayed through a real PTY.
- `tests/scrollback.cpp` — scrollback model and mode-state test.
- `tests/mouse.cpp` — mouse encoding byte assertions.
- `tests/selection.cpp` — selection geometry and text extraction.
- `tests/scrollbar.cpp` — scroll indicator geometry.
- `tests/process.cpp` — shell lifecycle against real processes.
- `tests/mouse_app.cpp` — btop driven with the mouse inside the widget.
- `tests/widget_smoke.cpp` — offscreen widget test.
- `tests/ipc.cpp` — protocol framing and field escaping.
- `tests/sockets.cpp` — Unix sockets, paths, permissions, peer credentials.
- `tests/snapshot.cpp` — screen snapshots, round-tripped through the model.
- `tests/session.cpp` — the session daemon over a real pty and socket.
- `tests/widget_session.cpp` — a window rendering a session owned by the daemon.
- `tests/app_window.cpp` — keeping a session, restoring one, and what closing would ask about.
- `tests/close_prompt.cpp` — when a close asks, and whether the words name the right thing.
- `tests/widget_panes.cpp` — selection clipped to the pane a drag started in.
- `tests/version.sh` — version test.
- `CMakeLists.txt` — build configuration.

## License

Kai-Term is licensed under the GNU General Public License v3.0 only. See [LICENSE](LICENSE).
