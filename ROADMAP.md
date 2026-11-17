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

Status: **in progress; window launch verified**.

Implemented:

- Native terminal emulator; no `libvterm`.
- Application window and Qt terminal widget.
- PTY creation with `forkpty()`.
- Shell execution with configurable shell.
- Keyboard input and terminal output.
- Basic resizing through `TIOCSWINSZ`.
- Scrollback buffer.
- Native VT parser for colors, cursor movement, erasing, and Unicode.
- Headless parser test and PTY test.

Not yet verified:

- GUI rendering and keyboard behavior without launching a window.
- Scrollback UI.
- Full shell compatibility across interactive applications.

## M2 — Terminal reliability

Status: **pending**.

Needed:

- Unicode and color refinements.
- Broader escape-sequence coverage.
- Interactive applications.
- Clipboard.
- Mouse reporting.
- Process lifecycle edge cases.
- X11 and Wayland runtime verification.

## M3 — Native session infrastructure

Status: **pending**.

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
