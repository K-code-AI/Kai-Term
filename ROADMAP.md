# Kai-Term Roadmap

Status vocabulary:

- **done** — implemented and verified.
- **in progress** — implemented, but runtime verification is incomplete.
- **pending** — not implemented.
- **blocked** — requires a dependency or interface decision.

## M0 — Research and architecture

Status: **done**.

- New project directory; old Kai-Term project deliberately not reused.
- Architecture document.
- Dependency report with verified Qt and plugin status.
- Initial CMake project.
- Development plan and handoff document.
- Version smoke test.

## M1 — Basic terminal

Status: **done; runtime verified offscreen and at a live Wayland session, on-screen
appearance not formally inspected**.

Implemented:

- Native terminal emulator; no `libvterm`.
- Application window and Qt terminal widget.
- PTY creation with `forkpty()`.
- Shell execution with configurable shell.
- Keyboard input and terminal output, with the byte encoding isolated in
  `src/terminal/keyencode.hpp` and checked both as pure bytes and through a real
  PTY.
- Basic resizing through `TIOCSWINSZ`.
- Scrollback buffer and a scrollback view: mouse wheel, Shift+PageUp,
  Shift+PageDown, Shift+Home, Shift+End, and an indicator along the right edge that
  shows how much history there is and where the window sits in it.
- Reflow. Retained lines are stored logically and re-wrapped when the width
  changes; the cursor keeps its window row and the line under it survives.
- Shell lifecycle. The session is signalled rather than the process group, so
  background jobs an exited shell left behind go with it; a job that traps HUP and
  TERM is killed after a short grace period; exit code and signal are reported; a
  shell named explicitly and found unusable says so instead of falling back.
- DECCKM application cursor keys for vim, less and fish Vi mode.
- Native VT parser for colors, cursor movement, erasing, and Unicode.
- Escape-sequence coverage beyond the basics: OSC 0/1/2 titles with the `ESC[22 t`
  and `ESC[23 t` stack, OSC 7 working directory, OSC 52 clipboard set and query,
  OSC 10/11 and OSC 4/104 colours, XTWINOPS size and title queries, the kitty
  keyboard query answered, SGR sub-parameters in both spellings, `?1005` UTF-8 mouse
  coordinates, `?2026` synchronized output paced to one repaint per 100 ms inside a
  block, the whole SGR attribute table including overline drawn as its own line, and
  DECSCUSR
  (`ESC[<n> q`) cursor shapes — block, underline, vertical bar — all three drawn and
  asserted offscreen by comparing the picture of the cell the cursor stands on.
- Headless parser, PTY, tmux ground-truth, input, PTY-input, scrollback, scroll
  indicator, process lifecycle and offscreen widget tests.

Not yet verified:

- How the rendered terminal looks to a person on a live X11 session. There is no X
  server, and no `Xvfb`, on the machine this is developed on, and the Wayland
  compositor here is not reachable from a shell session, so the X11 code path is
  reasoned about and not run.
- Full shell compatibility across interactive applications.

Known limits, deliberately left:

- Cells hold a resolved colour, not a palette index, so an OSC 4 or OSC 10 change
  affects what is drawn afterwards and not what is already on screen.
- OSC 8 hyperlinks are consumed, not stored: the text renders and cannot be
  followed.
- Cursor blink is not animated: DECSCUSR's blinking variants select their steady shape, and SGR 5 and 6 set a flag nothing reads yet.
- `?2026` is repaint pacing, not an off-screen frame: inside a synchronized block the
  widget paints at most once every 100 ms, which is what removes the half-drawn
  frames, but a repaint still shows whatever had arrived at that moment rather than a
  complete frame assembled somewhere else.

## M2 — Terminal reliability

Status: **done; selection, clipboard, paste and mouse reporting verified headlessly,
offscreen, and against a live session; interactive applications other than btop have
not been driven with the mouse**.

Implemented:

- Mouse text selection: drag, double-click for a word, triple-click for a line,
  Shift+click to move the end of a selection, and selecting retained lines while
  scrolled up. Shift takes the mouse back from an application that grabbed it. The
  highlight follows its text as output arrives or the view scrolls, and is dropped
  once it leaves the window.
- Copy on demand with Ctrl+Shift+C, paste with Ctrl+Shift+V or middle click.
  A finished drag copies nothing anywhere. Newlines in
  a paste become carriage returns, and the payload is fenced with `ESC[200~` /
  `ESC[201~` when the application asked for bracketed paste.
- Mouse reporting: click, drag and any-event tracking, wheel reports, modifier
  bits, and classic, urxvt, SGR and hexadecimal SGR coordinates, isolated in
  `src/terminal/mouseencode.hpp` as a pure function.
- Screen, row and selection text extraction as pure functions
  (`src/terminal/text.hpp`), including double-width glyphs and rows shorter than
  the screen.
- The mode bits those features read (`?1000`, `?1002`, `?1003`, `?1006`, `?1015`,
  `?1016`, `?2004`) are tracked and cleared on reset.
- SGR and hexadecimal SGR reports written the way xterm reads them:
  `CSI < button ; column ; row M`. They were previously written with the
  coordinates first and without the leading `<`, which made every report
  unreadable to the application — btop ignored all clicks, wheel turns and
  selections because of it.
- New tests: `kai-term-mouse` (exact report bytes), `kai-term-selection` (selection
  geometry and text), a mouse phase in `kai-term-input-pty` (reports replayed
  through a real PTY), mode-state checks in `kai-term-scrollback`, selection, paste
  and reporting checks in `kai-term-widget-smoke`, where the reports are read back
  from the terminal's own tty with `cat`, and `kai-term-mouse-app`, which runs btop
  inside the widget and checks that its own scroll counter moves.
- New since the milestone closed: `kai-term-scrollbar` (the indicator's geometry and
  the view bounds it reads) and `kai-term-process` (exit status, signals, jobs left
  behind by an exited shell, an unusable shell). `ctest` runs all twelve.

Confirmed at a live Wayland session (2026-11-17): the selection reads as a
selection, selected text pastes into other applications, and general appearance is
acceptable. That session is also where the btop bug turned up.

Not yet verified:

- Colours, fonts and spacing on a real display.
- Interactive applications other than btop with the mouse grabbed (vim, htop, less,
  tmux).
- Clipboard and PRIMARY behaviour on X11 specifically; what was confirmed was
  Wayland.

Needed:

- X11 runtime verification, which needs a machine with an X server on it.
- Interactive applications driven with the mouse by a person: vim, less, htop, tmux.
- Cursor blink: a timer for SGR 5/6 and for DECSCUSR's blinking variants.

## M3 — Native session infrastructure

Status: **pending**.

Required:

- **Pane-scoped selection.** When Kai-Term runs its own multiplexer, a selection
  must be clipped to the pane it started in, so dragging in one pane never picks up
  text from a neighbouring one. The terminal knows its own pane rectangles, so this
  is exact rather than a guess. It is deliberately not attempted for applications
  such as tmux that draw their own splits inside one screen: the terminal cannot
  know where those panes are without a protocol, and inferring them from
  box-drawing characters would be wrong often enough to be worse than not trying.
  Agreed with Kaida on 2026-11-17.

Needed:

- Session daemon.
- Unix socket IPC.
- PTY ownership by daemon.
- Session create/list/attach/detach.
- Process lifecycle.

## M4 — Native multiplexer

Status: **pending**.

Needed:

- Horizontal and vertical splits.
- Pane selection and resizing.
- Multiple windows and tabs.
- Session switching.

## M5 — File manager

Status: **pending**.

Needed:

- Single pane.
- Navigation, sorting, hidden files.
- Mouse support.
- Associated terminal.
- Basic file operations.

## M6 — Advanced file manager

Status: **pending**.

Needed:

- Dual panes.
- Copy/cut/paste and drag-and-drop.
- Image and text previews.
- File editing.
- Bookmarks and advanced navigation.

## M7 — Integration

Status: **pending**.

Needed:

- Integrate terminal, multiplexer, sessions, file manager, config, and keybindings.
- Test lifecycle combinations.

## M8 — Optimization and release

Status: **pending**.

Needed:

- Resource and error handling.
- Crash handling and logging.
- Configuration improvements.
- Documentation.
- Packaging and X11/Wayland verification.
