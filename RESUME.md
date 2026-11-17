# Kai-Term Resume State

## Current milestone

**M1 — Basic terminal.**

The native terminal emulator is implemented. Its parser, PTY handling, keyboard
byte encoding, scrollback model and widget code paths are verified by headless
tests (`ctest` runs seven binaries, all passing). The GUI has been exercised with
Qt's offscreen platform, and it has **not** been visually inspected on a live
Wayland or X11 session.

The old project of the same name was deliberately not reused; this is a fresh tree at `/home/kaida/Project/Kai-Term`. The project does **not** depend on `libvterm` or any external terminal emulator. Kai-Term is GPL-3.0-only (`LICENSE`).

## Completed work

- Fresh project directory and documentation.
- Initial CMake project; `ctest` targets for every test binary.
- Native VT parser and screen model.
- PTY creation with `forkpty()`.
- Configurable shell selection (`KAI_SHELL`, then `SHELL`, then `/bin/sh`).
- Qt terminal widget with rendering, keyboard input, output, resize, and scrollback buffer.
- Scrollback **view**: mouse wheel, Shift+PageUp, Shift+PageDown, Shift+Home, Shift+End; typing snaps back to the live bottom.
- Keyboard encoding extracted into `src/terminal/keyencode.hpp` as a pure function.
- DECCKM application cursor keys (`ESC[?1h` / `ESC[?1l`).
- GPL-3.0-only license, expanded `.gitignore`.
- Headless tests: parser, PTY, tmux ground truth, key encoding, key encoding through a real PTY, scrollback model, offscreen widget smoke.
- Removed the libvterm dependency from the design and docs.
- Automatic Qt platform selection for Wayland/X11.

## Current implementation

Key files:

- `src/terminal/screen.hpp` — cell grid, colors, attributes, cursor, scrollback, resize, DECCKM state, alternate screen.
- `src/terminal/keyencode.hpp` — `encodeKey(key, modifiers, text, app_cursor_keys)` returning the exact bytes to write to the PTY. No widget, no PTY, no event loop, so the byte sequences are directly assertable.
- `src/terminal/vtparser.cpp` — native VT parser. Handles cursor movement, SGR colors/attributes, erase, OSC ignore, DEC save/restore, UTF-8, and scroll.
- `src/terminal/terminalwidget.hpp` — Qt widget. Reads PTY output on a reader thread and posts output events to the Qt event loop. Keyboard input is encoded by `keyencode.hpp` and written to the PTY master. Holds `scroll_offset_`, the number of lines scrolled back from the live bottom.
- `src/process/pty.hpp` — `forkpty()` PTY and shell lifecycle. The child branch execs the shell; the parent owns the master descriptor. Resize uses `TIOCSWINSZ`.
- `src/app/main.cpp` — application entry point and terminal widget setup.
- `src/platform/platform.hpp` / `platform.cpp` — Qt platform-plugin selection.

Important implementation notes:

- `scroll_offset_` counts lines scrolled **back** from the bottom, so "older" is a larger offset and the offset is clamped to `historySize()`. Getting this sign consistent was a real bug: wheel-up and Shift+PageUp originally scrolled the wrong way, and the offscreen test caught it.
- Display rows come from `displayRow(r)`, which resolves a scrolled view into a history row or a live screen row. History rows keep the width they were written at, so painting clamps the column count to the row's own size.
- While scrolled back, new output keeps the view anchored on the same lines (`followScroll`) instead of jumping it, the way xterm holds the display still.
- The cursor marker is not drawn while scrolled back, and wheel scrolling is disabled while the alternate screen is active because the application inside it owns scrolling there.
- **Fixed:** lines scrolling off the alternate screen were pushed into the primary scrollback, so vim or less output polluted shell history. The alternate screen now keeps no history at all.
- **Fixed:** resizing while an alternate-screen application was running reassigned the saved primary grid and cleared the saved history, so leaving vim after a resize could reveal an empty shell with no scrollback. The saved grid and history are now re-fitted instead of discarded, and primary rows that no longer fit join the primary history.
- Enter sends `0x0D` (CR) and Backspace sends `0x7F`. Both were confirmed against a real PTY rather than assumed: `stty -a` in the child reports `icrnl` on and `erase = ^?`, and `cat` under the PTY turns a written CR into a delivered line and treats `0x7F` as erase. Output comes back CRLF because `onlcr` is on.
- `tests/input_pty.cpp` runs `cat` under the shell: canonical mode first (line discipline active), then `stty raw -echo` so every byte must return unchanged. Raw mode also keeps `ISIG` off, which is why writing Ctrl+C there is safe.
- Beware `\x1b` and `\x7f` string literals in tests: a hex escape swallows following hex digits (`"\x1bc"` is one character, not ESC + `c`, and `"a\xcc\x81b"` lost the `b`). Split the literals.
- Scrollback is capped at 1000 rows (`Screen::kMaxHistory`) through one `pushHistory()` helper; the cap trims the oldest lines and keeps order.
- `forkpty()` returns a valid master descriptor in the parent but the child branch must execute before checking `master < 0`; this was fixed earlier.
- The child shell gets `TERM=xterm-256color` so a `dumb` inherited term does not break shell startup.
- The PTY test uses `/bin/sh` for determinism, not the user's default `fish`.
- Alternate screen buffers (`ESC[?1049h` / `ESC[?1049l`) are supported; `ESC[3J` clears the screen and scrollback.
- Scroll regions (`ESC[...r]`) and scroll-content-up/down (`ESC[...S]` / `ESC[...T]`) are supported for tmux-style layouts. Content leaving a partial region is discarded, not treated as history.
- Primary Device Attribute queries (`ESC[c]` and `ESC[0c]`) are answered; fish's startup query gets a minimal DEC-style DA reply.
- The escape parser ignores DCS payloads and treats `ESC =` / `ESC >` as complete, so a following `ESC[K` is not printed as literal `[K`.
- A shell exit posts `TerminalExitEvent`, and the widget calls `QCoreApplication::quit()` so the app closes instead of hanging. Verified offscreen: with a shell script that sleeps one second, the app ran the event loop for that second and exited 0.
- `Pty::stop()` uses a SIGTERM timeout followed by SIGKILL.
- `window.show()` is required; without it the Qt process runs its event loop but no window is mapped.
- The terminal uses `QFont("Monospace")` and cell width from `QFontMetricsF::horizontalAdvance('M')`, not `maxWidth()`.

## Build and test status

Verified commands:

```sh
cd /home/kaida/Project/Kai-Term
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Result: build succeeded, no compiler warnings.

```sh
ctest --test-dir build --output-on-failure
```

Result: `100% tests passed, 0 tests failed out of 7` (17.6 s; `input-pty` and
`tmux-e2e` are the slow ones).

Individual results:

- `./build/kai-term-parser` → `PASS`.
- `./build/kai-term-pty` → `PASS: PTY output contains HELLO and resize 6 40`.
- `./build/kai-term-tmux-e2e` → `PASS: tmux E2E matches tmux ground truth at all stages`.
- `./build/kai-term-input` → `PASS: 66 key encodings match the expected PTY bytes`.
- `./build/kai-term-input-pty` → `PASS: 35 key sequences round-tripped through a real PTY unchanged`.
- `./build/kai-term-scrollback` → `PASS: 35 scrollback and cursor-mode checks passed`.
- `./build/kai-term-widget-smoke` → `PASS: 20 offscreen widget checks passed (no visual inspection performed)`.
- `./tests/version.sh ./build/kai-term` → `Kai-Term 0.0.0`.
- `KAI_TERM_TEST_NO_WINDOW=1 ./build/kai-term --help` → usage text printed; no Qt window opened.
- `QT_QPA_PLATFORM=offscreen KAI_SHELL=/bin/true ./build/kai-term` → exits 0.

What the offscreen widget test actually demonstrates: the widget builds, a typed
command reaches the shell and its output is rendered, `seq 1 60` creates
scrollback, one wheel notch moves the offset three rows, painting differs while
scrolled back and returns to identical pixels at the bottom, Shift+PageUp /
Shift+Home / Shift+End move the offset as intended, and typing resets it.

## Known issues

- Nobody has visually inspected the rendered terminal on a live Wayland or X11 session.
- No scrollbar widget, no scroll-position indicator, no "N lines above" hint.
- Mouse text selection, clipboard and mouse reporting are not implemented (M2).
- History rows keep the width they were written at; reflowing scrollback on resize is not done.
- Shift+PageUp/Home/End are claimed by the terminal, so they are not available to applications; a configurable binding is needed before this is final.
- Unicode width handling is basic; combining characters and unusual emoji sequences may need refinement.
- No session daemon, IPC, multiplexer, or file manager exists yet.
- No TOML configuration parser is selected.
- No GitHub Actions workflow has been committed.
- `Pty::stop()` was hardened, but broader process lifecycle work is still M2/M3.

## Project files

- `README.md` — current status, build, tests, usage, license.
- `ARCHITECTURE.md` — subsystem design, native terminal, IPC plan.
- `DEPENDENCIES.md` — verified dependencies and licensing; no libvterm.
- `ROADMAP.md` — M0–M8 status.
- `RESUME.md` — handoff state.
- `LICENSE` — GPL-3.0 license text; Kai-Term is GPL-3.0-only.
- `CMakeLists.txt` — build configuration and `ctest` registration.
- `src/app/main.cpp` — application entry point.
- `src/platform/platform.hpp` / `platform.cpp` — Qt platform-plugin selection.
- `src/terminal/screen.hpp` — native screen model.
- `src/terminal/keyencode.hpp` — Qt key event to PTY bytes.
- `src/terminal/vtparser.cpp` — native VT parser.
- `src/terminal/terminalwidget.hpp` — Qt terminal widget and scrollback view.
- `src/process/pty.hpp` — PTY and shell lifecycle.
- `tests/parser.cpp`, `tests/pty.cpp`, `tests/tmux_e2e.cpp`, `tests/input.cpp`,
  `tests/input_pty.cpp`, `tests/scrollback.cpp`, `tests/widget_smoke.cpp`,
  `tests/version.sh` — the test binaries behind the results above.
- `.gitignore` — ignores build artifacts, generated Qt code, editor state and scratch.

## Next concrete tasks

1. Launch the real GUI in a Wayland and an X11 session and have a person confirm rendering, keyboard feel, scrollback scrolling and resize. Offscreen checks cannot substitute for this.
2. Add a scrollbar or scroll-position indicator, and decide whether Shift+PageUp and friends stay terminal-owned.
3. M2 terminal reliability: mouse text selection, clipboard, mouse reporting, then interactive applications (vim, less, htop, tmux) checked against ground truth.
4. Reflow scrollback rows on resize instead of keeping their original width.
5. Implement M3 session daemon and IPC before multiplexer and file manager integration.

Do not claim the GUI is verified until a person has run it and looked at it.
