# Kai-Term

Kai-Term is a native Linux terminal workspace: a terminal emulator, a dual-pane file manager, and a native multiplexer with persistent sessions. It does not use tmux, GNU Screen, or another external multiplexer.

## Current status

M1 (the native terminal emulator) is implemented. M2 (input reliability: text
selection, clipboard, paste, and mouse reporting) is implemented and tested
headlessly; the milestone list is in [ROADMAP.md](ROADMAP.md).

The terminal emulator does **not** depend on `libvterm`; Kai-Term uses its own VT parser and screen model. Rendering uses a monospace font with cell spacing derived from the font advance.

Implemented:

- Qt 6 Widgets terminal window.
- Native VT/escape-sequence parser.
- PTY creation with `forkpty()`.
- Configurable shell selection through `KAI_SHELL` or `SHELL`.
- Shell input and output.
- Basic terminal resize through `TIOCSWINSZ`.
- Colors, attributes, cursor movement, erasing, and Unicode text.
- Scrollback buffer retained in memory, now visible: the mouse wheel scrolls
  history three rows per notch, and Shift+PageUp, Shift+PageDown, Shift+Home and
  Shift+End scroll by a screen, to the oldest line, and back to the live bottom.
  Typing any key returns the view to the bottom. There is no scrollbar widget
  yet: the view moves with the wheel and the Shift keys, and the position is not
  shown anywhere.
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
- Scroll regions (`ESC[...r]`) and scroll-content-up/down (`ESC[...S]` / `ESC[...T]`) are supported for tmux-style layouts.
- Primary Device Attribute queries (`ESC[c]` and `ESC[0c]`) are answered.
- Answers fish’s Primary Device Attribute query (`ESC[0c`) so fish startup does not print the 10-second warning.
- Ignores DCS payloads and handles `ESC =` at chunk boundaries without printing stray `[K` text.
- Shell exit closes the Qt terminal window/app.
- Mouse text selection: drag to select, double-click for a word, triple-click for
  a line. Finishing a drag copies the text; no key chord is needed. Selecting
  while scrolled up copies the retained lines, not the live rows underneath.
  Holding Shift while clicking takes the mouse back from an application that
  grabbed it, the xterm convention, which is how you copy out of vim.
- Middle click pastes. Newlines in pasted text become carriage returns, so pasting
  a script cannot run commands that were never pressed. When the application asks
  for it (`ESC[?2004h`), the paste is wrapped in the bracketed-paste markers
  `ESC[200~` and `ESC[201~`.
- Mouse reporting for applications: click (`ESC[?1000h`), drag (`ESC[?1002h`) and
  any-event (`ESC[?1003h`) tracking, wheel as buttons 64 and 65, Shift/Meta/Ctrl
  bits, and the classic three-byte, urxvt (`ESC[?1015h`), SGR (`ESC[?1006h`) and
  hexadecimal SGR (`ESC[?1016h`) coordinate forms. The encoding is a pure function
  in `src/terminal/mouseencode.hpp`, asserted on exact bytes.
- Text extraction is a pure function too (`src/terminal/text.hpp`): the text of a
  row, of the whole screen, or of a selection, with the padding that terminal rows
  carry trimmed off and double-width glyphs counted once.
- Clipboard support is Qt's: on X11 the selection goes to both the clipboard and
  PRIMARY, so a middle click in another terminal picks it up; on platforms without
  a separate selection only the clipboard is set. Which one your desktop offers is
  not something the headless tests can tell you — see the note above.
- Session daemon, multiplexer, and file manager are not implemented yet.

The widget has been exercised without a display server (`QT_QPA_PLATFORM=offscreen`):
the real widget is constructed, painted into an image, given synthetic key, wheel
and mouse events, and its output is read back from the shell. Mouse reports and
pastes are read back as bytes: the test puts the terminal's own tty into raw mode,
hands it to `cat` and compares the file against the bytes the encoder should have
produced, so the assertion is about what a program inside the terminal receives.
That proves the code paths run, that scrolling and selecting change what is
painted, and that the right bytes reach the shell.

Two things it cannot prove, and no test here claims them:

- It is **not** a visual inspection — nobody has confirmed what the rendered
  terminal looks like on a live Wayland or X11 session, including whether the
  selection colours read as a selection.
- Qt's offscreen platform has no real desktop clipboard: it has a clipboard and no
  X11-style PRIMARY selection, so the tests exercise our side of the call only.
  Whether a middle click in another terminal picks up your selection, and whether
  vim or htop behave as you expect with mouse reporting on, still needs a person
  at a real session.

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
./build/kai-term-widget-smoke
./tests/version.sh ./build/kai-term
```

Verified results on this machine:

- `ctest` — `100% tests passed, 0 tests failed out of 9`.
- `kai-term-parser` prints `PASS`.
- `kai-term-pty` prints `PASS: PTY output contains HELLO and resize 6 40`.
- `kai-term-tmux-e2e` prints `PASS: tmux E2E matches tmux ground truth at all stages`.
- `kai-term-input` prints `PASS: 66 key encodings match the expected PTY bytes`.
- `kai-term-input-pty` prints `PASS: 47 input sequences round-tripped through a real PTY unchanged`
  (35 key sequences and 12 mouse reports, classic and extended forms).
- `kai-term-scrollback` prints `PASS: 56 scrollback, cursor-mode and mouse-mode checks passed`.
- `kai-term-mouse` prints `PASS: 36 mouse encodings match the expected PTY bytes`.
- `kai-term-selection` prints `PASS: 101 selection and text-extraction checks passed`.
- `kai-term-widget-smoke` prints `PASS: 40 offscreen widget checks passed (no visual inspection
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
- `tests/widget_smoke.cpp` — offscreen widget test.
- `tests/version.sh` — version test.
- `CMakeLists.txt` — build configuration.

## License

Kai-Term is licensed under the GNU General Public License v3.0 only. See [LICENSE](LICENSE).
