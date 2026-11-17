# Kai-Term Resume State

## Current milestone

**M2 — Terminal reliability**, with M1 complete apart from on-screen inspection.

The native terminal emulator is implemented, and so is the first half of M2: mouse
text selection with copy-on-select, middle-click paste with bracketed-paste
wrapping, and mouse reporting for applications. The parser, PTY handling, keyboard
and mouse byte encoding, scrollback model, selection geometry, text extraction and
widget code paths are verified by headless tests (`ctest` runs nine binaries, all
passing). The GUI has been exercised with Qt's offscreen platform, and it has
**not** been visually inspected on a live Wayland or X11 session. The offscreen
platform also has no X11-style PRIMARY selection, so clipboard behaviour against a
real desktop is **not** verified either.

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
- Mouse text selection (drag, double-click word, triple-click line), copy on release, selection over retained lines while scrolled up, and Shift to take the mouse back from an application that grabbed it.
- Middle-click paste, newlines normalised to carriage returns, fenced with `ESC[200~` / `ESC[201~` when the application asked for bracketed paste.
- Mouse reporting: `?1000` click, `?1002` drag, `?1003` any-event, wheel as buttons 64/65, Shift/Meta/Ctrl bits, and classic, urxvt, SGR and hex-SGR coordinates.
- Screen, row and selection text extraction as pure functions.
- Headless tests: parser, PTY, tmux ground truth, key encoding, key and mouse encoding through a real PTY, scrollback and mode state, mouse encoding, selection and text extraction, offscreen widget smoke.
- Removed the libvterm dependency from the design and docs.
- Automatic Qt platform selection for Wayland/X11.

## Current implementation

Key files:

- `src/terminal/screen.hpp` — cell grid, colors, attributes, cursor, scrollback, resize, DECCKM state, alternate screen.
- `src/terminal/keyencode.hpp` — `encodeKey(key, modifiers, text, app_cursor_keys)` returning the exact bytes to write to the PTY. No widget, no PTY, no event loop, so the byte sequences are directly assertable.
- `src/terminal/mouseencode.hpp` — `encodeMouse(report, mode, coord)` in the same style. Takes a `MouseReport` (button, held bitmask, action, modifier bits, zero-based row and column) and returns the bytes, or an empty string when the event should not be reported. Deliberately free of Qt types so the screen model can include it without pulling Qt into the pure tests.
- `src/terminal/selection.hpp` — `CellPos`, `Selection{anchor, head}` with the head column exclusive, and `expandToWord()`, which takes a classifier callback so it does not depend on the cell representation.
- `src/terminal/text.hpp` — `rowText()`, `screenText()`, `selectionText()`, and `selectionTextAt()`, which takes a row callback so the widget can resolve scrolled-back rows through the same code.
- `src/terminal/vtparser.cpp` — native VT parser. Handles cursor movement, SGR colors/attributes, erase, OSC ignore, DEC save/restore, UTF-8, and scroll.
- `src/terminal/terminalwidget.hpp` — Qt widget. Reads PTY output on a reader thread and posts output events to the Qt event loop. Keyboard input is encoded by `keyencode.hpp` and written to the PTY master. Holds `scroll_offset_`, the number of lines scrolled back from the live bottom, and the current `Selection`. Mouse press, move and release either build a selection or, when the application asked for reporting, go to `encodeMouse()` and the PTY; `cellSize()` and `cellTopLeft()` expose the grid geometry so a test can aim at a character.
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
- Mouse events split on `screen_.mouseReporting()`, with Shift overriding it, which is the xterm convention and the only way to copy text out of a program that grabbed the mouse. A press with reporting on must not start a selection, and a wheel notch becomes a button report instead of a scrollback move — including in the alternate screen, where vim and less expect wheel reports.
- Motion is reported only when the pointer moves onto a different cell (`last_reported_`), the way xterm does; otherwise a slow application drowns in one report per pixel.
- Qt 6.11 as packaged here exposes no click count on `QMouseEvent` (`m_doubleClick` is protected in `QSinglePointEvent`), and a synthesised event would not carry one anyway. Clicks are therefore counted in the widget: presses on the same cell within 400 ms are one gesture, two select a word, three select the line. This also makes double- and triple-click testable offscreen.
- `QClipboard` has two modes and not every platform has both. The offscreen platform supports the clipboard and not the X11-style selection, and Qt prints `Data set on unsupported clipboard mode` when asked. Both `copySelection()` and the paste path check `supportsSelection()`; the widget-smoke test prints a bracketed note when it had to skip those checks rather than counting them as passed.
- A paste turns `\n` into `\r` and collapses `\r\n` into one newline, so a pasted script cannot run commands that were never pressed. Bracketed-paste fencing is applied per paste, based on the mode the application currently has set.
- The classic three-byte report carries button and both coordinates as single bytes offset by 32, so the largest coordinate it can name is 223; further out the coordinate clamps rather than the event being dropped. The SGR forms have no such limit and are asserted at column 256.
- A release is button code 3 in the classic and urxvt forms, which have no release marker, and the released button's own code with a lower-case `m` terminator in the SGR forms.
- New output clears the selection. A highlight that silently drifts onto different characters is worse than one that disappears; keeping a selection pinned to its text is later work.
- Word characters for double-click are ASCII letters, digits and underscore, matching shell word movement. Punctuation and non-ASCII glyphs select one cell at a time.
- `tests/widget_smoke.cpp` reads back what the widget writes to the PTY by running `printf` to set modes, then `stty raw -echo; timeout 2 cat > file`, delivering the events, and comparing the file with the bytes `encodeMouse()` should have produced. The shell is still reading commands when the modes are set, which is why the setup runs as one command line before `cat` starts.
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

Result: `100% tests passed, 0 tests failed out of 9` (28.6 s; `widget-smoke` and
`input-pty` are the slow ones now, because both wait on real timeouts).

Individual results:

- `./build/kai-term-parser` → `PASS`.
- `./build/kai-term-pty` → `PASS: PTY output contains HELLO and resize 6 40`.
- `./build/kai-term-tmux-e2e` → `PASS: tmux E2E matches tmux ground truth at all stages`.
- `./build/kai-term-input` → `PASS: 66 key encodings match the expected PTY bytes`.
- `./build/kai-term-input-pty` → `PASS: 47 input sequences round-tripped through a real PTY unchanged` (35 keys, 12 mouse reports in classic, urxvt, SGR and hex-SGR forms).
- `./build/kai-term-scrollback` → `PASS: 56 scrollback, cursor-mode and mouse-mode checks passed`.
- `./build/kai-term-mouse` → `PASS: 36 mouse encodings match the expected PTY bytes`.
- `./build/kai-term-selection` → `PASS: 101 selection and text-extraction checks passed`.
- `./build/kai-term-widget-smoke` → `PASS: 40 offscreen widget checks passed (no visual inspection performed) [this platform has no separate X11-style selection: only the clipboard copy was exercised]`. The bracketed part is the test reporting what it could not run.
- `./tests/version.sh ./build/kai-term` → `Kai-Term 0.0.0`.
- `KAI_TERM_TEST_NO_WINDOW=1 ./build/kai-term --help` → usage text printed; no Qt window opened.
- `QT_QPA_PLATFORM=offscreen KAI_SHELL=/bin/true ./build/kai-term` → exits 0.

What the offscreen widget test actually demonstrates: the widget builds, a typed
command reaches the shell and its output is rendered, `seq 1 60` creates
scrollback, one wheel notch moves the offset three rows, painting differs while
scrolled back and returns to identical pixels at the bottom, Shift+PageUp /
Shift+Home / Shift+End move the offset as intended, and typing resets it.

It also demonstrates the input work: a click selects nothing, a five-cell drag
selects five characters and copies them, a double click takes the word, a triple
click the line, a drag while scrolled back copies the retained line, painting
changes while a selection is held, and what a program inside the terminal receives
for a press, a drag, a release, a wheel turn and a paste is exactly the encoded
bytes — read back from the terminal's own tty through `cat`. With reporting on, a
press goes to the program instead of selecting; a paste is still a paste, and with
`?2004` set it arrives fenced.

## Known issues

- Nobody has visually inspected the rendered terminal on a live Wayland or X11 session.
- No scrollbar widget, no scroll-position indicator, no "N lines above" hint.
- Selection appearance, PRIMARY selection across applications, and mouse reporting against real interactive applications have not been inspected at a live session; offscreen Qt has a clipboard but no X11-style selection.
- A selection is dropped when new output arrives, so copying from a continuously updating display (a log tail, `yes`) needs the drag finished during a pause.
- There is no keyboard copy or paste binding yet (Ctrl+Shift+C and friends); copy happens on release and paste on middle click.
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
- `src/terminal/mouseencode.hpp` — mouse event to PTY bytes.
- `src/terminal/selection.hpp` — selection geometry and word expansion.
- `src/terminal/text.hpp` — screen, row and selection text extraction.
- `src/terminal/vtparser.cpp` — native VT parser.
- `src/terminal/terminalwidget.hpp` — Qt terminal widget and scrollback view.
- `src/process/pty.hpp` — PTY and shell lifecycle.
- `tests/parser.cpp`, `tests/pty.cpp`, `tests/tmux_e2e.cpp`, `tests/input.cpp`,
  `tests/input_pty.cpp`, `tests/scrollback.cpp`, `tests/mouse.cpp`,
  `tests/selection.cpp`, `tests/widget_smoke.cpp`, `tests/version.sh` — the test
  binaries behind the results above.
- `.gitignore` — ignores build artifacts, generated Qt code, editor state and scratch.

## Next concrete tasks

1. Launch the real GUI in a Wayland and an X11 session and have a person confirm rendering, keyboard feel, scrollback scrolling and resize. Offscreen checks cannot substitute for this.
2. Add a scrollbar or scroll-position indicator, and decide whether Shift+PageUp and friends stay terminal-owned.
3. Run vim, less, htop and tmux at a real session with the mouse: confirm reports reach them, that Shift still lets you copy, and that middle click pastes between applications.
4. Add a keyboard copy binding (Ctrl+Shift+C) and a paste binding, then decide what to do about a selection that survives new output.
5. Reflow scrollback rows on resize instead of keeping their original width.
6. Implement M3 session daemon and IPC before multiplexer and file manager integration.

Do not claim the GUI is verified until a person has run it and looked at it.
