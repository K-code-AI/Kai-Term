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

Planned socket location:

```
$XDG_RUNTIME_DIR/kai-term/daemon.sock
```

Planned protocol:

- Framed messages with a magic prefix, protocol version, message length, request id, and JSON payload.
- Protocol version is explicit and checked.
- Incoming payloads are validated and size-limited.
- Multiple GUI clients are supported where practical; session ownership remains explicit.
- Client disconnect is treated as detach, not as a shell kill.
- Stale sockets are detected by attempting a connection and verifying daemon identity.

The daemon owns PTY masters and shell process lifetimes. It also stores session metadata, window/pane layout, and terminal dimensions. Metadata persistence is not equivalent to preserving live process memory after a daemon crash.

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

Planned source structure:

```
src/
  app/
  terminal/
  process/
  session/
  daemon/
  multiplexer/
  filesystem/
  filemanager/
  editor/
  platform/
  config/
```

Current implemented modules:

- `src/app/main.cpp`
- `src/terminal/screen.hpp`
- `src/terminal/vtparser.cpp`
- `src/terminal/terminalwidget.hpp`
- `src/process/pty.hpp`
- `src/platform/platform.hpp`
- `src/platform/platform.cpp`

- `src/app/main.cpp`
- `src/terminal/screen.hpp`
- `src/terminal/vtparser.cpp`
- `src/terminal/terminalwidget.hpp`
- `src/process/pty.hpp`

## Reliability rules

- The daemon owns process lifetime in the final architecture.
- GUI closure is a detach event.
- Daemon crash loses live PTY processes; metadata persistence is not process persistence.
- Signals, controlling-terminal behavior, and descriptor lifetimes must be tested.
- No subsystem may be described as complete until it is implemented and verified.
