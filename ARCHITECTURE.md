# Kai-Term Architecture

## Design goal

Kai-Term is one application with four cooperating subsystems:

1. Terminal emulation.
2. Native session daemon and IPC.
3. Multiplexer layout and pane management.
4. Dual-pane file manager.

The GUI is a client of the session daemon. It never owns shell process lifetimes in the final architecture.

## Current M1 implementation

At M1, the application owns the PTY directly:

```
Qt terminal widget
  -> native VT parser and screen model
  -> Pty / forkpty()
  -> shell process
```

The PTY is held in `Pty`, not inside the widget rendering code. M3 moves PTY ownership to the daemon.

## Terminal subsystem

- No `libvterm`. Escape sequences are interpreted by a native parser in `src/terminal/vtparser.cpp`.
- The screen model in `src/terminal/screen.hpp` stores cells, colors, attributes, cursor position, and scrollback.
- PTY creation uses `forkpty()`, which creates the slave side as the child's controlling terminal.
- Shell selection uses `KAI_SHELL`, then `SHELL`, then `/bin/sh`.
- The child shell gets `TERM=xterm-256color` so shell startup does not depend on an inherited `dumb` term.
- Resizing uses `TIOCSWINSZ` on the PTY master and forwards `SIGWINCH`.
- Output is read from the PTY master on a reader thread and posted to the Qt widget through `QCoreApplication::postEvent`.
- Keyboard input is sent to the PTY master from Qt `keyPressEvent`.

Three rules about the view are worth stating because they are the ones a
well-meaning change breaks:

- **The view's scroll capacity is one number**, `scrollRange()` in the widget, and the
  indicator, the wheel, Shift+PageUp, the thumb drag and the edge-scroll timer all ask it
  rather than the model. It is zero while an application holds the alternate screen.
  A program that asked for the whole window (vim, less, btop, tmux) is drawing a grid with
  nothing above it and keeps its own idea of its history; the rows the model is holding
  belong to the shell underneath, so offering them would show a full-screen program a
  screenful of somebody else's text with a bar inviting the person to go and look. The
  model already discards alternate-screen rows instead of retaining them, so the shell's
  history comes back intact when the program gives the screen up.
- **A cell stores what it looks like, not a palette index.** `OSC 4` and `OSC 10/11`
  recolour what is written from then on and leave what is already on screen alone, which
  is what a program expects when it changes the background and repaints nothing.
- **The margin around the grid belongs to the screen's owner.** It exists to keep a
  shell's glyphs out of the rounded corners of a borderless window, and it is the
  terminal's own background — so while an application holds the alternate screen it goes
  to zero and the grid grows into the freed room, because a full-screen program paints
  its own frame and two backgrounds in one window clash. `activePadding()` is the one
  question, and the painter, the hit-tester and the grid geometry all ask it.
- **A restored screen is fed to the model in one piece.** A session's snapshot leaves the
  alternate screen to repaint the primary buffer and re-enters it at the end, and the
  margin rule above means a window that momentarily saw the primary buffer would resize
  itself — which makes the session resize and answer with another snapshot, a loop that
  reflows the whole scrollback every turn. So the reader thread takes the entire run the
  backend is holding and posts it as a single event; the transient never reaches the
  geometry. This is why a full-screen program used to hang only when there was scrollback:
  a snapshot with nothing behind it is small enough to land in one read, and a large one
  is not. See `tests/snapshot_resize.cpp`.

Still open: Unicode width handling is basic and may need refinement for combining
characters and unusual emoji sequences, and `OSC 8` hyperlinks are parsed and discarded.

## Session daemon and IPC

One daemon per user, keeping sessions alive whether or not anybody is looking:

```
$XDG_RUNTIME_DIR/kai-term/daemon-v3.sock
```

The directory is 0700 and the socket 0600, and every connection is checked with
`SO_PEERCRED` — the socket is a filesystem file, not an abstract one, so `ls` and
`socat` work on it, which means it cannot trust its name alone. Every missing level of
the path is created, since pointing `$XDG_RUNTIME_DIR` at a path of your own is how
sessions end up somewhere specific and a daemon that cannot bind leaves nothing but a
missing socket as the clue.

The protocol is frames, not JSON (`src/ipc/protocol.hpp`):

```
<len:4><kind:1><body>
```

Kinds are `Hello`, `Data`, `Resize`, `Detach`, `Kill`, `List`, `Welcome`, `Snapshot`,
`Exit`, `Error`, `SessionList`, `Ack`, `Clipboard`. Bodies that carry more than one thing
are `key=value` pairs with `\\`, `\n`, `\r` and `=` escaped, and lists of records
separated by `0x1e`. A version number and a request id were planned; neither earned its
place — a mismatch is an `Error` frame either way, and one daemon per user means requests
are answered in order on the connection that sent them.

The daemon owns the PTY masters, the shell process lifetimes, and a `Screen` per session.
Owning the model, rather than only the bytes, is what lets a view that attaches later be
shown what is on the screen now: the daemon reflows its own model to the new size and
sends an escape-sequence snapshot. Replaying raw bytes at a different column count lays
them out wrong, and a session's history is not a recording of a screen.

A snapshot carries the **modes as well as the grid**, and that distinction was worth a
sitting of bug reports. The glyphs alone arrive as a picture of a terminal rather than
one: the mouse stays with the window's selection instead of the application that asked
for it, a program on the alternate screen leaves lines it never drew visible around its
frame, arrow keys go out as plain CSI to an application reading SS3, and a paste arrives
unquoted to an application that asked for it quoted. So the snapshot says which of
1049/1000/1002/1003/1005/1006/1015/1016/?1/?6/?7/2004 the session is in, plus the scroll
region. The order is not free, and getting it wrong is a bug of its own. Origin mode
changes what a cursor address *means*, and a scroll region is the box every cursor address
is clamped inside — so both are **cleared** before the glyphs and restored after the last
one is placed. Replay a session's region beforehand and every address in the snapshot is
squeezed into it: a screen's worth of lines lands on a few rows, each overwriting the last,
and the window shows one character in the corner. A region outlives the program that set
it — vim, less, anything interrupted leaves one behind — so the region in the model need
not describe the screen being shown at all.

A snapshot carries both buffers. The primary screen and its history are painted first,
then the alternate screen is entered and painted, so a view that arrives while a program
holds the alternate screen has the primary screen and history to return to when the
program quits.

Because it owns the model, the daemon also answers the questions programs ask the
terminal — device attributes, cursor position — which it must: nobody answering is not a
silent failure, since fish waits ten seconds for a device attributes reply and then says
so out loud, and tmux will not start properly. The questions about the clipboard are the
ones it cannot answer, because a clipboard belongs to the machine with the window: a
program offering text (`OSC 52 ; … ; <base64>`) becomes a `Clipboard` frame for the views
to store, and a program *asking* for it (`OSC 52 ; … ; ?`, which is what vim and scp
actually send) becomes the same frame with `query=1`, which a view answers out of its own
clipboard. The answer goes back as a `Data` frame, because that is the way down to the
shell. Both directions matter: a view that cannot tell a read from a write answers a
request to read by emptying the clipboard.

Teardown rules, both learned the hard way:

- A descriptor is never closed under a thread blocked reading it. The session's reader
  thread owns the master and closes it last; a client socket belongs to the thread
  serving it.
- Waking that thread takes `shutdown(fd, SHUT_RDWR)`. `SHUT_WR` only tells the program on
  the far end that the session is gone; the thread blocked in `read()` on our side is
  woken by shutting down our own receive side.

### The protocol has a number, and the socket is named after it

`kProtocolVersion` goes into every hello, and the daemon's socket is
`daemon-v<N>.sock`. A client and a daemon that disagree about what a message means are
then never in a conversation at all, which is the only reliable way to handle it: an old
daemon asked to `save` answers `unknown command`, and — worse, because it looks like
success — it lists sessions without saying whether they were kept, so a new client hides
the shells it is being told about and a menu comes up empty. Both read as a terminal that
does not work, for a reason nobody can see.

A daemon already running is never stopped by this. It holds shells somebody started, and a
terminal that killed other people's sessions to tidy up after an upgrade would be worse
than the confusion it fixed. What it does instead is say once, in the window and in
`kai-term list`, that an older daemon is running, which protocol it speaks, which process
it is, and that its shells cannot be reached from this build.

### Kept and unkept

A session records whether a person has asked for it to be kept. A window's shell starts
unkept and named from a range nothing else uses (`w1`, `w2`), and the daemon is started
for it if nothing is running — needing a daemon started first would mean a terminal that
does not work until you have read about it.

What ends an unkept session is being told, not being left:

- A view that leaves while saying it is being closed (`Detach` with `discard=1`) ends it.
- A view that leaves without saying so — a crash, a `kill -9`, a machine that lost its
  temper — leaves it running, with no views, listed as left behind.
- `save` promotes one to kept, optionally renaming it; a name already in use is refused,
  because that name belongs to a shell somebody is watching. `discard` and `kill` end one.

A daemon holding people's shells is one they have good reasons never to restart, so its
resources are capped rather than hoped about: a bounded number of connection threads, a
timeout on connections that never introduce themselves, retired workers collected without
a thread ever joining itself — the retiring thread's own handle is detached, and a worker
does not reach into a slot table a shutdown has already swapped out — and a stop-itself
when no session is saved and no window is watching. And because it execs by
`/proc/self/exe`, it names itself `kai-term-daemon` with `prctl`: the kernel names a
process after the last part of the path it execs, and "exe" is the least informative word
a process holding every kept shell can introduce itself as.

A listing says which is which, and names what is running out front — the process group
the terminal reports owning, read from `/proc`, empty when the group is the shell itself.
That is what lets a window ask "Quit while `make` is running?" instead of guessing from
how long since you last typed, and it is asked of the daemon rather than of the window,
because the shell is not the window's child.

`kai-term list` shows kept sessions and the left-behind ones, and not the shells other
windows are looking at: those are somebody's typing, not a session you saved.

## Terminal window and its backends

The widget does not know what it is talking to (`src/terminal/backend.hpp`): a descriptor
to wait on, bytes in, bytes out, a size, and whether the far end is still there.
`PtyBackend` is a shell started here; `SessionBackend` is a session across the socket,
whose frames are turned back into bytes before the widget sees them.

Two differences are in the interface rather than hidden behind it, because both were got
wrong when they were hidden:

- A pty has a child, and only collecting it says the shell left; a socket has no child
  here, so its transport says so itself.
- A pty is the terminal a program is asking questions of. A view of a remote session is
  not, because the daemon already answered, and a second reply is a second answer for the
  program to choose the wrong one from.

The terminal widget **is the window**, and that is a rule rather than a detail. A close is
delivered to the top-level widget and to nothing below it, so a `closeEvent` written on a
widget that merely sits inside a `QMainWindow` is code that never runs: the terminal spent
a while wrapped as a central widget, and every window closed without the prompt its own
`closeEvent` was written to put up. A test that calls `close()` on the widget passes the
whole time. What catches it is `tests/wayland_close.sh`, which asks a compositor to close
the real binary.

Panes are the widget's business and its host's knowledge: `setPanes()` says which cells
belong to which pane, and a drag is clipped to the one it started in. A terminal with one
shell passes nothing and gets one pane, the screen.

A window can be pointed at another shell while it is open — restoring a kept session is
that and nothing more. `showSession()` joins the reader before the backend it reads from
is destroyed, tells the far end whether this window is closing or merely looking
elsewhere (which is what decides whether an unkept session outlives it), and empties the
model, because a window still showing the previous shell would be showing a session that
is not there. Events from the reader carry the number of the shell that produced them, so
bytes from the one behind cannot be painted onto the screen of the one in front; an event
queue does not empty itself just because the writer stopped.

The widget knows two key combinations belong to whoever owns it and never reach the
shell, and otherwise has no idea what a session is. It does, however, draw the dialogs
its host asks for: `askInTerminal()` and `chooseInTerminal()` put a question with a row
of buttons, or a walked list, over the grid in the terminal's own font on a panel of the
terminal's own background, and take the keyboard while they are up. A native dialog
arrives wearing the desktop's theme, font and button order, and reminds everybody that
the terminal is a rectangle the window manager decorates; these do not.
`src/app/appwindow.hpp` is where sessions and the two dialogs live, `src/app/sessionmenu.hpp`
is what a row of the list says, and the close prompt's *decision* is in
`src/app/closeprompt.hpp` with no Qt in it: what to ask is worth testing, what the dialog
looks like has to be looked at.

Settings live in key=value files — `~/.config/kai-term/kai-term.conf` for how the
program behaves and `~/.config/kai-term/theme.conf` for how it looks, `KAI_TERM_CONFIG`
and `KAI_TERM_THEME` to point either at somewhere else — because the words a person has
typed in a dozen other dotfiles beat a table syntax they will look up. The split is not
tidiness: it is what lets the picker save colours without ever rewriting the file that
holds somebody's other decisions, so the two writers never touch each other's file.
Parsing is `src/config/config.hpp`, no Qt, tested headless: comments and whitespace
free, colours `#rrggbb` and nothing as colourful as a name, numbers refused whole —
`14px` gets a complaint, not a silent read of the 14. Three rules underneath it. An
unknown key is announced with its file and line and the rest of the file still applies
— a setting skipped in silence is an afternoon of wondering, and losing your colours to
a typo'd key on another line is a worse afternoon than being told twice. The environment
beats the file everywhere they overlap: the environment is what you are holding in your
hand right now. And the theme file is read last, so where a colour somehow appears in
both, the file the picker writes is the more recent intention. `/etc/kai-term/` will
join the layering as the system defaults, read first, when there is an installer with
somewhere to put it; the names and the read order are chosen now so that layer is a
call and not a rethink.

The files reach the widget whole: `font_size`, `cols`, `rows` and `opacity` shape the
window's body before the first frame, and the colours reach it as a `Theme` — the five
window colours plus the sixteen ANSI palette entries — deciding the *next* screen, so
cells already written keep what they were written with, the same rule an escape-sequence
recolour follows.

**A colour the config file named is a lock, and a lock is the top authority.** It
outranks everything a session writes — `OSC 10/11` for the defaults, `OSC 4` for a palette
entry — and it holds on every window, including one attached to a running session. The
reason is concrete: fish, starship and prompt tools write `OSC 10/11` at startup, and a
session's snapshot carries its own colours, so a theme applied first was washed back to
defaults the moment the shell's opening bytes arrived — which is exactly what Kaida
reported ("it goes back to default colour when starting fresh… and even when attaching an
old session"). The saved theme supersedes the session because the person chose it and the
program did not. A colour the config left *unnamed* is not locked and stays the session's
to recolour, so a program that legitimately changes a colour is still heard; the built-in
table answers beneath both. The lock lives in the `Screen` (`lockDefaults`, `lockPalette`)
rather than the widget so the authority is one rule in the model, and it is deliberately
*not* touched by a program's colour writes — only by the config and the picker.

Choosing the colours without a text editor is the picker (Ctrl+Shift+P): the state
machine in `src/config/themepicker.hpp` has no Qt in it and is tested headless, the
widget paints what the model says and routes the keys, and the preview is the window
itself — every keystroke goes through the same `applyTheme` a file goes through, and moves
the same lock so a colour you are choosing is shown over the session rather than washed
out by it. Saving
writes back only the colours that actually moved, in place: a line being set keeps its
alignment and its trailing comment, comments and settings the picker has never heard of
are copied byte for byte, and the file is replaced through a temp file and a rename so
an interrupted save leaves the old one standing.

## Multiplexer subsystem

The multiplexer is a layout tree owned by session metadata:

- Windows and panes have stable IDs.
- Horizontal and vertical splits produce child panes.
- Each pane maps to an independent terminal session.
- Pane resize updates both GUI geometry and PTY dimensions.
- Tabs and session switching are metadata operations, not shell relaunches.
- Closing a pane closes or detaches its session according to an explicit policy.

GUI layout changes are separate from live PTY state. Layout metadata can be persisted; process state cannot be reconstructed from metadata after a daemon crash.

## File manager subsystem

Each file-manager pane has:

- Its own filesystem path.
- Its own associated shell session.
- Its own breadcrumb and navigation state.

The file manager's displayed directory is separate from the shell's actual working directory. Directory synchronization is an explicit feature, not an assumption.

File operations use POSIX filesystem APIs and Qt filesystem facilities. Copy, cut, paste, rename, delete, create file, create directory, multi-selection, and drag-and-drop must handle conflicts, permissions, and interruption safely. Large-file previews must stream or sample data instead of loading entire files.

## Platform integration

Application logic must not branch on X11 versus Wayland. Qt's platform-plugin abstraction is the boundary.

`src/platform/platform.cpp` selects a Qt platform plugin from the ambient environment:

- If `QT_QPA_PLATFORM` is already set, respect it.
- If `WAYLAND_DISPLAY` is absent, search `$XDG_RUNTIME_DIR` for a `wayland-*` socket.
- If a Wayland display is available and no `DISPLAY` is set, select the Qt `wayland` plugin.
- Otherwise leave Qt's default selection, which uses the X11 backend when `DISPLAY` is available.

This is platform selection, not compositor-specific application logic.

Verified on this machine:

- `/usr/lib64/qt6/plugins/platforms/libqxcb.so` exists.
- `/usr/lib64/qt6/plugins/platforms/libqwayland.so` exists.
- QtWaylandClient headers exist.

Do not hardcode compositor-specific behavior. Keyboard, mouse, clipboard, drag-and-drop, and window management belong in Qt's platform abstraction.

## Module layout

```
src/
  app/          main.cpp (arguments, then one window), appwindow.hpp (the keybinds, the
                session list, the close and switch questions, the config file's arrival),
                closeprompt.hpp (when closing asks — no Qt), sessionmenu.hpp (what a
                list row says — no Qt)
  config/       config.hpp (the key=value file, its colours, its complaints — no Qt)
  ipc/          protocol.hpp (frames, fields, records), socket.hpp (bind, connect, peers)
  process/      pty.hpp (forkpty, resize, read, reap), job.hpp (what is running out front)
  session/      daemon.hpp (sessions, views, commands), cli.hpp (the command surface and
                the operations a window uses), snapshot.hpp (a screen as escape
                sequences), sessionbackend.hpp
  terminal/     screen.hpp, vtparser.cpp, terminalwidget.hpp, backend.hpp,
                selection.hpp, text.hpp, keyencode.hpp, mouseencode.hpp,
                scrollbar.hpp, base64.hpp
  platform/     platform.hpp/.cpp — how Qt is told to behave on this machine
  multiplexer/  filesystem/  filemanager/  editor/  config/   (not started)
```

Everything under `ipc/`, `process/`, `session/` and `terminal/` except the widget is
Qt-free and header-only, which is what makes the headless tests possible: they use the
same code the application uses, not a reimplementation of it.

Tests are one executable each under `tests/`, named for what they check, and registered
with `ctest`. Those needing a display use `QT_QPA_PLATFORM=offscreen` and say so; those
needing a shell use a real pty and `/bin/sh`.

## Reliability rules

- The daemon owns process lifetime in the final architecture.
- GUI closure is a detach event.
- Daemon crash loses live PTY processes; metadata persistence is not process persistence.
- Signals, controlling-terminal behavior, and descriptor lifetimes must be tested.
- No subsystem may be described as complete until it is implemented and verified.
- Documentation is part of the deliverable, not a receipt left after it. A change that
  alters what the program does — a keybind, a dialog, a socket path, the number of
  checks a test passes, whether something has been verified by a person on a live
  display — is not finished until every document that describes it says so, in the same
  session that changed it. Stale documentation is worse than none, because it is
  believed.
- The repository is part of the deliverable too. When a task's tests pass, its work is
  committed — in logical commits whose messages explain why — and pushed to the remote,
  as the closing step of the task. A session that ends with uncommitted work has handed
  its risk to whoever loses power first.
