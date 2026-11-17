# Kai-Term Resume State

## Current milestone

**M1 — Basic terminal.**

The native terminal emulator is implemented and has headless parser and PTY tests that pass. The GUI was launched once, but it has not been visually verified.

The old project of the same name was deliberately not reused; this is a fresh tree at `/home/kaida/Project/Kai-Term`. The project does **not** depend on `libvterm` or any external terminal emulator.

## Completed work

- Fresh project directory and documentation.
- Initial CMake project.
- Native VT parser and screen model.
- PTY creation with `forkpty()`.
- Configurable shell selection (`KAI_SHELL`, then `SHELL`, then `/bin/sh`).
- Qt terminal widget with rendering, keyboard input, output, resize, and scrollback buffer.
- Headless parser test and PTY test.
- Removed the libvterm dependency from the design and docs.
- Automatic Qt platform selection for Wayland/X11.

## Current implementation

Key files:

- `src/terminal/screen.hpp` — cell grid, colors, attributes, cursor, scrollback, resize.
- `src/terminal/vtparser.cpp` — native VT parser. Handles cursor movement, SGR colors/attributes, erase, OSC ignore, DEC save/restore, UTF-8, and scroll.
- `src/terminal/terminalwidget.hpp` — Qt widget. Reads PTY output on a reader thread and posts output events to the Qt event loop. Keyboard input is sent to the PTY master.
- `src/process/pty.hpp` — `forkpty()` PTY and shell lifecycle. The child branch execs the shell; the parent owns the master descriptor. Resize uses `TIOCSWINSZ`.
- `src/app/main.cpp` — application entry point and terminal widget setup.
- `src/platform/platform.hpp` / `platform.cpp` — Qt platform-plugin selection.

Important implementation notes:

- `forkpty()` returns a valid master descriptor in the parent but the child branch must execute before checking `master < 0`; this was fixed.
- The child shell gets `TERM=xterm-256color` so a `dumb` inherited term does not break shell startup.
- The PTY test uses `/bin/sh` for determinism, not the user's default `fish`.
- Scrollback is retained in memory but has no scrollback UI yet.
- Keyboard behavior is implemented at the Qt widget level but not runtime-verified.
- Arrow keys are sent to fish as ANSI cursor bindings (`ESC[A]`, `ESC[B]`, `ESC[C]`, `ESC[D]`); fish moves its own command-line cursor and updates autosuggestions. A visible cursor marker follows the screen cursor.
- Rendering has a 10px padding buffer around the glyph grid.
- Alternate screen buffers (`ESC[?1049h` / `ESC[?1049l`) are supported; `ESC[3J` clears the screen and scrollback.
- Scroll regions (`ESC[...r]`) and scroll-content-up/down (`ESC[...S]` / `ESC[...T]`) are supported for tmux-style layouts.
- Primary Device Attribute queries (`ESC[c]` and `ESC[0c]`) are answered.
- Fish’s Primary Device Attribute query is answered with a minimal DEC-style DA response; the fish startup warning was tested in a standalone probe.
- The escape parser ignores DCS payloads and treats `ESC =` / `ESC >` as complete, so a following `ESC[K` is not printed as literal `[K`.
- A shell exit posts `TerminalExitEvent`, and the widget calls `QCoreApplication::quit()` so the app closes instead of hanging.
- `Pty::stop()` now uses a SIGTERM timeout followed by SIGKILL.
- `window.show()` is required; without it the Qt process runs its event loop but no window is mapped.
- The terminal uses `QFont("Monospace")` and cell width from `QFontMetricsF::horizontalAdvance('M')`, not `maxWidth()`.

## Build and test status

Verified commands:

```sh
cd /home/kaida/Project/Kai-Term
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Result: build succeeded for all three executables.

```sh
./build/kai-term-parser
```

Result: `PASS`.

```sh
./build/kai-term-pty
```

Result: `PASS: PTY output contains HELLO and resize 6 40`.

```sh
./tests/version.sh ./build/kai-term
```

Result: `Kai-Term 0.0.0`.

```sh
KAI_TERM_TEST_NO_WINDOW=1 ./build/kai-term --help
```

Result: usage text printed; no Qt window opened.

The GUI was launched in the Wayland session with automatic platform selection. The Qt window was mapped and appeared as `Kai-Term` in the compositor window list. The process stayed alive; no Qt platform-plugin error or new core dump was produced. It was not visually inspected, and keyboard behavior was not tested.

## Known issues

- GUI runtime behavior (rendering, keyboard focus, actual terminal interaction) is unverified.
- The app’s keyboard behavior with fish was not runtime-tested; fish’s startup query was tested with a standalone PTY probe.
- Scrollback buffer is not exposed in the UI.
- Mouse reporting and clipboard are not implemented.
- Unicode width handling is basic; combining characters and unusual emoji sequences may need refinement.
- No session daemon, IPC, multiplexer, or file manager exists yet.
- No TOML configuration parser is selected.
- No GitHub Actions workflow has been committed.
- `Pty::stop()` was hardened, but broader process lifecycle work is still M2/M3.

## Project files

- `README.md` — current status, build, tests, usage.
- `ARCHITECTURE.md` — subsystem design, native terminal, IPC plan.
- `DEPENDENCIES.md` — verified dependencies and licensing; no libvterm.
- `LICENSE` — GPL-3.0 license text; Kai-Term is GPL-3.0-only.
- `ROADMAP.md` — M0–M8 status.
- `RESUME.md` — handoff state.
- `CMakeLists.txt` — build configuration.
- `src/app/main.cpp` — application entry point.
- `src/platform/platform.hpp` / `platform.cpp` — Qt platform-plugin selection.
- `src/terminal/screen.hpp` — native screen model.
- `src/terminal/vtparser.cpp` — native VT parser.
- `src/terminal/terminalwidget.hpp` — Qt terminal widget.
- `src/process/pty.hpp` — PTY and shell lifecycle.
- `tests/parser.cpp` — parser test.
- `tests/pty.cpp` — PTY test.
- `tests/version.sh` — version test.
- `.gitignore` — ignores build artifacts and scratch.
- `build/` — generated build output; not source.

## Next concrete tasks

1. Launch the real GUI in a Wayland and X11 test session and verify rendering, keyboard input, resize, and shell output. The user verifies visual behavior.
2. Add a dedicated keyboard/input check using the PTY byte ground truth, not the screen.
3. Implement scrollback UI and refine Unicode width handling.
4. Move to M2 terminal reliability: clipboard, mouse reporting, interactive applications, signal handling.
5. Implement M3 session daemon and IPC before multiplexer and file manager integration.

Do not claim the GUI is verified until it is actually run and checked.
