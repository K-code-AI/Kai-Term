# Kai-Term

Kai-Term is a lightweight terminal emulator for Linux, written in C and built for native Wayland. It runs your shell in a terminal window with GPU-accelerated rendering, Unicode text, color emoji, scrollback, and clipboard support.

Kai-Term is currently a working prototype. It builds cleanly, runs a shell, and exits cleanly.

## Features

- **Native Wayland** — Resizable windows with a terminal grid that adapts to the window size.
- **Reflow on resize** — Changing the window width re-wraps the text instead of cutting it: a line that
  was wrapped at the old margin is joined back and re-broken at the new one, so widening the window
  brings the text back. Scrollback is re-wrapped too; narrowing far enough drops the oldest lines.
- **GPU rendering** — OpenGL rendering for fast text and graphics.
- **Unicode and color emoji** — Supports complex emoji sequences, including skin tones, flags, and joined emoji.
- **Images in the terminal** — An application can draw a picture inline in the text with the kitty
  graphics protocol (`timg -p k`). PNG payloads, optionally zlib-compressed, and raw RGB/RGBA pixels
  are decoded; the picture is placed at the cursor, sized from the terminal's cell pixel size, scrolls
  with the text around it, stays in scrollback, and never covers the text drawn over it.
- **Text and colors** — UTF-8 text, 16-color, 256-color, and 24-bit truecolor support.
- **Scrollback** — Retains history (2000 lines by default) and scrolls with the mouse wheel or the
  keyboard; new output snaps the view back to the live bottom.
- **Full-screen applications** — The alternate screen (`vim`, `htop`, `less`, `nano`) is supported; the
  main screen is parked while an app owns it and comes back re-wrapped when the app exits. Kai-Term also
  honours application cursor keys (DECCKM), so the arrow keys work in programs that ask for them — `less`,
  `man` and `htop` answer only that form.
- **Clear scrollback** — `clear` can remove both visible content and terminal history.
- **Mouse support** — Application mouse reporting, text selection, and clipboard integration.
- **Scrolling a program that owns its screen** — The mouse wheel scrolls `nano`, `less` and `man` even
  though they never asked for the mouse, by turning the wheel into cursor-key presses; a click moves the
  program's own cursor to the cell you clicked. See "The mouse wheel and a click inside a full-screen
  program".
- **Key repeat** — Holding a key down repeats it, with the delay and rate set in the configuration file.
- **Transparency** — Configurable window opacity, subject to compositor support.
- **Window padding** — A configurable margin around the text, so a compositor's rounded window corners
  never clip a glyph.
- **Several sessions in one window** — Each session has its own shell, scrollback, selection and title;
  opening, closing and switching between them are keybindings you set in the configuration file.
- **Tab bar** — With two or more sessions, one row at the top of the window names each of them and marks
  the ones with output you have not seen. A click switches to a session, a middle click closes it.
- **Side-by-side view** — The sessions can be drawn next to each other as equal-width panes in the same
  window; one shortcut flips between that and the one-session-per-window view.
- **File manager** — A browser and a live preview pane can be split into the top of the window, drawn with
  the same font, colours and cells as the terminal. It shows the directory contents, an image preview
  (PNG, JPEG, WebP, GIF), syntax-highlighted text, or the type, size, dates, owner and permissions of
  anything else, and the shell below follows the directory you browse into, keeping whatever you had already
  typed at the prompt. The listing starts with a `..` row, one click selects and a double-click opens.
  One key drops the selected entry's quoted path onto your half-typed command without running it. One
  shortcut shows the band, another moves the keyboard between the browser and the terminal. While the
  preview pane has the keyboard, `Page_Up`/`Page_Down`/`Home`/`End` scroll the previewed text.
- **Shell integration** — A fish, bash or zsh that Kai-Term starts is loaded with a small script of its own,
  which lets the file manager ask the shell to change its **own** working directory: your half-typed command is
  never erased, never re-typed and never sees a `cd`, and the shell confirms the directory it entered. A shell
  you start at the prompt (`bash` inside fish) is loaded with it too. Any other
  shell keeps the typed path (capture, erase, `cd`, re-type), and `shell_integration = 0` turns the script off
  entirely.
- **Font zoom** — Increase or decrease the font size without resizing the window.
- **System clipboard** — Copy and paste use the standard Wayland clipboard, so text copied in Kai-Term
  pastes into other applications and text copied anywhere else pastes into Kai-Term.
- **Configuration** — Two plain-text files: `kai-term.conf` for the settings and `kai-term.bind` for the
  key bindings, read from `$XDG_CONFIG_HOME/kai-term`, `~/.config/kai-term` or `/etc/kai-term`, with a
  commented template of every default in [`config/`](config) in the source tree.

## Requirements

- Linux on a Wayland desktop (Kai-Term is a native Wayland client; it does not run under X11)
- A C compiler supporting `-std=gnu11`
- `pkg-config`
- Wayland client, cursor, and EGL
- EGL and OpenGL
- xkbcommon
- FreeType
- fontconfig
- HarfBuzz
- libpng
- zlib
- libjpeg (the file manager's JPEG previews)
- libwebp (the file manager's WebP previews)
- wayland-scanner
- wayland-protocols

## Build

Build Kai-Term with:

```sh
make
```

The executable is created at `bin/kai-term`.

Other available targets:

| Command | Description |
|---|---|
| `make test` | Build and run the unit tests, the grid-model checker and the command-line tests |
| `make test-cli` | Run only the command-line tests against `bin/kai-term` |
| `make asan` | Run the tests with AddressSanitizer and UBSan |
| `make run` | Build and launch Kai-Term |
| `make clean` | Remove generated build files |

The command-line tests check the option parser by running the real binary, so part of them opens real
windows. Before those checks run the suite makes **one** probe launch; if it cannot draw (no Wayland
session, or a GPU with no video memory left) the window checks are reported as skipped with the reason
instead of being repeated. Set `KAI_TERM_TEST_NO_WINDOW=1` to skip them without even mapping the probe
window:

```sh
KAI_TERM_TEST_NO_WINDOW=1 make test
```

## Usage

Launch Kai-Term:

```sh
./bin/kai-term
```

Run a specific command:

```sh
./bin/kai-term -e htop
```

Open a terminal in a specific directory:

```sh
./bin/kai-term -w "/home/me/My Projects"
```

The default startup directory is the user's home directory. You can change it in the configuration file or override it for a single launch with `-w`.

### Command-line options

| Option | Long form | Description |
|---|---|---|
| `-e COMMAND` | `--exec COMMAND` | Run a command instead of the default shell |
| `-w DIR` | `--workingdir DIR` | Set the startup directory for this launch |
| `-c FILE` | `--config FILE` | Read the settings from FILE instead of the searched `kai-term.conf` |
| `-b FILE` | `--binds FILE` | Read the key bindings from FILE instead of the searched `kai-term.bind` |
| `-d` | `--debug` | Enable debug logging |
| `-v` | `--version` | Print the version and exit |
| `-h` | `--help` | Show the option list and exit |

Long options also accept the `--name=VALUE` form (for example `--workingdir=/tmp`).

### Exit status

Kai-Term exits `0` after a normal session (the last tab closed, the window closed, or `-h`/`-v`). It exits
`1` for a command-line error (an unknown option, or a flag with no argument) and also when the renderer
cannot get video memory for its glyph atlas — the log then ends with
`kai-term: no video memory for the glyph atlas — exiting` and `kai-term exiting — renderer failure`.

### Images

Kai-Term speaks the kitty graphics protocol: an application sends a picture as one or more escape
sequences, Kai-Term decodes it and draws it between the cell backgrounds and the text, so the text
stays readable on top of it. The picture is placed at the cursor, sized from the cell pixel size
(Kai-Term reports that to the shell in the PTY window size, and answers `CSI 14 t` / `CSI 16 t` for
applications that ask), scrolls with the text around it, and comes back when you scroll into history.

With [timg](https://timg.sh) installed:

```sh
timg -p k foo.png        # -p k selects the protocol Kai-Term understands
```

The picture appears at the cursor and the prompt lands below it. The mouse wheel or `Shift`+`PageUp`
bring it back after it has scrolled off; `clear` drops it together with the history.

timg does not choose that protocol by itself: it auto-detects graphics only on terminals whose `TERM`
is `xterm-kitty` or `xterm-ghostty` (measured with timg 1.6.3). Without `-p k` it draws the picture
with quarter-block characters instead, which needs no terminal support at all. What else is not
supported is listed in [ISSUES.md](ISSUES.md) under "Known limitations".

Command-line options take precedence over configuration values. Everything that describes how the
terminal looks — font, font size, columns, rows, scrollback, colours, opacity — is configured in the
configuration file only. Any option Kai-Term does not accept is rejected with
`kai-term: unknown option '<flag>'` followed by the option list above.

### File manager

`Ctrl`+`Shift`+`F` splits the top of the window into a **browser** (left) and a **preview** (right), and
leaves the ordinary terminal underneath. It is drawn with the same font, colours, borders and cells as the
terminal, and closing it returns the window to exactly what it was before.

The band takes three quarters of the window's text area by default and **keeps that share** when you resize
the window or change the font size — `fm_size` in the configuration file sets the percent (10–95). The
terminal always keeps its floor: if the window is too small for the band and three text rows, the band is
dropped for that frame rather than starving the shell.

The browser lists the current directory — directories first, then files, case-insensitively — with the
directory path in its header line and, in its footer, how many entries are shown, whether the listing was
capped, and any error. The first row is always `..`, the way back up (the root directory has no such row,
because there is nothing above it). Each row shows the name, a `@` for a symlink, and the size for a file.
The selected row is highlighted across the whole width and keeps the colour of its kind.

The preview pane shows whatever fits the selected entry:

| Selected | Preview |
|---|---|
| a directory | its contents (up to 512 entries), plus the entry count |
| an image | the picture, aspect-preserving and never upscaled, with its metadata in the rows above it — PNG, JPEG, WebP and GIF, recognised by their own magic bytes, not by the file name. A preview pane too short for both shows the picture and drops the metadata lines that do not fit |
| a text file | the first 256 KiB, with syntax highlighting for C/C++/headers, Python, shell, JSON, Markdown, Make, INI/TOML, HTML/XML, Rust, JS/TS, Go, CSS and YAML |
| anything else | its type, size, modification and access times, owner and group, and permission string |

Binary files, files that cannot be read, missing files and dangling symlinks are shown as metadata with a
line saying why there is no preview — never as garbage and never as an error that stops the browser.

| Key (default) | In the browser |
|---|---|
| `Up` / `Down` | move the selection (holding the key walks the list) |
| `Page_Up` / `Page_Down`, `Home` / `End` | move by a page, to the first / last entry — with the **preview pane** focused they scroll the previewed text instead (see below) |
| `Left` | go to the parent directory |
| `Right` | enter the highlighted directory — and **nothing at all** on a file, so holding it cannot write at your prompt |
| `Return` | open the highlighted directory — and **nothing at all** on a file, so holding it cannot write at your prompt |
| `p` | **type the selected entry's quoted path at your prompt**, at the cursor, without pressing Enter — the only file-manager key that touches the line, and it never runs anything |
| `BackSpace` | go to the parent directory |
| `r` | re-read the current directory |
| `v` | show or hide the preview pane (with it off, the browser is the full width of the band) |
| `.` | show or hide dotfiles |
| `Escape` | close the file manager |
| mouse left click | select the row under the pointer; a **second click on the same row** opens it (a directory is entered; a file does nothing); a click in the terminal pane hands the keyboard back |
| mouse wheel | over the browser it walks the listing; over the preview pane it scrolls that pane's contents (a file's text, or a directory's entries); over the terminal it scrolls history as usual — except inside a full-screen program that never asked for the mouse, where it becomes cursor keys (see below) |

`Ctrl`+`Shift`+`Up` gives the keyboard to the browser, `Ctrl`+`Shift`+`Down` gives it back to the terminal.

The **preview pane** takes the keyboard too: a click on it, or the wheel over it, focuses it, and while it
has the keyboard `Page_Up`/`Page_Down` scroll the previewed text by one page and `Home`/`End` jump to its
first and last line — which is what makes a long file readable in the pane that exists to read it. One page
is exactly what the pane shows at once, so holding `Page_Down` walks a file with no line shown twice and
none skipped. `Up`/`Down` keep moving the browser's selection there, because that is how you change which
entry is being previewed. A preview with nothing to scroll — a picture, or a metadata-only preview — does
not move at all.

While the band has the keyboard — browser or preview — the keys above belong to it **and no other key
reaches the terminal** —
typing with the band focused cannot write into a shell you are not looking at. The window's own shortcuts
(new session, close, switch, zoom, copy, paste, `Shift`+`PageUp`/`PageDown` scrolling) keep working from
either side, because they belong to the window and not to the text. The moment the terminal has the keyboard
those same keys go to the shell unchanged, so `Up` is still history recall. A thin border marks which region
has the keyboard.

By default, browsing into a directory also moves the shell there, so the prompt lands where you are looking
(`fm_follow = 0` turns that off).

**`p` puts a path on your command line; nothing else in the band does.** It types the selected entry's absolute
path in single quotes at the cursor and stops there — no Enter, so Kai-Term never runs a file it was only asked
to name. It works however far you have already got with the command, and the path lands **where your cursor is**:

| your line before `p` | your line after `p` on `somefile.txt` |
|---|---|
| (nothing) | `'/path/to/somefile.txt'` |
| `cat ` | `cat '/path/to/somefile.txt'` |
| `cat tail`, cursor moved back before `tail` | `cat '/path/to/somefile.txt'tail` |
| `cat ` with fish's grey autosuggestion showing | `cat '/path/to/somefile.txt'` — the suggestion is left alone |

A path with a space or an apostrophe in it arrives quoted the shell's way, so it needs no further quoting from
you. `p` is refused, and says so in the `-d` log, when the line is not one Kai-Term can describe: a full-screen
application (vim, less, htop) owns its screen, and a wrapped or scrolled-back or repainted prompt is not
something it will write into. `fm_key_path = none` removes the key entirely, and then **no** file-manager key can
write at your prompt.

**With fish or bash, the shell does the changing and your line is never touched.** Kai-Term starts a recognised
shell (fish, bash, zsh) with a small integration script — your own configuration is loaded first, nothing of
yours is replaced or reordered — and on a navigation it asks that shell to change its **own** directory. The
request is one key sequence the shell's line editor consumes (`Ctrl+x Ctrl+]`, which fish and bash leave
unbound by default) and the directory arrives out of band in a private file, so nothing is typed at your prompt,
nothing is echoed, and your half-typed command stays exactly where it was. The shell then confirms the directory
it entered, and that confirmation is the only thing Kai-Term believes: `-d` logs
`fm: shell integration (fish) asked to cd into …` followed by
`fm: the shell's integration changed directory to … — the line was not touched`. Two things to know: fish and zsh
repaint your prompt, so it names the new directory straight away, while bash cannot (readline expands the
prompt once per line and no readline command re-expands it mid-line), so in bash the directory in your prompt
string stays the old one until you press Enter — the shell's working directory has already changed, and `pwd`
says so; and `shell_integration = 0` turns the whole thing off, which
starts every shell exactly as before and gives that key sequence back to you. All three shells are verified — in
the user's own window (2026-10-20) and headless against fish 4.6, bash 5.3 and zsh 5.9, with the confirmation
arriving in 54-76 ms.

**A shell you start at the prompt is integrated too.** The script defines `fish`, `bash` and `zsh` as functions
that start the real program with the integration loaded, so typing `bash` at an integrated fish prompt gives
you an integrated bash, and the file manager keeps working inside it without touching your line. Only a bare
`bash`/`fish`/`zsh` is affected — `bash script.sh` and `fish -c …` run exactly as you typed them — and a name
you already defined as a function or an alias is never replaced. When that shell exits, the shell underneath
announces itself again, so the file manager knows which one it is talking to. If you would rather `type bash`
reported the plain program, `shell_integration = 0` gives you that back.

**Every other shell — and any shell you start yourself — gets the typed path.** With an empty
line Kai-Term types `cd '<that directory>'` and Enter. **If your line already holds text, the
file manager moves it rather than dropping it:** on a navigation it captures what you had
typed, erases the line, sends the `cd`, and types your text back onto the new prompt — so the
prompt always ends up in the directory the browser is showing, with your half-typed command
intact in front of it. Doing that in one write would be unsafe, so each step waits for proof
from the shell: the `cd` is sent only once the line is seen to be empty, and your text is typed
back only after the new prompt has been drawn (the shell has gone quiet). Nothing is ever
submitted for you — the re-typed text arrives without a newline, so it can never run by itself.
Repeating the navigation does the same thing again: the text Kai-Term put back is erased and
put back again, never appended to. `-d` logs each step (`fm: clearing the shell's input line to
follow the browser into …`, `fm: told the shell to follow: …`, `fm: re-typing what was on the
line: …`, `fm: the prompt now holds the browser's directory and what you had typed`).

Two cases the typed path will not write through, and in both the browser still moves and your line is left
alone:

- A line Kai-Term cannot describe — a command that has wrapped onto a second row, a view scrolled back into
  history, a full-screen program in front, a prompt the shell redrew with different colours, or text too long
  to hold. It logs `fm: not telling the shell to follow — its input line is not one I can describe`.
- A cursor parked in the **middle** of a command. The erase is Ctrl+U, which erases backwards from the cursor,
  so the text right of it would survive and the `cd` would run in front of it. Kai-Term then proves the line
  empty, fails, and drops the sequence instead of writing; the captured text is printed in the log so you can
  put it back by hand. A shell's autosuggestion ghost (fish's grey suggestion right of the cursor) is *not*
  one of these — it is ignored, and the navigation goes through.

If a sequence cannot finish within two seconds it is dropped rather than left hanging, and the log says which
step it stopped at and what had been captured.

A shell started in a way the script cannot see (`env bash`, `/usr/bin/bash`, `sudo bash`) has the environment
but not the key binding, so it never answers: Kai-Term waits 400 ms, logs
`fm: no integration ack from … — using the typed path`, and falls back to the typed path for that navigation.
Later navigations in that same shell go straight to the typed path, and as soon as an integrated shell is back
at the prompt the integration is used again — every integrated shell repeats its announcement after each
command, which is what tells Kai-Term who is reading the line. The sequence is inert in every supported
shell, so a shell that is not listening simply ignores it — nothing lands on your line.

To have the band up from startup, set `file_manager = 1` in the configuration file.

### The mouse wheel and a click inside a full-screen program

A program that takes over the whole screen — `nano`, `less`, `man` — owns its own text and has no terminal
history to scroll, so the wheel used to do nothing there. It now sends **cursor-key presses** instead, which is
what actually moves those programs (the same thing other terminals do). It works in both directions:

| Gesture | Inside `nano` / `less` / `man` |
|---|---|
| wheel up / down | Up / Down presses, as many as the gesture is worth |
| left click on a cell | the presses that walk the program's own cursor to that cell |
| click-drag | still selects text, so you can still copy out of `nano` and `less` |
| `Shift` + click | always selects, never moves the program's cursor |

Two things worth knowing, because they decide what you will see:

- **This is keystrokes, not mouse support.** The program is never told a mouse exists and receives no
  coordinates — which is deliberate: a program that has not asked for the mouse reads a mouse report as typed
  text, and one report would put something like `64;20;5M` into your file. So a click moves the program's
  cursor the way pressing the arrow keys would, and a program that uses the arrow keys for something other
  than moving a cursor will react to a click (a mixer would change the slider it is on). `click_moves_cursor = 0`
  turns the click half off; `wheel_sends_keys = 0` leaves the wheel doing nothing in that state, the way it did
  before.
- **A click past the end of a line lands at the end of that line**, not on the row below, and a click in the
  empty space of a blank row lands at its start.

A program that *did* ask for the mouse — `vim`, `htop`, `tmux`, `nano --mouse` — is untouched by all of this:
it keeps receiving mouse reports, including the wheel, and Kai-Term sends it the wheel code those programs
understand (64/65). The main screen keeps scrolling its scrollback. The cursor keys themselves now follow the
program's own request (DECCKM), which is a fix on its own: `less`, `man` and `htop` answer only the form they
ask for with `ESC[?1h`, so the arrow keys did nothing in them before.

## Configuration

Kai-Term reads two plain-text files, both in the same `key = value` format:

| File | Holds |
|---|---|
| `kai-term.conf` | the settings — font, grid, colours, shell, mouse, sessions, file manager, logging |
| `kai-term.bind` | the key bindings — every `key_*` and `fm_key_*` name |

### Where the files come from

Each file is searched for in this order, and the first location that holds it wins — the rest are never
opened, so your own file is never mixed with the system's:

1. `$XDG_CONFIG_HOME/kai-term/`
2. `~/.config/kai-term/`
3. `/etc/kai-term/`

If no location holds a file at all, the built-in defaults are used. The two files are searched
**independently**: a `kai-term.conf` in `~/.config/kai-term` and no `kai-term.bind` there still picks up
the system-wide bindings from `/etc/kai-term`. `-c FILE` and `-b FILE` name one file explicitly and skip
its search.

`kai-term.bind` is read **after** `kai-term.conf`, so for a name both files set, the bind file wins. A
binding left in an older `kai-term.conf` is still honoured (the log line says where the line belongs now),
and a setting written in `kai-term.bind` is refused out loud rather than ignored.

### Templates

[`config/kai-term.conf`](config/kai-term.conf) and [`config/kai-term.bind`](config/kai-term.bind) in the
source tree list every name with its built-in default and say what it does. Copy them to one of the
locations above and change what you want — a line left at its default does exactly nothing. The test suite
loads both files and compares every value with the compiled-in defaults, so the templates cannot drift
from the program.

### Settings (`kai-term.conf`)

| Setting | Default | Description |
|---|---|---|
| `font` | `monospace` | Fontconfig font pattern |
| `font_size` | `14` | Font size in pixels |
| `cols` | `80` | Initial number of columns |
| `rows` | `24` | Initial number of rows |
| `scrollback` | `2000` | History lines to retain. The grid allocates `rows + scrollback` lines once at startup, so a window taller than `rows` keeps fewer of them (`rows + scrollback - current_rows`) |
| `foreground` | `#e5e5e5` | Default text color — the ink of every cell an application has not colored itself |
| `background` | `#101014` | Default background color — the one value behind everything an application has not colored: the cells, the `padding` margin, and the pixels left over by whole cells at the bottom and right |
| `cursor_color` | `#d8d8d8` | Cursor colour. A session that is drawn in the window but is not the one the keyboard talks to gets the same colour dimmed toward `background` |
| `opacity` | `1.0` | Window opacity |
| `padding` | `8` | Pixels of margin between the window edge and the first/last row and column (`0` puts the text flush against the edge). Applied on all four sides; pixels left over by whole cells still go to the bottom and right. That margin is painted with `background`, so it is invisible unless an application colors the edge cells itself |
| `shell` | Empty | Shell to run; defaults to `$SHELL` |
| `shell_integration` | `1` | Start a recognised shell (fish, bash, zsh) with Kai-Term's integration script, so the file manager can ask the shell to change its own directory instead of typing `cd` at your prompt, and so a bare `bash`/`fish`/`zsh` you run at the prompt is integrated as well (`0` starts every shell exactly as before) |
| `working_dir` | Empty | Startup directory; defaults to the home directory |
| `app_id` | `Kai-Term` | Wayland app ID reported to the compositor (window rules, taskbar grouping) |
| `clear_scrollback` | `1` | Clear history when `clear` is used |
| `copy_on_release` | `0` | Copy to the clipboard when the selection drag is released |
| `wheel_sends_keys` | `1` | Inside a full-screen program that never asked for the mouse (`nano`, `less`, `man`), the wheel sends cursor-key presses, which is what scrolls them (`0` leaves the wheel doing nothing there) |
| `click_moves_cursor` | `1` | In that same state, a left click that does not drag walks the program's own cursor to the clicked cell (`0` means a click only ever selects text). A drag always selects, and `Shift` + click always selects |
| `repeat_delay` | `300` | Milliseconds a key must be held before it starts repeating (`0` disables repeating) |
| `repeat_rate` | `30` | Repeats per second once a key starts repeating |
| `max_tabs` | `8` | Sessions one window may hold (clamped to 1–64) |
| `tab_bar` | `auto` | `off` never draws the tab bar, `auto` draws it from the second session on, `always` also with one session |
| `split_max` | `4` | Sessions drawn side by side at most; more sessions stay open and stay listed in the bar |
| `file_manager` | `0` | Show the file-manager band from startup instead of only when the shortcut is pressed |
| `fm_size` | `75` | Percent of the window's text area the band asks for (clamped to 10–95, and shrunk further so the terminal keeps at least 3 rows; if even that does not fit, the band is dropped for that frame) |
| `fm_preview` | `1` | Show the preview pane next to the browser (`v` toggles it at runtime; off means the browser is full width) |
| `fm_show_hidden` | `0` | Include dotfiles in the listing from the start (`.` toggles it at runtime) |
| `fm_follow` | `1` | Browsing into a directory also sends `cd '<path>'` to the shell |
| `log` | `1` | Log level: 0 = error, 1 = warning, 2 = info, 3 = debug |

Font size, grid dimensions, scrollback limits, opacity, the key-repeat timings and the file-manager band
height have defined valid ranges; out-of-range values are clamped to the nearest valid one. Invalid
configuration values should be corrected according to the application's diagnostics.

`foreground` and `background` are the whole window's ink and paper: one line each covers the cells, the
`padding` margin and the leftover pixels at the bottom and right, so changing `background` cannot leave a
frame of a second colour around the text. Neither one overrides an application: a program that asks for a
color of its own gets it, and the 16-color `SGR 40` / `48;5;0` background is palette entry 0 (black), not
`background` — the same split every other terminal has, and worth knowing when a `background =` change seems
to have missed one program's background.

A `#` starts a comment when only whitespace comes before it, or when whitespace comes before **and** after
it. That is what makes `background = #101014` a colour and `value # note` a note, while
`working_dir = /home/me/my#project` keeps its value.

### Key bindings (`kai-term.bind`)

A binding is written as modifiers joined by `+` and one key, for example `Ctrl+Shift+T`, `Alt+Right`,
`Super+Return`, `Ctrl+Shift+F5`. Named keys are the arrows, `Home`, `End`, `Page_Up`, `Page_Down`,
`Insert`, `Delete`, `BackSpace`, `Tab`, `Return`, `Escape`, `Space` and `F1`–`F12`; any other character
stands for itself. `none` (or empty, `0`, `off`, `no`) unbinds an action.

A binding with **no modifier** is refused, so it cannot swallow ordinary typing — with one exception: the
`fm_key_*` bindings, which are matched only while the file manager owns the keyboard. With the file
manager closed, or with the terminal focused, `Up`, `r` and `Escape` reach the shell exactly as before.
`key_tab_prefix` is the other shape: it names modifiers only, and you press them with a digit `1`–`9`.

| Binding | Default | Action |
|---|---|---|
| `key_new_tab` | `Ctrl+Shift+T` | Open another session (`none` unbinds it) |
| `key_close_tab` | `Ctrl+Shift+W` | Close the visible session (`none` unbinds it) |
| `key_next_tab` | `Ctrl+Shift+Right` | Show the next session |
| `key_prev_tab` | `Ctrl+Shift+Left` | Show the previous session |
| `key_tab_prefix` | `Ctrl+Shift` | Hold these and press `1`–`9` to jump to that session (`none` disables it) |
| `key_toggle_layout` | `Ctrl+Shift+S` | Switch between the one-session view and the side-by-side view |
| `key_file_manager` | `Ctrl+Shift+F` | Show / hide the file manager (`none` unbinds it) |
| `key_fm_focus` | `Ctrl+Shift+Up` | Give the keyboard to the browser (`none` unbinds it) |
| `key_fm_release` | `Ctrl+Shift+Down` | Give the keyboard back to the terminal (`none` unbinds it) |
| `fm_key_up` | `Up` | Move the selection up |
| `fm_key_down` | `Down` | Move the selection down |
| `fm_key_pageup` | `Page_Up` | Move up one page — or scroll the preview up one page, when the preview pane has the keyboard |
| `fm_key_pagedown` | `Page_Down` | Move down one page — or scroll the preview down one page, when the preview pane has the keyboard |
| `fm_key_home` | `Home` | Select the first entry — or the first line of the previewed text, when the preview pane has the keyboard |
| `fm_key_end` | `End` | Select the last entry — or the last line of the previewed text, when the preview pane has the keyboard |
| `fm_key_left` | `Left` | Go to the parent directory |
| `fm_key_right` | `Right` | Enter the highlighted directory (does nothing to a file) |
| `fm_key_open` | `Return` | Open the highlighted directory (does nothing to a file) |
| `fm_key_path` | `p` | Type the selected entry's quoted path at the prompt, without running it (`none` leaves no key that can write at your prompt) |
| `fm_key_parent` | `BackSpace` | Go to the parent directory |
| `fm_key_refresh` | `r` | Re-read the current directory |
| `fm_key_preview` | `v` | Show / hide the preview pane |
| `fm_key_hidden` | `.` | Show / hide dotfiles |
| `fm_key_quit` | `Escape` | Close the file manager |

### Example configuration

`~/.config/kai-term/kai-term.conf`:

```ini
font = JetBrains Mono
font_size = 16
cols = 100
rows = 30
background = #1c1c22
opacity = 0.95
```

`~/.config/kai-term/kai-term.bind`:

```ini
key_new_tab = Super+T
fm_key_up = k
fm_key_down = j
fm_key_path = none
```

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| Mouse wheel | Scroll through terminal history |
| `Shift` + `PageUp` / `PageDown` | Scroll by one page |
| `Shift` + `Home` / `End` | Jump to the oldest history line / live bottom |
| `Ctrl` + `+` / `-` | Increase / decrease font size |
| `Ctrl` + `=` / `-` | Alternative font zoom shortcuts |
| `Ctrl` + `0` | Restore the configured font size |
| `Ctrl` + `Shift` + `C` | Copy the selection |
| `Ctrl` + `Shift` + `V` | Paste from the clipboard |
| `Ctrl` + `Shift` + `T` | Open another session in this window |
| `Ctrl` + `Shift` + `W` | Close the visible session (closing the last one exits Kai-Term) |
| `Ctrl` + `Shift` + `Right` / `Left` | Show the next / previous session |
| `Ctrl` + `Shift` + `1`…`9` | Jump to that session |
| `Ctrl` + `Shift` + `S` | Switch between the one-session view and the side-by-side view |
| `Ctrl` + `Shift` + `F` | Show / hide the file manager |
| `Ctrl` + `Shift` + `Up` / `Down` | Give the keyboard to the file manager / back to the terminal |

Dragging with the left mouse button selects text when the running application has not taken control of
the mouse. Releasing the button keeps the selection visible; `Ctrl`+`Shift`+`C` copies it to the system
clipboard, from where any other application can paste it. Set
`copy_on_release = 1` in the configuration file to copy as soon as the button is released (the earlier
behaviour). Text copied in another application pastes into Kai-Term with `Ctrl`+`Shift`+`V`.

Selection works a whole character at a time: a highlighted range always covers complete glyphs, never half of one. Empty space past the end of a line is not highlighted, while spaces between characters are — the copied text is unchanged either way.

When an application enables bracketed paste, multiline clipboard content is sent as a single paste operation rather than being executed line by line.

Applications offer clipboard text under several names, and which bytes arrive depends on the name chosen. Kai-Term asks for the most specific text type it is offered (`text/plain;charset=utf-8`, then `UTF8_STRING`, then `text/plain`) rather than whichever the source happened to advertise last, and it repairs what comes back before typing it: text served as UTF-16 is converted to UTF-8, and NUL bytes are never sent to the shell. This is what makes pasting from applications such as Firefox land as the text you copied instead of as a stray character. Plain `Ctrl`+`V` is not Kai-Term's — it goes to the shell, which reads the clipboard itself.

Holding a key down repeats it: the first repeat arrives after `repeat_delay` milliseconds and further
repeats follow at `repeat_rate` per second. Repeating works for everything typed into the shell — text,
`Backspace`, arrows, `Enter`, `Tab`, `Escape`, function keys, and `Ctrl` combinations — as well as
`Shift`+`PageUp`/`PageDown`/`Home`/`End` history scrolling. The shortcuts that change the terminal itself
(`Ctrl`+`+`/`-`/`0` zoom and `Ctrl`+`Shift`+`C`/`V`) never repeat, so a held key cannot run past a font
size or paste over and over. Releasing the key stops the repeat immediately; setting `repeat_delay = 0`
disables repeating entirely. The file manager's movement keys repeat the same way (holding `Down` walks the
listing); its one-shot keys — open, go up, go into a directory, refresh, preview, dotfiles, close — never do.

### File-manager keys

These belong to the file manager only while it owns the keyboard, and every one of them is configurable
(`fm_key_*` in `kai-term.bind`). With the file manager closed, or with the terminal focused, the same keys
go to the shell unchanged. Clicking the preview pane, or using the wheel over it, gives the keyboard to
that pane.

| Key | Action (browser focused) | Action (preview focused) |
|---|---|---|
| `Up` / `Down` | Move the selection (hold to walk the list) | same — this is how you change which entry is previewed |
| `Page_Up` / `Page_Down` | Move by one page | scroll the previewed text by one page |
| `Home` / `End` | First / last entry | first / last line of the previewed text |
| `Left` | Go to the parent directory | same |
| `Right` | Enter the highlighted directory; on a file it does nothing | same |
| `Return` | Open the highlighted directory; on a file it does nothing | same |
| `p` | Type the selected entry's quoted path at your prompt, at the cursor, without pressing Enter | same |
| `BackSpace` | Go to the parent directory | same |
| `r` | Re-read the current directory | same |
| `v` | Show / hide the preview pane | same |
| `.` | Show / hide dotfiles | same |
| `Escape` | Close the file manager | same |

The mouse wheel scrolls the preview body too, and a preview with nothing to scroll (a picture, or a
metadata-only preview) does not move at all. One page is exactly what the pane shows at once, so a held
`Page_Down` walks a file with no line shown twice and none skipped.

## Sessions and layouts

One Kai-Term window can hold several sessions. Each one runs its own shell and keeps its own scrollback,
selection, scroll position and title; they share the window, the font size and the layout.

With two or more sessions a bar appears at the top of the window (unless `tab_bar = off`). Each item shows
the session number, a `*` when a session you are not looking at has produced output, and that session's
title — the title the shell or application sets for itself, or `shell` until it does. The visible session's
item is highlighted. A left click on an item shows that session; a middle click closes it. In side-by-side
view a click inside a pane that is not the visible one focuses that session.

`Ctrl`+`Shift`+`S` flips between the two views: one session filling the window, and all sessions (up to
`split_max`) side by side as equal-width panes with the visible one bordered. Sessions beyond `split_max`
stay open and stay listed in the bar; the shown group of panes slides to contain the visible one. Every
session gets the same grid size in a given layout, so switching to a session that was hidden costs no
resize and no re-wrap of its scrollback.

Closing the last session exits Kai-Term; a session whose shell exits closes by itself and the others keep
running.

The shortcuts are configurable, in `kai-term.bind`, and a binding is written as modifiers joined by `+` and
one key, for example `Ctrl+Shift+T`, `Alt+Right`, `Super+Return`, `Ctrl+Shift+F5`. Named keys are the
arrows, `Home`, `End`, `Page_Up`, `Page_Down`, `Insert`, `Delete`, `BackSpace`, `Tab`, `Return`, `Escape`,
`Space` and `F1`–`F12`; any other character stands for itself. A binding with no modifier is refused, so it
cannot swallow ordinary typing, and `none` unbinds an action. `key_tab_prefix` is the modifier half of the
`1`–`9` jump: with the default `Ctrl+Shift`, pressing `Ctrl+Shift+3` shows the third session.

## Documentation

- [ISSUES.md](ISSUES.md) — open problems (currently none) and known limitations.
- [AGENT.md](AGENT.md) — Development journal: current state, design invariants and known pitfalls.
- [LICENSE](LICENSE) — MIT License.

## License

Kai-Term is licensed under the MIT License.
