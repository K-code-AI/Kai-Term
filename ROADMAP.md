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
  reasoned about and not run. What was checked rather than assumed: the Qt plugins
  installed here are `linuxfb`, `minimal`, `offscreen`, `vkkhrdisplay`, `vnc`,
  `wayland` and `xcb`; the only server binary is `Xwayland`, which needs a compositor
  to run under and there is none installed (`weston`, `cage`), no `DISPLAY` or
  `WAYLAND_DISPLAY` in a shell session, no `/dev/fb0` for `linuxfb`, and no
  passwordless `sudo` to install any of it. The `vnc` plugin does start — the app
  comes up with `QVncServer created on port 5911` and a listening socket — so a real
  raster platform runs the widget here, but it is not the X11 path and its
  framebuffer would not yield to a hand-written RFB 3.3 client.
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

Status: **complete, headless**. Built, tested with no display, and not yet looked at by
a person: what a window showing a session looks like is still unverified.

A session is a pty and a screen model owned by a process of their own, so a window is a
view of a session rather than the thing keeping it alive.

- Session daemon: `kai-term session serve`, in the foreground for whoever is watching it
  in a terminal. `kai-term` is the only program built — the daemon is the same binary
  started a different way, which matters because a separate daemon binary is a second
  thing to install, version and get wrong. One daemon per user, listening on
  `$XDG_RUNTIME_DIR/kai-term/daemon.sock` (directory 0700, socket 0600, and the peer's
  uid checked on every connection). Every missing level of that path is created, because
  pointing `$XDG_RUNTIME_DIR` somewhere of your own is how sessions end up somewhere
  specific.
- Unix socket IPC: `src/ipc/protocol.hpp`, a framed codec with escaped fields — length,
  kind, body — and `src/ipc/socket.hpp` for binding, connecting, stale-socket removal
  and peer credentials. No JSON, no external dependency; the payload is a handful of
  `key=value` pairs and records separated by `0x1e`.
- PTY ownership by the daemon: the daemon's reader thread per session feeds its own
  `Screen`, answers the queries programs ask (device attributes, cursor position), turns
  an OSC 52 copy request into a frame for a view to carry, and reaps the shell. A program
  that gets no answer waits — fish spends ten seconds on the device attributes query
  before announcing that its terminal is broken.
- Session create/list/attach/detach: `kai-term session new|list|attach|detach|kill|send`
  and `kai-term open NAME` for a window. The first `new` starts the daemon. Attaching
  later is shown what is on the screen now, from a snapshot of the daemon's model rather
  than a replay of raw bytes, because replaying bytes at a different column count lays
  them out wrong.
- Process lifecycle: a view hanging up disturbs nothing; `detach` sends the views away
  and leaves the shell; `kill` and the shell exiting end it; `shutdown` stops the daemon
  and every session with it, leaving no socket behind. A descriptor is never closed under
  a thread blocked reading it, and waking one takes `SHUT_RDWR` — `SHUT_WR` only tells
  the program on the far end.
- Backend abstraction: `src/terminal/backend.hpp` is what the widget talks to — a
  descriptor, bytes in, bytes out, a size — with `PtyBackend` for a shell started here
  and `SessionBackend` for a session across the socket. Two differences are part of the
  interface rather than hidden in it: a pty has a child to notice leaving, and a pty is
  the terminal a program is asking questions of, so a view of a remote session answers
  nothing.
- Pane-scoped selection, the requirement carried into this milestone: the widget can be
  told where its panes are, a drag belongs to the pane its first press landed in, and the
  middle rows of a drag, the highlight and a three-click line all stop at that pane's
  edge. No panes means one pane, the screen; a press on a divider selects unclipped
  rather than not at all. Still deliberately not attempted for applications that draw
  their own splits inside one screen (tmux), which the terminal cannot know about without
  a protocol. Agreed with Kaida on 2026-11-17.
- New tests: `kai-term-ipc` (framing and field escaping), `kai-term-sockets` (paths,
  binding, stale sockets, peer credentials, directories that have to be created),
  `kai-term-snapshot` (a screen snapshot fed back into a fresh model reproduces it cell
  for cell), `kai-term-session` (96 checks over a real pty and a real socket),
  `kai-term-widget-session` (a window showing a session: what was already printed,
  typing, size, closing the window, a second window, being pointed at another shell),
  `kai-term-widget-panes` (real mouse drags across a pane boundary).

Not yet verified:

- Anything a person has looked at. Every check here is offscreen or headless; what an
  attached window looks like, and what a session feels like to use, is for a person to
  say.
- X11, as before: no X server on this machine.

Needed:

- A person to run `kai-term session new`, `kai-term open NAME` and `session attach` and
  say whether it reads well.
- Scrollback is not transferred on attach, only the visible screen. A session that has
  been running since before the window existed shows the last screenful and the scroll
  bar says there is nothing above it.
- One view's size is the session's size, last writer wins. Two windows of different
  heights attached to one session will fight, which a multiplexer has to decide about
  rather than leave to whichever resized last.
- Writes to views are blocking and serialised, so a client that stops reading can stall
  a session's output. A person's window is unlikely to do that; a script that attaches
  and stops reading can.
- The daemon cannot answer an OSC 52 *read* (a program asking for the clipboard): the
  daemon has no clipboard, and only a view does. A view answers a copy request from its
  own clipboard but not a read query.

## M4 — Native multiplexer

Status: **in progress, headless**. No part of it has been looked at by a person.

Done:

- Every window's shell runs under the session daemon, which is started for the window if
  nothing is running. Nothing has to be launched before the terminal works; a window that
  cannot reach a daemon says so once and runs a shell of its own.
- **Ctrl+Shift+S** keeps the shell and starts a fresh one in the same window. The window
  stays, because what ended was the shell.
- **Ctrl+Shift+A** lists the kept sessions with what is happening in each and brings the
  one you click back into this window.
- Closing a window with something running asks — Yes, No, or Detach session before
  closing terminal — and names the program, from the process group the terminal owns.
  Nothing is asked about a shell at a prompt, or about a session that was kept, because
  closing a window does not stop either.
- How often that happens is a setting, `KAI_CONFIRM_QUIT=never|running|always`, under the
  same three words Old-Term's `confirm_quit` takes. It is read from the environment
  because there is no config file yet; when there is one, that is where it goes.
- Kept and unkept sessions are different things, and losing a window by accident does not
  destroy an unkept one. See ARCHITECTURE.md.
- A window can be pointed at another shell while it stays open, which is what restoring
  means; pane clipping from M3 is in place and waiting for a host that draws splits.

### The command surface, agreed with Kaida on 2026-11-17

The `session` word goes away, sessions get numbers, and the words come straight after
`kai-term`. Agreed verb by verb rather than in one sweep, because the long form
(`kai-term session attach -s work`) is what made the whole thing feel like work.

```
kai-term                      a new window, as before
kai-term list                 kept sessions, and any shell a window lost; marks the one
                              you are standing in
kai-term new [NAME]           create a session, print its id, leave you where you were
kai-term attach [ID]          show a session in this terminal, with a status line on top
kai-term attach -n NAME       the same, by name — names may be sentences, quoted
kai-term attach ID --window   a Kai-Term window onto it instead
kai-term detach               leave the session you are in; takes no argument
kai-term send ID TEXT [--enter]
kai-term save [ID] [NAME]     keep a session, optionally naming it; no id = this one
kai-term kill ID              end a session that was kept
kai-term start | shutdown | serve      automatic now; kept for debugging and labelled so
```

Keys reserved while this is being built, so that a later feature does not quietly take
one that has become somebody else's: **Ctrl+Shift+F** file manager, **Ctrl+Shift+N**
new tab, **Ctrl+Shift+H** switch between side-by-side panes and a tab view.

The rules behind it:

- **A number is the handle.** The daemon assigns it, `list` shows it first, and it is what
  `attach` takes without a flag. `-n` / `--name` is how a name is used instead; there is
  no guessing from the shape of a word, and no `-s`.
- Numbers are not reused while anything is running. When the last session goes, counting
  starts again at 0. Nothing is written to disk, so ids belong to the daemon's life.
- The session a command is run from is known to itself (`KAI_SESSION` in its environment),
  which is what lets `detach` and `save` be argument-free and lets `list` mark where
  you are.
- **Attaching shows a status line above the session's screen** — which session, what is
  running in it, how to leave — because attaching into a session from inside one is
  otherwise invisible, and people lose afternoons in nested terminals that way.
- A shell a window is holding, unkept, is nobody's session: it is not listed and cannot be
  attached to. Once its window is gone it is a session in every sense — listed,
  attachable — because at that point it is the only copy of the work there is.
- `new` takes its shell and directory from the environment. Config file, when there is
  one, is where a difference from the system default belongs; not a flag on every verb.
- Gone: `session <verb>`, `open NAME`, `-s`, `--as`, `--shell`, `--cwd`, `--rows`,
  `--cols`, `--text`. Nothing is kept "for compatibility" — nothing is released, and a
  word that means two things is what we are undoing.

One thing that was asked for cannot be had: **a terminal cannot tell Ctrl+Shift+S from
Ctrl+S.** A letter with Ctrl produces one byte and Shift is not in it — which is also why
Ctrl+Shift+D could never be the leave key in an attached view, since what arrives there is
Ctrl+D, the shell's own end-of-file. So the letter is the same on both sides and the
Shift is only where it can be seen: the window uses Ctrl+Shift+S, because Qt really does
receive the two separately, and the attached view says `Ctrl+S to leave`, which is the
byte that actually arrives. Ctrl+Shift+D is retired rather than kept as an alias, and
plain Ctrl+S still reaches the shell from the window, where it means stop and start.

Needed:

- Horizontal and vertical splits, with `setPanes()` called with the cells each pane owns.
- Pane selection and resizing.
- Multiple windows and tabs.
- A scrollback that comes across with a session when a second view joins it.
- The command surface above: ids, the flattened verbs, the attached view's status line.
- One question for the whole window, not one per shell: Old-Term counts every session in
  the window that has something in its foreground group and names each of them, so a
  window with four panes asks once about all four. That wants the tabs and splits above
  to exist first — today a window holds exactly one shell, and asking about all of them
  and asking about this one are the same question.
- A config file, so that `confirm_quit` and the rest have somewhere to live that is not
  the environment. Old-Term reads `config.c`'s key=value file; matching its words is
  already half the work.

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
