# Kai-Term Dependencies

## Verified on this machine

| Component | Status | License / source | Use |
|---|---|---|
| Qt 6 Widgets 6.11.2 | Installed; `pkg-config Qt6Widgets` resolves | Qt open-source edition, LGPL v3 (some modules GPL) | GUI, keyboard, mouse, clipboard, window management |
| Qt 6 X11 platform plugin | Installed: `/usr/lib64/qt6/plugins/platforms/libqxcb.so` | Same Qt package | X11 backend through Qt |
| Qt 6 Wayland platform plugin | Installed: `/usr/lib64/qt6/plugins/platforms/libqwayland.so` | Same Qt package | Wayland backend through Qt |
| QtWaylandClient headers | Installed under `/usr/include/qt6/QtWaylandClient` | Same Qt package | Wayland-specific extension code, if needed |
| CMake 4.3.4 | Installed | CMake project | Build system |
| POSIX PTY / `forkpty()` | OS facilities | Kernel / system library | PTY creation and shell lifecycle |
| Native VT parser | Project code | Own implementation | Escape-sequence interpretation and screen model |

## Removed dependency

`libvterm` is **not used**. It is not installed and the project does not require it. The terminal emulator and escape-sequence parser are native to Kai-Term.

## Pending decisions

- **TOML parser**: not selected. A small, maintained parser is preferable to a large desktop stack.
- **Testing**: GitHub Actions workflow is planned, not yet committed.
- **Editor**: no library selected.
- **Clipboard**: Qt platform integration is planned; it must be tested against a foreign clipboard client before being claimed.

## Licensing notes

Kai-Term itself is licensed under the **GNU General Public License v3.0 only** (`LICENSE`).

- Qt's open-source edition is LGPLv3; some individual Qt modules are GPL. Linking Qt against a GPLv3 program is compatible; keep Qt dynamically linked and preserve the LGPL notice so users can swap the library. Verify module licensing before redistribution.
- POSIX PTY APIs are system interfaces and do not add a project dependency.
- No third-party terminal emulation library is bundled.
- Any new third-party dependency must be GPL-v3.0-only compatible or it cannot be added.

