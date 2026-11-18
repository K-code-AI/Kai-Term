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

Two rules about the view are worth stating because they are the ones a well-meaning
change breaks:

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

Still open: Unicode width handling is basic and may need refinement for combining
characters and unusual emoji sequences, and `OSC 8` hyperlinks are parsed and discarded.

## Session daemon and IPC

One daemon per user, keeping sessions alive whether or not anybody is looking:

```
$XDG_RUNTIME_DIR/kai-term/daemon.sock
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
`session list`, that an older daemon is running, which protocol it speaks, which process
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

A listing says which is which, and names what is running out front — the process group
the terminal reports owning, read from `/proc`, empty when the group is the shell itself.
That is what lets a window ask "Quit while `make` is running?" instead of guessing from
how long since you last typed, and it is asked of the daemon rather than of the window,
because the shell is not the window's child.

`session list` shows kept sessions and the left-behind ones, and not the shells other
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
shell, and otherwise has no idea what a session is. `src/app/appwindow.hpp` is where
sessions, menus and the close prompt live, and the prompt's *decision* is in
`src/app/closeprompt.hpp` with no Qt in it: what to ask is worth testing, what the dialog
looks like has to be looked at.

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
                menu, the close prompt), closeprompt.hpp (when closing asks — no Qt)
  ipc/          protocol.hpp (frames, fields, records), socket.hpp (bind, connect, peers)
  process/      pty.hpp (forkpty, resize, read, reap), job.hpp (what is running out front)
  session/      daemon.hpp (sessions, views, commands), cli.hpp (the `session` verbs and
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
