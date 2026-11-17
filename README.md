# Kai-Term

Kai-Term is a native Linux terminal workspace: a terminal emulator, a dual-pane file manager, and a native multiplexer with persistent sessions. It does not use tmux, GNU Screen, or another external multiplexer.

## Current status (M1)

The native terminal emulator is implemented. It does **not** depend on `libvterm`; Kai-Term uses its own VT parser and screen model. Rendering uses a monospace font with cell spacing derived from the font advance.

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
  and no mouse text selection yet.
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
- Clipboard and mouse reporting are not implemented yet.
- Session daemon, multiplexer, and file manager are not implemented yet.

The widget has been exercised without a display server (`QT_QPA_PLATFORM=offscreen`):
the real widget is constructed, painted into an image, given synthetic key and
wheel events, and its output is read back from the shell. That proves the code
paths run and that scrolling changes what is painted. It is **not** a visual
inspection — nobody has confirmed yet what the rendered terminal looks like on a
live Wayland or X11 session.

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
- `build/kai-term-scrollback` — scrollback model and cursor-mode state.
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
./build/kai-term-widget-smoke
./tests/version.sh ./build/kai-term
```

Verified results on this machine:

- `ctest` — `100% tests passed, 0 tests failed out of 7`.
- `kai-term-parser` prints `PASS`.
- `kai-term-pty` prints `PASS: PTY output contains HELLO and resize 6 40`.
- `kai-term-tmux-e2e` prints `PASS: tmux E2E matches tmux ground truth at all stages`.
- `kai-term-input` prints `PASS: 66 key encodings match the expected PTY bytes`.
- `kai-term-input-pty` prints `PASS: 35 key sequences round-tripped through a real PTY unchanged`.
- `kai-term-scrollback` prints `PASS: 35 scrollback and cursor-mode checks passed`.
- `kai-term-widget-smoke` prints `PASS: 20 offscreen widget checks passed (no visual inspection performed)`.
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
- `src/terminal/vtparser.cpp` — native VT parser.
- `src/terminal/terminalwidget.hpp` — Qt terminal widget, including the scrollback view.
- `src/process/pty.hpp` — PTY and shell lifecycle.
- `tests/parser.cpp` — parser test.
- `tests/pty.cpp` — PTY test.
- `tests/tmux_e2e.cpp` — tmux ground-truth comparison.
- `tests/input.cpp` — key encoding byte assertions.
- `tests/input_pty.cpp` — key sequences replayed through a real PTY.
- `tests/scrollback.cpp` — scrollback model test.
- `tests/widget_smoke.cpp` — offscreen widget test.
- `tests/version.sh` — version test.
- `CMakeLists.txt` — build configuration.

## License

Kai-Term is licensed under the GNU General Public License v3.0 only. See [LICENSE](LICENSE).
