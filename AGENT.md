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

## Current Status (re-measured 2026-10-20, s89 — build and every suite green; the file manager's `cd` follow survives a BackSpace on an empty prompt (user-confirmed in their window), and the prompt snapshot's cursor anchor is now an explicit rule instead of an accident)

- **s89's follow fix is committed and pushed; the tree is clean.** `payload_cannot_insert()` + the anchor rule
  in `prompt_snapshot()`, both in `src/vt.c`, with 20 new checks; the user confirmed the behaviour in their own
  window (2026-10-20). Read the remote state with `git status -sb` / `git log --oneline origin/main -1`.
- `make -j8` exit 0, **0 warnings** under `WARN` (`-Wall -Wextra -Wshadow -Wwrite-strings
  -Wformat=2 -Wdiscarded-qualifiers`). Headless suites: `./bin/test_vt` **746 checks, 0 failures**,
  `./bin/dbg` **MODEL OK (0 problems)**, `./bin/test_fm` **247 checks, 0 failures**;
  `KAI_TERM_TEST_NO_WINDOW=1 make test` exit 0 (`CLI: 62 checks, 0 failures, 10 skipped` + those three);
  `make asan` exit 0 with the same counts and 0 sanitizer reports.
- **The follow is user-confirmed in their own window (2026-10-19): "I confirm it works"** — after the s86
  fix, running `./bin/kai-term -d` with fish. It is also proven headless: `.scratch/s86/follow2.c` runs the
  machine from `src/main.c` over the real `src/pty.c` + real `term_parse()`; two navigations on top of each
  other both end with `the prompt now holds the browser's directory and what you had typed` and the line holds
  `Some text ` — not the `Some text cd /etc` the user had reported. `g.log` = the fish-autosuggestion case now
  follows; `m.log` = the mid-line-cursor case fails safe (no `cd` ever sent, the captured text logged).
  The live key-injection harness was NOT re-run: the user is at this machine and asked not to be disturbed
  (s86, and the pitfall in Key Decisions) — the confirmation came from them running it, which is the better
  evidence anyway.
- `git status -sb` -> `main` == `origin/main`, nothing uncommitted except this journal while a docs session
  is in progress. Read the remote state with `git status -sb` / `git log --oneline origin/main -1`, never from
  a hash written here. The follow lives in `src/main.c` (the state machine + the quiet-period step),
  `src/vt.c` (the echo window needs a transition; a height-only resize keeps a dirty prompt snapshot),
  `include/kai-term/kai-term.h` (`term_prompt_line*`), `tests/test_vt.c`.
- The file manager IS reachable from the UI (`Ctrl+Shift+F`) and its band is drawn. Its input routing
  (toggle, band keys, focus handover, `cd` follow, close/reopen) was verified live by injecting keys through
  niri and reading the bytes the PTY received — three real defects found and fixed that way (s74). The user
  has now run the band in their own window and confirmed the behaviour (2026-10-19). **No explicit verdict on
  how it LOOKS has been given** — if they ever report something visual, start from the `fm:` log lines, not
  from the shader.
- **s88 changed how big that band is and what its preview may paint, and no human eye has seen it yet.**
  `fm_rows` (a row count) is gone from the whole tree; `fm_size` (percent of the grid area, default **75**,
  clamped 10–95 in config.c AND layout.c) decides the share, so the band keeps its proportion through a
  resize and a zoom. And the preview pane's rows now have ONE owner (`preview_head_rows()` +
  `preview_meta_rows()` in fm.c, used by both `paint_preview()` and `fm_image_fit()`), which is what stops
  the metadata lines being drawn across the picture — the user's "the metadata sometimes get written on the
  image preview". Both are pinned by checks (726 + 247, 0 failures) and the preview one by an A/B against the
  previous `src/fm.c` (8 failures there). Nothing here has been looked at in the user's window.
- **Image support (kitty graphics protocol) is in the tree and user-confirmed by eye** ("The image display
  properly. The feature works as expected", 2026-10-17, session 70, `timg -p k` with a PNG): APC/DCS parser
  states, chunk assembly, base64 + zlib + PNG (libpng simplified API) + raw f=24/32 decode, premultiplied
  pixels held in the model, images anchored to absolute grid rows, winsize pixel geometry +
  `CSI 14 t`/`CSI 16 t`, and a renderer that draws each picture between the background batch and the text
  batch. Also verified headless (real timg bytes through the real parser) and by log line
  (`graphics: 1 image(s) drawn, 1 texture(s) held`, no GL warning).
- No probe/experiment left in tree: `grep -rnE "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/ include/ tests/` ->
  empty (the s85 RETYPING probe was removed in s86).
- README's flag table (`-e -w -c -d -v -h`) and its **49** settings rows match `src/main.c` / `src/config.c`
  exactly — re-checked by set difference, not by eye, this session: the 49 keys `apply()` in `src/config.c`
  compares (`!strcmp(key, …)`, `font` included) and the 49 rows of its "Available settings" table are the same
  set, `diff` empty both ways. README also carries the "File manager" usage section, the 20 file-manager
  settings rows, the shortcut tables and libjpeg + libwebp in Requirements.
- **No open issues.** The 4 reports that used to be in ISSUES.md were unproven (never reproduced here) and
  were dropped entirely on 2026-10-17 — they are not in "Fixed" either, since no fix was ever verified.
  Recoverable with `git show d5afe4a:ISSUES.md`; recurrence -> file a new Open item with a reproduction.
- **User-confirmed by eye in their window:** multiplexing + tab bar + side-by-side ("works as expected and
  looks great", 2026-10-17); image display via `timg -p k` ("the image display properly. The feature works
  as expected", 2026-10-17); zoom (no stale prompt lines), padding, fullscreen refresh, black-window/no-input
  fix, heavy-output segfault fix, transparency, bracketed paste, `clear` drops scrollback, shell start dir,
  color emoji, text selection, `app_id`, several instances at once, window resize, key-hold repeat, clipboard
  interop both directions.
- Verified at model+live level (not all re-checked by hand): reflow on width change, color emoji + grapheme
  clusters, whole-glyph selection with no band over unwritten space, compositor transparency, CSI 3 J +
  startup dir, client-side key repeat, draw/commit + event-loop pacing.
- Git: `origin = https://github.com/K-code-AI/Kai-Term`, branch `main`. Never write a hash or an
  ahead/behind count in this file — the commit recording it makes it wrong. Read the truth with
  `git status -sb` and `git log --oneline origin/main -1`. `git fetch` before push — the user commits on
  GitHub too. Never commit `bin/` or `build/`.

## Current Task (s89 — CLOSED: a BackSpace on an empty prompt no longer kills the file manager's `cd` follow, and the prompt snapshot's cursor anchor is an explicit rule; committed and pushed)

**User report (2026-10-20):** after pressing BackSpace at an empty prompt, browsing into a directory stopped
moving the shell (`fm: dropped the follow while erasing (the wait ran out)`) until Enter was pressed.

**What was found and done** (all of it measured, the step-by-step log is `s89 entry 1` below):
- The session resumed with `src/vt.c` + `tests/test_vt.c` modified and **no journal entry at all** — the
  interrupted session had recorded nothing. Its intent was recoverable only from the code comments and
  `.scratch/s89/`. Lesson: the journal is what makes an interrupted session resumable; a modified file with no
  entry costs the next session a full re-derivation.
- The cure (`payload_cannot_insert()` + a `harmless && clean_before` branch in `term_note_input()`) was already
  written and already cured the bug in the harness; what was left was the one check it turned RED.
- The red check was a real invariant, not a test to relax: the snapshot's cursor anchor had been kept still by
  accident. Fixed in `prompt_snapshot()` (see invariant 4) rather than by narrowing the whitelist, which would
  have left Left/Right/Home/End broken for the user.
- Verified: 746 / MODEL OK / 247 checks, 0 failures; `KAI_TERM_TEST_NO_WINDOW=1 make test` exit 0
  (`CLI: 62 checks, 0 failures, 10 skipped`); `make asan` exit 0, 0 sanitizer reports; the A/B harnesses
  re-run against the final code (2 drops -> 2 fast paths; the s86 text-preserving path and the documented
  mid-cursor refusal unchanged).

**Owed to the user, not to the code:** nothing for this fix — they confirmed it in their own window.

### s89 entry 1 — the red check is green: the prompt snapshot's CURSOR ANCHOR is now honest (739 -> 746 checks)

**What was red:** `tests/test_vt.c:2245 prompt: a cursor moved off the prompt position does not count as
empty`. Cause, measured with `.scratch/s89/leftprobe{,_old}` (prints `at/pending/cx/cy/prompt_cx/prompt_cy`
per step): the new input whitelist keeps `input_pending` at 0 for a movement key, so the end-of-burst
`prompt_snapshot()` ran and re-took the snapshot **including `prompt_cx`** (8 -> 7). `prompt_row_matches()`
then compared the cursor with its own fresh anchor and answered "safe to write" with the cursor sitting
INSIDE the remembered prompt text, and `term_prompt_line()` would have described the line from the wrong
column. The old code held that invariant only by accident — `input_pending` was what kept the snapshot still.

**Chosen fix (not the whitelist narrowing):** `src/vt.c` — split `prompt_row_matches()` into
`prompt_row_same_cells()` (the row is cell-for-cell the remembered prompt, cursor says nothing) plus the
cursor test, and have `prompt_snapshot()` RETURN WITHOUT TOUCHING ANYTHING when `prompt_row_same_cells()` is
true. Meaning stated in code: `prompt_cx`/`prompt_cy` are where the cursor sits **when the line is EMPTY** —
the anchor `term_prompt_line()` measures typed text from and the column `term_at_prompt()` insists on. A
burst that rewrote the prompt (different text, other colours, a moved prompt row) still re-takes the whole
snapshot, anchor included.

**New checks (739 -> 746, all in the `term_at_prompt()` group):** moving the cursor within the prompt row
leaves the anchor alone and `term_at_prompt()` says no; putting it back says yes; a genuinely new prompt
re-takes the anchor at its own column; a redrawn shorter prompt re-takes it too. **A probe caught two wrong
assumptions in my first version of these checks** (`.scratch/s89_ss3.c`): `CSI H` moves the cursor to
another ROW (it is not a Home key), and `ESC O H` / `ESC [ 1 ~` are not cursor keys in this parser at all —
`ESC O H` prints a literal `H`. The checks now use `CSI G`, which moves within the row. Do not write a
prompt check that leans on SS3 or on `CSI H`.

**Measured, this session, after the change:**
- `make -j8` exit 0, 0 warning/error lines; `./bin/test_vt` **746 checks, 0 failures**; `./bin/dbg` **MODEL OK
  (0 problems)**; `./bin/test_fm` **247 checks, 0 failures**;
  `KAI_TERM_TEST_NO_WINDOW=1 make test` exit 0 (`CLI: 62 checks, 0 failures, 10 skipped` + those three);
  `make asan` exit 0, same three counts, 0 sanitizer reports.
- **A/B of the user's report, re-run against the final code** (`.scratch/s89/follow3{,_old}`, real fish over
  the real pty, no window): BackSpace on an empty prompt then two navigations — OLD: `captured the line: []`
  + `DROPPED the follow while erasing (the wait ran out)` twice. NEW: `FAST PATH (term_at_prompt says the line
  is EMPTY)` + `told the shell to follow: cd …` twice, and the line holds `[]`.
- **No regression in the s86 behaviour:** with `Some text ` on the line, both navigations end
  `DONE — the prompt holds the dir and what you had typed` and the line holds `Some text `.
- **The documented mid-command-cursor limitation is unchanged** (Left×2 with `Some text `): `captured the
  line: [Some tex] -> DROPPED the follow while erasing … — LOST the text`, identical in OLD and NEW.

**Next action:** docs (ISSUES.md `Fixed` entry + its follow limitation, which still says Enter/Ctrl+C is the
only re-arm; README's follow paragraph if needed; this journal's invariants + pitfalls), then commit this as
one verified change and push.

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

## Next Steps

1. **File manager: the open item is the user's eye on the new 75% band and on the fixed preview.** Built,
   wired, documented, green (247 model/layout/config checks + the s74 live key-injection run that proves the
   routing and the `cd` follow), and the user has run the band in their own window and confirmed its
   behaviour (2026-10-19) — but the band's new SHARE (`fm_size = 75`, s88) and its new preview row budget
   have never been seen by them, and they have never said whether they like how the band looks. If they
   report something visual, start from the `-d` lines `fm: opened on …`, `fm: listing <dir> (request N)`,
   `fm: dropping stale …`, `fm: keyboard -> …` and `graphics: … + the file-manager preview`, not from the
   shader. Two decisions they may want to revisit, both flagged, both one-line changes with different safety
   implications: `Return` on a file types its path instead of running it, and `fm_follow` sends `cd <path>`
   unprompted.
2. **Image support is done and confirmed by eye** (2026-10-17), and the user has settled its scope:
   "The goal was image support, Not auto-detection by timg so it work fine using `-p k`". So `-p k` IS the
   supported way to show a picture and nothing more is wanted here. Do NOT "improve" this by re-adding the
   graphics bits (4/16/18) to the DA1 answer or by inventing a `TERM` value: measured A/B (s70), advertising
   sixel without implementing it makes the picture vanish entirely, and timg's auto-detect list is hardcoded
   in its binary. Sixel/iTerm2 support would be a NEW feature — only on an explicit request.
3. **The prompt follow is implemented, idempotent, green and confirmed by the user in their own window (s86)
   — nothing is owed here but a diagnosis if it ever breaks.** The recipe, if a report arrives: run
   `./bin/kai-term -d`, type a partial command, open the band with `Ctrl+Shift+F`, navigate TWICE — each
   navigation must end with `fm: the prompt now holds the browser's directory and what you had typed`, with no
   `dropped the follow …` line, and the prompt must hold the text once, not `text cd <dir>`. Then diagnose
   headless (`.scratch/s86/follow2.c` drives the real machine over the real pty, no window) against the three
   predicates under "The prompt invariant" in Architecture & Invariants, before touching the model.
4. Housekeeping, none a bug:
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
5. Clipboard hardening niri does NOT require (do not do unasked — untested against a compositor that needs
   it): `wl_data_offer_accept()` before `wl_data_offer_receive()`; offer `UTF8_STRING` +
   `text/plain;charset=utf-8` next to `text/plain`.
6. Emoji paths that exist but have NEVER run here: SCALABLE color font branch (`FT_IS_SCALABLE` ->
   `FT_Set_Pixel_Sizes` + `hb_ft_font_changed` on zoom) — this box has only a bitmap-only Noto CBDT font; and a
   cluster HarfBuzz renders as >1 glyph, deliberately rejected (`ng == 1`), falls back to mono.
7. Scratch cleanup: throwaway probes live in `/home/kaida/Project/.scratch/` (outside project). Worth keeping
   as regression harnesses: `s70/timg_gate.c` (end-to-end image), `s74/inj_keys.sh` (the file-manager input
   routing gate — the only coverage `make test` does not have), `s86/follow2.c` (the prompt follow over a real
   pty), `s45_old/` + `s47_before/` (A/B trees for a suspected draw-path regression). `s61/` +
   `s61_apinject*` and the one-off probes of closed sessions need no longer be kept. Ask before deleting
   anything outside the repo — not done.
8. Git: keep committing verified work; `git fetch` before pushing.

## Project Files & Facts

```
Kai-Term/
├── .gitignore                   # bin/, build/, *.o/*.obj/*.d/*.so/*.a, editor leftovers, docs/ARCHITECTURE.md
├── LICENSE                      # MIT, "Copyright (c) 2026 Kaida"
├── AGENT.md                     # this journal
├── ISSUES.md                    # open items (none) + Fixed + known limitations
├── README.md                    # features, deps, build, flags, config keys, shortcuts, exit status
├── Makefile                     # src/*.c -> build/*.o -> bin/kai-term; all/test/test-cli/asan/run/clean
├── include/kai-term/kai-term.h  # the single shared header: all types, prototypes, log levels, helpers
├── src/                         # config.c vt.c img.c mux.c layout.c fm.c pty.c font.c render.c main.c
├── tests/test_vt.c              # 726 checks, 0 failures (parser, grid, graphics, config, prompt machinery)
├── tests/test_fm.c              # 247 checks: the file-manager model, previews, decoders, band geometry
├── tests/dbg.c                  # grid-model invariant checker (permutation + scroll_off/live_off bounds)
├── tests/cli_test.sh            # 95 checks driving the REAL bin/kai-term; ONE probe launch gates the
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
  (or `-e COMMAND`); sets `TERM`/`COLORTERM`/`TERM_PROGRAM`; `pty_setsize`.
- `src/config.c` — `key = value` config file, defaults, clamping, key-binding spec parser
  (`keybind_parse`/`keymods_parse`).
- `src/mux.c` — sessions of one window: dense `Tab` array (`{Term, Pty, attention}`), active index,
  open/close/goto/next/prev, `max_tabs`, unread-output flag, `mux_title()`. No Wayland/EGL/fd/fork — that is
  what makes it unit-testable.
- `src/layout.c` — pure geometry: in window px + cell px + padding + gap + mode + session list; out bar strip,
  `BarItem[]` rects + labels, `PaneGeom[]` rects (each with cols/rows), the file-manager band (`FmGeom`:
  browser rect + preview rect + their cols/rows) and `layout_hit_fm()`, `layout_grid_size()`,
  `layout_initial_size()`.
- `tests/test_vt.c` — 726 checks: parser, grid, selection, alt screen, `live_off`, emoji clusters, CSI 3 J,
  bracketed paste, `term_init` guard, reflow, `term_grid_geometry`, zoom/alt-screen row-shrink,
  `mouse_sgr_button`, the graphics path (base64/chunking/PNG/zlib/raw/premultiply/placement/delete/scroll
  anchoring/id replacement/DA1-DA2-DSR answers), mux, keybind/keymods parsing, multiplexing config keys,
  `layout_compute` / `layout_grid_size` / `layout_initial_size` (the tab bar, the side-by-side panes and the
  file-manager band's SHARE: `fm_size` percent, both clamps, the terminal's floor, the share surviving a
  resize and a zoom, and three startup round trips band on/off/bar), and the prompt machinery the file
  manager's follow is built from (`term_at_prompt()` / `term_prompt_line()` / `term_prompt_line_typed()` /
  `term_prompt_line_is()`, the echo window and its transition rule, the clear -> `cd` -> re-populate sequence
  as the model sees it). `main.c` itself is in no test binary — the machine is covered by
  `.scratch/s86/follow2.c` and the live harness, not here. 2 invariants with dedicated checkers are `row[]`
  permutation (also `tests/dbg.c`) and startup-size round trip.
- `tests/test_fm.c` — 247 checks against a real temporary directory tree (dirs, a dotfile, a dangling
  symlink, a FIFO, an unreadable dir, four image formats, a binary file): scan + sort + caps + truncation,
  the hidden filter and `fm_set_hidden`, selection and scrolling (`fm_row_pos`, `ensure_visible`),
  navigation, the request-id rule (a stale answer is dropped), every preview kind and its error path, the
  highlighter reaching the cells, the pane painters, `fm_image_fit`, the preview's row budget (no painted
  glyph may sit inside the box the picture is drawn into — `ink_in_box()`), and the band's rects inside
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

- `key = value`, read once at startup: `-c FILE` > `$XDG_CONFIG_HOME/kai-term/kai-term.conf` >
  `~/.config/kai-term/kai-term.conf`; CLI flags applied AFTER file so they win and never write back.
- `#` starts a comment only at line start or after a space/tab -> a value may contain `#`. Unknown keys log and
  are ignored; a missing file is not an error. Colours accept `#RRGGBB`, `0xRRGGBB`, `AARRGGBB`. Ranges are
  clamped at parse time.
- Defaults live in `config_default()` AND `DEF_*` macros — change both or README drifts.

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
  argv + config keys. It makes ONE probe launch (`-d -e true`) first: if that cannot draw, every
  window-opening check is skipped with the probe's reason printed. `KAI_TERM_TEST_NO_WINDOW=1` skips them
  without mapping any window — use it unless the window path is what is under test.
- Headless harness: link `src/vt.c` + `src/config.c` (+ `mux.c`, `layout.c`) into a throwaway program in
  `/home/kaida/Project/.scratch/` to exercise logic Wayland input events cannot reach.

## Key Decisions & Pitfalls

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
- **A check that passes on the buggy code pins nothing.** `ink_in_box()` — "no glyph may sit inside the box the
  picture is drawn into" — passed against the pre-fix `src/fm.c` when it was written around a small centred
  8x8 preview: that picture happened to land below the metadata. The overlap needs a picture whose box starts
  at the TOP of the picture area (one big enough to fill it), and only the A/B — the new test file against
  `git show HEAD:src/fm.c` — showed that. When a check is written to pin a bug, run it against the code that
  had the bug and report the failure count (s88: 8 failures there, 0 here).
