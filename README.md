# Kai-Term

Kai-Term is a lightweight terminal emulator for Linux, written in C and built for Wayland.

## Features

- **Integrated file manager** — Browse directories, preview images and text files, and change the shell's working directory without losing your current command.
- **Multiplexing** — Open and switch between sessions in one window, with an optional side-by-side layout.
- **Theme import** — Import color schemes from supported popular terminal emulators.
- **Customizable appearance** — Configure fonts, colors, transparency, and window dimensions.
- **Click-to-open URLs** — Open web addresses directly from the terminal.

## Requirements

Kai-Term requires Linux running a Wayland compositor. X11 is not supported.

### Runtime dependencies

- Wayland client, cursor, and EGL libraries
- EGL and OpenGL
- xkbcommon
- FreeType, fontconfig, and HarfBuzz
- libpng, zlib, libjpeg, and libwebp

### Build dependencies

- C compiler supporting GNU C11
- `make`
- `pkg-config`
- `wayland-scanner`
- `wayland-protocols`

## Build

```sh
make
```

The executable is created at `bin/kai-term`.

Run:

```sh
./bin/kai-term
```

Run tests:

```sh
make test
```

## Usage

Run a command directly:

```sh
./bin/kai-term -e htop
```

Start in a specific directory:

```sh
./bin/kai-term -w "/home/me/My Projects"
```

The default startup directory is the user's home directory.

### Command-line options

| Option | Description |
|---|---|
| `-e COMMAND` | Run a command instead of the default shell |
| `-w DIR` | Set the startup directory |
| `-c FILE` | Use a custom configuration file |
| `-b FILE` | Use custom key bindings |
| `--import-theme FILE` | Import a supported color scheme |
| `--force` | Overwrite an existing theme during import |
| `-d` | Enable debug logging |
| `-v` | Display version |
| `-h` | Display help |

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl` + `Shift` + `C` | Copy selection |
| `Ctrl` + `Shift` + `V` | Paste |
| `Ctrl` + `+` / `-` | Increase/decrease font size |
| `Ctrl` + `0` | Reset font size |
| `Ctrl` + `Shift` + `T` | Open a new session |
| `Ctrl` + `Shift` + `W` | Close the current session |
| `Ctrl` + `Shift` + `Right` / `Left` | Switch sessions |
| `Ctrl` + `Shift` + `1`–`9` | Select a session |
| `Ctrl` + `Shift` + `S` | Toggle side-by-side layout |
| `Ctrl` + `Shift` + `F` | Toggle file manager |
| `Ctrl` + `Shift` + `Up` / `Down` | Focus file manager / return to terminal |

Keyboard shortcuts can be customized in `kai-term.bind`.

## Configuration

Kai-Term uses plain-text configuration files:

- `kai-term.conf` — Appearance and terminal settings.
- `kai-term.bind` — Keyboard shortcuts.
- `NAME.theme` — Color themes.

Configuration files are searched in `$XDG_CONFIG_HOME/kai-term`, `~/.config/kai-term`, and `/etc/kai-term`.

Example configuration files are available in [`config/`](config/).

Example settings:

```ini
font = JetBrains Mono
font_size = 16
background = #1c1c22
opacity = 0.95
```

## License

MIT
