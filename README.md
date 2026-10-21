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
- **Text and colors** — UTF-8 text, 16-color, 256-color, and 24-bit truecolor support. The ANSI 16, the
  ink, the paper, the cursor and the selection highlight come from a theme file — kitty's format is read
  unchanged, and `kai-term.conf` keeps the last word on any colour it mentions itself.
- **A blinking bar cursor** — A thin vertical bar at the left edge of the cell, on for one second and off for
  one second (both the width and the half-period are settings, and `cursor_blink = 0` makes it steady). It
  stops blinking while the window has no focus. A program can ask for a different shape with DECSCUSR
  (`ESC[1 q` block, `ESC[3 q` bar, `ESC[5 q` underline) — what `vim`, `neovim` and `fish` do — and can ask
  for a steady cursor, but it cannot switch the blink back on once you turned it off.
- **A bell you can see** — Kai-Term has no audio, so a `BEL` is drawn instead: that session's field washes
  briefly toward the text colour and fades out. It is how `less` and `man` say a command was unknown or that
  there is nothing further to scroll to, how `sudo` says the password was wrong, how the shell says a `Tab` is
  ambiguous and how `vim` says no. In
  side-by-side view only the pane whose program rang washes, and a session you are not looking at is marked
  by the tab bar's `*` instead — `bell = off` draws nothing at all.
- **Scrollback** — Retains history (2000 lines by default) and scrolls with the mouse wheel or the
  keyboard; new output snaps the view back to the live bottom.
- **Full-screen applications** — The alternate screen (`vim`, `htop`, `less`, `nano`) is supported; the
  main screen is parked while an app owns it and comes back re-wrapped when the app exits. Kai-Term also
  honours application cursor keys (DECCKM), so the arrow keys work in programs that ask for them — `less`,
  `man` and `htop` answer only that form.
- **Clear scrollback** — `clear` can remove both visible content and terminal history.
- **Mouse support** — Application mouse reporting, text selection (drag, or a double click for a word and a
  triple click for a line) in the terminal AND in the file manager's preview pane, and clipboard
  integration.
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
  The preview pane's text is selectable too — drag, or a double click for a word and a triple click for a
  row — and `Ctrl`+`Shift`+`C` copies it with the terminal's rule for what a word is.
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
- **Ask before quitting** — Closing a window or a session hangs up its shell, so anything still running in it
  dies with it. By default Kai-Term asks first, but only when a session really has a command in the
  foreground of it: an idle window still closes the moment you ask it to. Closing a **window** asks with a box
  in the middle of the screen that names the process — `Warning ! "nano" is still running. Are you sure you
  want to close it ?` — and you answer it by clicking `Yes`/`No` or from the keyboard. Closing a **tab** asks
  in one row at the bottom of the window. `confirm_quit = always` asks at every close, `confirm_quit = never`
  never asks.
- **System clipboard** — Copy and paste use the standard Wayland clipboard, so text copied in Kai-Term
  pastes into other applications and text copied anywhere else pastes into Kai-Term.
  `middle_click_paste = 1` (off by default) also pastes it with a **middle click** in the terminal area, and
  changes what a middle click means nowhere else.
- **Configuration** — Two plain-text files: `kai-term.conf` for the settings and `kai-term.bind` for the
  key bindings, read from `$XDG_CONFIG_HOME/kai-term`, `~/.config/kai-term` or `/etc/kai-term`, with a
  commented template of every default in [`config/`](config) in the source tree. A third kind of file,
  `NAME.theme`, holds colours only.

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
| — | `--import-theme FILE` | Import a theme written for another terminal, write it into `themes/`, and exit |
| — | `--force` | With `--import-theme`: overwrite an existing theme file instead of asking |
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
| mouse left click | in the **browser**: select the row under the pointer, and a **second click on the same row** opens it (a directory is entered; a file does nothing). In the **preview pane**: select the text under the pointer — drag to extend it, a **double click** takes a word and a **triple click** the row. A click in the terminal pane hands the keyboard back |
| mouse wheel | over the browser it walks the listing; over the preview pane it scrolls that pane's contents (a file's text, or a directory's entries); over the terminal it scrolls history as usual — except inside a full-screen program that never asked for the mouse, where it becomes cursor keys (see below). It does **not** move the keyboard (see below) |

`Ctrl`+`Shift`+`Up` gives the keyboard to the browser, `Ctrl`+`Shift`+`Down` gives it back to the terminal.

**A pane of the band is focused by clicking it.** Where the pointer sits never takes the keyboard away from
the terminal on its own, and a scroll gesture over the band only scrolls — so you can read through a listing
with the wheel while the command line keeps the keyboard. `fm_focus_on_hover = 1` restores the older
behaviour, where a scroll gesture over a pane also moves the keyboard to that pane.

The **preview pane** takes the keyboard on a **click**: while it has the keyboard
`Page_Up`/`Page_Down` scroll the previewed text by one page and `Home`/`End` jump to its
first and last line — which is what makes a long file readable in the pane that exists to read it. One page
is exactly what the pane shows at once, so holding `Page_Down` walks a file with no line shown twice and
none skipped. `Up`/`Down` keep moving the browser's selection there, because that is how you change which
entry is being previewed. A preview with nothing to scroll — a picture, or a metadata-only preview — does
not move at all.

**The preview pane's text is selectable.** Drag the left button over a previewed file — its text, a
directory's entry list, or the two header lines — and the range is highlighted; `Ctrl`+`Shift`+`C` copies it.
A **double click** selects the word under the pointer and a **triple click** the whole row, using exactly the
terminal's rule for what a word is (see the selection notes under **Keyboard shortcuts**), so the gesture
means one thing wherever you make it in the window; `click_selects_word = 0` turns it off in both places.
The highlight is the same colour as every other selection (`selection_color`) and changes only the
background, so a syntax-coloured keyword stays readable under it, and it never covers the blank space past
the end of a line. What reaches the clipboard is the file's own text: the pane's two-column indent is not
part of it, and a row is copied as one line — the preview cuts a long line at the pane's right edge rather
than wrapping it, so a line wider than the pane copies the part that row shows and the rest of it is not
reachable by the mouse (the pane never scrolls sideways; widen the band if you need the tail). Scrolling the
pane, resizing it or previewing another file clears the selection, since in every one of those cases the range
would name different text. There is one selection at a time in the window: a click in the preview pane drops
the terminal's highlight and a click in a terminal pane drops the pane's, so `Ctrl`+`Shift`+`C` never has to
guess which one you meant.

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
- **A program that cannot honour one of those presses answers with a `BEL`, and Kai-Term does not flash for
  it.** A pager like `less` keeps its cursor on its own status line, so a click in its text area becomes "press
  Up N times"; at the top of the file that is a scroll before the beginning, which `less` reports with a bell.
  Kai-Term typed the keys, so that bell is not a message to you and the pane stays as it is. The wheel keeps
  its bell: there you asked to scroll, and "there is nothing further" is addressed to you.

A program that *did* ask for the mouse — `vim`, `htop`, `tmux`, `nano --mouse` — is untouched by all of this:
it keeps receiving mouse reports, including the wheel, and Kai-Term sends it the wheel code those programs
understand (64/65). The main screen keeps scrolling its scrollback. The cursor keys themselves now follow the
program's own request (DECCKM), which is a fix on its own: `less`, `man` and `htop` answer only the form they
ask for with `ESC[?1h`, so the arrow keys did nothing in them before.

### Middle-click paste

`middle_click_paste = 1` makes a **middle click paste the clipboard** — the same text `Ctrl`+`Shift`+`V` pastes,
which is whatever the compositor's clipboard holds, including another application's. It is **off by default**: a
middle click does nothing in the terminal area until you turn it on. Where the middle button already belongs to
something, it keeps belonging to it. The window asks in one order, and the first surface that claims the click
serves it:

| A middle click lands on | What happens |
|---|---|
| the confirm-quit box, while it is up | the box owns the pointer: a press on a button answers the question (as it does for any button), anywhere else it answers nothing |
| a session's item in the tab bar | that session **closes** — asking first, if something is still running in it |
| the file-manager band | the entry under the pointer is picked |
| a program that asked for the mouse (`vim`, `tmux`, `less`, `nano --mouse`) | the button is reported to that program — it asked for it |
| anywhere else in the window (the panes and the space around them, where a left click already selects text) | the clipboard is pasted |

Two things worth knowing:

- **It is the clipboard, not the X11 primary.** Kai-Term has no primary selection, so this gesture and
  `Ctrl`+`Shift`+`V` hand over the same bytes, and highlighting text with the mouse does not by itself make it
  middle-click pasteable — `Ctrl`+`Shift`+`C` is what puts a selection on the clipboard.
- **The press pastes; the release does nothing**, so one click cannot paste the same text twice. A paste leaves
  any selection that is on screen exactly where it is. `./bin/kai-term -d` logs which gesture asked the clipboard
  for its text, and why it got nothing back when it does (no clipboard device on the seat, a paste already in
  flight, or a clipboard holding no text).

## Configuration

Kai-Term reads two plain-text files, both in the same `key = value` format:

| File | Holds |
|---|---|
| `kai-term.conf` | the settings — font, grid, colours, shell, mouse, sessions, file manager, logging |
| `kai-term.bind` | the key bindings — every `key_*` and `fm_key_*` name |
| `NAME.theme` | the colours of a theme, if `kai-term.conf` names one with `theme = NAME` |

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

A theme is searched by **file name** in the same three locations, in their `themes/` subdirectory —
`$XDG_CONFIG_HOME/kai-term/themes/`, `~/.config/kai-term/themes/`, `/etc/kai-term/themes/` — and again the
first hit wins. See [Themes](#themes-theme-files) below.

### Templates

[`config/kai-term.conf`](config/kai-term.conf), [`config/kai-term.bind`](config/kai-term.bind) and
[`config/kai-term.theme`](config/kai-term.theme) in the source tree list every name with its built-in
default and say what it does. Copy them to one of the locations above and change what you want — a line
left at its default does exactly nothing. The test suite loads all three and compares every value with the
compiled-in defaults, and it checks the name lists in both directions, so the templates cannot drift from
the program.

### Settings (`kai-term.conf`)

| Setting | Default | Description |
|---|---|---|
| `font` | `monospace` | Fontconfig font pattern |
| `font_size` | `14` | Font size in pixels |
| `cols` | `80` | Initial number of columns |
| `rows` | `24` | Initial number of rows |
| `scrollback` | `2000` | History lines to retain. The grid allocates `rows + scrollback` lines once at startup, so a window taller than `rows` keeps fewer of them (`rows + scrollback - current_rows`) |
| `theme` | Empty | A theme file of colours: a name looked up as `NAME.theme` in the `themes/` directory of the three config locations, or a path (with a `/`, or starting with `~`). See [Themes](#themes-theme-files) |
| `foreground` | `#e5e5e5` | Default text color — the ink of every cell an application has not colored itself |
| `background` | `#101014` | Default background color — the one value behind everything an application has not colored: the cells, the `padding` margin, and the pixels left over by whole cells at the bottom and right |
| `cursor_color` | `#d8d8d8` | Cursor colour. A session that is drawn in the window but is not the one the keyboard talks to gets the same colour dimmed toward `background` |
| `selection_color` | `#3a4d66` | The selection highlight — and with it the tab bar's active item, the focus border, and the selected row in the file manager's band. One value, so the highlight always means the same thing |
| `osc_colors` | `1` | Whether a program running in the terminal may repaint the terminal itself: OSC 4 (set a palette entry), OSC 10/11 (set or ask for the default ink and paper), OSC 104 (put palette entries back). This is what lets a `pywal`-style tool theme a live Kai-Term with no file at all. `0` ignores all four. Note what `1` means: an OSC 10/11 overrides `foreground`/`background` for the rest of the run, and it changes the whole window and every session in it, because the default ink and paper are one value on purpose. `kai-term -d` logs every colour an application changed |
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
| `click_selects_word` | `1` | A second left click in the same cell selects the **word** under the pointer, a third the whole **line** (`0` leaves every click selecting one cell, extended by dragging). A word is everything up to the next space with punctuation at the END cut off, so double-clicking `file.txt,` selects `file.txt`; punctuation at the START stays, so `/etc/passwd` comes over whole. The line a triple click takes is the line the program wrote, so one that wrapped at the window edge is selected in every row it occupies. A word or line gesture never sends the program cursor keys, so it selects inside `nano` and `less` instead of walking their cursor twice |
| `middle_click_paste` | `0` | `1` makes a **middle click in the terminal area** paste the clipboard (`Ctrl`+`Shift`+`V` does that either way). Off by default, and off it changes nothing: a middle click in the terminal area does nothing at all. It never changes what a middle click means anywhere else — on a tab in the bar it still **closes** that session (asking first if something is running), in the file-manager band it still picks a file, and a program that asked for the mouse (`nano --mouse`, vim, `less`, tmux) keeps its middle button because it asked for it |
| `bell` | `visual` | What a `BEL` does. `visual` washes that session's field briefly toward the text colour and fades it out; `off` draws nothing. Kai-Term has no audio, so this is the only bell there is — and `BEL` is how `readline` (an ambiguous Tab), `less`/`man` (an unknown command, or scrolling past the end of the file), `sudo` (a wrong password) and vim (an error) say "no". A `BEL` that answers the cursor-key presses a click became is not flashed — Kai-Term caused it. The flash covers that session's own pane (in side-by-side view only the pane whose program rings flashes), sits **under** the text so the line stays readable, and never touches the window's margin. `1`/`on` and `0`/`off` are accepted spellings; any other word warns and keeps the current value |
| `cursor_blink` | `1000` | Cursor blink half-period in ms: the bar is drawn for this long, then hidden for this long (`0` = steady). Your setting is the ceiling — a program can ask for a steady cursor with DECSCUSR but cannot switch the blink back on. Blinking also stops while the window has no focus (clamped to 0–10000) |
| `cursor_width` | `2` | Pixels wide the bar cursor is — and how tall an underline is, when a program asks for one. Never wider than a third of a cell, so a large value cannot turn the bar back into a block (clamped to 1–64) |
| `repeat_delay` | `300` | Milliseconds a key must be held before it starts repeating (`0` disables repeating) |
| `repeat_rate` | `30` | Repeats per second once a key starts repeating |
| `max_tabs` | `8` | Sessions one window may hold (clamped to 1–64) |
| `tab_bar` | `auto` | `off` never draws the tab bar, `auto` draws it from the second session on, `always` also with one session |
| `split_max` | `4` | Sessions drawn side by side at most; more sessions stay open and stay listed in the bar |
| `confirm_quit` | `running` | What closing asks you before it throws work away. Closing a window or a tab hangs up that shell, so anything still running in it dies with it. `running` asks only when a session has a command in its **foreground** (a pipeline counts; a job you started with `&` does not, because the shell keeps the foreground group then), so an idle window still closes instantly; `always` asks at every close of the window and of a tab; `never` closes straight away. A window close names the running **process** and asks in a box you can click; a tab close asks in one row. When more than one session is at risk the sentence says how many and which, and answering yes closes all of them |
| `file_manager` | `0` | Show the file-manager band from startup instead of only when the shortcut is pressed |
| `fm_size` | `75` | Percent of the window's text area the band asks for (clamped to 10–95, and shrunk further so the terminal keeps at least 3 rows; if even that does not fit, the band is dropped for that frame) |
| `fm_preview` | `1` | Show the preview pane next to the browser (`v` toggles it at runtime; off means the browser is full width) |
| `fm_show_hidden` | `0` | Include dotfiles in the listing from the start (`.` toggles it at runtime) |
| `fm_follow` | `1` | Browsing into a directory also sends `cd '<path>'` to the shell |
| `fm_focus_on_hover` | `0` | Whether the pointer's position moves the keyboard into a band pane. `0` = a pane is focused by clicking it; scrolling over the band still scrolls it |
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
`working_dir = /home/me/my#project` keeps its value. The same rule applies in `kai-term.bind` and in a
theme file — one rule, one function, because getting it wrong once silently ate every `#RRGGBB` value in
the file.

### Themes (`.theme` files)

A theme is a file of **colours only**. Fonts, sizes, key bindings and everything else stay in
`kai-term.conf` and `kai-term.bind`.

```
# ~/.config/kai-term/kai-term.conf
theme = 13th-floor            # reads ~/.config/kai-term/themes/13th-floor.theme
theme = /Data/Themes/dusk.theme   # a value with a '/' (or starting with ~) is a path, read as given
```

The syntax is the one `kai-term.conf` uses — `key = value`, `#` comments, and the same colour forms
(`#RRGGBB`, `0xRRGGBB`, `RRGGBB`, `AARRGGBB`). Kitty's `key value` spelling (no `=`) is accepted too, so an
existing kitty theme loads unchanged:

```
name 13th Floor
color0 #bdaaad
color1 #e07f70
background #4a0710
foreground #f2e2c8
cursor #e48e81
selection_background #f2e2c8
```

What a theme may set:

| Name | Also accepted as | Sets |
|---|---|---|
| `foreground` | | the default ink |
| `background` | | the default paper — cells, `padding` margin, leftover pixels |
| `cursor` | `cursor_color` | the cursor, and the dimmed cursor of a session that is not the visible one |
| `selection_background` | `selection_color` | the selection highlight, the tab bar's active item, the focus border, the file manager's selected row |
| `color0` … `color15` | | the ANSI 16, which applications reach with `SGR 30-37`/`90-97` and `SGR 38;5;0-15` |
| `name` | | metadata: what the log calls the theme. Changes nothing on screen |

What a theme may **not** set: palette entries 16-255. The 6x6x6 colour cube and the grey ramp are computed,
and an application that asks for 196 means the cube's brightest red — a theme that could repaint the cube
would make every published 256-colour chart a lie about this terminal. Two names from other programs,
`selection_foreground` and `cursor_text_color`, are recognised and reported as dropped: Kai-Term draws the
ink of a selected cell, or of the glyph under the cursor, in that cell's own colour.

**Precedence.** The theme is applied after `kai-term.conf` has been read in full, and only for the colours
that file did not mention. So a `background =` line in `kai-term.conf` always wins, whether it sits above or
below the `theme =` line. `kai-term -d` logs the outcome per colour: `theme: background = #4a0710
(/home/you/.config/kai-term/themes/13th-floor.theme)`, or `theme: cursor from … ignored — kai-term.conf
sets cursor_color`.

[`config/kai-term.theme`](config/kai-term.theme) is the shipped example: every name above with the colour
Kai-Term uses when there is no theme at all.

**An application can also change the colours of a running window** — `osc_colors = 1` (the default) accepts
OSC 4 (set a palette entry, one or several in one sequence), OSC 10 and OSC 11 (set, or ask for with `?`, the
default ink and paper) and OSC 104 (put palette entries back, all of them or just the ones named). That is what
lets a `pywal`-style tool theme a live Kai-Term with no file at all. Three consequences worth knowing:

- Unlike a theme file, OSC 4 may repaint entries 16-255 (the colour cube and the grey ramp). A file must not —
  an application that asks for 196 means the cube's brightest red — but an application asking for it is the
  normal case, and `osc_colors = 0` is the switch if you do not want that.
- OSC 10/11 override the `foreground`/`background` lines for the rest of the run, and they change the whole
  window and every session in it: the default ink and paper are one value on purpose, so the cells and the
  `padding` margin can never disagree (see the note on `background` above).
- `OSC 104;N` restores only the entries you name, so a tool can borrow two colours and give them back without
  wiping a theme you set in a file.

`kai-term -d` logs every one of these: `osc: OSC 11 set the default background to #082135 — the kai-term.conf
background line no longer describes this window`. With `osc_colors = 0` the four sequences are ignored, colour
queries get no answer, and the title sequences (OSC 0/1/2) keep working — the switch is about colours only.

**Importing a theme written for another program.** `--import-theme FILE` reads one file, writes it into the
first writable `themes/` directory of the config search, and exits: no window, no shell, no root.

```
$ kai-term --import-theme ~/Downloads/Breeze.colorscheme
imported /home/you/Downloads/Breeze.colorscheme
  -> /home/you/.config/kai-term/themes/Breeze-Dark.theme
enable it in kai-term.conf:  theme = Breeze-Dark
```

The format is decided by the file's CONTENT, never by its name. Read: Kai-Term's own and kitty's spelling,
Xresources (`XTerm*color1: #dc322f`), `alacritty.toml`, Konsole `.colorscheme` (the modern sections and the
legacy `[Color Scheme]`), xfce4-terminal `terminalrc`, and a Windows Terminal / VS Code JSON scheme. Refused,
with the reason in the log: an iTerm2 `.itermcolors` (its colours are 0.0-1.0 floats in an XML plist, and a
reader nobody here can check against iTerm2 is worse than a refusal that says what it looked for) and anything
nothing matched. Kai-Term never guesses a format, and never guesses a colour either — an X11 name like `red`
is reported as dropped (`import:1: 'background' is not a colour Kai-Term can read …`) rather than
approximated, and the rest of the file still imports.

When a theme of that name already exists Kai-Term asks instead of deciding:

```
Warning: a theme with the same name already exists. Overwrite? [y/N]
```

`y` overwrites; anything else, or no terminal to answer from (a script, a closed stdin), writes nothing and
exits `1` with `nothing written. Re-run with --force to overwrite without asking.` `--force` skips the
question. The written file is Kai-Term's own format, so an imported theme is read afterwards by the same
reader as a hand-written one; the name it lands under is the theme's own `name` (or the source file's stem),
made safe — `Breeze Dark` becomes `Breeze-Dark`, and a name longer than 127 bytes is cut with a log line
saying so.

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

Selection works a whole character at a time: a highlighted range always covers complete glyphs, never half of one. Empty space past the end of a line is not highlighted, while spaces between characters are — the copied text is unchanged either way. The file manager's preview pane has its own selection, with the same gestures and the same rule for what a word is (see "File manager").

A **double click** selects the word under the pointer and a **triple click** the whole line; `click_selects_word = 0` turns both off. Two clicks count as one gesture when the second press lands in the same cell within 500 ms — the same window the file manager's double-click-to-open uses, so the two gestures feel like one thing. What a word is follows the rule you would expect: the run of characters up to the next space, with punctuation stuck to the END cut off (`file.txt,` and `word"` give `file.txt` and `word`) and punctuation at the START kept (`/etc/passwd`, `(paren`). A wide character — a CJK glyph or an emoji — is never cut in half, and the line a triple click takes is the line as the program wrote it: a line that wrapped at the window edge is selected across every row it occupies, and a line the program ended with a newline stops there. Because a word or line gesture is a selection, it never synthesises the cursor-key presses of `click_moves_cursor` — double-clicking inside `nano` or `less` selects a word instead of walking their cursor two columns. One limit worth knowing: a word so long that the window itself broke it is selected up to the break on the row you clicked; the triple click is what takes a wrapped line whole.

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
go to the shell unchanged. Clicking the preview pane gives the keyboard to that pane (the wheel over it
scrolls it without taking the keyboard, unless `fm_focus_on_hover = 1`).

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

### Asking before a close throws work away

Closing a window closes its pty and hangs up every shell in it, so a compiler, an editor with unsaved text or
a running server dies with it. `confirm_quit` says when Kai-Term checks with you first:

| value | what happens |
|---|---|
| `running` (default) | asks only when a session has a command in its **foreground**; an idle session closes at once |
| `always` | asks at every close of the window and of a session, idle or not |
| `never` | closes straight away, as earlier builds did |

The two kinds of close ask differently, on purpose.

**Closing a window** asks with a box in the middle of the window, drawn over the terminal, so the work you are
being asked about stays exactly where it was — the box costs the terminal no rows. It names the process by its
own name, read from the foreground process group's entry in `/proc`:

    Warning ! "nano" is still running. Are you sure you want to close it ?

`Yes` and `No` are buttons you can click; `Enter`/`y` and `Esc`/`n` answer the same two ways from the
keyboard. While the box is up the keyboard and the pointer belong to it: no key reaches the shell, a click on a
button answers, and a click anywhere else answers nothing — a click near a button is not an answer, and a click
on the terminal is not a `No` you did not give. Several sessions are listed by name, at most three of them and
then `and 2 more`:

    Warning ! "nano", "sleep" and "htop" are still running. Are you sure you want to close them ?
    Warning ! "a", "b", "c" and 2 more are still running. Are you sure you want to close them ?

A process whose name cannot be read is called `session 3` rather than guessed at. Confirming a window close
closes **every** session named, not just the visible one.

**Closing a tab** asks in one row of chrome at the bottom of the window, in the same highlight colour as a text
selection, and that row costs the terminal one row while it is up:

    A command is running here. Close it?  [Enter] yes  [Esc] no

A tab is cheap to open again, so its question stays short and stays out of the way.

What counts as "running" is measured, not guessed: Kai-Term reads the terminal's foreground process group.

- A command at the prompt, and a pipeline, count — in bash, zsh and fish alike.
- A job you started with `&` does **not**: the shell keeps the foreground group, so the gate protects a
  foreground command rather than a background daemon.
- A session started with `-e COMMAND` does not either. There the command *is* the session's shell process, so it
  owns the foreground group and reads idle — measured with `sleep 30` and with `sleep 30 | cat`. If you run
  `kai-term -e something` and want to be asked, set `confirm_quit = always`.
- A session whose shell has already exited does not count either, so Kai-Term never asks whether to kill
  nothing.
- The state converges within a second or two of a command starting, so a window can be asked about when the
  command had barely begun; the log (`kai-term -d`) prints the question and the answer either way.
- A question you cannot see never blocks a close. A window too small to hold the box and its two buttons, or a
  tab row too short to give up its row, closes without asking: the terminal wins over the question, exactly as
  it wins over the tab bar and the file-manager band.

## Documentation

- [ISSUES.md](ISSUES.md) — open problems (currently none) and known limitations.
- [AGENT.md](AGENT.md) — Development journal: current state, design invariants and known pitfalls.
- [LICENSE](LICENSE) — MIT License.

## License

Kai-Term is licensed under the MIT License.
