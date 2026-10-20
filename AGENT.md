# Kai-Term — Agent Journal

Read this + ISSUES.md before touching the project. User-facing docs: README.md. Current problems: ISSUES.md
"Open". Root causes of long-fixed bugs: git history, not this file — `git show d5afe4a:ISSUES.md`.
Host/tool facts: `/home/kaida/Project/ENVIRONMENT.md` (outside the repo on purpose — not project documentation).

Renamed 2026-10-16 (s59): `K-Term`/`k-term` -> `Kai-Term`/`kai-term` (dir, header, config path, app id). Old
entries and git history keep the old spelling — they record what was really run. Pre-rename journal:
`git show 1ab9dc8:AGENT.md`.

**What this file is.** A journal plus the rules a change must not break — not a changelog. Per-session
narrative collapses into era entries and the step-by-step detail (including every failed probe and wrong
theory) goes to git history; that is the rule ISSUES.md has followed since s66, applied to this file in s63,
s65, s87, s100 and s106 (1942 -> 1459 lines, 183,517 -> 137,411 bytes; the pre-cleanup text is in
`.scratch/s106/AGENT.md.pre-cleanup`). What stays is what constrains a change: the status, the plan, the
invariants, the diagnosis reference and the pitfalls. What goes is session narrative.
Dates in older entries run ahead of the host clock — **git log is the authority for order, not those dates.**

## Project Overview

Wayland terminal emulator in C. xdg-shell window + PTY shell + VT/ANSI parser + EGL/OpenGL text rendering,
2 glyph atlases (alpha8 mono, RGBA8 color emoji) plus one GL texture per application picture (kitty graphics
protocol), two vertex batches per frame with the pictures drawn between them. In-window multiplexing: N
sessions per window, tab bar, side-by-side panes. No GUI framework.

## Current Status (re-measured 2026-10-19, s110 — build, every suite and `make asan` all green)

- **Build:** `make clean && make -j8` exit 0, **0 warning lines** under `WARN` (`-Wall -Wextra -Wshadow
  -Wwrite-strings -Wformat=2 -Wdiscarded-qualifiers`) — and the same for
  `make bin/test_vt bin/dbg bin/test_fm bin/test_render`. **Measure BOTH**: `make -j8` builds only `src/`, and
  the s103 lesson was a test build carrying a warning that made one palette check feed the wrong bytes while
  still passing. Command worth copying:
  `make clean && make -j8 2>&1 | grep -ciE 'warning|error'`, then
  `make bin/test_vt bin/dbg bin/test_fm bin/test_render 2>&1 | grep -ciE 'warning|error'`.
- **Suites** (`KAI_TERM_TEST_NO_WINDOW=1 make test`, exit 0): `CLI: 91 checks, 0 failures, 13 skipped`,
  `./bin/test_vt` **1133 checks, 0 failures**, `./bin/dbg` **MODEL OK (0 problems)**, `./bin/test_fm` **305
  checks, 0 failures**, `./bin/test_render` **25 checks, 0 failures** (the renderer's pure colour decisions —
  no display, no GL context). With a display `tests/cli_test.sh` reports **141 checks, 0 failures, 0 skipped**
  (measured again in s109; the 50 window checks are the difference from the headless 91). What only its
  windowed group covers is `main()`'s own argv handling (`-c/--config`, `-b/--binds`, combined flags, `-d`
  beating `log =`, `-w`, `-e`) and that a real launch applies the multiplexing / display / file-manager keys
  and the band's share. The config SEARCH order and the split-by-name rules are NOT exclusive to it —
  `tests/test_vt.c`'s `cfgsearch` group and `config split` group cover those headless — and neither is
  `--import-theme`: the import path needs no display, so s107 pinned it in the part of the script that always
  runs. **Re-diff these numbers, do not copy them.**
- **`make asan` is GREEN — exit 0, 0 warning lines, no LeakSanitizer report** (s107). It was red when s106
  closed, and both halves of the red came in with the uncommitted 1e work: two `-Wformat-truncation` warnings
  in `src/theme.c` (`snprintf(v->name, sizeof v->name, "%s", val)` — a `val` of up to 1023 bytes into a
  128-byte field, visible ONLY in the asan build where `import_kv` is inlined and the compiler can bound the
  argument), and 134 bytes in 18 allocations leaked by the option-pinning harness, which mallocs the names it
  reads out of `usage()` and README and never freed them. The warnings went by way of `set_name()` — one
  function that cuts at `sizeof v->name - 1` and LOGS the cut, because the cut decides the file name an
  imported theme lands under; the leak went by freeing the two name lists. Both fixes are pinned: the C check
  asserts the exact 127-byte stem, the CLI group greps the log line.
- **Docs are pinned to the code by check, not by eye:** `tests/test_vt.c` reads the **33** setting names and the
  **24** binding names out of the two strcmp ladders in `src/config.c`, the **23** theme names out of
  `src/theme.c`'s `THEME_KEYS` table, and the **9** long options out of `usage()` in `src/main.c`, and compares
  each set with `config/kai-term.conf` + `config/kai-term.bind` + `config/kai-term.theme` + README's tables —
  the option table by `readme_opts_of()`, README's **settings** and **binding** tables by
  `readme_table_names_of()` + `names_diff()` (added s110: before it, deleting a settings row from README left
  every check green, so that half of this sentence was a claim and not a check). A
  name added to a parser without a template line, a template line no parser knows, and a deleted template line
  each fail a check. Never hand-copy a name list into a test — derive it from the source.
- **No leftover probe or experiment in the tree, and no `TODO`/`FIXME`/`XXX`.** `grep -rnE
  "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/ include/ tests/` returns only the three lines in
  `tests/test_render.c` that WRITE a probe file — the token is split by string concatenation there on purpose, so
  the scanner can never count its own source (see the s103 entry). `grep -rn "TODO\|FIXME\|XXX"` returns only
  `mkdtemp`'s `kai-term-int-XXXXXX` template and one test's fill string. If that ever changes, re-word this line
  rather than leave a claim that no longer matches the grep.
- **No `install` target**, and nothing copies `config/*` to `/etc/kai-term` — deliberate; README tells the user
  to copy the templates. Adding one is the user's call.
- **Git:** `origin = https://github.com/K-code-AI/Kai-Term`, branch `main`. s90-s104, 1e (s107), s109 and s110
  are pushed (verified 2026-10-19: `git status -sb` shows no ahead/behind).
  Read the remote state with `git status -sb` / `git log --oneline origin/main -1`, never from a hash written
  here. `git fetch` before push — the user commits on GitHub too. Never commit `bin/` or `build/`.
- **Owed to the user's eye, not to a harness:** the look of a themed window, a live OSC colour repaint, and the
  configured `cursor_color` — those three have never been confirmed in the user's window. (`--import-theme` and
  the theme file itself HAVE been: the user confirmed the import works, 2026-10-19.) Recipe for the theme: put
  their kitty theme at `~/.config/kai-term/themes/13th-floor.theme`, add `theme = 13th-floor` to
  `~/.config/kai-term/kai-term.conf`, then `printf '\033]11;#082135\007'` — the paper changes and `-d` logs that
  an application set it, not the file. Hand these to them; do not map a window to judge a look yourself.

## Current Task — step 2, the cursor bar: **in progress (s111)**. Plan steps 3-7 are next

The s102 plan (steps 1-7 below) is still the plan. All eight open questions were answered by the user
2026-10-22 and the answers change four details: no `cursor_style` key, no primary-selection work, plain-click
URLs, an interactive theme-overwrite prompt. The answers are recorded under "Decisions already made" — do not
re-ask them, and do not re-propose what was dropped.

### Where each step stands

- **1a — palette is process-wide runtime state (DONE).** `g_palette[256]` in `src/vt.c` with `term_palette()` /
  `term_set_palette_entry()` / `term_palette_reset()`, same pattern as `g_def_fg`/`g_def_bg`. It had to leave the
  `Term` struct because `term_init()` memsets `Term`: a per-Term palette is refilled from the built-in base16 by
  every new tab and every `ESC c`, so a theme in a file would have applied to the first session only. Only
  entries **0-15** are themeable; the cube (16-231) and the ramp (232-255) stay computed. `ESC c`/RIS
  deliberately do NOT wipe the palette — `term_palette_reset()` is reserved for OSC 104's meaning.
- **1b — the 6x6x6 cube's top level wrapped to near-black (FIXED).** The formula was `55 + 41 * level`, so level
  5 gave **260**, and `260 << 16` leaves `0x04` in the channel: 91 of 216 entries. Measured pre-fix: entry 196
  (bright red in every chart) `ff040000`, 226 `ff050400`, 231 `ff050504` — `SGR 38;5;196` painted near-black, so
  vim/neovim colorschemes, htop, bat, delta and pygments drew dark where they mean bright. Alpha stayed `0xff`
  because the carry landed on a bit already set, which is why no check could ever have noticed. Fix: the six
  levels are a table `{ 0, 95, 135, 175, 215, 255 }` — a table cannot overflow.
- **Step 0 — the dead `cursor_color =` (FIXED).** `Render.cursor_color`, passed from `main.c`, derived in the
  pure `render_chrome()`, checked at the consumer in `tests/test_render.c`. The lesson is in Key Decisions:
  a key that is defaulted, parsed, stored and parse-tested but read by nothing leaves the whole suite green
  while the feature is absent.
- **1c — a `.theme` file, `theme =`, conf-beats-theme precedence (DONE, `c84258a`).** `src/theme.c`: 23 names in
  one `THEME_KEYS` table, `theme_find()` resolving through the `themes/` subdirectory of the same three config
  directories, `config/kai-term.theme` as the shipped example. A theme is COLOURS only, in kai-term.conf's
  `key = value` and in kitty's `key value`, so an existing kitty theme loads unchanged. Entries 16-255 are
  refused in a FILE (the cube and ramp are computed) while OSC 4 may set them — a decision with a check on each
  side. Precedence: the theme is applied once, after `kai-term.conf` has been read in full, and only for names
  that file never mentioned (`Config.set_keys`, written on `apply()`'s path), so a `background =` line wins
  above OR below the `theme =` line; both orders are checked.
- **1d — OSC 4 / 10 / 11 / 104 behind `osc_colors = 1` (DONE, same commit).** OSC 4 sets a palette entry (single
  and `N;c;M;c` multi form); OSC 10/11 set the default ink/paper and answer `?` in the X11 16-bit form; OSC 104
  restores the whole palette or named entries. Before it, `osc_end()` knew OSC 0/1/2 only — nothing an application
  could use to repaint anything. Colours go through the ONE parser `cfg_parse_color` plus X11
  `rgb:RR/GG/BB` and `rgb:RRRR/GGGG/BBBB`; X11 NAMES are refused. `term_palette_builtin_entry()` is the single
  source for a built-in colour (`term_palette_entry_reset()` needed one for `OSC 104;N`, and a second palette
  table is a copy that can drift). `term_recolor_defaults()` (vt.c) is one dumb match-and-replace over the live
  grid, the parked MAIN grid, the pen and the prompt snapshot — an unswept snapshot makes `prompt_cell_same()`
  report "the prompt changed" forever and the shell follow dies for the session; it is idempotent,
  which is what lets `mux_recolor_defaults()` sweep every session without excluding the one that raised the
  change. Why it must sweep: a `Cell` stores a RESOLVED colour, so moving only `g_def_fg`/`g_def_bg` repaints
  only FUTURE output (half-themed window) — and why it must NOT skip blanks: `cell_pristine()` decides "never
  written" against the CURRENT defaults, so skipped blanks would look blank yet no longer be pristine,
  `row_used()` would count trailing blanks as content, reflow would join lines it must not, and a selection band
  would appear over nothing. Also found here: the first `rgb:` converter "rounded" a 16-bit component with
  `(d + 0x80) >> 8`; `0xffff + 0x80` is `0x1007f`, `>> 8` gives **256**, and `256 << 16` puts `0x01` in the GREEN
  byte, so `rgb:ffff/ffff/ffff` — the canonical X11 spelling of WHITE — painted `#010100`. Same defect class as
  the cube. Fix: one shift, arithmetic that provably cannot exceed a byte, mixed widths refused.
- **`selection_color =` became a real key (DONE, `5c1f58d`)** — the theme prerequisite: `Config.selection_color`
  (default `SEL_BG`) -> `Render.selection_color` -> `render_chrome()` derives the band, the active bar item and
  the focus border from it, and `fm_paint()` takes a `sel` argument so the file manager's selected row matches.

### 1e. `--import-theme FILE` — DONE (built s105, taken green and committed s107)

What is in the tree: the whole importer in `src/theme.c` (~940 lines), its API in `kai-term.h`, the flag in
`main()` + `usage()`, README's two table rows and its "Importing a theme written for another program" block,
and 67 new checks in `tests/test_vt.c` (1060 -> 1127) plus 24 in `tests/cli_test.sh` (67 -> 91).

- **The importer lives in `src/theme.c`, not in `main()`** — `main()` is in no linked test, so a flag whose work
  is done there would be covered only by the CLI script (same reasoning as "Geometry lives in `src/layout.c`").
  What is in `main()` is the flag, the question, and the report. It runs BEFORE `config_load` so the settings
  file's log lines do not bury the import report, and it needs no display.
- **API:** `theme_sniff()` (format from CONTENT, never the file name), `theme_parse_text()`, `ThemeVals`
  (a colour of 0 means "the file did not name it", so an unset colour falls back to Kai-Term's default rather
  than to black), `theme_write_file()` (writes the NATIVE format, so an imported theme is read afterwards by the
  same reader as a hand-written one — that round trip is a check), `theme_dest_dir()` (first writable `themes/`
  of the config search, never `/etc` — importing must not need root), `theme_name_of_path()`, `theme_import()`
  (returns 0 written / 1 exists-and-not-granted / -1 refused).
- **Formats:** kai-term's own and kitty's (one reader, already there), Xresources, alacritty.toml, Konsole
  `.colorscheme` (modern sections and legacy `[Color Scheme]`), xfce4-terminal `terminalrc`, Windows Terminal /
  VS Code JSON. **iTerm2 `.itermcolors` is recognised and REFUSED** — 0.0-1.0 floats in an XML plist, no
  iTerm2 on this machine to check a reader against, and a parser nobody can verify is worse than a refusal that
  says what it looked for. Two rules the whole block obeys: **never guess a format** (nothing matched -> refuse
  and list what was looked for) and **never guess a colour** (X11 names, alacritty's `CellForeground`, a JSON
  `"background": "red"` are refused and logged, not approximated).
- **Measured corpus, not assumed** (`.scratch/s105/importprobe.c` + `.scratch/s105/corpus/`, re-run s106):
  kitty 20 set / 10 dropped; Xresources 19 / 3; alacritty 20 / 2; Konsole 19 / 2 (name `Breeze Dark`);
  XFCE4 19 / 1; JSON 20 / 0 (name `Campbell`); iTerm2 rc=-1 refused; prose rc=-1 refused. End-to-end verified
  with the real binary against a throwaway `HOME`: writes
  `<config>/kai-term/themes/Breeze-Dark.theme`, prints the `theme = Breeze-Dark` line to enable it, re-running
  asks `Overwrite? [y/N]` and with stdin closed refuses with "nothing written. Re-run with --force", `--force`
  overwrites, and an unrecognised file is refused listing every marker it looked for.
- **Closed in s107:** the red `make asan` (the two warnings via `set_name()`, the leak by freeing the harness's
  two name lists), the A/B, and the CLI coverage of the flag. Both A/Bs were RUN, not reasoned about:
  `.scratch/s107/ab_noimport/` — HEAD's `src/theme.c` plus the 1e API stubbed to "nothing recognised, nothing
  parsed, nothing written" — runs the same 1127 checks and reports **44 failures**; `.scratch/s107/head_tree/`
  — a HEAD build, so `--import-theme` is an unknown option — run through the NEW `tests/cli_test.sh` reports
  **17 failures out of the 24 new checks**. The checks that pass in both trees are the negative ones (a tree
  that refuses everything satisfies "a missing file exits 1"), which is a limit of a feature A/B, not a defect
  in those checks — see Key Decisions.
- **Still owed:** nothing. The user confirmed `--import-theme` works in their own use (2026-10-19, s108).

### 2. Cursor: a thin bar, classic blink

The user's words: "A thin rectangle, the most classic blinking cursor". Today the cursor **is** the cell's
background quad, emitted in pass 1, and the later passes re-tint the glyph on the cursor cell; the
unfocused-session colour is `mix_c(cursor_color, bg, 55)`.

- A bar/underline must be a **separate quad**, not the cell background. Emit it in **batch B, between the
  selection bands and the glyphs** — batch B is one draw call where the later quad simply wins, so glyphs must
  come after it or the bar hides the character. Keep the existing rule that the cursor is hidden exactly where a
  band is painted (the comment in `render_draw()` pass 1/2 says they must match; a bar that forgets it
  re-creates that bug).
- Blink needs a **fourth timerfd** (there are three: frame `tfd`, key repeat `krfd`, paste watchdog `pwfd`). A
  tick must set `W.dirty` and go through `redraw()` on the normal path — never swap inside a Wayland listener,
  and never arm a `wl_surface_frame` callback without attaching a buffer (frame-pacing rules below). Blink phase
  is window state (`W`), **not** `Term` state: `term_init()` memsets `Term`, and DEC 25l must still hide it.
  Blink only while the window has focus — classic behaviour, and it stops a 2 Hz redraw on an idle unfocused
  window.
- Keys: `cursor_blink = 1000` ms half-period (the user's answer: "blink 1 sec, 0.5 sounds too fast"), `0` =
  steady; `cursor_width = 2` px for the bar, clamped 1..cell_w/3. **No `cursor_style` key** — always a bar.
  **Honour DECSCUSR** (`CSI <n> SP q`, 0 hits today) so vim/neovim/fish can still ask for block or underline;
  the query plumbing already exists (`CSI c`, `CSI 6 n`).

### 3. Double-click selects a word, triple-click a line (on by default)

The user's rule, verbatim: "a word is everything no separated by a space, but we don't take the trailing ',' or
'.'". So: maximal run of non-space cells around the clicked cell, then strip trailing punctuation — **any**
punctuation that can stick to the end of a word, not just `,` and `.` (their answer to question 4). Leading
punctuation stays part of the word.

- The double-click detector already exists for the file manager and should be copied, not reinvented:
  `#define FM_DCLICK_MS 500` and the same-row/same-entry test in `src/main.c`, using the `time` argument
  `ptr_button()` already receives.
- Selection endpoints go through `sel_set_endpoints()` with fractional 0..1000 offsets; a word/line selection
  uses whole cells (0 and 1000). `SelSpan` and `sel_band_pixels()` need no change, and
  `term_selection_text()` already trims trailing spaces, so a line selection reaching the right edge copies
  cleanly.
- **The interaction that must be handled:** on release of a click that never moved, `main.c` synthesises
  cursor-key presses for a program that owns its screen (`click_moves_cursor` + `term_wants_key_mouse`). A double
  click inside nano would walk its cursor twice — the second press of a double/triple click must mark the
  gesture as a word/line selection and suppress that synthesis on release. `Shift` already means "selection, not
  click".
- New key: `click_selects_word = 1` (they asked for on-by-default). Triple-click = the whole logical row; decide
  whether it includes trailing blanks (they copy as nothing anyway) and whether it extends to the next
  **physical** row when the line soft-wrapped (`Grid.wrap` knows).
- Needs a headless check: a word spanning a wide glyph (CJK/emoji occupies 2 cells, `ATTR_WIDE` continuation)
  must not be cut in half — the same wide-pair rule reflow uses.

### 4. Ask before quitting when something is still running

Today `toplevel_close()` sets `W.running = 0` immediately, closing the last tab does the same, and both close the
pty master and `kill(child, SIGHUP)` — whatever was running dies with no warning.

- How to know something is running: `tcgetpgrp(mfd)` on the pty **master** fd returns the pty's foreground
  process group. Kai-Term never calls `tcsetpgrp` (measured: 0 hits in `src/`), so the shell owns that value and
  it is trustworthy: idle at the prompt ⇒ `tcgetpgrp(mfd) == tb->pty.child`; a command running ⇒ it does not.
  **Verify with a probe before writing the gate** (`.scratch/s102/fgprobe.c`: spawn a shell on a pty, read
  `tcgetpgrp` idle, then with `sleep 30` running) — one syscall, and it is the whole feature's correctness.
- New key: `confirm_quit = running|always|never`, default `running`. Same gate on **closing a tab**
  (`key_close_tab`, and the bar's middle-click close), which is where work is lost more often.
- The prompt is chrome, drawn in the grid with the tab bar's font/colours — one row, modal, keyboard-driven
  (Enter/`y` confirms, Esc/`n` cancels); it must swallow keys without fighting the file-manager handover
  (`FM_FOCUS_*`) or the key-repeat timer. Log the decision either way.
- Multi-session case: if two sessions have something running, say how many and which, and confirm must apply to
  all of them, not just the visible one.

### 5. Middle-click paste (opt-in, off by default)

The user: "I actually hate middle click paste, but we could add it as an option (off by default)". Their answer
to question 5 settles the design: **it pastes the CLIPBOARD**, so no `wl_primary` work is needed at all and that
whole sub-task is dropped. Middle press pastes from the clipboard offer; ordering in `ptr_button()` matters —
the bar's middle-click-closes-a-tab, the file-manager band, and an application that grabbed the mouse all come
first and must keep winning. New key: `middle_click_paste = 0`.

### 6. Visual bell

`BEL` is swallowed today: `case 0x07: break;   /* BEL: nothing to do here */` in `exec_c0()` (`src/vt.c`).
Programs ring it on real failures — readline's ambiguous completion and empty Enter, `sudo` on a wrong password,
`less`/`man` on a failed search, vim on an error.

Cheapest honest implementation, no new dependency and no GL blending: `exec_c0` sets a per-Term "bell rang"
flag; `main.c` turns it into a deadline; `render_draw()` emits **one extra quad in batch A, after every cell
background** — over the backgrounds, under batch B (bands, glyphs, labels, border), so the text stays readable
while the field flashes. Colour = `mix_c(bg, fg, small)`. A margin flash is not an option: s101 made the margin
the same colour as the cells. New key: `bell = visual|off`, **default on** (their answer to question 6).

### 7. URLs: plain click opens

0 hits for OSC 8 and for any URL detection. **Do not store URIs in `Cell`** — `Cell` is allocated per column per
row and reflow moves cells around; a per-row list of `(col_start, col_end)` runs computed lazily is the cheap
shape. Recommended first version: at click time take the logical line (reflow knows the `Grid.wrap` marks), scan
it for `http://` / `https://`, take the run containing the clicked cell, stop at whitespace, strip trailing
punctuation. No storage, no reflow interaction, no per-frame cost.

Their answer to question 7 was a question back — "could it just be click url, no modifier ?" — and the answer is
yes, with gates, because a plain click already belongs to other things. It opens a URL only when the click lands
on an `http://`/`https://` run, the click never moved (a drag still selects), it is not the first click of a
double/triple click (that selects a word/line), it is not on the tab bar, not in the file-manager band, and the
program does not own the mouse (`term_wants_key_mouse` — nano/less/vim keep their click-to-place-cursor). So the
URL test happens on RELEASE, after the double-click window says "this was a single click". Opening needs
`xdg-open` — fork/exec from the terminal process.

### Decisions already made — do not re-ask, do not re-propose

Answered by the user 2026-10-22 (the eight questions) and 2026-10-19 (the dropped features):

- **Precedence:** `kai-term.conf` beats a theme; `kai-term.bind` is not in the chain at all (it holds bindings
  only, and a colour key there stays the error it already is). Consequence, already implemented: a theme cannot
  be applied line-by-line while the settings file is read, because a `background =` line ABOVE the `theme =` line
  must still beat it — the settings file records which keys it set explicitly and the theme fills only the rest.
- **OSC 4/10/11/104: yes**, with an off switch (`osc_colors = 1`) so "any program can repaint the window" can be
  turned off without a rebuild.
- **Cursor a bar always (no `cursor_style` key, blink 1000 ms, DECSCUSR honoured) / word selection strips any
  trailing punctuation but keeps leading / middle-click pastes the CLIPBOARD and is off by default, so no
  `wl_primary` work / visual bell on by default / URLs on a plain click with the gates above.** Each is spelled out
  in its step (2, 3, 5, 6, 7) — these are the answers, not open questions.
- **`--import-theme`: ask, do not silently overwrite and do not silently refuse** —
  `Warning: a theme with the same name already exists. Overwrite? [y/N]`; with no stdin to answer from it
  refuses and says to re-run with `--force`.
- **Native theme format:** Kai-Term's own `key = value`, in a third file, with **kitty spelling as primary and
  Kai-Term's spelling as an alias** (`cursor`/`cursor_color`, `selection_background`/`selection_color`) — that is
  what makes the kitty corpus drop-in. The user's own "13th Floor" kitty file is the acceptance case: 20 of its
  22 lines apply, `selection_foreground` and `cursor_text_color` are reported as dropped (Kai-Term has no
  per-cell "text on this colour" concept — the glyph there keeps the cell's own fg).
- **`colorN` names deliberately NOT in `apply_setting()`:** the template-coverage check derives the accepted
  settings names from that strcmp ladder and would demand 16 more commented lines in `kai-term.conf` for a knob
  nobody tweaks by hand. They live in the theme parser, which has its own derived name list.
- **Dropped on the user's word — do not re-propose:** scrollback search ("not really usefull imo"); HiDPI /
  fractional scaling (their `~/.config/niri/config.kdl` sets no `output { scale }`, so scale is 1 and
  `wl_surface_set_buffer_scale(…, 1)` is exactly right — nothing is blurry for them); OSC 52 (what they already
  do — paste inside tmux / ssh / ssh>termux — is the *local* clipboard typed into the pty, a different direction
  from the remote→local copy-out OSC 52 provides); IME (`zwp_text_input_v1`, 0 hits, they don't type CJK);
  **Noctalia / wallpaper-colour integration** ("let's drop it, that wouldn't work on a system without kitty" —
  the only parseable source was another application's config file). Kept because it cost measurements: Noctalia
  themes applications by **writing their config files**, not by sending escape sequences, and
  `~/.config/noctalia/colors.json` holds Material You tokens, not an ANSI 16 — the wrong source for a terminal.
  Now that 1d exists, a wallpaper tool CAN theme a live Kai-Term through OSC 4/10/11/104 with no file and no
  dependency on any other application; that is 1d's own acceptance case, not a separate feature.

### Rules for the whole session

One feature per commit, each: edit → AGENT.md → build → every suite → commit; never commit red. Every new config
key needs a line in the shipped template **and** a value check in the template-parity group, or the name-coverage
check fails (that check is what caught a deleted `padding = 8` line in s99). Re-measure and re-quote the check
counts every time — do not copy them from this file. Probe first, then write the check around what the probe
printed. **A/B every new check against the code that lacks the feature and report the failure count** — a check
that has never failed pins nothing. Measure BOTH builds (`src` and the test binaries). Do not map windows while
the user is at the machine: `KAI_TERM_TEST_NO_WINDOW=1`, and hand the visual verdicts (theme colours, cursor
look, bell flash) to the user.

## Progress Journal (era entries; root causes of fixed bugs: git history)

- **2026-10-12/14, s1-23 — scaffold, first build, first features.** vt.c grid -> rotation permutation (`row[]`
  always valid); CSI params; event loop -> `prepare_read -> epoll_wait -> read_events`; EGL/FreeType init; render
  fixes (vertex stride, MVP mapping, 6-vertex quads, atlas upload format, first-frame bootstrap). First 2 user
  bugs: black window (font metrics in font units, not px); 10 s startup (unanswered DA1/CPR/DA2). Then HW
  instancing, alt screen, SGR mouse, scrollback keyboard nav, zoom, selection + Wayland clipboard, wheel scroll
  (seat v5), the `Grid.live_off` anchor (fixed the wheel direction and the auto-follow). Tree -> project template;
  Makefile rewritten; `.gitignore` + stricter warnings.
- **2026-10-14/15, s24-47 — selection precision, emoji, reflow, paste, key repeat, padding.** Sub-cell endpoints
  and per-fragment highlight without GL blending; selection-vs-pointer causes (grid anchoring, `wl_fixed_to_int`);
  fontconfig fallback faces with the source face stored per glyph (so `atlas_grow` re-renders from the right one);
  3 back-to-front passes; COLOR emoji end to end (clusters in vt.c, ONE glyph id via HarfBuzz, FreeType color
  bitmap, RGBA8 atlas, un-tinted, wide emoji = 2 cells, regional-indicator pairs, U+FE0F/U+20E3 presentation).
  CSI 3 J so `clear` drops scrollback (`clear_scrollback` gate); shell start dir (`-w DIR` > config `working_dir`
  > `$HOME`, tilde expansion, `stat` check). Bracketed paste (DEC 2004, `paste_wrap()` as tested reference,
  buffered, <=4096 B chunks, 8 MiB cap, 400 ms idle watchdog); the latent `term_init`
  free-of-uninitialized-grid bug -> `GRID_VALID`. `app_id`; real window `opacity` (premultiplied source-over);
  selection endpoints snapped to whole cells; no band over never-written cells; **REFLOW** on width change
  (`Grid.wrap` soft-wrap marks, `grid_reflow()`, `grid_resize_plain()` for the alt screen, cursor travels with its
  logical cell, narrowing evicts oldest rows). CLIENT-SIDE KEY REPEAT (`krfd` timerfd, `send_key()`
  repeatability, `repeat_delay`/`repeat_rate`); `copy_on_release` (default off); the draw/commit cycle rewritten
  -> the 3 frame-pacing rules; the pre-existing segfault on heavy output in a grown window fixed by
  `sb_limit() = min(scrollback_max, rcap - rows)` as the single bound.
- **2026-10-15, s50-55 — repo hygiene and the CLI.** GitHub; dead code removed; `WARN` tightened; `.gitignore`
  replaced (it ignored neither `bin/` nor `build/`); config flag `-c` (was `-C`); one generic unknown-option
  message. `tests/cli_test.sh` + `make test-cli` (the real binary — `main()` is in no linked test); the zoom
  stale-prompt cause in `grid_resize_rows`; WINDOW PADDING (`padding` key + the single `term_grid_geometry()`
  deciding cols/rows AND origin), which exposed the `DEF_BG` clear-colour bug.
- **2026-10-15/16, s56-58, s60-62** — MULTIPLEXING (`src/mux.c`: N `{Term, Pty}` per window, config-driven key
  bindings) + TAB BAR + SIDE-BY-SIDE VIEW (`src/layout.c`: pure geometry). Three defects fixed: bar/pane click
  logged a switch it never made (`tab_switch()` never called `mux_goto()`), `BTN_RIGHT`/`BTN_MIDDLE` swapped in
  the SGR mapping, `tab_bar = always` asked the compositor for `cell_w` instead of `cell_h` of extra height
  (startup grid 1 row short) -> arithmetic moved to `layout_initial_size()` so a check pins it.
- **2026-10-16, s59** — RENAME `K-Term` -> `Kai-Term` (dir, header, binary, config path,
  `DEF_APP_ID`/`DEF_TITLE`, log prefix, `TERM_PROGRAM`, tests pinning those literals). 4 items OUTSIDE the repo
  were listed for the user (niri window rule, `/usr/local/bin/k-term`, `desktop-terminal` + its .desktop entry,
  GitHub repo name).
- **2026-10-16/17, s63-s67 — the doc rules.** `docs/ARCHITECTURE.md` deleted (it mixed project design with host
  facts, which must not ship inside a project); host facts -> `/home/kaida/Project/ENVIRONMENT.md` OUTSIDE the
  repo; its invariants -> "Architecture & Invariants", its entry points -> "Debugging this project". ISSUES.md
  gutted to verified current info only (673 -> 100 lines): filing rule = symptom + exact reproduction + what
  `./bin/kai-term -d` logs; not reproducible -> not filed. s67: the "56px emoji strike" fact was wrong IN THE
  DOCS, not in the code — it came from reading CBLC's `bitmapSize` (a BYTE count) as a pixel size; the installed
  NotoColorEmoji strike is 136x128 and `color_px == 128`, so font.c was right. Rule from it: no git SHAs in
  source comments — they age; state the mechanism inline.
- **2026-10-17/18, s68-s70 — image support (kitty graphics protocol), user-confirmed by eye.** Model/parser
  (`src/vt.c`): APC `ESC _ G` + DCS-discard states, control-block parser (`k=v` separated by `;` OR `,` — timg
  uses `,` the spec says `:`), chunk assembly (`m=1`/`m=0`), base64 + zlib + PNG (libpng simplified) + raw
  f=24/32, premultiplied pixels held in the model, images anchored to ABSOLUTE document rows (`Grid.row_base`),
  span math in vt.c (also what `a=T` moves the cursor past), byte budget + prune, `CSI 14 t`/`CSI 16 t` answers,
  `ws_xpixel`/`ws_ypixel` in `TIOCSWINSZ`. Renderer: one GL texture per image keyed by `KImage.serial`, and the
  frame split into TWO batches so text stays on top with no GL blending. Three defects found by writing the
  README honestly and then running the example: the "timg auto-detects" claim was false (its list is hardcoded
  to `TERM=xterm-kitty|xterm-ghostty`); Kai-Term's DA1 answer advertised sixel (`;4`), so `timg foo.png` sent DCS
  the parser discarded — the picture vanished, worse than block characters; and a re-transmission to an existing
  id stacked a second picture, so an animation left a trail. DA1 is now `ESC[?64;1;2;6;7;12;22c`, `CSI 5 n` is
  answered `ESC[0n`, and a transmission to an existing id replaces that image. Rule: **a documented example is a
  claim — run it before writing it.**
- **2026-10-18/19, s71-s74 — the integrated file manager.** `src/img.c` (every decoder in one place: PNG libpng,
  JPEG libjpeg, WebP libwebp, GIF decoded in-tree because giflib is absent; premultiplied RGBA8; `img_sniff()`
  on magic bytes, never a file name) — the kitty path and the previews share it. `src/fm.c` (model only, no
  Wayland/EGL/threads), band geometry in `src/layout.c` (`FmGeom`, `layout_hit_fm`), band drawing in
  `src/render.c` (same batches, shared `emit_cell_glyph()`), wiring in `src/main.c` (worker thread + two
  newest-request slots + result pipe fd in epoll, key/mouse routing, focus handover, the `cd` follow), 20 config
  keys, `tests/test_fm.c`. Decoders verified by pixel-diffing against ffmpeg (PNG/JPEG/WebP/GIF byte-exact),
  which found a libjpeg `output_scanline` off-by-one (heap corruption) and a wrong GIF interlace mapping. Three
  real product defects found ONLY by the live key-injection harness, because `main.c` is in no test binary:
  `fm_keys()`'s two-state return let every one-shot band key also reach the shell (Return on a file typed its
  path AND a newline); an out-parameter passed next to the call that fills it made every post-navigation listing
  read `request 0` and get discarded; and `fm_send_cd()` fired on refresh, typing a command at the prompt.
- **2026-10-19, s75-s79 — the file manager's four reported fixes, and a core pty bug.** Added `Left`/`Right`
  bindings, `..` as a real first row, double-click to open, and the empty-prompt gate: the model remembers the
  cursor row as the shell last printed it (`prompt_row`/`prompt_cx`/`prompt_cy`) and `term_at_prompt()` =
  `!input_pending && prompt_row_matches()`. `alt_screen_leave()` now RE-TAKES that snapshot instead of clearing
  it — clearing made the follow dead for the rest of the session after every vim/less run. **`src/pty.c` was
  putting every child's terminal into raw mode** (`cfmakeraw()` on the master; a pty pair shares one termios),
  which broke Ctrl+C, Ctrl+D, echo and ONLCR for every program that does not set its own termios — measured A/B
  live, then fixed by deleting the block.
- **2026-10-19, s81 — a refused texture upload is a decision, not a log line.** 85 SIGSEGV core dumps from one
  CLI-suite run on a GPU with no memory left: every `glTexImage2D` site printed its error and drew with a texture
  that had no texels, faulting inside `libnvidia-eglcore`. Now the glyph atlas is retried once after dropping
  every picture texture and then ends the run (`render_draw()` -> -1, exit 1); a refused color atlas falls back
  to mono glyphs with a ~1 Hz retry; a refused picture/preview texture skips that picture. `tests/cli_test.sh`
  now makes ONE probe launch and skips the window checks with the reason printed;
  `KAI_TERM_TEST_NO_WINDOW=1` skips them all — a test that maps a window changes the user's desktop.
- **2026-10-19, s82-s86 — the prompt follow: clear -> `cd` -> re-populate, idempotent, user-confirmed.** Design:
  capture the line (`term_prompt_line()`), Ctrl+U, send the `cd` only once a burst is seen to CHANGE that row back
  into the remembered prompt, then re-type with no CR — `IDLE/ERASING/CDING/RETYPING` plus a 2 s deadline, so
  "runs a mangled command" is closed by construction. Two hazards only a real shell exposes: opening the band
  resizes the grid and that destroyed the snapshot of a DIRTY line (a height-only resize now moves the snapshot
  with its row instead of discarding it), and the end-of-burst rule closed the echo window on a STEADY state, so
  a second navigation appended `cd '<dir>'` to the user's command — the window now needs a transition, and CDING
  additionally waits 150 ms of silence (`FMCD_QUIET_MS`) because a burst boundary is not a prompt. fish's
  autosuggestion ghost is followed via `term_prompt_line_typed()` / `term_prompt_line_is()`; the accepted cost is
  a cursor parked mid-command: the whole-row proof fails, the sequence is dropped (a `cd` is never written in
  front of the surviving half) and the captured text goes to the log. Proven headless against real fish through
  the real pty (`.scratch/s86/follow2.c`).
- **2026-10-19/20, s88 — the band became a SHARE, and the preview's rows got one owner.** `fm_rows` is gone
  everywhere; `fm_size` (percent of the grid area, default 75, clamped 10-95 at parse time AND in layout.c)
  decides the band: `band_h = grid_h * pct / 100` then clamped to the terminal's floor, and `layout_initial_size()`
  solves the inverse `grid_h = ceil((rows*ch + gap) * 100 / (100 - pct))` — the startup round trip is what proves
  that arithmetic. The preview overlap was two hardcoded numbers, not a renderer problem: the header's size was a
  literal 3 in `paint_preview()` and `fm_image_fit()` independently, and the 8 metadata lines an image preview
  carries were counted in neither, so lines from row 3 down were painted into the picture's box (glyphs are
  emitted in the batch AFTER the picture). Now `preview_head_rows()` + `preview_meta_rows()` are the single owner,
  called by both. Rule: **a check that passes on the buggy code pins nothing** — the first version (a small
  centred 8x8 picture) passed against the old fm.c by luck; the case that actually overlaps is a picture whose
  box starts at the top of the picture area, and the A/B found that.
- **2026-10-20, s89 — a BackSpace at an empty prompt had disabled the shell follow.** Measured (real shells):
  bash and sh answer BackSpace at an empty prompt with a bell only, fish not at all — so no burst ever re-proved
  the line empty and `term_at_prompt()` stayed "not empty" until Enter. Fix: `payload_cannot_insert()` — a payload
  that cannot put a character on the line (cursor movement, deletion, Ctrl+L, a bare Escape) does not open the
  echo window **when the line is already provably the bare prompt**. That turned a long-green check red: the
  property had been held by accident, because every key Kai-Term sent set `input_pending`, which is what stopped
  `prompt_snapshot()` re-taking the anchor at the moved cursor. Now `prompt_snapshot()` returns untouched when
  `prompt_row_same_cells()` says the row is cell-for-cell the remembered prompt, so `prompt_cx`/`prompt_cy` mean
  "where the cursor sits when the line is EMPTY" — pinned by `CSI G` checks. Do not write a prompt check leaning
  on `CSI H` (it moves the cursor to another ROW) or on SS3 (`ESC O H` prints a literal `H` here).
- **2026-10-20, s90-s92 — shell integration: the file manager asks the SHELL to `cd` instead of Kai-Term typing
  it.** There is exactly one channel to the shell, so the control carries no data: the directory travels in a
  per-session file and the sequence is ONE byte pair for every shell, `C-x C-]` (0x18 0x1d), chosen from a
  measured table of what each candidate inserts when unbound and of the tty's own specials — `C-\` is inert and
  unbound in both shells but `stty -a` reports `quit = ^\`, so it would SIGQUIT whatever owns the terminal.
  Announce `ESC _ K ; kai-term ; integration ; <kind> ST`, confirm `ESC _ K ; kai-term ; cd ; <dir> ST`, 400 ms
  then fall back to the typed machine. APC kind `K` is one more branch in `apc_end()` — no new parser state. s92
  fixed the two defects the user reported from their own window: a shell they started at the prompt was not
  integrated at all (the scripts now wrap a bare `fish`/`bash`/`zsh`, never over a name the user defined, never
  when any argument is present, and re-announce after every command — `Term.shell_int_gen` + `Tab.int_gen` make
  an announcement an EVENT that revives a dead integration, because the kind alone cannot: fish -> nested fish ->
  fish announces three times with one kind), and the prompt kept naming the OLD directory (fish `commandline -f
  repaint`, zsh `zle reset-prompt`; **bash cannot** — readline expands the prompt once per `readline()` call,
  `C-l` leaves it stale and printing `${PS1@P}` over the line garbles it, both measured, so bash is documented
  and nothing is pretended).
- **2026-10-20, s93-s94 — the cleanliness pass, and zsh becomes verifiable.** A stricter second-compiler pass
  (clang, `WARN` plus `-Wcast-qual -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition -Wundef
  -Wunused-macros -Wextra-semi`) found exactly three things in OUR code: `img_decode_gif` needed `static`,
  `klog` needed `__attribute__((format(printf, 2, 3)))` — verified both directions — and one `-Wcast-qual` at
  zlib's non-const `next_in`, correct as written, which is why `-Wcast-qual` stays out of `WARN`. Also found: the
  whole s90-s92 work had never been committed. s94: the user installed zsh and confirmed the integration for fish,
  bash and zsh. Still unmeasured in zsh: what `C-x C-]` does in a zsh that has no binding. Grep found seven stale
  "no zsh on this box" claims, four inside source comments and check MESSAGES — **a check message that misstates
  the evidence is worse than no message, because the next agent trusts it.**
- **2026-10-20, s95 — the user changed a design decision, not a bug** ("it is not a bug, it was a design
  decision, But we are changing it"). Now `p` types the selected entry's quoted path at the prompt, `v` toggles
  the preview, and `Return`/double-click on a file do nothing. Measured BEFORE changing anything: the strict gate
  refused fish's autosuggestion ghost and a mid-line cursor, but `cat ` already typed already worked in bash and
  zsh — the user's login shell is fish, which is why it looked universal. The gate is now the non-strict
  `term_prompt_line_typed()`; the `cd` follow machine is provably untouched. Build the expected text with the
  program's own `shell_quote()`, never by hand.
- **2026-10-21, s96 — the mouse inside full-screen programs.** Measured first: nano and less are alt-screen with
  `mouse_mode == 0`, so the wheel landed in `scrollback_nav()` on a grid built with `scrollback_max = 0` and was
  thrown away; **less answers ONLY the SS3 arrow form** because it asked for DECCKM with `ESC[?1h`; on
  `nano --mouse` the wheel report Kai-Term sent (4/5) does nothing while 65 scrolls, so the wheel was dead in
  every ncurses program; and a mouse report sent to a program that never asked for the mouse is **typed into its
  document**. Shipped: DECCKM (`Term.app_cursor`, saved/restored across the alternate screen, every cursor key
  through one `term_arrow_seq()`), the wheel as cursor keys, a click as the arrow-key walk to the clicked cell
  with the target column clamped to that row's last glyph + 1, 64/65 for programs that DID grab the mouse, and
  preview-pane scrolling. All of it in `src/vt.c`/`src/fm.c`, never in `main.c`.
- **2026-10-21/22, s97-s98 — the stray `h`: two mechanisms, one ours.** s97 (ours): a C0 byte inside a CSI
  sequence ENDED it — `ESC [ ? 2004 <TAB> h` left `bracketed_paste` at 0 and painted an `h`; `h` is the final
  byte of every mode-ENABLING sequence, which is why the leaked character was an `h`. Fix: ONE file-local
  `exec_c0()` shared by `PS_GROUND` and `PS_CSI` — the rule existed twice and the two copies disagreed. ESC is
  the exception: it cancels the sequence, so the caller handles it. s98 (the source's): measured against the
  user's live Firefox selection — ten advertised types ending `text/x-moz-url-priv`, which Firefox serves as
  **UTF-16LE**, and `offer_offer()` overwrote the recorded type for every `text/*` name, so the LAST advertisement
  won. Fixed in two places because two things were wrong: `clipboard_mime_rank()` (ask for the best type, never
  the last) and `paste_repair()` (a NUL must never reach the pty; dense one-sided NULs or a BOM mean UTF-16,
  anything else is NUL-stripped). Their own clues — "the source matters", "ctrl+v works but ctrl+shift+v does
  not" — were the diagnosis. Also measured and NOT a bug: bracketed paste is off for ~20 ms (bash, zsh) to
  ~200 ms (fish) after a shell starts, so a paste flushed in that window goes out raw.
- **2026-10-22, s99 — the config split, and a config reader that had been throwing away every colour.**
  `apply()` became `apply_bind_key()` (the 24 `key_*`/`fm_key_*` names) + `apply_setting()` (the others) under one
  dispatcher, split by NAME and never by which file was open, so an older `kai-term.conf` with bindings in it
  keeps working and a setting written in `kai-term.bind` is refused by name rather than treated as unknown.
  `config_find(name)` = the first REGULAR file with that name in `$XDG_CONFIG_HOME/kai-term`,
  `~/.config/kai-term`, `/etc/kai-term`, searched per FILE NAME; `-b/--binds` added; the bind file is read
  second, so it wins. The `#` rule was "whitespace before it is enough" — exactly the shape of a colour value —
  so `background = #1c1c22` became `background =`, and an empty value is legal, so nothing was even logged.
  Measured against the user's own file: old `ff101014` (the built-in default), new `ff1c1c22` (what they wrote).
  New durable check: the template name-coverage check, which reads the accepted names out of the two strcmp
  ladders — A/B proved the hole was real (deleting `padding = 8` from the template: 0 failures on the old suite,
  1 on the new). One harness bug cost 10 windowed checks: `run_env` used `env "$@"`, so `env` parsed the
  binary's `-w` as its own `--split-string` and the command never ran.
- **2026-10-22, s100-s101 — the visible border around the grid: one runtime value now owns the default ink and
  paper.** User-confirmed in their own window ("It's fixed, you can push"). Three background-ish numbers existed
  and only two were in play: `Config.bg` (the `background =` line) -> `Render.bg` -> `set_clear_color()`, which
  paints the `Config.padding` margin and the whole-cell remainder; and `DEF_BG` -> what `src/vt.c` put in every
  blank, erased, scrolled-in and reflowed cell, which pass 1 paints as the grid's interior. So the frame was
  `ff1c1c22` around `ff101014`; it "appeared recently" only because the s99 `#` fix made their `background =`
  line take effect for the first time. Fix: file-scope `g_def_fg`/`g_def_bg` in `src/vt.c` with
  `term_set_default_colors()` + `term_default_fg()`/`term_default_bg()`, and every `DEF_FG`/`DEF_BG` use in that
  file became a read of them — including the `cell_pristine()` sentinel, on purpose: the paint colour and the
  "never written" sentinel must be one variable, not two that can drift. `main.c` calls the setter once, after
  the two config readers and before `mux_open()`/`render_init()`. Three patch-overs that existed only because of
  the split were deleted rather than left behind: the `pen.bg/pen.fg = conf.*` assignments after `mux_open()` and
  in `tab_open()`, and `paint_blank_cells()` with its three call sites — keeping them would have left a second
  writer of the same value, and `paint_blank_cells` was not even faithful (it re-painted every `ch == 0` cell
  from `Config.bg`, destroying a blank an application had erased with a colour of its own). `DEF_FG`/`DEF_BG` stay
  as the built-in defaults; the constants stayed, the uses became the variable. Both directions A/B'd: the
  end-to-end probe prints `match=1` on the new code and `match=0` against `git show HEAD:src/vt.c`; 7 of the 14
  new checks fail on the reported state, and against a deliberately divergent variant (paint sites read `g_def_*`
  but `cell_pristine()` still compares `DEF_BG`) 2 fail — the reflow cursor position and the narrow/wide round
  trip, which is what shows the sentinel check pins something.
- **2026-10-22, s102 — the feature survey, the dead `cursor_color`, and the plan.** Every candidate was grepped
  for first: `zwp_fractional_scale`/`wp_viewporter` 0, `wl_primary` 0, OSC 52 0, OSC 8 / URL detection 0,
  DECSCUSR 0, `zwp_text_input` 0, `tcgetpgrp` 0, `BEL` = `case 0x07: break;`. Two of my claims were wrong and the
  user corrected them with facts about their own machine (scaling, OSC 52) — lesson: **a missing-protocol grep
  proves the code is absent, it does not prove the user is missing anything.** Found while reading the colour
  paths: `cursor_color =` is dead config — defaulted, parsed, stored, read nowhere; every test naming it tests
  *parsing*. Step 0 then landed as two commits: the behaviour-free seam (`render_chrome()`, verified by a probe
  comparing all eight `Chrome` fields over 25 bg×fg pairs — 200 comparisons, 0 differ), then the fix
  (`Render.cursor_color` + `tests/test_render.c`, 15 checks; A/B against the pre-fix renderer: 5 of 15 fail
  there, 0 here). `bin/test_render` links `render.o` + what it needs to LINK but calls only the pure function, so
  it needs no display and no GL context. Two of its checks are SOURCE SCANS, honestly: the one link that cannot be
  executed headless is the `render_init()` call, because it lives in `main()`. Then 1a (the palette becomes
  process-wide) and 1b (the cube fix) — see "Where each step stands".
- **2026-10-19, s103 — `selection_color =`, and two ways a check can lie.** The tree opened RED and
  half-recorded: `test_render` 23 checks / 2 failures. Root cause, measured: the probe tokens were named *inside
  the `CHECK` calls that counted them*, and a `CHECK` line is code — so the scan of `tests/test_render.c` counted
  its own arguments. **A self-scan cannot pin a token counter: the test is always part of the corpus.** Fix: the
  checks scan a probe file the test writes, and every mention of a probe token in that source is split by string
  concatenation so the scanner never sees the whole word; that also let the line-comment and multi-line-block
  rules get pinned. Part 2: compiling the TEST binaries printed `hex escape sequence out of range` — in C a hex
  escape takes every following hex digit, so `feed("\x1bc")` is ONE escape (0x1bc), and the palette group's "ESC
  c / RIS does not wipe the palette" check had been feeding a single 0xbc byte: **RIS was never tested.** It
  passed for an unrelated reason, which is why the s102 A/B could not see it. Fixed to `feed("\x1b" "c")` and
  proven non-vacuous by MUTATION (inserting `term_palette_reset()` into the RIS handler makes it fail). New gate:
  a `hex escape` group — `src_hexesc_count()` walks every C file, skips comments so prose ABOUT the trap is not
  counted, and skips a match preceded by another backslash so a message quoting it is not counted either.
- **2026-10-19, s104 — 1c (the `.theme` file) and 1d (OSC 4/10/11/104), one commit.** Why one commit
  (`c84258a`): they share one colour plumbing — `cfg_parse_color` exported so theme and OSC read colours the same
  way (the s99 bug was exactly two readers disagreeing), the shared `config_strip_comment()`, `Config.set_keys`,
  and the pinned settings-name count 30 -> 32 that both features move. Splitting them would have meant inventing
  an intermediate tree that was never built, never measured and never green. The two A/B harnesses the previous
  session left were RE-RUN, not quoted: `ab_osc/` (the OSC block absent) prints 1060 checks / 20 failures;
  `ab_nosweep/` (a sweep that skips cells holding no glyph) prints 2 failures — the blank-cell check and the
  narrow-resize wrap gate. This tree: 0.
- **2026-10-19, s105 — 1e `--import-theme` built, and left uncommitted.** See "Current Task / 1e": the importer,
  the corpus probe it was written against, and the four things owed before it can be committed (asan red, no A/B,
  no CLI coverage of the flag, the user's eye).
- **2026-10-19, s107 — objective: take 1e from red to green and push it.** Re-measured the tree first rather
  than trusting the numbers above: `make -j8` 0 warning lines, `make bin/test_vt bin/dbg bin/test_fm bin/test_render`
  0 warning lines, `KAI_TERM_TEST_NO_WINDOW=1 make test` = CLI 67/0/13, test_vt 1125/0, MODEL OK, test_fm 296/0,
  test_render 25/0 — and `make asan` **exit 2**, reproduced exactly as recorded (2 × `-Wformat-truncation` in
  `src/theme.c`, LeakSanitizer 134 bytes in 18 allocations from `usage_opts_of`/`readme_opts_of`). Plan, in order:
  (1) the two warnings — a name that does not fit is a decision, so make it one and log it; (2) free the names the
  option-pinning harness mallocs; (3) re-measure `make asan` green; (4) the A/B of the 65 import checks against a
  tree with the importer absent, reporting the failure count; (5) `tests/cli_test.sh` coverage of
  `--import-theme`/`--force` (argv in `main()` has no other coverage, and the import path needs no display so it
  runs headless); (6) re-measure every suite, update the docs, commit, `git fetch`, push. Nothing is committed
  while red.
  **Done, in that order.** `set_name()` in `src/theme.c` — one cap at `sizeof v->name - 1`, the cut logged —
  replaced both `snprintf(v->name, sizeof v->name, "%s", val)` sites; the option-pinning block now frees both
  malloc'd name lists. Re-measured after: `make clean && make -j8` 0 warning lines, the four test binaries 0
  warning lines, `KAI_TERM_TEST_NO_WINDOW=1 make test` exit 0 with CLI **91/0/13** (was 67/0/13 — 24 new checks
  for `--import-theme`/`--force`, placed in the part of the script that runs with no display because the import
  path needs none), `./bin/test_vt` **1127/0** (was 1125 — the 127-byte name cap), MODEL OK, test_fm 296/0,
  test_render 25/0, and `make asan` **exit 0, 0 warning lines, no LeakSanitizer report**. Both A/B harnesses
  are in `.scratch/s107/` and their numbers in "Current Task / 1e". README gained the import block, written
  against real runs rather than against the code: the Konsole file landing as `Breeze-Dark.theme`, the
  `Overwrite? [y/N]` question with stdin closed exiting 1 and leaving the file byte-identical, `y` overwriting,
  and `background = red` refused per key while the rest of that file still imported. The windowed CLI group was
  deliberately NOT run — the user is at the machine and it maps real windows.
  1e is `8add0e5`, and it was verified **in a fresh worktree at that hash** (`git worktree add` + build + all
  four suites + `make asan`, all green) rather than only in the working tree — the working tree also held the
  s106 journal cleanup, and "green" has to mean the commit, not the directory. The journal and ISSUES.md follow
  as a second commit, which is the only split that keeps every commit green: committing the importer as s105
  left it would have committed a red `make asan`.
  **Pushed** (`01f97a2..ebd62d9`, two commits; `main` level with `origin/main`). Session state at close: no
  uncommitted work, no red target, nothing half-edited, and the A/B harnesses left in `.scratch/s107/`
  (`ab_noimport/`, `head_tree/`) for re-running. Still owed from 1e: the windowed group of
  `tests/cli_test.sh` (not run then — the user was at the machine; it ran in s108) and the user's eye on an
  imported theme (confirmed working in s108, so 1e is fully closed). The next session opens on
  plan step 2, the cursor bar: Current Task / 2 already holds the measured facts (a separate quad in batch B
  under the glyphs, a fourth timerfd, blink phase in `W` and never in `Term`, `cursor_blink = 1000`,
  `cursor_width = 2`, DECSCUSR honoured, no `cursor_style` key) — probe first, A/B every new check, hand the
  look to the user.
- **2026-10-19, s106 — this cleanup (docs only, no source touched).** 1942 -> 1459 lines, 183,517 -> 137,411
  bytes (the 1942 side is verifiable: `git show 8add0e5:AGENT.md | wc -l` — the cleanup sat uncommitted while
  1e was taken green, so the feature commit still carried the pre-cleanup text; the file landed as 1514 lines
  in `ebd62d9`, because s107's own entries were added on top of the 1459). The s101-s105 step-by-step
  narrative collapsed into the era entries above, and the "measured starting
  point" and done-step plan blocks went to git history. Every mechanism name, measured number and pitfall was
  checked to survive outside the deleted text before it was deleted (the check: build the post-deletion text and
  grep it for each name — `exec_c0`, `clipboard_mime_rank`, `paste_repair`, `config_find`, `apply_bind_key`,
  `term_wants_key_mouse`, `term_click_arrow_keys`, `ink_in_box`, `pv_scroll`, `shell_int_gen`, `prompt_cx`,
  `C-x C-]`, `cell_pristine`, `g_palette`, `render_chrome`, `THEME_KEYS`, `term_recolor_defaults`, 64/65 — all
  present). Re-measured the tree while doing it, which is how the red `make asan` was found. Pre-cleanup text:
  `.scratch/s106/AGENT.md.pre-cleanup`.

- **2026-10-19, s108 — the windowed group of `tests/cli_test.sh`, on the user's explicit go-ahead.** It has
  not run since s104 (117/0/0) and the 24 checks s107 added make that total stale. Checked the machine first
  rather than assuming: `WAYLAND_DISPLAY=wayland-1`, no `kai-term` of mine running, no new core dump, and
  `nvidia-smi` showing **11680/12227 MiB used** — the local LLM holds 9632 MiB, which is the condition
  AGENT.md's Next Steps item 4 warns about, because a refused atlas upload fails EVERY window check for one
  unrelated reason after putting a dozen windows in front of the user. So ONE probe launch first, my own
  (`./bin/kai-term -d -e true`): exit 0, no atlas/video-memory line in the log, and it reached `kai-term
  exiting` — the atlases fit in the ~547 MiB free, so the windowed group can really run. Then the full suite
  WITHOUT `KAI_TERM_TEST_NO_WINDOW=1`: ~15 real windows, argv in `main()` and the keys a real launch applies,
  which is the only coverage those have anywhere. Expect the total to move from 117 to roughly 141 (117 + the
  24 s107 added); re-diff, do not assume. Afterwards: put the measured number in AGENT.md and ISSUES.md in
  place of the stale 117, and hand the user the result.
  **Result: `CLI: 141 checks, 0 failures, 0 skipped`, exit 0** — exactly the predicted 117 + 24, and 0 skipped
  is what shows the window checks ran rather than being gated off (the headless run reports the same script as
  91 checks / 13 skipped, so the windowed group is 50 checks and has not moved since s104). Nothing was found:
  `-c/--config`, `-b/--binds`, combined flags, `-d` beating `log =`, `-w`, `-e`, and a real launch applying the
  multiplexing / display / file-manager keys and the band's share all still hold. The stale 117 is replaced by
  141 in Current Status, in the Project Files tree, and in ISSUES.md. One correction to the rule this run
  tested: Next Steps item 4 said to wait for `nvidia-smi` to show VRAM free, and that is too strict — the LLM
  held 9632 of 12227 MiB and the atlases still fit. The probe launch, not the free-megabytes number, is the
  gate.

- **2026-10-19, s109 — the file manager must not take the keyboard from the pointer. Objective, mechanism, plan.**
  The user's report: "When the file manager is open, hovering the mouse on it gives it the focus, same for the
  preview pane. They should require a click to take the focus. we could make that a config too (Default: needs
  a click to take focus)."
  **The mechanism, found by reading every path that writes `g_fm.focus`** (there are six in `src/main.c`, and
  grepping `hover` returns 0 hits — there is no hover code): the only one a moving pointer can reach is
  `fm_wheel()`, which calls `fm_focus_set(hit == FM_HIT_LIST ? … )` on its **fifth line**, before `steps` is
  even computed. `steps` is `W.frame_discrete` or `(int)W.frame_axis_val / cell_h`, and the function then
  returns early when `steps == 0` — so a `wl_pointer.frame` that produced no whole-cell scroll (a trackpad
  frame with a sub-cell delta, a gesture that moved less than one row, a frame arriving without axis data)
  **moves the keyboard into whichever pane the pointer happens to be over and scrolls nothing**. That is
  exactly "hovering gives it the focus", and it explains both halves of the report, because the same line
  picks the pane from the pointer position. `ptr_motion` and `ptr_enter` touch no focus state at all, and
  `render_draw()` draws the focus border from `fm->focus` only — nothing is pointer-driven on screen.
  **Plan.** (1) The decision becomes a pure function in `src/fm.c`, `fm_wheel_focus(hit, steps,
  focus_on_hover) -> FM_FOCUS_* or -1`, because `main.c` is in no test binary and a rule there is untestable
  (the standing "geometry lives in layout.c" reasoning, applied to a pointer rule). (2) It returns -1 when
  `steps == 0` in BOTH modes — a gesture that scrolled nothing must not move the keyboard, which is the bug
  itself, independent of the new key. (3) New setting `fm_focus_on_hover`, default **0** = a click is
  required, which is what the user asked for; `1` restores the old pointer-follows behaviour. (4) The click
  path (`fm_pointer_select`) is untouched — it is the "click to take focus" half. (5) The wheel still walks
  the listing / scrolls the preview body in both modes; only the keyboard handover is gated. (6) Plumbing:
  `config_default`, the `apply_setting` ladder (settings-name count 32 -> 33), a line in
  `config/kai-term.conf`, a README table row, the template-parity value check, a default check, a parse
  check, and a truth table in `tests/test_fm.c` A/B'd against the pre-fix code. (7) No new `cli_test.sh`
  check: the key changes no log line and no launch output, so the CLI script could only assert that the name
  is accepted, which the parity check already does — the pointer interaction itself is for the user's eye.
  **Done, and green.** `fm_wheel_focus()` in `src/fm.c` (declared in the header next to `fm_row_pos()`),
  `fm_wheel()` in `main.c` now computes `steps` first and asks the function; the click path is untouched. The
  function also refuses `FM_HIT_NONE` itself rather than trusting the caller's filter. Measured: `make clean
  && make -j8` 0 warning lines, the four test binaries 0 warning lines, `KAI_TERM_TEST_NO_WINDOW=1 make test`
  exit 0 with CLI 91/0/13, `./bin/test_vt` **1130/0** (was 1127: the default, the parse, and the template
  value check), MODEL OK, `./bin/test_fm` **305/0** (was 296: the 9-check truth table), test_render 25/0,
  `make asan` exit 0 with no report, and the windowed CLI group **141/0/0** again (probe first: the LLM still
  holds ~9.6 GiB, the atlases still fit).
  **A/B, run not assumed:** `.scratch/s107/ab_wheel/` replaces `fm_wheel_focus` with the old unconditional
  rule (`return hit == FM_HIT_PREVIEW ? FM_FOCUS_PREVIEW : FM_FOCUS_LIST;`, ignoring `steps` and the key —
  exactly what `main.c` did) and the same 305 checks report **7 failures**. The 2 that pass there are the
  `fm_focus_on_hover = 1` opt-in checks, which *should* pass under the old rule — that is what the opt-in is.
  Two more mutations in `.scratch/s107/ab_tpl/` prove the docs gate bites: commenting out the
  `fm_focus_on_hover = 0` line in `config/kai-term.conf` -> 2 failures ("kai-term.conf does not list
  fm_focus_on_hover"), and changing it to `1` so it disagrees with the built-in default -> 2 failures
  ("template: fm_focus_on_hover matches"). README's two places that documented the old behaviour ("a click on
  it, or the wheel over it, focuses it") were corrected — a stale sentence in the docs is a bug the user reads.
  **Owed to the user's eye:** that hovering the band no longer moves the focus border, that a click still
  does, and that the wheel still scrolls the listing and the preview body without taking the keyboard.

- **2026-10-19, s110 — verify s109 and finish it. Objective, plan, and what the verification found.**
  The user: "Hovering focus is now fixed, but not sure all the docs have been updated. Do the verification,
  finish it." So: re-measure every number the s109 entries claim, re-run the A/Bs rather than trust their
  recorded counts, and diff the three docs against the code by hand. Plan: (1) both builds + every suite +
  `make asan`; (2) the marker greps and the derived name counts; (3) re-run `.scratch/s107/ab_wheel/` and
  the two `ab_tpl` mutations; (4) hand-diff README's settings/binding tables against the parser's name lists;
  (5) fix whatever the diff shows, A/B the fix, re-measure, commit.
  **Measured, matches the docs:** `make clean && make -j8` exit 0 / 0 warning lines; the four test binaries
  exit 0 / 0 warning lines; `KAI_TERM_TEST_NO_WINDOW=1 make test` exit 0 with CLI 91/0/13, test_vt 1130/0,
  MODEL OK, test_fm 305/0, test_render 25/0; `make asan` exit 0, 0 warning lines, no LeakSanitizer report.
  The marker greps still return only the three self-splitting lines in `tests/test_render.c` and only
  `mkdtemp`'s template plus one test's fill string. Derived counts re-read out of the source: 33 settings,
  24 bindings, 23 theme names, 9 options in `usage()` — all as claimed. `fm_focus_set`/`g_fm.focus` writes in
  `src/main.c` re-counted: the wheel path is the only one a moving pointer can reach, and the click path
  (`fm_pointer_select`, one call site, `ptr_button`) still takes the keyboard — the fix is where it should be.
  **Two doc defects found, both real:**
  (a) **The `ab_tpl` failure counts in this file and in ISSUES.md are wrong.** Re-run in a harness copy that
  holds README.md (`.scratch/s107/ab_tpl2/`, baseline 1130/0): deleting the `fm_focus_on_hover = 0` line gives
  **1** failure ("config/kai-term.conf does not list fm_focus_on_hover — the file says it holds every
  default"), and setting it to `1` against the built-in `0` gives **1** failure ("template:
  fm_focus_on_hover matches"). Not 2 and 2. The old harness `.scratch/s107/ab_tpl/` has no README.md, so its
  second failure was `options: the option names were read out of README.md's table` — an artifact of the copy,
  not of the mutation. The gate still bites; the numbers were inflated.
  (b) **README's settings and binding tables were NOT pinned by any check**, which is what "not sure all the
  docs have been updated" was right to suspect. Measured: deleting the `fm_focus_on_hover` row from README's
  settings table leaves all 1130 checks green. The Current Status claim that the name lists are compared with
  "README's tables" was true only for the command-line OPTION table (`readme_opts_of`); the settings and
  binding tables were never read. (The hand-diff of the CURRENT README is clean — 33 rows matching the 33
  parser names, 24 matching the 24 bindings — so nothing is missing today; what is missing is the check that
  keeps it that way.) Fix: read both tables the way the templates are read and set-difference them in both
  directions. README's theme table is deliberately NOT set-differenced: it collapses `color0` … `color15` into
  one row, so a name-by-name diff would fail on purpose; those 23 names stay pinned through
  `config/kai-term.theme`.
  **Not re-run by me:** the windowed group of `tests/cli_test.sh` (the 141/0/0). It maps ~15 real windows and
  AGENT.md's rule is to ask before that run, not to do it unprompted while the user is at the machine. The
  number quoted is s109's, measured on this same uncommitted tree; a probe launch is the gate if it is re-run.
  **Done.** (a) The two inflated counts are corrected here and in ISSUES.md, and the harness that produced them
  is replaced by `.scratch/s107/ab_tpl2/` (a copy that DOES hold README.md, baseline 1130/0).
  (b) `readme_table_names_of()` + `names_diff()` in `tests/test_vt.c` read README's settings and binding tables
  the way `tpl_check_names()` reads the templates, and the template group set-differences each against the
  parser's list in both directions. `test_vt` 1130 -> **1133** (the heading guard and the two tallies). README
  itself needed no edit — its tables were already complete; what was missing was the check.
  **A/B, run not assumed** (`.scratch/s110/ab_readme/`, baseline 1133/0): delete the `fm_focus_on_hover`
  settings row -> **1 failure** ("README's settings table does not document `fm_focus_on_hover` — the parser
  accepts it"); delete the `fm_key_path` binding row -> **1 failure**; add a `made_up_key` row -> **1 failure**
  ("… documents `made_up_key`, which no parser accepts"); delete the whole settings table -> **1 failure** of
  the heading guard; rename the heading away from `### Settings` -> **1 failure** of the same guard. Renaming
  it to `### Settings file (…)` does NOT fail, on purpose: the match is on the heading's prefix, so re-wording
  a section keeps the check alive. A/B of the corrected `ab_tpl` numbers is in (a) above; `ab_wheel` was
  re-run unchanged and still reports 7 failures out of 305.
  **Committed and pushed.** Two commits, split so each one's own claims are true at that commit: `2ebaf3b`
  "File manager: the wheel no longer takes the keyboard — a click does" (the s109 feature; verified at that
  tree: both builds 0 warning lines, `KAI_TERM_TEST_NO_WINDOW=1 make test` exit 0 with test_vt **1130/0**,
  test_fm 305/0, test_render 25/0, CLI 91/0/13) and `0435567` "Tests: README's settings and binding tables
  pinned to the parser" (the new gate + the corrected counts; verified at that tree: test_vt **1133/0**).
  `git fetch` first, then `git push`: `git status -sb` now reports `main...origin/main` with no ahead/behind,
  and `git log --oneline origin/main -1` is the docs commit. A probe launch (`./bin/kai-term -d -e true`, one
  window, exit 0, no atlas/video-memory line, reached `kai-term exiting`) re-confirmed a real launch on this
  tree; the full windowed group was NOT re-run — it maps ~15 windows and the rule is to ask first.
  **Owed to the user's eye, unchanged from s109:** that hovering the band no longer moves the focus border,
  that a click still does, and that the wheel still scrolls the listing and the preview body without taking the
  keyboard. Nothing about those three is a harness question.

- **2026-10-19, s111 — step 2, the cursor bar. Objective, measured starting point, plan.**
  The user's words (s102): "A thin rectangle, the most classic blinking cursor". Decided then and not to be
  re-litigated: always a bar (NO `cursor_style` key), `cursor_blink = 1000` ms half-period with `0` = steady,
  `cursor_width = 2` px clamped 1..cell_w/3, DECSCUSR honoured so vim/neovim/fish can still ask for block or
  underline, blink phase in `W` and never in `Term`, blink only while the window has focus.
  **Measured before writing anything** (read, not assumed): the cursor is today the cell's own background quad
  in `render_draw()` pass 1 (`src/render.c:1157-1170`, `cellbg = cur_col`) and pass 3 re-tints the glyph under
  it (`:1300-1310`, inverting the ink); the colour comes from `render_chrome()` (`chrome.cursor` /
  `chrome.cursor_off`). DECSCUSR is collected but discarded: the CSI state skips intermediate bytes
  (`src/vt.c:3611`) so `ESC [ 3 SP q` reaches `csi_dispatch()` with final byte `q` and param 3, and the only
  `case 'q'` in the file is the kitty-graphics APC parser (`:2961`) — 0 hits in the CSI dispatch. Window focus
  is already tracked: `W.is_activated` is decoded from the xdg-toplevel states in `toplevel_configure()`
  (`src/main.c:1735`), so the "blink only when focused" gate needs no new Wayland plumbing. There are three
  timerfds (`tfd` frame, `krfd` key repeat, `pwfd` paste watchdog) — the blink is the fourth.
  **Plan.** (1) Probe the parser first: feed `ESC[0..6 q` and print what the model does today (expect: nothing
  changes), so the check is written around a measured before/after. (2) `Term.cur_style` (0 = the app never
  asked, 1 block, 2 bar, 3 underline) and `Term.cur_blink` (0 = not asked, 1 blinking, 2 steady), set by
  `csi_dispatch`'s new `q` case; they live in `Term` because DECSCUSR is per-session, and `term_init()` memsets
  so 0 = "not asked" is the only safe encoding. (3) The per-cell decision becomes ONE pure function in
  `render.c` (the standing reason: `main.c` and the draw loop are in no test binary, and `test_render.c` already
  proves `render_chrome()` is a working seam): given shape, blink phase, cell size and the configured width, it
  answers three things — does pass 1 paint the block, does batch B emit a separate quad and at what rect, does
  pass 3 invert the ink. A bar must NOT invert the ink and must NOT paint the cell background, or the glyph is
  drawn over a block and the bar is invisible. (4) The bar/underline quad is emitted in **batch B, after the
  selection bands and before the glyphs** — batch B is one draw call where the later quad wins, so glyphs after
  it keep the character readable. The existing rule that the cursor is hidden exactly where a band is painted
  must be obeyed by the new quad too (the comment at `:1164` says pass 1 and pass 2 must match; a bar that
  forgets it re-creates that bug). (5) A fourth timerfd in `main.c`; a tick sets `W.dirty` and goes through
  `redraw()` on the normal path — never swap inside a Wayland listener. Phase in `W`, DEC 25l still hides the
  cursor, and the timer is only armed while `W.is_activated` and while some session wants a blinking cursor.
  (6) Two config keys: `cursor_blink` (ms half-period, 0 = steady) and `cursor_width` (px) — `config_default`,
  the `apply_setting` ladder (settings names 33 -> 35), a line in `config/kai-term.conf`, a README row each
  (the s110 gate now FAILS if a README row is missing — that is the first use of the check added in s110), the
  template-parity value check, a default check and a parse check. (7) A/B every new check against the code that
  lacks it and report the failure counts; measure BOTH builds; hand the look to the user rather than mapping a
  window.
  **Expected outcome:** a 2 px bar at the left edge of the cursor cell, full cell height, blinking with a 1 s
  half-period while the window is focused and steady while it is not; `cursor_blink = 0` gives a steady bar; an
  app that sends `ESC[1 q` gets a block back, `ESC[3 q` a blinking bar, `ESC[5 q` an underline.

## Next Steps

1. **Step 2, the cursor bar — IN PROGRESS, see the s111 entry above for the plan and the measured starting
   point.** Everything that would have had to be decided is decided — a separate quad emitted in batch B under
   the glyphs, a fourth timerfd for the blink, blink phase in `W` and never in `Term`, `cursor_blink = 1000` /
   `cursor_width = 2`, DECSCUSR honoured, no `cursor_style` key. Probe first, A/B every new check, and hand the
   look to the user instead of mapping a window.
2. **Then steps 3-7 of the plan** (word/line click selection, confirm-quit, middle-click paste, visual bell,
   plain-click URLs). Each is measured and gated in "Current Task"; the decisions that would have blocked them
   are already made. Do not re-derive the measured facts.
3. **Owed to the user's eye:** the look of a themed window, a live OSC repaint, and the configured
   `cursor_color`. `--import-theme` is no longer on that list — the user confirmed it works (s108). Nothing
   about the remaining three is a harness question.
4. **The windowed group of `tests/cli_test.sh` ran in s108 (141/0/0) at the user's request and passed.** It
   maps ~15 real windows, so it is still a run to ask about, not one to do unprompted while the user is at the
   machine. Check `nvidia-smi` first: a refused atlas upload fails EVERY window check for one unrelated reason
   after putting a dozen windows in front of the user. Measured s108: the LLM held 9632 MiB of 12227 and the
   atlases still fit in ~547 MiB free, so "the LLM is running" is NOT by itself a reason to skip — one probe
   launch (`-d -e true`, one window) decides, and that is what the script's own gate does.
5. **Closed — diagnosis recipe only, nothing owed.** Each of these is built, green and confirmed by the user in
   their own window; if a report ever arrives, start from the `-d` line named, not from the shader.
   - Config split: `-d` logs `config loaded from <path>` / `bindings loaded from <path>`; a line in the wrong
     file logs `is a key binding — those live in kai-term.bind now` or `is not a key binding — kai-term.bind
     holds bindings only`. Their own `~/.config/kai-term/` holds only `kai-term.conf` and there is no
     `/etc/kai-term`, so their bindings are the built-ins — a `kai-term.bind` they never wrote is valid, not a
     bug.
   - File-manager band (`fm: opened on …`, `fm: listing <dir> (request N)`, `fm: dropping stale …`,
     `fm: keyboard -> …`), the `p` path key (`fm: typed the selected path at the prompt: <path>`; a refusal logs
     `fm: not typing the path — the input line is not one I can describe`; the keys are config: `fm_key_path`,
     `fm_key_preview`, `fm_key_open`), the prompt follow (type a partial command, open the band, navigate TWICE —
     each must end with `fm: the prompt now holds the browser's directory and what you had typed`, and the text
     must appear once, not `text cd <dir>`; then diagnose headless with `.scratch/s86/follow2.c` against the
     prompt predicates before touching the model).
   - Mouse in full-screen programs (`mouse: the wheel became N cursor key press(es) for the application`; if the
     wheel scrolls nothing check `term_wants_key_mouse()`'s two conditions first — `alt_screen` and no
     `MOUSE_PRESS`/`MOUSE_DRAG` grabbed, so a program that asked for the mouse is deliberately excluded; both
     halves are config: `wheel_sends_keys`, `click_moves_cursor`).
   - Shell integration (`fm: shell integration (<kind>) asked to cd into <dir>` then `fm: the shell's integration
     changed directory to <dir> — the line was not touched`; `no integration ack` means the reader was not an
     integrated shell — expected for `env bash`, `sudo bash`, `/usr/bin/bash`; in **bash** a stale prompt string
     is expected behaviour, not a bug).
   - Images: `-p k` IS the supported way to show a picture ("The goal was image support, Not auto-detection by
     timg"). Do NOT re-add the graphics bits (4/16/18) to the DA1 answer or invent a `TERM` value — advertising
     sixel without implementing it makes the picture vanish entirely. Sixel/iTerm2 would be a NEW feature, only
     on an explicit request.
   - The stray `h`: the parser half is fixed; if it ever recurs, do not re-open the parser — the second mechanism
     is a paste flushed before the application asks for DEC mode 2004, which goes out RAW and the shell then
     interprets the escape inside it (`printf 'AA\e[?2004hBB' | wl-copy`, paste at a fresh bash prompt — with the
     delimiters the line reads `AA^[[?2004hBB`, inert; without them `AA2004hBB`). The checks are the `C0 inside
     CSI` group in `tests/test_vt.c`, and `grid_has_glyph()` is what proves a sequence was consumed rather than
     printed).
6. **Housekeeping, none a bug:**
   - ASan build of the `kai-term` binary itself as a Makefile target (only tests have one) — it is the only thing
     that would exercise `fm_shutdown()` and the heap-grown `paste_buf` under LeakSanitizer.
   - The file manager's input routing lives in `main.c`, which is in no test binary, so it is covered only by the
     injection harness in `.scratch/s74/`. Extracting the key-routing table into a testable unit would close
     that, but it is a refactor of the file the three s74 bugs were in — do it deliberately, not as a side
     effect.
   - Animated WebP previews are refused by libwebp's simple decode; taking the first frame needs `libwebpdemux`
     — a new dependency, so only on an explicit request.
   - Idle-frame optimization: stop the frame cycle when nothing is dirty — `redraw()` calls `layout_apply()`
     every frame, so that path is hotter than before.
   - Color atlas re-uploaded whole with `glTexImage2D` on every new shaped emoji; `glTexSubImage2D` per new shelf
     would be cheaper.
   - Reflow allocates a fresh row buffer per produced line — fine at terminal sizes, but a huge-width window with
     huge scrollback churns the allocator.
   - Window title is `mux_title()`'s own text; an app's OSC title only reaches the tab bar label. Showing the
     active session's OSC title in the window title is a one-line change in `update_title()`, but it changes
     visible behaviour — ask first.
   - Clipboard hardening niri does NOT require (untested against a compositor that needs it):
     `wl_data_offer_accept()` before `wl_data_offer_receive()`; offer `UTF8_STRING` +
     `text/plain;charset=utf-8` next to `text/plain`.
   - Emoji paths that exist but have NEVER run here: the SCALABLE color font branch (`FT_IS_SCALABLE` ->
     `FT_Set_Pixel_Sizes` + `hb_ft_font_changed` on zoom) — this box has only a bitmap-only Noto CBDT font; and a
     cluster HarfBuzz renders as >1 glyph, deliberately rejected (`ng == 1`), falls back to mono.
   - A `tests/` check running `fish -n` / `bash -n` / `zsh -n` over the generated shell scripts would catch a
     script no shell can parse, but it makes the suite depend on external shells unless it skips when one is
     missing.
7. **Scratch:** throwaway probes live in `/home/kaida/Project/.scratch/` (outside the project). **What the user's
   "don't touch .scratch for now" means, settled with them 2026-10-20: do not DELETE or prune anything there;
   writing new files and editing existing ones is fine and expected** — that is where harnesses belong, and an
   agent that refuses to write there ends up putting throwaway work in `/tmp`, where it cannot be found again.
   Deleting is still the user's call. Harnesses worth keeping: `s70/timg_gate.c` (end-to-end image),
   `s74/inj_keys.sh` (the file-manager input-routing gate — the only coverage `make test` does not have),
   `s86/follow2.c` (the prompt follow over a real pty), `s89/follow3` + `s89/bstoprobe` (the echo-window
   transition), `s90b/intgate.c` (the shell-integration gate — re-diff it against `main.c` before trusting it),
   `s103/ab_ris`, `s103/ab_hex` (the mutation harnesses behind the hex-escape gate), `s104/ab_osc`,
   `s104/ab_nosweep` (the OSC sweep gates), `s105/importprobe.c` + `s105/corpus/` (the eight-format import
   corpus every importer number was copied from), `s107/ab_noimport/` (HEAD's `src/theme.c` with the 1e API
   stubbed out — the importer's A/B, 44 failures) and `s107/head_tree/` (a full HEAD build, what the new CLI
   checks were A/B'd against). One fact to keep in mind: this file cites those paths and a
   fresh clone cannot run them — so anything that is a real regression gate belongs in `tests/` eventually.
8. **Git:** keep committing verified work; `git fetch` before pushing.

## Project Files & Facts

```
Kai-Term/
├── .gitignore                   # bin/, build/, *.o/*.obj/*.d/*.so/*.a, editor leftovers, docs/ARCHITECTURE.md
├── LICENSE                      # MIT, "Copyright (c) 2026 Kaida"
├── AGENT.md                     # this journal
├── ISSUES.md                    # open items + Fixed + known limitations
├── README.md                    # features, deps, build, flags, the config files, shortcuts, exit status
├── Makefile                     # src/*.c -> build/*.o -> bin/kai-term; all/test/test-cli/asan/run/clean
├── config/kai-term.conf         # the shipped SETTINGS template: all 32 names at their built-in default
├── config/kai-term.bind         # the shipped KEY-BINDING template: all 24 names at their built-in default
├── config/kai-term.theme        # the shipped THEME example: all 23 theme names at the built-in colours
├── include/kai-term/kai-term.h  # the single shared header: all types, prototypes, log levels, helpers
├── src/                         # config.c theme.c vt.c img.c mux.c layout.c fm.c shellint.c pty.c font.c render.c main.c
├── tests/test_vt.c              # 1133 checks: parser, grid, graphics, config, themes, OSC colours, imports, prompt machinery
├── tests/test_fm.c              # 305 checks: the file-manager model, previews, decoders, band geometry, pointer rules
├── tests/test_render.c          # 25 checks: the renderer's pure colour decisions (no display, no GL call)
├── tests/dbg.c                  # grid-model invariant checker (permutation + scroll_off/live_off bounds)
├── tests/cli_test.sh            # drives the REAL bin/kai-term: 141 checks / 0 skipped with a display,
│                                # 91 checks / 13 skipped with none (the 13 are the window groups).
│                                # ONE probe launch gates the window checks; KAI_TERM_TEST_NO_WINDOW=1 skips them
├── bin/                         # kai-term, test_vt, dbg, test_fm, test_render (+ *_asan)  [generated, git-ignored]
└── build/                       # *.o + generated/xdg-shell-*                [generated, git-ignored]
```

- `src/vt.c` — unit-tested core: VT/ANSI/UTF-8 parser (incl. the APC/DCS states the kitty graphics protocol
  needs), grid model, the image list (decode + placement + lifetime), selection text extraction, alt screen,
  `term_grid_geometry()`, evdev -> SGR button mapping (`mouse_sgr_button`), the process-wide palette and default
  ink/paper, the OSC colour handlers. No Wayland/GL dependency — the decoded PIXELS live here, so the whole
  protocol is testable headless.
- `src/img.c` — every image decoder in one place: PNG (libpng), JPEG (libjpeg), WebP (libwebp) and GIF (own LZW
  decoder, because giflib is not installed here), all returning premultiplied RGBA8, plus `img_sniff()` (magic
  bytes, never a file name) and `img_premultiply()`. Shared by the kitty graphics path in vt.c and the file
  manager's previews, so the two cannot drift. GIF decodes the FIRST image block only; an animated WebP is
  refused by libwebp's simple decode.
- `src/fm.c` — the file-manager model, no Wayland/EGL/threads: directory scan (`opendir`/`lstat`, sort, hidden
  filter, caps), navigation state, preview building (directory listing / image / text with syntax highlighting /
  metadata) and painting the two panes into `Cell` grids the existing renderer draws unchanged. Also
  `fm_image_fit()` (the preview box in cells AND in px, aspect kept in pixels, never upscaled, centred) — the
  renderer only draws the box it is given, which is what makes the box testable. The two functions that touch
  nothing but malloc and the filesystem (`fm_scan_build`, `fm_preview_build`) are the only ones main.c's worker
  thread may call.
- `src/main.c` — all Wayland shell/input/clipboard/epoll logic: xkb keyboard, client-side key repeat, session
  shortcuts, pointer (bar clicks, pane focus, selection, mouse reporting, wheel), `layout_apply()`, frame-paced
  redraw, one epoll loop over every session's pty fd, SIGCHLD reaping by pid, CLI flags, data-device clipboard.
  Look here for pointer, keyboard, resize, frame pacing. **In no linked test binary.**
- `src/render.c` — EGL (platform Wayland) + GL 2.x: 2 glyph atlases + one GL texture per visible image, 1 shader,
  3 back-to-front passes emitted into ONE staging buffer but drawn as TWO batches (backgrounds, then bands +
  glyphs + chrome) with one draw per picture between them; instanced or expanded. Draws what `src/layout.c` says,
  owns no geometry. `render_chrome()` is the pure colour decision a test can call with no display.
- `src/font.c` — fontconfig pattern -> FreeType face; alpha8 glyph atlas (shelf packer + hash by codepoint);
  fontconfig fallback faces; HarfBuzz-shaped color emoji (`font_color_glyph`) + RGBA8 color atlas;
  `font_resize()` re-rasterizes at a new pixel size.
- `src/pty.c` — `posix_openpt`/`grantpt`/`unlockpt`; child: `setsid` + `TIOCSCTTY` + `chdir` + exec `$SHELL` (or
  `-e COMMAND`); sets `TERM`/`COLORTERM`/`TERM_PROGRAM`; `pty_setsize`. With integration on, `tab_spawn()` hands
  it the string `shellint_argv()` built (the per-session request file in the environment plus the shell's load
  flag) instead of the bare shell path. It does NOT touch termios — see the s75-s79 entry.
- `src/theme.c` — themes: a file of COLOURS only, in kai-term.conf's `key = value` syntax (and kitty's
  `key value`, so an existing kitty theme loads unchanged). `theme_find()` resolves a name through the `themes/`
  subdirectory of the same three config directories (or takes a path / `~`-path as given);
  `theme_apply_file()` applies the 23 names in its `THEME_KEYS` table — the four chrome colours into `Config` and
  `color0`-`color15` into the process-wide palette — and skips any colour the settings file set itself
  (`config_key_was_set`), which is what makes a `background =` line win above OR below the `theme =` line.
  `selection_foreground`/`cursor_text_color` are recognised and reported as dropped, never guessed at; entries
  16-255 are refused because the cube and the ramp are computed. Also the `--import-theme` importer
  (`theme_sniff`, `theme_parse_text`, `ThemeVals`, `theme_write_file`, `theme_dest_dir`, `theme_name_of_path`,
  `theme_import`). No Wayland/GL/Term dependency: all of it is testable headless, and `tests/test_vt.c` derives
  the accepted-name list from this file's table exactly as it derives the config names from config.c's ladders.
- `src/config.c` — the two config files (`kai-term.conf` settings, `kai-term.bind` bindings), one `key = value`
  line reader serving both, the search order (`config_find`: `$XDG_CONFIG_HOME/kai-term`, `~/.config/kai-term`,
  `/etc/kai-term`, first REGULAR file wins, empty result = built-in defaults), defaults, clamping, the
  name-routing dispatcher `apply()` (`apply_bind_key` then `apply_setting`), the `#` comment rule
  (`config_strip_comment`), `cfg_parse_color` (the ONE colour parser, shared with theme.c and the OSC handlers),
  and the key-binding spec parser (`keybind_parse`/`keymods_parse`).
- `src/mux.c` — sessions of one window: dense `Tab` array (`{Term, Pty, attention, cd_req, int_dead, int_gen}`),
  active index, open/close/goto/next/prev, `max_tabs`, unread-output flag, `mux_title()`,
  `mux_recolor_defaults()`. No Wayland/EGL/fd/fork — that is what makes it unit-testable. The three integration
  fields live in `Tab` because `mux_close()` shifts the array: a side array indexed by tab number would hand a
  closing session's request path to its successor.
- `src/layout.c` — pure geometry: in window px + cell px + padding + gap + mode + session list; out bar strip,
  `BarItem[]` rects + labels, `PaneGeom[]` rects (each with cols/rows), the file-manager band (`FmGeom`: browser
  rect + preview rect + their cols/rows) and `layout_hit_fm()`, `layout_grid_size()`, `layout_initial_size()`.
- `src/shellint.c` — the shell integration, deliberately dumb and testable: the three shell scripts (fish, bash,
  zsh) as string constants, the private per-instance directory (`mkdtemp`, 0700, removed at exit), one
  `cd-request.<id>` file per session (temp + `rename()`), the ONE control sequence and the measurements that
  chose it, and the command line `tab_spawn()` hands `pty_spawn()` so a recognised shell loads the script without
  losing the user's own configuration. Nothing here talks to the shell except through those files and the pty;
  the protocol's other half is `kai_apc()` in `src/vt.c` and the state machine in `src/main.c`.
- `tests/test_vt.c` — 1133 checks: parser (a C0 byte inside a control sequence — executed, sequence still
  collecting, nothing painted — and a CSI sequence split across two bursts), clipboard text policy
  (`paste_repair`: UTF-16LE/BE, BOMs, surrogate pairs, sparse NULs stripped not decoded; `clipboard_mime_rank`
  ordering), grid, selection, alt screen, `live_off`, emoji clusters, CSI 3 J, bracketed paste, `term_init`
  guard, reflow, `term_grid_geometry`, zoom/alt-screen row-shrink, `mouse_sgr_button`/`mouse_wheel_button` (the
  SGR order and the 64/65 wheel codes), DECCKM (`app_cursor` set/cleared by `ESC[?1h`/`ESC[?1l`, saved and
  restored across the alternate screen, `term_arrow_seq()` answering SS3 or CSI), the mouse-as-keystrokes
  helpers (`term_wants_key_mouse`, `term_wheel_arrow_keys`, `term_click_arrow_keys` — the alt-screen/no-mouse
  gate, the vertical-then-horizontal order, the end-of-line column clamp, the buffer bounds), the graphics path
  (base64/chunking/PNG/zlib/raw/premultiply/placement/delete/scroll anchoring/id replacement/DA1-DA2-DSR
  answers), mux, keybind/keymods parsing, config keys and template name-coverage (the two config ladders, the
  theme table, and `usage()` vs README's option table, and since s110 README's own settings and binding tables
  diffed against the parser in both directions), the `hex escape` gate, `layout_compute` /
  `layout_grid_size` / `layout_initial_size` (the tab bar, the panes and the band's SHARE: `fm_size` percent,
  both clamps, the terminal's floor, the share surviving a resize and a zoom, three startup round trips), the
  palette/cube/OSC-colour groups, the theme file and precedence, the theme importer (sniff, per-format mapping,
  dropped keys, the native round trip), the shell-integration scripts as they land on disk (each binds exactly what
  `shellint_seq()` returns, fish repaints, bash does NOT pretend to), and the prompt machinery the follow is built
  from
  (`term_at_prompt()` / `term_prompt_line()` / `term_prompt_line_typed()` / `term_prompt_line_is()`, the echo
  window and its transition rule, the clear -> `cd` -> re-populate sequence as the model sees it). `main.c`
  itself is in no test binary — that machine is covered by `.scratch/s86/follow2.c` and the live harness. Two
  invariants with dedicated checkers: the `row[]` permutation (also `tests/dbg.c`) and the startup-size round
  trip.
- `tests/test_fm.c` — 305 checks against a real temporary directory tree (dirs, a dotfile, a dangling symlink, a
  FIFO, an unreadable dir, four image formats, a binary file): scan + sort + caps + truncation, the hidden filter
  and `fm_set_hidden`, selection and scrolling (`fm_row_pos`, `ensure_visible`), navigation, the request-id rule
  (a stale answer is dropped), every preview kind and its error path, the highlighter reaching the cells, the
  pane painters, `fm_image_fit`, the preview's row budget (no painted glyph may sit inside the box the picture is
  drawn into — `ink_in_box()`), the preview pane's own scroll (`pv_scroll` / `fm_preview_scroll` /
  `fm_preview_scroll_max`: the painter obeys it for text and for a directory listing, both ends clamp, a new
  preview starts at the top, a pane with no body has nothing to scroll, and a resize pulls the scroll back inside
  what the NEW pane can show), the selection colour reaching `fm_paint()`, the band's rects inside
  `layout_compute()`, and the wheel-over-the-band keyboard rule (`fm_wheel_focus`: a gesture that scrolled
  nothing never moves the keyboard, the default needs a CLICK to focus a pane, and `fm_focus_on_hover = 1`
  restores the older pointer-decides behaviour).
- `tests/test_render.c` — 25 checks, the only coverage of the renderer's colour decisions that needs no display:
  it links `render.o` (plus `fm.o font.o img.o vt.o shellint.o layout.o mux.o config.o`, which is what
  `render.o` needs to LINK) and calls only `render_chrome()`, which reads a `Render` struct and nothing else —
  never `render_init()`, never a GL call. Covers: the configured `cursor_color` is what an active pane's cursor
  is drawn with, an inactive session's cursor is that colour mixed with the configured background, the built-in
  default still yields `CURSOR_COL`, two different values change both, a different background moves the dimmed
  colour but not the active one, window opacity never reaches these values, and the selection band / active bar
  item / focus border follow `Config.selection_color`. The last checks are SOURCE SCANS of `src/render.c`,
  `src/main.c` and `src/fm.c` — the honest way to cover the `render_init()` call, which lives in `main()` and so
  is in no linked test; every scan reads a probe file the test writes, never this file (a self-scan counts its own
  `CHECK` lines).
- `Makefile` — pkg-config-driven; `-Iinclude/kai-term -Ibuild/generated`; output dirs via order-only
  prerequisites so they never force rebuilds; generates xdg-shell bindings with `wayland-scanner`.

**Facts not in code** (deps + MIT license: README "Requirements"/"License"):

- **Dependency rules:** harfbuzz required since s37 — README must keep listing it. Deliberately no
  cairo/pango/Qt/GUI framework. xdg-shell bindings generated at build time, not vendored.
- **Naming:** project/binary/identifiers `kai-term` (`kai_term` in C), display name `Kai-Term`. Renamed from
  `K-Term`/`k-term` 2026-10-16 (s59) due to a name conflict — never "restore" the short form anywhere: dir,
  binary, header, `DEF_APP_ID`/`DEF_TITLE`, log prefix, `TERM_PROGRAM`, config path all move together;
  `tests/test_vt.c` pins two `DEF_*` literals so a half-rename fails a check.
- **Layout:** sources `src/`, public header `include/kai-term/`, tests `tests/`, binaries `bin/`, objects +
  generated bindings `build/`. `make clean` deletes `bin/` and `build/` entirely. No `docs/` any more: the
  architecture doc was removed in s63 because it carried host-system facts; its invariants are in "Architecture
  & Invariants" below, its tooling parts in `/home/kaida/Project/ENVIRONMENT.md`.
- **License:** MIT, "Copyright (c) 2026 Kaida" — user's choice; must ship with any redistribution. No source-file
  SPDX headers exist; if added they must read `SPDX-License-Identifier: MIT`.
- **Known limitations a newcomer must not "fix" by accident:** narrowing can evict OLDEST scrollback rows; the
  alternate screen is deliberately NOT re-wrapped; a taller window keeps less scrollback than configured (`rcap`
  fixed at startup); the window title is Kai-Term's own text, not the app's OSC title; UTF-16 with no BOM and no
  NULs at all is undetectable; bash's prompt string stays stale after an integrated `cd`. Full list: ISSUES.md
  "Known limitations".

## Architecture & Invariants

Rules a change must not break. Why each exists: git history of fixed bugs (`git show d5afe4a:ISSUES.md`).

**Data flow.** PTY bytes -> `term_parse()` (vt.c) -> one `Grid` per session -> `Mux.tab[]` owns one `{Term, Pty}`
per session, one ACTIVE -> `layout_compute()` (window px + cell px + padding + mode + session list) -> bar strip,
item rects, pane rects -> `render_draw(&rend, &font, &g_layout, terms)` -> bar + one grid per pane + focus
border -> EGL -> `wl_surface`. Input: `kb_key`/`ptr_*` -> pty_write to the ACTIVE session, after `ptr_*` is
hit-tested bar then panes.

**Event loop (main.c).**

- epoll over: `wl_display` fd; one pty master fd per session (`tab_for_fd()` maps back; pty EOF closes that
  session, not the program); the 8 ms timerfd (`FRAME_INTERVAL_NS`); `signalfd(SIGCHLD)` (SIGCHLD blocked so only
  signalfd reads it; reap `waitpid(-1,…,WNOHANG)`, match pids via `tab_for_child()`, then zero `pty.child`); the
  paste idle timerfd; the key-repeat timerfd; the clipboard paste pipe read end joins transiently.
- **Finish the wayland read before handling ANY other descriptor** (`read_events`/`cancel_read`, then
  `dispatch_pending`): `eglSwapBuffers` waits for a `wl_buffer.release`; while a `wl_display_prepare_read()` is
  outstanding that release can never be read -> swap blocks forever -> the terminal looks dead. `read_events()`
  inside a drain loop instead blocks on the display fd and starves timerfd/signalfd.

**Frame pacing — 3 rules, each from a real freeze.**

1. Arm a frame callback only together with a commit that attaches a NEW buffer (from `redraw()` after a
   successful swap). Damage-driven compositors run a render pass only on change, so a callback armed on an EMPTY
   commit can stay pending forever, and every draw path is gated on "no callback in flight" -> frozen display.
   `solicit_frame()` was deleted for exactly this — do not re-add it. Failed swap: keep `W.dirty` for the timer
   to retry, arm nothing.
2. `eglSwapBuffers` is not guaranteed to return — it waits for the compositor to release a buffer. The constraint
   is on wayland READ, not on listeners: swapping inside a listener is fine once the read is complete. At most one
   buffer in flight (`W.frame_cb`).
3. The FIRST buffer must be attached by the FIRST `xdg_surface_configure` handler (`!W.swapped_once`): a window
   without a buffer never maps, so it never gets a frame callback either. A configure changing pixel size sets
   `W.resized`; the timerfd branch and `frame_done()` draw when `dirty || resized` — waiting on `dirty` alone is
   what froze the screen in fullscreen.

**Wayland/input.**

- Pointer coords are fixed point: `wl_fixed_t` is `int32_t` encoding px*256 -> apply `wl_fixed_to_int` before
  storing (storing the raw int compiles warning-free and is 256x too large).
- Key repeat is the client's job: `wl_keyboard.key` delivers one press + one release per physical keypress;
  `repeat_info` only reports the system setting. `send_key()` returns whether the press is repeatable; `krfd`
  arms for `repeat_delay`, re-arms at `1/repeat_rate`; release / focus loss / keymap change call `repeat_stop()`,
  which DRAINS an expiry that already fired. `it_interval.tv_nsec` must stay < 1e9. The key is re-TRANSLATED on
  every repeat -> Shift while holding `a` starts producing `A`.
- Seat bound at v5 for `axis_discrete`/`frame`; every listener slot the bound version can dispatch must be
  non-NULL or libwayland aborts.
- Clipboard is push-based (no `request_selection`): copy = `set_selection`; paste = receive the offered
  `text/plain` into a pipe whose read end joins epoll. One paste = ONE bracketed block: accumulate, write only at
  EOF (or the 400 ms watchdog), in <=4096-byte pieces, wrapped `ESC[200~` … `ESC[201~` when DEC mode 2004 is on;
  over 8 MiB sent unwrapped with a warning.

**Multiplexing/layout.**

- `Mux` owns a dense `Tab tab[TAB_MAX]` plus `n`/`active`; `cur_term()`/`cur_pty()` resolve through `mux_cur()` —
  no second copy of "which session is visible".
- `term_init()` memsets the whole `Term`; `term_destroy()` frees both grids -> a session is initialized and
  destroyed exactly once; `mux_close()` zeroes the vacated slot. Because `mux_close()` shifts later tabs down it
  must RE-ARM `tab[i].term.ps.t` for every moved tab and must capture `Pty` BEFORE the shift.
- Output on a session with no pane: set `Tab.attention`, do NOT dirty the frame. Output on a drawn-but-inactive
  session: dirty it, set no flag.
- `layout_apply()` is the only caller of `layout_compute()`; it resizes EVERY session to `layout_grid_size()` (so
  a tab switch costs no resize or reflow) and sets `W.gx/W.gy` to the active session's pane origin, which
  `sel_cell()`/`mouse_cell()` map through. Runs from every geometry change AND from `redraw()`, because bar
  labels are part of the picture.
- `ptr_button()` checks `layout_hit_bar()` BEFORE the mouse-reporting branch — an app that grabbed the mouse
  still cannot eat the bar. Re-check `cur_term()` after a middle click (closing the last session makes it NULL
  inside a listener).
- Chrome colours come from the CONFIGURED palette (`Render.fg`/`Render.bg`/`Render.cursor_color`/
  `Render.selection_color`, derived in `render_chrome()`), never from a `Term`'s pen — that is the application's
  current SGR state and would let `printf '\e[31m'` repaint the bar that names its tab.

**Parser (vt.c).**

- **A C0 control byte inside a control sequence is EXECUTED and the sequence KEEPS collecting** (DEC VT500 and
  xterm both do this). ESC is the one exception: it CANCELS the sequence and starts a new escape, so it is handled
  by the caller and never reaches the shared executor. ONE file-local `exec_c0()` is what both `PS_GROUND` and
  `PS_CSI` call — the rule used to exist in two copies that disagreed (CSI's copy was `state = PS_GROUND; i--;`,
  which ENDED the sequence), and that is why the states drifted. Ending a sequence at a control byte does two
  damages at once: the mode the sequence was setting is silently never applied, and the sequence's remaining bytes
  are painted as text. Because every mode-ENABLING sequence ends in `h` (`?2004h`, `?1049h`, `?25h`, `?1h`), the
  painted character is an `h` — a stray `h` on the user's line is the visible half of an invisible mode bug.
  Pinned by 14 checks, which also pin the OPPOSITE case: a CSI sequence split across two pty reads with NO control
  byte in it must stay open and finish in the second burst. Do not "simplify" either one.
- **A sequence is only complete when its final byte arrives; a burst boundary never ends one.** `term_parse()`
  re-enters with `ps->state` intact, and that is the whole mechanism — a probe or a fix that treats the end of a
  read as the end of a sequence invents a bug.

**Clipboard (main.c + the paste helpers in vt.c).**

- **A NUL byte must never reach the pty.** `paste_flush()` runs `paste_repair()` on the buffered clipboard block
  before writing any of it. A source may serve UTF-16 under a UTF-8 label (Firefox does, under
  `text/x-moz-url-priv`), and UTF-16 written verbatim into a bracketed paste is what the user saw as a stray `h`:
  bash's bracketed reader stops at the first NUL, zsh inserts every byte and paints each NUL as `^@`. Repair only
  when the block actually contains a NUL — a block with none is copied through byte-for-byte, unchanged.
- **Which mime type to ask for is a preference, not an enumeration accident.** `clipboard_mime_rank()` decides;
  `offer_offer()` keeps the highest rank seen. The old rule overwrote the recorded type for every `text/*` name,
  so the LAST type the source advertised won — with Firefox that is `text/x-moz-url-priv`, which is UTF-16. Never
  "fix" this back to last-wins or first-wins: both are at the source's mercy.
- **UTF-16 is decoded only when the evidence is unambiguous** — a BOM, or NULs that are dense (`nuls*2 >= len`)
  and one-sided. Sparse NULs are stripped, not decoded: guessing on ambiguous input turns good text into garbage,
  which is worse than removing the NUL. UTF-16 with no BOM and no NULs at all is undetectable and is a documented
  limitation, not a bug to chase.
- **`Ctrl`+`Shift`+`V` is Kai-Term's paste; plain `Ctrl`+`V` is the shell's.** Only the first is intercepted
  (`send_key()` gates on `shift_held`). The shell's path reads the clipboard itself and drops NUL bytes, so the
  two can differ from the same clipboard — when a user reports "one works and the other doesn't", that difference
  is the diagnosis, not a second bug.
- **When a paste arrives wrong, measure the SOURCE before the parser:** `wl-paste -l` for the advertised types
  and `wl-paste --type <name> | od -c` for the bytes served under each. That single command pair settles in
  seconds what cost two wrong theories here.

**Grid model (vt.c).**

- `Grid.row` has fixed capacity `rcap = rows + scrollback_max` and is always a PERMUTATION of the allocated
  buffers: scrolling is rotation (`rot_left`/`rot_right`), never `memmove` of pointers -> no slot is NULLed,
  duplicated or double-freed (`tests/dbg.c` asserts it). `grid_vprow(g,y) == row[scroll_off + y]`.
- Anchor invariant: `0 <= scroll_off <= live_off <= sb_limit = min(scrollback_max, rcap - rows)` and
  `live_off + rows <= rcap`. `live_off` is `scroll_off` showing the newest output; user navigation moves
  `scroll_off` below it; `snap_to_live()` restores it before parsing so new output lands at the live bottom.
- `cy` is a LIVE-view coordinate (the parser addresses `row[live_off + cy]`) -> `term_resize` re-anchors `cy` with
  `live_off`. The cursor is drawn only when `scroll_off == live_off`. Whole-viewport scrolls snap `scroll_off`
  back to `live_off` first.
- `Grid.valid == GRID_VALID` between init and destroy — a plain 0/1 flag is not enough because uninitialized
  stack memory is routinely non-zero (stack garbage measured `32511`).
- Alt screen: `Term.g` stays the ACTIVE grid, the main grid is parked in `Term.alt` (alt has scrollback 0);
  `?1049` saves/restores the cursor, `?47`/`?1047` switch without saving, `?1048` is cursor-only; `term_resize`
  resizes the parked grid too.
- CSI 3 J: `erase_scrollback()` moves the viewport rows to slots `0..rows-1` through a validated scratch array,
  blanks the rest, sets `scroll_off = live_off = 0`; gated by a process-wide setting, deliberately NOT a `Term`
  field because RIS memsets the whole `Term`.
- Reflow: `Grid.wrap[i] != 0` means the line in `row[i]` was broken by the RIGHT MARGIN and continues in
  `row[i+1]`; a byte array parallel to `row[]` that rotates with it and is cleared by every operation that blanks
  or erases a slot. `grid_reflow()` measures each row with `row_used()`/`cell_pristine()`, not full width —
  otherwise never-written cells and wide-glyph padding become literal spaces and the tracked cursor cell gets
  trimmed; it joins and re-breaks at the new width, pulling a boundary back only when the cell that would START
  the next chunk carries `ATTR_WIDE` — peek the NEXT source cell, because reacting to a wide pair that
  legitimately ENDED a chunk loses a character; it builds output oldest-first in a fixed `2*rcap+2` array,
  evicting oldest rows once `rcap` exist, and commits by installing new buffers (permutation kept). The eviction
  step must run for EVERY produced row, blank rows included, or `kept` can exceed `rcap` and the commit drops
  buffers instead of installing them (a leak). Those 3 traps each cost a real bug. `grid_resize_plain()` is the
  no-reflow variant for the ALTERNATE screen (a full-screen app repaints itself; re-wrapping would scramble it) —
  the parked main grid is still reflowed.
- **Images are anchored to an ABSOLUTE document row** (`Grid.row_base` + the row the cursor was on), never to a
  viewport row or a row pointer: a viewport row is invalidated by every scroll and a pointer by every reflow.
  `row_base` moves only when rows are evicted at the top or the window slides over its scrollback, so
  `image_prune()` can decide "this picture's rows no longer exist" from two numbers. The span in cells is
  computed in vt.c, not in the renderer, because it is also what `a=T` moves the cursor past — if the renderer
  scaled differently from the model the prompt would land on the wrong row.

**File manager band (fm.c + layout.c + render.c + main.c).**

- The band is geometry, model and drawing in the same three places as everything else: `layout_compute()` splits
  the grid area VERTICALLY first (a band taking `fm_size` PERCENT of its height on top, terminal pane(s) below at
  `pane_y = grid_y + band_h + gap`), then splits the band horizontally (browser | preview). Sessions stay
  ordinary `pane[]` entries, only shorter — that is what keeps `layout_hit_pane`, `layout_pane_of_tab`,
  `layout_grid_size()`, the focus border and `W.gx/W.gy` working with the band on AND off. `layout_hit_fm()` must
  be checked BEFORE `layout_hit_pane()`.
- The band is clamped, never trusted: `band_h = grid_h * pct / 100` (a `long long` multiply, so a 4K window cannot
  overflow) is reduced so the shell keeps `FM_TERM_MIN_ROWS` rows, and dropped entirely when even that leaves
  fewer than `FM_ROWS_MIN`. Clamped in layout.c, not config.c, so no caller can ask for a 1-row band — the tab
  bar's "the terminal wins" rule again. It is a SHARE and not a row count on purpose: the band then survives a
  resize or a zoom with the proportion the user asked for. `layout_initial_size()` is its inverse
  (`grid_h = ceil((rows*ch + gap) * 100 / (100 - pct))`) and the round-trip checks in `tests/test_vt.c` are what
  prove that arithmetic.
- **The preview pane's rows have ONE owner.** `preview_head_rows()` (2, +1 when the preview has an error line)
  and `preview_meta_rows()` (image previews only: `min(nmeta, prev.rows - head - 1)`, so the picture always keeps
  a row) are called by `paint_preview()` AND by `fm_image_fit()`; the picture box starts at `head + meta_rows`.
  Glyphs are emitted in the batch AFTER the picture, so any disagreement between the two about which rows text
  owns paints metadata across the image — that was the bug, and `ink_in_box()` in `tests/test_fm.c` keeps it from
  coming back.
- The two panes are `FmPane` = a flat `Cell` array + cols/rows, walked exactly like a session grid, so the
  renderer needs no new drawing path: "a grid with no cursor and no selection". Glyphs go through the same
  `emit_cell_glyph()` as a session (a file name can hold any codepoint, so the emoji/wide rules must be shared,
  not copied), backgrounds into batch A, glyphs into batch B, the preview picture between them.
- **The worker thread may call exactly two functions** — `fm_scan_build()` and `fm_preview_build()` — which touch
  nothing but malloc, the filesystem and their own argument. The main thread owns `FileMgr` and installs every
  answer, so the request-id rule in `fm_install_scan()`/`fm_install_preview()` is the ONLY synchronisation the
  model needs. Ids come from one monotonic counter (`FileMgr.seq`) with per-kind newest ids (`scan_req`,
  `pv_req`); a single shared counter was wrong because every preview then invalidated an in-flight listing.
  Answers cross a pipe as one 24-byte `FmResult` (atomic, far under `PIPE_BUF`); the request slots hold only the
  NEWEST request of each kind, so a superseded directory is never read at all.
- **The keyboard gate is the feature's safety:** `fm_keys()` is matched only while `g_fm.on` and
  `g_fm.focus != FM_FOCUS_TERM`, which is the only reason the default bindings can be unmodified keys.
  `fm_keys()` returns -1 (not the band's), 0 (handled, must not repeat) or 1 (handled, repeatable) — two states
  cannot express that, and getting it wrong sends Return/BackSpace/`r`/`Escape` to the shell as well as to the
  browser (measured live, s74).
- **The band has THREE focus states, and the keyboard routing differs between them for exactly four keys.**
  `FM_FOCUS_TERM` / `FM_FOCUS_LIST` / `FM_FOCUS_PREVIEW`; a CLICK on the preview pane gives it the keyboard.
  While the PREVIEW owns it, the band's `fm_key_pageup`/`pagedown`/`home`/`end`
  bindings scroll the previewed TEXT (`fm_preview_page`/`fm_preview_top`/`fm_preview_bottom` in fm.c, routed in
  `fm_keys()` in main.c) instead of moving the listing; `fm_key_up`/`down` deliberately keep moving the listing,
  because changing WHICH entry is previewed is the one thing the preview cannot do for itself. One page is
  `fm_preview_page_rows()` — the body rows the pane shows at once — because a Page key must move by exactly what
  the user can see; all three go through `fm_preview_scroll()`, so the clamp has one owner and a preview with no
  body (a picture, plain metadata) simply does not move. Same four bindings, no second set of names to keep in
  sync in `kai-term.bind`.
- **The pointer's POSITION never takes the keyboard — a click does** (`fm_focus_on_hover = 0`, the default).
  A scroll gesture over the band scrolls (it walks the listing, or scrolls the preview body) and leaves the
  keyboard where it was; `fm_focus_on_hover = 1` opts back into the pane under the pointer owning it. The rule
  is `fm_wheel_focus(hit, steps, on_hover)` in `src/fm.c`, not an `if` in `main.c`, for the standing reason
  that main.c is in no test binary. What it replaced is the shape worth remembering: `fm_wheel()` called
  `fm_focus_set()` on its fifth line, BEFORE computing `steps`, and returned early when `steps == 0` — so a
  `wl_pointer.frame` that produced no whole-cell scroll moved the keyboard and did nothing else, which a user
  can only describe as "hovering steals the focus". **A state change placed before the guard that decides
  whether the event counts is a bug even when every later check is correct.** There is no hover handler in this
  project and there should not be one: every `hover` hit in `src/` is a comment naming this bug, and
  `render_draw()` draws the focus border from `fm->focus` alone — nothing on screen follows the pointer.
- `fm_send_cd()` is the only place the feature writes to the shell, never to a session on the alternate screen,
  and only when the directory actually changed (`fm_last_cd`, cleared by `tab_switch()`).
- **The prompt invariant (what the follow may never violate).** Kai-Term cannot see where a prompt ENDS — the tty
  echo arrives as ordinary output — so the model remembers the whole cursor row as the shell last printed it
  (`prompt_row`/`prompt_cx`/`prompt_cy`) and treats "does that row still match, cell for cell" as the only
  evidence available. Five rules, each with a check in `tests/test_vt.c`:
  1. **The `cd` is written only from ERASING/CDING behind `term_at_prompt()`** — i.e. only once the row is proven
     to be the bare remembered prompt. "Runs a mangled command" is closed by construction, not by trusting a key
     binding, so no capture rule may ever gate the write.
  2. **A steady state is not evidence.** The echo window closes only when the row CHANGED into the remembered
     prompt between the start and the end of one burst (`prompt_matched_before` in `term_parse()`). A burst that
     leaves the row exactly as it was — fish repainting OSC 7 / OSC 0 / DECSET after Kai-Term re-typed the user's
     text — must not close it, or the next burst re-takes the snapshot over Kai-Term's own echo and a line holding
     text starts answering "empty".
  3. **A burst boundary is not a prompt.** CDING additionally waits for the session to go quiet
     (`FMCD_QUIET_MS` 150, advanced from the frame timer) before believing the new prompt is drawn: fish answers a
     `cd` with the echo, a continuation row and prompt chrome in separate bursts, each of which re-takes the
     snapshot.
  4. **The snapshot's cursor anchor means "where the cursor sits when the line is EMPTY".** `prompt_cx`/
     `prompt_cy` are the column `term_at_prompt()` insists on AND the column `term_prompt_line()` measures typed
     text from, so `prompt_snapshot()` must not re-take them when the row is cell-for-cell the remembered prompt
     and only the cursor moved. `prompt_row_same_cells()` is that test and also the row half of
     `prompt_row_matches()` — one comparison, two callers.
  5. **Opening the echo window has to mean something.** `term_note_input()` opens it for any payload that can put
     a character on the line (text, Tab, Up/Down, Ctrl+R/S/T/V/Y/_, Insert, PageUp/PageDown, F-keys, an unknown or
     truncated sequence) and — only when the line is already provably the bare prompt — not for one that cannot
     (cursor movement, deletion, Ctrl+L, a bare Escape). The row comparison still re-checks everything on every
     call, so a key that really moves the cursor or rewrites the row still makes `term_at_prompt()` answer "no".
  The three description predicates must not be confused: `term_at_prompt()` = "safe to WRITE here"; strict
  `term_prompt_line()` = "I can describe this line and nothing survives an erase"; `term_prompt_line_typed()` /
  `term_prompt_line_is()` = "ignore the shell's ghost text right of the cursor", legal only for the capture (which
  is followed by the proof) and for confirming Kai-Term's own typing. `input_pending` is deliberately NOT consulted
  by the description predicates — the row comparison is the evidence, and Kai-Term's own typed path sets that flag
  while leaving a perfectly describable line behind.

**Shell integration (shellint.c + the `K` APC channel in vt.c + main.c).**

- **One channel, so the data never rides with the control.** The pty input stream is the only thing Kai-Term can
  write to the shell, and a key binding is a fixed byte sequence — anything appended would land on the user's
  line. So the sequence carries NO data and the directory travels in a per-session file (`cd-request.<id>`,
  written as a temp + `rename()`d so the shell can never read a half path). A stray sequence can therefore never
  run a path, and no path can ever be smuggled into a sequence.
- **The fast path is used only after THIS session's shell announced itself** (`ESC _ K ; kai-term ; integration ;
  <kind> ST`, dispatched by `kai_apc()` — one more branch on the APC kind byte, no new parser state), and a
  navigation is believed only by the matching ack (`ESC _ K ; kai-term ; cd ; <dir> ST`). No ack inside
  `FM_INT_ACK_MS` (400 ms) -> `fm_int_failed()`: mark `Tab.int_dead`, log, and run the typed machine for that
  navigation. The ack is cleared (`term_shell_cd_ack_clear()`) before every request — one slot answers one
  request, or a navigation back to an old directory is "confirmed" by an ack nobody owed it.
- **The sequence is one measured byte pair for every shell: `C-x C-]` (0x18 0x1d).** Four requirements, each
  measured (s91): parses as ONE key in the shell's reader (fish ignores literal byte sequences it cannot parse as a
  key), inert in EVERY supported shell when unbound (that is what bounds the damage when a nested shell is the one
  reading), unbound by default (Kai-Term steals nothing), and free of the tty's own special characters. The last
  one is the trap: `C-\` (0x1c) is inert and unbound in both shells but `stty -a` reports `quit = ^\`, so it would
  SIGQUIT whatever owns the terminal whenever the shell is not reading. Check `stty -a` before adding a byte;
  `0x18`/`0x1d` are clean (`swtch = <undef>`).
- **The integration write never opens the echo window.** `term_note_input()` is for bytes whose echo may appear on
  the line; these are consumed by the line editor, so the write goes through `pty_write()` alone. Opening the
  window would make `term_at_prompt()` answer "not empty" for no reason and hold the typed fallback shut behind
  the s89 whitelist. The ack, not the echo, is the evidence for this path.
- **Per-session state lives in `Tab` (`cd_req`, `int_dead`, `int_gen`), not in an array indexed by tab number.**
  `mux_close()` shifts the array and a freed index is reused, so an index-keyed side array would give two live
  sessions the same request file — one shell then cds into a directory the other asked for. `g_int_seq` numbers
  the files and never repeats within a run, which is why `shellint_cleanup()` SCANS the directory for
  `cd-request*` instead of enumerating `0..TAB_MAX-1`.
- **The scripts must not replace the user's configuration:** fish gets `-C "source …"` (evaluated after the user's
  config), bash gets `--rcfile <ours>` which sources `~/.bashrc` first, zsh gets `ZDOTDIR=<ours>` with a `.zshrc`
  that sources the user's real one first (`KAI_TERM_REAL_ZDOTDIR` remembers where that was). `-e COMMAND` is never
  wrapped — that command line is what the user asked to run. The private directory is `mkdtemp` under
  `$XDG_RUNTIME_DIR` else `/tmp`, `chmod 0700`, refused if the name contains a `'` (the command line quotes with
  `'`), and removed at exit.
- **`ESC c` (RIS) memsets `Term`, and the announcement must survive it** — the script announces once, at shell
  startup, so losing it would silently drop the feature for the rest of the session. The prompt snapshot is
  deliberately NOT preserved (a reset really does invalidate it).
- **An announcement is an EVENT, counted, not just a kind.** `Term.shell_int_gen` goes up on every accepted
  announcement and `Tab.int_gen` is the value main.c last saw; when they differ, `fm_int_try()` clears
  `Tab.int_dead` (and logs it). Without the counter the first shell that failed to answer — a plain `env bash`
  started inside an integrated fish — would keep that session on the typed path for the rest of the run. The kind
  cannot express this (fish -> a nested fish -> back to fish announces three times with one kind), which is why it
  is a counter and not a flag.
- **The scripts wrap a bare `fish`/`bash`/`zsh` and re-announce after every command.** That is what makes a shell
  the user starts at the prompt integrated too, and what feeds the counter above: fish `fish_postexec`, bash
  `PROMPT_COMMAND` (PREPENDED, string AND array form handled, guarded by a NON-exported variable so a nested bash
  still installs its own), zsh `precmd_functions+=`. Three limits, all deliberate: only a command with NO
  arguments is wrapped, a name the user already defined as a function or alias is never replaced, and the wrapper
  re-announces its own kind when the nested shell exits. A heartbeat was checked not to leak into a command
  substitution before it shipped.
- **The prompt is repainted only where the shell can be asked to.** fish: `commandline -f repaint` at the end of
  the handler (measured to re-expand the prompt with the typed text byte-identical). zsh: `zle reset-prompt`
  (verified in zsh 5.9 since s94 — the gate's zsh run repaints and answers in 54 ms; what has never been measured
  in zsh is the inert-when-unbound case). bash: NOTHING is attempted, because readline expands the prompt once per
  `readline()` call — `C-l` leaves the old directory and printing `${PS1@P}` over the line garbles it (both
  measured). Do not "fix" bash by pressing Enter for the user or by writing over the prompt line.

**Geometry: one helper decides a grid's size AND its origin.**

- `term_grid_geometry()` (vt.c) is called once per pane from `layout_compute()` with `pad = 0` (the window's
  `Config.padding` margin and the pane gap are already in the pane rect). 3 users must agree: `layout_apply()`
  (grid + PTY size), `sel_cell()`/`mouse_cell()` (pointer -> cell), `render_draw()` (where the quads go).
- Padding is applied on all 4 sides, capped at `(window - one cell)/2` per side;
  `cols = (win_w - 2*xoff)/cell_w`; the whole-cell remainder stays at RIGHT and BOTTOM, never top/left, so
  `row = (ptr_y - gy)/cell_h`, `col = (ptr_x - gx)/cell_w` is exact. Anchor anywhere else and every click, drag
  and SGR report lands on the wrong row.
- The margin is painted by `glClear`, so the clear colour must be the CONFIGURED background (`Render.bg`
  premultiplied by `Config.opacity`), not a hardcoded default; `render_draw` clears every frame.

**Rendering.**

- 2 glyph atlas textures (alpha8 mono, RGBA8 color emoji), each uploaded only when dirty, AFTER the emit passes
  and BEFORE the draw — glyph lookups run inside pass 3, so uploading at the top of `render_draw` shows every new
  glyph one frame late (and a first emoji as a black box). Application pictures are the other kind of texture: one
  per image the model shows, keyed by `KImage.serial` (unique per session, never reused, so a recycled pointer
  cannot hand back a stale texture), re-uploaded when `version` changes, and deleted after any frame in which no
  visible image claimed it (`ImgTex.seen` stamped with `Render.img_frame`) — the GPU never holds more than what is
  on screen.
- One shader; `a_glyph` selects the path: 0 background quad, 1 mono glyph (tinted by `v_fg`), 2 color emoji
  (premultiplied, not tinted), 3 an application picture (the same premultiplied composite against the picture's
  own texture; vt.c premultiplies every decoded pixel, so still no GL blending).
- Pass order back to front: all cell backgrounds, then selection bands, then glyph quads — a WIDE glyph quad spans
  into the next cell and the neighbour's background must not be emitted after it. Continuation cells
  (`ATTR_WIDE`) are skipped in pass 3.
- **A frame is TWO draw batches, not one call**, because a picture must be over the cell backgrounds and under the
  text and blending is never enabled: batch A = pass 1 + bar backgrounds (range `[0,nq_bg)` of the staging
  buffer), then one draw per visible picture, then batch B = bands + glyphs + labels + focus border. Both ranges
  live in ONE staging buffer (records are fixed-size, so a batch is just a range); `glClear` runs ONCE per frame —
  clearing between batches would erase batch A. `setup_draw_state()` is re-applied before each batch because an
  atlas texture can be created between them.
- Instanced path when both `GL_ARB_draw_instanced` and `GL_ARB_instanced_arrays` are advertised; otherwise the
  same 19-float records are expanded to 21 floats per vertex and drawn with `glDrawArrays`.
- No GL blending is ever enabled: the fragment shader composites `premul + bg*(1-a)` and a Wayland EGL ARGB buffer
  is PREMULTIPLIED, so writing raw colour with partial alpha reads washed out; `max(a, bg.a)` is wrong too (it
  painted the glyph's bounding box opaque). `Config.opacity` must be clamped with `fclamp_`, never `iclamp_` (an
  int clamp turns 0.5 into 0).
- Color emoji: `putch` folds ZWJ, variation selectors, skin tones, keycap, tag chars and the second regional
  indicator into the ANCHOR cell's `extra[]` (`CLUSTER_MAX` 8) so a `Cell` stays copyable by value;
  `font_color_glyph()` hands the whole sequence to `hb_shape`, which collapses it to one glyph id (anything else
  falls back to mono). FreeType loads it with `FT_LOAD_COLOR | FT_LOAD_NO_SCALE`; the renderer scales by
  `cell_h / color_px`, so zoom never re-rasterizes an emoji. A wide cluster reserves TWO grid cells
  (`cluster_is_wide()` is the single source of truth, also true when U+FE0F/U+20E3 switch the base to emoji
  presentation) and `render_draw` sizes the quad with the same predicate. Cluster state is invalidated on every
  row rotation, cursor jump, BS/HT/CR, newline and resize. Misses are cached; the color atlas starts at 1024x512
  and doubles up to 8192; a glyph that cannot fit falls back to mono rather than drawing with stale texcoords.

**Selection.**

- A drag only ever produces CELL edges: `sel_cell()` maps the pointer to a cell; `sel_set_endpoints()` gives the
  earlier cell its LEFT edge and the later one its RIGHT edge; direction is recomputed on every motion event, so
  dragging left past the anchor SWAPS the pair.
- Pass 2 skips a cell that was never written (`ch == 0`) so a drag past the end of a line stops at the last glyph,
  while real spaces and `ATTR_WIDE` continuation cells keep their band; pass 1's cursor block uses the SAME
  predicate.
- Band edges stay ABSOLUTE buffer pixels — correct across panes because panes never overlap in x.

**Config.**

- `key = value`, read once at startup, TWO files, each searched on its own: `-c FILE` / `-b FILE` >
  `$XDG_CONFIG_HOME/kai-term/` > `~/.config/kai-term/` > `/etc/kai-term/` — the first location holding a REGULAR
  file with that name wins and the rest are never opened, and nothing found anywhere leaves `config_find()`'s
  `out` empty, which is how the callers know to keep the built-in defaults. The search is per FILE NAME, not per
  directory: a `kai-term.conf` in `~/.config` and no `kai-term.bind` there still picks up the system bindings.
  `kai-term.bind` is read AFTER `kai-term.conf`, so for a name both set the bind file wins. CLI flags are applied
  AFTER both files so they win and never write back.
- The split is by NAME, not by which file is open: `apply_bind_key()` takes the 24 `key_*`/`fm_key_*` names,
  `apply_setting()` the 32 others. A binding written in `kai-term.conf` is applied with a log line naming where it
  belongs now (files written before the split must not lose bindings silently); a setting written in
  `kai-term.bind` is refused by name, never treated as an unknown key. `tests/test_vt.c` reads both name lists out
  of the strcmp ladders in `src/config.c` and compares them with `config/kai-term.conf` + `config/kai-term.bind`
  AND with README's tables — a name added to the parser without a template line, a template line the parser does
  not know, and a deleted template line each fail a check.
- `#` starts a comment when ONLY whitespace precedes it, or when whitespace precedes AND follows it. The old rule
  (whitespace before is enough) was a bug: it cut `background = #1c1c22` at the `#`, and an empty value is legal,
  so a documented colour line set nothing and logged nothing. `working_dir = /home/me/my#project` keeps its value;
  `value # note` is stripped. Unknown keys log and are ignored; a missing file is not an error. Colours accept
  `#RRGGBB`, `0xRRGGBB`, bare `RRGGBB` and `AARRGGBB` (`cfg_parse_color` skips an optional `#`/`0x`, reads hex,
  and adds `0xff000000` when 6 digits or fewer were given) — ONE colour parser for the config file, the theme
  file and the OSC handlers. Ranges are clamped at parse time.
- **One value owns the default ink and paper.** `Config.fg`/`Config.bg` reach the grid through
  `term_set_default_colors()` (file-scope `g_def_fg`/`g_def_bg` in `src/vt.c`, read by `term_default_fg()` /
  `term_default_bg()`), called from `main.c` after the config readers and before the first `term_init()`. That one
  variable is BOTH the colour every blank/erased/scrolled-in/reflowed cell is painted with AND the
  `cell_pristine()` sentinel meaning "never written" — they must stay one value, or reflow's `row_used()` counts
  trailing blanks as content and selection bands appear over nothing. `DEF_FG`/`DEF_BG` are only the built-in
  defaults `config_default()` starts from; nothing in `src/vt.c` may read them again. And nothing may paint a
  background from a different source: `glClear` uses `Config.bg`, pass 1 uses the cell's `bg`, so a third value
  anywhere draws a visible frame around the grid (s101). `palette[0]` is deliberately NOT the configured
  background — `SGR 40` / `48;5;0` stay black, as every other terminal does it; README says so, because it is the
  next thing a user will report.
- **The palette is process-wide, never `Term` state.** `g_palette[256]` in `src/vt.c`, read through
  `term_palette()`; `term_init()` and `ESC c` must NOT refill it, because they run for every new tab and would
  throw the user's theme away (`term_palette_reset()` exists for OSC 104 only). Two consequences to remember: a
  `Cell` stores a RESOLVED colour, so changing an entry repaints nothing already on screen (a theme file is
  therefore applied before the first session is created, and an OSC colour change sweeps the grid — see 1d), and
  entries 16-255 are computed, so a theme that writes them is fighting the cube — the theme layer accepts 0-15
  only.
- Defaults live in `config_default()` AND the `DEF_*` macros AND the files in `config/` — change one and the
  checks fail, which is the point. Do not hand-copy a name list into a test: derive it from the source.

## Debugging this project

- `./bin/kai-term -d` (log level 3) prints: the font chosen + `color emoji: <path> (<n>px strike)`; the GL
  version + instancing state; `shell starts in <dir>`; `grid resized to CxR (window WxH px, cell wxh, padding P,
  unused ux x uy px, tab i/n, tab view|side-by-side view, N panes)`; one
  `layout: tab view|side-by-side view, N pane(s), tab bar on|off, grid CxR per session` line per LAYOUT SHAPE
  change (client-side evidence for any bar/pane report; what `tests/cli_test.sh` greps for); session lifecycle
  (`mux: session 1/N started`, `tab i/n opened (pid P, pty fd F, grid CxR)`, `tab i/n visible`,
  `tab i closed, N left`, `the pty of tab i/n closed`, `the shell of tab i/n exited`,
  `the last session is gone, exiting`); bar clicks (`bar: click selects session i/n`,
  `bar: middle click closes session i/n`); `reflow: OLD -> NEW rows at N cols (K evicted)`;
  `compositor repeat_info: rate R/s delay D ms (kai-term uses …)`; `toplevel state: fullscreen F maximized M
  activated A (configure WxH px)` (the only client-side evidence of entering fullscreen);
  `configure answered: buffer WxH attached (grid CxR)`; `zoom: font Npx, grid CxR`;
  `selection copied: N bytes` / `selection finished: ax,ay -> bx,by (copy_on_release off — Ctrl+Shift+C to
  copy)`; and while a drag is live `sel press/drag cell=(col,row) frac=… ptr=(x,y) …` — how to check a
  selection-vs-pointer complaint without a screenshot (it also gives the window's absolute position as
  `click_target - ptr`). A `swap failed (N in a row), retrying on the next frame` warning means `eglSwapBuffers`
  returned failure; a few are self-healing, a long streak means the compositor is not showing this window.
  Colour/theme lines: `config loaded from <path>`, `bindings loaded from <path>`, `theme: '<name>' resolves to
  <path>`, and the line naming which file set a colour; an OSC repaint logs that an APPLICATION set it.
- **Images.** `graphics: N image(s) drawn, M texture(s) held` is logged once per CHANGE in how many pictures are
  on screen (not per frame) — the client-side evidence that the renderer reached the image path at all; an
  accompanying `image glTexImage2D failed: 0x...` names the GL problem. Others:
  `graphics: dropped image N (W x H px) to stay within the budget`, `graphics: deleted N image(s)`,
  `graphics: unsupported payload (f=..., n bytes)`, `graphics: APC payload over ... bytes, discarded`,
  `graphics: file transmission (t=x) is not supported`. `./bin/kai-term -d -e 'timg -p k FILE.png; sleep 4'`
  exercises the whole chain and prints those lines. For the model alone, `.scratch/s70/timg_gate.c` runs the real
  timg in a PTY with Kai-Term's winsize, feeds its bytes through `term_parse()` and writes back the answers the
  model queues — no window. The file-manager preview is counted separately and named — the line ends
  ` + the file-manager preview` when one is drawn, because it has its own texture (`Render.fm_tex`) and is not in
  the `M` count. `fm preview glTexImage2D failed: 0x... (WxH)` names a GL problem with it.
- **File manager.** `fm: opened on <dir>` / `fm: closed — the terminal is the whole window again`; a request
  `fm: listing <dir> (request N)` and its rejection `fm: dropping stale listing of <dir> (request X, newest Y)`
  (that pair is how you see the request-id rule working, and how a broken id shows up — s74's argument-order bug
  made every one of them read `request 0`); navigation `fm: open -> <dir>` / `fm: parent -> <dir>` /
  `fm: refresh -> <dir>`; the keyboard handover `fm: keyboard -> terminal|browser|preview`; a click
  `fm: click row N selects <name>`; what was typed at the prompt
  `fm: typed the selected path at the prompt: <path>`; the shell follow `fm: told the shell to follow: cd <dir>`
  (absent = the directory did not change, or the session is on the alternate screen, or `fm_follow = 0`); the
  clear -> `cd` -> re-populate sequence logs its steps — `fm: clearing the shell's input line to follow the
  browser into <dir>`, that `told the shell to follow` line, `fm: re-typing what was on the line: <text>`, and the
  completion `fm: the prompt now holds the browser's directory and what you had typed`. Anywhere it stops instead:
  `fm: dropped the follow while <erasing|cd|re-typing> (<why>)`, and when the captured text never made it back that
  line adds `— the shell's line does NOT hold what you had typed; it was: <text>` (the only way to recover it, so
  quote it in full in a report). A refusal to write at all is `fm: not telling the shell to follow — its input line
  is not one I can describe` or `fm: not typing the path — …`; a navigation arriving mid-sequence logs
  `fm: the follow already in flight now targets <dir>`; a failed read `fm: cannot open <dir>: <reason>`;
  `fm: worker started (result fd N)` — absent means the band was never opened, so no thread and no descriptor
  exist. The band's size is visible in the ordinary `grid resized to CxR …` line: with the band on at the default
  `fm_size = 75`, `R` is roughly a quarter of what it is with the band off (grid area 58 rows -> terminal 14
  rows), and `layout: … grid CxR per session` is what `tests/cli_test.sh` compares between a band-off and a
  band-on run.
- `./bin/dbg` — scripted scroll/resize/reflow/zoom stages; each prints `scroll_off/live_off/rows/rcap/used` plus
  one 2-character token per slot (`v` viewport / `s` scrollback, then the row's first cell), and asserts `row[]`
  is a NULL-free, duplicate-free permutation.
- `tests/cli_test.sh` drives the real binary — `main()` is in no linked test, so it is the only honest coverage of
  argv and of what a real launch does with the config it loads. (The config SEARCH order itself is not exclusive
  to it — the `cfgsearch` group in `tests/test_vt.c` covers that headless.) It makes ONE probe launch
  (`-d -e true`) first: if that cannot draw, every window-opening check is skipped with the probe's reason
  printed. `KAI_TERM_TEST_NO_WINDOW=1` skips them without mapping any window — use it unless the window path is
  what is under test.
- Headless harness: link `src/vt.c` + `src/config.c` (+ `src/theme.c`, `mux.c`, `layout.c`) into a throwaway
  program in `/home/kaida/Project/.scratch/` to exercise logic Wayland input events cannot reach. That is how the
  cube, the OSC colours, the sweep and the import corpus were all measured.

## Key Decisions & Pitfalls

- **"The parser knows this key" is never evidence that the key does anything.** `cursor_color =` was defaulted
  (`src/config.c`), parsed and stored in `Config`, and no line of code anywhere read it — `src/render.c` drew the
  cursor from the `CURSOR_COL` macro and `render_init()` was handed only `bg`/`fg`. Every test naming it asserted
  on the PARSED value, and the template parity check compares parsed values field by field, so the whole suite was
  green while the feature was absent. A config key is only implemented at its far end: trace the value from
  `apply_setting()` to the pixel that reads it (the s101 border bug was the same shape — a configured colour that
  reached `glClear` but not the cells). **When adding a key, write the check at the consumer, not at the parser.**
- **A check that passes on the buggy code pins nothing.** `ink_in_box()` — "no glyph may sit inside the box the
  picture is drawn into" — passed against the pre-fix `src/fm.c` when written around a small centred 8x8 preview:
  that picture happened to land below the metadata. The overlap needs a picture whose box starts at the TOP of the
  picture area, and only the A/B — the new test file against `git show HEAD:src/fm.c` — showed that. When a check
  is written to pin a bug, run it against the code that had the bug and report the failure count (s88: 8 failures
  there, 0 here).
- **A computed constant is a claim — print it before you pin it or write prose about it.** The 256-colour cube's
  level formula `55 + 41 * level` looks plausible, is wrong at level 5 (260), and the shift hides the overflow in a
  way that leaves alpha looking perfect: 91 of 216 entries were near-black and no check had ever asserted on a
  cube value, so the suite was green for the whole life of the project. The probe took one minute
  (`.scratch/s102/cube.c`), found the bug, and stopped me from writing five checks around values I had assumed.
  Same shape as the OSC `rgb:` converter that "rounded" `0xffff` to 256 and painted white as `#010100`.
- **A check that walks a table must use the producer's index layout.** The cube entry for (r,g,b) is
  `16 + 36r + 6g + b`; a loop computing `16 + 36 * L * stride` looks like it is sweeping one channel's levels and
  is reading something else. Derive the stride from the layout (36 / 6 / 1), and confirm the check fails on the
  broken code — a check that never fails is decoration.
- **A self-scan cannot pin a token counter: the test is always part of the corpus.** The `test_render` scanner
  counted its own `CHECK` lines because the probe tokens were named inside the calls that counted them (3 hits for
  a "code" token: declaration, `(void)` cast, CHECK argument). Fix: the test writes a probe file it controls and
  scans that, and every mention of a probe token in its own source is split by string concatenation.
- **In C a hex escape takes every following hex digit — `"\x1bc"` is ONE byte-sized escape (0x1bc), not ESC +
  `c`.** The palette check named "ESC c / RIS does not wipe the palette" had never sent a reset; it passed for an
  unrelated reason. Write `"\x1b" "c"`. The durable gate is the `hex escape` group in `tests/test_vt.c`
  (`src_hexesc_count()` over every C file, skipping comments and quoted mentions), and its non-vacuity is pinned
  by mutation, not by argument.
- **Measure BOTH builds.** `make -j8` builds only `src/`; the test binaries are a separate build, and the
  `\x1bc` warning lived there unnoticed while the "0 warning lines" claim covered only `src`. The same class again
  in s105: two `-Wformat-truncation` warnings in `src/theme.c` that appear ONLY in the asan build, where the
  inlining lets the compiler bound the argument.
- **A guard that reads a field the change has not applied yet is a no-op that looks exactly like a guard.**
  `fm_set_size()` clamped `pv_scroll` to `fm_preview_scroll_max(fm)` on the line where it stored the REQUESTED
  pane size — but `fm_preview_scroll_max()` measures `fm->prev.rows`, the pane that actually EXISTS (the same
  field the painter reads), and the `realloc` that changes it runs 20 lines later. So the clamp measured the size
  the pane used to have and could never change anything; every check written as "scroll, resize, assert it is
  inside the new range" still passed, because the range it compared against was the old one. Moved after the
  allocation. Two traps made it easy to miss: (a) the natural test — shrink the pane, expect the scroll to shrink
  — is the direction where the range GROWS, so it pins nothing in either version; the clamp only bites when the
  pane GROWS. (b) The next check, `fm_preview_scroll(5)` expecting 5, was red for an unrelated reason: the
  previous check had left `pv_scroll` at its maximum. When a clamp/validate call takes a max from a getter, ask
  what state that getter reads and whether the change has landed in it yet (s96).
- **`eglSwapBuffers` blocks, and a blocked swap looks like a dead terminal.** It waits until the driver gets a
  buffer back and only the compositor releases buffers, so a client whose surface is not being rendered stalls
  inside swap: no keyboard, no PTY reads, no redraw (measured `wchan = futex_do_wait`; `eglSwapInterval(0)`
  changes nothing on this NVIDIA driver). The rule is about wayland READ — the earlier "never swap inside a
  listener" wording was BACKWARDS and caused a regression. Answering EVERY configure with a swap is also wrong (it
  spammed `eglSwapBuffers failed: 0x3003` every frame).
- **Geometry lives in `src/layout.c`, not main.c.** main.c is in no test binary, so arithmetic there is
  untestable, and N panes need N origins, which one global offset cannot express. The round trip
  `layout_initial_size()` -> `layout_compute()` -> `layout_grid_size()` is what proves startup height is right —
  it caught a `cell_w`/`cell_h` typo (s62). Same reasoning put `mouse_sgr_button()` in vt.c, the mouse-as-keys
  helpers in vt.c, and the theme importer in theme.c.
- **The evdev button order is not protocol order.** `BTN_LEFT` 0x110, `BTN_RIGHT` 0x111, `BTN_MIDDLE` 0x112, but
  SGR numbers them 0 left, 1 middle, 2 right. A switch written in kernel order silently swaps RIGHT and MIDDLE —
  in the bar's middle-click-close AND in every SGR report to vim/htop.
- **A log line that prints the value it was GIVEN can lie.** `tab_switch(idx)` logged `mux: tab idx/n visible`
  while nothing had changed, so the log looked like proof the click worked and the bug survived every reading of
  it. Log what the OWNER of the state REPORTS (`mux_active()`).
- **A log line that counts two different things as one number lies the same way.**
  `graphics: N image(s) drawn, M texture(s) held` began counting the file-manager preview in `N` while `M` still
  counted only the application-picture texture cache, so a preview alone logged `1 image(s) drawn, 0 texture(s)
  held`. Count each thing with the counter that owns it (s74).
- **`Term` is not a place for settings.** `ESC c`/RIS calls `term_init()`, which memsets the whole `Term`, so
  process-wide settings are file-scope values with a setter (`kai_term_loglevel`, `g_clear_scrollback`,
  `g_def_fg`/`g_def_bg`, `g_palette`) applied from config in main.c. `bracketed_paste` is deliberately opposite:
  it is application-negotiated, so a reset SHOULD clear it.
- **A Kai-Term-to-Kai-Term clipboard test proves nothing** — it works even if the client never shares its
  selection. Test against a foreign client (`wl-copy` in, `wl-paste` out). With `copy_on_release = 0` (the
  default) a mouse drag does NOT call `set_selection`; only `Ctrl`+`Shift`+`C` does — that is what reads like
  "Kai-Term keeps a private clipboard".
- **A sanitizer target with an incomplete prerequisite list produces confident, wrong results.** `bin/dbg_asan`
  once kept running a stale binary and reported a 312-byte leak the current source did not have. When a sanitizer
  finding contradicts the tests, check the binary's mtime against the sources first.
- **A `(void)x;` line does not mean `x` is unused.** `(void)def1;` in `csi_dispatch` read like a leftover, but
  `def1` is used by `CSI J`, `CSI K` and `CSI 6 n`. Grep for uses, then let the compiler confirm.
- **Never pass an out-parameter and the call that fills it as arguments of the same call.**
  `fm_nav(fm_go_sel(&g_fm, &seq), seq, "open")` compiles clean under the whole `WARN` set and is broken: C
  evaluates arguments in an unspecified order and gcc reads the caller's `seq = 0` BEFORE `fm_go_sel()` writes it.
  Measured live (s74): every listing issued after a navigation carried request id 0, so `fm_install_scan()`
  discarded all of them and the browser kept showing the directory the user had left. Fill the out-param in its
  own statement. Reproduce with `.scratch/s74/argorder.c` (three lines).
- **One int return value cannot mean "not handled" and "handled, do not repeat" at the same time.**
  `send_key()`'s return says only "is this repeatable", and falling off the function is what sends the key to the
  PTY. `if (fm_keys(...)) return 0;` therefore delivered every one-shot file-manager key to the shell too: Return
  on a file typed its path AND a newline (the shell would have run it), BackSpace went to the parent AND deleted a
  character at the prompt. Three states (-1 / 0 / 1) fixed it. Symptom to look for: the action happens AND its key
  appears in the terminal.
- **A refused texture upload is a decision, not a log line.** Every `glTexImage2D` site used to print its error
  and draw with the texture anyway; on the NVIDIA driver a draw that samples a texture with no storage faults
  inside `libnvidia-eglcore` (SIGSEGV at 0x4 in `draw_quads`, 85 core dumps in one suite run). The rule now: the
  glyph atlas is worth exiting over (nothing can be drawn without it — `render_draw()` returns -1 and `main()`
  exits 1), the color atlas is worth downgrading to mono glyphs, a picture is worth skipping. Retrying an upload
  is fine; drawing the result is not.
- **A steady-state condition is not evidence of an event.** `else if (prompt_row_matches(t))
  t->input_pending = 0;` read as "the shell put the prompt back, so the line is empty" — but it was also true for
  a burst that had changed NOTHING, e.g. fish repainting only OSC 7 / OSC 0 / DECSET after Kai-Term had re-typed
  the user's text. The echo window closed over bytes that had not been echoed yet, every later burst re-took the
  prompt snapshot (including the one carrying the echo of Kai-Term's own typing), and a row holding `Some text`
  began answering `term_at_prompt()` as EMPTY — so the next file-manager navigation appended `cd '<dir>'` to the
  command and the shell ran it (user report). Snapshot the condition at the START of the burst and act only on the
  transition. Same class as "a log line that prints its own argument can lie": a predicate that is true before and
  after an event cannot report that event.
- **A burst boundary is not a semantic boundary.** Advancing a "wait for the shell's next prompt" state at the end
  of any pty read looks reasonable and is wrong: fish answers a `cd` with the command echo, then a continuation
  row, then prompt chrome, then the prompt — four separate reads, and the model re-takes the prompt snapshot at
  the end of each. Where the evidence is "the application has FINISHED printing", wait for silence
  (`FMCD_QUIET_MS`, driven from the frame timer) — no further burst is coming to trigger it.
- **An invariant held by accident is invisible until something else changes.** "A cursor moved off the prompt
  column means the line is not empty" was true for months because EVERY key Kai-Term sent set `input_pending`,
  and that is what stopped `prompt_snapshot()` from re-taking the anchor at the moved cursor. Making movement keys
  stop opening the echo window (the s89 BackSpace fix) turned the pinned check red and would otherwise have made a
  cursor sitting INSIDE the prompt text answer "safe to write here". When a change flips a check that has been
  green for ages, find the mechanism that was really holding the property — `.scratch/s89/leftprobe{,_old}` prints
  `at/pending/cx/cy/prompt_cx/prompt_cy` per step and shows it in one run — and state it explicitly instead of
  restoring the accident.
- **A red check can mean the test's setup is unrepresentative, not that the model is wrong.**
  `tests/test_vt.c` looked like "the prompt snapshot breaks after a scroll" and would have invited a change to
  correct snapshot logic. `.scratch/s82/promptprobe.c` replayed the sequence and printed the state at each step:
  the test wrote the whole line with `feed()` (application output), and `term_parse()` re-takes the snapshot at
  the end of every such burst, so the typed text legitimately was `""`. The realistic sequence (`feed("p ")` then
  `type(...)`) passes at the same scroll position. Print the state before editing the logic a check exercises.
- **Never write a check around an escape sequence without measuring what THIS parser does with it.** Assumed
  "Home"/"End", got three realities: `CSI H` moves the cursor to another ROW (not a Home key), `ESC O H` prints a
  literal `H` (SS3 is not implemented here), `ESC [ 1 ~` is ignored, and `CSI G` is the one that moves within the
  row. Same class of mistake in the probe itself: `"\\e[H"` in C sends a backslash and an `e`, which read for two
  minutes like a parser bug.
- **Never hand-count a length into `term_parse()` — use `strlen()`.** `.scratch/s97/csiC0.c` wrote
  `term_parse(&T, "\e[?2004", 8)`: that literal is **7** bytes, so the hand-counted 8 fed the NUL terminator as an
  extra byte. The probe then reported a perfectly good case as broken — a CSI sequence split across two pty reads
  came back as "mode not applied, an `h` painted". `strlen()` showed that case was never broken. The check
  invented the bug; the A/B against the real code is what caught it.
- **A harness that counts bytes by hand sends whatever follows the literal in memory.** `input_write(t, p,
  "PS1='%~> '\n", 13)` on an 11-byte literal also wrote the NUL and the next byte of `.rodata` — which happened to
  be the `S` of the following string literal, so the zsh prompt appeared to hold `SSome text` and the terminal
  looked like it was duplicating a keystroke. It was the probe. Always `strlen()` (or `sizeof(x)-1`) for a
  literal, and when a harness reports a character nobody typed, suspect the harness first (s94).
- **A harness that types into the user's real shell history pollutes its own later runs.** The s95 path gate ran
  real fish/bash/zsh in a pty, so its commands landed in `~/.config/fish/fish_history` and `~/.bash_history`. Two
  separate false greens followed: a later run's autosuggestion ghost was a command an EARLIER run had typed, and
  in ghost mode Enter printed the expected output because fish's own suggestion completed the command. Run shells
  with their history redirected (`fish --private`, `HISTFILE=/tmp/...`) and make the needle something no history
  can already contain (a pid in the file name). When a check passes on the code you expect it to fail, suspect the
  check before you believe the result.
- **A probe that hosts a shell must answer that shell's queries, or it is measuring a shell that never started.**
  A section of `.scratch/s97/hprobe2` reported "fish: bracketed paste NEVER within 3 s". `.scratch/s97/fishdiag`
  shows why that was the probe: fish's first 83 bytes are terminal QUERIES (`CSI ? u`, `CSI > 0 q`, `OSC 11 ; ?`,
  `DCS + q …`, `CSI 0 c`) and fish paints nothing until they are answered or time out. `.scratch/s97/fishtime`
  drains the response queue the way `src/main.c` does and measures 200 ms. Any headless measurement of shell
  STARTUP behaviour that never writes back to the pty is measuring a stalled shell, not a slow one.
- **When a bug is in what someone ELSE hands you, measure that someone before theorising about your own code.**
  The stray-`h` report got two confident wrong explanations (a leaked parser byte, then an escape sequence in the
  pasted text) before one command pair settled it: `wl-paste -l` and `wl-paste --type <name> | od -c`. Both wrong
  theories were internally consistent and neither had been tested against the actual clipboard. The user's own
  clues — "the source matters", "ctrl+v works but ctrl+shift+v does not" — pointed straight at it; a report that
  distinguishes two paths is telling you which path to instrument. Related: the s97 C0-inside-CSI fix was a real
  bug found while chasing this one, so "I found and fixed something" is not evidence that the reported bug is
  explained. Confirm the reported symptom is reproduced, then confirm it is gone.
- **A feature A/B cannot pin a negative check, and the failure count is only meaningful if you say which
   checks it counts.** Stubbing `--import-theme` out (nothing recognised, nothing parsed, nothing written) made
   44 of the 1127 checks fail — but 17 of the import group's 60 passed, because "a missing file is refused", "a
   file with no colour is refused" and "there is no destination without HOME" are all satisfied by a tree that
   refuses everything. That is not a defect in those checks: they are pinned by MUTATION (make the refusal
   accept the file) and by the positive checks beside them, not by an absence A/B. Report both numbers and name
   the kind, or the next session reads "44 failures" as "all of them matter".
- **A silent truncation is a decision someone already made for you.** `snprintf(v->name, sizeof v->name, "%s",
   val)` is memory-safe and cut a 236-byte theme name into a 128-byte field without a word — and the compiler
   only said so in the asan build, where inlining let it bound `val`. Writing the cap as one function that
   copies `sizeof field - 1` and logs the cut removes the warning AND makes the behaviour something the tests
   can assert on (the CLI group greps the log line; the C check asserts the exact 127-byte stem, with an
   alternating `ab` name so an off-by-one changes the last character too). Same shape as every other "the value
   was quietly changed" bug here: make the change visible at the place it happens.
- **A harness that mallocs must free, or LeakSanitizer reports YOUR test as the leak.** The option-pinning
   block read 9 names out of `usage()` and 9 out of README and never freed them: 134 bytes in 18 allocations
   that looked like a shipped-code leak and would have hidden a real one. The suite already had the pattern
   (`free((char *)set_names[i])`); the new block just did not copy it.
- **A failed build leaves the previous binary in place, and it will happily pass.** `make -j8` errored on a C
  mistake, the very next command ran `./bin/test_vt` and it printed `859 checks, 0 failures` — for code that had
  never been rebuilt. Check the build exit status (and that the check count actually moved) before believing any
  green run.
- **A test that maps a window is a change to the user's desktop.** `tests/cli_test.sh` ran the real binary 15
  times per `make test`, and one session looped it 25 more times to sample a failure — that is what filled the
  user's screen and `coredumpctl`. One probe launch decides whether the window checks run; the rest are skipped
  with the reason printed, and `KAI_TERM_TEST_NO_WINDOW=1` skips them all. Never loop launches to count failures:
  diagnose the first one (and see ENVIRONMENT.md "Launching real windows in a test").
- **Never run the live key-injection or window-mapping harnesses while the user is at the machine, and never
  assume a red screen came from your own test.** The user was disturbed mid-session. Measured after the fact
  rather than guessed: no `bin/kai-term` of mine was alive, no `.scratch/inject` process, no new core dump, and
  the CLI suite had mapped 0 windows — what actually happened was `noctalia` (the lock screen) SIGSEGVing and
  niri restarting the session, a recurring host fault. Default to headless coverage (`.scratch/s86/follow2.c`
  drives the real machine over the real pty with no window at all) and hand the in-window check to the user.
- **`pkill -f <name>` matches the shell that runs it.** `pkill -f clipowner` killed the whole command line, output
  and all. Use `pkill -x <name>`, or put the sequence in a script file.
- **A background job spawned with `&` makes this shell tool swallow the call's entire output** — even the `echo`s
  that follow it. Split "start the daemon" and "query it" into separate calls, or run the whole scenario from a
  script file.
- **`cat > file` is fully buffered when stdout is a file**, so killing the `cat` that captures a paste discards
  the capture and the run reports 0 bytes. Close its stdin (Ctrl+D) and let it exit normally.
- **A missing-protocol grep proves the code is absent, not that the user is missing anything.** The s102 survey
  grepped every candidate feature first (all 0 hits) and two of the proposals were still wrong, because the user's
  own setup did not ask for them (scaling at 1, and OSC 52 vs the local clipboard they already use). Check what
  their setup actually asks for before ranking a feature.
