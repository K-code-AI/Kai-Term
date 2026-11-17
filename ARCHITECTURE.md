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

Limitations:

- Scrollback is retained in memory but has no scrollback UI yet.
- Keyboard behavior was not runtime-verified without launching the GUI.
- Clipboard and mouse reporting are not implemented yet.
- Unicode width handling is basic and may need refinement for combining characters and unusual emoji sequences.

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
so out loud, and tmux will not start properly. A program asking for its text to be copied
(`OSC 52`) gets a `Clipboard` frame instead, because the daemon has no clipboard; a view
that receives one puts it in the one it has. A program *asking for* the clipboard is not
answered, for the same reason in reverse.

Teardown rules, both learned the hard way:

- A descriptor is never closed under a thread blocked reading it. The session's reader
  thread owns the master and closes it last; a client socket belongs to the thread
  serving it.
- Waking that thread takes `shutdown(fd, SHUT_RDWR)`. `SHUT_WR` only tells the program on
  the far end that the session is gone; the thread blocked in `read()` on our side is
  woken by shutting down our own receive side.

Client disconnect is a detach, never a shell kill. `kill` and `shutdown` are the ways a
session ends.

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

Panes are the widget's business and its host's knowledge: `setPanes()` says which cells
belong to which pane, and a drag is clipped to the one it started in. A terminal with one
shell passes nothing and gets one pane, the screen.

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
  app/          main.cpp — the window, and the argument handling in front of it
  daemon/       main.cpp — the same loop as `session serve`, in the foreground
  ipc/          protocol.hpp (frames, fields, records), socket.hpp (bind, connect, peers)
  process/      pty.hpp — forkpty, resize, read, reap
  session/      daemon.hpp (sessions, views, commands), cli.hpp (the `session` verbs),
                snapshot.hpp (a screen as escape sequences), sessionbackend.hpp
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
