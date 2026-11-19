# Kai-Term Resume State

## Current milestone

**M4 — Native multiplexer.** M1, M2 and M3 are closed.

M3 put the pty and the screen model in a process of their own: a session is
something a window looks at rather than something a window keeps alive. There is a
daemon (`kai-term serve` — the same binary as the window, started a different
way), a framed protocol over a Unix
socket, `kai-term new|list|attach|detach|kill|send|shutdown` on the command line,
`attach --window` for a window onto a running session, a backend seam so the widget
cannot tell a local shell from a session across the socket, and pane-scoped selection
so a drag stops at the edge of the pane it started in.

M4 has its first pieces in: a window's shell is held by the daemon (started for the
window, not before it), Ctrl+Shift+S keeps that shell and starts a fresh one in the same
window, Ctrl+Shift+A lists the kept and left-behind sessions inside the terminal
and brings the chosen one back — asking first when something is running where you
stand — and closing a window with something running asks instead of ending it. The
command line was flattened to what Kaida agreed verb by verb: the words come straight
after `kai-term`, a number from the listing is the handle, and a shell started by the
daemon carries its number in `KAI_SESSION`, which is what lets `save` and `detach` be
argument-free inside a session and lets `list` star the session you are standing in. A window can be pointed at
another shell while it stays open, which is what restoring means.

**The keep-this-shell keybind and the close question in M4 have been used at a live
Wayland session, on niri, by Kaida, who reports they work.** (The keybind was
Ctrl+Shift+D when that was said; it is Ctrl+Shift+S now, by request, and the D is
retired. The question was a desktop dialog then; it is the terminal's own now, also by
request.) Everything else in M3 and M4 is checked headless or
offscreen only: 126 daemon checks over a real pty and a real socket, 44 checks of a
window rendering a session and switching between two, 78 checks of the application
window's keybinds and dialogs, 18 of when a close asks at all and what it offers, 24
checks of drags across a pane boundary, plus the protocol, socket and snapshot tests.
What an attached window looks like has been seen — the re-attach bugs Kaida found
were found at a live session — and the close question has been read and answered
there twice over, once as a native dialog and once as the terminal's own question.
What the session list and the switch question look like on a live display, and what
attaching feels like to someone who did not build it, belong to whoever runs it next.

M1 and M2 are unchanged in that respect: tested headlessly and offscreen, and used at a
live Wayland session by a person, but never inspected on X11.

The native terminal emulator and the input work are implemented: mouse text
selection with copy on demand, middle-click paste with bracketed-paste wrapping,
mouse reporting for applications, a scrollback view that reflows and shows where it
is, and a shell teardown that takes the whole session with it. The parser, PTY
handling, keyboard and mouse byte encoding, scrollback model, scroll indicator,
process lifecycle, selection geometry, text extraction and widget code paths are
verified by headless tests (`ctest` runs twenty-one binaries, all passing), and one of
them drives a real application (btop) with the mouse inside the widget. The GUI has
been exercised with Qt's offscreen platform, and a person has used it at a live
Wayland session for selection, paste into other applications and general
appearance. It has **not** been inspected on X11, and this machine cannot do that:
there is no X server and no `Xvfb`, and the live compositor is not reachable from a
shell session. The full inventory was checked — Qt plugins `linuxfb`, `minimal`,
`offscreen`, `vkkhrdisplay`, `vnc`, `wayland`, `xcb`; the only server binary is
`Xwayland`, which needs a compositor above it and none is installed; no `DISPLAY`, no
`WAYLAND_DISPLAY`, no `/dev/fb0`, no passwordless `sudo`. The `vnc` plugin does bring
the app up (`QVncServer created on port 5911`, socket listening), which shows the
widget works on a real raster platform, but that is not the X11 path either. Note also
that `kai-term-widget-smoke` sets `QT_QPA_PLATFORM=offscreen` itself, so pointing the
environment at another platform does not move the suite — which was worth finding out
before believing a run that had not actually moved. The offscreen platform also has no X11-style PRIMARY selection, so
PRIMARY behaviour against a real desktop is **not** verified either.

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
- Mouse text selection (drag, double-click word, triple-click line), selection over retained lines while scrolled up, and Shift to take the mouse back from an application that grabbed it.
- Middle-click paste, newlines normalised to carriage returns, fenced with `ESC[200~` / `ESC[201~` when the application asked for bracketed paste.
- Mouse reporting: `?1000` click, `?1002` drag, `?1003` any-event, wheel as buttons 64/65, Shift/Meta/Ctrl bits, and classic, urxvt, SGR and hex-SGR coordinates. The SGR and hex-SGR forms were later fixed: they were written with the coordinates before the button and without the leading `<`, so no application could read them.
- Copy on demand: Ctrl+Shift+C and Ctrl+Shift+V. Finishing a drag no longer touches the clipboard; it offers PRIMARY on X11 and leaves the highlight for the user to read.
- A selection follows its text: it moves when output pushes lines or the view scrolls, and is dropped when its lines leave the window. Shift+click moves the end of a selection.
- `kai-term-mouse-app`: btop run inside the widget offscreen, checked by watching its own scroll counter move.
- Screen, row and selection text extraction as pure functions.
- Headless tests: parser, PTY, tmux ground truth, key encoding, key and mouse encoding through a real PTY, scrollback and mode state, mouse encoding, selection and text extraction, offscreen widget smoke.
- Removed the libvterm dependency from the design and docs.
- Automatic Qt platform selection for Wayland/X11.

Work done after M2 closed, from an audit of what the terminal silently ignored:

- Scrollback **reflows** on resize: retained lines are re-wrapped to the new width
  and the cursor keeps its window row.
- A scroll-position indicator along the right edge, drawn only while the view is
  away from the live bottom.
- A selection dragged to the top or bottom edge scrolls the view on its own.
- Process lifecycle: exit code and signal reported, the shell's **session**
  signalled so background jobs cannot outlive it, a bounded wait in place of one
  that could hang, an explicitly named shell that fails loudly instead of falling
  back to `/bin/sh`, and a reader thread that notices the shell is gone even while
  a stray job holds the stream open.
- A cursor position past the last column wrote outside the cell grid. Columns are
  clamped wherever one can arrive, and the reproductions run clean under
  ASan/UBSan.
- OSC acted on rather than swallowed: titles (with the `ESC[22 t` / `ESC[23 t`
  stack), working directory, clipboard set and query (`OSC 52`), default colours
  (`OSC 10/11`) and palette entries (`OSC 4`, `OSC 104`).
- XTWINOPS size and title queries answered, and the kitty keyboard query answered
  with "no" — an application that gets no reply sits waiting for one.
- SGR sub-parameters in both spellings (`38:2::r:g:b`), out-of-range palette
  indices left alone instead of clamped to a grey, and 21, 28 and 6 doing something.
- Lines SU pushes off a whole screen go into the scrollback, as a newline's would.
- DECRC puts back the rendition and the wrap flag that DECSC saved, not just the
  position. UTF-8 is decoded with its three checks — shortest form, no surrogates,
  nothing past the last codepoint — so a lone surrogate becomes a replacement
  character instead of a cell whose text cannot be written back out. The leading
  Hangul jamo are wide and the vowels after them are not; they had been one narrow
  block, which put Korean in jamo half a cell out and took every cell after it with
  it. Origin mode survives a resize; the scroll region deliberately does not —
  keeping the old margins, which seemed the better behaviour, left a window that
  grew from 24 rows to 50 unable to scroll at all, because rows 25 to 49 were
  outside the region, and the widget test's `seq 1 60` produced no history.
- DECSCUSR (`ESC[<n> q`) selects the cursor shape — block, underline or bar — and
  the widget draws all three. Kai-Term's default stays the underline it has always
  drawn, so an application that never asks sees no change. Getting a fair offscreen
  comparison took some thought: the sequence has to be printed by a program inside
  the terminal (bytes typed at a prompt are eaten by the line editor and never reach
  the terminal's own parser), and each command costs a line of screen, so the
  assertion compares the cell the cursor stands on rather than the whole window. A
  full and a soft reset hand the shape back, and `ESC c` now also returns the colour
  defaults and any custom palette entries.
- `?1005` UTF-8 mouse coordinates, with the report bytes pinned by test: column 300
  plus 32 is U+014C, which is `C5 8C` on the wire. Getting that expectation right
  mattered more than the code — an expectation written wrong in the same way as the
  implementation proves nothing.
- `?2026` synchronized output. Inside a block the widget repaints at most once every
  100 ms instead of once per chunk read from the PTY, which is what stops a program
  drawing a whole screen from being shown half drawn. It is pacing rather than a true
  off-screen frame, and says so in the comment. The mode ending arrives with a chunk,
  so the finished frame is painted then, and an application that turns the mode on and
  dies cannot leave the screen frozen: the next chunk repaints once the interval has
  passed either way. The pacing is asserted as the decision the widget makes rather
  than as a wall-clock race, which would fail on a loaded machine and pass for the
  wrong reason everywhere else.
- SGR 53 overline, and the whole attribute table pinned by test: each of bold, dim,
  italic, underline, blink, reverse, hidden, strike and overline checked on, off with
  the code xterm uses, and cleared by a reset. Adding overline exposed a painting
  mistake underneath: underline, strike and overline shared one position variable, so
  whichever was asked for last won and the others were silently dropped — a struck
  word with an underline lost its underline. Each now draws its own line, and the
  test compares two rows carrying the same word, one overlined, so the difference it
  measures is that line.
- `-Wall -Wextra` on for the whole build, compiling clean. What it found the same
  day: a `string_view` over a temporary `std::string` in the SGR parser — invisible
  at `-O1`, wrong at `-O3` — and an initialiser list whose order was not the
  declaration order. The same run of warnings caught `"\x1b7"` in a test, where
  the digit after ESC is itself a hex digit; that trap is written down above, and
  still managed to happen again.

M3 — sessions that outlive their window:

- `src/ipc/protocol.hpp`: frames `<len><kind><body>` and `key=value` fields with
  `\`, `\n`, `\r` and `=` escaped, records separated by `0x1e`. No JSON and no
  dependency; a request id and a version field were planned and did not earn their
  place — one daemon per user means a connection answers its own requests in order.
- `src/ipc/socket.hpp`: `$XDG_RUNTIME_DIR/kai-term/<name>.sock`, directory 0700, socket
  0600, `SO_PEERCRED` uid checked on every connection, a stale socket removed only if it
  is a socket and ours, and a foreign file in the way reported rather than unlinked.
  Every missing level of the path is created; `XDG_RUNTIME_DIR` pointing somewhere that
  does not exist yet is how someone puts sessions in a place of their own, and it took a
  real `kai-term open` to notice the daemon could not bind there.
- `src/session/snapshot.hpp`: the daemon's `Screen` rendered back out as escape
  sequences, so a view joining later is shown what is on the screen now. Round-tripped
  through a fresh model in the test, cell for cell. This is why the daemon owns a model
  rather than only the bytes: replaying raw output at a different column count lays the
  screen out wrong.
- `src/session/daemon.hpp`: sessions, views, and the commands; the reader thread per
  session feeds the model, **answers the queries programs ask** (fish waits ten seconds
  for a device attributes reply and then prints a warning about its terminal; tmux will
  not start properly), turns `OSC 52` into a `Clipboard` frame for a view to carry, and
  reaps the shell.
- `src/terminal/backend.hpp` / `src/session/sessionbackend.hpp`: what the widget talks
  to. Two differences are in the interface instead of hidden behind it, because both were
  wrong when hidden — a pty has a child to notice leaving (a socket's transport says so
  itself), and a pty is the terminal a program is asking questions of (a view of a remote
  session answers nothing, or the program gets two replies).
- Pane-scoped selection: `PaneRect` in `selection.hpp`, `setPanes()` on the widget, and
  clipping in three places — the drag's ends, the middle rows of a multi-row drag
  (`selectionTextInPane`), and the highlight (`selectedAt`). Three clicks take the line of
  the pane, not of the screen. No panes means one pane, the screen; a press on a divider
  selects unclipped rather than not at all. Turning the clipping off fails 12 of the 24
  pane checks, which is how the tests were shown to be watching.
- Learned about sockets and recorded in the code: waking a thread blocked in `read()` on
  *our* socket needs `shutdown(fd, SHUT_RDWR)` — `SHUT_WR` only tells the peer; and
  `send(..., MSG_NOSIGNAL)` fails with `ENOTSOCK` on a pipe, which is why CLI output uses
  a plain `write` loop and an `attach` with its output redirected stopped printing
  nothing at all.
- A background daemon now execs `kai-term serve`, because a copy of whatever
  started it shows up in `ps` under a stranger's command line and nobody can
  tell what is holding their shells. The Command column has said so ever since; the
  Program column still said `exe`, because that is the last part of the
  `/proc/self/exe` path the daemon execs by, so `serve` now names the task
  `kai-term-daemon` with `prctl(PR_SET_NAME)` before it starts listening.

Fixes found by a person running it (niri, 2026-11-17):

- Closing a window with the compositor's own key closed the window and **left the shell
  running**, so the next `session list` showed a session nobody had kept. The word that
  ends an unkept shell was only said in the branch where the close prompt appeared, and a
  window closed with nothing running never gets there — the quiet path is the common one.
  An on-purpose close now ends an unkept shell whatever else it does. The checks go
  through `closeEvent` rather than calling the teardown by hand, which is what let this
  through: the test that covered it made the call itself, and passed while the window
  never did. Turning the fix off fails two checks, verified.

- Restoring a session showed an empty window until a key was pressed, and then the
  whole screen appeared at once. The session's screen arrives with the handshake that
  opens the connection, and a screen bigger than one read (4 KB) is left in the
  backend's buffer while the reader waits for the socket to become readable — which
  nothing is going to do, because the session has already said everything it means to.
  The first 4 KB of a restored screen is usually its clearing sequence, so what was on
  screen was a blank, and the first keystroke made the shell answer, which woke the
  wait and let the rest of it out. A backend that is holding bytes is now read instead
  of waited on. The check fills a 60x200 session, prints distinct text and joins it
  without typing anything: with the fix off, both its checks fail.

Work done since, which is the start of M4 — the workflow a person described rather than
the one a multiplexer manual describes:

- `kai-daemon` is gone. One program is built; the daemon is `kai-term serve`, and
  a window asks for a daemon rather than requiring one. Twenty test binaries moved to
  `build/tests/` so the build directory holds the terminal and nothing else.
- A session records whether it has been kept. A window's shell is unkept and named `w1`,
  `w2` …; `kai-term list` does not show those, but does show one that was left behind when
  a window died without saying so, next to a column naming what is running out front.
- `Detach` with `discard=1` ends an unkept session; silence does not. That is the whole
  difference between closing a terminal and losing one, and it is a message field rather
  than a new command so a view that does not know about it still detaches safely.
- `save` promotes a session, optionally renaming it, and refuses a name in use.
- `src/process/job.hpp`: the foreground process group of a terminal, read from `/proc`,
  empty when it is the shell. This is what a close prompt names.
- `TerminalWidget::showSession()` points an open window at another shell: the reader is
  joined before the backend it reads from dies, the far end is told whether this window is
  closing or only looking elsewhere, the model is emptied, and reader events carry the
  number of the shell that produced them so a late byte from the old one cannot be painted
  on the new screen.
- `src/app/appwindow.hpp`: Ctrl+Shift+S keeps and starts fresh, Ctrl+Shift+A offers the
  kept ones, closing with something running asks. `src/app/closeprompt.hpp` holds the
  decision with no Qt in it, so the asking is testable and only the dialog has to be
  looked at.
- An `OSC 52` read (`… ; ?`, which is what vim and scp send — an empty payload alone was
  treated as the query, so the commonest form of it emptied the clipboard instead) is
  forwarded to views with `query=1`, and a view's answer goes back down as a `Data` frame.
- The protocol between a window and the daemon has a number, `kProtocolVersion`, and the
  daemon's socket is named after it (`daemon-v3.sock`), so a daemon left running by an
  older build is not the one a new build talks to. This was found by it actually
  happening: a daemon from before the kept-sessions work was still running here, and the
  new keybind said `unknown command: save` — while `session list`, reading a reply with no
  `saved` in it, hid every shell that daemon was holding. Both look like a broken terminal.
  An old daemon is now noticed by pinging the other `daemon*.sock` files in the runtime
  directory and reported once — protocol, pid, and that its shells cannot be reached — in
  the window's corner, in `session list` and when a daemon starts. It is never stopped by
  itself: those shells belong to whoever started them. A client that says a different
  version is refused by name, with the same instruction. Checked live against the real
  stale daemon on this machine, and in the test with a socket pretending to be one.
- A program that takes the whole screen (vim, less, btop, tmux) now gets the whole screen:
  the view's scroll capacity is one function, `scrollRange()`, and it is zero on the
  alternate screen, so the wheel, Shift+PageUp, the thumb drag and edge auto-scroll all do
  nothing and no scroll bar is drawn over the program. Before this the indicator was
  drawn from the hidden shell's history and Shift+PageUp scrolled a full-screen
  application's viewer through text it had never shown. The history itself was always
  safe — the model discards alternate-screen rows — and comes back with the shell.

- Three checks in the widget tests were found to be passing without testing anything: they
  pasted a command and looked for its output, and a shell with bracketed paste on — bash
  and fish both — leaves a pasted command sitting at the prompt. They press Return now and
  look for what the command *prints*, which is not in what was typed.

## Current implementation

Key files:

- `src/terminal/screen.hpp` — cell grid, colors, attributes, cursor, scrollback, resize, DECCKM state, alternate screen.
- `src/terminal/keyencode.hpp` — `encodeKey(key, modifiers, text, app_cursor_keys)` returning the exact bytes to write to the PTY. No widget, no PTY, no event loop, so the byte sequences are directly assertable.
- `src/terminal/mouseencode.hpp` — `encodeMouse(report, mode, coord)` in the same style. Takes a `MouseReport` (button, held bitmask, action, modifier bits, zero-based row and column) and returns the bytes, or an empty string when the event should not be reported. Deliberately free of Qt types so the screen model can include it without pulling Qt into the pure tests.
- `src/terminal/selection.hpp` — `CellPos`, `Selection{anchor, head}` with the head column exclusive, and `expandToWord()`, which takes a classifier callback so it does not depend on the cell representation.
- `src/terminal/text.hpp` — `rowText()`, `screenText()`, `selectionText()`, and `selectionTextAt()`, which takes a row callback so the widget can resolve scrolled-back rows through the same code.
- `src/terminal/vtparser.cpp` — native VT parser. Handles cursor movement, SGR colors/attributes, erase, OSC ignore, DEC save/restore, UTF-8, and scroll.
- `src/terminal/terminalwidget.hpp` — Qt widget, owning the selection, the copy and paste bindings, and the scrollback view. Reads PTY output on a reader thread and posts output events to the Qt event loop. Keyboard input is encoded by `keyencode.hpp` and written to the PTY master. Holds `scroll_offset_`, the number of lines scrolled back from the live bottom, and the current `Selection`. Mouse press, move and release either build a selection or, when the application asked for reporting, go to `encodeMouse()` and the PTY; `cellSize()` and `cellTopLeft()` expose the grid geometry so a test can aim at a character. Draws the window's dialogs inside itself (`askInTerminal()`, `chooseInTerminal()`), in the terminal's own font, and insets the grid by a margin that goes to zero while an application holds the alternate screen (`activePadding()`).
- `src/process/pty.hpp` — `forkpty()` PTY and shell lifecycle. The child branch execs the shell; the parent owns the master descriptor. Resize uses `TIOCSWINSZ`.
- `src/app/main.cpp` — application entry point: arguments, then one window.
- `src/app/appwindow.hpp` — the window a person runs. Owns the session name, Ctrl+Shift+S
  (keep this shell, start a fresh one here), Ctrl+Shift+A (the sessions you could go back
  to, listed inside the terminal, which restores the one chosen after asking the close
  question's twin when something is running where you stand) and the close prompt. The
  widget stays ignorant of sessions.
- `src/app/sessionmenu.hpp` — what one row of the session list says: the id, the name,
  what is running, kept or left behind. No Qt, so the words are tested without a window.
- `src/app/closeprompt.hpp` — `ConfirmPolicy` (`never` / `running` / `always`, the words
  Old-Term's `confirm_quit` takes) and `closeQuestion()` / `closeOfferSave()`:
  when closing has to ask, and what it asks. No Qt, so the decision is tested and only the
  dialog is left for a person to look at.
- `src/process/job.hpp` — `foregroundJob(master, shell)`: the command name of the process
  group the terminal says owns the foreground, empty when that group is the shell itself.
- `src/session/daemon.hpp` — sessions, views, commands, and the difference between a kept
  session and a window's shell: `save`, `discard`, and a `Guard` that ends an unkept
  session when the view that leaves said it was closing.
- `src/session/cli.hpp` — the `session` verbs, and the small set of operations a window
  uses (`startWindowSession`, `saveSession`, `savedSessions`). One client implementation
  for both, so the command line and the window cannot drift apart.
- `src/platform/platform.hpp` / `platform.cpp` — Qt platform-plugin selection.

Important implementation notes:

- `scroll_offset_` counts lines scrolled **back** from the bottom, so "older" is a larger offset and the offset is clamped to `historySize()`. Getting this sign consistent was a real bug: wheel-up and Shift+PageUp originally scrolled the wrong way, and the offscreen test caught it.
- Display rows come from `displayRow(r)`, which resolves a scrolled view into a history row or a live screen row. A resize re-wraps the retained lines to the new width, but painting still clamps the column count to the row's own size, because a re-wrapped line can end short of the margin.
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
- **Fixed:** the SGR and hex-SGR mouse reports were written as `CSI < col ; row ; button M`. xterm's form is `CSI < button ; col ; row M`, and the leading `<` is what distinguishes a mouse report from a colour change. Every report was therefore garbage to the application: btop, which asks for `?1002;1006`, ignored all clicks, wheel turns and selections. `tests/mouse.cpp` and `kai-term-input-pty` both passed the whole time because both were written from the same wrong idea as the encoder — a byte-level test only proves the encoder is self-consistent. `kai-term-mouse-app` is what closes that gap: it runs btop and checks that the application's own state moved. When a protocol has an implementation available, assert against the implementation, not against your reading of it.
- A selection is kept in display coordinates and slid by however far the text underneath it moved, measured as the change in `viewTop()` (`historySize() - scroll_offset_`) across an output burst or a scroll. One helper, `keepSelectionOnScreen()`, serves both, because output and scrolling are the same thing to a selection. It is dropped when any part of it leaves the window: a highlight pointing at text nobody selected is worse than one that disappears.
- Ctrl+Shift+C and Ctrl+Shift+V are matched before any other key handling and return early, so Ctrl+Shift+C cannot fall through to `encodeKey()` and send Ctrl+C, which would interrupt whatever is running. The same early handling means a copy does not clear the selection it is about to read.
- Finishing a drag does nothing to any clipboard: `copySelection()` runs only on Ctrl+Shift+C (and on `OSC 52`, when a program inside the terminal asks for it). This is the behaviour a person asked for after using it — first auto-copy on release, then PRIMARY-only on release, and neither was right. Selecting is not copying, and the highlight staying put is what tells you what Ctrl+Shift+C is about to take.
- Word characters for double-click are ASCII letters, digits and underscore, matching shell word movement. Punctuation and non-ASCII glyphs select one cell at a time.
- `tests/mouse_app.cpp` aims clicks by walking cells, not by searching a row's text for a byte offset: btop draws box-drawing characters, so a byte offset is several columns past the character it was meant to hit, and a click that lands in the wrong box looks exactly like the application ignoring the mouse. The same trap applies to finding the scroll counter, which is searched from the bottom of the screen up because `/` also appears in the memory gauges and in the paths in the process list.
- `pasteText()` on this machine's `/bin/sh` (bash 5.3) does not execute what it pastes, because bash asks for bracketed paste (`?2004`) and the paste is correctly fenced. Tests that need a command to run type it with key events and press Return instead. This looks like a broken paste and is not one.
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

Result: build succeeded, no compiler warnings. `-Wall -Wextra` are on for every
target; Qt's own headers trip one diagnostic that is suppressed where it is set.

```sh
ctest --test-dir build --output-on-failure
```

Result: `100% tests passed, 0 tests failed out of 20` (51 s; `widget-smoke` and
`input-pty` are the slow ones, because both wait on real timeouts, `process` waits on
real signals, and `session`, `widget-session` and `app-window` wait on real shells).

The test binaries live in `build/tests/`; `build/` itself holds `kai-term` and nothing
else, because a person who has just built the terminal should not have to work out which
of twenty programs is the terminal.

Individual results:

- `./build/tests/kai-term-parser` → `PASS`.
- `./build/tests/kai-term-pty` → `PASS: PTY output contains HELLO and resize 6 40`.
- `./build/tests/kai-term-tmux-e2e` → `PASS: tmux E2E matches tmux ground truth at all stages`.
- `./build/tests/kai-term-input` → `PASS: 66 key encodings match the expected PTY bytes`.
- `./build/tests/kai-term-input-pty` → `PASS: 47 input sequences round-tripped through a real PTY unchanged` (35 keys, 12 mouse reports in classic, urxvt, SGR and hex-SGR forms).
- `./build/tests/kai-term-scrollback` → `PASS: 86 scrollback, cursor-mode and mouse-mode checks passed`.
- `./build/tests/kai-term-scrollbar` → `PASS: 25 scroll indicator geometry checks passed`.
- `./build/tests/kai-term-process` → `PASS: 37 process lifecycle checks passed`.
- `./build/tests/kai-term-mouse` → `PASS: 40 mouse encodings match the expected PTY bytes`.
- `./build/tests/kai-term-selection` → `PASS: 119 selection and text-extraction checks passed`.
- `./build/tests/kai-term-mouse-app` → `PASS: 8 mouse checks passed against a running btop (no visual inspection performed)`. With btop absent it prints that the checks were not exercised; with the pre-fix encoder it fails, which is the point of it.
- `./build/tests/kai-term-widget-smoke` → `PASS: 112 offscreen widget checks passed (no visual inspection performed) [this platform has no separate X11-style selection: only the clipboard copy was exercised]`. The bracketed part is the test reporting what it could not run.
- `./build/tests/kai-term-ipc` → `PASS: 43 protocol framing and field checks passed`.
- `./build/tests/kai-term-sockets` → `PASS: 44 unix socket checks passed`.
- `./build/tests/kai-term-snapshot` → `PASS: 64 screen snapshot checks passed`.
- `./build/tests/kai-term-session` → `PASS: 109 session daemon checks passed`.
- `./build/tests/kai-term-widget-session` → `PASS: 41 window-showing-a-session checks passed`.
- `./build/tests/kai-term-widget-panes` → `PASS: 24 pane-scoped selection checks passed`.
- `./build/tests/kai-term-app-window` → `PASS: 43 application window checks passed`.
- `./build/tests/kai-term-close-prompt` → `PASS: 24 close prompt checks passed`.
- `./tests/version.sh ./build/kai-term` → `Kai-Term 0.0.0`.
- `KAI_TERM_TEST_NO_WINDOW=1 ./build/kai-term --help` → usage text printed; no Qt window opened.
- `QT_QPA_PLATFORM=offscreen KAI_SHELL=/bin/true ./build/kai-term` → exits 0.

What the offscreen widget test actually demonstrates: the widget builds, a typed
command reaches the shell and its output is rendered, `seq 1 60` creates
scrollback, one wheel notch moves the offset three rows, painting differs while
scrolled back and returns to identical pixels at the bottom, Shift+PageUp /
Shift+Home / Shift+End move the offset as intended, and typing resets it.

One trap worth recording, because it made three checks pass that proved nothing. A test
that pastes `echo MARKER\n` into the widget and then looks for `MARKER` on the screen is
looking at the *command line*, not at the command's output: bash and fish turn on
bracketed paste, and a shell with it on takes a pasted newline as text inside the paste
and leaves the command sitting at the prompt. That is correct terminal behaviour, and it
means a paste is not a run. The widget tests now press Return themselves and look for
something the command line does not contain — `M=FIRST; echo ${M}-WINDOW` prints
`FIRST-WINDOW`, which cannot be found in what was typed.

The related one, which showed up as a check that failed only when the machine was busy:
`type()` on a session queues bytes, and the terminal echoes them the instant they arrive.
The command's *output* appears when the shell is scheduled and does the work. So a test
that types a line and then immediately looks at the screen can see the command line and
not the result, and a window joining a session at that moment is legitimately shown a
screen without it. Six copies of the widget test at once made it fail about a third of the
time. Tests now wait for the output before concluding anything about who is shown what.

It also demonstrates the input work: a click selects nothing, a five-cell drag
selects five characters and copies them, a double click takes the word, a triple
click the line, a drag while scrolled back copies the retained line, painting
changes while a selection is held, and what a program inside the terminal receives
for a press, a drag, a release, a wheel turn and a paste is exactly the encoded
bytes — read back from the terminal's own tty through `cat`. With reporting on, a
press goes to the program instead of selecting; a paste is still a paste, and with
`?2004` set it arrives fenced.

## Known issues

- **The close prompt never appeared at a real desktop, and here is why** — fixed, and the
  reason is worth keeping in the file. `main.cpp` built the window as a `QMainWindow` with
  the terminal inside it as the central widget. A close is delivered to the top-level
  widget and to nothing below it, so `AppWindow::closeEvent` — the code that asks before
  throwing a running program away — was never called by anything, on any platform. The
  window closed, `app.exec()` returned, `term->shutdown()` ran, and the unkept shell went
  with it, which is why `session list` showed nothing afterwards.
  Two of my own readings were wrong along the way and are recorded so they are not
  repeated. "The shell died, therefore a close was handled and said `discard=1`" was bad
  reasoning: `shutdown()` says the same word, and it was the one speaking. And the
  offscreen check that "closing the window asks" called `close()` on the terminal widget
  directly, which is not how anything closes a window — it passed the whole time the bug
  was there. What found it was running the real binary under a real compositor (sway with
  no display, asked to close the window the way a desktop does) and seeing the process exit
  with status 0 and no trace line at all, next to a plain Qt window that did get the close.
  Fixed by making the terminal the window. `tests/wayland_close.sh` closes the real binary
  under sway and fails when the wrapper goes back; it runs its own runtime directory, so it
  starts its own daemon and touches neither the developer's daemon nor their sway config —
  which matters, because sway reading a developer's config starts whatever they have it
  start, and one of these runs began downloading Minecraft.
  Also found in the daemon's listing while debugging: eleven sessions left behind from
  those silent closes, several with `btop` still in the foreground. A close that is never
  handled does not destroy anything, which is the correct rule and a visible mess.

- **The close prompt has been seen by a person now.** Kaida ran btop on niri, closed the
  window, and read the question — the first dialog in this project that a human being has
  looked at. The same try found the next bug and it was a real one: re-attaching to the
  session and closing again went silently, because the window counted a kept session as
  nothing-to-lose. Kaida's rule, which is the one implemented: attaching to a session does
  not make it kept, and closing a window with something running always asks. Being kept
  changes the buttons, not whether it asks.
- **Parts of M3 and M4 have still not been looked at by a person.** The keep-this-shell
  keybind and the close question have been used at a live Wayland session (niri), and the
  dialog's current shape — one line, Yes / No / Save Session, letters answering, No
  focused — came out of that use. What the session list looks like with four sessions in
  it and the daemon a bit slow, what the switch question feels like mid-build, and what
  the margin rule does at a live compositor's rounded corners are unverified; the dialogs
  and both sides of the margin are photographed offscreen, which is a picture of a
  decision, not a display. The daemon, the command line and the window showing a session
  are tested with no display.
- The session list is built when Ctrl+Shift+A is pressed and drawn inside the terminal,
  so it is correct at the moment it opens and wrong afterwards; nothing updates a list
  that is already on the screen. Nobody has tried holding it open while a session ends.
- Attaching transfers the visible screen and the scrollback above it, including the
  primary screen and history behind an alternate-screen program. The snapshot sends
  the last 2000 history rows.
- One view's size is the session's size and the last writer wins. Two windows of
  different heights attached to one session will fight over it; a multiplexer has to
  decide about that rather than leave it to whoever resized last.
- Writes to views are blocking and serialised under one lock, so a client that stops
  reading can stall a session's output. A person's window will not do this; a script that
  attaches and stops reading can.
- An `OSC 52` read is answered by the view and sent back down the byte stream, so it
  works for a session the daemon owns as well as a shell started here. Nobody has tried
  it against a real `scp` or vim over ssh; what is tested is the daemon forwarding both
  directions.
- Two views of one session both answer a clipboard read, so the program gets two replies
  and keeps the first. Same class as the size question below: nothing decides which view
  speaks for a session. The reply also arrives back as a clipboard *write* for every view,
  which is the same text and so harmless, but it is the model treating the answer as
  though a program had offered it.
- Nobody has visually inspected the rendered terminal on X11, and this machine
  cannot: no X server, no `Xvfb`, and the live Wayland compositor is not reachable
  from a shell session.
- What a person has confirmed at a live Wayland session (niri, 2026-11-17): the selection reads as a selection, selected text pastes into other applications, and the general appearance is fine. That session found the btop bug, which is fixed and now covered by `kai-term-mouse-app`.
- Interactive applications other than btop (vim, less, htop, tmux) have still not been driven with the mouse by anyone, and X11 specifically has not been checked.
- Scrolling during an edge auto-scroll drag takes the text with it: a downward drag
  loses whatever leaves the top of the window. Every terminal makes this trade.
- On Wayland, cross-application middle-click paste of a *fresh* selection depends on
  the compositor supporting primary selection, because nothing copies automatically
  any more. Ctrl+Shift+C is the explicit copy and always works.
- A selection is kept in *display* coordinates and shifted to follow its text, so a selection whose lines have been dropped by the 1000-line scrollback cap is cleared rather than copied from memory. Anchoring to absolute buffer lines would remove that limit.
- Cells store a resolved RGB colour, not a palette index, so `OSC 4` and `OSC 10/11`
  recolour what is written from then on and leave what is already on screen alone.
- `OSC 8` hyperlinks are parsed and discarded: the text renders and cannot be followed.
- Cursor blink is accepted and ignored, because nothing animates yet: DECSCUSR's
  blinking variants select their steady shape, and SGR 5 and 6 set a flag no timer
  reads.
- `?1005` UTF-8 mouse coordinates are handled: the classic `ESC [ M` framing with
  each coordinate written as UTF-8 of its value plus 32, so a click in a window 300
  columns wide arrives intact instead of clamping at 223. SGR (`?1006`) still outranks
  it and `?1016` outranks both, which is the order applications actually see today.
- A one-sided scroll region (`ESC[3r`), a bottom bound past the last row and an
  inverted region behave the way xterm does — checked by hand rather than assumed,
  because an audit report claimed otherwise and was wrong: it expected scrolling
  *outside* the margins.
- Shift+PageUp/Home/End are claimed by the terminal, so they are not available to applications; a configurable binding is needed before this is final.
- Unicode width handling is basic; combining characters and unusual emoji sequences may need refinement.
- No session daemon, IPC, multiplexer, or file manager exists yet.
- No TOML configuration parser is selected.
- No GitHub Actions workflow has been committed.
- Writing to the PTY master after the shell exits still succeeds on Linux: the pair
  stays wired while the master is open, and only a read reports the end of the
  stream. `Pty::write()` therefore says nothing about whether a shell is listening;
  the reader thread reaps and reports the exit instead of waiting for a write to
  fail.

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
- `src/terminal/base64.hpp` — decoding and encoding for `OSC 52`, with no toolkit in it.
- `src/terminal/terminalwidget.hpp` — Qt terminal widget and scrollback view.
- `src/process/pty.hpp` — PTY and shell lifecycle.
- `src/ipc/protocol.hpp`, `src/ipc/socket.hpp` — frames, fields, and the Unix socket.
- `src/session/snapshot.hpp`, `daemon.hpp`, `sessionbackend.hpp`, `cli.hpp` — the session daemon, screen snapshots, the remote backend, and the `session` verbs.
- `src/terminal/backend.hpp` — the seam between the widget and whatever is on the other end.
- `tests/parser.cpp`, `tests/pty.cpp`, `tests/tmux_e2e.cpp`, `tests/input.cpp`,
  `tests/input_pty.cpp`, `tests/scrollback.cpp`, `tests/mouse.cpp`,
  `tests/selection.cpp`, `tests/scrollbar.cpp`, `tests/process.cpp`,
  `tests/mouse_app.cpp`, `tests/widget_smoke.cpp`, `tests/ipc.cpp`,
  `tests/sockets.cpp`, `tests/snapshot.cpp`, `tests/session.cpp`,
  `tests/widget_session.cpp`, `tests/widget_panes.cpp`, `tests/version.sh` — the test
  binaries behind the results above.
- `.gitignore` — ignores build artifacts, generated Qt code, editor state and scratch.

## A requirement carried into M4

Pane-scoped selection is done in the terminal: `TerminalWidget::setPanes()` takes the
cells each pane owns and a drag stops at the edge of the one it started in, including the
highlight and a three-click line. What is left for the multiplexer is to **call it** — a
split that does not describe its panes gets a terminal that selects across them, exactly
as it did before the feature existed, so the requirement is met only if M4 passes the
rectangles. The clipping itself is tested (`kai-term-widget-panes`); the wiring is not,
because there is nothing to wire it to yet.

Still deliberately **not** done for applications like tmux that draw their own splits
inside a single screen: the terminal has no way to know where those panes are, and
inferring them from box-drawing characters would be wrong often enough to be worse than
useless. Agreed with Kaida on 2026-11-17.

## Next concrete tasks

1. Launch the real GUI in a Wayland and an X11 session and have a person confirm
   rendering, keyboard feel, scrollback scrolling and resize. Offscreen checks cannot
   substitute for this, and the X11 half needs a machine with an X server on it.
2. Have a person run the whole M4 workflow and say whether it reads well: open a window,
   start something long, Ctrl+Shift+S, close the window, reopen, Ctrl+Shift+A, click it
   back. What to ask about most: whether the menu's names mean anything, whether the
   status line is noticed, and whether the close prompt's three buttons say what they do.
3. Decide whether Shift+PageUp and friends stay terminal-owned or become configurable
   bindings, and whether Ctrl+Shift+S/A are the right keys once a config file exists.
4. Run vim, less, htop and tmux at a real session with the mouse: confirm reports reach
   them, that Shift still lets you copy, and that middle click pastes between
   applications. btop is already covered by `kai-term-mouse-app`; the others are not.
5. Decide the two policy questions the daemon leaves open: what happens to a session's
   size when a second view of a different size attaches, and what a wedged client that
   stops reading should cost the session.
6. Transfer scrollback on attach — done, including the primary buffer behind an
   alternate-screen program.
7. The command surface agreed with Kaida on 2026-11-17, written out in ROADMAP.md:
   session ids as the default handle, `kai-term list` / `attach 3` / `detach` / `save`
   with no `session` word, `KAI_SESSION` so a command knows where it is running, and a
   status line in the attached view. The `session` verb and `open` go away rather than
   staying as aliases.
8. Splits, pane focus and pane resize, calling `setPanes()` with the cells each pane owns
   so the pane-scoped selection is wired rather than merely available.
8. A CI workflow would catch the class of failure this machine only finds by hand: a
   build that warns, a test that hangs.

Do not claim the GUI is verified until a person has run it and looked at it.
