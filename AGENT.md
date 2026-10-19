# Kai-Term — Agent Journal

Read this + ISSUES.md before touching project. User-facing docs: README.md. Current problems: ISSUES.md
"Open" (empty). Root causes of long-fixed bugs: git history, not this repo —
`git show d5afe4a:ISSUES.md`. Host/tool facts: `/home/kaida/Project/ENVIRONMENT.md` (outside repo on purpose
— not project documentation).

> Renamed 2026-10-16 (session 59): `K-Term`/`k-term` -> `Kai-Term`/`kai-term` (dir, header
> `include/kai-term/kai-term.h`, config `~/.config/kai-term/kai-term.conf`, app id `Kai-Term`). Old journal
> entries and git history keep old spelling — they record commands really run and log lines really
> printed. Pre-refactor journal: `git show 1ab9dc8:AGENT.md`.

## Project Overview

Wayland terminal emulator in C. xdg-shell window + PTY shell + VT/ANSI parser + EGL/OpenGL text
rendering, 2 glyph atlases (alpha8 mono, RGBA8 color emoji) plus one GL texture per application picture
(kitty graphics protocol), two vertex batches per frame with the pictures drawn between them. In-window
multiplexing: N sessions per window, tab bar, side-by-side panes. No GUI framework.

## Current Status (re-measured 2026-10-22, s101 — build and every suite green. One small open bug (dead
`cursor_color =`, filed in ISSUES.md); s102 is planned in "Current Task", not started)

- **Build:** `make clean && make -j8` exit 0, **0 warning lines** under `WARN` (`-Wall -Wextra -Wshadow
  -Wwrite-strings -Wformat=2 -Wdiscarded-qualifiers`).
- **Suites:** `KAI_TERM_TEST_NO_WINDOW=1 make test` exit 0 — `CLI: 67 checks, 0 failures, 13 skipped`,
  `./bin/test_vt` **958 checks, 0 failures**, `./bin/dbg` **MODEL OK (0 problems)**, `./bin/test_fm` **293 checks,
  0 failures**. `make asan` exit 0, same three counts, 0 sanitizer reports. With a display and spare VRAM,
  `tests/cli_test.sh` reports **117 checks / 0 failures / 0 skipped**. What its windowed group alone covers is
  `main()`'s own argv handling — `-c/--config`, `-b/--binds`, combined flags, `-d` beating `log =`, `-w`, `-e` — and
  that a real launch applies the multiplexing / display / file-manager keys and the band's share of the window. The
  config SEARCH order and the split-by-name rules are NOT exclusive to it: `tests/test_vt.c` covers those headless
  (13 `cfgsearch` checks + the `config split` group). A no-window "0 failures" therefore leaves only the argv half
  uncovered (last full windowed run measured in s99; s100 and s101 skipped it — the user at the machine). **Re-diff
  these numbers, do not copy them.**
- **One open bug, small and filed (ISSUES.md "Open"): `cursor_color =` is dead config.** Parsed, stored and
  parse-tested, never read — `src/render.c` uses the `CURSOR_COL` macro and `render_init()` takes only `bg`/`fg`. Same
  class as the border bug below and fixed by the same move (pass the configured value to the renderer); it is folded
  into s102 step 1, where the palette becomes runtime state anyway.
- **No open question from s101.** The border fix is **user-confirmed in their own window** ("It's fixed, you can
  push", 2026-10-22): one runtime value (`term_set_default_colors()` / `term_default_fg()` / `term_default_bg()` in
  `src/vt.c`) now owns the default ink and paper, so the `background =` line covers the cells as well as the
  `padding` margin glClear paints. A/B'd both ways (7 of the 14 new checks fail on the reported state, 2 fail on a
  sentinel-divergent variant, and the end-to-end probe prints match=1 where the old code prints match=0).
- **No open question.** The four items this journal and ISSUES.md were holding for the user's eye were answered
  2026-10-22 ("1. yes / 2. good / 3. works good" — the s100 era entry): the two-file config split is theirs, the
  background change their own config file now produces is accepted, and the s98 Firefox paste fix, the preview-pane
  key scrolling and the 75 % band's look all work.
- **Implemented since s88, each green, A/B'd where a bug was claimed, and user-confirmed in their own window:**
  s95 the `p` path key / `v` preview toggle / `Return` doing nothing on a file; s96 DECCKM + the wheel and a click
  inside full-screen programs + the 64/65 wheel codes + preview-pane scrolling; s97 one shared `exec_c0()` (a C0
  byte inside a CSI is executed and the sequence keeps collecting); s98 `clipboard_mime_rank()` + `paste_repair()`
  (a NUL byte never reaches the pty); s99 the config split (`kai-term.conf` / `kai-term.bind`, `-b/--binds`, the
  three-directory search) + the `#`-comment fix that had been eating every `#RRGGBB` colour + the template
  name-coverage check.
- **Docs are pinned to the code by check, not by eye:** `tests/test_vt.c` reads the 29 setting names and the 24
  binding names out of the two strcmp ladders in `src/config.c` and compares them with `config/kai-term.conf` +
  `config/kai-term.bind`; README's option table matches `usage()` by set difference (7 options). A name added to
  the parser without a template line, a template line the parser does not know, and a deleted template line each
  fail a check. Do not hand-copy a name list into a test — derive it from the source.
- **No probe or experiment left in the tree:** `grep -rnE "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/ include/
  tests/` -> empty; no `TODO`/`FIXME`/`XXX`.
- **No `install` target**, and nothing copies `config/*` to `/etc/kai-term` — the Makefile has never had one and
  README tells the user to copy the templates themselves. Deliberate; adding one is the user's call.
- **Git:** `origin = https://github.com/K-code-AI/Kai-Term`, branch `main`, s90-s100 pushed. Read the remote state
  with `git status -sb` / `git log --oneline origin/main -1`, never from a hash written here. `git fetch` before
  push — the user commits on GitHub too. Never commit `bin/` or `build/`.
- **The 4 reports that once lived in ISSUES.md "Open"** were unproven and were dropped 2026-10-17 (no fix was ever
  verified, so they are not in "Fixed" either). Recoverable with `git show d5afe4a:ISSUES.md`; if one recurs, file
  a new Open item with a reproduction.

## Current Task (s102 — PLANNED, NOT STARTED. Seven features the user agreed to on 2026-10-22, in the order below.
Nothing has been written. The facts in each block were measured against this tree; the open questions at the end are
the user's and must be answered before step 1 is started.)

**Dropped on the user's word — do not re-propose these:** scrollback search ("not really usefull imo");
HiDPI / fractional scaling (their `~/.config/niri/config.kdl` sets no `output { scale }`, so the output is at scale 1
and `wl_surface_set_buffer_scale(W.surface, 1)` at `src/main.c:2809` is exactly right — nothing is stretched or
blurry for them, and `zwp_fractional_scale`/`wp_viewporter` are 0 hits and stay 0 hits); OSC 52 (they paste inside
tmux, ssh and ssh>termux already — that is the *local* clipboard, typed into the pty, and has always worked; OSC 52
is the remote→local **copy-out** direction, which they don't need); IME (`zwp_text_input_v1`, 0 hits, they don't type
CJK).

### 1. Themes: the palette becomes runtime state, then a theme file, then `--import-theme FILE`

**Measured starting point.** `palette[256]` is a **`Term` field** (`include/kai-term/kai-term.h:271`) filled inside
`term_init()` from a `static const base16[16]` (`src/vt.c:830-847`, then the 6×6×6 cube and the 24-step grey ramp,
computed). `term_init()` memsets the whole `Term` — that is the exact shape of the s101 bug, so the palette must
become file-scope `g_palette[256]` + `term_set_palette()` / `term_palette()`, the same pattern as
`g_def_fg`/`g_def_bg` (`src/vt.c:168-177`). Only entries **0-15** are themeable; the cube (16-231) and the ramp
(232-255) stay computed — they are identical in every terminal and no theme in the wild touches them.
`SEL_BG` (`kai-term.h:32`) and `CURSOR_COL` (`kai-term.h:31`) are compile-time macros read at `src/render.c:1082,
1091, 1093, 1094, 1135, 1288`; a theme has to replace those too. `osc_end()` (`src/vt.c:3125-3163`) handles only
OSC 0/1/2, so today **no program can change any colour at all**.

**Bug to fix in the same pass (filed in ISSUES.md "Open"): `cursor_color =` is dead config.** It is defaulted
(`src/config.c:44`), parsed (`src/config.c:463-464`), stored (`kai-term.h:652`) and **never read**: `src/render.c`
uses the `CURSOR_COL` macro and `render_init()` takes only `bg`/`fg` (`src/render.c:256`, `src/main.c:2818`). Every
test that mentions it tests *parsing* (`tests/test_vt.c:2149/2154/2196`), and the template parity check compares
parse results, so it stays green while the key does nothing. Fix = pass it to `Render` and use it at those six
sites.

**Native format = Kai-Term's own `key = value`, in a third file.** `config_find()` already searches per **file name**
across `$XDG_CONFIG_HOME/kai-term`, `~/.config/kai-term`, `/etc/kai-term` (`src/config.c:585` comment), so a theme is
nearly free. `theme = NAME` resolves to `NAME.theme` in `<each of those dirs>/kai-term/themes/`; `theme = /path` or a
relative path opens directly. Precedence: **theme file, then `kai-term.conf`, then `kai-term.bind`**, so an explicit
`background =` line still beats a theme, and the log names which file set the final value (the readers already log
their sources).

**Key names: use the kitty spelling as primary, with Kai-Term's own spelling as an alias.** That is what makes the
kitty corpus drop-in — the user's "13th Floor" file below is already valid except for two keys with nowhere to go.
Accepted: `foreground`, `background`, `cursor` (alias `cursor_color`), `selection_background` (alias
`selection_color`), `color0`…`color15`. `parse_color()` already takes `#RRGGBB`, `0xRRGGBB`, bare `RRGGBB` and
`AARRGGBB`, so no new colour code.

**Deliberately NOT in `apply_setting()`:** the `colorN` names. The template-coverage check derives the accepted
settings names from that strcmp ladder (`cfg_names_of("src/config.c", "static void apply_setting(", …)`,
`tests/test_vt.c:2270`) and demands a commented template line for every name — 16 more lines in `kai-term.conf` for a
knob nobody tweaks by hand. Put them in the theme parser only, and give that parser its own derived name list, same
technique.

**`kai-term --import-theme FILE`.** The importer lives in a testable function (new `src/theme.c`, or
`src/config.c`) — **not** in `main()`; `main()` is in no linked test, so the flag would be the only coverage
(same reasoning as "Geometry lives in `src/layout.c`, not main.c"). It sniffs the format from the first bytes, maps
the names, writes `~/.config/kai-term/themes/<name>.theme`, refuses to overwrite unless told, and **logs every key it
dropped**. Sniff: `<plist`/`Red Component` → iTerm2; leading `{` → JSON (Windows Terminal / VS Code);
`[colors.primary]` → alacritty; `[Color Scheme]`/`ColorBackground=` → Konsole/XFCE4; `colorN:` or `XTerm*`/`*.` →
Xresources; otherwise `key value` → kitty. Nothing matched → refuse and list what was looked for; never guess.
`<name>` comes from a `name =` key when present, else the file's basename.

**Acceptance case — the user's own theme, verbatim (kitty spelling, "13th Floor"):**

    color0 #bdaaad   color1 #e07f70   color2 #9eae80   color3 #d0af63
    color4 #849caf   color5 #b78ea6   color6 #7bb2aa   color7 #f2e2c8
    color8 #c5b4b7   color9 #e48e81   color10 #aab88f  color11 #d6b976
    color12 #93a8b9  color13 #c09cb1  color14 #8bbbb4  color15 #f4e5cf
    background #4a0710
    selection_foreground #4a0710
    cursor #e48e81
    cursor_text_color #4a0710
    foreground #f2e2c8
    selection_background #f2e2c8

(That is their file reflowed onto fewer lines for this document; the importer must accept it as one key per line.)
Expected: 16 palette entries + `background` + `foreground` + `cursor` → `cursor_color` + `selection_background` →
`selection_color` applied; `selection_foreground` and `cursor_text_color` reported as dropped (Kai-Term has no
per-cell "text on this colour" concept — the glyph colour there is the cell's own fg).

**Optional, cheap once the palette is runtime state:** OSC 4 (set an entry), OSC 10/11 (set/query fg/bg), OSC 104
(reset). That is what lets `pywal`-style tools theme a live Kai-Term with no file at all. Ask the user before
building it — it is a security-relevant capability (any program in the terminal can repaint the window).

### 2. Cursor: a thin bar, classic blink

The user's words: "A thin rectangle, the most classic blinking cursor". Today the cursor **is** the cell's background
quad, emitted in pass 1 (`src/render.c:1143-1152`), and pass 2/3 re-tint the glyph on the cursor cell
(`src/render.c:1286-1292`); `cur_off = mix_c(CURSOR_COL, r->bg, 55)` is the unfocused-session colour.

- A bar/underline must be a **separate quad**, not the cell background. Emit it in **batch B between the selection
  bands and the glyphs** (batch B is one draw call where the later quad simply wins, so glyphs must come after it or
  the bar hides the character). Keep the existing rule that the cursor is hidden exactly where a band is painted —
  the comment at `src/render.c:1150-1152` says pass 1 and pass 2 must match, and a bar that forgets it re-creates
  that bug.
- New keys: `cursor_style = block|bar|underline` (default `bar`, since that is what they asked for — confirm),
  `cursor_width = 2` px for the bar (clamped 1..cell_w/3), `cursor_blink = 500` ms half-period, `0` = steady.
- Blink needs a **fourth timerfd** (there are three: frame `tfd` `src/main.c:3053`, key repeat `krfd`
  `src/main.c:1966-2002`, paste watchdog `pwfd` `src/main.c:2488-2497`). A tick must set `W.dirty` and go through
  `redraw()` on the normal path — never swap inside a Wayland listener, and never arm a `wl_surface_frame` callback
  without attaching a buffer (the frame-pacing rules in "Architecture & Invariants"). Blink phase is window state
  (`W`), **not** `Term` state: `term_init()` memsets `Term`, and DEC 25l (`show_cursor = 0`) must still hide it.
  Classic behaviour blinks only while the window has focus — worth doing, it also stops a 2 Hz redraw on an idle
  unfocused window.
- DECSCUSR (`CSI <n> SP q`, the *application* asks for block/bar/underline) is 0 hits today. Optional; the query
  plumbing already exists (`CSI c`, `CSI 6 n`). Ask the user — vim/neovim/fish set it, and honouring it means an app
  can override their chosen style.

### 3. Double-click selects a word, triple-click a line (on by default)

The user's rule, verbatim: "a word is everything no separated by a space, but we don't take the trailing ',' or '.'".
So: maximal run of non-space cells around the clicked cell, then strip trailing `,` and `.`.

- The double-click detector already exists for the file manager and should be copied, not reinvented:
  `#define FM_DCLICK_MS 500` (`src/main.c:233`) and the same-row/same-entry test at `src/main.c:1596-1598`, using the
  `time` argument `ptr_button()` already receives (`src/main.c:2177`).
- Selection endpoints are set through `sel_set_endpoints()` (`src/main.c:2064`) with fractional 0..1000 offsets; a
  word/line selection uses whole cells (0 and 1000). `SelSpan` (`kai-term.h:512-515`) and `sel_band_pixels()` need no
  change, and `term_selection_text()` already trims trailing spaces, so a line selection that reaches the right edge
  copies cleanly.
- **The interaction that must be handled:** on release of a click that never moved, `src/main.c:2259-2274` synthesises
  cursor-key presses for a program that owns its screen (`click_moves_cursor` + `term_wants_key_mouse`). A double
  click inside nano would walk its cursor twice. The second press of a double/triple click must mark the gesture as a
  word/line selection and suppress that synthesis on release. `Shift` already means "selection, not click".
- New key: `click_selects_word = 1` (they asked for on-by-default). Triple-click = the whole logical row; decide
  whether it includes the trailing blanks (they copy as nothing anyway) and whether it extends to the next **physical**
  row when the line soft-wrapped (`Grid.wrap` knows).
- Needs a headless check: a word spanning a wide glyph (CJK/emoji occupies 2 cells, `ATTR_WIDE` continuation) must not
  be cut in half — the same wide-pair rule reflow uses.

### 4. Ask before quitting when something is still running

Today `toplevel_close()` (`src/main.c:1758-1762`) sets `W.running = 0` immediately, closing the last tab does the same
(`src/main.c:759-762`), and both close the pty master and `kill(child, SIGHUP)` (`src/main.c:753/755`, `3330-3340`).
So whatever was running dies with no warning.

- How to know something is running: `tcgetpgrp(mfd)` on the pty **master** fd returns the pty's foreground process
  group. Kai-Term never calls `tcsetpgrp` (measured: 0 hits in `src/`), so the shell owns that value and it is
  trustworthy: idle at the prompt ⇒ `tcgetpgrp(mfd) == tb->pty.child`; a command running ⇒ it does not. **Verify this
  with a probe before writing the gate** (`.scratch/s102/fgprobe.c`: spawn a shell on a pty, read `tcgetpgrp` idle,
  then with `sleep 30` running) — it is one syscall and it is the whole feature's correctness.
- New key: `confirm_quit = running|always|never`, default `running`. Same gate on **closing a tab**
  (`key_close_tab`, and the bar's middle-click close at `src/main.c:2192`), which is where work is lost more often.
- The prompt itself is chrome, drawn in the grid with the same font/colours as the tab bar and the band — one row,
  modal, keyboard-driven (Enter/`y` confirms, Esc/`n` cancels), and it must swallow keys without fighting the
  file-manager keyboard handover (`FM_FOCUS_*`) or the key-repeat timer. Log the decision either way.
- Multi-session case: if two sessions have something running, the message must say how many and which, and confirm
  must apply to all of them, not just the visible one.

### 5. Middle-click paste (opt-in, off by default)

The user: "I actually hate middle click paste, but we could add it as an option (off by default)". `wl_primary*` is
0 hits — on Wayland the **client** owns the primary selection, niri will not do it. So this needs a second
`wl_data_device` selection: offer the selection as primary on `WL_POINTER_BUTTON` release when the option is on, and
paste from the primary offer on middle press. Ordering in `ptr_button()` matters: the bar's middle-click-closes-a-tab
(`src/main.c:2192`), the file-manager band, and an application that grabbed the mouse (`src/main.c:2232`) all come
first and must keep winning. New key: `middle_click_paste = 0`.

### 6. Visual bell

`BEL` is swallowed today: `case 0x07: break;   /* BEL: nothing to do here */` in `exec_c0()`
(`src/vt.c:3179`). Programs ring it on real failures — readline's ambiguous completion and empty Enter, `sudo` on a
wrong password, `less`/`man` on a failed search, vim on an error.

Cheapest honest implementation, no new dependency and no GL blending: `exec_c0` sets a per-Term "bell rang" flag;
`main.c` turns it into a deadline; `render_draw()` emits **one extra quad in batch A, after every cell background** —
so it covers the backgrounds but sits under batch B (bands, glyphs, labels, border), which means the text stays
readable while the field flashes. Colour = `mix_c(bg, fg, small)`. A margin flash is not an option: s101 made the
margin the same colour as the cells. New key: `bell = visual|off`. Ask whether it defaults on.

### 7. URLs: Ctrl+click opens

0 hits for OSC 8 and for any URL detection. **Do not store URIs in `Cell`** — `Cell` is allocated per column per row
and reflow moves cells around; a per-row list of `(col_start, col_end)` runs computed lazily is the cheap shape.
Recommended first version: at Ctrl+click time, take the logical line (reflow already knows the `Grid.wrap` marks),
scan it for `http://` / `https://`, take the run containing the clicked cell, stop at whitespace, strip trailing
punctuation. No storage, no reflow interaction, no per-frame cost. Opening needs a decision (below): `xdg-open` means
fork/exec from the terminal process.

### Open questions — the user's, answer before starting step 1

1. **Theme precedence**: theme file → `kai-term.conf` → `kai-term.bind` (my proposal: an explicit `background =` still
   beats a theme). Or should a theme win outright?
2. **OSC 4 / 10 / 11 / 104**: build them with the palette, or leave the palette file-only? Any program in the terminal
   could then repaint the window.
3. **Cursor**: default `cursor_style = bar` for everyone, or keep `block` as the default and let them set `bar`?
   Bar width fixed at 2 px, or scaled with the font so it survives a zoom? Blink on by default at 500 ms half-period?
   Honour DECSCUSR (apps may then override the style they chose)?
4. **Word selection**: strip only `,` and `.` as they said, or also `:;!?)]}"'` (a trailing `)` from `(foo)` and a
   trailing `:` from `path:` are common)? Leading punctuation kept? Triple-click on a soft-wrapped line — one logical
   line or one screen row?
5. **Middle-click**: paste the **primary** selection (Wayland-correct, but empty unless they select with the mouse)
   or the **clipboard** (what Ctrl+Shift+C copies — usually what people actually want)?
6. **Bell**: default on or off?
7. **URLs**: open with `xdg-open`, or a configurable `url_opener` command, or print the URL and let them copy it?
8. **`--import-theme`**: overwrite an existing theme of the same name, or refuse and require a new name?

### Rules for the whole session

One feature per commit, each: edit → AGENT.md → build → every suite → commit; never commit red. Every new config key
needs a line in the shipped template **and** a value check in the template-parity group, or the name-coverage check
fails (that check is what caught a deleted `padding = 8` line in s99). Re-measure and re-quote the check counts every
time — do not copy them from this file. Probe first, then write the check around what the probe printed (the s101
reflow check and the `tcgetpgrp` gate both need this). A/B every new check against the code that lacks the feature and
report the failure count. Do not map windows while the user is at the machine: `KAI_TERM_TEST_NO_WINDOW=1`, and hand
the visual verdicts (theme colours, cursor look, bell flash) to the user.

## Progress Journal (root causes of fixed bugs: git history)



- **2026-10-12/14, s1-12** — scaffold; first build. vt.c grid -> rotation permutation (`row[]` always valid);
  CSI params; event loop -> `prepare_read -> epoll_wait -> read_events`; EGL/FreeType init; render fixes
  (vertex stride, MVP mapping, 6-vertex quads, atlas upload format, first-frame bootstrap, clearing
  realloc-grown cols). First 2 user bugs: black window (font metrics in font units, not px); 10 s startup
  (unanswered DA1/CPR/DA2).
- **2026-10-14, s13-23** — HW instancing; alt screen; SGR mouse; scrollback keyboard nav; zoom; selection +
  Wayland clipboard; wheel scroll (seat v5); `Grid.live_off` anchor (fixed wheel direction + auto-follow).
  Tree -> project template; Makefile rewritten; `.gitignore` + stricter warnings; docs rewritten.
- **2026-10-14, s24-32** — selection precision: sub-cell endpoints; per-fragment highlight without GL
  blending; selection-vs-pointer causes: grid anchoring, `wl_fixed_to_int`. User confirmed.
- **2026-10-14, s33-38** — doc audit; fontconfig fallback faces with source face stored per glyph (so
  `atlas_grow` re-renders from right one); 3 back-to-front passes; COLOR emoji end to end (clusters in
  vt.c, ONE glyph id via HarfBuzz, FreeType color bitmap, RGBA8 atlas, un-tinted, wide emoji = 2 cells,
  regional-indicator flag pairs, U+FE0F/U+20E3 presentation).
- **2026-10-14, s39** — CSI 3 J so `clear` drops scrollback (`clear_scrollback` gate); shell start dir
  (`-w DIR` > config `working_dir` > `$HOME`, tilde expansion, `stat` check).
- **2026-10-15, s40** — bracketed paste (DEC 2004, `paste_wrap()` as tested reference, buffered, <=4096 B
  chunks, 8 MiB cap, 400 ms idle watchdog); latent `term_init` free-of-uninitialized-grid bug -> `GRID_VALID`.
- **2026-10-15, s41-43** — `app_id`; real window `opacity` (premultiplied source-over); selection endpoints
  snapped to whole cells; no band over never-written cells; **REFLOW** on width change (`Grid.wrap` soft-wrap
  marks, `grid_reflow()`, `grid_resize_plain()` for alt screen, cursor travels with its logical cell,
  narrowing evicts oldest rows).
- **2026-10-15, s44-45** — CLIENT-SIDE KEY REPEAT (`krfd` timerfd, `send_key` repeatability,
  `repeat_delay`/`repeat_rate`), user-confirmed; CLI cut back; `copy_on_release` (default off);
  fullscreen-freeze cause found (frame callback armed on an empty commit).
- **2026-10-15, s46-47** — draw/commit cycle rewritten -> 3 frame-pacing rules. Pre-existing segfault on
  heavy output in a grown window: `sb_limit() = min(scrollback_max, rcap - rows)` as single bound. Both
  user-confirmed.
- **2026-10-15, s48-49** — docs/state audit; "K-Term has its own clipboard" investigated against a foreign
  client, NOT reproduced (likely intentional `copy_on_release = 0`). No source change.
- **2026-10-15, s50** — GitHub; dead code removed; dangling `NextFeature #N` refs deleted; `WARN` tightened
  (tree clean under it); `.gitignore` replaced (had ignored neither `bin/` nor `build/`).
- **2026-10-15, s51** — CLI: config flag `-c` (was `-C`); retired-flag messaging deleted; one generic
  unknown-option message.
- **2026-10-15, s52-55** — `tests/cli_test.sh` + `make test-cli` (real binary — `main()` is in no linked
  test); zoom stale-prompt cause in `grid_resize_rows`; WINDOW PADDING (`padding` key + single
  `term_grid_geometry()` deciding cols/rows AND origin), which exposed `DEF_BG` clear-colour bug, fixed
  same session. Padding + zoom user-confirmed.
- **2026-10-15/16, s56-58** — MULTIPLEXING (`src/mux.c`: N `{Term, Pty}` per window, config-driven key
  bindings) + TAB BAR + SIDE-BY-SIDE VIEW (`src/layout.c`: pure geometry). s58 reported a fullscreen-freeze
  regression.
- **2026-10-16, s59** — RENAME `K-Term` -> `Kai-Term` (dir, header, binary, config path,
  `DEF_APP_ID`/`DEF_TITLE`, log prefix, `TERM_PROGRAM`, Makefile, tests pinning those literals) vs a
  pre-rename baseline. 4 items OUTSIDE repo listed for user (niri window rule,
  `/usr/local/bin/k-term`, `/usr/local/bin/desktop-terminal` + its .desktop entry, GitHub repo name): repo
  rename done, other 3 pending when last checked. Fullscreen freeze returned intermittently; user deferred
  (then ISSUES.md Open #4).
- **2026-10-16, s60-62** — s56/57 finished: main.c wired to `layout_apply()` + `render_init(..., conf.fg)` +
  `render_draw(&rend, &font, &g_layout, terms)` (this made binary link again); live key/pointer-injection
  verification; 3 defects fixed — bar/pane click logged a switch it never made (`tab_switch()` never called
  `mux_goto()`), `BTN_RIGHT`/`BTN_MIDDLE` swapped in SGR mapping, `tab_bar = always` asked compositor
  for `cell_w` instead of `cell_h` of extra height (startup grid 1 row short; arithmetic ->
  `layout_initial_size()` so a check pins it). Tests 436 -> 501; pushed `d05c817`.
- **2026-10-16, s63 — docs split.** AGENT.md had grown to ~2070 lines of per-session detail;
  `docs/ARCHITECTURE.md` mixed project design with host facts, which must not ship inside a project. Created
  `/home/kaida/Project/ENVIRONMENT.md` OUTSIDE the repo (host/toolchain/library/tool inventory + every debug
  technique that works here + traps; project-agnostic). `docs/ARCHITECTURE.md` deleted (`git rm`, path
  git-ignored so it cannot return); its invariants -> "Architecture & Invariants", its entry points ->
  "Debugging this project". AGENT.md 2067 -> 491 lines.
- **2026-10-17, s64 — user confirmation + issue clear-out.** Tab bar + side-by-side confirmed by eye ("it
  works as expected and looks great"). The 4 ISSUES.md "Open" items were never reproduced here, so they were
  CLEARED, not moved to "Fixed" — no fix was ever verified. Recoverable: `git show d5afe4a:ISSUES.md`.
- **2026-10-17, s65 — AGENT.md refactor.** 526 -> 469 lines, 45.8 -> 37.2 KB: prose walls -> bulleted
  invariants per module; project facts (deps, naming, layout, license, git) moved to "Project Files & Facts",
  leaving Key Decisions as bug traps + working rules; telegraphic wording. Fact-checked against the
  pre-refactor text: 0 project facts lost. **Line count is a poor proxy for context cost — bytes and words are
  what matter.**
- **2026-10-17, s66 + s66b — ISSUES.md gutted to current, verified info only.** 673 lines / 9059 words ->
  100 / 1149: the 54 historical "Fixed" root-cause entries and the 4 unproven reports went to git history;
  all 19 limitations kept and each re-checked against the source rather than trusted. Filing rule established:
  symptom + exact reproduction + what `./bin/kai-term -d` logs; not reproducible -> not filed.
- **2026-10-17, s67 — the emoji-strike fact was wrong in the DOCS, not in the code.** `.scratch/s67/facts.c`
  (FreeType, the API the code uses) and the app's own `-d` line agree: the installed NotoColorEmoji strike is
  136x128 px and `color_px == 128`, so font.c was right. The false "56px" came from reading CBLC's
  `bitmapSize` (a BYTE count) as a pixel size — that bad number had been written into ENVIRONMENT.md and the
  s66 entry, and both were corrected. font.c's comments now attribute the figures to the installed font and
  its magic `color_px = 128` fallback is `f->px`. The 6 dangling `ISSUES.md #N` citations in source comments
  were removed; no git SHAs in comments — they age, the mechanism is stated inline instead.
- **2026-10-17/18, s68-s70 — image support (the kitty graphics protocol), user-confirmed by eye.**
  Model/parser (`src/vt.c`): APC `ESC _ G` + DCS-discard parser states, control-block parser (`k=v` separated
  by `;` OR `,` — timg uses `,`, the spec says `:`), chunk assembly (`m=1`/`m=0`), base64 + zlib + PNG
  (libpng simplified API) + raw f=24/32, premultiplied pixels held in the model, images anchored to ABSOLUTE
  document rows (`Grid.row_base`), span math in vt.c (it is also what `a=T` moves the cursor past), byte
  budget + prune, `CSI 14 t`/`CSI 16 t` answers, and `ws_xpixel`/`ws_ypixel` in `TIOCSWINSZ`. Renderer
  (`src/render.c`): one GL texture per image keyed by `KImage.serial`, and the frame split into TWO batches
  (backgrounds, then pictures, then bands+glyphs+chrome) so text stays on top with no GL blending.
  Three defects, all found by writing the README honestly and then running the example: the "timg
  auto-detects" claim was false (its list is hardcoded to `TERM=xterm-kitty|xterm-ghostty`); Kai-Term's DA1
  answer advertised sixel (`;4`), so `timg foo.png` sent DCS that the parser discarded — the picture vanished,
  worse than block characters; and a re-transmission to an existing id stacked a second picture, so an
  animation left a trail. DA1 is now `ESC[?64;1;2;6;7;12;22c`, `CSI 5 n` is answered with `ESC[0n`, and a
  transmission to an existing id replaces that image. Tests 501 -> 553. Rule this produced: **a documented
  example is a claim — run it before writing it.**
- **2026-10-18/19, s71-s74 — the integrated file manager.** New `src/img.c` (every decoder in one place: PNG
  libpng, JPEG libjpeg, WebP libwebp, GIF decoded in-tree because giflib is absent here; premultiplied RGBA8;
  `img_sniff()` on magic bytes, never a file name) — the kitty path and the previews now share it. New
  `src/fm.c` (model only, no Wayland/EGL/threads: scan, navigation, previews, syntax highlighter, the two
  `Cell` painters, `fm_image_fit`), band geometry in `src/layout.c` (`FmGeom`, `layout_hit_fm`), band drawing
  in `src/render.c` (same batches, shared `emit_cell_glyph()`), wiring in `src/main.c` (worker thread + two
  newest-request slots + result pipe fd in epoll, key/mouse routing, focus handover, the `cd` follow), 20
  config keys, new `tests/test_fm.c`. Decoders were verified by pixel-diffing against ffmpeg (PNG/JPEG/WebP/
  GIF byte-exact), which found a libjpeg `output_scanline` off-by-one (heap corruption) and a wrong GIF
  interlace pass mapping. Three real product defects were found ONLY by the live key-injection harness,
  because `main.c` is in no test binary: `fm_keys()`'s two-state return let every one-shot band key also reach
  the shell (Return on a file typed its path AND a newline — the shell would have run it); an out-parameter
  passed next to the call that fills it made every post-navigation listing read `request 0` and get discarded;
  and `fm_send_cd()` fired on refresh, typing a command at the user's prompt. Tests -> 613 / 220 / 92;
  committed and pushed.
- **2026-10-19, s75-s79 — the file manager's four reported fixes, and a core pty bug.** Added `Left`/`Right`
  bindings, `..` as a real first row of every listing, double-click to open, and the empty-prompt gate: the
  model remembers the cursor row as the shell last printed it (`prompt_row`/`prompt_cx`/`prompt_cy`) and
  `term_at_prompt()` = `!input_pending && prompt_row_matches()`. `alt_screen_leave()` now RE-TAKES that
  snapshot instead of clearing it — clearing it made the follow dead for the rest of the session after every
  vim/less run. **`src/pty.c` was putting every child's terminal into raw mode** (`cfmakeraw()` on the master;
  a pty pair shares one termios), which broke Ctrl+C, Ctrl+D, echo and ONLCR for every program that does not
  set its own termios — measured A/B live, then fixed by deleting the block. Also fixed: the selection opening
  on the `..` row, and a navigation previewing a path built from the PREVIOUS listing.
- **2026-10-19, s81 — a refused texture upload is a decision, not a log line.** 85 SIGSEGV core dumps from
  one CLI-suite run on a GPU with no memory left: every `glTexImage2D` site printed its error and drew with a
  texture that had no texels, faulting inside `libnvidia-eglcore`. Now the glyph atlas is retried once after
  dropping every picture texture and then ends the run (`render_draw()` -> -1, exit 1); a refused color atlas
  falls back to mono glyphs with a ~1 Hz retry; a refused picture/preview texture skips that picture. Forced
  and verified by A/B in `.scratch/s81/`. `tests/cli_test.sh` now makes ONE probe launch and skips the window
  checks with the reason printed; `KAI_TERM_TEST_NO_WINDOW=1` skips them all — a test that maps a window
  changes the user's desktop.
- **2026-10-19, s82-s86 — the prompt follow: clear -> `cd` -> re-populate, idempotent, user-confirmed.**
  Requirement: the shell's input line must always sit in the browser's directory, keeping what the user had
  typed. Design: capture the line (`term_prompt_line()`), Ctrl+U, send the `cd` only once a burst is seen to
  CHANGE that row back into the remembered prompt, then re-type with no CR — `IDLE/ERASING/CDING/RETYPING`
  plus a 2 s deadline in `main.c`, so "runs a mangled command" is closed by construction. Two hazards only a
  real shell exposes: opening the band resizes the grid and that destroyed the snapshot of a DIRTY line (a
  height-only resize now moves the snapshot with its row instead of discarding it), and the end-of-burst rule
  closed the echo window on a STEADY state, so a second navigation appended `cd '<dir>'` to the user's
  command — the window now needs a transition, and CDING additionally waits for 150 ms of silence because a
  burst boundary is not a prompt. fish's autosuggestion ghost is followed via `term_prompt_line_typed()` /
  `term_prompt_line_is()`; the measured, accepted cost is a cursor parked mid-command: the whole-row proof
  fails, the sequence is dropped (a `cd` is never written in front of the surviving half) and the captured text
  goes to the log. Proven headless against real fish through the real pty (`.scratch/s86/follow2.c`) and by
  719 checks; the user confirmed it in their own window ("I confirm it works", 2026-10-19).
- **2026-10-19, s87 — AGENT.md cleanup (docs only, no source touched).** The s68-s86 "Current Task /
  Previous Task / entry N" narrative (~2700 lines: plans, measured-start blocks, per-step logs, expected
  outcomes) is replaced by the era entries above; the long s63-s67 entries were cut to the same level.
  3579 -> 838 lines, 45481 -> 11005 words, 295 KB -> 72 KB (75% smaller). Nothing that constrains a change
  was removed: Architecture & Invariants, Debugging this project, Key Decisions & Pitfalls, Project Files &
  Facts, Next Steps and the verified Current Status are kept, and the deleted narrative is in git history — the
  same rule ISSUES.md has followed since s66. Session labels (`s74`, `s81`, `s86`) still resolve to the era
  entries above; the per-entry citations that no longer exist were repointed.
- **2026-10-19/20, s88 — the band became a SHARE, and the preview's rows got one owner.** User request: the
  file manager should take ~75% of the window and be configurable, and "the metadata sometimes get written on
  the image preview" — with the explicit instruction "don't keep fm_rows we don't need it". So `fm_rows` is
  gone everywhere (key, fields, `FM_ROWS_MAX`, docs, checks) and `fm_size` (percent of the grid area, default
  75, clamped 10–95 at parse time AND in layout.c) decides the band: `band_h = grid_h * pct / 100` then
  clamped to the terminal's floor, and `layout_initial_size()` solves the inverse
  `grid_h = ceil((rows*ch + gap) * 100 / (100 - pct))` — the startup round trip is what proves that
  arithmetic, for the band on, off, with a bar, and with no padding or gap. The preview overlap was two
  hardcoded numbers, not a renderer problem: the header's size was a literal 3 in `paint_preview()` and
  `fm_image_fit()` independently, and the 8 metadata lines an image preview carries were counted in neither,
  so lines from row 3 down were painted into the picture's box (glyphs are emitted in the batch AFTER the
  picture). Now `preview_head_rows()` + `preview_meta_rows()` are the single owner, called by both, and the
  picture always keeps a row. Rule this produced: **a check that passes on the buggy code pins nothing** — the
  first version (a small centred 8x8 picture) passed against the old fm.c by luck; the case that actually
  overlapped is a picture whose box starts at the top of the picture area, and the A/B (`.scratch/s88/`, old
  `src/fm.c` from git against the new test file: 8 failures) is what found that. Tests 719 -> 726 and
  235 -> 247, all green, asan clean, committed and pushed.

- **2026-10-20, s89 — a BackSpace at an empty prompt had disabled the shell follow, and fixing it exposed an
  invariant that was only held by accident.** Measured (`.scratch/s89/bstoprobe`, real shells): bash and sh answer
  BackSpace at an empty prompt with a bell only, fish not at all — so no burst ever re-proved the line empty and
  `term_at_prompt()` stayed "not empty" until Enter. Fix: `payload_cannot_insert()` in `src/vt.c` — a payload that
  cannot put a character on the line (cursor movement, deletion, Ctrl+L, a bare Escape) does not open the echo
  window **when the line is already provably the bare prompt**; anything that can insert still opens it. That
  turned a long-green check red, and the property had been held by accident: every key Kai-Term sent set
  `input_pending`, which is what stopped `prompt_snapshot()` re-taking the anchor at the moved cursor. Now
  `prompt_snapshot()` returns untouched when `prompt_row_same_cells()` says the row is cell-for-cell the remembered
  prompt, so `prompt_cx`/`prompt_cy` mean "where the cursor sits when the line is EMPTY" — stated in code and
  pinned by `CSI G` checks (726 -> 746). Do not write a prompt check leaning on `CSI H` (it moves the cursor to
  another ROW) or on SS3 (`ESC O H` prints a literal `H` in this parser).

- **2026-10-20, s90-s92 — shell integration: the file manager asks the SHELL to `cd` instead of Kai-Term typing
  it.** Requested by the user ("can we use shell integration to change directories without disturbing the user's
  pending input … Fish: use its command-line editing functions to save and restore the current buffer. Bash: use
  Readline integration … Zsh: use ZLE widgets … with a fallback for unsupported shells"). There is exactly one
  channel to the shell, so the control carries no data: the directory travels in a per-session file and the
  sequence is ONE byte pair for every shell, `C-x C-]` (0x18 0x1d), chosen from a measured table of what each
  candidate inserts when unbound (`ESC [ 2 6 ~` -> `~` in bash; `C-x q` -> `q` in fish; `ESC [ 9 0 0 1 u` -> `?` /
  `001u`) and of the tty's own specials — `C-\` is inert and unbound in both shells but `stty -a` reports
  `quit = ^\`, so it would SIGQUIT whatever owns the terminal. Announce `ESC _ K ; kai-term ; integration ; <kind>
  ST`, confirm `ESC _ K ; kai-term ; cd ; <dir> ST`, 400 ms then fall back to the typed machine. APC kind `K` is
  one more branch in `apc_end()` — no new parser state. s92 fixed the two defects the user then reported from
  their own window: a shell they started at the prompt was not integrated at all (the scripts now wrap a bare
  `fish`/`bash`/`zsh`, never over a name the user defined, never when any argument is present, and re-announce
  after every command — `Term.shell_int_gen` + `Tab.int_gen` make an announcement an EVENT that revives a dead
  integration, because the kind alone cannot: fish -> nested fish -> fish announces three times with one kind),
  and the prompt kept naming the OLD directory (fish `commandline -f repaint`, zsh `zle reset-prompt`; **bash
  cannot** — readline expands the prompt once per `readline()` call, `C-l` leaves it stale and printing `${PS1@P}`
  over the line garbles it, both measured, so bash is documented and nothing is pretended). Measured acks 52-76 ms
  over real fish/bash/zsh; the user confirmed all three in their window.

- **2026-10-20, s93 — the cleanliness pass the user asked for** ("We need to check to make sure we have a clean
  codebase"). A stricter second-compiler pass (clang 22, `WARN` plus `-Wcast-qual -Wmissing-prototypes
  -Wstrict-prototypes -Wold-style-definition -Wundef -Wunused-macros -Wextra-semi`) found exactly three things in
  OUR code: `img_decode_gif` needed `static`, `klog` needed `__attribute__((format(printf, 2, 3)))` — verified in
  both directions, a wrong `%` now fails to compile and compiled silently before — and one `-Wcast-qual` at zlib's
  non-const `next_in`, which is correct as written and is the ONLY such hit in our tree, which is why
  `-Wcast-qual` stays out of `WARN`. Everything else that pass reports is in the wayland-scanner-generated header.
  Also found: the whole s90-s92 work had never been committed (`src/shellint.c` was untracked).

- **2026-10-20, s94 — zsh became verifiable because the user installed it.** Their verdict ("I verified the shell
  integration, Fish Bash and Zsh are working as expected now. Perfect") plus the first headless zsh run: announce,
  ack 54 ms, line byte-identical, prompt repainted by `zle reset-prompt`. What is STILL unmeasured in zsh: what
  `C-x C-]` does in a zsh that has no binding — the inertness that protects an unwrapped nested shell was measured
  in fish and bash only — and no zsh check lives in `make test` beyond the script-on-disk pins. Grep found seven
  stale "no zsh on this box" claims, four of them inside source comments and check MESSAGES: a check message that
  misstates the evidence is worse than no message, because the next agent trusts it.

- **2026-10-20, s95 — the user changed a design decision, not a bug** ("it is not a bug, it was a design decision,
  But we are changing it"). Now `p` types the selected entry's quoted path at the prompt, `v` toggles the preview,
  and `Return`/double-click on a file do nothing. Measured BEFORE changing anything: the strict gate refused fish's
  autosuggestion ghost and a mid-line cursor, but `cat ` already typed already worked in bash and zsh — the user's
  login shell is fish, which is why it looked universal. The gate is now the non-strict
  `term_prompt_line_typed()`; the `cd` follow machine is provably untouched (`.scratch/s90b/diffgate.sh`: seven
  copied functions byte-identical to `main.c`, all six integration modes still `MATCH` with the typed text intact).
  18-run matrix over real fish/bash/zsh including a file literally named `odd name'"<pid>.txt`, whose quoting is
  correct end to end. Build the expected text with the program's own `shell_quote()`, never by hand.

- **2026-10-21, s96 — the mouse inside full-screen programs.** Asked for by the user ("When opening a file with
  nano or similar program it would be nice to be able to scroll with the mouse. Also scrolling in the preview pane
  of the file manager …"), then framed by their comparison with kitty ("In kitty I can't click, but I can scroll").
  Measured first (`.scratch/s96/nanoprobe`, `.scratch/s96/clickprobe`): nano and less are alt-screen with
  `mouse_mode == 0`, so Kai-Term's wheel landed in `scrollback_nav()` on a grid built with `scrollback_max = 0` and
  was thrown away; **less answers ONLY the SS3 arrow form** because it asked for DECCKM with `ESC[?1h`, so its
  arrow keys were inert; on `nano --mouse` the wheel report Kai-Term sent (4/5) does nothing while 65 scrolls (same
  in X10: `32+65` scrolls, `32+4` does not), so the wheel was dead in every ncurses program; and a mouse report
  sent to a program that never asked for the mouse is **typed into its document** — one `ESC[<64;20;5M` put
  `64;20;5M` on the user's row 1 and set `Modified`. Shipped: DECCKM (`Term.app_cursor`, saved/restored across the
  alternate screen, every cursor key through one `term_arrow_seq()`), the wheel as cursor keys, a click as the
  arrow-key walk to the clicked cell with the target column clamped to that row's last glyph + 1, 64/65 for
  programs that DID grab the mouse, and preview-pane scrolling. All of it in `src/vt.c`/`src/fm.c`, never in
  `main.c`, because main.c is in no test binary. 31 + 27 new checks; the resize clamp A/B'd.

- **2026-10-21, s97 — the stray `h`, first half: a C0 byte inside a CSI sequence ENDED it.** Measured
  (`.scratch/s97/hprobe2` against `git show HEAD:src/vt.c`): `ESC [ ? 2004 <TAB> h` left `bracketed_paste` at 0 and
  painted an `h`; `ESC [ ? 1049 <TAB> h` never entered the alternate screen; `ESC [ ? 1 <TAB> h` left DECCKM off.
  `h` is the final byte of every mode-ENABLING sequence, which is why the leaked character was an `h`. Fix: ONE
  file-local `exec_c0()` shared by `PS_GROUND` and `PS_CSI` — the rule existed twice and the two copies disagreed.
  ESC is the exception: it cancels the sequence, so the caller handles it. 14 checks, A/B 9 failures -> 0. Also
  measured and NOT a bug: bracketed paste is off for ~20 ms (bash, zsh) to ~200 ms (fish) after a shell starts, so
  a paste flushed in that window goes out raw — ISSUES.md "Paste".

- **2026-10-21/22, s98 — the stray `h`, real cause: the clipboard SOURCE.** Two confident wrong theories came
  before the one command pair that settled it (`wl-paste -l`, then `wl-paste --type <name> | od -c`). Measured
  against the user's live Firefox selection: ten advertised types ending `text/x-moz-url-priv`, which Firefox
  serves as **UTF-16LE** (`68 00 74 00 74 00 70 00 …`), and `offer_offer()` overwrote the recorded type for every
  `text/*` name, so the LAST advertisement won. bash's bracketed reader stops at the first NUL (one `h`); zsh
  inserts every byte and paints each NUL as `^@`. Fixed in two places because two things were wrong:
  `clipboard_mime_rank()` (ask for the best type, never the last) and `paste_repair()` (a NUL must never reach the
  pty; dense one-sided NULs or a BOM mean UTF-16, anything else is NUL-stripped). A/B on the user's own clipboard:
  42 UTF-16 bytes -> 6 clean UTF-8; on a source that offers ONLY UTF-16, 33 in -> 17 out. 19 checks. Their own
  clues — "the source matters", "ctrl+v works but ctrl+shift+v does not" — were the diagnosis; the second one works
  because the shell's own paste path drops NULs and Kai-Term's did not.

- **2026-10-22, s99 — the config split (their own request, confirmed 2026-10-22) and a config reader that had been
  throwing away every colour.** `apply()` became `apply_bind_key()` (the 24 `key_*`/`fm_key_*` names) +
  `apply_setting()` (the 29 others) under one dispatcher, split by NAME and never by which file was open, so an
  older `kai-term.conf` with bindings in it keeps working and a setting written in `kai-term.bind` is refused by
  name rather than treated as unknown. `config_find(name)` = the first REGULAR file with that name in
  `$XDG_CONFIG_HOME/kai-term`, `~/.config/kai-term`, `/etc/kai-term`, searched per FILE NAME (so a user with only a
  `kai-term.conf` still gets the system bindings); `-b/--binds` added; the bind file is read second, so it wins.
  The `#` rule was "whitespace before it is enough" — exactly the shape of a colour value — so
  `background = #1c1c22` became `background =`, and an empty value is legal, so nothing was even logged. Measured
  against the user's own file (`.scratch/s99/hashab/cfgprobe.c`): old `ff101014` (the built-in default), new
  `ff1c1c22` (what they wrote). New durable check: the template name-coverage check, which reads the accepted names
  out of the two strcmp ladders in `src/config.c` — A/B proved the hole was real (deleting `padding = 8` from the
  template: 0 failures on the old suite, 1 on the new). One harness bug cost 10 windowed checks: `run_env` used
  `env "$@"`, so `env` parsed the binary's `-w` as its own `--split-string` and the command never ran.

- **2026-10-22, s100 — close-out, the user's answers, and this cleanup.** The tree arrived with s99's code commit
  unpushed and its journal/ISSUES edits uncommitted; re-measured from clean, committed, `git fetch`, pushed,
  verified. The user then answered the four items that were open ("1. yes / 2. good / 3. works good"): the two-file
  split is theirs, the background change their own config file now produces is accepted, and the s98 paste fix, the
  preview-pane keys and the 75 % band's look work. Then, on request, this file was cut from 2843 lines /
  247969 bytes / 38871 words to a little under half that: the s89-s100 "Previous Task / entry N" narrative
  (1557 lines) collapsed into the era entries above — the same rule s87 applied to s68-s86 and ISSUES.md has
  followed since s66. Every mechanism name, measured number and pitfall was checked to survive outside the deleted
  text before it was deleted (the check: build the post-deletion text and grep it for each name — DECCKM,
  `exec_c0`, `clipboard_mime_rank`, `paste_repair`, `config_find`, `apply_bind_key`, `term_wants_key_mouse`,
  `term_click_arrow_keys`, `ink_in_box`, `pv_scroll`, `shell_int_gen`, `prompt_cx`, `C-x C-]`, 64/65 — all present),
  and the two stale counts the sweep found (`tests/test_vt.c` described as 878 checks and `tests/test_fm.c` as 274,
  both in Project Files) were corrected to 944 / 293. The full step-by-step detail of those sessions, including
  every failed probe and wrong theory, is in git history and in `.scratch/s100/AGENT.md.pre-cleanup`.

- **2026-10-22, s101 — the visible border around the grid: one runtime value now owns the default ink and paper.**
  The user's report and the diagnosis are in "Current Task"; the fix is the shape the plan predicted.
  `src/vt.c` gained file-scope `g_def_fg`/`g_def_bg` (initialised to `DEF_FG`/`DEF_BG`) with
  `term_set_default_colors(fg, bg)` + `term_default_fg()`/`term_default_bg()`, and every `DEF_FG`/`DEF_BG` use in
  that file became a read of them. Three background-ish numbers exist and only two were in play: `Config.bg` (the
  `background =` line) -> `Render.bg` -> `set_clear_color()`, which paints the `Config.padding` margin and the
  whole-cell remainder; `DEF_BG` -> what `src/vt.c` put in every blank, erased, scrolled-in and reflowed cell, which
  pass 1 of `render_draw()` paints as the grid's interior; and `palette[0] = ff000000`, what `SGR 40`/`48;5;0` paint,
  not involved. So the frame was `ff1c1c22` around `ff101014`. It "appeared recently" only because with the built-in
  defaults the two are the same number — the s99 `#` fix made their `background =` line take effect for the first
  time. Ruled out by measurement before writing any code: niri's focus ring (`width 4`, `active-color "#7fc8ff"`,
  `prefer-no-csd`, drawn as a solid rectangle *behind* windows that shows through semitransparent ones) — their file
  says `opacity = 1`, and the probe puts the difference inside Kai-Term's own two colours.
  The sites that changed: `row_new_cols`, `row_fillblank`, `row_resize` (both the grown columns and its
  `bg == 0` repair), reflow's fresh rows, the four `grid_blank_slot(…, DEF_BG)` calls in `grid_resize_rows`,
  `pen_default()` (so `term_init` and `ESC c`), `SGR 39/49`, and the `cell_pristine()` sentinel — the last one on
  purpose: the paint colour and the "never written" sentinel must be one variable, not two that can drift.
  `main.c` calls the setter once, right after the two config readers and before `mux_open()`/`render_init()`.
  Three patch-overs that existed only because of the split were deleted rather than left behind: the
  `pen.bg/pen.fg = conf.*` assignments after `mux_open()` in `main()` and in `tab_open()`, and `paint_blank_cells()`
  with its three call sites (startup, `tab_open`, `zoom_apply`) — vt.c now blanks what a zoom reveals by itself.
  Keeping them would have left a second writer of the same value, and `paint_blank_cells` was not even faithful: it
  re-painted every `ch == 0` cell from `Config.bg`, which would have destroyed a blank an application had erased
  with a colour of its own. `DEF_FG`/`DEF_BG` stay as the built-in defaults (`config_default()` and the template
  parity check need them) — the constants stayed, the uses became the variable.
  **Both directions A/B'd, not asserted.** The end-to-end headless evidence is `.scratch/s101/border/bgprobe2.c`:
  it reads the user's own config with the shipped reader, calls the setter the way `main.c` does, builds a Term, and
  compares the two numbers that make the picture — `Render.bg` (the glClear colour of the margin) and a blank cell's
  `bg` (pass 1's quad). New code: `clear=ff1c1c22 blank cell=ff1c1c22 match=1` for their file, for the built-in
  default, and for a third colour written just to test it (`background = 2050a0`, `padding = 40`, `opacity = 0.5` —
  both premultiplied identically). Built against `git show HEAD:src/vt.c` plus the new API (the state the user
  reported) the same probe prints `clear=ff1c1c22 blank cell=ff101014 match=0` and exits 1. In the unit suite, 7 of
  the 14 new checks fail on that same state — the fresh Term's pen, a never-written cell, `SGR 49`, `SGR 39`,
  `ESC c`, an erased cell, and the rows/columns a zoom reveals. Against a deliberately divergent variant (paint
  sites read `g_def_*`, `cell_pristine()` still compares `DEF_BG` — the pitfall the plan warned about): 2 fail, the
  reflow cursor position and the narrow/wide round trip, which is what shows the sentinel check pins something.
  The reflow check needed a measurement before it could be written: `.scratch/s101/reflowprobe.c` prints
  `cy/cx/scroll_off/wrap/bg` per step and shows that a 3-cell line at 40 cols re-broken to 2 cols leaves the cursor
  at `cy=0 cx=1` (scroll_off moved to 1), not `cy=1` — the first version of that check was red for that reason and
  the probe said so in one run. `tests/test_vt.c` 944 -> 958 checks, 0 failures; `./bin/dbg` MODEL OK;
  `./bin/test_fm` 293; `KAI_TERM_TEST_NO_WINDOW=1 make test` green; `make -j8` 0 warnings.
  **User-confirmed in their own window 2026-10-22** ("It's fixed, you can push") — the look is theirs to judge and
  they judged it. The windowed group of `tests/cli_test.sh` was not run (they were at the machine).

- **2026-10-22, s101 close-out — the confirmation and the push.** The user reported the frame gone in their window and
  said to push. `git fetch` first (the user commits on GitHub too): `origin/main` was still at `aec8add`, nothing to
  merge; `git push` then `git status -sb` to verify `## main...origin/main` with no ahead/behind marker rather than
  trusting the push output. Docs-only follow-up commit: the "outstanding / not yet verified" sentences in this file's
  Current Status, Current Task, Next Steps and s101 era entry, and in ISSUES.md's Open + Fixed entries, now say what
  is true — confirmed by the user. Re-ran the suites after the doc edits: `make -j8` 0 warnings, `CLI: 67 checks,
  0 failures, 13 skipped`, `test_vt` 958 / 0, `dbg` MODEL OK, `test_fm` 293 / 0.

- **2026-10-22, s101 follow-up — a coverage claim in the docs was too strong (the user asked, and asked for the
  fix).** AGENT.md's Current Status and ISSUES.md's re-measurement line both said the config-search, `kai-term.bind`
  and `-b` checks "execute only when a window can be mapped" / "run only when a window can be mapped". Checked
  against the suites before rewording: `tests/test_vt.c` has a headless `cfgsearch` group — 13 CHECKs, counted with
  `grep -c "cfgsearch:"` — that drives `config_find()` / `config_load(NULL)` / `config_load_binds(NULL)` against a
  scratch `HOME` + `XDG_CONFIG_HOME` tree (XDG wins, `~/.config` next, `/etc` last and never invented, an empty
  `XDG_CONFIG_HOME` means "not set", a DIRECTORY named `kai-term.conf` is skipped, `kai-term.bind` is searched
  independently), plus the `config split` group for the by-name rules. So the real gap left by
  `KAI_TERM_TEST_NO_WINDOW=1` is narrower and is worth naming exactly: `main()`'s argv handling (`-c/--config`,
  `-b/--binds`, combined flags, `-d` beating `log =`, `-w`, `-e`) and that a real launch applies the multiplexing /
  display / file-manager keys and the band's share. Both files now say that. Docs only — no source touched, suites
  not re-run (nothing they exercise changed).

- **2026-10-22, s102 planning — a feature survey measured before it was proposed, and the plan.** The user asked what
  is missing and then chose from the list, so every candidate was grepped for first rather than asserted; the counts
  below are hits in `src/` + `include/` and they are what the plan rests on: `zwp_fractional_scale`/`wp_viewporter` 0,
  `wl_primary` 0, OSC 52 0, OSC 8 / any URL detection 0, DECSCUSR 0, `zwp_text_input` 0, `tcgetpgrp` 0, and `BEL` is
  `case 0x07: break;` at `src/vt.c:3179`. Two claims of mine were **wrong and the user corrected them with facts about
  their own machine**: scaling — their `~/.config/niri/config.kdl` sets no `output { scale }`, so scale is 1 and
  `wl_surface_set_buffer_scale(…, 1)` is right, nothing is blurry, so the feature is pointless here; and OSC 52 —
  what they already do (paste inside tmux / ssh / ssh>termux) is the *local* clipboard typed into the pty, a different
  direction from the remote→local copy-out OSC 52 provides. Lesson worth keeping: a missing-protocol grep proves the
  code is absent, it does not prove the user is missing anything — check what their setup actually asks for before
  ranking it. Scrollback search and IME were dropped on their word.
  **Found while reading the colour paths: `cursor_color =` is dead config** — defaulted (`src/config.c:44`), parsed
  (`src/config.c:463-464`), stored (`kai-term.h:652`), and read nowhere; `src/render.c` uses the `CURSOR_COL` macro at
  six sites and `render_init()` takes only `bg`/`fg`. Every test naming it tests *parsing*, and the template parity
  check compares parse results, so the suite is green while the key does nothing — the same failure shape as the
  border bug, and the reason "the parser has the key" is never evidence "the key works". Filed in ISSUES.md "Open",
  folded into s102 step 1.
  **The user's own theme file is the acceptance case** for the importer (kitty spelling, "13th Floor"): 16 `colorN`,
  `background`, `foreground`, `cursor`, `selection_background`, plus `selection_foreground` and `cursor_text_color`
  which have nowhere to go and must be reported as dropped. That file is what decided the native format: use the
  kitty spelling as primary with Kai-Term's own spelling as an alias, so the kitty corpus is drop-in and the importer
  only has to sniff and map the *other* formats. Plan (7 features, measured facts, per-feature pitfalls and checks,
  8 open questions for the user) is in "Current Task". No source, test or README change in this entry — README must
  not document unimplemented features.

## Next Steps

0. **s102 — the seven features the user agreed to. Plan in "Current Task", NOT started, and it opens with eight
   questions that are the user's.** Do not start step 1 until they are answered; the plan already records what was
   measured, so do not re-derive it, and it records what the user rejected (search, scaling, OSC 52, IME), so do not
   re-propose those.

0. **s101 — the visible border around the grid: CLOSED.** Fixed, A/B'd, documented, and confirmed by the user in
   their own window ("It's fixed, you can push", 2026-10-22); pushed. Nothing about it is owed — do not re-derive the
   diagnosis and do not run a pixel harness to judge a look the user has already judged.
   a. The windowed group of `tests/cli_test.sh` (117 checks with a display) has not been run since s99. Run it
      only when the user is away and VRAM has room — it maps real windows.

0. **s99/s98 — all four open items were put to the user and answered on 2026-10-22 ("1. yes / 2. good / 3. works
   good", the s100 era entry). Nothing here is owed any more; what follows is the diagnosis recipe, not a to-do.**
   a. **The two-file config split is theirs — they asked for it.** Do not undo it and do not re-ask. Recipe for a
      config report: `./bin/kai-term -d` logs `config loaded from <path>` and `bindings loaded from <path>`; a line
      in the wrong file logs `is a key binding — those live in kai-term.bind now` or `is not a key binding —
      kai-term.bind holds bindings only`; nothing found logs `no kai-term.conf in $XDG_CONFIG_HOME/kai-term,
      ~/.config/kai-term or /etc/kai-term`. A colour is no longer confusable with a comment: `background = #1c1c22`
      is a value, `background = # note` is a comment. Note for a future session: the user's own
      `~/.config/kai-term/` holds only `kai-term.conf` (5 lines) and there is no `/etc/kai-term`, so their bindings
      are the built-ins — a `kai-term.bind` they never wrote is a valid state, not a bug.
   b. **Their background change is accepted** ("good"). `background = #1c1c22` in their file now applies; if they
      ever report the colours being wrong, that line is the first thing to read, not the shader.
   c. **The preview-pane keys are confirmed working by the user** ("works good"). The housekeeping item under them
      is unchanged and still real: the ROUTING lives in `fm_keys()` in `src/main.c`, which is in no test binary
      (item 5), so a regression there shows up as a report, not as a failing check.
1. **File manager: done and confirmed — the 75 % band, its preview row budget and the preview scrolling have the
   user's eye on them ("works good", 2026-10-22).** Built, wired, documented, green (293 model/layout/config checks
   + the s74 live key-injection run that proves the routing and the `cd` follow). If they report something visual,
   start from the `-d` lines `fm: opened on …`, `fm: listing <dir> (request N)`, `fm: dropping stale …`,
   `fm: keyboard -> …` and `graphics: … + the file-manager preview`, not from the shader. One decision they may
   still want to revisit, flagged and one line to change: `fm_follow` sends `cd <path>` unprompted. (`Return`
   typing a path is no longer one of them — s95 moved that to `p` at their request.)
1b. **The path key (s95) is done: built, green, measured headless AND confirmed by the user in their own window**
   ("The feature works", 2026-10-20). Nothing is owed here but a diagnosis if a report ever arrives. The recipe:
   `./bin/kai-term -d`, `Ctrl+Shift+F`, type `cat `, press `p` on a file — the prompt must read
   `cat '/path/to/file'` with the cursor after it and nothing executed. With `-d` the line is
   `fm: typed the selected path at the prompt: <path>`; a refusal logs
   `fm: not typing the path — the input line is not one I can describe` (alternate screen, wrapped line,
   scrolled-back view, repainted prompt, or a `cd` follow mid-flight). If they dislike a key, it is a config
   default, not a code change: `fm_key_path`, `fm_key_preview`, `fm_key_open`.
1c. **The mouse inside full-screen programs (s96) is done: built, green, measured headless AND confirmed by the
   user in their own window** ("I tested it and now I can scroll in nano", "nano --mouse works as expected", "I
   confirm it to be good", 2026-10-21). Nothing is owed here but a diagnosis if a report ever arrives. The recipe:
   `./bin/kai-term -d`, open `nano file`, turn the wheel — `-d` logs `mouse: the wheel became N cursor key
   press(es) for the application` (or `SS3 cursor key press(es)` once nano has asked for `ESC[?1h`, which it does
   on entry). A click logs `mouse: click at (col,row) became N cursor key press(es) for the application`. If the
   wheel scrolls nothing, check `term_wants_key_mouse()`'s two conditions first (`alt_screen` and no
   `MOUSE_PRESS`/`MOUSE_DRAG`) — a program that asked for the mouse is deliberately excluded and keeps getting
   reports. If a click lands one row off, that is the end-of-line clamp in `term_click_arrow_keys()` meeting a
   program whose arrow keys do not preserve the column (measured on nano; a program that wraps Right differently
   is the known limit). Both halves are config, not code: `wheel_sends_keys`, `click_moves_cursor`.
1d. **The stray `h` (s97): the parser bug is fixed, green and A/B'd — what is owed is the user's eye.** Their
   report was "the h can happen from opening Kai-Term and trying to paste … I just can't reproduce it at will".
   Ask them to paste the same way they did before and say whether the `h` is gone. If it is NOT gone, do not
   re-open the parser: the second mechanism is already measured and is not ours to fix — a paste flushed before the
   application asks for DEC mode 2004 (bash/zsh ~20 ms, fish ~200 ms after the shell starts) goes out RAW, and an
   escape sequence inside the pasted text is then interpreted by the shell, which echoes back `2004h`
   (`.scratch/s97/hprobe`). Reproduce that in one line: `printf 'AA\e[?2004hBB' | wl-copy`, paste into a fresh
   Kai-Term at a bash prompt — with the delimiters the line reads `AA^[[?2004hBB` (inert), without them it reads
   `AA2004hBB`. The parser rule is under "Architecture & Invariants" — "Parser (vt.c.)"; the checks are the
   `C0 inside CSI` group in `tests/test_vt.c`, and `grid_has_glyph()` is what proves a sequence was consumed
   rather than printed.
2. **Shell integration (s90-s92) is done: wired, green, measured headless AND confirmed by the user's eye in
   their own window for fish, bash and zsh (2026-10-20).** Nothing is owed here but a diagnosis if a report ever
   arrives. The recipe: run `./bin/kai-term -d`, browse into a directory with the band open, and read the log —
   `fm: shell integration (<kind>) asked to cd into <dir>` followed by
   `fm: the shell's integration changed directory to <dir> — the line was not touched`. `no integration ack` means
   the reader was not an integrated shell (expected for `env bash`, `sudo bash`, `/usr/bin/bash`); `shell
   integration: off for this run` means the script directory could not be created. In **bash** a stale prompt
   string is expected behaviour, not a bug (ISSUES.md "Shell integration"). One optional improvement, the user's
   call, not a defect: a `tests/` check running `fish -n` / `bash -n` / `zsh -n` over the generated scripts would
   catch a script that no shell can parse, but it makes the suite depend on external shells unless it skips when
   one is missing. (The zsh-mode gate itself now lives in `.scratch/s90b/intgate.c` with a `build.sh` beside it —
   added in s94.)
3. **Image support is done and confirmed by eye** (2026-10-17), and the user has settled its scope:
   "The goal was image support, Not auto-detection by timg so it work fine using `-p k`". So `-p k` IS the
   supported way to show a picture and nothing more is wanted here. Do NOT "improve" this by re-adding the
   graphics bits (4/16/18) to the DA1 answer or by inventing a `TERM` value: measured A/B (s70), advertising
   sixel without implementing it makes the picture vanish entirely, and timg's auto-detect list is hardcoded
   in its binary. Sixel/iTerm2 support would be a NEW feature — only on an explicit request.
4. **The prompt follow is implemented, idempotent, green and confirmed by the user in their own window (s86)
   — nothing is owed here but a diagnosis if it ever breaks.** The recipe, if a report arrives: run
   `./bin/kai-term -d`, type a partial command, open the band with `Ctrl+Shift+F`, navigate TWICE — each
   navigation must end with `fm: the prompt now holds the browser's directory and what you had typed`, with no
   `dropped the follow …` line, and the prompt must hold the text once, not `text cd <dir>`. Then diagnose
   headless (`.scratch/s86/follow2.c` drives the real machine over the real pty, no window) against the three
   predicates under "The prompt invariant" in Architecture & Invariants, before touching the model.
5. Housekeeping, none a bug:
   - ASan build of `kai-term` binary itself as a real Makefile target (only tests have one). The
     hand-built one covered mux/layout and found `row[rcap]` overrun; covering Wayland/EGL would pay off
     (also covers heap-grown `paste_buf`), and it is the only thing that would exercise `fm_shutdown()`
     under LeakSanitizer.
   - The file manager's input routing lives in `main.c`, which is in no test binary, so it is covered only
     by the injection harness in `.scratch/s74/`. Extracting the key-routing table into a testable unit
     would close that, but it is a refactor of the file the three s74 bugs were in — do it deliberately,
     not as a side effect.
   - Animated WebP previews are refused by libwebp's simple decode (measured, s74). Taking the first frame
     would need `libwebpdemux` — a new dependency, so only on an explicit request.
   - Idle-frame optimization: stop frame cycle when nothing is dirty — `redraw()` now calls
     `layout_apply()` every frame, so that path is hotter than before.
   - Color atlas re-uploaded whole with `glTexImage2D` on every new shaped emoji; `glTexSubImage2D` per new
     shelf would be cheaper.
   - Reflow allocates a fresh row buffer per produced line — fine at terminal sizes, but a huge-width window
     with huge scrollback churns allocator.
   - Window title is `mux_title()`'s own text; an app's OSC title only reaches tab bar label. Showing the
     active session's OSC title in window title is a one-line change in `update_title()`, but it changes
     visible behaviour — ask first.
6. Clipboard hardening niri does NOT require (do not do unasked — untested against a compositor that needs
   it): `wl_data_offer_accept()` before `wl_data_offer_receive()`; offer `UTF8_STRING` +
   `text/plain;charset=utf-8` next to `text/plain`.
7. Emoji paths that exist but have NEVER run here: SCALABLE color font branch (`FT_IS_SCALABLE` ->
   `FT_Set_Pixel_Sizes` + `hb_ft_font_changed` on zoom) — this box has only a bitmap-only Noto CBDT font; and a
   cluster HarfBuzz renders as >1 glyph, deliberately rejected (`ng == 1`), falls back to mono.
8. Scratch: throwaway probes live in `/home/kaida/Project/.scratch/` (outside project, 171 MB of it — mostly
   binaries and logs of closed sessions). **What the user's "don't touch .scratch for now" actually means, settled
   with them on 2026-10-20: do not DELETE or prune anything there; writing new files and editing existing ones is
   fine and expected** — that directory is where harnesses belong, and an agent that refuses to write there ends up
   putting throwaway work in `/tmp`, where it cannot be found again. Deleting is still the user's call. Harnesses
   worth keeping: `s70/timg_gate.c` (end-to-end image), `s74/inj_keys.sh` (the file-manager input
   routing gate — the only coverage `make test` does not have), `s86/follow2.c` (the prompt follow over a real
   pty), `s89/follow3` + `s89/bstoprobe` (the echo-window transition, old-vs-new A/B), `s90b/intgate.c` (the
   shell-integration gate: the machine copied from `main.c` over real fish/bash — re-diff it against `main.c`
   before trusting it, see the s92 pitfall), `s92/repaint.c` + `s92/nested.c` + `s92/hbcheck.c` (the three
   measurements behind the s92 fixes), `s45_old/` + `s47_before/` (A/B trees for a suspected draw-path
   regression). One fact to keep in mind either way: AGENT.md and ISSUES.md cite these paths, and a fresh clone of
   the repo cannot run them — so anything that is a real regression gate belongs in `tests/` eventually.
9. Git: keep committing verified work; `git fetch` before pushing.

## Project Files & Facts

```
Kai-Term/
├── .gitignore                   # bin/, build/, *.o/*.obj/*.d/*.so/*.a, editor leftovers, docs/ARCHITECTURE.md
├── LICENSE                      # MIT, "Copyright (c) 2026 Kaida"
├── AGENT.md                     # this journal
├── ISSUES.md                    # open items (none) + Fixed + known limitations
├── README.md                    # features, deps, build, flags, the two config files, shortcuts, exit status
├── Makefile                     # src/*.c -> build/*.o -> bin/kai-term; all/test/test-cli/asan/run/clean
├── config/kai-term.conf         # the shipped SETTINGS template: all 29 names at their built-in default
├── config/kai-term.bind         # the shipped KEY-BINDING template: all 24 names at their built-in default
├── include/kai-term/kai-term.h  # the single shared header: all types, prototypes, log levels, helpers
├── src/                         # config.c vt.c img.c mux.c layout.c fm.c shellint.c pty.c font.c render.c main.c
├── tests/test_vt.c              # 958 checks, 0 failures (parser, grid, graphics, config, prompt machinery)
├── tests/test_fm.c              # 293 checks: the file-manager model, previews, decoders, band geometry
├── tests/dbg.c                  # grid-model invariant checker (permutation + scroll_off/live_off bounds)
├── tests/cli_test.sh            # 117 checks driving the REAL bin/kai-term; ONE probe launch gates the
│                                # window checks, KAI_TERM_TEST_NO_WINDOW=1 skips them entirely
├── bin/                         # kai-term, test_vt, dbg (+ *_asan)          [generated, git-ignored]
└── build/                       # *.o + generated/xdg-shell-*                [generated, git-ignored]
```

- `src/vt.c` — unit-tested core: VT/ANSI/UTF-8 parser (incl. the APC/DCS states the kitty graphics
  protocol needs), grid model, the image list (decode + placement + lifetime), selection text
  extraction, alt screen, `term_grid_geometry()`, evdev -> SGR button mapping (`mouse_sgr_button`).
  No Wayland/GL dependency — the decoded PIXELS live here, so the whole protocol is testable headless.
- `src/img.c` — every image decoder in one place: PNG (libpng), JPEG (libjpeg), WebP (libwebp) and GIF
  (own LZW decoder, because giflib is not installed here), all returning premultiplied RGBA8, plus
  `img_sniff()` (magic bytes, never a file name) and `img_premultiply()`. Shared by the kitty graphics
  path in vt.c and the file manager's previews, so the two cannot drift. GIF decodes the FIRST image
  block only; an animated WebP is refused by libwebp's simple decode.
- `src/fm.c` — the file-manager model, no Wayland/EGL/threads: directory scan (`opendir`/`lstat`, sort,
  hidden filter, caps), navigation state, preview building (directory listing / image / text with syntax
  highlighting / metadata) and painting the two panes into `Cell` grids the existing renderer can draw
  unchanged. Also `fm_image_fit()` (the preview box in cells AND in px, aspect kept in pixels, never
  upscaled, centred) — the renderer only draws the box it is given, which is what makes the box testable.
  The two functions that touch nothing but malloc and the filesystem (`fm_scan_build`,
  `fm_preview_build`) are the only ones main.c's worker thread may call.
- `src/main.c` — all Wayland shell/input/clipboard/epoll logic: xkb keyboard, client-side key repeat, session
  shortcuts, pointer (bar clicks, pane focus, selection, mouse reporting, wheel), `layout_apply()`,
  frame-paced redraw, one epoll loop over every session's pty fd, SIGCHLD reaping by pid, CLI flags,
  data-device clipboard. Look here for pointer, keyboard, resize, frame pacing.
- `src/render.c` — EGL (platform Wayland) + GL 2.x: 2 glyph atlases + one GL texture per visible image,
  1 shader, 3 back-to-front passes emitted into ONE staging buffer but drawn as TWO batches (backgrounds,
  then bands + glyphs + chrome) with one draw per picture between them; instanced or expanded. Draws what
  `src/layout.c` says, owns no geometry.
- `src/font.c` — fontconfig pattern -> FreeType face; alpha8 glyph atlas (shelf packer + hash by codepoint);
  fontconfig fallback faces; HarfBuzz-shaped color emoji (`font_color_glyph`) + RGBA8 color atlas;
  `font_resize()` re-rasterizes at a new pixel size.
- `src/pty.c` — `posix_openpt`/`grantpt`/`unlockpt`; child: `setsid` + `TIOCSCTTY` + `chdir` + exec `$SHELL`
  (or `-e COMMAND`); sets `TERM`/`COLORTERM`/`TERM_PROGRAM`; `pty_setsize`. With the integration on, `tab_spawn()`
  hands it the string `shellint_argv()` built (`/bin/sh -c` form: the per-session request file in the environment
  plus the shell's load flag) instead of the bare shell path.
- `src/config.c` — the two config files (`kai-term.conf` settings, `kai-term.bind` bindings), one `key = value`
  line reader serving both, the search order (`config_find`: `$XDG_CONFIG_HOME/kai-term`, `~/.config/kai-term`,
  `/etc/kai-term`, first REGULAR file wins, empty result = built-in defaults), defaults, clamping, the
  name-routing dispatcher `apply()` (`apply_bind_key` then `apply_setting`, so a binding in the settings file is
  honoured with a pointer and a setting in the bind file is refused by name), the `#` comment rule (only-whitespace-
  before, or whitespace before AND after — the only-whitespace rule was eating `background = #1c1c22`), and the
  key-binding spec parser (`keybind_parse`/`keymods_parse`).
- `src/mux.c` — sessions of one window: dense `Tab` array (`{Term, Pty, attention, cd_req, int_dead, int_gen}`),
  active index, open/close/goto/next/prev, `max_tabs`, unread-output flag, `mux_title()`. No Wayland/EGL/fd/fork —
  that is what makes it unit-testable. The three integration fields live in `Tab` because `mux_close()` shifts the
  array: a side array indexed by tab number would hand a closing session's request path to its successor.
- `src/layout.c` — pure geometry: in window px + cell px + padding + gap + mode + session list; out bar strip,
  `BarItem[]` rects + labels, `PaneGeom[]` rects (each with cols/rows), the file-manager band (`FmGeom`:
  browser rect + preview rect + their cols/rows) and `layout_hit_fm()`, `layout_grid_size()`,
  `layout_initial_size()`.
- `src/shellint.c` — the shell integration, deliberately dumb and testable: the three shell scripts (fish, bash,
  zsh) as string constants, the private per-instance directory (`mkdtemp`, 0700, removed at exit), one
  `cd-request.<id>` file per session (temp + `rename()`), the ONE control sequence and the measurements that
  chose it, and the command line `tab_spawn()` hands `pty_spawn()` so a recognised shell loads the script without
  losing the user's own configuration. Nothing here talks to the shell except through those files and the pty;
  the protocol's other half is `kai_apc()` in `src/vt.c` and the state machine in `src/main.c`.
- `tests/test_vt.c` — 958 checks: parser (including a C0 byte inside a control sequence — executed, sequence
  still collecting, nothing painted — and a CSI sequence split across two bursts), clipboard text policy
  (`paste_repair`: UTF-16LE/BE, BOMs, surrogate pairs, sparse NULs stripped not decoded; `clipboard_mime_rank`
  ordering), grid, selection, alt screen,
  `live_off`, emoji clusters, CSI 3 J,
  bracketed paste, `term_init` guard, reflow, `term_grid_geometry`, zoom/alt-screen row-shrink,
  `mouse_sgr_button` and `mouse_wheel_button` (the SGR button order and the 64/65 wheel codes), DECCKM
  (`app_cursor` set/cleared by `ESC[?1h`/`ESC[?1l`, saved and restored across the alternate screen,
  `term_arrow_seq()` answering SS3 or CSI), and the mouse-as-keystrokes helpers (`term_wants_key_mouse`,
  `term_wheel_arrow_keys`, `term_click_arrow_keys` — the alt-screen/no-mouse gate, the vertical-then-horizontal
  order, the end-of-line column clamp, the buffer bounds), the graphics path
  (base64/chunking/PNG/zlib/raw/premultiply/placement/delete/scroll
  anchoring/id replacement/DA1-DA2-DSR answers), mux, keybind/keymods parsing, multiplexing config keys,
  `layout_compute` / `layout_grid_size` / `layout_initial_size` (the tab bar, the side-by-side panes and the
  file-manager band's SHARE: `fm_size` percent, both clamps, the terminal's floor, the share surviving a
  resize and a zoom, and three startup round trips band on/off/bar), and the prompt machinery the file
  manager's follow is built from (`term_at_prompt()` / `term_prompt_line()` / `term_prompt_line_typed()` /
  `term_prompt_line_is()`, the echo window and its transition rule, the clear -> `cd` -> re-populate sequence
  as the model sees it). `main.c` itself is in no test binary — the machine is covered by
  `.scratch/s86/follow2.c` and the live harness, not here. 2 invariants with dedicated checkers are `row[]`
  permutation (also `tests/dbg.c`) and startup-size round trip.
  The shell-integration half: the APC kind-`K` channel (announcement, ack, an unknown kind refused, an oversized
  payload, a reset keeping the announcement), the announcement **generation** counter, and the scripts as they
  land on disk (each binds exactly what `shellint_seq()` returns, fish repaints, bash does NOT pretend to,
  every script names `$KAI_TERM_INT_DIR`, every `shellint_argv()` carries it, the nested-shell wrappers are
  there, and cleanup finds a request file whose id is past `TAB_MAX`).
- `tests/test_fm.c` — 293 checks against a real temporary directory tree (dirs, a dotfile, a dangling
  symlink, a FIFO, an unreadable dir, four image formats, a binary file): scan + sort + caps + truncation,
  the hidden filter and `fm_set_hidden`, selection and scrolling (`fm_row_pos`, `ensure_visible`),
  navigation, the request-id rule (a stale answer is dropped), every preview kind and its error path, the
  highlighter reaching the cells, the pane painters, `fm_image_fit`, the preview's row budget (no painted
  glyph may sit inside the box the picture is drawn into — `ink_in_box()`), the preview pane's own scroll
  (`pv_scroll` / `fm_preview_scroll` / `fm_preview_scroll_max`: the painter obeys it for a text file and for a
  directory listing, both ends clamp, a new preview starts at the top, a pane with no body has nothing to scroll,
  and a resize pulls the scroll back inside what the NEW pane can show), and the band's rects inside
  `layout_compute()`.
- `Makefile` — pkg-config-driven; `-Iinclude/kai-term -Ibuild/generated`; output dirs via order-only
  prerequisites so they never force rebuilds; generates xdg-shell bindings with `wayland-scanner`.

**Facts not in code** (deps + MIT license: README "Requirements"/"License"):

- **Dependency rules:** harfbuzz required since s37 — README must keep listing it. Deliberately no
  cairo/pango/Qt/GUI framework. xdg-shell bindings generated at build time, not vendored.
- **Naming:** project/binary/identifiers `kai-term` (`kai_term` in C), display name `Kai-Term`. Renamed from
  `K-Term`/`k-term` 2026-10-16 (s59) due to a name conflict with another of user's projects — never
  "restore" short form anywhere: dir, binary, header, `DEF_APP_ID`/`DEF_TITLE`, log prefix,
  `TERM_PROGRAM`, config path all move together; tests/test_vt.c pins two `DEF_*` literals so a
  half-rename fails a check.
- **Layout:** sources `src/`, public header `include/kai-term/`, tests `tests/`, binaries `bin/`, objects +
  generated bindings `build/`. `make clean` deletes `bin/` and `build/` entirely. No `docs/` any more: the
  architecture doc was removed 2026-10-16 (s63) because it carried host-system facts; its invariants are in
  "Architecture & Invariants" below, its tooling parts in `/home/kaida/Project/ENVIRONMENT.md`.
- **License:** MIT, "Copyright (c) 2026 Kaida" — user's choice; must ship with any redistribution. No
  source-file SPDX headers exist; if added they must read `SPDX-License-Identifier: MIT`.
- **Git:** `origin = https://github.com/K-code-AI/Kai-Term`, branch `main`; agent has push access (user
  said so 2026-10-15). Still run `git log --oneline` / `git status` before assuming anything is committed;
  never commit `bin/` or `build/`.
- **Known limitations a newcomer must not "fix" by accident:** narrowing can evict OLDEST scrollback rows;
  alternate screen is deliberately NOT re-wrapped; a taller window keeps less scrollback than configured
  (`rcap` fixed at startup); window title is Kai-Term's own text, not app's OSC title. Full list:
  ISSUES.md "Known limitations".

## Architecture & Invariants

Rules a change must not break. Why each exists: git history of fixed bugs (`git show d5afe4a:ISSUES.md`).

**Data flow.** PTY bytes -> `term_parse()` (vt.c) -> one `Grid` per session -> `Mux.tab[]` owns one
`{Term, Pty}` per session, one ACTIVE -> `layout_compute()` (window px + cell px + padding + mode + session
list) -> bar strip, item rects, pane rects -> `render_draw(&rend, &font, &g_layout, terms)` -> bar + one grid
per pane + focus border -> EGL -> `wl_surface`. Input: `kb_key`/`ptr_*` -> pty_write to ACTIVE session,
after `ptr_*` hit-tested bar then panes.

**Event loop (main.c).**

- epoll over: `wl_display` fd; one pty master fd per session (`tab_for_fd()` maps back; pty EOF closes that
  session, not program); 8 ms timerfd (`FRAME_INTERVAL_NS`); `signalfd(SIGCHLD)` (SIGCHLD blocked so only
  signalfd reads it; reap `waitpid(-1,…,WNOHANG)`, match pids via `tab_for_child()`, then zero `pty.child`);
  paste idle timerfd; key-repeat timerfd; clipboard paste pipe read end joins transiently.
- **Finish wayland read before handling ANY other descriptor** (`read_events`/`cancel_read`, then
  `dispatch_pending`): `eglSwapBuffers` waits for a `wl_buffer.release`; while a
  `wl_display_prepare_read()` is outstanding that release can never be read -> swap blocks forever -> terminal
  looks dead. `read_events()` inside a drain loop instead blocks on display fd and starves
  timerfd/signalfd.

**Frame pacing — 3 rules, each from a real freeze.**

1. Arm a frame callback only together with a commit that attaches a NEW buffer (from `redraw()` after a
   successful swap). Damage-driven compositors run a render pass only on change, so a callback armed on an
   EMPTY commit can stay pending forever, and every draw path is gated on "no callback in flight" -> frozen
   display. `solicit_frame()` was deleted for exactly this — do not re-add it. Failed swap: keep `W.dirty` for
   timer to retry, arm nothing.
2. `eglSwapBuffers` is not guaranteed to return — it waits for compositor to release a buffer. The
   constraint is on wayland READ, not on listeners: swapping inside a listener is fine once read is
   complete. At most one buffer in flight (`W.frame_cb`).
3. The FIRST buffer must be attached by FIRST `xdg_surface_configure` handler (`!W.swapped_once`): a
   window without a buffer never maps, so never gets a frame callback either. A configure changing pixel
   size sets `W.resized`; timerfd branch and `frame_done()` draw when `dirty || resized` — waiting on
   `dirty` alone is what froze screen in fullscreen.

**Wayland/input.**

- Pointer coords are fixed point: `wl_fixed_t` is `int32_t` encoding px*256 -> apply `wl_fixed_to_int` before
  storing (storing raw int compiles warning-free and is 256x too large).
- Key repeat is client's job: `wl_keyboard.key` delivers one press + one release per physical keypress;
  `repeat_info` only reports system setting. `send_key()` returns whether press is repeatable; `krfd`
  arms for `repeat_delay`, re-arms at `1/repeat_rate`; release / focus loss / keymap change call
  `repeat_stop()`, which DRAINS an expiry that already fired. `it_interval.tv_nsec` must stay < 1e9. The key
  is re-TRANSLATED on every repeat -> Shift while holding `a` starts producing `A`.
- Seat bound at v5 for `axis_discrete`/`frame`; every listener slot bound version can dispatch must be
  non-NULL or libwayland aborts.
- Clipboard is push-based (no `request_selection`): copy = `set_selection`; paste = receive offered
  `text/plain` into a pipe whose read end joins epoll. One paste = ONE bracketed block: accumulate, write only
  at EOF (or 400 ms watchdog), in <=4096-byte pieces, wrapped `ESC[200~` … `ESC[201~` when DEC mode 2004
  is on; over 8 MiB sent unwrapped with a warning.

**Multiplexing/layout.**

- `Mux` owns a dense `Tab tab[TAB_MAX]` plus `n`/`active`; `cur_term()`/`cur_pty()` resolve through
  `mux_cur()` — no second copy of "which session is visible".
- `term_init()` memsets whole `Term`; `term_destroy()` frees both grids -> a session is initialized and
  destroyed exactly once; `mux_close()` zeroes vacated slot. Because `mux_close()` shifts later tabs down
  it must RE-ARM `tab[i].term.ps.t` for every moved tab and must capture `Pty` BEFORE shift.
- Output on a session with no pane: set `Tab.attention`, do NOT dirty frame. Output on a drawn-but-inactive
  session: dirty it, set no flag.
- `layout_apply()` is only caller of `layout_compute()`; resizes EVERY session to `layout_grid_size()` (so
  a tab switch costs no resize or reflow) and sets `W.gx/W.gy` to active session's pane origin, which
  `sel_cell()`/`mouse_cell()` map through. Runs from every geometry change AND from `redraw()`, because bar
  labels are part of picture.
- `ptr_button()` checks `layout_hit_bar()` BEFORE mouse-reporting branch — an app that grabbed mouse
  still cannot eat bar. Re-check `cur_term()` after a middle click (closing last session makes it NULL
  inside a listener).
- Chrome colours come from CONFIGURED palette (`Render.fg`/`Render.bg`), never from a `Term`'s pen — that
  is application's current SGR state and would let `printf '\e[31m'` repaint bar that names its tab.

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
  read as the end of a sequence invents a bug (see the pitfall on hand-counted lengths).

**Clipboard (main.c + the paste helpers in vt.c).**

- **A NUL byte must never reach the pty.** `paste_flush()` runs `paste_repair()` on the buffered clipboard block
  before writing any of it. A source may serve UTF-16 under a UTF-8 label (Firefox does, under
  `text/x-moz-url-priv`), and UTF-16 written verbatim into a bracketed paste is what the user saw as a stray `h`:
  bash's bracketed reader stops at the first NUL, zsh inserts every byte and paints each NUL as `^@`. Repair only
  when the block actually contains a NUL — a block with none is copied through byte-for-byte, unchanged.
- **Which mime type to ask for is a preference, not an enumeration accident.** `clipboard_mime_rank()` decides;
  `offer_offer()` keeps the highest rank seen. The old rule overwrote the recorded type for every `text/*` name, so
  the LAST type the source advertised won — with Firefox that is `text/x-moz-url-priv`, which is UTF-16. Never
  "fix" this back to last-wins or first-wins: both are at the source's mercy.
- **UTF-16 is decoded only when the evidence is unambiguous** — a BOM, or NULs that are dense (`nuls*2 >= len`) and
  one-sided. Sparse NULs are stripped, not decoded: guessing on ambiguous input turns good text into garbage, which
  is worse than the NUL being removed. UTF-16 with no BOM and no NULs at all is undetectable and is a documented
  limitation, not a bug to chase.
- **`Ctrl`+`Shift`+`V` is Kai-Term's paste; plain `Ctrl`+`V` is the shell's.** Only the first is intercepted
  (`send_key()` gates on `shift_held`). The shell's path reads the clipboard itself and drops NUL bytes, so the two
  can differ from the same clipboard — when a user reports "one works and the other doesn't", that difference is the
  diagnosis, not a second bug.
- **When a paste arrives wrong, measure the SOURCE before the parser:** `wl-paste -l` for the advertised types and
  `wl-paste --type <name> | od -c` for the bytes served under each. That single command pair settles in seconds what
  cost two wrong theories here.

**Grid model (vt.c).**

- `Grid.row` has fixed capacity `rcap = rows + scrollback_max` and is always a PERMUTATION of allocated
  buffers: scrolling is rotation (`rot_left`/`rot_right`), never `memmove` of pointers -> no slot is NULLed,
  duplicated or double-freed (`tests/dbg.c` asserts it). `grid_vprow(g,y) == row[scroll_off + y]`.
- Anchor invariant: `0 <= scroll_off <= live_off <= sb_limit = min(scrollback_max, rcap - rows)` and
  `live_off + rows <= rcap`. `live_off` is `scroll_off` showing newest output; user navigation moves
  `scroll_off` below it; `snap_to_live()` restores it before parsing so new output lands at live bottom.
- `cy` is a LIVE-view coordinate (parser addresses `row[live_off + cy]`) -> `term_resize` re-anchors `cy`
  with `live_off`. Cursor drawn only when `scroll_off == live_off`. Whole-viewport scrolls snap `scroll_off`
  back to `live_off` first.
- `Grid.valid == GRID_VALID` between init and destroy — a plain 0/1 flag is not enough because uninitialized
  stack memory is routinely non-zero (stack garbage measured `32511`).
- Alt screen: `Term.g` stays ACTIVE grid, main grid is parked in `Term.alt` (alt has scrollback 0);
  `?1049` saves/restores cursor, `?47`/`?1047` switch without saving, `?1048` is cursor-only;
  `term_resize` resizes parked grid too.
- CSI 3 J: `erase_scrollback()` moves viewport rows to slots `0..rows-1` through a validated scratch
  array, blanks rest, sets `scroll_off = live_off = 0`; gated by a process-wide setting, deliberately NOT a
  `Term` field because RIS memsets whole `Term`.
- Reflow: `Grid.wrap[i] != 0` means line in `row[i]` was broken by RIGHT MARGIN and continues in
  `row[i+1]`; a byte array parallel to `row[]` that rotates with it and is cleared by every operation that
  blanks or erases a slot. `grid_reflow()` measures each row with `row_used()`/`cell_pristine()`, not full
  width — otherwise never-written cells and wide-glyph padding become literal spaces and tracked cursor
  cell gets trimmed; joins and re-breaks at new width, pulling a boundary back only when cell that
  would START next chunk carries `ATTR_WIDE` — peek NEXT source cell, because reacting to a wide pair
  that legitimately ENDED a chunk loses a character; builds output oldest-first in a fixed `2*rcap+2` array,
  evicting oldest rows once `rcap` exist; commits by installing new buffers (permutation kept). The
  eviction step must run for EVERY produced row, blank rows included, or `kept` can exceed `rcap` and the
  commit drops buffers instead of installing them (a leak). Those 3 traps each cost a real bug.
  `grid_resize_plain()` is no-reflow variant for ALTERNATE screen (a full-screen app repaints itself;
  re-wrapping would scramble it) — parked main grid is still reflowed.

- **Images are anchored to an ABSOLUTE document row** (`Grid.row_base` + the row the cursor was on), never
  to a viewport row or a row pointer: a viewport row is invalidated by every scroll and a pointer by every
  reflow. `row_base` moves only when rows are evicted at the top or the window slides over its scrollback,
  so `image_prune()` can decide "this picture's rows no longer exist" from two numbers. The span in cells
  is computed in vt.c, not in the renderer, because it is also what `a=T` moves the cursor past — if the
  renderer scaled differently from the model the prompt would land on the wrong row.

**File manager band (fm.c + layout.c + render.c + main.c).**

- The band is geometry, model and drawing in the same three places as everything else: `layout_compute()`
  splits the grid area VERTICALLY first (a band taking `fm_size` PERCENT of its height on top, terminal
  pane(s) below at `pane_y = grid_y + band_h + gap`), then splits the band horizontally (browser | preview).
  Sessions stay ordinary `pane[]` entries, only shorter — that is what keeps `layout_hit_pane`,
  `layout_pane_of_tab`, `layout_grid_size()`, the focus border and `W.gx/W.gy` working with the band on AND
  off. `layout_hit_fm()` must be checked BEFORE `layout_hit_pane()`.
- The band is clamped, never trusted: `band_h = grid_h * pct / 100` (a `long long` multiply, so a 4K window
  cannot overflow) is reduced so the shell keeps `FM_TERM_MIN_ROWS` rows, and dropped entirely when even that
  leaves fewer than `FM_ROWS_MIN`. Clamped in layout.c, not config.c, so no caller can ask for a 1-row band —
  the tab bar's "the terminal wins" rule again. It is a SHARE and not a row count on purpose: the band then
  survives a resize or a zoom with the proportion the user asked for. `layout_initial_size()` is its inverse
  (`grid_h = ceil((rows*ch + gap) * 100 / (100 - pct))`) and the round-trip checks in `tests/test_vt.c` are
  what prove that arithmetic.
- **The preview pane's rows have ONE owner.** `preview_head_rows()` (2, +1 when the preview has an error
  line) and `preview_meta_rows()` (image previews only: `min(nmeta, prev.rows - head - 1)`, so the picture
  always keeps a row) are called by `paint_preview()` AND by `fm_image_fit()`; the picture box starts at
  `head + meta_rows`. Glyphs are emitted in the batch AFTER the picture, so any disagreement between the two
  about which rows text owns paints metadata across the image — that was the bug (the header's size used to
  be a literal 3 in two functions and the meta rows were counted in neither), and `ink_in_box()` in
  `tests/test_fm.c` is what keeps it from coming back.
- The two panes are `FmPane` = a flat `Cell` array + cols/rows, walked exactly like a session grid, so the
  renderer needs no new drawing path: "a grid with no cursor and no selection". Glyphs go through the same
  `emit_cell_glyph()` as a session (a file name can hold any codepoint, so the emoji/wide rules must be
  shared, not copied), backgrounds into batch A, glyphs into batch B, the preview picture between them.
- **The worker thread may call exactly two functions** — `fm_scan_build()` and `fm_preview_build()` — which
  touch nothing but malloc, the filesystem and their own argument. The main thread owns `FileMgr` and
  installs every answer, so the request-id rule in `fm_install_scan()`/`fm_install_preview()` is the ONLY
  synchronisation the model needs. Ids come from one monotonic counter (`FileMgr.seq`) with per-kind newest
  ids (`scan_req`, `pv_req`); a single shared counter was wrong because every preview then invalidated an
  in-flight listing. Answers cross a pipe as one 24-byte `FmResult` (atomic, far under `PIPE_BUF`); the
  request slots hold only the NEWEST request of each kind, so a superseded directory is never read at all.
- **The keyboard gate is the feature's safety:** `fm_keys()` is matched only while `g_fm.on` and
  `g_fm.focus != FM_FOCUS_TERM`, which is the only reason the default bindings can be unmodified keys.
  `fm_keys()` returns -1 (not the band's), 0 (handled, must not repeat) or 1 (handled, repeatable) — two
  states cannot express that, and getting it wrong sends Return/BackSpace/`r`/`Escape` to the shell as well
  as to the browser (measured live, s74).
- **The band has THREE focus states, and the keyboard routing differs between them for exactly four keys.**
  `FM_FOCUS_TERM` / `FM_FOCUS_LIST` / `FM_FOCUS_PREVIEW`; a click on the preview pane or the wheel over it owns
  the keyboard to the preview. While the PREVIEW owns it, the band's `fm_key_pageup`/`pagedown`/`home`/`end`
  bindings scroll the previewed TEXT (`fm_preview_page`/`fm_preview_top`/`fm_preview_bottom` in fm.c, routed in
  `fm_keys()` in main.c) instead of moving the listing; `fm_key_up`/`down` deliberately keep moving the listing,
  because changing WHICH entry is previewed is the one thing the preview cannot do for itself. One page is
  `fm_preview_page_rows()` — the body rows the pane shows at once — because a Page key must move by exactly what
  the user can see; all three go through `fm_preview_scroll()`, so the clamp has one owner and a preview with no
  body (a picture, plain metadata) simply does not move. Same four bindings, no second set of names to keep in
  sync in `kai-term.bind`.
- `fm_send_cd()` is the only place the feature writes to the shell, never to a session on the alternate
  screen, and only when the directory actually changed (`fm_last_cd`, cleared by `tab_switch()`).
- **The prompt invariant (what the follow may never violate).** Kai-Term cannot see where a prompt ENDS — the
  tty echo arrives as ordinary output — so the model remembers the whole cursor row as the shell last printed
  it (`prompt_row`/`prompt_cx`/`prompt_cy`) and treats "does that row still match, cell for cell" as the only
  evidence available. Three rules follow, and every one of them has a check in `tests/test_vt.c`:
  1. **The `cd` is written only from ERASING/CDING behind `term_at_prompt()`** — i.e. only once the row is
     proven to be the bare remembered prompt. "Runs a mangled command" is closed by construction, not by
     trusting a key binding, so no capture rule may ever gate the write.
  2. **A steady state is not evidence.** The echo window closes only when the row CHANGED into the remembered
     prompt between the start and the end of one burst (`prompt_matched_before` in `term_parse()`). A burst
     that leaves the row exactly as it was — fish repainting OSC 7 / OSC 0 / DECSET after Kai-Term re-typed
     the user's text — must not close it, or the next burst re-takes the snapshot over Kai-Term's own echo
     and a line holding text starts answering "empty" (the s86 bug the user reported).
  3. **A burst boundary is not a prompt.** CDING additionally waits for the session to go quiet
     (`FMCD_QUIET_MS` 150, advanced from the frame timer) before believing the new prompt is drawn: fish
     answers a `cd` with the echo, a continuation row and prompt chrome in separate bursts, each of which
     re-takes the snapshot.
  4. **The snapshot's cursor anchor means "where the cursor sits when the line is EMPTY".** `prompt_cx`/
     `prompt_cy` are the column `term_at_prompt()` insists on AND the column `term_prompt_line()` measures
     typed text from, so `prompt_snapshot()` must not re-take them when the row is cell-for-cell the
     remembered prompt and only the cursor moved. `prompt_row_same_cells()` is that test and also the row half
     of `prompt_row_matches()` — one comparison, two callers. Before s89 a movement key kept `input_pending`
     set, which froze the snapshot by accident; the `CSI G` checks in `tests/test_vt.c` pin the rule now.
  5. **Opening the echo window has to mean something.** `term_note_input()` opens it for any payload that can
     put a character on the line (text, Tab, Up/Down, Ctrl+R/S/T/V/Y/_, Insert, PageUp/PageDown, F-keys, an
     unknown or truncated sequence) and — only when the line is already provably the bare prompt — not for one
     that cannot (cursor movement, deletion, Ctrl+L, a bare Escape). Measured (s89): bash/sh answer BackSpace
     at an empty prompt with a bell only and fish with nothing, so no burst ever re-proved the line empty and
     the follow stayed dead until Enter. The row comparison still re-checks everything on every call, so a key
     that really moves the cursor or rewrites the row still makes `term_at_prompt()` answer "no".
  The three description predicates must not be confused: `term_at_prompt()` = "safe to WRITE here"; strict
  `term_prompt_line()` = "I can describe this line and nothing survives an erase"; `term_prompt_line_typed()`
  / `term_prompt_line_is()` = "ignore the shell's ghost text right of the cursor", legal only for the capture
  (which is followed by the proof) and for confirming Kai-Term's own typing. `input_pending` is deliberately
  NOT consulted by the description predicates — the row comparison is the evidence, and Kai-Term's own typed
  path sets that flag while leaving a perfectly describable line behind.

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
  measured (s91): parses as ONE key in the shell's reader (fish ignores literal byte sequences it cannot parse
  as a key), inert in EVERY supported shell when unbound (that is what bounds the damage when a nested shell is
  the one reading), unbound by default (Kai-Term steals nothing), and free of the tty's own special characters.
  The last one is the trap: `C-\` (0x1c) is inert and unbound in both shells but `stty -a` reports `quit = ^\`,
  so it would SIGQUIT whatever owns the terminal whenever the shell is not reading. Check `stty -a` before
  adding a byte; `0x18`/`0x1d` are clean (`swtch = <undef>`).
- **The integration write never opens the echo window.** `term_note_input()` is for bytes whose echo may appear
  on the line; these are consumed by the line editor, so the write goes through `pty_write()` alone. Opening the
  window would make `term_at_prompt()` answer "not empty" for no reason and hold the typed fallback shut behind
  the s89 whitelist. The ack, not the echo, is the evidence for this path.
- **Per-session state lives in `Tab` (`cd_req`, `int_dead`, `int_gen`), not in an array indexed by tab number.**
  `mux_close()` shifts the array and a freed index is reused, so an index-keyed side array would give two live
  sessions the same request file — one shell then cds into a directory the other asked for. `g_int_seq` numbers
  the files and never repeats within a run, which is why `shellint_cleanup()` SCANS the directory for
  `cd-request*` instead of enumerating `0..TAB_MAX-1`.
- **The scripts must not replace the user's configuration:** fish gets `-C "source …"` (evaluated after the
  user's config), bash gets `--rcfile <ours>` which sources `~/.bashrc` first, zsh gets `ZDOTDIR=<ours>` with a
  `.zshrc` that sources the user's real one first (`KAI_TERM_REAL_ZDOTDIR` remembers where that was). `-e
  COMMAND` is never wrapped — that command line is what the user asked to run. The private directory is
  `mkdtemp` under `$XDG_RUNTIME_DIR` else `/tmp`, `chmod 0700`, refused if the name contains a `'` (the command
  line quotes with `'`), and removed at exit.
- **`ESC c` (RIS) memsets `Term`, and the announcement must survive it** — the script announces once, at shell
  startup, so losing it would silently drop the feature for the rest of the session. The prompt snapshot is
  deliberately NOT preserved (a reset really does invalidate it).
- **An announcement is an EVENT, counted, not just a kind.** `Term.shell_int_gen` goes up on every accepted
  announcement and `Tab.int_gen` is the value main.c last saw; when they differ, `fm_int_try()` clears
  `Tab.int_dead` (and logs it). Without the counter the first shell that failed to answer — a plain `env bash`
  started inside an integrated fish — would keep that session on the typed path for the rest of the run even
  after an integrated shell was reading the line again. The kind cannot express this (fish -> a nested fish ->
  back to fish announces three times with one kind), which is why it is a counter and not a flag.
- **The scripts wrap a bare `fish`/`bash`/`zsh` and re-announce after every command.** That is what makes a shell
  the user starts at the prompt integrated too (the user asked for it, 2026-10-20) and what feeds the counter
  above: fish `fish_postexec`, bash `PROMPT_COMMAND` (PREPENDED, string AND array form handled, guarded by a
  NON-exported variable so a nested bash still installs its own), zsh `precmd_functions+=`. Three limits, all
  deliberate: only a command with NO arguments is wrapped, a name the user already defined as a function or alias
  is never replaced, and the wrapper re-announces its own kind when the nested shell exits. A heartbeat was
  checked not to leak into a command substitution before it shipped (`.scratch/s92/hbcheck.c`).
- **The prompt is repainted only where the shell can be asked to.** fish: `commandline -f repaint` at the end of
  the handler (measured to re-expand the prompt with the typed text byte-identical). zsh: `zle reset-prompt`
  (verified in zsh 5.9 since s94, 2026-10-20 — the gate's zsh run repaints and answers in 54 ms; what has never
  been measured in zsh is the inert-when-unbound case). bash: NOTHING is attempted, because readline expands the prompt once per
  `readline()` call — `C-l` leaves the old directory and printing `${PS1@P}` over the line garbles it (both
  measured). Do not "fix" bash by pressing Enter for the user or by writing over the prompt line.

**Geometry: one helper decides a grid's size AND its origin.**

- `term_grid_geometry()` (vt.c) is called once per pane from `layout_compute()` with `pad = 0` (window's
  `Config.padding` margin and pane gap are already in pane rect). 3 users must agree:
  `layout_apply()` (grid + PTY size), `sel_cell()`/`mouse_cell()` (pointer -> cell), `render_draw()` (where the
  quads go).
- Padding applied on all 4 sides, capped at `(window - one cell)/2` per side; `cols = (win_w - 2*xoff)/cell_w`;
  whole-cell remainder stays at RIGHT and BOTTOM, never top/left, so `row = (ptr_y - gy)/cell_h`,
  `col = (ptr_x - gx)/cell_w` is exact. Anchor anywhere else and every click, drag and SGR report lands on the
  wrong row.
- The margin is painted by `glClear`, so clear colour must be CONFIGURED background (`Render.bg`
  premultiplied by `Config.opacity`), not a hardcoded default; `render_draw` clears every frame.

**Rendering.**

- 2 glyph atlas textures (alpha8 mono, RGBA8 color emoji), each uploaded only when dirty, AFTER emit
  passes and BEFORE draw — glyph lookups run inside pass 3, so uploading at top of `render_draw` shows
  every new glyph one frame late (and a first emoji as a black box). Application pictures are the other
  kind of texture: one per image the model shows, keyed by `KImage.serial` (unique per session, never
  reused, so a recycled pointer cannot hand back a stale texture), re-uploaded when `version` changes,
  and deleted after any frame in which no visible image claimed it (`ImgTex.seen` stamped with
  `Render.img_frame`) — the GPU never holds more than what is on screen.
- One shader; `a_glyph` selects path: 0 background quad, 1 mono glyph (tinted by `v_fg`), 2 color emoji
  (premultiplied, not tinted), 3 an application picture (same premultiplied composite against the
  picture's own texture; vt.c premultiplies every decoded pixel, so still no GL blending).
- Pass order back to front: all cell backgrounds, then selection bands, then glyph quads — a WIDE glyph
  quad spans into next cell and neighbour's background must not be emitted after it. Continuation
  cells (`ATTR_WIDE`) are skipped in pass 3.
- **A frame is TWO draw batches, not one call**, because a picture must be over the cell backgrounds and
  under the text and blending is never enabled: batch A = pass 1 + bar backgrounds (range `[0,nq_bg)` of
  the staging buffer), then one draw per visible picture, then batch B = bands + glyphs + labels + focus
  border. Both ranges live in ONE staging buffer (records are fixed-size, so a batch is just a range);
  `glClear` runs ONCE per frame — clearing between batches would erase batch A. `setup_draw_state()` is
  re-applied before each batch because an atlas texture can be created between them.
- Instanced path when both `GL_ARB_draw_instanced` and `GL_ARB_instanced_arrays` are advertised; otherwise the
  same 19-float records are expanded to 21 floats per vertex and drawn with `glDrawArrays`.
- No GL blending is ever enabled: fragment shader composites `premul + bg*(1-a)` and a Wayland EGL ARGB
  buffer is PREMULTIPLIED, so writing raw colour with partial alpha reads washed out; `max(a, bg.a)` is wrong
  too (it painted glyph's bounding box opaque). `Config.opacity` must be clamped with `fclamp_`, never
  `iclamp_` (an int clamp turns 0.5 into 0).
- Color emoji: `putch` folds ZWJ, variation selectors, skin tones, keycap, tag chars and second regional
  indicator into ANCHOR cell's `extra[]` (`CLUSTER_MAX` 8) so a `Cell` stays copyable by value;
  `font_color_glyph()` hands whole sequence to `hb_shape`, which collapses it to one glyph id (anything
  else falls back to mono). FreeType loads it with `FT_LOAD_COLOR | FT_LOAD_NO_SCALE`; renderer scales by
  `cell_h / color_px`, so zoom never re-rasterizes an emoji. A wide cluster reserves TWO grid cells
  (`cluster_is_wide()` is single source of truth, also true when U+FE0F/U+20E3 switch base to emoji
  presentation) and `render_draw` sizes quad with same predicate. Cluster state is invalidated on
  every row rotation, cursor jump, BS/HT/CR, newline and resize. Misses are cached; color atlas starts at
  1024x512 and doubles up to 8192; a glyph that cannot fit falls back to mono rather than drawing with stale
  texcoords.

**Selection.**

- A drag only ever produces CELL edges: `sel_cell()` maps pointer to a cell; `sel_set_endpoints()` gives
  earlier cell its LEFT edge and later one its RIGHT edge; direction is recomputed on every motion
  event, so dragging left past anchor SWAPS pair.
- Pass 2 skips a cell that was never written (`ch == 0`) so a drag past end of a line stops at last
  glyph, while real spaces and `ATTR_WIDE` continuation cells keep their band; pass 1's cursor block uses the
  SAME predicate.
- Band edges stay ABSOLUTE buffer pixels — correct across panes because panes never overlap in x.

**Config.**

- `key = value`, read once at startup, TWO files, each searched on its own:
  `-c FILE` / `-b FILE` > `$XDG_CONFIG_HOME/kai-term/` > `~/.config/kai-term/` > `/etc/kai-term/` — first
  location holding a REGULAR file with that name wins and the rest are never opened, and nothing found anywhere
  leaves `config_find()`'s `out` empty, which is how the callers know to keep the built-in defaults. The search
  is per FILE NAME, not per directory: a `kai-term.conf` in `~/.config` and no `kai-term.bind` there still picks
  up the system bindings. `kai-term.bind` is read AFTER `kai-term.conf`, so for a name both set the bind file
  wins. CLI flags are applied AFTER both files so they win and never write back.
- The split is by NAME, not by which file is open: `apply_bind_key()` takes the 24 `key_*`/`fm_key_*` names,
  `apply_setting()` the 29 others. A binding written in `kai-term.conf` is applied with a log line naming where
  it belongs now (files written before the split must not lose bindings silently); a setting written in
  `kai-term.bind` is refused by name, never treated as an unknown key. `tests/test_vt.c` reads both name lists
  out of the strcmp ladders in `src/config.c` and compares them with `config/kai-term.conf` +
  `config/kai-term.bind` AND with README's tables — a name added to the parser without a template line, a
  template line the parser does not know, and a deleted template line each fail a check.
- `#` starts a comment when ONLY whitespace precedes it, or when whitespace precedes AND follows it. The old
  rule (whitespace before is enough) was a bug: it cut `background = #1c1c22` at the `#`, and an empty value is
  legal, so a documented colour line set nothing and logged nothing. `working_dir = /home/me/my#project` keeps
  its value; `value # note` is stripped. Unknown keys log and are ignored; a missing file is not an error.
  Colours accept `#RRGGBB`, `0xRRGGBB`, bare `RRGGBB` and `AARRGGBB` (`parse_color()` skips an optional `#`/`0x`,
  reads hex, and adds `0xff000000` when 6 digits or fewer were given). Ranges are clamped at parse time.
- **One value owns the default ink and paper.** `Config.fg`/`Config.bg` reach the grid through
  `term_set_default_colors()` (file-scope `g_def_fg`/`g_def_bg` in `src/vt.c`, read by `term_default_fg()` /
  `term_default_bg()`), called from `main.c` after the config readers and before the first `term_init()`. That one
  variable is BOTH the colour every blank/erased/scrolled-in/reflowed cell is painted with AND the `cell_pristine()`
  sentinel meaning "never written" — they must stay one value, or reflow's `row_used()` counts trailing blanks as
  content and selection bands appear over nothing. `DEF_FG`/`DEF_BG` are only the built-in defaults `config_default()`
  starts from; nothing in `src/vt.c` may read them again. And nothing may paint a background from a different source:
  `glClear` uses `Config.bg`, pass 1 uses the cell's `bg`, so a third value anywhere draws a visible frame around the
  grid (s101). `palette[0]` is deliberately NOT the configured background — `SGR 40` / `48;5;0` stay black, as every
  other terminal does it; README says so, because it is the next thing a user will report.
- Defaults live in `config_default()` AND `DEF_*` macros AND the two files in `config/` — change one and the
  checks fail, which is the point. Do not hand-copy the name list into a test: derive it from the source.

## Debugging this project

- `./bin/kai-term -d` (log level 3) prints: font chosen + `color emoji: <path> (<n>px strike)`; GL version
  + instancing state; `shell starts in <dir>`; `grid resized to CxR (window WxH px, cell wxh, padding P,
  unused ux x uy px, tab i/n, tab view|side-by-side view, N panes)`; one
  `layout: tab view|side-by-side view, N pane(s), tab bar on|off, grid CxR per session` line per LAYOUT SHAPE
  change (client-side evidence for any bar/pane report; what `tests/cli_test.sh` greps for); session
  lifecycle (`mux: session 1/N started`, `tab i/n opened (pid P, pty fd F, grid CxR)`, `tab i/n visible`,
  `tab i closed, N left`, `the pty of tab i/n closed`, `the shell of tab i/n exited`,
  `the last session is gone, exiting`); bar clicks (`bar: click selects session i/n`,
  `bar: middle click closes session i/n`); `reflow: OLD -> NEW rows at N cols (K evicted)`;
  `compositor repeat_info: rate R/s delay D ms (kai-term uses …)`; `toplevel state: fullscreen F maximized M
  activated A (configure WxH px)` (only client-side evidence of entering fullscreen);
  `configure answered: buffer WxH attached (grid CxR)`; `zoom: font Npx, grid CxR`;
  `selection copied: N bytes` / `selection finished: ax,ay -> bx,by (copy_on_release off — Ctrl+Shift+C to
  copy)`; while a drag is live `sel press/drag cell=(col,row) frac=… ptr=(x,y) …` — how to check a
  selection-vs-pointer complaint without a screenshot, and it gives window's absolute position as
  `click_target - ptr`. A `swap failed (N in a row), retrying on the next frame` warning means
  `eglSwapBuffers` returned failure; a few are self-healing, a long streak means compositor is not showing
  this window.
- **Images.** `graphics: N image(s) drawn, M texture(s) held` is logged once per CHANGE in how many
  pictures are on screen (not per frame) — it is the client-side evidence that the renderer reached the
  image path at all; an accompanying `image glTexImage2D failed: 0x...` names the GL problem. Others:
  `graphics: dropped image N (W x H px) to stay within the budget`, `graphics: deleted N image(s)`,
  `graphics: unsupported payload (f=..., n bytes)`, `graphics: APC payload over ... bytes, discarded`,
  `graphics: file transmission (t=x) is not supported`. `./bin/kai-term -d -e 'timg -p k FILE.png; sleep 4'`
  exercises the whole chain and prints those lines. For the model alone, `.scratch/s70/timg_gate.c` runs
  the real timg in a PTY with Kai-Term's winsize, feeds its bytes through `term_parse()` and writes back
  the answers the model queues — no window, and it reports what timg chose (`--verbose` line) next to what
  the model holds. `timg`'s own `--verbose` output shows every query it sent and what it got back.
  The file-manager preview is counted separately and named — the line ends
  ` + the file-manager preview` when one is drawn, because it has its own texture (`Render.fm_tex`) and is
  not in the `M` count. `fm preview glTexImage2D failed: 0x... (WxH)` names a GL problem with it.
- **File manager.** `fm: opened on <dir>` / `fm: closed — the terminal is the whole window again`; a
  request `fm: listing <dir> (request N)` and its rejection `fm: dropping stale listing of <dir>
  (request X, newest Y)` (that pair is how you see the request-id rule working, and how a broken id shows
  up — s74's argument-order bug made every one of them read `request 0`); navigation
  `fm: open -> <dir>` / `fm: parent -> <dir>` / `fm: refresh -> <dir>`; the keyboard handover
  `fm: keyboard -> terminal|browser|preview`; a click `fm: click row N selects <name>`; what was typed at
  the prompt `fm: typed the selected path at the prompt: <path>`; the shell follow
  `fm: told the shell to follow: cd <dir>` (absent = the directory did not change, or the session is on the
  alternate screen, or `fm_follow = 0`); the clear -> `cd` -> re-populate sequence logs its steps —
  `fm: clearing the shell's input line to follow the browser into <dir>`, that `told the shell to follow`
  line, `fm: re-typing what was on the line: <text>`, and the completion `fm: the prompt now holds the
  browser's directory and what you had typed`. Anywhere it stops instead: `fm: dropped the follow while
  <erasing|cd|re-typing> (<why>)`, and when the captured text never made it back that line adds
  `— the shell's line does NOT hold what you had typed; it was: <text>` (the only way to recover it, so quote
  it in full in a report). A refusal to write at all is `fm: not telling the shell to follow — its input line
  is not one I can describe` or `fm: not typing the path — …`; a navigation arriving mid-sequence logs
  `fm: the follow already in flight now targets <dir>`; a failed read `fm: cannot open <dir>: <reason>`;
  `fm: worker started (result fd N)` — absent means the band was never opened, so no thread and no
  descriptor exist. The band's size is visible in the ordinary `grid resized to CxR …` line: with the band on
  at the default `fm_size = 75`, `R` is roughly a quarter of what it is with the band off (grid area 58 rows
  -> terminal 14 rows), and `layout: … grid CxR per session` is what `tests/cli_test.sh` compares between a
  band-off and a band-on run.
- `./bin/dbg` — scripted scroll/resize/reflow/zoom stages; each prints `scroll_off/live_off/rows/rcap/used`
  plus one 2-character token per slot (`v` viewport / `s` scrollback, then row's first cell), and asserts
  `row[]` is a NULL-free, duplicate-free permutation.
- `tests/cli_test.sh` drives real binary — `main()` is in no linked test, so it is the only honest coverage of
  argv and of what a real launch does with the config it loads. (The config SEARCH order itself is not exclusive
  to it — the `cfgsearch` group in `tests/test_vt.c` covers that headless.) It makes ONE probe launch
  (`-d -e true`) first: if that cannot draw, every
  window-opening check is skipped with the probe's reason printed. `KAI_TERM_TEST_NO_WINDOW=1` skips them
  without mapping any window — use it unless the window path is what is under test.
- Headless harness: link `src/vt.c` + `src/config.c` (+ `mux.c`, `layout.c`) into a throwaway program in
  `/home/kaida/Project/.scratch/` to exercise logic Wayland input events cannot reach.

## Key Decisions & Pitfalls

- **A guard that reads a field the change has not applied yet is a no-op that looks exactly like a guard.**
  `fm_set_size()` clamped `pv_scroll` to `fm_preview_scroll_max(fm)` on the line where it stored the REQUESTED
  pane size — but `fm_preview_scroll_max()` measures `fm->prev.rows`, the pane that actually EXISTS (the same field
  the painter reads), and the `realloc` that changes it runs 20 lines later. So the clamp measured the size the
  pane used to have and could never change anything; every check written as "scroll, resize, assert it is inside
  the new range" still passed, because the range it compared against was the old one. Moved after the allocation.
  Two traps made it easy to miss: (a) the natural test — shrink the pane, expect the scroll to shrink — is the
  direction where the range GROWS (40 lines in a 6-row pane is 36 rows of range, not less), so it pins nothing in
  either version; the clamp only bites when the pane GROWS. (b) The next check, `fm_preview_scroll(5)` expecting
  5, was red for an unrelated reason: the previous check had left `pv_scroll` at its maximum, so the call clamped.
  When a clamp/validate call takes a max from a getter, ask what state that getter reads and whether the change has
  landed in it yet (s96).

- **`eglSwapBuffers` blocks, and a blocked swap looks like a dead terminal.** It waits until driver gets a
  buffer back and only compositor releases buffers, so a client whose surface is not being rendered stalls
  inside swap: no keyboard, no PTY reads, no redraw (measured `wchan = futex_do_wait`;
  `eglSwapInterval(0)` changes nothing on this NVIDIA driver). The rule is about wayland READ — earlier
  "never swap inside a listener" wording was BACKWARDS and caused a regression. Answering EVERY configure with
  a swap is also wrong (it spammed `eglSwapBuffers failed: 0x3003` every frame).
- **Geometry lives in `src/layout.c`, not main.c.** main.c is in no test binary, so arithmetic there is
  untestable, and N panes need N origins, which one global offset cannot express. The round trip
  `layout_initial_size()` -> `layout_compute()` -> `layout_grid_size()` is what proves startup height is
  right — it caught `cell_w`/`cell_h` typo (s62). Same reasoning put `mouse_sgr_button()` in vt.c.
- **The evdev button order is not protocol order.** `BTN_LEFT` 0x110, `BTN_RIGHT` 0x111,
  `BTN_MIDDLE` 0x112, but SGR numbers them 0 left, 1 middle, 2 right. A switch written in kernel order
  silently swaps RIGHT and MIDDLE — in bar's middle-click-close AND in every SGR report to vim/htop.
- **A log line that prints value it was GIVEN can lie.** `tab_switch(idx)` logged `mux: tab idx/n visible`
  while nothing had changed, so log looked like proof click worked and bug survived every reading
  of it. Log what owner of state REPORTS (`mux_active()`).
- **`Term` is not a place for settings.** `ESC c`/RIS calls `term_init`, which memsets whole `Term`, so
  process-wide settings are file-scope values with a setter (`kai_term_loglevel`, `g_clear_scrollback`) applied
  from config in main.c. `bracketed_paste` is deliberately opposite: it is application-negotiated, so a
  reset SHOULD clear it.
- **A Kai-Term-to-Kai-Term clipboard test proves nothing** — it works even if client never shares its
  selection. Test against a foreign client (`wl-copy` in, `wl-paste` out). With `copy_on_release = 0` (the
  default) a mouse drag does NOT call `set_selection`; only `Ctrl`+`Shift`+`C` does — that is what reads like
  "Kai-Term keeps a private clipboard".
- **A sanitizer target with an incomplete prerequisite list produces confident, wrong results.** `bin/dbg_asan`
  once kept running a stale binary and reported a 312-byte leak current source did not have. When a
  sanitizer finding contradicts tests, check binary's mtime against sources first.
- **A `(void)x;` line does not mean `x` is unused.** `(void)def1;` in `csi_dispatch` read like a leftover, but
  `def1` is used by `CSI J`, `CSI K` and `CSI 6 n`. Grep for uses, then let compiler confirm.
- **Never pass an out-parameter and the call that fills it as arguments of the same call.**
  `fm_nav(fm_go_sel(&g_fm, &seq), seq, "open")` compiles clean under the whole `WARN` set and is broken:
  C evaluates arguments in an unspecified order and gcc reads the caller's `seq = 0` BEFORE `fm_go_sel()`
  writes it. Measured live (s74): every listing issued after a navigation carried request id 0, so
  `fm_install_scan()` discarded all of them and the browser kept showing the directory the user had left.
  Fill the out-param in its own statement. Reproduce with `.scratch/s74/argorder.c` (three lines).
- **One int return value cannot mean "not handled" and "handled, do not repeat" at the same time.**
  `send_key()`'s return says only "is this repeatable", and falling off the function is what sends the key
  to the PTY. `if (fm_keys(...)) return 0;` therefore delivered every one-shot file-manager key to the
  shell too: Return on a file typed its path AND a newline (the shell would have run it), BackSpace went to
  the parent AND deleted a character at the prompt. Three states (-1 / 0 / 1) fixed it. Symptom to look for:
  the action happens AND its key appears in the terminal.
- **A log line that counts two different things as one number lies the same way one that prints its
  argument does.** `graphics: N image(s) drawn, M texture(s) held` began counting the file-manager preview
  in `N` while `M` still counted only the application-picture texture cache, so a preview alone logged
  `1 image(s) drawn, 0 texture(s) held`. Count each thing with the counter that owns it (s74).
- **A refused texture upload is a decision, not a log line.** Every `glTexImage2D` site used to print its
  error and draw with the texture anyway; on the NVIDIA driver a draw that samples a texture with no
  storage faults inside `libnvidia-eglcore` (SIGSEGV at 0x4 in `draw_quads`, 85 core dumps in one suite
  run, s79/s81). The rule now: the glyph atlas is worth exiting over (nothing can be drawn without it —
  `render_draw()` returns -1 and `main()` exits 1), the color atlas is worth downgrading to mono glyphs, a
  picture is worth skipping. Retrying an upload is fine; drawing the result is not.
- **A steady-state condition is not evidence of an event.** `else if (prompt_row_matches(t))
  t->input_pending = 0;` read as "the shell put the prompt back, so the line is empty" — but it was also true
  for a burst that had changed NOTHING, e.g. fish repainting only OSC 7 / OSC 0 / DECSET after Kai-Term had
  re-typed the user's text. The echo window closed over bytes that had not been echoed yet, every later burst
  re-took the prompt snapshot (including the one carrying the echo of Kai-Term's own typing), and a row
  holding `Some text` began answering `term_at_prompt()` as EMPTY — so the next file-manager navigation
  appended `cd '<dir>'` to the command and the shell ran it (user report, 2026-10-19). Snapshot the condition
  at the START of the burst and act only on the transition. Same class as "a log line that prints its own
  argument can lie": a predicate that is true before and after an event cannot report that event.
- **A burst boundary is not a semantic boundary.** Advancing a "wait for the shell's next prompt" state at the
  end of any pty read looks reasonable and is wrong: fish answers a `cd` with the command echo, then a
  continuation row, then prompt chrome, then the prompt — four separate reads, and the model re-takes the
  prompt snapshot at the end of each. The state machine left that step on a BLANK row, typed the user's text
  there and could never confirm it. Where the evidence is "the application has FINISHED printing", wait for
  silence (`FMCD_QUIET_MS`, driven from the frame timer) — no further burst is coming to trigger it.
- **A harness that counts bytes by hand sends whatever follows the literal in memory.** `input_write(t, p,
  "PS1='%~> '\n", 13)` on an 11-byte literal also wrote the NUL and the next byte of `.rodata` — which happened to
  be the `S` of the following string literal, so the zsh prompt appeared to hold `SSome text` and the terminal
  looked like it was duplicating a keystroke. It was the probe. Always `strlen()` (or `sizeof(x)-1`) for a literal,
  and when a harness reports a character nobody typed, suspect the harness first (s94).
- **Never run the live key-injection or window-mapping harnesses while the user is at the machine, and never
  assume a red screen came from your own test.** The user was disturbed mid-session (2026-10-19). Measured
  after the fact rather than guessed: no `bin/kai-term` of mine was alive, no `.scratch/inject` process, no
  new core dump, and the CLI suite had mapped 0 windows (`KAI_TERM_TEST_NO_WINDOW=1`, 10 SKIP lines) — what
  actually happened was `noctalia` (the lock screen) SIGSEGVing and niri restarting the session, a recurring
  host fault also present at 17:43, 18:06 and 20:59. Default to headless coverage (`.scratch/s86/follow2.c`
  drives the real machine over the real pty with no window at all) and hand the in-window check to the user.
- **A red check can mean the test's setup is unrepresentative, not that the model is wrong.**
  `tests/test_vt.c:2410/:2416` looked like "the prompt snapshot breaks after a scroll" and would have
  invited a change to correct snapshot logic. `.scratch/s82/promptprobe.c` replayed the sequence and printed
  the state at each step: the test wrote the whole line with `feed()` (application output), and
  `term_parse()` re-takes the snapshot at the end of every such burst, so the typed text legitimately was
  `""`. The realistic sequence (`feed("p ")` then `type(...)`) passes at the same scroll position. Print the
  state before editing the logic a check exercises (s82).
- **A test that maps a window is a change to the user's desktop.** `tests/cli_test.sh` ran the real binary
  15 times per `make test`, and s79 looped it 25 more times to sample a failure — that is what filled the
  user's screen and `coredumpctl`. One probe launch decides whether the window checks run; the rest are
  skipped with the reason printed, and `KAI_TERM_TEST_NO_WINDOW=1` skips them all. Never loop launches to
  count failures: diagnose the first one (s81, and ENVIRONMENT.md "Launching real windows in a test").
- **An invariant held by accident is invisible until something else changes.** "A cursor moved off the prompt
  column means the line is not empty" was true for months because EVERY key Kai-Term sent set
  `input_pending`, and that is what stopped `prompt_snapshot()` from re-taking the anchor at the moved cursor.
  Making movement keys stop opening the echo window (the s89 BackSpace fix) turned the pinned check red and
  would otherwise have made a cursor sitting INSIDE the prompt text answer "safe to write here". When a change
  flips a check that has been green for ages, find the mechanism that was really holding the property —
  `.scratch/s89/leftprobe{,_old}` prints `at/pending/cx/cy/prompt_cx/prompt_cy` per step and shows it in one
  run — and state it explicitly instead of restoring the accident.
- **Never write a check around an escape sequence without measuring what THIS parser does with it.** Assumed
  "Home"/"End", got three realities (`.scratch/s89_ss3.c`): `CSI H` moves the cursor to another ROW (not a
  Home key), `ESC O H` prints a literal `H` (SS3 is not implemented here), `ESC [ 1 ~` is ignored, and `CSI G`
  is the one that moves within the row. Same class of mistake in the probe itself: `"\\e[H"` in C sends a
  backslash and an `e`, which read for two minutes like a parser bug.
- **When a bug is in what someone ELSE hands you, measure that someone before theorising about your own code.**
  The stray-`h` report got two confident wrong explanations (a leaked parser byte, then an escape sequence in the
  pasted text) before one command pair settled it: `wl-paste -l` and `wl-paste --type <name> | od -c`. Both wrong
  theories were internally consistent and neither had been tested against the actual clipboard. The user's own clues
  — "the source matters", "ctrl+v works but ctrl+shift+v does not" — pointed straight at it; a report that
  distinguishes two paths is telling you which path to instrument. Related: the s97 C0-inside-CSI fix was a real bug
  found while chasing this one, so "I found and fixed something" is not evidence that the reported bug is explained.
  Confirm the reported symptom is reproduced, then confirm it is gone.
- **A failed build leaves the previous binary in place, and it will happily pass.** `make -j8` errored on a C
  mistake, the very next command ran `./bin/test_vt` and it printed `859 checks, 0 failures` — for code that had
  never been rebuilt. Check the build exit status (and that the check count actually moved) before believing any
  green run.
- **`pkill -f <name>` matches the shell that runs it.** `pkill -f clipowner` killed the whole command line, output
  and all. Use `pkill -x <name>`, or put the sequence in a script file.
- **A background job spawned with `&` makes this shell tool swallow the call's entire output** — even the `echo`s
  that follow it. Split "start the daemon" and "query it" into separate calls, or run the whole scenario from a
  script file.
- **`cat > file` is fully buffered when stdout is a file**, so killing the `cat` that captures a paste discards the
  capture and the run reports 0 bytes. Close its stdin (Ctrl+D) and let it exit normally.
- **Never hand-count a length into `term_parse()` — use `strlen()`.** `.scratch/s97/csiC0.c` wrote
  `term_parse(&T, "\e[?2004", 8)`: that literal is **7** bytes (ESC `[` `?` `2` `0` `0` `4`), so the hand-counted
  8 fed the NUL terminator as an extra byte. The probe then reported a perfectly good case as broken — a CSI
  sequence split across two pty reads came back as "mode not applied, an `h` painted". `strlen()` in v4
  (`.scratch/s97/hprobe2.c`) showed that case was never broken. Same shape as every other entry here: the check
  invented the bug, and the A/B against the real code is what caught it.
- **A probe that hosts a shell must answer that shell's queries, or it is measuring a shell that never started.**
  `.scratch/s97/hprobe2` section B reported "fish: bracketed paste NEVER within 3 s". `.scratch/s97/fishdiag`
  shows why that was the probe: fish's first 83 bytes are terminal QUERIES (`CSI ? u`, `CSI > 0 q`, `OSC 11 ; ?`,
  `DCS + q …`, `CSI 0 c`) and fish paints nothing until they are answered or time out. `.scratch/s97/fishtime`
  drains the response queue the way `src/main.c` does and measures 200 ms. Any headless measurement of shell
  STARTUP behaviour that never writes back to the pty is measuring a stalled shell, not a slow one.
- **A check that passes on the buggy code pins nothing.** `ink_in_box()` — "no glyph may sit inside the box the
  picture is drawn into" — passed against the pre-fix `src/fm.c` when it was written around a small centred
  8x8 preview: that picture happened to land below the metadata. The overlap needs a picture whose box starts
  at the TOP of the picture area (one big enough to fill it), and only the A/B — the new test file against
  `git show HEAD:src/fm.c` — showed that. When a check is written to pin a bug, run it against the code that
  had the bug and report the failure count (s88: 8 failures there, 0 here).
- **A harness that types into the user's real shell history pollutes its own later runs.** The s95 path gate ran
  real fish/bash/zsh in a pty, so its commands landed in `~/.config/fish/fish_history` and `~/.bash_history`.
  Two separate false greens followed: a later run's autosuggestion ghost was a command an EARLIER run had typed
  (the row check passed on the machine that typed nothing), and in ghost mode Enter printed the expected output
  because fish's own suggestion completed the command. Run shells with their history redirected (`fish --private`,
  `HISTFILE=/tmp/...`) and make the needle something no history can already contain (a pid in the file name).
  Same class as every other "the harness caused the symptom" entry: when a check passes on the code you expect
  it to fail, suspect the check before you believe the result (s95).
- **"The parser knows this key" is never evidence that the key does anything.** `cursor_color =` is defaulted
  (`src/config.c:44`), parsed (`src/config.c:463-464`) and stored in `Config`, and no line of code anywhere reads it —
  `src/render.c` draws the cursor from the `CURSOR_COL` macro and `render_init()` is handed only `bg`/`fg`. Every test
  naming it asserts on the PARSED value, and the template parity check compares parsed values field by field, so the
  whole suite is green while the feature is absent. A config key is only implemented at its far end: trace the value
  from `apply_setting()` to the pixel that reads it (the s101 border bug was the same shape — a configured colour that
  reached `glClear` but not the cells). When adding a key, write the check at the consumer, not at the parser.
