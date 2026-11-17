# Kai-Term

Kai-Term is a native Linux terminal workspace: a terminal emulator, a dual-pane file manager, and a native multiplexer with persistent sessions. It does not use tmux, GNU Screen, or another external multiplexer.

## Current status

M1 (the native terminal emulator) and M2 (input reliability: text selection,
clipboard, paste, and mouse reporting) are implemented, tested headlessly and
offscreen, and have been used at a live Wayland session. Since that milestone
closed, the parts an audit found thin have been filled in: scrollback reflows when
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

This builds:

- `build/kai-term` — the application.
- `build/kai-term-parser` — headless VT parser test.
- `build/kai-term-pty` — headless PTY test.
- `build/kai-term-tmux-e2e` — parser comparison against real tmux output.
- `build/kai-term-input` — key encoding, asserted on exact bytes.
- `build/kai-term-input-pty` — the same sequences replayed through a real PTY.
- `build/kai-term-scrollback` — scrollback model, cursor-mode and mouse-mode state.
- `build/kai-term-mouse` — mouse encoding, asserted on exact bytes.
- `build/kai-term-selection` — selection geometry and the text it produces.
- `build/kai-term-scrollbar` — the scroll-position indicator and the view bounds it reads.
- `build/kai-term-process` — shell lifecycle: exit status, signals, leftover jobs, an unusable shell.
- `build/kai-term-mouse-app` — a real application (btop) driven with the mouse.
- `build/kai-term-widget-smoke` — the real widget driven offscreen.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

or individually:

```sh
./build/kai-term-parser
./build/kai-term-pty
./build/kai-term-tmux-e2e
./build/kai-term-input
./build/kai-term-input-pty
./build/kai-term-scrollback
./build/kai-term-mouse
./build/kai-term-selection
./build/kai-term-scrollbar
./build/kai-term-process
./build/kai-term-mouse-app
./build/kai-term-widget-smoke
./tests/version.sh ./build/kai-term
```

Verified results on this machine:

- `ctest` — `100% tests passed, 0 tests failed out of 12` (42 s).
- `kai-term-parser` prints `PASS`.
- `kai-term-pty` prints `PASS: PTY output contains HELLO and resize 6 40`.
- `kai-term-tmux-e2e` prints `PASS: tmux E2E matches tmux ground truth at all stages`.
- `kai-term-input` prints `PASS: 66 key encodings match the expected PTY bytes`.
- `kai-term-input-pty` prints `PASS: 47 input sequences round-tripped through a real PTY unchanged`
  (35 key sequences and 12 mouse reports, classic and extended forms).
- `kai-term-scrollback` prints `PASS: 86 scrollback, cursor-mode and mouse-mode checks passed`.
- `kai-term-scrollbar` prints `PASS: 25 scroll indicator geometry checks passed`.
- `kai-term-process` prints `PASS: 31 process lifecycle checks passed`.
- `kai-term-mouse` prints `PASS: 40 mouse encodings match the expected PTY bytes`.
- `kai-term-selection` prints `PASS: 101 selection and text-extraction checks passed`.
- `kai-term-mouse-app` prints `PASS: 8 mouse checks passed against a running btop (no visual
  inspection performed)`. When btop is not installed it prints that the checks were not
  exercised instead of pretending to pass.
- `kai-term-widget-smoke` prints `PASS: 96 offscreen widget checks passed (no visual inspection
  performed) [this platform has no separate X11-style selection: only the clipboard copy was
  exercised]`. The bracketed note is the test being honest about the platform it ran on.
- `tests/version.sh` prints `Kai-Term 0.0.0`.

`kai-term-input-pty` needs a working `/bin/sh`, and `kai-term-widget-smoke` needs
Qt's offscreen platform plugin; both set what they need themselves and open no
window.

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
- `src/platform/platform.hpp` / `platform.cpp` — Qt platform-plugin selection.
- `src/terminal/screen.hpp` — native screen model.
- `src/terminal/keyencode.hpp` — Qt key event to PTY bytes, as a pure function.
- `src/terminal/mouseencode.hpp` — mouse event to PTY bytes, as a pure function.
- `src/terminal/selection.hpp` — selection geometry and word expansion.
- `src/terminal/text.hpp` — screen, row and selection text extraction.
- `src/terminal/vtparser.cpp` — native VT parser.
- `src/terminal/base64.hpp` — the decoding OSC 52 needs, with no toolkit in it.
- `src/terminal/terminalwidget.hpp` — Qt terminal widget, including the scrollback view.
- `src/process/pty.hpp` — PTY and shell lifecycle.
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
- `tests/version.sh` — version test.
- `CMakeLists.txt` — build configuration.

## License

Kai-Term is licensed under the GNU General Public License v3.0 only. See [LICENSE](LICENSE).
