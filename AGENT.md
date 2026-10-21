# Kai-Term — Agent Journal

Read this + ISSUES.md before touching the project. User-facing docs: README.md. Current problems: ISSUES.md
"Open". Root causes of long-fixed bugs: git history, not this file — `git show d5afe4a:ISSUES.md`.
Host/tool facts: `/home/kaida/Project/ENVIRONMENT.md` (outside the repo on purpose — not project documentation).

**The user always tests with the freshly built binary from `/home/kaida/Project/Kai-Term/bin/kai-term`.** When they
report that a feature does not work, that binary and that source tree are what they mean. Never suggest they are
running an older or installed copy, never go looking at an installed binary or a launcher for it, and never touch
their `~/.config/kai-term/desktop-terminal.conf`, its `.desktop` entry or anything under `/usr/local/bin` — that is
their own setup and off-limits. A report is a bug in THIS tree until a measurement in THIS tree says otherwise.

Renamed 2026-10-16 (s59): `K-Term`/`k-term` -> `Kai-Term`/`kai-term` (dir, header, config path, app id). Old
entries and git history keep the old spelling — they record what was really run.

**What this file is.** A journal plus the rules a change must not break — not a changelog. Per-session
narrative collapses into era entries and the step-by-step detail (every failed probe, wrong theory, source line
number and superseded check count) goes to git history; that is the rule ISSUES.md has followed since s66,
applied to this file in s63, s65, s87, s100, s106 and s119. What stays is what constrains a change: the status,
the plan, the invariants, the diagnosis reference and the pitfalls. What goes is session narrative.
Dates in older entries run ahead of the host clock — **git log is the authority for order, not those dates.**

## Project Overview

Wayland terminal emulator in C. xdg-shell window + PTY shell + VT/ANSI parser + EGL/OpenGL text rendering,
2 glyph atlases (alpha8 mono, RGBA8 color emoji) plus one GL texture per application picture (kitty graphics
protocol), two vertex batches per frame with the pictures drawn between them. In-window multiplexing: N
sessions per window, tab bar, side-by-side panes. No GUI framework.

## Current Status (re-measured 2026-10-21, s135 — **GREEN; the s102 plan's seven steps are all closed and the hover
underline the user then asked for is built, checked and fixed (it was drawing across the letters) — committed as
`24638cc` and pushed; read the commit state from git, never from here**)

- **Build:** `make clean && make -j8` exit 0, **0 warning lines** under `WARN` (`-Wall -Wextra -Wshadow
  -Wwrite-strings -Wformat=2 -Wdiscarded-qualifiers`) — and the same for
  `make bin/test_vt bin/dbg bin/test_fm bin/test_render`. **Measure BOTH**: `make -j8` builds only `src/`, and
  the s103 lesson was a test build carrying a warning that made one palette check feed the wrong bytes while
  still passing. Command worth copying:
  `make clean && make -j8 2>&1 | grep -ciE 'warning|error'`, then
  `make bin/test_vt bin/dbg bin/test_fm bin/test_render 2>&1 | grep -ciE 'warning|error'`.
- **Suites — green, s133/s134 included, every number below re-run at s135's close-out.** Both builds clean (exit 0, 0 warning lines) and
  `./bin/test_vt` reports **1673 checks, 0 failures** (the confirm-quit wording, the process name, the box
  geometry, the four wrap cuts, the `middle_click_paste`, `bell` and `click_opens_url` config groups, the 16-case
  pointer-precedence table, the BEL-rings / BEL-terminates-a-string split, the `bell_strength` ladder, the 15-check
  `mux_bell_live` drawn-vs-hidden group, the 18-check `bell_is_ours` table, and the **157-check `url` groups**:
  the run rule and its spans, the trailing-mark cut/keep table, the scheme rules, the non-ASCII/wide refusal, the
  window-broke vs program-broke split (including a hard break AT the margin), a tab's gap, the alt screen, the
  `URL_MAX` boundary on all three sides, the delimiter corpus, the scrolled-away limit, `url_is_openable`, the
  9-case gate table, the **16-case `url_refusal` table (s132 — every reason named, the order the reasons are asked
  in, and `url_click_opens` pinned to `refusal == NONE`)**, the `url_open_due` boundary against `term_click_count`, the
  two header
  constants, an end-to-end `url_spawn` through two stand-in openers, and s133's **`url/hover` gate table (7 cases + a
  32-combination agreement loop: the underline shows exactly when a plain click would open)** and **`url/row` group**
  (which cells of a soft-wrapped run sit on which row — the underlined cells must equal the bytes that get opened).
  Everything else is green: `./bin/dbg`
  **MODEL OK (0 problems)**, `./bin/test_fm` **384 checks, 0 failures**, `./bin/test_render` **617 checks, 0
  failures** (the renderer's pure decisions — its colour choices, the cursor's shape, the hover underline's rect
  against the MEASURED font metrics, the question row's two
  colours, the box's six, the bell's colour — plus the source scans that cover `main.c` and `render_draw()`, which
  are in no test binary; s129 added 14 scans for the URL wiring, s132 3 more for the refusal reason, s133 14 for the
  hover wiring and s134 5 for the position rule), and
  `CLI: 99 checks, 0 failures, 13 skipped`
  with `KAI_TERM_TEST_NO_WINDOW=1` — those 13 are the
  windowed group, last run for real in s124 (**149 checks, 0 failures, 0 skipped**, twice, ~15 real windows each
  time; ask before running it again while the user is at the machine). `make asan` is green on THIS tree (re-run at
  s135 on the committed tree): all four asan binaries exit 0, 0 warning lines, no LeakSanitizer or
  UBSan report, same four counts. The whole set was then re-run on a **fresh clone of the step-7 commit**
  (`.scratch/s131/verify_commit`) to prove the commit is self-contained: `src/url.c` is tracked, both builds exit 0
  with 0 warning lines, and the four counts come out identical there.
- **`make asan` is GREEN at `ab0809a` and again on the s121 tree — exit 0, 0 warning lines, no LeakSanitizer
  report** (s107, re-run s121). It was red
  when s106 closed, and both halves of the red came in with the uncommitted 1e work: two `-Wformat-truncation`
  warnings in `src/theme.c` (visible ONLY in the asan build, where inlining lets the compiler bound the
  argument — see Key Decisions), and 134 bytes in 18 allocations leaked by the option-pinning harness, which
  mallocs the names it reads out of `usage()` and README (`usage_opts_of`, `readme_opts_of`) and never freed
  them. The warnings went by way of `set_name()` — one function that cuts at `sizeof v->name - 1` and LOGS the
  cut, because the cut decides the file name an imported theme lands under; the leak went by freeing the two
  name lists. Both fixes are pinned: the C check asserts the exact 127-byte stem, the CLI group greps the log
  line.
- **Docs are pinned to the code by check, not by eye:** `tests/test_vt.c` reads the **40** setting names (the
  hard-coded `ns == 40` beside the derived one — 36 -> 37 in s120 for `confirm_quit`, 37 -> 38 in s125 for
  `middle_click_paste`, 38 -> 39 in s126 for `bell`, 39 -> 40 in s129 for `click_opens_url`), the **24** binding names out of the two strcmp ladders
  in `src/config.c`, the **23** theme names out of `src/theme.c`'s `THEME_KEYS` table, and the **9** long options
  out of `usage()` in `src/main.c`, and
  compares each set with `config/kai-term.conf` + `config/kai-term.bind` + `config/kai-term.theme` + README's
  tables — the option table by `readme_opts_of()`, README's **settings** and **binding** tables by
  `readme_table_names_of()` + `names_diff()` (added s110: before it, deleting a settings row from README left
  every check green). A name added to a parser without a template line, a template line no parser knows, a
  template line the parser does not know, and a bogus README row (a `made_up_key`) each fail a check. Never
  hand-copy a name list into a test — derive it from the source.
- **No leftover probe or experiment in the tree, and no `TODO`/`FIXME`/`XXX`.** `grep -rnE
  "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/ include/ tests/` returns only the three lines in
  `tests/test_render.c` that WRITE a probe file — the token is split by string concatenation there on purpose,
  so the scanner can never count its own source (s103). `grep -rn "TODO\|FIXME\|XXX"` returns only `mkdtemp`'s
  `kai-term-int-XXXXXX` template and one test's fill string. If that ever changes, re-word this line rather
  than leave a claim that no longer matches the grep.
- **No `install` target**, and nothing copies `config/*` to `/etc/kai-term` — deliberate; README tells the user
  to copy the templates. Adding one is the user's call.
- **Git:** `origin = https://github.com/K-code-AI/Kai-Term`, branch `main`. Step 3b is committed and pushed —
  but read that from git, never from here: `git status -sb` and `git log --oneline origin/main -1`. `git fetch`
  before push — the user commits on GitHub too. Never commit `bin/` or `build/` — and note `.scratch/` is NOT
  in `.gitignore`, so it shows as untracked and must never be `git add`-ed wholesale.
- **Owed to the user's eye, not to a harness:** the look of a themed window, a live OSC colour repaint, the
  configured `cursor_color`, the whole step-2 cursor (a bar that reads as a bar, the 1 s blink rhythm, the blink
  stopping when the window loses focus, vim/fish getting the shape they ask for), and **step 3b's preview
  selection** (that a drag there highlights, that the band reads as the terminal's own highlight, that a double
  click picks the word they meant, that `Ctrl`+`Shift`+`C` gives the file's own text with no leading indent, and
  that scrolling or resizing drops the highlight). None of those has ever been confirmed in the user's window;
  every one has a check on the DECISION and none has a check on the PIXEL. (`--import-theme`, the theme file and
  **step 3, the double/triple click** HAVE been confirmed — the import 2026-10-19, the click gesture 2026-10-20,
  s114.) Step 3b's `main.c` half (the pointer mapping, the release that ends a drag, the copy precedence) is in
  the file no test binary links, so it is the part their eye actually settles. Recipe for the theme: put their
  kitty theme at `~/.config/kai-term/themes/13th-floor.theme`, add `theme = 13th-floor` to
  `~/.config/kai-term/kai-term.conf`, then `printf '\033]11;#082135\007'` — the paper changes and `-d` logs that
  an application set it, not the file. Hand these to them; do not map a window to judge a look yourself.
  **Step 4 is CONFIRMED IN BOTH FORMS — the user had eyes on the question row (2026-10-20, s124) and on the
  WINDOW-close box (2026-10-20, s125: "I confirm the feature to be working. I had my eyes on it.").** Nothing in
  step 4 is owed to their eye any more, and step 4 is closed. Do not re-ask about it or re-propose it.
  **What is on this list now:** the look of a themed window, a live OSC colour repaint, the configured
  `cursor_color`, the whole step-2 cursor, step 3b's preview selection, and **step 5's middle click** — that
  with `middle_click_paste = 1` a middle press in the terminal area pastes what `Ctrl`+`Shift`+`V` pastes,
  that a middle click on a tab item still CLOSES that session, that one in the band still picks a file, and
  that vim/tmux/`nano --mouse` keep their middle button. **Step 6's bell is on the list too**: that a `BEL`
  reads as a bell and not as a glitch, that the wash fades rather than snapping off, that in side-by-side view
  only the pane whose program rang flashes, and that the text stays readable through it — `render_draw()` is in
  no test binary, so two mutants (`render_draws_nothing`, `render_wrong_surface`) survive every check here by
  construction. Recipe for the user: `printf '\a'` at the prompt (or Enter on an empty prompt, or `less` on an
  unknown command, or `Up` at the top of a file in `less`) — the pane washes toward the text colour for a quarter
  of a second and fades; with `bell = off` nothing is drawn at all. **And the s128 half: that a plain click in
  `less` no longer washes the pane** (it used to, because the click became Up presses and `less` belled at the
  beginning of the file), while a click in `nano` still moves the cursor and a BEL the program rings on its own
  still flashes. **Step 7's click is CONFIRMED — the user had eyes on it 2026-10-22 (s133): "The link clicking now
  works and open as expected."** Nothing in step 7 is owed to their eye any more. Recipe, for anyone re-checking it:
  `printf 'see https://example.com/a?b=1\n'` then click it; `./bin/kai-term -d` logs
  `url: a click asked xdg-open for …` and, when a click is refused, why.
  **What is on the list from s133: the look of the hover underline** — that the address under the pointer gains an
  underline that READS as an underline at their font size (its thickness and its place in the cell), that it follows
  the pointer cell for cell, that it is gone when the pointer leaves the address, the pane, or the window, and that it
  does NOT appear where a click would not open (`less`/`nano`/`man`, `Shift` held, a drag in progress, or
  `click_opens_url = 0`). Every one of those is a decision with a check; none has a check on the pixel.

## Current Task — s135 CLOSED (2026-10-21): close out s134 — the final verification ISSUES.md already claims, then the
commit and the push. RESULT: every claim verified by a run in this session (see the s135 journal entry), feature
committed as `24638cc` and pushed, `origin/main` re-read from git. Nothing is in progress; the hover underline is
owed to the user's eye.

### s135 task block — verify the claims, then commit s132+s133+s134

**State found at session start (from git, not from the prose).** HEAD is `124983c` (s131 journal). The working tree
carries ALL of s132 (refusal logging), s133 (hover underline) and s134 (underline position fix) uncommitted:
`AGENT.md ISSUES.md Makefile README.md config/kai-term.conf include/kai-term/kai-term.h src/main.c src/render.c
src/url.c tests/test_render.c tests/test_vt.c` modified, nothing staged. The s134 journal entry ends at STEP 5 DONE
and says the ISSUES entry "is written after the final measurement below" — but ISSUES.md ALREADY carries the
final-looking numbers (both builds 0 warnings, test_vt 1673/0, dbg MODEL OK, test_fm 384/0, test_render 617/0,
`make asan` clean, CLI 99/0/13). The journal records no run producing the asan or CLI numbers. So those claims are
UNVERIFIED until re-measured in this session; if a measurement disagrees, ISSUES.md gets corrected, not defended.

**Plan, in order.** (1) `make clean && make -j8` and the test build, counting warning lines in both. (2) Run
`./bin/test_vt`, `./bin/dbg`, `./bin/test_fm`, `./bin/test_render`; the counts must match what ISSUES/AGENT claim.
(3) `make asan` (build + run all four asan binaries; watch for LeakSanitizer/UBSan reports). (4) Headless CLI group
only — `KAI_TERM_TEST_NO_WINDOW=1 sh tests/cli_test.sh ./bin/kai-term` — the windowed group stays skipped, the user
is not at the machine to consent to ~15 mapped windows. (5) Re-run `.scratch/s134/mutate.py` only if any source
changed during verification (none expected). (6) Correct any doc number that disagrees; update this file's Current
Status/Current Task and the Progress Journal; commit the feature and the journal in the repo's usual two-commit
shape; push to `origin` and verify the push from git. (7) Record what stays owed to the user's eye: the underline
reading as an underline at their font size.

**Expected outcome.** Every number in ISSUES.md and Current Status confirmed by a run in THIS session, or corrected.
Tree committed and pushed; s132/s133/s134 closed; nothing left half-documented.

**RESULT (s135, all runs in this session, host clock 2026-10-21).** `make clean && make -j8` exit 0 / 0 warning
lines; test build exit 0 / 0 warning lines; `test_vt` **1673/0**, `dbg` **MODEL OK (0 problems)**, `test_fm`
**384/0**, `test_render` **617/0**; `make asan` exit 0, 0 warning lines, **0** AddressSanitizer/LeakSanitizer/UBSan
reports, same four counts; `KAI_TERM_TEST_NO_WINDOW=1 sh tests/cli_test.sh ./bin/kai-term` → **CLI: 99 checks, 0
failures, 13 skipped**; `.scratch/s134/mutate.py` re-run: **CONTROL green** (1673/0, 617/0), **36 mutants, 34
killed, 2 survivors** (`hover_no_cap`, `renderer_adds_own_rule` — the two honest ones ISSUES names). Every number
ISSUES.md and Current Status carry was produced by a run in this session — none needed correcting. README's
refusal-log promise was grepped against the code before committing (s131's lesson): `main.c` logs
`url: the click at (%d,%d) opened nothing — %s` and `url.c` names all six reasons verbatim. Feature commit
`24638cc` (src, include, tests, Makefile, config templates, README — s132+s133+s134 in one commit, as the s134
block planned; their hunks share files and were not surgically separable), pushed, `git log --oneline origin/main -1`
reads `24638cc`. No source changed during verification, so the mutant run above doubles as the s133-mutant re-run
the s134 plan owed (the harness re-anchors every s133 mutant).

---

The s134 report and plan (CLOSED by `24638cc`; kept for the measured font ladder and the coordinate-space facts,
which the journal entry references):
the hover underline drew ACROSS the text, not under it (the user's report
2026-10-22; the measurement and the plan are in the s134 journal entry). s133's hover underline and s132's
refusal-naming are in the working tree, green (`test_vt` 1673/0, `test_render` 604/0, re-measured at the head of the
s134 entry), uncommitted, and get committed together with this fix. Step 7 (plain-click URLs) is CLOSED and
CONFIRMED by the user's eye (2026-10-22: "The link clicking now works and open as expected"); the hover underline is
back on the owed-to-their-eye list until they say the line reads as an underline. The s102 plan's seven steps are all
done. "Next Steps" holds the rest. Steps 0-7 keep their sections below: they are the rules a change must not break.

### s134 task block — the underline is over the text instead of under it

**The report.** "The Links now get visual feedback but it's over the text instead of under it. It would be better
under" — with a screenshot of `printf 'see https://example.com/a?b=1 now'` showing the line crossing the letters.

**Measured starting point (`.scratch/s134/metrics.c`, `metrics2.c`, `desc.c`, run 2026-10-22 against the working
tree; every number below is copied from that output).**
* **The quad's y grows UP.** `render_init`'s MVP is the plain pixel ortho `2/vw`, `2/vh` with no flip, and every call
  site in `render_draw()` computes `py = (nrows-1-y)*ch + pad_gl`, where `pad_gl = vh - pg->y - nrows*ch` is
  documented as "the GL y of the bottom of the last drawn row". So a quad's `y` is its BOTTOM edge measured up from
  the cell's bottom edge, and `y = cell_h - h` — which reads like "the bottom edge" only to someone assuming y grows
  down — paints the line in the TOP h pixels of the cell. That is the report.
* `HoverDraw`'s own comment in the header says "y is measured UP from the cell's bottom edge" (right); the
  `CursorDraw` comment above it says "origin at the cell's top-left" (wrong), and `render_hover()` was written from
  `render_cursor()`'s `CUR_UNDERLINE` branch, which uses that wrong convention. **So the DECSCUSR underline cursor
  (`ESC[5 q`) has been drawn at the top of the cell since s112** — same defect, never seen because no app the user
  runs asks for an underline cursor and the default bar spans the whole cell height, where the sign of y cannot show.
* The checks PINNED the bug: `tests/test_render.c` asserts `d.y + d.h == cell_h` ("always flush with the cell's
  bottom edge") and `d.y == ch - 2` for the underline cursor. A check that restates the implementation's assumption
  cannot catch it.
* The font, measured through the shipped loader (`font_init("monospace", px)` → Liberation Mono, what
  `font = monospace` resolves to here): px 10 → cell_h 11, ascent 9, descent 3; px 12 → 14/10/4; **px 14 (the
  default `font_size`) → cell_h 16, ascent 12, descent 5**; px 16 → 18/14/5; px 20 → 23/17/6; px 24 → 27/20/8;
  px 32 → 36/27/10. `cell_h - ascent` is the room the cell leaves under the baseline: 4 px at 14 px, 7 px at 24 px.
  Note `cell_h < ascent + descent` at every size — this font's descender is deeper than the cell's own leading.
* The glyphs of the URL in the screenshot, measured the same way: `p` reaches **3 px below the baseline at 14 px**
  (5 px at 24 px); every other character of `https://example.com/a?b=1` stops at the baseline. So at the default size
  the whole descender band is 3 px deep and the cell has 4 px under the baseline — an underline belongs in that last
  pixel row, clear of the `p`.
* The font's own OS/2 answer agrees: `underline_position` −435 units / `underline_thickness` 84 units at upm 2048
  (font units, NOT pixels — they do not change with px, which is what the first probe got wrong) → 3 px below the
  baseline, 1 px thick at 14 px. Not used by the fix (it would need two new `Font` fields and a fallback for faces
  that report 0); recorded because it is the cross-check that the rule below lands where the font designer put it.
* `f->scale` is 1.0 and stays 1.0 (only `font_init` writes it); zoom goes through `font_resize()`, which calls
  `font_metrics()` again, so `ascent`/`descent` are always the metrics of the CURRENT pixel size.

**Plan.** (1) `src/render.c`: ONE static rule for "a horizontal line under the characters of a cell" —
`underline_y(cell_h, ascent_px, descent_px, h)` — used by both underlines, so they cannot drift again; it puts the
line at the bottom of the DESCENDER band (`baseline - descent`), never below the cell and never above the baseline,
which is what keeps a font with generous leading from getting its line under the empty leading instead of under the
text. (2) `render_hover()` and `render_cursor()` take the font's `ascent`/`descent` (scaled) — the three
`render_cursor()` call sites in `render_draw()` already have them. (3) The header: fix the `CursorDraw` comment
(top-left origin → the quad's y is its bottom edge, measured up) and say what `ascent`/`descent` are for. (4) Checks:
replace the two pinned-the-bug assertions with the real invariants (never above the baseline when the cell has room,
never below the cell, at the bottom of the descender band when the cell has leading) plus the measured Liberation
Mono ladder at 10/12/14/16/20/24/32 px, for BOTH underlines, and scans that both call the one helper and that the
font's descent actually reaches the draw loop. (5) Mutants in `.scratch/s134/` — including the exact old formula.
(6) README/ISSUES/this file, both builds, every suite, `make asan`, the headless CLI group, commit, push — which also
closes the verification s133 left unfinished (`dbg`, `test_fm`, asan, the CLI group, the s133 mutants).

**Expected outcome.** `test_render` up by the position table and the new scans; `test_vt` unchanged (the position is
a render.c decision); both builds at 0 warning lines; asan clean. What stays owed to the user's eye: that the line
now reads as an underline at their font size — the checks pin the rect against the measured font metrics, and no
check pins the pixel.

The s102 plan (steps 1-7 below) is still the plan. All eight open questions were answered by the user
2026-10-22 and the answers change four details: no `cursor_style` key, no primary-selection work, plain-click
URLs, an interactive theme-overwrite prompt. The answers are under "Decisions already made" — do not re-ask
them, and do not re-propose what was dropped.

### s129 task block — step 7: a plain click opens a URL

**Objective.** A single left click on an `http://`/`https://` run in a terminal pane opens it with the desktop's
opener, with the gates the user's answer to question 7 already settled, and a key to turn the whole thing off.

**Measured starting point (`.scratch/s129/urlprobe.c`, run 2026-10-21 against the tree at `bfaa749`; every
number in the new checks is copied from that output, not guessed).**
* `grep -rniE "url" src/ include/ tests/` → 11 hits, all incidental (`url_color` in the importer, one comment
  each in vt.c and main.c). No URL handling of any kind, and no OSC 8.
* A URL sits in the grid as ordinary cells, one codepoint each; the cells after the text are `ch == 0`, and a tab
  leaves `ch == 0` gaps, so a run ends at a blank with no special case.
* A soft-wrapped URL is ONE logical line: at 40 cols, `read https://example.org/a/very/long/path/that/wraps/
  over/two/rows now` gives the span `(0,5)..(1,25)` from a click on EITHER row, and the wrap mark sits on the row
  that was broken (`term_line_span` already walks it — reuse it, do not re-derive the direction).
* A URL ending exactly at the margin wraps mid-host: `a https://example.com` at 20 cols → `(0,2)..(1,0)`.
* A URL the PROGRAM broke with a newline is NOT joined: `https://example.com/\r\nwrapped-by-the-program` gives
  `(0,0)..(0,19)` and the next row is its own line.
* A run whose start has scrolled above the viewport is not found (`term_line_span` walks viewport rows only) —
  the same limit the triple click has; it goes in ISSUES, not fixed.
* A non-ASCII cell ends a run, which CUTS a URL in half: `https://ex\xc3\xa9.com` scanned naively returns
  `https://ex` — a different page. So a run whose right neighbour is a non-ASCII or wide cell is REFUSED
  (never open half a URL). A non-ASCII cell to the LEFT of the scheme is fine: the scheme match makes the start
  unambiguous.
* Trailing marks measured with the draft rule: `https://x.` → cut the `.`; `https://x,` → cut; `https://x?` → cut;
  `(https://x/a(b)c)` → the leading `(` is outside (the run starts at the scheme) and the last `)` is cut as
  unbalanced while `https://x/a(b)` keeps its balanced pair; `https://x/` KEEPS the slash (a trailing slash is
  part of a URL, and cutting it changes the page); `"https://x"` and `<https://x>` end the run at the quote/bracket.
* `http://` with nothing after it, `https://` followed by a space, `ftp://…` and `mailto:…` are refused; a scheme
  mid-word (`xhttps://example.com`) matches from the `h` (that is what `grep -o 'https\?://[^ ]*'` does too).
* The alt screen: the model still finds the run (`https://example.com/alt` → `(0,0)..(0,22)`) and
  `term_wants_key_mouse()` answers 1 — so the gate, not the scan, is what keeps `less`/`nano`/`man` owning
  their click.
* A 3000-character run IS found by the scan (16 rows at 200 cols), so the opener needs a length ceiling.
* `xdg-open` exists here (`/usr/bin/xdg-open`), `xdg-desktop-portal` is running, and glibc 2.43 has
  `closefrom()` — all verified, not assumed.

**Plan.** (1) `src/url.c` (a new file, so the whole feature — including the fork/exec — is in a binary the tests
link; `main.c` is in none): the run rule, the logical-line span, the text extraction, the opener-argument
validation, the click gate, the double-click wait, and the spawn. (2) `Config.click_opens_url` (default **1** —
the user asked for the plain click), with the template line, the README row and the settings count 39 → 40.
(3) `src/main.c`: the flags on release, a pending open that waits out `CLICK_DCLICK_MS` (so a double click on a
URL selects the word and opens NOTHING — the plan's "after the double-click window says this was a single click"),
re-validated at the deadline against the same cell, then `url_spawn("xdg-open", url)`; `url_tick()` joins
`blink_tick()`/`bell_tick()` on the frame timer. (4) Checks in `tests/test_vt.c` (spans, refusals, gates, the
wait, and an end-to-end spawn that proves the argument arrives and that Kai-Term's own descriptors are closed
in the child) plus source scans in `tests/test_render.c` for the `main.c` wiring. (5) A mutant harness
(`.scratch/s129/mutate.py`) to prove the new checks are not vacuous, A/B against a tree without the feature.
(6) README + ISSUES, both builds, every suite, `make asan`, the headless CLI group, one commit, push.

**Expected outcome.** `test_vt` up by roughly the 60-80 checks of a new `url` group, everything else unchanged,
both builds at 0 warning lines, asan clean, and the honest note that whether the click OPENS a browser is owed to
the user's eye — `url_spawn` is exercised end-to-end by a test with a script, but `xdg-open` launching their
browser is a window-and-desktop fact.

**What is already in the working tree when s130 opened (measured with `git status -sb` / `git diff --stat`, not
remembered).** Plan steps (1)-(3) are WRITTEN but NOT committed and NOT yet journaled: `src/url.c` (334 lines,
untracked), and modifications to `include/kai-term/kai-term.h` (+93: `URL_MAX`, `URL_OPENER_PROGRAM`, `UrlSpan`,
`UrlClick`, `URL_OPEN_WAIT_MS`, the five function declarations, `Config.click_opens_url`), `src/config.c` (+9:
default 1, the ladder entry), `config/kai-term.conf` (+13: the template block), `src/main.c` (+89: the `W` fields,
the release-side flags, `url_tick()`, the frame-timer call), `Makefile` (+4/-4: `src/url.c` in `SRCS`, in
`bin/test_vt`, in `bin/test_vt_asan`). Plan steps (4)-(6) — the checks, README/ISSUES, the harness, the
verification, the commit — were NOT started. Verified state of that tree, measured 2026-10-21 (s130) before any
edit: `make clean && make -j8` exit 0 with **0 warning lines** (so `closefrom()` really is declared on this
host — glibc 2.43, and the link proves it, which an assumption could not), and
`make bin/test_vt bin/dbg bin/test_fm bin/test_render` exit 0 with **0 warning lines**, but
**`./bin/test_vt` = 1516 checks, 2 failures**, both of them the doc-pinning gate noticing the new key:
`FAIL tests/test_vt.c:3401: template: the parser accepts 39 settings and 24 bindings` and
`FAIL: README's settings table does not document click_opens_url`. That is the tree s130 starts from: the feature
exists in the code and is RED in the suite until the checks and the docs catch up. Nothing below "s129 task
block" may be quoted as verified until it is re-measured.

### Where each step stands

Done work is listed here only to say it is done and where its rules live; the mechanism itself is in
"Architecture & Invariants", and the session that built it is in git.

- **Step 0 — the dead `cursor_color =` (FIXED).** `Render.cursor_color`, passed from `main.c`, derived in the
  pure `render_chrome()`, checked at the consumer in `tests/test_render.c`. The lesson is in Key Decisions: a
  key that is defaulted, parsed, stored and parse-tested but read by nothing leaves the whole suite green while
  the feature is absent.
- **1a — palette is process-wide runtime state (DONE).** `g_palette[256]` in `src/vt.c`, read through
  `term_palette()`. Only entries **0-15** are themeable; the cube (16-231) and the ramp (232-255) stay
  computed. The rule and why it left the `Term` struct are in Architecture & Invariants ("the palette is
  process-wide").
- **1b — the 6x6x6 cube's top level wrapped to near-black (FIXED).** The formula was `55 + 41 * level`, so level
  5 gave **260**, and `260 << 16` leaves `0x04` in the channel: 91 of 216 entries painted near-black where they
  mean bright (vim/neovim colorschemes, htop, bat, delta, pygments). Alpha stayed `0xff` because the carry
  landed on a bit already set, which is why no check could ever have noticed. Fix: the six levels are a table
  `{ 0, 95, 135, 175, 215, 255 }` — a table cannot overflow. The pitfall is in Key Decisions.
- **1c — a `.theme` file, `theme =`, conf-beats-theme precedence (DONE, `c84258a`).** `src/theme.c`: 23 names in
  one `THEME_KEYS` table, `theme_find()` resolving through the `themes/` subdirectory of the same three config
  directories, `config/kai-term.theme` as the shipped example. A theme is COLOURS only, in kai-term.conf's
  `key = value` and in kitty's `key value`, so an existing kitty theme loads unchanged. Entries 16-255 are
  refused in a FILE while OSC 4 may set them — a decision with a check on each side. The precedence rule (a
  `background =` line wins above OR below the `theme =` line, both orders checked) is in "Decisions already
  made" and in the Config invariants.
- **1d — OSC 4 / 10 / 11 / 104 behind `osc_colors = 1` (DONE, same commit).** OSC 4 sets a palette entry
  (single and `N;c;M;c` multi form); OSC 10/11 set the default ink/paper and answer `?` in the X11 16-bit form;
  OSC 104 restores the whole palette or named entries. Before it, `osc_end()` knew OSC 0/1/2 only. Colours go
  through the ONE parser `cfg_parse_color` plus X11 `rgb:RR/GG/BB` and `rgb:RRRR/GGGG/BBBB`; X11 NAMES are
  refused. `term_palette_builtin_entry()` is the single source for a built-in colour (a second palette table is
  a copy that can drift). `term_recolor_defaults()` (vt.c) is one dumb match-and-replace over the live grid, the
  parked MAIN grid, the pen and the prompt snapshot, and `mux_recolor_defaults()` sweeps every session with it —
  why it must sweep and why it must NOT skip blanks is in the one-value-owns-ink-and-paper and `cell_pristine()`
  rules. Also found here: the first `rgb:` converter "rounded" a 16-bit component with `(d + 0x80) >> 8`;
  `0xffff + 0x80` is `0x1007f`, `>> 8` gives **256**, and `256 << 16` puts `0x01` in the GREEN byte, so
  `rgb:ffff/ffff/ffff` — the canonical X11 spelling of WHITE — painted `#010100`. Same defect class as the cube.
  Fix: one shift, arithmetic that provably cannot exceed a byte, mixed widths refused.
- **`selection_color =` became a real key (DONE, `5c1f58d`)** — the theme prerequisite: `Config.selection_color`
  (default `SEL_BG`) -> `Render.selection_color` -> `render_chrome()` derives the band, the active bar item and
  the focus border from it, and `fm_paint()` takes a `sel` argument so the file manager's selected row matches.
- **1e — `--import-theme FILE` (DONE, built s105, green and committed s107, confirmed by the user s108).** The
  importer lives in `src/theme.c` (~940 lines), not in `main()` — `main()` is in no linked test, so a flag whose
  work is done there would be covered only by the CLI script (same reasoning as "Geometry lives in
  `src/layout.c`"). What is in `main()` is the flag, the question and the report; it runs BEFORE `config_load`
  so the settings file's log lines do not bury the import report, and it needs no display. API: `theme_sniff()`
  (format from CONTENT, never the file name), `theme_parse_text()`, `ThemeVals` (a colour of 0 means "the file
  did not name it", so an unset colour falls back to Kai-Term's default rather than to black),
  `theme_write_file()` (writes the NATIVE format, so an imported theme is read afterwards by the same reader as
  a hand-written one — that round trip is a check), `theme_dest_dir()` (first writable `themes/` of the config
  search, never `/etc` — importing must not need root), `theme_name_of_path()`, `theme_import()` (0 written /
  1 exists-and-not-granted / -1 refused). Formats: kai-term's own, kitty's, Xresources, alacritty.toml, Konsole
  `.colorscheme` (modern sections and legacy `[Color Scheme]`), xfce4-terminal `terminalrc`, Windows Terminal /
  VS Code JSON. **iTerm2 `.itermcolors` is recognised and REFUSED** — 0.0-1.0 floats in an XML plist, no iTerm2
  on this machine to check a reader against, and a parser nobody can verify is worse than a refusal that says
  what it looked for. Two rules the whole block obeys: **never guess a format** (nothing matched -> refuse and
  list what was looked for) and **never guess a colour** (X11 names, alacritty's `CellForeground`, a JSON
  `"background": "red"` are refused and logged, not approximated). Measured corpus, not assumed
  (`.scratch/s105/importprobe.c` + `.scratch/s105/corpus/`, re-run s106) — the per-format set/dropped counts
  every importer number was copied from live there.
- **Step 2 — the bar cursor, its blink, and DECSCUSR (DONE, s112).** Settings names 33 -> 35. Rules below.
- **Step 3 — double-click a word, triple-click a line (DONE, s114).** `term_word_span()` / `term_line_span()` /
  `term_click_count()` in vt.c, the gesture state in `W`, one shared `CLICK_DCLICK_MS`, and the key
  `click_selects_word = 1` (settings names 35 -> 36). Rules below.
- **Step 3b — the same selection in the file manager's preview pane (DONE, s117).** `cell_row_word_span()` in
  vt.c (the shared row rule), the selection state and the six `fm_preview_*` selection functions in fm.c, the
  gesture in `src/main.c`. No new config key. Rules below.
- **Step 4 — ask before quitting when something is still running (DONE, s120+s121, committed).** Its own
  section below holds the measured predicate, the rules each stage added and what is still owed.
- **Step 5 — middle-click paste, opt-in and off by default (DONE, s125+s126, `f3c812a`, pushed).** `PtrPress` +
  `ptr_press_action()` in vt.c (the precedence as one decision), the key `middle_click_paste`, and one paste path
  (`clipboard_paste(why)`) shared with `Ctrl`+`Shift`+`V`. Settings names 36 -> 37. Rules below.
- **Step 6 — the visual bell (DONE, s126+s127, `1f79221`, pushed).** `Term.bell_rang` (the event) +
  `Tab.bell_ms` (the moment, on main.c's clock) + `bell_strength()` (the fade) + `render_bell()` (the colour) +
  `mux_bell_live()` (which sessions are on screen to flash) + one quad per drawn pane in batch A. The key
  `bell = visual|off`, default `visual`; settings names 37 -> 38 -> 39 across steps 5-6. Rules below.
- **Step 7 — a plain click opens a web address (DONE, s129+s130+s131, `4610911`, pushed).** `src/url.c`: the run
  rule (`term_url_span`), the text (`term_url_text`), the argument rules (`url_is_openable`), the gates as one
  ordered decision (`url_click_opens`), the double-click wait (`url_open_due`) and the spawn (`url_spawn`). The key
  `click_opens_url`, default **1**; settings names 39 -> 40. The opener's name is a header constant
  (`URL_OPENER_PROGRAM`), not a key, on purpose. Rules below.

### 2. Cursor: a thin bar, classic blink — DONE (s112)

The user's words: "A thin rectangle, the most classic blinking cursor". In the tree: `Parser.inter` (the last
intermediate byte, reset at `CSI`, dropped on the floor before) + `Term.cur_style`/`cur_blink` with the `CUR_*`
and `CURBLINK_*` enums (0 = the app never asked, which is the only safe encoding because `term_init()` memsets),
the pure `render_cursor()` in `src/render.c` returning `CursorDraw { block, quad, invert, tint, x,y,w,h }` and
asked by all three passes of `render_draw()`, `Render.cursor_width`/`Render.cursor_on` with `render_init()`
taking the width, a new pass 2b emitting the bar/underline quad in **batch B between the selection bands and the
glyphs**, the fourth timerfd `cbfd` with `blink_half_ms()`/`blink_update()` in `src/main.c`, and the keys
`cursor_blink = 1000` / `cursor_width = 2`.

The rules a later change must not break, all of them checked:
- A bar/underline is a **separate quad**, never the cell background, and it is emitted AFTER the bands and
  BEFORE the glyphs: batch B is one draw call in which the later quad simply wins, so a bar after the glyphs
  would hide the character and a bar before the bands would vanish under a selection. A BLOCK stays the cell's
  own background from pass 1 — and it still steps aside exactly where a band is painted (the "must match pass
  2" comment), because a block that forgets it erases itself. A bar/underline deliberately does NOT step aside.
- All three passes ask `render_cursor()` the SAME question with the same arguments, so they cannot disagree.
  `term_cell_highlightable()` gates `hl_on` in passes 1 and 2b and not in pass 3; that is harmless and known,
  because it returns 0 only for a never-written blank cell, which has no glyph to tint.
- The blink is a timerfd whose tick only flips `W.cursor_on` and sets `W.dirty`; the swap happens on the normal
  `redraw()` path. Never draw from a Wayland listener, never arm a `wl_surface_frame` callback without
  attaching a buffer. Phase lives in `W`, never in `Term` (`term_init()` memsets `Term`; DEC 25l must still hide
  the cursor; only the visible session's cursor blinks — an inactive pane's is steady and dimmed).
- `conf.cursor_blink` is the CEILING: `0` never blinks whatever an application asks, DECSCUSR can only make the
  cursor less flashy. That is a deliberate difference from xterm, which lets `ESC[1 q` switch blinking back on —
  a user who set `cursor_blink = 0` asked not to have a blinking cursor. Blinking is gated on `W.is_activated`
  (already decoded from the xdg-toplevel states in `toplevel_configure()` — no new Wayland plumbing needed) and
  on `t->show_cursor`, and `blink_update()` re-arms when the half-period OR the visible session changes:
  `cbfd_term` records the session the arming was decided for, and without it a tab switch kept the old phase and
  could show a window with no cursor for up to a full half-period.
- `cursor_width` is clamped twice: 1..64 by the parser, and to `cell/3` (bar) / `cell_h/3` (underline) with a
  floor of 1 px by `render_cursor()`, so the cursor can never be 0 px and never a block wearing a hat.
- **No `cursor_style` key** — always a bar unless an application asks (the user's answer: "blink 1 sec, 0.5
  sounds too fast" is why the default half-period is 1000 ms).
- DECSCUSR's space is required on purpose: it is what the sequence IS, and it keeps `CSI Ps q` out. `ESC[1q`
  changes nothing; `9 q`, `99 q`, `ESC[?1 q` are ignored. There is exactly ONE CSI entry point in `term_parse`,
  so `Parser.inter` cannot leak between sequences. The only other `case 'q'` in `src/vt.c` is the kitty-graphics
  APC parser — untouched.

### 3. Double-click selects a word, triple-click a line — DONE (s114)

The user's rule, verbatim: "a word is everything no separated by a space, but we don't take the trailing ',' or
'.'", and asked which trailing marks that covers, "any of it". In the tree: the three vt.c functions plus
`CLICK_DCLICK_MS` in the header (replacing main.c's private `FM_DCLICK_MS` — one number for both gestures, so
they feel like one gesture), and the click state `W.click_count/click_ms/click_col/click_row/click_word`.

The rules a later change must not break, all of them checked:
- **The separators are exactly two**: a never-written cell (`ch == 0`) and a real space. Any `ATTR_WIDE` cell —
  anchor or continuation — is part of the word, which is what makes cutting a CJK/emoji glyph impossible by
  construction rather than by an extra branch.
- **Trailing punctuation is cut a GLYPH at a time**, never a cell: a full-width mark (`。` U+3002) is a wide
  pair, and dropping its anchor while leaving its continuation puts half a glyph outside the selection. The cut
  never strips the word below one glyph, and the guard for that is what stops the span going INVERTED
  (`c1 < c0`) when the whole word is one wide mark.
- **`trailing_punct()` is ASCII `ispunct()` plus an explicit list** of the marks a terminal really draws
  (curly quotes, en/em dash, ellipsis, CJK/full-width punctuation). Everything else stays part of the word:
  there is no Unicode punctuation table in this project, and guessing at the rest of Unicode cuts words that
  should stay whole. A word that keeps a mark is a smaller mistake than a word cut in half.
- **Leading punctuation stays** (the user's wording is "we don't take the TRAILING"), so `/etc/passwd` and
  `(paren` come over whole. A comma in the MIDDLE of a run is not trailing: `a,b` stays `a,b`.
- **A click on a blank selects the clicked cell**, and an out-of-range cell answers 0. "The double click did
  nothing" reads as a broken terminal; a one-cell highlight does not.
- **`term_line_span()` reads `Grid.wrap` by SLOT index (`scroll_off + row`) and in one direction**: the mark
  sits on the row that WAS broken and continues on the NEXT one. Both are easy to write backwards and a
  backwards walk extends every triple click one row the wrong way; both are pinned by mutation
  (`.scratch/s114/mutate.sh`). `wrap[i]` is NOT set until the character that crosses the margin is written, so a
  line ending exactly at the margin correctly stops above the blank row below.
- **The gesture suppresses the synthesised cursor keys.** `W.click_word` is what stops `click_moves_cursor`
  from walking nano's or less's cursor twice on release — the interaction the plan called out, and the one part
  of step 3 that lives in main.c and so has no headless check. **Confirmed by the user in their own window,
  2026-10-20 (s114): "I checked and it worked."**
- The comparison in `term_click_count()` is unsigned, so Wayland's 32-bit ms clock wrapping mid-gesture still
  reads as one gesture; the 500 ms window is inclusive at the boundary.
- **A word the WINDOW broke is selected per row** (measured: `0..19` then `0..7` at 20 cols). Documented in
  README and in ISSUES.md "Known limitations" — the triple click is the gesture that takes a wrapped line.
- The checks put a returned span through `span_copy()`, the same whole-cell endpoints `sel_set_endpoints()`
  produces, so they pin what would land in the clipboard and not just two integers.

### 3b. Text selection in the file manager's preview pane — DONE (s117)

The user's words: "could we also do the same for the preview pane ? that and text selection. That would be
useful." In the tree: `cell_row_word_span()` in `src/vt.c` — the row-level word rule lifted onto a
`const Cell *row` + cols, with `term_word_span()` now a wrapper over it — the selection state in `FileMgr`
(`pv_sel_on`, `pv_ax/pv_ay/pv_bx/pv_by`), `fm_preview_select()` / `_select_clear()` / `_selected()` /
`_word_span()` / `_row_span()` / `_selection_text()` in `src/fm.c`, the band painted into the cells' own `bg` by
`paint_preview()`, and the gesture in `src/main.c` (`pv_cell()`, the preview's own click pair, the drag flag
`W.pv_selecting`, the release that ends a drag, and a `selection_copy()` with one owner per copy). **No new
config key** — `click_selects_word` is the one switch for the gesture, and a second knob for the same gesture in
a second pane is something nobody would understand.

The measured facts the design rests on (`.scratch/s115/pvprobe` and `selprobe{,2,3,4}` — do not re-derive):
- `FmPane` is a **flat `Cell` array**, `cell[y*cols+x]`, with the same `ATTR_WIDE` anchor+continuation
  convention a session grid uses (measured: `[0:U+4F60] [1:WIDE-CONT] [2:U+597D] [3:WIDE-CONT]`), so a preview
  range is expressible in pane cells and the word rule can be SHARED rather than copied.
- The head is rows `0 .. preview_head_rows()-1` (name, then path, then an error line when there is one); the
  body starts at the row `paint_preview_head()` leaves and its text starts at **column 2** — the two-column
  gutter is never written. The head rows are selectable text too, which is right: the file's name is text.
- **A line is CUT at the right edge, never wrapped**, so one file line is exactly one pane row and `pv_scroll`
  counts lines = rows. The preview's LINE gesture is therefore "the row", with no wrap marks to walk — the
  opposite of a session grid's case. Tabs are 4-column stops aligned to the text start.
- A wide glyph at the pane's LAST column is not widened (`wide = cp_is_wide(cp) && x + 1 < p->cols`), so the
  right edge can hold an anchor with no continuation cell.
- The highlight mechanism already existed: a selection changes a cell's **`bg`** only, and `fm_paint()` already
  receives `selection_color`. So the band needed no renderer change and no new quad — and that is also what
  makes it headless-checkable in `tests/test_fm.c`, which already inspects pane cells.

The rules a later change must not break, all of them checked (the `pvsel` group in `tests/test_fm.c`; the
non-vacuity harness is `.scratch/s116/mutate.py`, 13 kinds, every one caught):
- **One copy of the word rule.** `fm_preview_word_span()` calls vt.c's `cell_row_word_span()`. A second
  `trailing_punct()` in fm.c is exactly the drift the one-colour-parser rule exists to prevent, and the
  `word_rule_forked` mutant (6 failures) is what catches it. `punct_ascii_only` — the same mutant s114 ran
  against a session — fails the PREVIEW's checks too, because the preview has no table of its own.
- **The row gesture spans the row's OWN cells, not the pane's width.** The gutter was never written, so a span
  from column 0 copies two spaces the file does not contain and pastes them into the user's shell. A line the
  FILE indents is safe either way, because those spaces are real cells — "the first cell that holds something"
  is exactly where the file's own text starts. A row holding nothing spans the whole row: the gesture still
  means something, it just copies nothing.
- **The copy reads each row inside its USED range**, so a middle row of a multi-row selection does not turn the
  gutter into leading spaces; a gap BETWEEN two used cells (an entry name and its right-aligned size) is still
  a space. Trailing blanks per row are trimmed, rows are joined with `\n`, and a wide continuation cell
  contributes nothing — the same rules `term_selection_text()` follows, for the same reason.
- **The band lands only on a cell that HOLDS something** (a glyph, a real space, or the continuation half of a
  wide glyph), never on the never-written space past the end of a line — `term_cell_highlightable()`'s rule in
  a session. It writes `bg` and leaves `fg` alone, so a syntax-coloured keyword stays readable under it, and it
  is painted LAST so it sits over whatever the body wrote.
- **Endpoints are clamped to the pane and normalised in READING order.** The clamp is not cosmetic: the
  `no_clamp` mutant walks the painter off the pane and dies of SIGSEGV. Normalising by COLUMN alone anchors a
  drag that went down-and-left on the LATER row, after which `fm_preview_selected()` answers "nothing" for every
  row — a drag that selected nothing at all, and the first two checks of that pair pass with that mutant.
- **Invalidation is total:** a new preview, a `pv_scroll` that actually moved, and a pane resize each clear the
  range, because in every one of those cases the cells the endpoints name hold different text. A wheel gesture
  that moved nothing is not a scroll and keeps the selection. `fm_close()` clears it, and `main.c` drops its
  drag flag in the same breath.
- **One selection at a time in the window.** A press in the preview clears the session's highlight and a press
  in a terminal pane clears the pane's, so `selection_copy()`'s precedence (preview first, then the session) is
  a fallback and never a guess — and its log line names the owner that served the bytes.
- **The preview's click pair is kept apart from the LIST's.** `fm_click_pos` compares a view POSITION (a second
  click on the same ENTRY opens it); `pv_cell()` compares a pane CELL. The two numbers are not comparable, and
  merging them would let a click in one pane break the other's gesture. Only the LEFT button selects text — a
  right or middle click in that pane still only moves the keyboard, exactly as before step 3b.
- **A release ends the drag BEFORE every hit test** in `ptr_button()`: an early return in the bar, band or pane
  branch would leave a drag live, and the next motion event would keep extending a range under a pointer the
  user already let go of.
- **`cap == 0` writes nothing at all** — not even one byte before the caller's buffer.
  `fm_preview_selection_text()` guarded its first store with `if (cap) out[0] = 0;` but its terminating store is
  `out[n < cap ? n : cap - 1] = 0;`, which for `cap == 0` is `out[-1]` — ASan: WRITE of size 1 one byte before
  the caller's array. No caller in the tree passes 0, which is why the guard went in the function and not in the
  callers: it is a public entry point in the header. Fix: `if (!cap) return 0;` before any write. `make asan`
  cannot find this on its own — the suite never passes 0 — so the check pins it by putting the buffer at the
  SECOND byte of a two-byte array (`pad + 1`), so `out[-1]` is `pad[0]` and the plain build sees the underflow.
  The `cap_zero_unguarded` mutant fails exactly that one check. The same lesson applies to step 4's
  `session_list()`.
- **A row longer than the pane copies only what the pane holds.** A 130-character line in a 30-column pane gives
  a row span of `2..29` and a copy of **28 bytes** — the preview CUTS and never scrolls sideways, so the tail is
  in no cell to select. README and ISSUES say that, and the `pvsel` group pins the span and the exact byte count
  so the sentence cannot drift back.

### 4. Ask before quitting when something is still running — row DONE (s120/s121, confirmed by the user); WINDOW box DONE (s123/s124, awaiting their eyes)

Today `toplevel_close()` sets `W.running = 0` immediately, closing the last tab does the same, and both close
the pty master and `kill(child, SIGHUP)` — whatever was running dies with no warning.

**The predicate is measured, not assumed** (`.scratch/s117/fgprobe2.c`; the first attempt, `fgprobe.c`, was
wrong because it never answered the shell's startup queries and so hosted a stalled shell — see Key Decisions).
`tcgetpgrp()` on the pty **master** returns its foreground process group, and Kai-Term never calls `tcsetpgrp`
(0 hits in `src/`), so the shell owns that value:
- idle at the prompt ⇒ `tcgetpgrp(mfd) == child`; `sleep 6` ⇒ `!= child`; `sleep 6 | cat` ⇒ `!= child` — in
  bash, zsh **and** fish.
- a job backgrounded with `&` ⇒ **idle** (the shell keeps the foreground group): the gate protects a
  foreground command, not a daemon. Document it as a limitation.
- a session started with `-e COMMAND` ⇒ **idle**, even for a pipeline. `pty_spawn()` runs it as `sh -c`, so the
  command owns the foreground group and `fg == child` (re-measured s121, `.scratch/s121/espawn`: `sleep 30` and
  `sleep 30 | cat` answer idle for the whole 900 ms, with `/proc/<child>/comm` proving the command is alive).
  README and ISSUES say so; `confirm_quit = always` is the setting for a `-e` window.
- after the shell exits, **bash's pty answers `tcgetpgrp` with 0**, so a naive `fg != child` calls a dead
  session "running" and the window asks whether to kill nothing (zsh/fish left the old value, idle by luck).
  Hence the pure `fg_busy(fg, child)`, which refuses `fg <= 0` and `child <= 0`, and `pty_fg_busy()`, which
  refuses `mfd < 0`.
- fish was slow to converge here (IDLE at +1.5 s after the command was typed, BUSY by +4.5 s); bash and zsh
  read BUSY at +1.5 s. The state converges in seconds and the gate reads it at close time, so the lag is not a
  correctness problem — but it is why "asked and nothing was running" is a possible log line.

**Design:** the decision is pure and headless (`fg_busy`, `mux_running`, `confirm_message` in mux.c so the
WORDING is a checkable decision), the syscall wrapper is `pty_fg_busy()` in pty.c, the key is
`confirm_quit = running|always|never` (default `running`, accepting running/auto, always/1/on/yes/true,
never/0/off/no/false, with a warn-and-keep line for anything else), and the question is one row of chrome laid
out by layout.c and drawn by render.c with the tab bar's font and colours. Stages, each left green: (A) config +
pure decisions + checks + docs, (B) the notice row's geometry, (C) the render pass, (D) main.c's gate and modal
keys.

**In the tree** (committed in s121; stages A, B, C and D all green): `CONFIRM_NEVER/RUNNING/ALWAYS`,
`Config.confirm_quit`, `fg_busy()`, `pty_fg_busy()`, `mux_running()` (returns 0-based tab indices; the message
adds 1), `confirm_message()` with `CONFIRM_QUIT`/`CONFIRM_CLOSE_TAB`, the config default and parser branch, the
`confirm_quit = running` template block, the README settings row, `ns == 37`, and `build/pty.o` added to
test_vt, dbg, test_render and their asan rules — `mux_running()` calls `pty_fg_busy()`, so without it
`bin/test_vt` fails to LINK with `undefined reference to pty_fg_busy`; pty.c is pure libc and the suites never
call it, because a test hands a Tab an `mfd` of -1, which answers idle before any syscall. The seam the header
declares is implemented: `LayoutReq.notice_on`/`notice_text` feed `Layout.notice_on` + rect +
`notice[NOTICE_MAX]`, and `Chrome.notice_bg`/`notice_fg` are derived in `render_chrome()` from `selection_color`
and the default ink.

**The checks that exist** (all in `tests/test_vt.c`, all non-vacuous — see the s120 journal entry for the A/B
and the 19 mutants): the config group (default, every spelling of all three values fed AFTER the opposite value
so a word the parser does not know cannot pass, and warn-and-KEEP), `fg_busy()` (idle, busy, `fg == 0`,
`fg == -1`, `child <= 0`, `child == -1`, pid 1), `pty_fg_busy()` (no master fd, NULL, a closed fd), a REAL pty
with a hand-built process tree (`.scratch/s120/fgprobe3.c` is what the harness copies: a fork that does what
`pty_spawn()`'s child does, takes the foreground group for itself, then hands a command's own group the
foreground on a byte from a control pipe — idle at the prompt, busy for the command, and after the "shell"
exits `tcgetpgrp` answers **0** on a real tty, which is the dead-session guard pinned against a real terminal
and not only against a written 0), `mux_running()` (0/1/many, non-adjacent tab indices, `max` caps the LIST and
not the COUNT, a session that ended drops out), and `confirm_message()` (every branch's exact text, the
one-based numbers, the four-number cap with `, ...`, a NULL index list keeping the count, a small `cap`
returning what it WROTE and staying terminated, `cap == 0` and a NULL buffer touching nothing).

**The rules stages B, C and D added, all of them checked:**
- **The row costs ONE grid cell row and no gap** (the bar costs `ch + gap` because its ITEMS need separating; a
  full-width row does not — costing the gap would take a SECOND row from a window whose height is a whole number
  of rows: measured 986 px grid area, 17 px cell, 58 rows -> 57 with the row, 56 with row+gap). It is reserved
  in `layout_compute()` BEFORE the file-manager band is measured, so the band's PERCENT applies to the grid area
  that is LEFT (`b_band_sees_full_grid` fails 14 checks, most of them pre-existing fm geometry checks — that is
  the point).
- **The terminal wins over the question**, exactly as it wins over the bar and the band: a grid area that cannot
  keep one row after paying gets `notice_on == 0` and the window closes without asking. That is a decision, and
  `Layout.notice_on` says what was actually drawn.
- **The row sits at the BOTTOM** so the work being asked about stays visible above it (`b_at_top` fails 7).
- **The text is cut to the row's columns IN layout.c** (`clip_utf8`), so the clipping is a checked decision and
  the renderer draws exactly what the geometry decided. `snprintf` into a fixed buffer can split a UTF-8
  sequence, so `trim_partial_utf8()` undoes a byte bound that landed inside one — for the notice row AND for
  every bar label whose OSC title is longer than `BAR_LABEL_MAX`, which is a claim the old "never
  mid-sequence" check did not actually cover.
- **The field is one quad at the end of batch A, its text in batch B** with the bar labels and the same
  per-column bound; `maxq` grows by `NOTICE_MAX + 1` when the row is on (a staging budget that forgets a pass
  is a buffer overrun waiting for a wide window). Colours come from `render_chrome()`, never a literal.
- **Three close paths are gated, one deliberately is not:** `toplevel_close()`, the `key_close_tab` binding and
  the bar's middle click. The SIGCHLD / closed-pty path that reaps a session whose process already exited must
  NEVER ask — there is nothing left to protect. A second close request while a question is up is ignored, not
  stacked.
- **The question has the keyboard:** the modal block sits in `send_key()` before EVERY binding, the file
  manager's keys and the PTY paths, so no chord, no band key and no byte reaches the shell while it is up.
  `Enter`/`y` confirms, `Esc`/`n` cancels, anything else is swallowed and logged at debug level. Returning 0
  means a held key never repeats an answer.
- **A question about a TAB belongs to an index, and `mux_close()` shifts every later tab down.** If the session
  it asked about closes on its own while the row is up, answering YES would close a DIFFERENT shell — so
  `tab_close()` drops the question whenever it closes that index or an earlier one (the same rule the fm `cd`
  follow already obeys).
- **YES for a window closes every session named in the sentence**, not just the visible one — that is what the
  count promises.

**Not in the tree:** nothing step 4 needs, and nothing it owes. Both forms of the question are confirmed by the
user in their own window — the row (s124) and the box (s125).

**Superseded, and deliberately kept:** `confirm_message()`'s `CONFIRM_QUIT` branches are no longer what a window
shows — `confirm_warning()` took that over — and `main.c` never calls it with `CONFIRM_QUIT` (a source scan pins
that; the `main_tab_wording` mutant kills on it). They stay because the function is a public entry point whose
safety contract (`cap == 0`, a NULL buffer, a small cap) is checked through them, and the header and the test group
carry a NOTE saying so. Deleting them is a legitimate cleanup for a later session; it is not a bug.

**Exact next action:** step 6 (the visual bell), then step 7 (plain-click URLs). Steps 4 and 5 are finished; do not
re-open them.

### 5. Middle-click paste (opt-in, off by default) — DONE (s125/s126, `f3c812a`, pushed); owed to the user's eye

**Task block, s125 — objective, measured starting point, planned steps, expected outcome.**
MEASURED starting point (read from the tree at `dc36c46`, not assumed):
* `ptr_button()` in `src/main.c` is a chain of early returns, in this order: (1) a preview-pane drag ending on
  release; (2) the confirm-quit box owning the pointer; (3) a tab-bar item (`btn == 1` there CLOSES the
  session, gated by `confirm_gate_tab`); (4) the file-manager band (`fm_pointer_select`); (5) a click in a
  non-visible pane switching to that session; (6) the application-grabbed-the-mouse branch
  (`cur_term()->mouse_mode & (MOUSE_PRESS|MOUSE_DRAG)` → `mouse_send`, return); (7) `btn == 0` only — the left
  button drives selection, the double/triple gesture and the click-moves-cursor synthesis.
* **A middle click in a plain terminal pane does nothing today**: stage 7 tests `btn == 0`, so `btn == 1` falls
  out of the function. That is the whole gap step 5 fills.
* `clipboard_paste()` (`src/main.c:3081`) is the paste entry `Ctrl+Shift+V` already uses: it needs `W.data_dev`, no
  paste already in flight, and `sel_offer && sel_offer_text`, where `sel_offer` is the offer the compositor last
  sent to `dd_selection` — i.e. **whatever the clipboard holds, including another application's text**. So the
  paste half of this feature exists and is already the s98-tested path; step 5 adds only the gesture that calls it.
* `ptr_shift_held()` is used in exactly ONE place today (the click-moves-cursor gate at `src/main.c:2690`); the
  mouse-report branch does NOT consult Shift. Recorded so a later "Shift should bypass mouse reporting" change is
  not smuggled in as part of this one.
* Config: **37** settings names, pinned by `CHECK(ns == 37 && nb == 24, ...)` at `tests/test_vt.c:3088` and by
  37 `key =` lines in `config/kai-term.conf`; README's settings table is diffed against the parser by
  `readme_table_names_of()` + `names_diff()`. Bool keys go through `cfg_bool` (see `copy_on_release` at
  `src/config.c:511`, `click_selects_word` at 524).
Objective: a middle click pastes the clipboard, **opt-in and off by default**, without changing what any other
button or any other surface already does.
Planned steps, each left green: (A) the precedence as ONE pure decision — `PtrPress` + `ptr_press_action()` in
`src/vt.c`, next to `mouse_sgr_button()` and `term_wants_key_mouse()`, because "what does this gesture mean" is a
rule a test can pin and main.c is in no test binary; (B) the key `middle_click_paste` (default 0) — Config field,
default, parser branch, template line, README row, `ns` 37 -> 38; (C) main.c computes the flags once and dispatches
on the action, so the chain's order is the same order a check can flip; (D) checks — the precedence table headless,
source scans for the main.c wiring, an A/B and mutants, README/ISSUES, both builds, every suite, asan.
Expected outcome: with `middle_click_paste = 1`, a middle press in a terminal pane pastes the clipboard and `-d`
logs it; with the default `0` nothing at all changes; a middle click on a tab item still closes that session (and
still asks when it is busy), one in the file-manager band still selects an entry, one while the box is up still
answers nothing, and one while an application holds the mouse is still reported to it.

**Progress, s125 — stages (A), (B) and (C) are in the tree; (D) is what is left.**
* (A) `PtrPress` + `ptr_press_action()` — declared in `include/kai-term/kai-term.h` next to `mouse_sgr_button()`,
  implemented in `src/vt.c`. Actions: `PTR_NONE / PTR_DIALOG / PTR_BAR / PTR_FM / PTR_REPORT / PTR_SELECT /
  PTR_PASTE`. The order it encodes is the chain main.c already has: box > bar(press only) > band > application
  that asked for the mouse > pane. `PTR_NONE` is 0 and that is safe here (unlike `CONFIRM_NONE` in s122) because
  nothing uses this value as a truthiness test — main.c compares it against named actions.
* (B) the key `middle_click_paste`: `Config` field + comment, `config_default` = **0**, an `apply_setting` branch
  through `cfg_bool`, a commented template line in `config/kai-term.conf`, a README settings row, and the pinned
  count `ns == 37` -> **38**.
* Checks added to `tests/test_vt.c`: the config group (default OFF first — the default *is* the feature; every
  spelling of both values fed AFTER the opposite one) and a 16-case precedence table for `ptr_press_action`.
  Two of my own first-draft checks were wrong and were replaced, both caught by running them rather than by
  reading them: (i) I wrote a warn-and-KEEP case for a bool, but `cfg_bool()` reads only 0/off/no/false as off
  and ANY other word as on — warn-and-KEEP is `confirm_quit`'s rule for an enum, so the honest check now pins
  `= maybe` -> ON, started from OFF so it cannot pass vacuously; (ii) I asserted a bar RELEASE is nobody's,
  but main.c's bar branch is `bi >= 0 && pressed`, so a LEFT release falls through and ends the selection today —
  the table now says press=BAR, left-release=SELECT (unchanged behaviour), middle-release=NONE.
  MEASURED: `make -j8` 0 warning lines; test build 0 warning lines; `./bin/test_vt` **1446 checks, 0 failures**
  (1413 -> 1446: the config group, the precedence table, the template row).
* (C) `ptr_button()` now asks the decision and gates every branch on it (s125, in the tree, unverified at the
  time it was written — see the s126 entry). `bi`/`fm_hit`/`cur_term()` are computed once above the chain; the
  box, bar, band, report, paste and select branches each read `act`. The answer is asked a SECOND time after the
  "click in another pane switches session" branch, because that switch can land the pointer on the session that
  asked for the mouse (a split with `nano --mouse` beside a shell) and the first answer was computed from the
  session that was active when the event arrived. `clipboard_paste()` took a `why` argument and returns 0/1, and
  every early return now says why in the log — "the middle click is broken" has to be answerable.
* NEXT (D): run `.scratch/s125/mutate.py` (written, never run), README's mouse section, ISSUES, both builds,
  every suite, asan.

### s126 — step 5 stage (D): the harness, the documents, the suites

Objective: finish step 5 the way steps 3, 3b and 4 were finished — the new checks proven non-vacuous by mutation,
the documents matching the code, every suite and the asan build re-measured on the SAME tree. Planned steps: (1)
re-measure both builds and all four suites on the tree as found, so the s125 numbers are not carried forward on
trust; (2) run `.scratch/s125/mutate.py` and record which mutants die and which survive; (3) README's mouse /
middle-click prose and ISSUES; (4) `make asan`; (5) commit. Expected outcome: a step-5 box that can be committed
with measured numbers, and a list of what still belongs to the user's eye (a real middle click pasting in a real
window is not headless-checkable).

FOUND: the tree is one stage ahead of the page — (C) was written after the s125 journal entry was made and the
entry said it was next. Corrected above. `git status` shows 9 modified files, nothing committed since `ca9f58d`.
MEASURED on the tree as found: `make clean && make -j8` exit 0 with **0** warning lines; `make bin/test_vt
bin/dbg bin/test_fm bin/test_render` exit 0 with **0** warning lines; `./bin/test_vt` **1446 checks, 0 failures**;
`./bin/dbg` **MODEL OK (0 problems)**; `./bin/test_fm` **384 checks, 0 failures**; `./bin/test_render` **529
checks, 0 failures** (524 -> 529: the five main.c wiring scans). The s125 entry's 1446 is confirmed, and the
529 is new since it.

MEASURED — `.scratch/s125/mutate.py` (written in s125, never run; run now, one mutant added): **14 mutants, 14
killed, 0 survived**, and the `control_noop` staging tree stays green (1446 / 529 checks, 0 failures) — the
harness prints "SURVIVED" for the control by construction, which is what a control must do, not a finding.
Failures are `test_vt / test_render`:
`paste_ignores_optin` 1/0 (the opt-in IS the feature) · `paste_before_bar` 4/0 · `paste_on_release` 2/0 ·
`dialog_after_bar` 1/0 · `select_only_press` 2/0 (a left RELEASE stops being the selection's) ·
`no_null_guard` — `test_vt` **crashes**, exit -11, 0 FAIL lines: that one is pinned by a segfault, and a crash
counts as killed only if the harness says so (it does) · `cfg_default_on` 2/0 (the default check AND the template
check) · `cfg_no_branch` 9/0 · `tpl_ships_on` 1/0 · `main_no_paste_branch` 0/2 · `main_no_optin_feed` 0/1 ·
`main_ungated_report` 0/1 · `main_second_paste` 0/1 · `main_no_reask` 0/1 (added this session — it kills the
"main.c asks the decision twice" scan).
The split is the point: a `src/vt.c` mutant dies only in `test_vt` and a `src/main.c` mutant only in
`test_render`. The table and the wiring are two separate claims and each has exactly one suite watching it.

MEASURED after the documents: `make asan` exit 0, **0** warning lines, **0** sanitizer/UBSan reports, the same
four counts (1446 / MODEL OK / 384 / 529). `KAI_TERM_TEST_NO_WINDOW=1 sh tests/cli_test.sh ./bin/kai-term` →
**CLI: 99 checks, 0 failures, 13 skipped** (the windowed group not re-run this session — it maps ~15 real windows;
ask the user). README gained a "Middle-click paste" subsection (the precedence as a table, clipboard-not-primary,
press-not-release) and a Features line; ISSUES gained a "Selection / clipboard" limitation and its re-measured
counts paragraph now reads s126.

**The rules step 5 added, and where each is checked:**
- **The order is ONE decision in `src/vt.c`, and `main.c` asks it.** `main.c` computes the flags (it owns the
  geometry, the model and the config) and every branch of `ptr_button()` is gated on the answer. A branch that
  re-derives its own condition can drift from the table silently — `main_ungated_report` is that mutant and the
  `act == PTR_` count is what catches it.
- **The answer is asked a SECOND time after the pane-focus switch**, because clicking another pane can land the
  pointer on the session that asked for the mouse (a split with `nano --mouse` beside a shell). The first answer
  was computed from the session that was active when the event arrived; using it after the switch reports to the
  wrong program. `main_no_reask` kills the `ptr_press_action` count of 2.
- **The opt-in is checked in the decision, not in `main.c`.** `main.c` feeds `conf.middle_click_paste` as a flag,
  so "off changes nothing" is one row of the table (`paste_ignores_optin`) and not an `if` buried in an event
  handler.
- **`PTR_NONE == 0` is safe here only because nothing tests it for truthiness** — `main.c` compares against named
  actions. Contrast `CONFIRM_NONE` in s122, where 0-as-true was a real bug. Adding a truthiness test on an action
  is the change that would break that.
- **Only the PRESS pastes** (a release would paste the same text twice), and **one paste path exists**:
  `clipboard_paste(why)` is shared with `Ctrl`+`Shift`+`V` and every early return says why in the log. A middle
  click that drove `start_paste()` itself is `main_second_paste`.
- **The paste is deliberately NOT gated on a pane hit.** `PTR_PASTE` means "no surface above claimed this click",
  which includes the `padding` margin and the gaps between panes — the same area where a left click already
  selects text. Gating it on `layout_hit_pane()` would make a middle click in the margin do nothing while a left
  click there still selects, which is the kind of inconsistency this project keeps hitting.
- **Not pinned, and on the user's-eye list:** a real middle click reaching the shell with the option on (no
  headless check can inject a button), and the `W.selecting = 0` inside the paste branch — a middle press while a
  left drag is held ends the drag, and the left release still finalizes the selection. No check covers that line.

DONE: step 5 committed as `f3c812a` ("Middle click: an opt-in paste, with one owner per surface") and PUSHED —
verified from git, not from memory: `git log --oneline origin/main -1` prints `f3c812a` and `git status -sb`
shows `main...origin/main` with nothing ahead. `.scratch/` stayed untracked and was never `git add`-ed.

NEXT: step 6, the visual bell. Its task block is below.

**Task block, s126 — step 6, the visual bell. Objective, measured starting point, planned steps, expected
outcome.**
MEASURED starting point (read from the tree at `26b20d9`, not assumed):
* `exec_c0()` in `src/vt.c`: `case 0x07: break;   /* BEL: nothing to do here */` — that is the whole of today's
  BEL handling in the ground state and inside a CSI. BEL is ALSO the ST terminator in three string states
  (`src/vt.c:3858` OSC, `:3876` APC, `:3897`), and those never reach `exec_c0` — so "a BEL rings" and "a BEL ends
  a sequence" are two rules that must not be merged, and the second must not ring.
* The vt.c → main.c hand-off flag already has a shape: `Term.colors_dirty` (+ the two old colours), zeroed by
  `term_init()`'s memset with the rest of `Term`, raised by the parser, consumed by the pty-read branch.
* `Tab` (the mux struct in the header) is where per-session window-facing state that must survive `mux_close()`
  shifting the array lives: `attention`, `cd_req`, `int_dead`, `int_gen`.
* `render_draw()`'s batch A order is: every pane's cell backgrounds → the fm band's backgrounds → the bar items →
  the notice field → the dialog; then the pictures; then batch B (selection bands, glyphs, the cursor quad, bar
  labels, the border). The staging budget is `maxq` at `src/render.c:1203`. Later quad wins — there is no GL
  blending — so "over the backgrounds, under the text" means "appended to batch A".
* `mix_c(a, b, k100)` (`src/render.c:660`) is the only colour mixer and `render_chrome()` is where a configured
  colour becomes a vertex colour. `mono_ms()` (`src/main.c:259`) is the CLOCK_MONOTONIC ms clock.
* The frame timer `tfd` fires every 8 ms and calls `blink_update()` + `fm_cd_tick()`; the redraw happens only when
  `W.dirty || W.resized`. So anything that changes on a schedule needs a tick that sets `dirty` while it is live —
  `blink_update()` is the precedent, and its rule ("the tick only flips state and sets dirty; never draw from a
  listener") applies here.
* Config: **38** settings names; an enum-valued key is parsed warn-and-KEEP (`confirm_quit`), which is NOT what a
  bool does (`cfg_bool` reads only 0/off/no/false as off — the s125 lesson).
Objective: a BEL the user can SEE, on by default, with no new dependency, no GL blending and no per-frame cost
when nothing rang.
Planned stages, each left green: **(A)** `Term.bell_rang` set by `exec_c0`'s BEL + the pure `bell_strength(now,
rang, dur)` and the `BELL_FLASH_MS` / `BELL_MIX_MAX` rules in the header, checked in `tests/test_vt.c` (a BEL sets
the flag; a BEL that terminates an OSC/APC does NOT ring; the strength ladder). **(B)** the key `bell = visual|off`
(default **visual**), enum parse with warn-and-keep, template line, README row, `ns` 38 -> 39. **(C)** `render_bell
(bg, fg, strength)` in `src/render.c` (pure, checked by `tests/test_render.c`) + ONE quad per flashing pane
appended to batch A + the `maxq` budget. **(D)** `main.c`: `Tab.bell_ms`, the pty-read hand-off, `rend.now_ms` /
`rend.bell_ms[]` filled per frame, and a `bell_tick()` on the frame timer that keeps the window dirty while a flash
is live. **(E)** README + ISSUES, both builds, every suite, asan, and a mutant harness.
Expected outcome: with the default `bell = visual` a BEL washes that pane's field toward the ink and fades it out
over `BELL_FLASH_MS`, text and pictures staying readable over it; `bell = off` draws nothing at all; a bell in a
session that is NOT drawn flashes nothing (the bar's `*` stays the mark for that); in side-by-side view only the
pane whose session rang flashes. What will still be owed to the user's eye: that it reads as a bell and not as a
glitch — no headless check can see a pixel.

**Progress, s126 — (A), (B) and (C) are in and green. (D) is NOT: the feature cannot draw yet.**
* (A) `Term.bell_rang` — set by `exec_c0()`'s `case 0x07`, the same one-way hand-off shape as `colors_dirty`.
  The pure `bell_strength(now, rang, dur)` in `src/vt.c` plus `BELL_FLASH_MS` (250) and `BELL_MIX_MAX` (25) in
  the header. `Tab.bell_ms` added (the ring's CLOCK_MONOTONIC ms, 0 = never) — it lives with `attention` because
  it has to travel when `mux_close()` shifts the array.
* (B) `bell = visual|off`, default **visual** (the user's answer to question 6), enum parse with warn-and-KEEP
  in both directions, template line, README settings row, `ns` 38 -> **39**.
* (C) `render_bell(bg, fg, strength)` in `src/render.c` (pure, next to `mix_c`), and **pass 1b** in
  `render_draw()`: at most ONE quad per drawn pane, appended to batch A after every pane's cell backgrounds and
  before the band/bar/notice, so it is over the paper and under the bands, glyphs, cursor and pictures. The rect
  is the PANE's own (`lay->pane[p].x/y/w/h`), not the grid's, so the leftover pixels at a pane's right/bottom
  flash with the cells — s101's margin rule. `maxq` grows by `+ lay->npane`.
* Checks: `tests/test_vt.c` — the BEL group (ordinary output rings nothing; a BEL rings; a 0x07 that TERMINATES
  an OSC / APC / DCS does NOT, with a second check proving that OSC really was parsed as a title so the first
  cannot pass by the sequence being abandoned; a BEL inside a CSI still rings, the s97 rule; three BELs are one
  event) + the `bell_strength` ladder (full at the ring, 500 at half, >0 at the last ms, 0 at the deadline, 0 on
  a clock that went backwards, 0 for never-rang / negative / dur 0 / dur < 0, and a sweep proving 0..1000 and
  monotone). `tests/test_render.c` — a `group_bell_color()` (11 checks: 0 and negative are the paper, 1000 is
  exactly `BELL_MIX_MAX`, never the ink, clamped above 1000, half strength is half the mix, monotone over the
  whole range, a LIGHT paper flashes downward too, alpha stays opaque, ink == paper flashes to that colour) and
  four source scans (the draw loop asks `bell_strength` once, calls `render_bell` and writes no literal, names
  `BELL_FLASH_MS` once, and the budget pays `+ lay->npane`).
* MEASURED: `make -j8` exit 0, **0** warning lines; the test build exit 0, **0** warning lines; `./bin/test_vt`
  **1483 checks, 0 failures** (1446 -> 1483); `./bin/dbg` **MODEL OK (0 problems)**; `./bin/test_fm` **384
  checks, 0 failures**; `./bin/test_render` **544 checks, 0 failures** (529 -> 544). One warning appeared and
  was fixed on the spot: a loop local named `s` in the new sweep shadowed an outer `s` (`-Wshadow`) — the test
  build was re-measured after the rename, not assumed clean.
* **FOUND, and the reason this entry exists:** the journal was six file changes behind HEAD (README, conf, the
  header, config.c, render.c, vt.c, both test files all modified with no entry after each). That is the project's
  first critical rule; the entry was written before stage (D) was started, and the tree was re-measured to get
  the numbers above rather than carrying the s125 ones forward.
* **The honest state of the feature: it cannot draw yet.** `Tab.bell_ms` is never written and `Render.now_ms` /
  `Render.bell_ms[]` are never filled, so every `bell_strength()` call the renderer makes gets 0. Do not describe
  step 6 as working, and do not hand it to the user to look at, until (D) is in.
* **NEXT (D), in `src/main.c`:** (1) the pty-read hand-off — `if (tb->term.bell_rang) { tb->term.bell_rang = 0;
  tb->bell_ms = mono_ms(); }` next to the `colors_dirty` block, marking the window dirty only when that session
  is actually drawn; (2) `redraw()` fills `rend.now_ms` and `rend.bell_ms[i]` from the Tabs, indexed by TAB index
  like `terms`; (3) `Render.bell_on` from `conf.bell`, and the renderer asks `bell_strength(now, ms, r->bell_on ?
  BELL_FLASH_MS : 0)` — the duration-0 rule is already checked, so "off draws nothing" needs no new branch and no
  new number; (4) `bell_tick()` on the 8 ms frame timer: dirty while any DRAWN session is flashing, plus exactly
  one more frame when the last flash ends (without that final frame the screen keeps the last flash forever —
  strength 0 alone never asks for a redraw). Then (E): the wiring scans, a mutant harness, ISSUES, both builds,
  every suite, asan.

**Progress, s126 — (D) is in. The feature is now wired end to end; (E) (harness, ISSUES, asan) is what is left.**
* `Render.bell_on` is a `render_init()` argument, fed `conf.bell == BELL_VISUAL` in `main.c` — the same route the
  dead `cursor_color` needed, and `src_call_passes_arg` pins it. The renderer asks
  `bell_strength(now, ms, r->bell_on ? BELL_FLASH_MS : 0)`: `bell = off` needs no branch and no second number,
  because the duration-0 rule is already a checked case of `bell_strength`.
* `main.c`: the pty-read hand-off (read `term.bell_rang`, clear it, stamp `Tab.bell_ms` with `mono_ms()`, mark the
  window dirty only when `layout_pane_of_tab() >= 0`, and log which of the two the config asked for); `redraw()`
  fills `rend.now_ms` + `rend.bell_ms[i]` indexed by TAB index; `bell_tick()` runs on the 8 ms frame timer next to
  `blink_update()`/`fm_cd_tick()`.
* **The one non-obvious rule (D) added, and the reason `W.bell_live` exists:** while a flash is live the tick must
  mark the window dirty every frame or the fade is one frozen colour, AND on the frame where the last flash ends
  it must ask for **one more** frame — at that moment `bell_strength()` answers 0, so nothing else would ever
  redraw and the pane would keep the last half-faded wash forever. Checked only as a text pin
  (`live || W.bell_live` appears once); its behaviour belongs to the user's eye.
* **Found while wiring (D), fixed:** `mux_open()` reset `attention`, `cd_req`, `int_dead` and `int_gen` for a
  reused slot but not the new `bell_ms` — a new tab could have come up mid-flash of a BEL rung by a shell that was
  already gone. `t->bell_ms = 0;` added, and a scan pins that `src/mux.c` names `bell_ms` once.
* Seven more source scans in `tests/test_render.c` pin the wiring: `conf.bell` reaches `render_init`, `r->bell_on`
  is stored once and consulted once, `redraw()` fills `rend.now_ms` and `rend.bell_ms[`, the pty path reads AND
  clears `term.bell_rang`, `main.c` asks `bell_strength` exactly once (no second copy of the fade arithmetic),
  `bell_tick` is defined and called, and `mux.c` clears the slot.
* MEASURED after (D): `make -j8` exit 0, **0** warning lines; the test build exit 0, **0** warning lines;
  `./bin/test_vt` **1483 checks, 0 failures**; `./bin/dbg` **MODEL OK (0 problems)**; `./bin/test_fm` **384
  checks, 0 failures**; `./bin/test_render` **551 checks, 0 failures** (544 -> 551: the seven wiring scans).
* NEXT (E): `.scratch/s126/mutate.py` (the harness does not exist yet), README's Features line, ISSUES, `make
  asan`, commit. Expect at least one honest SURVIVOR: `render_draw()`'s pass itself is in no test binary, so a
  mutant that makes the quad draw nothing — or draw at the wrong rect — cannot be caught headlessly. Say so in the
  entry rather than pretending the count covers it.

**Progress, s127 — (E) in progress: the harness ran, and it found three real gaps.**
`.scratch/s126/mutate.py` (31 mutants + the `control_noop` staging tree, built AND run in a copy of the tree
because the scans read `src/*.c` by relative path): **24 KILLED, 7 SURVIVED** (the control survives by
construction, so 6 findings). Per mutant, `test_vt` / `test_render` failures:
`bell_never_rings` 3/0 · `bell_counts_not_levels` 1/0 · `bell_rings_on_osc_st` 3/0 · `bell_rings_on_apc_st` 2/0 ·
`strength_no_deadline` 1/0 · `strength_step_not_fade` 1/0 · `strength_no_clock_guard` 1/0 ·
`strength_never_rang_flashes` 1/0 · `cfg_default_off` 2/0 · `cfg_no_branch` 8/0 · `cfg_warn_clobbers` 1/0 ·
`tpl_ships_off` 1/0 · `bell_color_no_clamp` 0/1 · `bell_color_full_ink` 0/3 · `render_off_ignored` 0/1 ·
`render_no_budget` 0/1 · `main_no_handoff` 0/1 · `main_no_clear` 0/1 · `main_no_now_fill` 0/1 ·
`main_no_bellms_fill` 0/1 · `main_second_fade` 0/1 · `main_no_tick_call` 0/1 · `main_tick_no_last_frame` 0/1 ·
`mux_no_slot_clear` 0/1. Again the split is the point: a `src/vt.c`/`src/config.c` mutant dies only in `test_vt`,
a `src/render.c`/`src/main.c`/`src/mux.c` mutant only in `test_render`.
The six survivors, each with its verdict:
* `bell_color_ignores_strength` — **a real gap, fixed.** Dropping `if (strength <= 0) return bg;` changed no
  check, because the group's only negative input was `-7` and `BELL_MIX_MAX * -7 / 1000` is **0** in C, so the
  mutant mixed by 0 % and still returned the paper. A check on `-1000` separates them (without the guard the
  percent goes negative and `mix_c` clamps every channel toward BLACK — a colour nobody asked for). Added to
  `group_bell_color()`; the mutant now dies there.
* `main_live_scans_all_tabs` — **a real gap, fixed by moving the decision.** "Only a session the user can SEE
  keeps the window redrawing" lives in `main.c`'s `bell_live()`, which no test links, so scanning every tab
  instead of every pane changed nothing measurable. That is the same shape as `mux_running()`, which lives in
  `mux.c` for exactly this reason — so the rule moves there as `mux_bell_live(mux, layout, now, dur)` and
  `main.c` keeps only the clock and the config. Checked in `tests/test_vt.c`, which already links `mux.c` and
  `layout.c`.
* `main_dirty_ignores_drawn` — **a real gap, pinned.** The pty hand-off's `layout_pane_of_tab(...) >= 0` gate
  (mark the window dirty only for a session that is drawn) had no scan; one is added next to the other bell
  wiring scans.
* `strength_no_dur_guard` — **honest survivor, and the guard is unreachable.** With the clock guard in place,
  `elapsed >= dur_ms` already answers 0 for every `dur_ms <= 0` (elapsed is never negative there), so the
  division can never be reached and no input can distinguish the guard from its absence. The BEHAVIOUR
  (`dur 0 → no flash`) is pinned; the guard is defensive. Recorded here so nobody "tests" it with a fake case.
* `render_draws_nothing` and `render_wrong_surface` — **expected survivors, owed to the user's eye.** The pass
  asks `bell_strength()` and `render_bell()` correctly and then emits nothing / emits the quad over the tab bar
  instead of the pane; `render_draw()` is in no test binary, so no headless check can see either. That is the
  limit of the wiring scans and it is stated in ISSUES, not papered over.
* `main_no_bell_on_feed` first survived as a **harness artifact**: the mutant's own comment said
  "ignores conf.bell", and `src_call_passes_arg()` scans lines with `strstr` without skipping comments, so the
  scan found the string in the comment. The mutant text no longer names the key; re-run below.

**MEASURED — the re-run after those three fixes (`.scratch/s126/mutate.py`, now 32 mutants + the control):**
**29 KILLED, 4 SURVIVED**, and the control is one of them by construction. The four:
`control_noop` (the staging tree is green — 1498 / 555 checks, 0 failures — which is what makes "KILLED" mean
something) · `strength_no_dur_guard` (unreachable guard, see above) · `render_draws_nothing` and
`render_wrong_surface` (the draw pass is in no test binary). Every mutant that was a real gap now dies:
`bell_color_ignores_strength` 0/1 · `main_no_bell_on_feed` 0/1 · `main_live_scans_all_tabs` 0/1 ·
`main_dirty_ignores_drawn` 0/1 · and the new `muxlive_scans_all_tabs` (the same rule mutated where it now lives,
in `src/mux.c`) 2/0 — it dies in `test_vt`, which is exactly where the rule is now checked.
MEASURED after the three fixes: `make -j8` exit 0, **0** warning lines; the test build exit 0, **0** warning
lines; `./bin/test_vt` **1498 checks, 0 failures** (1483 -> 1498: the 15 `mux_bell_live` checks);
`./bin/dbg` **MODEL OK (0 problems)**; `./bin/test_fm` **384 checks, 0 failures**; `./bin/test_render` **555
checks, 0 failures** (552 -> 555: the negative-strength colour check and the two new scans).
One of my own new checks was wrong and was caught by running it, not by reading it: the "an undrawn session keeps
no frame alive" case first left the DRAWN pane inside its own flash window, so it answered 1 for the wrong reason
and passed vacuously. It now expires the drawn pane's ring before asking about the hidden one.

The user: "I actually hate middle click paste, but we could add it as an option (off by default)". Their answer
to question 5 settles the design: **it pastes the CLIPBOARD**, so no `wl_primary` work is needed at all and that
whole sub-task is dropped. Middle press pastes from the clipboard offer; ordering in `ptr_button()` matters —
the bar's middle-click-closes-a-tab, the file-manager band, and an application that grabbed the mouse all come
first and must keep winning. New key: `middle_click_paste = 0`.

**Task block, s128 — the bell bug report. Objective, measured starting point, planned steps, expected outcome.**
The user's report, verbatim: *"the bell feature doesn't work properly, Failling a search with less trigger no
flash. while it should. But clicking the terminal when "less" is active makes it flash. while it shouldn't"*.
Two symptoms, and they may or may not share a cause:
* **(a) no flash where one is wanted** — a failed `/search` in `less` flashes nothing.
* **(b) a flash where none is wanted** — a plain click in the terminal while `less` owns the screen flashes.
MEASURED so far (read from the tree at `632065b`, not assumed):
* The hand-off is `if (tb->term.bell_rang) { … tb->bell_ms = mono_ms(); }` in the pty-read branch, and the flag is
  raised ONLY by `exec_c0()`'s `case 0x07`. So both symptoms are claims about **which bytes carry a 0x07** — (a)
  says `less` sends one the parser does not turn into a ring, (b) says something in the click path sends one.
* A click in a program that owns its screen and never asked for the mouse goes through
  `term_click_arrow_keys()` (src/vt.c), which synthesizes up to `TERM_ARROW_MAX` Up/Down then Left/Right presses
  to walk the program's cursor to the clicked cell. `less` cannot move past the end of its file — and a program
  that is asked to move where it cannot is exactly the thing that rings a BEL. That is the leading theory for (b)
  and it is testable without a window.
* `infocmp xterm-256color` has **no `vb` (visual bell) capability** — only `bel=^G` — so a program that consults
  terminfo will send a literal `0x07`, not a visual-bell string. `less` 704, and the user's `LESS` is
  `-R -M --shift 5` (no `-q`/`-Q`, which would silence the bell entirely — worth asking about only if the probe
  shows `less` sending nothing at all).
* `pty_spawn()` sets `TERM=xterm-256color`, so a probe that spawns through it sees the same terminfo a real
  Kai-Term session does.
Planned steps: (1) `.scratch/s128/bellprobe.c` — the real `less` in a real pty (24x80, queries answered, the s117
lesson), printing for each gesture the number of `0x07` bytes in the output AND the number of times Kai-Term's own
parser raised `bell_rang` over those same bytes, with the hex around each `0x07`; (2) fix what the probe shows,
with a check written around what the probe printed; (3) re-measure both builds and every suite, `make asan`;
(4) ISSUES "Open" entry → then "Fixed", README if the behaviour or a setting changes, AGENT.md;
(5) commit and push. Expected outcome: a failed search in `less` flashes, a click in `less` does not, and each
half has a check that fails without the fix.

**MEASURED (s128) — the probe ran; the leading theory for (b) is right and (a) is not Kai-Term's bug.**
`.scratch/s128/bellprobe{,3,4}.c` (v4 is the one to re-run: it drains with `poll` until 400 ms of SILENCE — v1/v2
stopped at the first `EAGAIN`, which could have missed a BEL written just after the message — and they reset the
counters immediately before each single write). Real `less` 704 in a real pty, 24x80, `TERM=xterm-256color`,
queries answered, `/tmp/kbell_probe.txt` = 200 lines. `bellprobe4 search`, six `$LESS` values (the user's
`-R -M --shift 5`, unset, empty, `-q`, `-Q`, `-M`) — the counts are the SAME in all six:
* `/zzzzz` (no such pattern) → **0 `0x07` bytes, 0 rings**; the bottom row reads `Pattern not found: zzzzz
  (press RETURN)`. Same for `n` (repeat) and for `/[` (an invalid pattern, `missing terminating ] for character
  class`). **`less` does not bell for a failed search at all — it answers with a message.** So symptom (a) has
  nothing for Kai-Term to flash: the bytes never arrive, in any `$LESS` setting, and no terminal on this machine
  could flash for them. `man less` agrees: `-q`/`-Q` are about scrolling past the end/beginning and other errors;
  a failed search is reported by message. NOT a Kai-Term bug — report it to the user with the measurement.
* What `less` DOES bell for, and what Kai-Term's parser turns into a ring (so the feature works): an unknown
  command `x` (1 byte, 1 ring), `Up` at the top of the file (1, 1), `Down` at the end (1, 1). With `LESS=-q` the
  scroll-past-end BELs are gone (0, 0) and with `-Q` even the unknown-command BEL is gone — a bell silenced by
  the program cannot be flashed by the terminal.
* `less` leaves the model's cursor on its STATUS line: `cur=(39,23)` with `-M`, `(20,23)` without. That is what
  makes symptom (b) happen.
`bellprobe4 click` (the click built by the very function main.c calls, `term_click_arrow_keys`, on the model that
parsed less's own output):
* click (10,5) at the top of the file → **18 Up + 29 Left presses**; click (10,12) → 11 Up + 29 Left → **1 BEL
  byte, 1 ring**; click (10,22) → 1 Up + 29 Left → 1 BEL, 1 ring; click (40,12) → 11 Up + 1 Right → 1 BEL, 1 ring.
  The presses are measured from the status-line row, so a click in less's TEXT area is always "press Up N times",
  and at the top of the file that is exactly the scroll-before-the-beginning `less` bells for. **The flash is
  Kai-Term flashing for a complaint about keystrokes Kai-Term itself typed.**
* Side effect worth knowing (not what the user reported, and pre-existing): the horizontal presses move less's
  horizontal scroll — click (40,12) left the bottom row reading `lines 1-23/200 12% (column 6)`. That is the
  `click_moves_cursor` caveat the header already states (keystroke synthesis, not mouse support).
So the fix for (b) is a rule, not a heuristic about pagers: **a BEL that answers keystrokes Kai-Term synthesized
for a click is not a bell to show the user.** It stays program-agnostic (nothing may learn what `less` is), the
parser still raises `bell_rang` (the BEL really happened), and the decision not to flash is a pure function next
to `bell_strength()`. Next action: measure how long after the synthesized write the BEL actually arrives, so the
window is a measured number and not a guess.

**DONE (s128) — the fix, the measurements behind its numbers, and what the harness says.**
* Latency measured before choosing the window (`bellprobe4` timestamps the write and every `0x07`): the reply
  arrives **0-1 ms** after the write, in every run. `BELL_OURS_MS = 250` — two orders of magnitude of headroom,
  still far under what a person can type after a click. It is a separate constant from `BELL_FLASH_MS`; the two
  numbers have nothing to do with each other beyond the coincidence.
* `bell_is_ours(now, synth, window)` in `src/vt.c` next to `bell_strength()`, same shape and the same EXCLUSIVE
  boundary, so the two time rules cannot disagree about "within the window". `Tab.synth_ms` (main.c's clock, like
  `bell_ms`, travels with the Tab through a `mux_close()` shift). `main.c` stamps it in the click branch where it
  sends the presses and asks the decision in the BEL hand-off; the muted branch still clears `bell_rang` and logs
  `bell: session N/M rang, but that answers the cursor keys a click became — no flash`. `mux_open()` clears the
  stamp for a reused slot.
* **The wheel path is deliberately NOT stamped** — there the user asked to scroll, so "there is nothing further"
  is a message addressed to them. Pinned by a count: `synth_ms` appears in `main.c` exactly twice, stamp and read.
* Checks: 18 in the `bell_is_ours` group in `tests/test_vt.c` (never stamped; never stamped AND a clock that reads
  inside the window, which is what kills dropping the `synth <= 0` guard; the 1 ms lag; the last ms inside; the
  boundary; long after; clock backwards; negative stamp; window 0 and negative; the window as the caller's
  argument — 300 ms muted for 500 and flashing for 100, which kills a definition that ignores its parameter; the
  BEL still recorded; the stamp travelling with a shifted Tab; a reused slot starting un-stamped) + 6 source scans
  in `tests/test_render.c` for the `main.c` wiring (counts measured with `.scratch/s128/scancount.py`, which
  replicates the C scanners — do not guess a scan count).
* Harness `.scratch/s128/mutate.py`: **18 mutants, 16 KILLED, 2 SURVIVED** — `control_noop` (the staging tree is
  green, which is what makes KILLED mean something) and `ours_no_window_guard` (with the other two guards in
  place, `window_ms <= 0` is unreachable in effect: `now >= synth` makes `elapsed < 0` impossible, so the
  comparison already answers 0 — the same kind of survivor as s126's `strength_no_dur_guard`). The feature-absent
  A/B is `ours_never_mutes` (`bell_is_ours` answering 0, i.e. the reported bug back): **4 failures** in
  `test_vt`. Every wiring mutant dies in the scans: `main_no_stamp` 2, `main_stamp_wrong_tab` 1,
  `main_stamp_the_wheel_too` 1, `main_no_ask` 3, `main_literal_window` 1, `main_mute_skips_clear` 1,
  `mux_no_slot_clear` 1.
* Re-measured from `make clean`: `make -j8` exit 0 / 0 warning lines; the test build exit 0 / 0 warning lines;
  `test_vt` **1516 / 0** (1498 → 1516), `dbg` MODEL OK, `test_fm` **384 / 0**, `test_render` **561 / 0**
  (555 → 561); `make asan` exit 0, 0 warning lines, same four counts, no LeakSanitizer/UBSan report;
  `CLI: 99 checks, 0 failures, 13 skipped` headless.
* Docs: README's bell bullet, the `bell` settings row and the mouse section now say what `less` actually bells for
  (it said "a failed search", which the probe disproved) and state the click-mute rule; ISSUES carries the report
  under "Fixed" with both halves and the numbers.
* **What is NOT fixed, deliberately:** a click in `less` still sends the Up/Left presses, so it still scrolls the
  file and can move `less`'s horizontal position (measured: the bottom row reading `lines 1-23/200 12%
  (column 6)`). That is the documented `click_moves_cursor` caveat — keystroke synthesis, not mouse support — and
  the user reported the FLASH, not the scroll. `click_moves_cursor = 0` turns the whole click half off.

### 6. Visual bell — DONE (s126 stages A-D, s127 stage E): the BEL event, the key, the colour, the quad, the
`main.c` wiring, the mutant harness and the documents. Owed to the user's eye: that it READS as a bell

Before step 6, `BEL` was swallowed: `case 0x07: break;   /* BEL: nothing to do here */` in `exec_c0()`
(`src/vt.c`). Programs ring it on real failures — readline's ambiguous completion and empty Enter, `sudo` on a
wrong password, `less`/`man` on a failed search, vim on an error.

Cheapest honest implementation, no new dependency and no GL blending: `exec_c0` sets a per-Term "bell rang" flag
(in since s126); `main.c` turns it into a deadline; `render_draw()` emits **one extra quad in batch A, after every
cell background** — over the backgrounds, under batch B (bands, glyphs, labels, border), so the text stays readable
while the field flashes. Colour = `mix_c(bg, fg, small)`, decided by `render_bell()`. A margin flash is not an
option: s101 made the margin the same colour as the cells. Key: `bell = visual|off`, **default `visual`** (their
answer to question 6).

**The rules step 6 added, and where each is checked:**
- **A BEL is an event, not a state.** `Term.bell_rang` is raised by `exec_c0` and consumed by the pty-read path,
  which clears it and stamps `Tab.bell_ms` with `mono_ms()`. vt.c reads no clock, so the model stays reproducible;
  a flag left set would re-arm the flash forever (`main_no_clear`). Three BELs are one event — it is a level.
- **A `0x07` that TERMINATES a string is not a bell.** OSC/APC/DCS consume it as ST and never reach `exec_c0`.
  Merging the two would flash the window for every OSC title a shell sets and for every shell-integration
  announcement. Checked with a paired check proving the OSC really was parsed, so the "did not ring" check cannot
  pass by the sequence being abandoned. A BEL inside a CSI still rings (the s97 rule).
- **The fade is ONE pure function.** `bell_strength(now, rang, dur)` answers 0..1000 and 0 for "never rang", "past
  the deadline", "the clock went backwards" and "duration 0". `render_draw()`, `mux_bell_live()` and the checks all
  ask it; nothing else in the tree writes that arithmetic (two scans pin the counts).
- **`bell = off` costs no branch:** the caller hands a duration of 0 and `bell_strength()` already answers 0 for
  that. `Render.bell_on` is a separate field from the timestamps, so "off" cannot be faked by forgetting to record
  a ring.
- **Only a DRAWN session is live.** `mux_bell_live()` (src/mux.c) walks `Layout.pane[]`, not every tab: an unseen
  session has no field to wash out (the bar's `*` is its mark) and must not keep the window redrawing every 8 ms.
  This rule moved out of `main.c` in s127 because a mutant that ignored it survived every check.
- **The tick must ask for one frame MORE when the last flash ends** (`live || W.bell_live`): at that moment
  `bell_strength()` answers 0, so nothing else would redraw and the pane would keep the last half-faded wash
  forever. Pinned as a text pin only — its behaviour belongs to the user's eye.
- **One quad per drawn pane, appended to batch A** (after the cell backgrounds, before the band/bar/notice/dialog),
  so it is over the paper and under the bands, glyphs, cursor and pictures; the rect is the PANE's, not the grid's,
  so the leftover pixels at a pane's right/bottom flash with the cells (s101's margin rule). `maxq` pays `+
  lay->npane` — a staging budget that forgets a pass is a buffer overrun waiting for a wide window.
- **The colour is `render_bell()`, never a literal in the draw loop:** the paper mixed `BELL_MIX_MAX` (25 %) toward
  the ink at full strength, clamped at both ends, never the ink itself, alpha untouched, and it works on a LIGHT
  paper too (mixing toward a dark ink). A strength far below 0 is a checked case — `-7` alone could not tell the
  guard from no guard, because `25 * -7 / 1000` is 0 in C.
- **`mux_open()` clears `bell_ms`** for a reused slot, exactly as it clears `attention`/`cd_req`/`int_gen`/
  `int_dead`: a new tab must not come up mid-flash of a BEL rung by a shell that is already gone.
- **Kai-Term does not flash for a BEL that answers keystrokes it synthesized for a CLICK** (s128, the user's
  "clicking the terminal when `less` is active makes it flash. while it shouldn't"). One owner for the rule:
  `bell_is_ours(now, synth_ms, window)` in vt.c, the stamp in `Tab.synth_ms` written by `main.c` where the presses
  are sent and read by the BEL hand-off. Three things it must not become: not a guess about which program is a
  pager (nothing in src/ may learn what `less` is), not a parser decision (`bell_rang` is raised either way — the
  BEL really happened, and a hidden session still gets its bar `*`), and not a rule for the wheel (there the user
  asked to scroll). The window is the caller's argument and the boundary is exclusive, matching
  `bell_strength()`, so the two time rules cannot disagree.

### 7. URLs: plain click opens — DONE (s129-s131, `4610911`)

The design the plan predicted held, with one change worth keeping: no per-row run list is computed at all. The scan
runs at click time over the ONE logical line `term_line_span()` already walks, so there is no storage, no reflow
interaction and no per-frame cost — and the wrap marks have one owner instead of two.

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

**The rules step 7 added, and where each is checked:**
- **The scan never declines a click; the gate does.** `term_url_span()` finds the run (on the alternate screen too);
  `url_click_opens()` is what refuses. A scanner that hid the address could not be tested for finding it, and the
  refusal would be an absence instead of a decision — which is exactly the dead-`cursor_color` shape.
- **One owner for the wrap marks.** `term_url_span()` walks the logical line with `term_line_span()`, the function a
  triple click already uses; a second walk of `Grid.wrap` is a second answer about which rows are one line (a scan
  pins that `src/url.c` calls it exactly once). The case that separates a WINDOW break from a PROGRAM break is a line
  ended exactly AT the margin: the row is full, no wrap mark is set, and only the mark tells them apart —
  `url_join_every_row` survived the first corpus because in every other program-broken case the blank cells ended the
  run anyway.
- **Never open half a URL.** A non-ASCII or wide cell to the RIGHT of the run refuses the whole run (the draft rule
  opened `https://ex` for `https://ex<U+00E9>.com` — a different page); `term_url_text()` writes NOTHING when the
  bytes do not fit the buffer; over `URL_MAX` it returns -1 and the caller logs instead of opening. A non-ASCII cell
  on the LEFT is fine — the scheme match fixes where the run starts.
- **The trailing-mark rule is a decision with a check on each side.** Cut `. , : ; ! ?` and a closer with no matching
  opener inside the run; KEEP a trailing slash (`https://x/` and `https://x` are different pages, so cutting one is a
  wrong answer, not a smaller one); keep a balanced pair. Quotes and angle brackets are deliberately NOT in the cut
  list — `url_cell()` ends the run on them, and a branch that cannot be taken is a branch nobody can test.
- **The address is data, never a command line.** `execvp` with `char *argv[3]` and no shell anywhere (five scans
  assert `system`/`popen`/`execl`/`execlp`/`execve` are all absent); a leading `-` is refused because that is the one
  way a URL could change what the opener DOES; the opener's name is `URL_OPENER_PROGRAM` in the header and never a
  config key, because a key holding a program name is a way to make the terminal exec anything. Only `http`/`https`
  reach it — `ftp://`, `mailto:`, `file://` open nothing.
- **Four promises the spawn makes, each with a check that is not vacuous.** `setsid` (Ctrl+C at the prompt cannot
  reach the opener), stdin/out/err on `/dev/null`, `closefrom` (every pty master, the Wayland socket, the timerfds,
  the signalfd — none is close-on-exec, because `posix_openpt` is called without `O_CLOEXEC`), and the blocked
  SIGCHLD mask cleared (a mask survives exec and the real opener is a shell script that waits for children). Two of
  these checks were VACUOUS until probed: a pty MASTER shows in `/proc/self/fd` as `/dev/ptmx`, not `/dev/pts/N`, so
  grepping for `pts` reported 0 for a child holding every master (`s129/fdprobe.c`); and `/bin/sh` normalises its own
  signal mask at startup, so the mask half now runs against `/usr/bin/awk` (`s129/maskprobe.c`).
- **The wait is the double-click window, and the two rules agree at the boundary.** `URL_OPEN_WAIT_MS` IS
  `CLICK_DCLICK_MS`, and `url_open_due()` is strictly-greater because `term_click_count()` still calls a press at
  exactly that instant the same gesture — a check pins the agreement at that millisecond. The stamp is cleared in
  both places that end a wait (a new press, and the deadline); `url_tick()` rides the frame timer, because a wait
  nothing drives never expires.
- **The open is RE-ASKED at the deadline, not trusted from the click.** The tab may have closed (`mux_close` shifts
  the later indices), the pane may have scrolled, the program may have repainted. `main.c` re-runs the scan on the
  same cell and `strcmp`s the text; anything else opens nothing. Opening an address nobody clicked is worse than
  opening nothing.
- **`main.c` computes the flags, `src/url.c` owns the order.** No early-return chain in the event handler: "a drag
  still selects", "the 2nd and 3rd click select a word/line", "Shift means selection", "the tab bar and the band are
  not panes" and "a program that owns its screen keeps its click" are one table in a file the tests link. The pending
  open is keyed to `mux_active(&mux)`, which is the clicked pane's tab because a press in a non-visible pane calls
  `tab_switch()` before the release computes the cell. The forked opener is reaped by the existing signalfd loop
  (`waitpid(-1)` then `tab_for_child()`, which answers -1 for a non-shell pid), so it cannot become a zombie.

### Decisions already made — do not re-ask, do not re-propose

Answered by the user 2026-10-22 (the eight questions) and 2026-10-19 (the dropped features):

- **Precedence:** `kai-term.conf` beats a theme; `kai-term.bind` is not in the chain at all (it holds bindings
  only, and a colour key there stays the error it already is). Consequence, already implemented: a theme cannot
  be applied line-by-line while the settings file is read, because a `background =` line ABOVE the `theme =`
  line must still beat it — the settings file records which keys it set explicitly and the theme fills only the
  rest.
- **OSC 4/10/11/104: yes**, with an off switch (`osc_colors = 1`) so "any program can repaint the window" can be
  turned off without a rebuild.
- **Cursor a bar always (no `cursor_style` key, blink 1000 ms, DECSCUSR honoured) / word selection strips any
  trailing punctuation but keeps leading / middle-click pastes the CLIPBOARD and is off by default, so no
  `wl_primary` work / visual bell on by default / URLs on a plain click with the gates above.** Each is spelled
  out in its step (2, 3, 5, 6, 7) — these are the answers, not open questions.
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
look, bell flash) to the user. **And when a report arrives, the user is testing `bin/kai-term` from this tree — say
nothing about other binaries, look at nothing outside this tree, and go find the failing stage with a measurement.**

## Progress Journal (era entries; root causes of fixed bugs: git history)

One entry per era. What landed, and the durable lesson — the probe output, the wrong theories, the source line
numbers and the intermediate check counts are in git.

- **2026-10-12/14, s1-23 — scaffold, first build, first features.** vt.c grid -> rotation permutation (`row[]`
  always valid); CSI params; event loop -> `prepare_read -> epoll_wait -> read_events`; EGL/FreeType init;
  render fixes (vertex stride, MVP mapping, 6-vertex quads, atlas upload format, first-frame bootstrap). First
  2 user bugs: black window (font metrics in font units, not px); 10 s startup (unanswered DA1/CPR/DA2). Then
  HW instancing, alt screen, SGR mouse, scrollback keyboard nav, zoom, selection + Wayland clipboard, wheel
  scroll (seat v5), the `Grid.live_off` anchor (fixed the wheel direction and the auto-follow). Tree -> project
  template; Makefile rewritten; `.gitignore` + stricter warnings.
- **2026-10-14/15, s24-47 — selection precision, emoji, reflow, paste, key repeat, padding.** Sub-cell
  endpoints and per-fragment highlight without GL blending; selection-vs-pointer causes (grid anchoring,
  `wl_fixed_to_int`); fontconfig fallback faces with the source face stored per glyph (so `atlas_grow`
  re-renders from the right one); 3 back-to-front passes; COLOR emoji end to end (clusters in vt.c, ONE glyph id
  via HarfBuzz, FreeType color bitmap, RGBA8 atlas, un-tinted, wide emoji = 2 cells, regional-indicator pairs,
  U+FE0F/U+20E3 presentation). CSI 3 J so `clear` drops scrollback (`clear_scrollback` gate); shell start dir
  (`-w DIR` > config `working_dir` > `$HOME`, tilde expansion, `stat` check). Bracketed paste (DEC 2004,
  `paste_wrap()`, buffered, <=4096 B chunks, 8 MiB cap, 400 ms idle watchdog); the latent `term_init`
  free-of-uninitialized-grid bug -> `GRID_VALID`. `app_id`; real window `opacity` (premultiplied source-over);
  selection endpoints snapped to whole cells; no band over never-written cells; **REFLOW** on width change
  (`Grid.wrap` soft-wrap marks, `grid_reflow()`, `grid_resize_plain()` for the alt screen, cursor travels with
  its logical cell, narrowing evicts oldest rows). CLIENT-SIDE KEY REPEAT (`krfd` timerfd, `send_key()`
  repeatability, `repeat_delay`/`repeat_rate`); `copy_on_release` (default off); the draw/commit cycle
  rewritten -> the 3 frame-pacing rules; the pre-existing segfault on heavy output in a grown window fixed by
  `sb_limit() = min(scrollback_max, rcap - rows)` as the single bound.
- **2026-10-15, s50-55 — repo hygiene and the CLI.** GitHub; dead code removed; `WARN` tightened; `.gitignore`
  replaced (it ignored neither `bin/` nor `build/`); config flag `-c` (was `-C`); one generic unknown-option
  message. `tests/cli_test.sh` + `make test-cli` (the real binary — `main()` is in no linked test); the zoom
  stale-prompt cause in `grid_resize_rows`; WINDOW PADDING (`padding` key + the single `term_grid_geometry()`
  deciding cols/rows AND origin), which exposed the `DEF_BG` clear-colour bug.
- **2026-10-15/16, s56-58, s60-62 — MULTIPLEXING + TAB BAR + SIDE-BY-SIDE VIEW.** `src/mux.c`: N `{Term, Pty}`
  per window, config-driven key bindings. `src/layout.c`: pure geometry. Three defects fixed: bar/pane click
  logged a switch it never made (`tab_switch()` never called `mux_goto()`), `BTN_RIGHT`/`BTN_MIDDLE` swapped in
  the SGR mapping, `tab_bar = always` asked the compositor for `cell_w` instead of `cell_h` of extra height
  (startup grid 1 row short) -> arithmetic moved to `layout_initial_size()` so a check pins it.
- **2026-10-16, s59 — RENAME `K-Term` -> `Kai-Term`** (dir, header, binary, config path,
  `DEF_APP_ID`/`DEF_TITLE`, log prefix, `TERM_PROGRAM`, tests pinning those literals). 4 items OUTSIDE the repo
  were listed for the user (niri window rule, `/usr/local/bin/k-term`, `desktop-terminal` + its .desktop entry,
  GitHub repo name).
- **2026-10-16/17, s63-s67 — the doc rules.** `docs/ARCHITECTURE.md` deleted (it mixed project design with host
  facts, which must not ship inside a project); host facts -> `/home/kaida/Project/ENVIRONMENT.md` OUTSIDE the
  repo; its invariants -> "Architecture & Invariants", its entry points -> "Debugging this project". ISSUES.md
  gutted to verified current info only (673 -> 100 lines): filing rule = symptom + exact reproduction + what
  `./bin/kai-term -d` logs; not reproducible -> not filed. s67: the "56px emoji strike" fact was wrong IN THE
  DOCS, not in the code — it came from reading CBLC's `bitmapSize` (a BYTE count) as a pixel size; the installed
  NotoColorEmoji strike is 136x128 and `color_px == 128`, so font.c was right. Rule: **no git SHAs in source
  comments — they age; state the mechanism inline.**
- **2026-10-17/18, s68-s70 — image support (kitty graphics protocol), user-confirmed by eye.** Model/parser
  (`src/vt.c`): APC `ESC _ G` + DCS-discard states, control-block parser (`k=v` separated by `;` OR `,` — timg
  uses `,` the spec says `:`), chunk assembly (`m=1`/`m=0`), base64 + zlib + PNG (libpng simplified) + raw
  f=24/32, premultiplied pixels held in the model, images anchored to ABSOLUTE document rows
  (`Grid.row_base`), span math in vt.c (also what `a=T` moves the cursor past), byte budget + prune,
  `CSI 14 t`/`CSI 16 t` answers, `ws_xpixel`/`ws_ypixel` in `TIOCSWINSZ`. Renderer: one GL texture per image
  keyed by `KImage.serial`, and the frame split into TWO batches so text stays on top with no GL blending. Three
  defects found by writing the README honestly and then running the example: the "timg auto-detects" claim was
  false (its list is hardcoded to `TERM=xterm-kitty|xterm-ghostty`); Kai-Term's DA1 answer advertised sixel
  (`;4`), so `timg foo.png` sent DCS the parser discarded — the picture vanished, worse than block characters;
  and a re-transmission to an existing id stacked a second picture, so an animation left a trail. DA1 is now
  `ESC[?64;1;2;6;7;12;22c`, `CSI 5 n` is answered `ESC[0n`, and a transmission to an existing id replaces that
  image. Rule: **a documented example is a claim — run it before writing it.**
- **2026-10-18/19, s71-s74 — the integrated file manager.** `src/img.c` (every decoder in one place: PNG libpng,
  JPEG libjpeg, WebP libwebp, GIF decoded in-tree because giflib is absent; premultiplied RGBA8; `img_sniff()`
  on magic bytes, never a file name) — the kitty path and the previews share it. `src/fm.c` (model only, no
  Wayland/EGL/threads), band geometry in `src/layout.c` (`FmGeom`, `layout_hit_fm`), band drawing in
  `src/render.c` (same batches, shared `emit_cell_glyph()`), wiring in `src/main.c` (worker thread + two
  newest-request slots + result pipe fd in epoll, key/mouse routing, focus handover, the `cd` follow), 20 config
  keys, `tests/test_fm.c`. Decoders verified by pixel-diffing against ffmpeg (PNG/JPEG/WebP/GIF byte-exact),
  which found a libjpeg `output_scanline` off-by-one (heap corruption) and a wrong GIF interlace mapping. Three
  real product defects found ONLY by the live key-injection harness, because `main.c` is in no test binary —
  `fm_keys()`'s two-state return, an out-parameter passed next to the call that fills it, and `fm_send_cd()`
  firing on refresh. Each is a pitfall in Key Decisions.
- **2026-10-19, s75-s79 — the file manager's four reported fixes, and a core pty bug.** Added `Left`/`Right`
  bindings, `..` as a real first row, double-click to open, and the empty-prompt gate: the model remembers the
  cursor row as the shell last printed it (`prompt_row`/`prompt_cx`/`prompt_cy`) and `term_at_prompt()` =
  `!input_pending && prompt_row_matches()`. `alt_screen_leave()` now RE-TAKES that snapshot instead of clearing
  it — clearing made the follow dead for the rest of the session after every vim/less run. **`src/pty.c` was
  putting every child's terminal into raw mode** (`cfmakeraw()` on the master; a pty pair shares one termios),
  which broke Ctrl+C, Ctrl+D, echo and ONLCR for every program that does not set its own termios — measured A/B
  live, then fixed by deleting the block. `src/pty.c` still does NOT touch termios.
- **2026-10-19, s81 — a refused texture upload is a decision, not a log line.** 85 SIGSEGV core dumps from one
  CLI-suite run on a GPU with no memory left. The policy is now in Key Decisions. `tests/cli_test.sh` makes ONE
  probe launch and skips the window checks with the reason printed; `KAI_TERM_TEST_NO_WINDOW=1` skips them all —
  a test that maps a window changes the user's desktop.
- **2026-10-19, s82-s86 — the prompt follow: clear -> `cd` -> re-populate, idempotent, user-confirmed.**
  `IDLE/ERASING/CDING/RETYPING` plus a 2 s deadline, so "runs a mangled command" is closed by construction. Two
  hazards only a real shell exposed, both now invariants: a height-only resize destroyed the snapshot of a DIRTY
  line (it now moves the snapshot with its row instead of discarding it), and closing the echo window on a STEADY
  state let a second navigation append `cd '<dir>'` to the user's command (the window now needs a transition,
  and CDING additionally waits `FMCD_QUIET_MS` because a burst boundary is not a prompt). fish's autosuggestion
  ghost is followed via `term_prompt_line_typed()` / `term_prompt_line_is()`; the accepted cost is a cursor
  parked mid-command: the whole-row proof fails, the sequence is dropped (a `cd` is never written in front of
  the surviving half) and the captured text goes to the log. Proven headless against real fish through the real
  pty (`.scratch/s86/follow2.c`).
- **2026-10-19/20, s88 — the band became a SHARE, and the preview's rows got one owner.** `fm_rows` is gone
  everywhere; `fm_size` (percent of the grid area, default 75, clamped 10-95 at parse time AND in layout.c)
  decides it. The preview overlap was two hardcoded numbers, not a renderer problem: the header's size was a
  literal 3 in `paint_preview()` and `fm_image_fit()` independently, and the 8 metadata lines an image preview
  carries were counted in neither, so lines from row 3 down were painted into the picture's box. Now
  `preview_head_rows()` + `preview_meta_rows()` are the single owner, called by both. Rule: **a check that
  passes on the buggy code pins nothing** — the first version (a small centred 8x8 picture) passed against the
  old fm.c by luck; the A/B found the case that actually overlaps.
- **2026-10-20, s89 — a BackSpace at an empty prompt had disabled the shell follow.** Measured (real shells):
  bash and sh answer BackSpace at an empty prompt with a bell only, fish not at all — so no burst ever
  re-proved the line empty and `term_at_prompt()` stayed "not empty" until Enter. Fix: `payload_cannot_insert()`
  (invariant 5 of the prompt rules). That turned a long-green check red, because the property had been held by
  accident — see the "invariant held by accident" pitfall. Do not write a prompt check leaning on `CSI H` (it
  moves the cursor to another ROW) or on SS3 (`ESC O H` prints a literal `H` here).
- **2026-10-20, s90-s92 — shell integration: the file manager asks the SHELL to `cd` instead of Kai-Term
  typing it.** One channel, so the control carries no data; the sequence is the measured byte pair `C-x C-]`,
  announce/ack over the `K` APC kind (one more branch in `apc_end()` — no new parser state), 400 ms then fall
  back to the typed machine. s92 fixed the two defects the user reported from their own window: a shell they
  started at the prompt was not integrated at all, and the prompt kept naming the OLD directory. All the rules
  are in the shell-integration invariants; the bash limitation (readline expands the prompt once per
  `readline()` call — `C-l` leaves it stale and printing `${PS1@P}` over the line garbles it, both measured) is
  documented and nothing is pretended.
- **2026-10-20, s93-s94 — the cleanliness pass, and zsh becomes verifiable.** A stricter second-compiler pass
  (clang, `WARN` plus `-Wcast-qual -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition -Wundef
  -Wunused-macros -Wextra-semi`) found exactly three things in OUR code: `img_decode_gif` needed `static`,
  `klog` needed `__attribute__((format(printf, 2, 3)))` — verified both directions — and one `-Wcast-qual` at
  zlib's non-const `next_in`, correct as written, which is why `-Wcast-qual` stays out of `WARN`. Also found:
  the whole s90-s92 work had never been committed. s94: the user installed zsh and confirmed the integration for
  fish, bash and zsh. Still unmeasured in zsh: what `C-x C-]` does in a zsh that has no binding. Grep found
  seven stale "no zsh on this box" claims, four inside source comments and check MESSAGES — **a check message
  that misstates the evidence is worse than no message, because the next agent trusts it.**
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
  sequence ENDED it — `ESC [ ? 2004 <TAB> h` left `bracketed_paste` at 0 and painted an `h`. Fix: ONE file-local
  `exec_c0()` shared by `PS_GROUND` and `PS_CSI` — the rule existed twice and the two copies disagreed. The
  parser rule is in Architecture & Invariants; the diagnosis recipe for a recurrence is in Next Steps. s98
  (the source's): measured against the user's live Firefox selection — ten advertised types ending
  `text/x-moz-url-priv`, which Firefox serves as **UTF-16LE**, and `offer_offer()` overwrote the recorded type
  for every `text/*` name, so the LAST advertisement won. Fixed in two places because two things were wrong:
  `clipboard_mime_rank()` and `paste_repair()`. Their own clues — "the source matters", "ctrl+v works but
  ctrl+shift+v does not" — were the diagnosis. Also measured and NOT a bug: bracketed paste is off for ~20 ms
  (bash, zsh) to ~200 ms (fish) after a shell starts, so a paste flushed in that window goes out raw.
- **2026-10-22, s99 — the config split, and a config reader that had been throwing away every colour.**
  `apply()` became `apply_bind_key()` (the 24 `key_*`/`fm_key_*` names) + `apply_setting()` (the others) under
  one dispatcher, split by NAME and never by which file was open. `config_find(name)` = the first REGULAR file
  with that name in `$XDG_CONFIG_HOME/kai-term`, `~/.config/kai-term`, `/etc/kai-term`, searched per FILE NAME;
  `-b/--binds` added; the bind file is read second, so it wins. The `#` rule was "whitespace before it is
  enough" — exactly the shape of a colour value — so `background = #1c1c22` became `background =`, and an empty
  value is legal, so nothing was even logged. Measured against the user's own file: old `ff101014` (the
  built-in default), new `ff1c1c22` (what they wrote). New durable check: the template name-coverage check — A/B
  proved the hole was real (deleting `padding = 8` from the template: 0 failures on the old suite, 1 on the new).
  One harness bug cost 10 windowed checks: `run_env` used `env "$@"`, so `env` parsed the binary's `-w` as its
  own `--split-string` and the command never ran.
- **2026-10-22, s100-s101 — the visible border around the grid: one runtime value now owns the default ink and
  paper.** User-confirmed in their own window ("It's fixed, you can push"). Three background-ish numbers existed
  and only two were in play: `Config.bg` -> `Render.bg` -> `set_clear_color()` (which paints the
  `Config.padding` margin and the whole-cell remainder), and `DEF_BG` -> what `src/vt.c` put in every blank,
  erased, scrolled-in and reflowed cell. So the frame was `ff1c1c22` around `ff101014`; it "appeared recently"
  only because the s99 `#` fix made their `background =` line take effect for the first time. The fix and its
  rule are in Architecture & Invariants. Three patch-overs that existed only because of the split were deleted
  rather than left behind: the `pen.bg/pen.fg = conf.*` assignments after `mux_open()` and in `tab_open()`, and
  `paint_blank_cells()` with its three call sites — keeping them would have left a second writer of the same
  value, and `paint_blank_cells` was not even faithful (it re-painted every `ch == 0` cell from `Config.bg`,
  destroying a blank an application had erased with a colour of its own).
- **2026-10-22, s102 — the feature survey, the dead `cursor_color`, and the plan.** Every candidate was grepped
  for first: `zwp_fractional_scale`/`wp_viewporter` 0, `wl_primary` 0, OSC 52 0, OSC 8 / URL detection 0,
  DECSCUSR 0, `zwp_text_input` 0, `tcgetpgrp` 0, `BEL` = `case 0x07: break;`. Two of my claims were wrong and
  the user corrected them with facts about their own machine (scaling, OSC 52) — lesson in Key Decisions. Found
  while reading the colour paths: `cursor_color =` is dead config. Step 0 landed as two commits: the
  behaviour-free seam (`render_chrome()`, verified by a probe comparing all eight `Chrome` fields over 25 bg×fg
  pairs — 200 comparisons, 0 differ), then the fix. `bin/test_render` links `render.o` + what it needs to LINK
  but calls only the pure function, so it needs no display and no GL context. Then 1a and 1b.
- **2026-10-19, s103 — `selection_color =`, and two ways a check can lie.** The tree opened RED and
  half-recorded: `test_render` 23 checks / 2 failures. Root cause, measured: the probe tokens were named *inside
  the `CHECK` calls that counted them* — a self-scan that counted its own corpus. And compiling the TEST
  binaries printed `hex escape sequence out of range`: `feed("\x1bc")` is ONE escape (0x1bc), so the palette
  group's "ESC c / RIS does not wipe the palette" check had never sent a reset. Both are now durable gates and
  pitfalls: the probe-file-scan rule, and the `hex escape` group (`src_hexesc_count()` walks every C file,
  skipping comments so prose ABOUT the trap is not counted, and skipping a match preceded by another backslash
  so a message quoting it is not counted either). The RIS check is proven non-vacuous by MUTATION (inserting
  `term_palette_reset()` into the RIS handler makes it fail).
- **2026-10-19, s104-s107 — 1c, 1d and 1e (`--import-theme`), and taking 1e green.** 1c+1d landed as one commit
  (`c84258a`) because they share one colour plumbing — `cfg_parse_color` exported so theme and OSC read colours
  the same way (the s99 bug was exactly two readers disagreeing), the shared `config_strip_comment()`,
  `Config.set_keys`, and the pinned settings-name count both features move. Splitting them would have meant
  inventing an intermediate tree that was never built, never measured and never green. 1e was built in s105 and
  left uncommitted and RED; s107 took it green (the two warnings via `set_name()`, the leak by freeing the
  harness's name lists, 24 new CLI checks placed in the part of the script that runs with no display because the
  import path needs none, plus 67 in `tests/test_vt.c`). README's import block was written against real runs
  rather than against the code: the `Overwrite? [y/N]` question with stdin closed exiting 1 and leaving the file
  byte-identical, `y` overwriting, and `background = red` refused per key while the rest of that file still
  imported. 1e is `8add0e5`, verified **in a fresh worktree at that hash** (`git worktree add` + build + all four
  suites + `make asan`, all green) rather than only in the working tree — the working tree also held the s106
  journal cleanup, and "green" has to mean the commit, not the directory. A/B harnesses in `.scratch/s107/`
  (`ab_noimport/`, `head_tree/`, `ab_wheel/`, `ab_tpl2/`); their limits are in Key Decisions.
- **2026-10-19, s106 — the previous cleanup (docs only, no source touched).** 1942 -> 1459 lines, 183,517 ->
  137,411 bytes (the 1942 side is verifiable: `git show 8add0e5:AGENT.md | wc -l`). The s101-s105 step-by-step
  narrative collapsed into era entries and the "measured starting point" blocks went to git history. Every
  mechanism name, measured number and pitfall was checked to survive outside the deleted text before it was
  deleted. Re-measured the tree while doing it, which is how the red `make asan` was found. Pre-cleanup text:
  `.scratch/s106/AGENT.md.pre-cleanup`.
- **2026-10-19, s108 — the windowed group of `tests/cli_test.sh`, on the user's explicit go-ahead.** It had not
  run since s104 and the 24 checks s107 added made the recorded total stale. Checked the machine first rather
  than assuming: `WAYLAND_DISPLAY=wayland-1`, no `kai-term` of mine running, no new core dump, and `nvidia-smi`
  showing **11680/12227 MiB used** — the local LLM holds 9632 MiB, the condition the old Next Steps item warned
  about, because a refused atlas upload fails EVERY window check for one unrelated reason after putting a dozen
  windows in front of the user. ONE probe launch first (`./bin/kai-term -d -e true`) showed the atlases fit in
  the ~547 MiB free, so the group ran: **141 checks, 0 failures, 0 skipped**, exactly the predicted 117 + 24,
  and 0 skipped is what shows the window checks ran rather than being gated off. Nothing was found. Correction
  to the rule this run tested: waiting for `nvidia-smi` to show VRAM free is too strict — **the probe launch,
  not the free-megabytes number, is the gate.**
- **2026-10-19, s109-s110 — the file manager must not take the keyboard from the pointer, and README's tables
  get pinned.** The user's report: "When the file manager is open, hovering the mouse on it gives it the focus,
  same for the preview pane. They should require a click to take the focus." The mechanism and the rule are in
  Architecture & Invariants (`fm_wheel_focus`). Two doc defects found while verifying: the `ab_tpl` failure
  counts recorded in this file and in ISSUES.md were **inflated** (2 and 2, actually 1 and 1) because the old
  harness had no README.md, so its second failure was the OPTION-table check — an artifact of the copy, not of
  the mutation (replaced by `.scratch/s107/ab_tpl2/`); and **README's settings and binding tables were NOT
  pinned by any check** — deleting the `fm_focus_on_hover` row left every check green, so the Current Status
  claim that the name lists were compared with "README's tables" was true only for the command-line OPTION
  table. (The hand-diff of the current README was clean, so nothing was missing today; what was missing is the
  check that keeps it that way.) Fix: `readme_table_names_of()` + `names_diff()`. README's theme table is
  deliberately NOT set-differenced: it collapses `color0` … `color15` into one row, so a name-by-name diff would
  fail on purpose; those 23 names stay pinned through `config/kai-term.theme`. Renaming a heading to
  `### Settings file (…)` does NOT fail, on purpose: the match is on the heading's prefix, so re-wording a
  section keeps the check alive.
- **2026-10-19/20, s111-s112 — step 2, the cursor bar.** Measured before writing anything: the cursor was the
  cell's own background quad in pass 1 with pass 3 re-tinting the glyph under it, and DECSCUSR was collected but
  discarded — the CSI state skipped intermediate bytes, so `ESC [ 3 SP q` reached `csi_dispatch()` with final
  byte `q` and param 3, and the only `case 'q'` in the file was the kitty-graphics APC parser. Window focus was
  already tracked, so the "blink only when focused" gate needed no new Wayland plumbing. The probe
  (`.scratch/s111/decusrprobe.c`, replaced by `.scratch/s112/qprobe.c`) fed twelve forms BEFORE any code change
  and every one was consumed and changed nothing; after the change the same probe prints xterm's table exactly.
  s112 took the tree from half-finished and non-building to green. **One contradiction inside the uncommitted
  s111 code was resolved rather than left to rot:** the `CURBLINK_*` header comment said an application may
  override the user's blink "in BOTH directions" while `blink_half_ms()` treats `conf.cursor_blink` as a CEILING
  — the code's rule was kept and the comment rewritten. The probe paid for itself immediately: it found three
  FAILs on its first run and all three were **errors in the new checks, not in the code** (a check written
  against `ESC[5 q`, a *blinking* underline, expecting steady). A/Bs in `.scratch/s112/` (`ab_model/`,
  `ab_shape/`); the limits of an object-file A/B are recorded in Key Decisions.
- **2026-10-20, s113-s114 — step 3, double-click a word / triple-click a line.** Probed first
  (`.scratch/s113/wrapprobe.c`) for the two shapes the rules must not break — a soft-wrapped line (`wrap[i] = 1`
  marks the row that WAS broken, indexed by the row SLOT, the same index `row[]` and `grid_vprow()` use, not by
  viewport row) and a wide glyph (anchor holds the codepoint with `ATTR_WIDE`, continuation has `ch == 0` AND
  `ATTR_WIDE`). The probe's emoji case was mangled by a C hex escape (`"\x9F"` swallows the next digit) — the
  CJK case proves the same layout, so it was left rather than fixed. s114 resumed by measuring, not by reading
  the s113 entry: `./bin/test_vt` reported 2 failures, both in the template group, exactly the state s113
  predicted — so the code half was in the tree and the docs/check half was not. Probe findings worth keeping:
  `Grid.wrap[i]` is NOT set until the character that crosses the margin is written, so a line ending exactly at
  the margin leaves the row below blank and `term_line_span` correctly stops — no off-by-one to fix
  (`.scratch/s114/wprobe3`); a word the window broke mid-word is selected only up to the break on each row
  (`wprobe2`: `word at (5,0) = 0..19`, `(5,1) = 0..7`), a limitation now stated in README and ISSUES.md, not
  something step 3 claims to solve. **Non-vacuity, both kinds, RUN not reasoned about** (`.scratch/s114/ab/` and
  `.scratch/s114/mutate.sh`, seven mutants): the interesting result is that **the never-strip-below-one-glyph
  guard mutant pinned NOTHING until a check was added for it** — a lone full-width `。` is a wide pair, so
  removing that guard sends the span INVERTED (`c1 < c0`) and no existing check noticed. Three of the new checks
  were red when first written and all three were the CHECK's fault, not the model's — `don’t` is 7 bytes not 6,
  a 26-char line does not reach the margin so `wrap[1]` never gets set, and `int r0, r1` at main()'s scope
  shadowed the emoji block's `r1` (-Wshadow, which is why the warning count is measured, not assumed). **Then
  the user confirmed step 3 in their own window** — "I checked and it worked" — closing the only owed verdict,
  including the `main.c` half no headless check reaches.
- **2026-10-19/20, s115-s117 — step 3b, the preview pane's text selection.** Probed first
  (`.scratch/s115/selprobe.c`, run not reasoned about): in a 30x12 pane the body starts at row 2 column 2, the
  head row is selectable too, a row span is the whole pane width with trailing blanks trimmed, a drag past the
  bottom clamps, the band stops at the last real glyph, wide glyphs and punctuation follow the session's rule
  because it is the same function. s116 stopped *between* two halves with a green tree (the model half in, the
  pointer half not), which is the state to resume from; s117 found the previous session had actually gone past
  its own "Next:" line and stopped before anything was verified or committed, so the first action was to
  re-measure, not to write. The harness was **RE-RUN, not quoted** (`.scratch/s116/mutate.py`, 13 kinds, every
  one caught); `no_clamp` printed 1 FAIL and then **died of SIGSEGV** — a finding in itself, since without the
  clamp the painter walks off the pane, so that check is pinned by a crash, not by a count. The `out[-1]`
  defect, the README/ISSUES claim the code did not support, and the two setup mistakes on the way (a block
  inserted in the MIDDLE of a group silently replacing the preview the checks below it read; a run printing
  `381 checks, 0 failures` from a STALE binary after a build that had errored) are in step 3b's rules and Key
  Decisions.
- **2026-10-20, s118 — step 4 (confirm-quit) opened with the measurement the plan asked for, then built.** The
  probe's first version reported fish IDLE with `sleep 6` "running" — the journal's own pitfall: a probe that
  hosts a shell but never answers that shell's queries measures a STALLED shell (fish's first 83 bytes are
  queries; the probe's own output count showed exactly 83 bytes at startup). `fgprobe2` answers them with the
  strings `src/vt.c`'s `TERM_RESP` sites send and proves the command ran by looking for its OUTPUT — and the
  needle matters: the shell echoes the typed line, so `echo DONE` proves nothing (the echo contains DONE);
  `printf 'O%sK\n' 2` prints `O2K`, which the echo does not contain. The measured predicate, what is in the tree
  and the exact next action are in "Current Task / 4". The session stopped mid-stage-A with the tree RED, and
  nothing of step 4 is committed — `ab0809a` + `8ccc73d` (step 3b) are still the pushed state.
- **2026-10-20, s120 — step 4 stage A finished: the tree back to green, then the checks step 4 owed.**
  Objective: take the RED tree (`test_vt` 1217/3, all three from the docs-pinning group) back to green and finish
  stage A of "Current Task / 4" — the docs half of `confirm_quit` plus the headless checks the pure decisions
  still lacked. Planned steps, in the order "Current Task / 4" already named: (1) `confirm_quit = running` +
  comment block in `config/kai-term.conf`, the README settings-table row, bump `ns == 36` to `37` in
  `tests/test_vt.c`; (2) config-group checks for the three values and the warn-and-keep path, and a new group for
  `fg_busy()` / `mux_running()` / `confirm_message()`; (3) an A/B with the busy answer stubbed to "never busy"
  plus the mutation of the `fg <= 0` guard; (4) stages B/C/D (layout notice row, render pass, main.c gate and
  modal keys); (5) README/ISSUES, both builds, every suite, `make asan`, commit, push.
  Expected outcome after (1): 1217 checks, 0 failures with the settings-name count at 37 — nothing about the
  parser changes, only the documents catch up to it.
  DONE (1a): `confirm_quit = running` with its comment block went into `config/kai-term.conf` after `split_max`,
  inside the "sessions (tabs) and layout" section — the same place the parser's branch sits in the ladder — and
  the block names the three values, the `&` limitation measured in s117, and the fact that a multi-session
  question says how many and which. Result: the file now lists 37 settings names; the template-parity check that
  failed on "config/kai-term.conf does not list confirm_quit" has one fewer reason to fail.
  DONE (1b): the README settings-table row went in after `split_max`, same order as the template. NOTE for the
  next session: that row describes the FINISHED behaviour (the question, the multi-session list), and stages B/C/D
  are not in the tree yet — so this state must not be committed on its own. The row exists now because the
  name-coverage check demands template+README for every name the parser accepts; the feature is what makes the
  sentence true, and it gets committed together with this row or not at all.
  DONE (1c): `ns == 36` -> `ns == 37` in `tests/test_vt.c` (the hard-coded count beside the derived one). Result,
  measured: `make bin/test_vt` 0 warning lines, `./bin/test_vt` **1217 checks, 0 failures** — the tree is GREEN
  again, and the settings-name count is 37 in all three places (parser, template, README).
  DONE (2a, probe before checks): `.scratch/s120/fgprobe3.c` — a pty with a HAND-BUILT process tree on it
  (no shell binary, so no history is touched and no startup query needs answering): the child does what
  `pty_spawn`'s child does (open the slave, `setsid`, `TIOCSCTTY`) and then what an interactive shell does
  (`tcsetpgrp` its own group; on a byte from a pipe, fork a command into a group of its own and hand THAT
  group the foreground). Every wait polls for the state it needs, so nothing sleeps and hopes. Measured:
  fresh pty before anyone takes the group `fg = 0` -> idle; shell at the prompt `fg == child` -> idle;
  command in its own group `fg != child` -> **busy**; shell exited with the command still alive `fg = 0` ->
  idle (the dead-session case, reproduced without bash); `mfd = -1` -> idle with no syscall at all; a CLOSED
  master answers `tcgetpgrp` = -1 EBADF, which `fg <= 0` also turns into idle. One NEW limitation found and
  it is real: `pty_spawn(&q, "sleep 30", ...)` reads **idle** (`fg == child`), because `sh -c` runs its
  command in its OWN process group — the predicate sees the foreground GROUP, so a command that never takes
  a new group is invisible to it. An interactive shell always takes one for a foreground job, which is the
  case that matters, but the sentence belongs in README/ISSUES next to the `&` limitation.
  DONE (2b): writing the checks found a crash in `confirm_message()` before they could pin it — the
  multi-session branch called `session_list(idx, ...)` with no `idx` guard, so `confirm_message(CONFIRM_QUIT,
  3, NULL, ...)` dereferenced NULL. It is a public entry point in the header, so it is guarded there, not in
  a caller (the step-3b `cap == 0` lesson, same class). While in it: the list is capped at FOUR numbers and
  the wording shortened, because the s118 form ("A command is still running in this session. Close it
  anyway?  [Enter] yes   [Esc] no") is 84 characters and would have been clipped by an 80-column window — a
  question whose answer hint is cut off is worse than a shorter question. The longest form now fits 80, and
  the exact-text checks are what pins that. Result: `make -j8` exit 0, 0 warning lines.
  DONE (2c): the checks are in `tests/test_vt.c` — a `confirm_quit` config group (the three values and every
  spelling, and warn-and-KEEP proved by setting `never` first and then feeding a value that means nothing, so a
  parser that fell back to the default on a typo cannot pass), a pure `fg_busy()` group, a group against a REAL
  pty with a hand-built process tree (the new `FakeSess` helpers: no shell binary, every wait polls for the state
  it needs), a `mux_running()` group (busy tabs at 0 and 3, non-adjacent so an off-by-one list cannot pass; max
  caps the list not the count; NULL list; a session that ended drops out), and a `confirm_message()` group with
  every branch's exact text copied from `.scratch/s120/msgprobe` plus the cap/NULL cases. Result, measured:
  `make bin/test_vt` 0 warning lines, `./bin/test_vt` **1291 checks, 0 failures** (was 1217/0 before the group;
  +74). Two things the harness found on the way, both now in the code: (i) writing to the control pipe after the
  session died raises **SIGPIPE, which killed the suite** — it looked exactly like a crash in the code under test,
  so `fake_sess_open` ignores it (the harness owns those pipes; `src/` never needs it, a pty master answers a
  dead slave with EIO); (ii) reaping the fake shell in one place and calling the close helper afterwards means a
  second `waitpid` on a reaped pid, so the helper treats `ECHILD` as "already reaped" and the caller zeroes
  `p.child` when it reaped it itself. Three `-Wshadow` warnings appeared on the first test build (`w1`, `n`,
  `small` all exist at main()'s scope) — that is what measuring the warning lines is for.
  DONE (2d, the A/B and the mutants, all RUN): `.scratch/s120/mutate.py`, 19 kinds. Feature A/B (`pty_fg_busy()`
  stubbed to "never busy", the answer the tree gave before step 4): **6 failures** — the pty busy check and the
  four mux count checks; the survivors are the idle-side checks ("no pty is idle", "a dead session is idle"),
  which an always-idle tree satisfies by construction and which the mutants below pin instead. Mutants and what
  each cost: `fg_no_guards` 6, `fg_no_child_guard` 2, `mux_max_caps_count` 1, `mux_idx_one_based` 3,
  `mux_no_null_guard` **died of SIGSEGV** (a crash is a finding, not a counted check), `list_seven` 2,
  `list_no_and` 2, `list_no_ellipsis` 1, `msg_zero_based` 1, `msg_no_list` 2, `msg_cap0_unguarded` 1,
  `msg_null_idx_unguarded` 1, `cfg_no_auto` 1, `cfg_bad_resets` 1, `cfg_default_never` 1, `cfg_no_key` 13.
  TWO SURVIVORS, said out loud rather than hidden: dropping `pty_fg_busy()`'s `mfd < 0` guard (tcgetpgrp(-1)
  answers -1, which `fg_busy()` calls idle anyway — the guard is a syscall saved, not a behaviour) and dropping
  `mux_running()`'s `n < max` bound (the suite's `idx[]` has room for every index, so writing past `max` is
  invisible to these checks). `session_list()`'s own `!idx` guard is likewise unreachable through the public
  entry point and is pinned by the comment only.
  THE MUTATION THAT MATTERED: `cfg_no_auto` ran **0 failures** against the first version of the config group.
  Each spelling was being fed into a config whose default already equalled the expected answer, so a word the
  parser did not know still passed — the vacuity the harness exists to catch. Fixed by feeding every spelling
  AFTER the opposite value, so the check proves the word CHANGED the state; the mutant then failed exactly 1.
  Re-measured after the fix: `make bin/test_vt` 0 warning lines, `./bin/test_vt` **1291 checks, 0 failures**.
  Measured starting point, re-run not copied: `make clean && make -j8` exit 0 / 0 warning lines; test build 0
  warning lines; `test_vt` **1217 checks, 3 failures** (the three named at lines 2913, 2920, 2939), `dbg`
  MODEL OK, `test_fm` 384/0, `test_render` 495/0. `git status -sb`: `main` even with `origin/main` at `8ccc73d`,
  modified `AGENT.md Makefile include/kai-term/kai-term.h src/config.c src/mux.c src/pty.c` — the s118 work is
  still uncommitted, as that entry said.
  DONE (3, stage B — the notice row's geometry, `src/layout.c`): `layout_compute()` now takes
  `rq->notice_on`/`rq->notice_text` and reserves ONE cell row at the BOTTOM of the grid area, before the
  file-manager band and before the panes are measured, so the band's PERCENT applies to what is left. Two
  decisions were made here rather than copied from the bar: the row costs **`ch` and no gap** (the bar costs
  `ch + gap` because its ITEMS need separating; one full-width row does not, and costing the gap would have
  taken a SECOND row from a window whose height is a whole number of rows — measured: 986 px grid area, 17 px
  cell, 58 rows -> 57 with the row, 56 with row+gap), and the terminal wins over the question, so a window
  whose grid area cannot keep one row after paying gets `notice_on == 0` and closes without asking. The text
  is cut to the row's columns IN layout.c (`clip_utf8`), which is what makes the clipping checkable.
  Found while writing it: `snprintf` into a fixed buffer can split a UTF-8 sequence, and `clip_utf8` only
  ever cuts at an edge, so a byte-bound cut left a dangling lead byte — for the notice row AND, as it turned
  out, for every bar label whose OSC title is longer than `BAR_LABEL_MAX` (a claim the existing
  "never mid-sequence" check did not actually cover). Fixed with one `trim_partial_utf8()` in layout.c applied
  to both, with checks on both sides. 29 new checks (1291 -> **1320, 0 failures**), and every one of them
  non-vacuous: the stage-B feature A/B (`layout_compute` ignoring `notice_on`, i.e. the tree before this
  stage) fails **21**, and the nine mutants fail — `b_no_reserve` 12, `b_cost_gap` 6, `b_at_top` 7,
  `b_no_short_guard` 2, `b_no_clip` 1, `b_cols_from_window` 1, `b_no_trim_notice` 2, `b_no_trim_label` 1,
  `b_band_sees_full_grid` 14 (that one breaks the pre-existing fm checks too, which is the point: measuring
  the band against the grid area the question has not paid for moves EVERYTHING below it). Existing geometry
  checks were re-run, not assumed: all still pass, because `LayoutReq` is memset in both `lreq()` and
  `layout_apply()`, so `notice_on` is 0 for every request that does not ask for it.
  DONE (4, stage C — the row is drawn, `src/render.c`): one full-width field quad at the end of batch A (next
  to the bar items, so the pictures go over it and its text goes over them), its text in batch B with the same
  text font and the same per-column bound a bar label uses, and the staging budget (`maxq`) grown by
  `NOTICE_MAX + 1` when the row is on — a budget that forgets a pass is a buffer overrun waiting for a wide
  window. Colours come from `render_chrome()` (`notice_bg` = the selection highlight, `notice_fg` = the default
  ink), which is where a configured colour is checkable. What is checkable headlessly is the COLOUR decision
  (`tests/test_render.c`, 3 checks) and the draw loop by source scan (2 checks: the two colour tokens are both
  derived and consumed, and `notice_on` is read three times — the field pass, the text pass, the budget);
  the row's LOOK is owed to the user's eye like every other pixel. 5 new checks (495 -> **500, 0 failures**),
  and the harness for them is `.scratch/s120/mutate_render.py`, which taught something worth keeping: a source
  scan reads a file BY PATH, so running the mutant binary from the project root scans the UNMUTATED
  `src/render.c` and the first two mutants passed for the wrong reason. Fixed by building AND running in a
  staging tree (`mut_render/<name>/src/` holds the patched file and the files the scans read). Measured after
  that fix: `c_chrome_no_derive` 4, `c_field_hard_colour` 1, `c_no_text_pass` 1, feature A/B (the whole row
  never drawn) 5 — no survivors.
- **2026-10-20, s119 — this cleanup (docs only, no source touched).** Pre-cleanup text:
  `.scratch/s119/AGENT.md.pre-cleanup` — **2272 lines / 219,649 bytes**, against **1746 lines / 165,832 bytes**
  here (`wc -l -c` on both; the committed side is `git show 8ccc73d:AGENT.md | wc -l` = 2179, the working tree
  holding the s118 entry on top). What went: the s107-s117 session narratives, and the per-session "objective /
  plan / expected outcome / measured starting point" blocks that git already holds — the Progress Journal fell
  839 -> 306 lines. What did NOT go, deliberately: the invariants, the pitfalls, the diagnosis reference and the
  step-rule sections, which are this file's stated purpose; those grew slightly as step-4 detail was consolidated
  into "Current Task". Superseded check counts, source line numbers and per-session A/B blow-by-blow were
  dropped as git's job. Every rule a change must not break, every measured number a check rests on, every
  pitfall, every mechanism name and every `.scratch/` path a harness lives at was checked to survive outside the
  deleted text before it was deleted (the check: build the post-deletion text and grep it for each name). The
  stale counts in "Project Files & Facts" were re-measured and corrected: the settings template and README's
  settings table hold **36** names each, the parser accepts **37** (`confirm_quit` is the gap — that is the RED),
  and `usage()` has **9** long options. Re-measured the tree while doing it: both builds exit 0 with 0 warning
  lines, `test_vt` 1217/3, `dbg` MODEL OK, `test_fm` 384/0, `test_render` 495/0, headless CLI 91/0/13.
- **2026-10-20, s121 — step 4 stages B/C/D: what the last session left, and what it still owed.** Objective:
  the journal's last word was "stage A DONE, B/C/D to go", but the working tree says otherwise — `src/layout.c`,
  `src/render.c` and `src/main.c` are all modified and the s120 entry ends after stage C. So the first job is to
  find the last VERIFIED state rather than trust either the status line or the file list. Planned steps: (1)
  read the diff of every modified file and say what stage each one is; (2) re-measure the whole tree (both
  builds, every suite, `make asan`, the headless CLI group) and report the numbers, not the ones on this page;
  (3) A/B what s120 never A/B'd — stage D's six wiring checks — against the real pre-stage-D `main.c` (`git show
  HEAD:src/main.c`) and against a mutant that stops using the shared predicate, plus the seven headless config
  checks in `tests/cli_test.sh` against a `config.c` with no `confirm_quit` branch; (4) fix anything the A/B
  shows to be vacuous; (5) correct the stale status/task/next-steps text; (6) commit step 4 as one commit and
  push. Expected outcome: a green tree, a step-4 feature whose every headless decision has a check that has
  actually failed against the code that lacks it, and a journal that says what is really in the tree.
  Known-unfinished when this entry was written: nothing in the feature itself is missing from the tree; what is
  missing is the verification record, and the look of the row is owed to the user's eye like every other pixel.
  DONE (1, the state read from the tree rather than from the page): stages B, C **and D** are all in the tree —
  `src/layout.c` 13:39, `src/render.c` 13:46, `src/main.c` 13:54 (the gate, the modal keys, `confirm_answer`,
  `confirm_clear`), `tests/test_render.c` 13:55 (six `src/main.c` wiring scans), `tests/cli_test.sh` 13:58 (the
  headless config group), README 13:59, ISSUES 14:00. The s120 entry simply stopped after stage C, so the
  journal was a whole stage behind the tree while its "Current Task" text still listed B/C/D as to-do.
  DONE (2, re-measured, not copied): `make clean && make -j8` exit 0 / **0** warning lines; the four test
  binaries exit 0 / **0** warning lines; `./bin/test_vt` **1320 checks, 0 failures**; `./bin/dbg` **MODEL OK (0
  problems)**; `./bin/test_fm` **384, 0**; `./bin/test_render` **506, 0**; `KAI_TERM_TEST_NO_WINDOW=1
  tests/cli_test.sh` **99 checks, 0 failures, 13 skipped** (no window was mapped — the display is the user's);
  `make asan` exit 0 / **0** warning lines and all four asan binaries exit 0 with no LeakSanitizer or UBSan
  report.
  DONE (3, the A/B stage D never had): `.scratch/s121/ab_stage_d.py` builds AND runs in a staging tree, because
  the six wiring checks are source scans and a scan reads `src/main.c` BY PATH (the s120 lesson). `d_head_main`
  — `src/main.c` exactly as committed at HEAD, the real tree before stage D rather than a hand-made patch —
  fails **6 of 6** wiring checks. `d_predicate_local` — stage D present but main.c invents its own
  `tcgetpgrp` test and its own count instead of `pty_fg_busy()`/`mux_running()` — fails exactly **1**, the
  shared-predicate count; nothing else in the suite can see a second busy-test starting to drift. The stage-D
  mutants sitting in `.scratch/s120/mutate_render.py` were also RUN at last: `d_no_window_gate` 1,
  `d_bar_ungated` 1, `d_key_ungated` 1, `d_no_layout_input` 1, `d_modal_gone` 1, `d_feature_ab_no_stage_d` 4 —
  no survivors.
  DONE (4, the CLI A/B, and the two things it found): `.scratch/s121/ab_cli.sh` builds a WHOLE kai-term from a
  patched copy of the tree (the suite reads `config/kai-term.conf` relative to its cwd, so a mutant binary run
  from the project root would be checked against the unmutated template — everything is staged). `cfg_no_key`
  (no `confirm_quit` branch in `apply_setting()` at all) fails **4** of the new checks; `cfg_keeps_always` (the
  bad-value path resets to the default instead of keeping the value already set) fails exactly **1**, the
  warn-and-KEEP check. FINDING 1: "`confirm_quit = always` is accepted" grepped for the ABSENCE of `is not a
  value`, which a parser that has never heard of the setting also satisfies — the check was vacuous in exactly
  the direction that matters. Fixed by adding the check that a good value leaves no `confirm_quit` line in the
  log at all (measured: it does), which `cfg_no_key` then fails: 98 -> **99** checks. FINDING 2, about the
  harness itself: the `cfg_keeps_always` patch died of a Python `IndentationError` inside the heredoc, `make`
  succeeded on the UNPATCHED copy and the suite printed **"0 failures" for a mutant that never existed**. The
  harness now refuses to report a result unless the patch is verifiably in the source it built.
  DONE (4b, one measured limitation the docs still owed): `.scratch/s121/espawn` spawns `-e` commands on a real
  pty through `pty_spawn()` itself and prints `tcgetpgrp` beside `p.child` with `/proc/<child>/comm` proving the
  command is alive: `sleep 30` and `sleep 30 | cat` are both **idle for the whole 900 ms**, because `sh -c` puts
  the command in the child's OWN foreground group (`fg == child`). So `confirm_quit = running` never asks about a
  `-e` window — the s120 entry had noted this and README/ISSUES never got the sentence. Both now say it, and
  point at `confirm_quit = always` as the setting for that case.
  DONE (5): the stale status text corrected — "Current Status" (the s121 numbers, asan green on this tree,
  step 4's row added to the owed-to-the-eye list with its recipe), "Current Task / 4" (stages A-D done, the
  rules B/C/D added, the next action now the user's verdict), "Where each step stands", and Next Steps 1.
  DONE (6, close-out): step 4 committed as ONE commit, `4bcd56b` "Confirm-quit: a window asks before a close
  throws work away" — 15 files, the feature plus its checks plus its docs, because the README row and the
  template block are only true together with the gate. `bin/`, `build/` and `.scratch/` stayed out (`.scratch/`
  is not in `.gitignore`, so it shows untracked and was never `git add`-ed). `git fetch` first (clean, nothing
  new upstream), then `git push origin main`: `8ccc73d..4bcd56b`, and `git status -sb` now reads `main` even
  with `origin/main` — verified from git, not asserted. STILL OWED: the row's look in the user's window (the
  recipe is in "Current Status"), and the rest of the owed-to-the-eye list.
- **2026-10-20, s122 — the user's window-close report: a real bug, and a redesign of the window question.**
  Their words: "It just doesn't quit. It doesn't ask anything" — closing with niri's `Mod+Q` — and "but it
  works for tabs, can we keep the current implementation for Tabs only. and make the modification for the full
  windows close?", with a picture of the shape they want: a centred box, one sentence naming the running
  process, and two buttons to click. Text they asked for: `Warning ! "name of the process" is still running.
  Are you sure you want to close it ?`
  Objective: (1) find and fix why a window close neither asks nor closes; (2) keep the tab question exactly as
  it is; (3) give the WINDOW close a centred, mouse-clickable Yes/No dialog that names the foreground process.
  Planned steps: probe the state machine -> fix the cause + pin it with a check -> the process name (pty.c) ->
  the wording (mux.c) -> the dialog geometry (layout.c) -> the pixels (render.c) -> main.c's state, gate and
  pointer hit test -> docs, both builds, every suite, asan, commit, push.
  Expected outcome: `Mod+Q` on an idle window still closes it at once; on a busy one it puts up the box, and
  either button or Enter/Esc answers it.
  DONE (1, the cause, measured not guessed): `.scratch/s122/kindprobe.c` replicates main.c's state machine with
  the REAL enum values and prints what each path decides. `enum { CONFIRM_QUIT = 0, CONFIRM_CLOSE_TAB = 1 }`
  collides with main.c's `confirm_kind == 0` meaning "no question is up", so asking about the WINDOW set the
  state to "nothing is being asked": `notice_drawn=0 modal_keyboard=0 window_still_running=1` — asked? NO,
  closed? NO — while the tab path prints `kind=1 notice_drawn=1`. That is the report exactly: the gate had
  already swallowed the close, nothing was ever drawn, and the keyboard was never modal. Tabs worked because
  CONFIRM_CLOSE_TAB is 1.
  DONE (2, the fix): `enum { CONFIRM_NONE = 0, CONFIRM_QUIT = 1, CONFIRM_CLOSE_TAB = 2 }` with the trap named in
  the comment, and main.c comparing against CONFIRM_NONE everywhere instead of truthiness. Plus the rule that
  makes this class of bug survivable rather than fatal: **a close is never blocked by a question the user cannot
  see.** `confirm_show()` now returns whether the question really is on screen (it reads `g_layout.notice_on`
  back after `layout_apply()`), and the gate closes instead of swallowing when it is not — so the worst case is
  the old behaviour (close at once), never a window that answers neither button.
  DONE (3, the checks, all non-vacuous): three checks in `tests/test_vt.c` pin the enum invariant (CONFIRM_NONE
  is 0; no question kind is 0; the two kinds differ), and `tests/test_render.c` gains `src_sub_count()` — a
  substring scan, because a WORD count also counts a token that only appears in a comment, which is how "main.c
  feeds the row to the layout" could stay green after the assignment was deleted. `.scratch/s122/mutate.py`, all
  RUN: `kind_zero` (the shipped bug) 1, `no_visible_guard` 1, `no_layout_input` 1, `notice_always_on` (the
  geometry decision removed) 2 — no survivors. Measured: both builds exit 0 with 0 warning lines, `test_vt`
  **1323, 0**, `dbg` MODEL OK, `test_fm` **384, 0**, `test_render` **507, 0**.
  TWO HARNESS BUGS FOUND AND FIXED, both worth keeping: the staging tree was missing `config/`, `README.md` and
  the other `tests/*.c`, which test_vt reads by relative path — 6 failures that had nothing to do with the
  mutant; and, worse, the harness **symlinked `include/` and then wrote the patched header through it,
  mutating the real project tree**. Now a copy, and every mutant run ends by asserting the project's own header
  still holds the expected enum and no source carries mutant text.
- **2026-10-20, s123 — the WINDOW-close dialog, continued. The tree was RED when this session opened.**
  The user's instruction: finish the redesign before pushing — pushing a broken feature is worse than not
  pushing. Read the state from the tree, not from the page: `git status -sb` says `main` **ahead 1** of
  `origin/main` (`357b4ee`, s122's enum fix, is NOT pushed), and three files are modified and uncommitted
  (`include/kai-term/kai-term.h`, `src/layout.c`, `src/vt.c`) — the half-started dialog.
  Objective: the user's report of 2026-10-20 asked for a WINDOW close to ask with a centred box, one sentence
  naming the running process, and two clickable buttons, while a TAB close keeps the one-row notice. Text they
  asked for, verbatim: `Warning ! "name of the process" is still running. Are you sure you want to close it ?`
  Planned steps, each left green: (0) finish the half-done `trim_partial_utf8` -> `utf8_trim_partial` move so
  the tree builds; (1) the process name — pure `proc_comm_name()` + syscall `pty_fg_name()` in pty.c;
  (2) the wording — `confirm_warning()` in mux.c; (3) the geometry — `wrap_sentence()`, the box, the two
  buttons and `layout_hit_dialog()` in layout.c; (4) the pixels — `Chrome.dialog_*` in `render_chrome()` and
  the draw pass in `render_draw()`; (5) main.c's state, gate, layout feed, pointer hit test and modal keys;
  (6) checks with an A/B and mutants, README + ISSUES, both builds, every suite, asan.
  Expected outcome: `Mod+Q` on a busy window puts up a centred box that names the process and answers to a
  click or to Enter/Esc; on an idle window it still closes at once; a tab close is unchanged.
  DONE (0): the tree did not build. `src/layout.c:238` still called `trim_partial_utf8` after the function was
  moved to `src/vt.c` as `utf8_trim_partial` — `error: implicit declaration of function 'trim_partial_utf8'`,
  `make: *** [Makefile:45: build/layout.o] Error 1`. Fixed (call site and the comment naming it). Measured:
  `make -j8` now exits 0 with **one** warning line left, `src/layout.c:77:13: 'wrap_sentence' defined but not
  used` — that is the unwired dialog, not a defect; step (3) uses it. The tree is therefore NOT green yet and
  nothing is committed until it is.
  KNOWN-UNFINISHED at this point: steps 1-6 above. What is in the tree is the header's declarations and a
  `wrap_sentence()` that is never called — and that first draft has a real bug in it: it compares a pointer
  into the SOURCE string with `dst` (`last_space > dst`, `last_space - dst`) to find the wrap point, which is
  two different objects. Step (3) rewrites it over indices into the source.
  DONE (1, the process name): `proc_comm_name()` (pure) and `pty_fg_name()` (the syscall half) in `src/pty.c`.
  The name comes from `/proc/<fg>/comm` where `fg` is the SAME `tcgetpgrp()` value `fg_busy()` already reads —
  the foreground group's leader is exactly the process at risk, so the box names the thing the predicate
  protected. The pure half trims the newline, refuses an empty name and refuses any byte < 0x20 or 0x7f (the
  name goes inside a quoted sentence drawn one cell per codepoint), allows UTF-8, and runs the result through
  `utf8_trim_partial` because the byte cap can cut a multi-byte name in half. `pty_fg_name` uses open/read, not
  fopen: it runs inside a Wayland event handler on the way to closing the window, so no malloc and no stdio
  buffer. It refuses `fg <= 0` — the same dead-session guard `fg_busy()` refuses on.
  DONE (2, the wording): `confirm_warning()` in `src/mux.c` (plus the static `name_phrase()` / `name_list()`).
  The wording lives in mux.c for the same reason `confirm_message()` does — main.c is in no test binary, so a
  string there is a claim and a string here is a check. Measured strings (`.scratch/s123/msgprobe`, the checks
  copy these, they are not typed from the page):
    1 named   `Warning ! "nano" is still running. Are you sure you want to close it ?`  (the user's sentence)
    1 unnamed `Warning ! session 3 is still running. Are you sure you want to close it ?`
    2 named   `Warning ! "nano" and "sleep" are still running. Are you sure you want to close them ?`
    3 named   `Warning ! "nano", "sleep" and "htop" are still running. ... close them ?`
    5 named   `Warning ! "a", "b", "c" and 2 more are still running. ... close them ?`
    n <= 0    `Warning ! Are you sure you want to close Kai-Term ?`
  Three names is the cap, and the "and" goes before the last NAME only when the list ENDS with a name —
  `"a", "b" and "c" and 2 more` reads as a mistake. A name that could not be read falls back to `session N`
  rather than inventing one ("a command" is exactly what the user's box replaces).
  DONE (3, the geometry): `wrap_sentence()` rewritten, the box and its two buttons in `layout_compute()`, and
  `layout_hit_dialog()`. The first draft in the tree was wrong and is gone: it looked for the wrap point by
  comparing a pointer into the SOURCE string against `dst` (`last_space > dst`, `last_space - dst`) — two
  different objects, so the pointer compare and the subtraction are both meaningless. The rewrite works on
  byte offsets into the source and keeps `cut`, the offset of the last COMPLETED word.
  Measured (`.scratch/s123/dlgprobe`, `dlgprobe2` — cell 10x20, pad 8, gap 4):
  * **The box costs the terminal no row** — the whole point of drawing it over the grid. Same window with the
    box off and on: `pane=(8,8 1184x784) cols=118 rows=39` both times.
  * The box hugs its text: a 70-column sentence in a 1200x800 window gives `box=(240,370 720x60)` —
    (70+2) cells wide, centred in the GRID area (not the window), so the bar and the margin are excluded.
  * The buttons set a width FLOOR: `need_w = (5+4)cw + 2cw + 2pad`. A 60px-wide window therefore gets
    `on=0` — a box narrower than the two answers it holds cannot be clicked, and main.c's rule (a close is
    never blocked by a question the user cannot see) closes the window instead.
  * A word wider than the line is hard-broken at the column limit, never inside a codepoint: a 70-letter name
    at 36 columns gives `line[1] = "aaaa...aaa` and `line[2] = aaa...aaa"`.
  * A sentence longer than DIALOG_LINES keeps the last line and ends it in ` ...` (`word13 word14 word15
    word16 word ...`) — visible shortening, not a silent cut, and the buttons are still there.
  * `layout_hit_dialog` returns the button index at each button centre and -1 at the box centre and outside.
    A click inside the box but off both buttons answers NOTHING on purpose: a click near a button must not
    answer it by accident and a click outside must not be read as a NO the user did not give.
  * `proc_comm_name` measured (`.scratch/s123/msgprobe`, `nameprobe2`): trims `\n` and `\r\n`; refuses empty,
    NULL, a control byte and DEL; allows UTF-8; a byte cap that would split a codepoint refuses the whole name
    rather than emitting a dangling lead byte; **`cap == 0` touches ZERO bytes of the caller's buffer** — the
    same lesson s116 pinned for `fm_preview_selection_text`'s `out[-1]`, applied before it could happen here.
  * `pty_fg_name` on a REAL pty (`.scratch/s123/msgprobe`) names the process: `sleep` from +300 ms on. Note
    `pty_fg_busy` says **idle** for that whole run — this is the documented `-e` limitation (s121: `sh -c` puts
    the command in the child's own foreground group), NOT a new bug; the name still reads because the group
    leader is the command. Two probe bugs worth remembering: `"a\x7fb"` is ONE hex escape to gcc (0x7fb), not
    DEL + `b`, so the first probe never tested DEL at all; and a `cap == 0` tail check that reads `strlen()` of
    a buffer the function never terminated reports a write that never happened.
- **2026-10-20, s124 — the WINDOW-close dialog, finished. The tree was RED when this session opened.**
  The user came back with two things: "continue working on Kai-Term", and **the verdict step 4 owed since
  s121 — they had eyes on the quit confirmation and it works.** That closes the step-4 row from the
  "owed to the user's eye" list (the ROW, which is what was committed; the box below is new and is a NEW
  debt on that list).
  Read the state from the tree, not from the page. `git status -sb`: `main` **ahead 1** of `origin/main`
  (`357b4ee`, s122's enum fix, is still NOT pushed) and ten files modified and uncommitted. The s123 entry
  stopped at step (3); the tree actually holds steps (4) and (5) too — `Chrome.dialog_*` derived AND consumed
  in `render_draw()`, and main.c's state/gate/layout-feed/pointer-hit/modal-keys wiring — so the page
  understated the tree by two stages. Measured at open:
  * `make -j8` exit 0, **0 warning lines** (the `wrap_sentence defined but not used` warning s123 left is
    gone — the function is wired now).
  * `make bin/test_vt bin/dbg bin/test_fm bin/test_render` exit 0 but **1 warning line**:
    `tests/test_vt.c:4580: declaration of 'n' shadows a previous local [-Wshadow]`.
  * `./bin/test_vt` **1403 checks, 2 FAILURES**; `dbg` MODEL OK; `test_fm` 384/0; `test_render` 524/0.
  Objective: take the tree to green and ship the box — the user's sentence, a centred box, two clickable
  buttons — with checks that are non-vacuous, both builds clean, asan clean, README/ISSUES true, committed
  and pushed.
  Planned steps: (1) find out whether each failure is a code bug or a check bug, by probe, not by reading;
  (2) fix whichever side is wrong; (3) kill the shadow warning; (4) re-run every suite and both builds;
  (5) A/B and mutants on the new checks; (6) README + ISSUES; (7) `make asan`; (8) commit, `git fetch`, push.
  MEASURED (1) — **both failures are bad checks, not bad code**, and both were written from the page instead
  of from a probe (`.scratch/s124/wrapprobe`, `.scratch/s124/nameprobe`):
  * `dialog layout: no line ends on a UTF-8 continuation byte` can NEVER pass for real UTF-8 text: a COMPLETE
    2-byte codepoint ENDS on a continuation byte. The probe shows the wrap is correct — 150 x U+00E9 at
    33 columns gives three lines of 66 bytes = 33 whole codepoints each, and the last line ends ` ...`;
    the flagged byte `0xa9` is the second byte of a complete U+00E9. The property that matters is "no line
    ends on an INCOMPLETE sequence", which is what the check has to say.
  * `comm: a cap that would SPLIT a codepoint refuses the name` tests `cap = 3` on `"éà"`, and cap 3 does not
    split anything: `keep = cap - 1 = 2` bytes is exactly one whole codepoint. The measured contract is
    **keep the whole codepoints that fit, refuse only when not even one fits**: cap 1,2 -> 0; cap 3,4 -> `é`;
    cap 5,6 -> `éà`; a 4-byte emoji refuses at caps 1-4 and passes at 5; the canary past the cap is untouched
    at every cap. This CONTRADICTS the s123 line above ("refuses the whole name") — that line is what the
    check was written from, and it is wrong; the neighbouring cap=4 check in the same group already asserted
    the opposite (shortening), which is how the pair could sit in one file unnoticed.
  Next action: rewrite those two checks over the measured contract, then steps (3)-(8).
  DONE (2, the two checks rewritten over the measurement, `tests/test_vt.c`):
  * The comm cap block now pins the whole measured ladder instead of one wrong case: cap 5 keeps `éà`, cap 4
    (a bound INSIDE a sequence) trims the partial one and returns `é`, cap 3 (a bound ON an edge) keeps `é`,
    cap 2 refuses because not one codepoint fits, the 4-byte emoji refuses at cap 4 and passes at cap 5.
    Two new cases came with it — the emoji at a cap that HOLDS it, so the refusal check cannot be passed by a
    parser that rejects every multi-byte name.
  * The wrap check got a real validator, `utf8_whole()`, which decodes from the start and fails on a truncated
    sequence or on a continuation byte where a lead byte belongs. The old predicate is gone. A `drew_multi`
    guard sits in front of it so the check cannot be passed by a box that drew nothing, and a second case
    exercises the OTHER branch — a single word wider than the line, hard-broken at a column limit — which the
    first draft never touched.
  DONE (3, the shadow warning): it went with the deleted `size_t n` in the old predicate; no separate fix was
  needed and none was invented.
  MEASURED (4) — both builds and every suite, at this tree:
  * `make bin/test_vt bin/dbg bin/test_fm bin/test_render` -> **0 warning lines** (`grep -ciE 'warning|error'`).
  * `./bin/test_vt` **1408 checks, 0 failures** (1403 with the 2 failures before; +5 net from the rewritten
    comm ladder and the two new wrap cases). `./bin/dbg` **MODEL OK (0 problems)**. `./bin/test_fm` **384/0**.
    `./bin/test_render` **524/0**.
  * `./tests/cli_test.sh` -> **CLI: 149 checks, 0 failures, 0 skipped**. Note what that means: this machine has
    a live `WAYLAND_DISPLAY=wayland-1`, so the probe window passed and the **windowed group ran for real** —
    149 is no longer the arithmetic s121 predicted, it is a measurement, and it is the first windowed run since
    s109. It mapped ~15 real windows; that is a thing to ask the user about before repeating, not a routine
    command.
  Next action: (5) A/B and mutants on the s123/s124 checks — every new check has to be shown to fail when the
  behaviour it names is removed.
  DONE (5, `.scratch/s124/mutate.py`, 22 mutants + a no-op control). The control is not decoration: it builds
  and runs the STAGING tree with nothing removed and must report 0 failures, or "KILLED" would only mean the
  staging tree was already broken. It does (1408/0 and 524/0 at the time it was first run).
  **The harness found a real bug, which is the whole reason it is worth writing.** `wrap_no_trim` SURVIVED the
  first run: removing `utf8_trim_partial()` from `wrap_sentence()` changed nothing any check could see. Chasing
  why led to `.scratch/s124/wrap2`, which shows what happens when a line's BYTE bound (127) binds before its
  COLUMN bound does — only in a WIDE box, which no check in the tree used:
  * `wrap_sentence()` hard-broke a word at 127 bytes, trimmed the partial codepoint out of the line, and then
    advanced the source pointer by the UNTRIMMED count. Two consequences, both measured: the trimmed character
    vanishes from the sentence (147 of 150 codepoints reach the screen), and the NEXT line starts on its
    leftover continuation byte (`line[1] = a9 c3 a9 ...`), which the font draws as a codepoint of its own.
    With 3-byte characters two of the four lines start mid-codepoint.
  * Fix in `src/layout.c`: the line trims itself in the hard-break branch and `p` then steps over exactly the
    bytes the line KEPT. "A cut has to agree with the advance."
  Two more holes the harness exposed, both in the CHECKS rather than the code:
  * A sentence of 150 identical characters cannot reveal a gap — every byte position looks like every other,
    so the "nothing was dropped" check passed a mutant that had skipped a whole codepoint. The inputs are now
    VARYING (U+00C0, U+00C1, ... and U+2800, U+2801, ...).
  * `utf8_whole()` as first written accepted a continuation byte that STARTS a codepoint (it derived the
    length from whatever byte it was on). It now refuses one, which is what catches the second half of the bug.
  Four cuts in `wrap_sentence()` now have a check and a mutant each — the hard-break trim, the advance that has
  to agree with it, the `" ...` shortening clamp (reachable only with a sentence too long for a WIDE box: the
  `" ...` hides the bad byte from any check that only looks at a line's tail), and the post-loop trim that
  handles malformed INPUT. Plus a case for a sentence that simply ends mid-codepoint.
  MEASURED (final, this tree): both builds **0 warning lines**; `./bin/test_vt` **1413 checks, 0 failures**;
  `./bin/dbg` **MODEL OK (0 problems)**; `./bin/test_fm` **384/0**; `./bin/test_render` **524/0**.
  A trap worth recording: mid-edit, `make bin/test_vt` FAILED (`conflicting types for 'big'`, `'ln' shadows`) —
  and the suite still printed its old numbers, because a failed link leaves the previous binary in `bin/`. The
  1412 I nearly wrote down was a stale binary. Re-measure after a clean build, never from the last run.
  Mutant table (each row is one removed decision; KILLED = at least one suite noticed):
    pty_no_trim 3 | pty_no_control 3 | pty_cap_offbyone 5 | mux_plural 3 | mux_name_cap 1 | mux_invent_name 3
    | mux_and_sep 2 | wrap_hardbreak_no_trim 3 | wrap_advance_over_trimmed 3 | short_no_trim 1 | loop_no_trim 1
    | wrap_no_dots 2 | box_no_floor 2 | box_no_centre 2 | hit_always_yes 5 | red_is_literal 2(render)
    | box_flat 1(render) | draw_off 1(render) | main_no_feed 2(render) | main_always_vis 2(render)
    | main_no_pointer 1(render) | main_tab_wording 1(render). 22/22 killed, control survives.
  Next action: (6) README + ISSUES, (7) `make asan`, (8) commit and push.
  DONE (6, the docs): README's feature bullet, its `confirm_quit` settings row and its "Asking before a close
  throws work away" section now describe the TWO forms — a window asks with a box that names the process, a tab
  with the one-row notice — and every sentence quoted in README was printed by `.scratch/s124/wording`, not
  typed from the page (`A command is running here. Close it?  [Enter] yes  [Esc] no` is the tab row; the box's
  sentences are the `Warning !` family). The "a window too short to give up a row closes without asking" line
  became the general rule it always was: a question you cannot see never blocks a close. ISSUES gained one
  `Fixed` entry for the wrap bug, marked as caught before it shipped. The docs-pinning checks (README's settings
  and binding tables against the parsers) stayed green through the edit — 1413/0 after it, not just before it.
  Also corrected, because the page had started to lie: `confirm_message()`'s `CONFIRM_QUIT` wording is no longer
  what a window shows. The header comment ("the one-row question the window shows"), the Config comment ("the
  question is confirm_message()") and the test group's heading ("the exact row the window shows") all said
  otherwise; each now says the branches are superseded and kept only for the safety contract they carry. The
  `main_tab_wording` mutant is what proves main.c really does use the new wording.
  DONE (7, asan): `make asan` on this tree — 0 warning lines, all four asan binaries green (1413/0, MODEL OK,
  384/0, 524/0), no LeakSanitizer or UBSan report.
  DONE (8, committed and pushed): `aab3e15 "Confirm-quit: a window asks with a box that names the process"`
  (12 files, +1523/-118). `git fetch` first — nothing had landed on GitHub. `git push` took `12b789a..aab3e15`,
  which pushed s122's `357b4ee` as well: `git status -sb` now reads `main...origin/main` with nothing ahead, and
  the only untracked thing is `.scratch/`, which is never `git add`-ed. Verified at the COMMITTED tree, not at a
  dirty one: `make clean && make -j8` and the test build both exit 0 with **0 warning lines**, and the suites
  report **1413/0, MODEL OK, 384/0, 524/0**.
  WHAT THIS SESSION LEARNED, worth carrying: (a) a mutant harness is not a formality — it found a live bug in
  code that every check in the tree called correct, and it found it by SURVIVING; (b) a check written from the
  page rather than from a probe is a liability: two of the three wrong checks here asserted something the code
  never did, and one asserted something no correct UTF-8 string can satisfy; (c) a test input made of identical
  characters cannot reveal a gap; (d) a failed link leaves the old binary in `bin/`, so a suite can print a
  confident number for code that no longer builds.
  OWED NOW: the user's eyes on the box (recipe in "Current Status"). The row they confirmed is closed.
  Next session: steps 5-7 of the s102 plan — middle-click paste (opt-in, off by default, pastes the CLIPBOARD),
  the visual bell, plain-click URLs. All three are measured and gated in "Current Task"; their blocking
  decisions are already made.
- **2026-10-20, s125 — the user's verdict on the box: it works. Step 4 is CLOSED.**
  Their words: "I confirm the feature to be working. I had my eyes on it." That is the second confirmation of
  step 4 (the row was confirmed in s124), so both forms of the question — the tab's one-row notice and the
  window's centred box with its two clickable buttons — are now seen working in the user's own window, which is
  the half no harness can reach. Step 4 leaves the "owed to the user's eye" list entirely; the entries still on
  it (a themed window, a live OSC repaint, `cursor_color`, the step-2 cursor, step 3b's preview selection) are
  unchanged. Nothing was rebuilt or re-measured for this entry: it records a verdict, not a code change, and the
  tree it applies to is `aab3e15`/`dc36c46` (1413/0, MODEL OK, 384/0, 524/0, CLI 149/0/0, asan clean).
  Also corrected here, because the rules forbid leaving stale measurements in documentation: ISSUES.md's
  "Re-measured" paragraph still carried the s117 numbers (1217 checks, test_render 495, `CLI: 91/0/13`, and a
  sentence saying the windowed group had not run since s109). It now carries s124's, including the fact that the
  windowed group DID run — twice — on a machine with a live `WAYLAND_DISPLAY`.
  **Next: step 5, middle-click paste.** Its design is already settled by the user's answer to question 5 (it
  pastes the CLIPBOARD, so no `wl_primary` work at all; opt-in, `middle_click_paste = 0` by default). Task block
  with the measured starting point goes in below before any file is touched.
- **2026-10-20, s127 — step 6 stage (E): the harness, the documents, the sanitizer.**
  Objective: finish the visual bell the way steps 3, 3b, 4 and 5 were finished — every new check proven
  non-vacuous by mutation, the documents matching the code, both builds and every suite re-measured on the SAME
  tree, `make asan` green, then one commit.
  MEASURED on the tree as found (uncommitted, 11 files modified, nothing committed since `379fc85`):
  `make clean && make -j8` exit 0 with **0** warning lines; `make bin/test_vt bin/dbg bin/test_fm bin/test_render`
  exit 0 with **0** warning lines; `./bin/test_vt` **1483 checks, 0 failures**; `./bin/dbg` **MODEL OK
  (0 problems)**; `./bin/test_fm` **384 checks, 0 failures**; `./bin/test_render` **552 checks, 0 failures** —
  one MORE than the 551 the s126 entry recorded, so that entry was written before the last wiring scan landed;
  the measured number is the one that gets quoted from here on.
  Planned steps: (1) `.scratch/s126/mutate.py` — one mutant per decision step 6 added, built AND run in a staging
  tree, including the two the s126 entry predicted would survive (the pass itself draws nothing / draws at the
  wrong rect); (2) whatever a SURVIVOR shows to be a rule no test can reach, lift to where one can, and check it;
  (3) README's Features bullet + ISSUES (a bell limitation, and the re-measured counts paragraph); (4) `make
  asan` + the headless CLI group; (5) commit and push. Expected outcome: a step-6 commit whose every claim has a
  number next to it, and an explicit list of what only the user's eye can settle.
  DONE (1, the harness): 32 mutants + `control_noop`, run in a staging tree. First pass 24 killed / 7 survived;
  three of those survivors were real gaps and are fixed (the colour guard `-7` could not pin, the drawn-vs-hidden
  rule sitting in untestable `main.c`, and an unpinned dirty-flag gate), one was a harness artifact (a mutant
  comment naming the very string a scan looks for), one is an unreachable guard recorded as such. Re-run: **29
  killed, 3 survived + the control** — the two expected survivors are `render_draws_nothing` and
  `render_wrong_surface`, both because `render_draw()` is in no test binary, and that is written into ISSUES
  rather than papered over. Full table and verdicts in the step-6 section.
  DONE (2, the gap that mattered): the "only a session the layout DRAWS can be flashing" rule moved from
  `main.c`'s `bell_live()` to `mux_bell_live(mux, layout, now, dur)` in `src/mux.c`, with 15 checks in
  `tests/test_vt.c` (no pty, no clock, no window — a hand-built Layout). `main.c` keeps only the clock and the
  config line. The mutant that motivated it now dies in `test_vt` (2 failures), and the same rule mutated in
  `main.c` dies in `test_render` — the two halves are separate claims with separate watchers.
  DONE (3, the documents): README gained a "A bell you can see" Features bullet (the `bell` settings row was
  already there from s126); ISSUES gained a "The bell" limitations group — no audio so a BEL is only ever seen, a
  `0x07` that terminates a string is not a bell, an unseen session does not flash, the flash is one quad under the
  text and never in the margin, and what no harness here can see — and its "Re-measured" paragraph now carries
  this session's numbers instead of s126's.
  DONE (4, measured): `make clean && make -j8` exit 0, **0** warning lines; the test build exit 0, **0** warning
  lines; `./bin/test_vt` **1498/0**, `./bin/dbg` **MODEL OK (0 problems)**, `./bin/test_fm` **384/0**,
  `./bin/test_render` **555/0**; `make asan` exit 0, **0** warning lines, no LeakSanitizer/UBSan report, the same
  four counts; `KAI_TERM_TEST_NO_WINDOW=1 sh tests/cli_test.sh ./bin/kai-term` → **CLI: 99 checks, 0 failures,
  13 skipped**. The windowed group was NOT run (it maps ~15 real windows on the user's desktop — ask first).
  `grep` for `TODO`/`FIXME`/`XXX` and for `TEMPORARY|PROBE|EXPERIMENT|NEGATIVE` still returns only the known
  `mkdtemp` template and the three probe-writing lines in `tests/test_render.c`.
  DONE (5, committed and pushed): `1f79221 "Bell: a BEL you can see -- the pane that rang washes and fades"`
  (12 files). `git fetch` first — nothing had landed on GitHub. `git push` took `f3c812a..1f79221`, which also
  pushed s126's three journal commits; `git status -sb` now reads `main...origin/main` with nothing ahead and the
  only untracked thing is `.scratch/`, which was never `git add`-ed. The measurements above were taken on this
  tree, and the tree is what was pushed.
  WHAT THIS SESSION LEARNED, worth carrying: (a) a check on a small negative number can be worth nothing —
  `BELL_MIX_MAX * -7 / 1000` is 0 in C, so the "negative strength" check passed with the guard deleted; a check
  that is meant to pin a guard has to be A/B'd against the guard's absence, which is exactly what the harness is;
  (b) a mutant's own comment can satisfy the scan that is supposed to catch it (`src_call_passes_arg` uses
  `strstr` and does not skip comments) — the mutant text must not name the thing the scan looks for; (c) a rule
  that lives in `main.c` has only text pins watching it, and a text pin watches the SHAPE of the code, not its
  effect — the fix is to move the rule, not to write a second scan.
  OWED NOW: the user's eyes on the bell (recipe in "Current Status"), plus everything still on that list from
  steps 2, 3b and 5.
  Next session: **step 7, plain-click URLs** — the shape is written in the step-7 section and the user's answer to
  question 7 (a plain click, with gates) is already under "Decisions already made".

- **2026-10-20, s128 — the bell bug report: one half fixed, one half measured and found not to be a bug.**
  The user's report, verbatim: *"the bell feature doesn't work properly, Failling a search with less trigger no
  flash. while it should. But clicking the terminal when "less" is active makes it flash. while it shouldn't"*.
  MEASURED before writing any code (`.scratch/s128/bellprobe{,3,4}.c`, real `less` 704 in a real pty, 24x80,
  `TERM=xterm-256color`, startup queries answered — the s117 lesson): v1/v2 drained with "read until `EAGAIN`",
  which stops at the first empty read and could have missed a BEL written just after the message, so v4 drains with
  `poll` until 400 ms of silence and resets its counters immediately before each single write (v1 attributed one
  action's BEL to the next). Six `$LESS` values including the user's own `-R -M --shift 5`, all identical:
  a failed search (`/zzzzz`, `n`, and an invalid `/[`) is **0 `0x07` bytes and 0 parser rings** — `less` answers
  with the message `Pattern not found: zzzzz  (press RETURN)` and does not bell at all, so there is nothing for
  any terminal to flash; what it DOES bell for is an unknown command, `Up` at the top of the file and `Down` at the
  end (1 byte, 1 ring each), and `LESS=-q`/`-Q` silence those, which no terminal can undo. **Symptom (a) is not a
  Kai-Term bug, and README's claim that `less`/`man` bell for a failed search was wrong and is corrected.**
  Symptom (b) is: `less` leaves the model's cursor on its STATUS line (`cur=(39,23)` with `-M`), so
  `term_click_arrow_keys()` answers a click in its text area with Up presses (measured: click (10,5) → 18 Up + 29
  Left), at the top of the file that is a scroll before the beginning, and `less` answers with a BEL arriving
  **0-1 ms** after Kai-Term's own write. The pane was flashing for a complaint about Kai-Term's typing.
  THE FIX is a rule, not a pager heuristic: `bell_is_ours(now, synth_ms, window)` (src/vt.c, beside
  `bell_strength()`, same shape and same exclusive boundary), `Tab.synth_ms` stamped by `main.c` where the click's
  presses are sent and read by the BEL hand-off, `mux_open()` clearing it for a reused slot, `BELL_OURS_MS = 250`
  chosen from the measured 0-1 ms rather than guessed, the parser still raising `bell_rang`, and the **wheel** path
  deliberately left unstamped (there the user asked to scroll). Full rule in the step-6 section.
  VERIFIED: 18 checks in the `bell_is_ours` group + 6 source scans; `.scratch/s128/mutate.py` **18 mutants, 16
  KILLED, 2 SURVIVED** (the control, and `window_ms <= 0` — unreachable in effect once the other two guards hold,
  the same kind of survivor as s126's `strength_no_dur_guard`); the feature-absent tree (`ours_never_mutes`) fails
  4. Re-measured from `make clean`: both builds exit 0 with **0** warning lines; `test_vt` **1516/0**,
  `dbg` **MODEL OK (0 problems)**, `test_fm` **384/0**, `test_render` **561/0**; `make asan` exit 0, **0** warning
  lines, same four counts, no LeakSanitizer/UBSan report; headless CLI **99/0/13**. The windowed group was not run.
  WHAT THIS SESSION LEARNED, worth carrying: (a) a probe that stops reading at the first `EAGAIN` can silently
  miss the very byte it is counting — a claim about "the program never sent X" needs a drain bounded by SILENCE,
  not by emptiness; (b) two of the three probes were wrong before v4 was right (v1 hand-wrote `ESC[B` for a click
  and measured a bell that was the probe's own bug, because `less` sets DECCKM; v2 reset its counters between the
  write and the drain, attributing one action's BEL to the next) — a probe that produces a tidy answer to the
  user's question is worth re-running against a mechanism you do not yet believe; (c) a user report can be two
  reports: fix the half that reproduces and MEASURE the half that does not, then correct the documentation that
  asserted the thing the measurement disproves.
  OWED NOW: the user's eyes on the click no longer flashing (recipe in "Current Status"), plus the rest of that
  list. NOT fixed deliberately: a click in `less` still sends the presses, so it still scrolls the file and can
  move `less`'s horizontal column — the documented `click_moves_cursor` caveat, and not what was reported.
  COMMITTED AND PUSHED: `1fc5867 "Bell: a click's own BEL no longer flashes the pane"` (10 files — src, header,
  tests, the shipped template, README, ISSUES) and `0d735b3 "Journal: s128 -- the bell report closed, both halves
  with numbers"`. `git fetch` first: nothing had landed on GitHub since `632065b`. `git push` took
  `632065b..0d735b3`; `git status -sb` now reads `main...origin/main` with nothing ahead, and the only untracked
  thing is `.scratch/`, which was never `git add`-ed. Every measurement above was taken on the tree that was
  pushed, and the push was verified from git after the fact, not assumed from the command's output.
  Next session: **step 7, plain-click URLs**.

- **2026-10-21, s129-s130 — step 7, a plain click opens a URL (IN PROGRESS, nothing committed yet).** OBJECTIVE:
  the last step of the s102 plan — a single left click on an `http(s)://` run in a terminal pane hands it to
  `xdg-open`, gated so it never takes a click that already belongs to something else, with `click_opens_url` to
  turn it off. The measured starting point and the plan are the s129 task block above (`.scratch/s129/urlprobe.c`,
  re-run by s130 against the wired tree: every corpus line it prints is what the new checks copy).
  WHAT s129 LEFT (see "What is already in the working tree" above): `src/url.c` and the `main.c`/`config.c`/
  header/template/Makefile wiring, unjournaled and unverified beyond a clean build; the suite RED at 1516/2 because
  the doc gate noticed the new key before any check for the feature existed.
  WHAT s130 PLANS, in order: (1) the `url` group in `tests/test_vt.c` — the run rule and the spans against the
  probe corpus, the refusals (non-ASCII cut, no scheme, empty authority, over-`URL_MAX`), `url_is_openable`, the
  `url_click_opens` gate table, the `url_open_due` boundary against `term_click_count`'s window, and an
  end-to-end `url_spawn` through a script that prints its argv and its open descriptors; (2) the settings count
  39 -> 40, the template field check, and `src/url.c` added to the hex-escape scan list; (3) README's settings row
  and the click prose; (4) the `main.c` wiring scans in `tests/test_render.c`; (5) ISSUES' limits (a run that
  scrolled above the viewport, the non-ASCII refusal, the 2048 ceiling, the opener having to exist);
  (6) `.scratch/s129/mutate.py` for non-vacuity plus the feature-absent A/B; (7) both builds, every suite,
  `make asan`, the headless CLI group, one commit, push. EXPECTED: `test_vt` up by the new group, everything else
  unchanged, and the honest note that a browser actually opening is owed to the user's eye.
  STATUS RIGHT NOW: step (1) in progress; nothing verified beyond the two build commands and the 1516/2 above.
  DONE (1) — the `url` group in `tests/test_vt.c` (inserted after the `ptr` precedence group, so the click
  features sit together). Before writing a single number I ran a NEW probe, `.scratch/s129/realprobe.c`, against
  the shipped `src/url.c` — because `.scratch/s129/urlprobe.c` measured a DRAFT rule and the two differ: the draft
  answered `https://ex` for `https://ex<U+00E9>.com`, `src/url.c` refuses it. Two corpus numbers came from the draft
  and were wrong for the shipped rule and were corrected from realprobe's output (the non-ASCII refusal, and
  `a-b_c~d%20e` — urlprobe had fed a doubled `%%` and printed 32; the real run is 31). The group is 110 checks:
  the run rule and its spans (plain, query+fragment, click anywhere in the run, out-of-range, NULL parameters, a
  refused click leaving a ZEROED span, a too-small buffer writing nothing); every trailing mark cut and kept (the
  slash kept is a decision with a check on it); scheme case-insensitivity, `http://` alone refused, ftp/mailto
  refused, a scheme mid-word; the non-ASCII/wide refusal with the wide-pair setup pinned (`cellch(0,8) == 0x4F8B
  && cellattr(0,9) & ATTR_WIDE`) and a wide glyph on the LEFT accepted; the soft-wrapped run found from either row
  (61 bytes, break removed) against a program-broken one that is NOT joined; an address ending exactly at the
  margin; a tab's `ch == 0` gap; the alt screen (the scan FINDS it, `term_wants_key_mouse()` is what declines —
  pinning that division is the point); the `URL_MAX` ceiling measured on all three sides (2047 / 2048 / 2049) plus
  the 3000-character run; the delimiter corpus (`<url>`, `[url]`, trailing `?` vs a `?` in a query, two runs on one
  line, `#frag`, a balanced pair at the end, brackets and braces INSIDE a path, padded spaces); the scrolled-away
  limit; `url_is_openable` (dash-option, empty, other schemes, space/control/DEL/non-ASCII, the length boundary);
  the 9-case `url_click_opens` gate table; the `url_open_due` boundary WITH the check that the two rules agree at
  exactly `CLICK_DCLICK_MS` (`term_click_count` still says 2 there and `url_open_due` still says not due); the
  `click_opens_url` config group (default ON first, every spelling of both values fed after the opposite one, the
  cfg_bool contract); and an end-to-end `url_spawn` through a stand-in opener that reports its own argv, its
  session id, its `SigBlk` mask and its `/proc/self/fd` — the checks are argc=1, the address verbatim, `pts=0`
  (a pty master the parent opened is NOT inherited — `posix_openpt` without `O_CLOEXEC`, the real condition),
  SIGCHLD clear in the child's mask, a different session id, and a `;touch` in the address that never runs.
  Also in the same edit: the settings count 39 -> 40, a template field check that the shipped line really is the
  built-in **1** (a template shipping 0 would ship the feature off), and `src/url.c` added to the hex-escape scan
  list. VERIFIED: `make bin/test_vt` 0 warning lines (after four `-Wshadow` fixes — `small` and three inner `n`
  against `main()`'s `n`), `./bin/test_vt` **1626 checks, 1 failure**, and that one failure is the doc gate:
  `FAIL: README's settings table does not document click_opens_url`. No SKIP lines; the spawn group leaves no
  file behind in /tmp. NEXT: README's row and prose, then the `main.c` wiring scans in `tests/test_render.c`.
  DONE (2) — README: a Features bullet, a new "### Clicking a web address" subsection (what the click never takes
  away; what counts as an address, including the window-broke/program-broke split, the kept trailing slash, the
  non-ASCII refusal and the http/https-only rule; and the two honest limits — whether a window appears is the
  desktop's business, and a run scrolled above the top is not found), and the `click_opens_url` row in the settings
  table. That row is what the doc gate was failing on: `./bin/test_vt` is now **1626 checks, 0 failures**.
  DONE (3) — 14 source scans in `tests/test_render.c`'s `group_source_wiring()`, every count measured with the new
  `.scratch/s129/scancount.py` (a replica of the C scanner) before it was written: main.c asks `url_click_opens`
  once and feeds it `conf.click_opens_url`; the scan is asked twice (click + deadline) and the deadline compares
  `strcmp(holds, clicked)`; `url_tick` defined and called; the wait uses `URL_OPEN_WAIT_MS` not a literal; the
  pending stamp is cleared in both places that end a wait; exactly one `url_spawn` site, taking
  `URL_OPENER_PROGRAM` and the re-validated text; the opener's name appears 3 times in main.c and ZERO times in
  src/url.c; `url_is_openable` 0 in main.c / 2 in url.c; `term_line_span` used once by url.c; `closefrom`/`setsid`/
  `sigprocmask` each present; `system`/`popen`/`execl`/`execlp`/`execve` all 0; one `char *argv[3]`.
  VERIFIED: `make bin/test_render` 0 warning lines, `./bin/test_render` **575 checks, 0 failures** (was 561).
  NEXT: ISSUES' limits, then the mutant harness, then the full re-measure (both builds, every suite, asan, CLI).
  DONE (4) — ISSUES: a new "**A click on a web address**" block under Known limitations (the scan-never-declines /
  gate-declines division, the scrolled-above-viewport limit, the non-ASCII refusal with the draft named as the
  thing it replaces, the 2048 ceiling and its log line, the deadline re-validation and its log line, the opener
  having to exist and why it is a header constant and not a key, and the honest "a browser opening is owed to the
  user's eye").
  HARNESS (5) — `.scratch/s129/mutate.py` (38 mutants: 11 run rule, 6 text/argument, 6 gate, 5 wait/header, 5
  spawn, 2 key, 5 main.c wiring, plus the control), building and running BOTH `test_vt` and `test_render` in a
  staging tree. Two things it found, both real:
  (a) **the spawn check was vacuous.** `spawn_no_closefrom` survived `test_vt` (only the source scan killed it).
      Cause measured with `.scratch/s129/fdprobe.c`: a pty MASTER fd in `/proc/self/fd` links to **/dev/ptmx**, not
      `/dev/pts/N`, so `grep -c pts` reported 0 for a child that had inherited every master in the process — the
      exact failure the check exists to catch. Fixed: the stand-in opener now counts `-e pts -e ptmx` and the check
      reads `ptys=0`; the reason is written into the test so nobody "simplifies" it back. Re-measured: the real
      tree still prints `ptys=0`, the mutant now prints `3 -> /dev/ptmx` and FAILS.
  (b) **a corpus gap.** `url_join_every_row` (ignore the wrap marks, treat every viewport row as one line) survived
      the first corpus, because in every program-broken case the blank cells between the two rows ended the run
      anyway. The case that distinguishes them is a line the program ended AT the margin: the row is full, no wrap
      mark is set, and only the mark tells a window's break from the program's. Measured with realprobe (section
      6b: `wrap[0] == 0`, the run stops at column 19) and added as two checks. `test_vt` is now **1628 checks,
      0 failures**, 0 warning lines.
  (c) **the signal-mask check was vacuous too.** `spawn_no_unblock` survived `test_vt` as well. Measured with
      `.scratch/s129/maskprobe.c`: `/bin/sh` normalises its own signal mask at startup, so a shell stand-in
      reports `SigBlk: 0` whatever the parent blocked. The mask half of the spawn group now runs against
      `/usr/bin/awk` (whose whole program IS the one argument, which also proves the argument arrives as data),
      and it is not vacuous: the real tree reports `0000000000000000`, the mutant `0000000000010000`. Skipped
      with a printed SKIP if `/usr/bin/awk` is not executable — verified present here, not assumed.
  (d) **the header's constants were unpinned.** `hdr_url_max_small` (URL_MAX 2048 -> 64) survived because every
      length check is written in terms of `URL_MAX` and follows it wherever a mutant moves it. Two checks added
      (`URL_MAX == 2048`, `URL_OPENER_PROGRAM == "xdg-open"`), the way `CLICK_DCLICK_MS == 500` is pinned.
  FULL HARNESS RUN (`.scratch/s129/mutate_run.txt`, 2026-10-21): **39 mutants — 38 KILLED, 1 control SURVIVED
  (as it must) + 2 survivors that are equivalent by construction and now carry a comment in `src/url.c`**
  (`url_openable_dash`: the scheme test makes a leading dash unreachable; `wait_no_clock_guard`: a backwards
  clock makes the difference negative, which the return expression already refuses). Every mutant is built AND
  run in a staging tree against BOTH `test_vt` and `test_render`; the harness's docstring records the four
  vacuous/gap findings above so the next agent does not re-introduce them.

- **2026-10-22, s131 — step 7 close-out: the full re-measure, then the commit (CLOSED).** OBJECTIVE: finish
  plan step (7) of the s130 block — nothing in the tree is committed, and "Current Status" above claims a green
  full re-measure that the Progress Journal never records running. So every number in this file is treated as
  UNVERIFIED until re-measured on this tree, per the standing rule (a failed build leaves the old binary in
  `bin/`). PLANNED, in order: (a) `make clean && make -j8` and the test-binary build, counting warning lines in
  both; (b) run `test_vt`, `dbg`, `test_fm`, `test_render` and quote the counts only after checking each build
  exited 0; (c) `make asan` (four binaries, watching for LeakSanitizer/UBSan text, not just exit 0); (d) the
  headless CLI group with `KAI_TERM_TEST_NO_WINDOW=1` — no windows, the user may be at the machine; (e) re-check
  the doc-pinning claims by reading the checks, not the prose (settings count, template line, README row);
  (f) `git diff` review of every modified file for anything left over from the harness work; (g) one feature
  commit + one journal commit, `git fetch` then push, then verify the push from git. EXPECTED: the counts the
  harness control printed (`test_vt` 1632/0, `test_render` 575/0) hold; if any number differs, the status block
  gets corrected, not the number. NOT planned: any new feature work, and the windowed CLI group (ask first).
  RESULTS SO FAR, all measured on this tree at 2026-10-22 09:14:
  (a) `make clean && make -j8` -> exit **0**, `grep -ciE 'warning|error'` -> **0**; and
      `make bin/test_vt bin/dbg bin/test_fm bin/test_render` -> exit **0**, **0** warning lines. All five
      binaries in `bin/` carry the same fresh mtime, so no stale-binary trap.
  (b) `./bin/test_vt` **1632 checks, 0 failures**; `./bin/dbg` **MODEL OK (0 problems)**; `./bin/test_fm`
      **384 checks, 0 failures**; `./bin/test_render` **575 checks, 0 failures**. No FAIL lines, no SKIP lines.
      These match the harness control run exactly, so the numbers "Current Status" claimed were real — they are
      now re-verified rather than trusted.
  NEXT: (c) `make asan`, (d) the headless CLI group.
  (c) `make asan` -> build exit **0**, **0** warning lines; all four asan binaries exit **0** with the SAME four
      counts (1632 / MODEL OK / 384 / 575) and no `LeakSanitizer`, `runtime error` or `SUMMARY` line anywhere in
      their output.
  (d) `KAI_TERM_TEST_NO_WINDOW=1 sh tests/cli_test.sh ./bin/kai-term` -> exit **0**, **CLI: 99 checks, 0
      failures, 13 skipped** (the 13 are the windowed group; no window was mapped, the user may be at the machine).
  (e) The doc-pinning claims re-read at the checks, not the prose: `tests/test_vt.c:3998` asserts `ns == 40 &&
      nb == 24`, `:3918` asserts the shipped template line equals the built-in **1**, and the README row is what
      `readme_table_names_of()` diffs — all green, which is only meaningful because the same check went RED in s130
      when the key existed without the row. The grep claims re-run: `TODO|FIXME|XXX` in src/include/tests returns
      only `kai-term-int-XXXXXX` and two test fill strings; the probe-token grep returns only the three
      string-concatenated lines in `tests/test_render.c` that WRITE a probe file.
  (f) `git diff` read line by line (src/url.c in full, main.c, config.c, the header, the template, the Makefile,
      both test files, README, ISSUES). Nothing left over from the harness work: no probe code, no debug print
      beyond the four `klog` lines that are the feature's own diagnostics. Two things checked because they are the
      kind of thing a diff hides: the pending open is keyed to `mux_active(&mux)` and that is correct because a
      press in a non-visible pane calls `tab_switch()` BEFORE the release computes the cell, and the forked opener
      is reaped by the existing signalfd loop (`waitpid(-1)` then `tab_for_child()`, which answers -1 for a
      non-shell pid), so it cannot become a zombie.
  (g) **The feature-absent A/B, which s130's harness did not run** (`.scratch/s131/ab_nourl/`: the whole tree with
      `src/url.c` replaced by a stub answering "no URL" everywhere and a spawn that refuses; the header keeps the
      vocabulary so the checks still compile). `test_vt`: **1628 checks, 52 failures** — every one inside the `url`
      group. `test_render`: **575 checks, 4 failures**, all four the source scans reading the stub's `src/url.c`
      (`closefrom`/`setsid`/`sigprocmask` absent, `char *argv[3]` absent, `url_is_openable` absent, `term_line_span`
      absent). Two numbers to read honestly, per the standing rule about A/B counts: the stub tree runs **4 fewer**
      checks than the real tree (1628 vs 1632) because the four spawn assertions live inside `if (appeared)` and a
      spawn that fails never reports — and **60 of the group's 112 checks that DID run still passed**, because "a
      non-ASCII run is refused", "`ftp://` is refused" and "`http://` alone is refused" are all satisfied by a tree
      that refuses everything. Those are pinned by the MUTANTS (s129's 38-mutant run) and by the positive checks
      beside them, not by this A/B. What this A/B does prove is that the positive half — the spans, the marks, the
      wrap join, the gates, the wait, the spawn — has no coverage without `src/url.c`.
  NEXT: the stale counts in this file's own "Project Files" section (1498/1320/555/506 -> the measured 1632/384/
  575, and `src/url.c` was missing from the tree listing entirely), then the commit.
  DONE — the stale counts were corrected (the tree listing had no `src/url.c` at all, and the four check counts were
  two features out of date), `src/url.c` got its own entry in "What each file OWNS", and the `url` group and the 14
  URL scans were written into the two test files' descriptions. ISSUES' re-measure block was two features stale
  (1516/561) and now carries the measured 1632/575 plus the note that step 7 added a settings key and NO command-line
  flag. "Current Task" now reads NONE IN PROGRESS, "Next Steps" item 1 says the s102 plan is finished, and the
  owed-to-the-user's-eye list gained step 7's four verdicts with the `-d` recipe beside them.
  COMMITTED AND PUSHED: `4610911 "URLs: a plain left click opens the address under it"` (10 files — the new
  `src/url.c`, `main.c`, `config.c`, the header, the shipped template, the Makefile, both test files, README,
  ISSUES). `git fetch` first: nothing had landed on GitHub since `bfaa749`. `git push` took `bfaa749..4610911`;
  verified from git afterwards, not from the push output — `git log --oneline origin/main -1` is `4610911` and
  `git status -sb` reads `main...origin/main` with nothing ahead. Then the commit itself was tested: a fresh clone of
  it (`s131/verify_commit`) builds both targets at 0 warning lines and prints the same four counts, which is the only
  way to know nothing needed was left untracked (`.scratch/` is not in `.gitignore`, so `git add -A` would have been
  a mistake — the ten files were staged by name). This journal close-out is the commit after it.
  NEXT SESSION: nothing is in progress and the s102 plan is closed. Ask the user what they want, or take a
  housekeeping item (Next Steps item 5). The one thing genuinely outstanding is the user's eye on step 7's click —
  and on steps 5 and 6, which are still owed the same.
  ONE DOC BUG FOUND WHILE VERIFYING THE LOG LINES (fixed after the feature commit): README claimed `-d` logs "when
  the click was refused, why". It does not, and `grep -n 'klog' src/main.c | grep url:` shows there are exactly six
  `url:` lines, none of them for a refused click. That is deliberate — a click on a cell with no address is what
  every ordinary click in a terminal is, so logging the refusal would fill the log — but the sentence promised a
  diagnostic that does not exist. README now says what IS logged, and answers the real question the way the code
  actually answers it: whether the `url: the click at (col,row) holds …` line appeared for that click. AGENT.md's
  "Debugging this project" gained the six lines with what each one's absence means. Re-measured after the README
  edit (README is read by the doc-pinning checks): test build 0 warning lines, `test_vt` **1632/0**, `dbg` MODEL OK,
  `test_fm` **384/0**, `test_render` **575/0**.

- **2026-10-22, s132 — the user reports step 7 does not work: "it's not clickable" (IN PROGRESS).**
  FIRST, A CORRECTION TO HOW I OPENED THIS: my first move was to look for an older installed binary and at the
  user's `desktop-terminal` setup. That was an assumption, not a measurement, and it is now a standing rule at the
  top of this file: **the user tests `bin/kai-term` from this tree; nothing outside the tree is fair game.**
  MEASURED SO FAR, all headless, no window mapped:
  (a) **The model half is correct on REAL shell output, not just on the synthetic text the suite feeds.**
      `.scratch/s132/clickprobe.c` runs the user's own shell (fish) through the real `src/pty.c` and the real
      `term_parse()`, with the parser's queued replies written back to the pty the way `main.c` does (without that
      the shell paints NOTHING — the s97 trap, and the probe's first run hit it), then asks the three questions at
      the cell the address is really printed at: `printf 'see https://example.com/a?b=1 now'` lands at cell (26,2),
      `term_url_span` -> span (26,2)..(50,2), `term_url_text` -> 25 bytes `https://example.com/a?b=1`,
      `url_is_openable` -> 1, `url_click_opens` with a plain click's flags -> 1, and a click at the middle, at the
      first cell and at the LAST cell of the run all find the same run. So the scan, the text and the gate table are
      not the failure.
  (b) **The spawn machinery is fine in THIS session.** `.scratch/s132/spawnprobe.c` blocks SIGCHLD the way main.c
      does, calls the shipped `url_spawn(URL_OPENER_PROGRAM, …)` and waits for the child. With a scheme `xdg-open`
      refuses (so nothing can appear on the user's desktop) the child exits 0, i.e. `execvp` found and ran it — not
      127. Environment: `XDG_CURRENT_DESKTOP=niri`, `WAYLAND_DISPLAY=wayland-1`, `DISPLAY=:0`, PATH contains
      /usr/bin, and `xdg-settings get default-web-browser` answers `firefox-bin.desktop`.
  (c) `bin/kai-term` (the binary the user runs) contains the feature: `strings` finds 8 of the `xdg-open` /
      `click_opens_url` / `url:` strings in it.
  SO THE DECLINE IS IN A GATE — and here is the real problem, which is also a product gap: **a refused click logs
  NOTHING** (s131 documented exactly that). The user cannot tell me what happened and neither can I. PLAN: make the
  refusal name itself. `url_refusal()` in src/url.c returns WHY (off / not a release / a gesture / the pointer moved
  / Shift / the program owns the click / no address there) with `url_click_opens()` reduced to "refusal == NONE", and
  `url_refusal_name()` keeps the wording in the same file so it is checkable; `main.c` logs it at debug. Then the
  report becomes one line of log instead of a guess. NOT changing any gate until a measurement says which one fired.
  CLOSED 2026-10-22 by the user's own report: **"The link clicking now works and open as expected."** That is step 7's
  verdict from the user's eye, so it leaves the owed-to-their-eye list (the click opens, and a double click on the same
  address selecting the word was part of what they saw). The refusal-naming half is in the working tree, green
  (`test_vt` 1654/0, `test_render` 578/0 — measured by s133 below, not by s132), and is committed with s133's work.
  What s132 actually fixed is the DIAGNOSABILITY, not a gate: no gate was changed, and the report resolved itself —
  which is worth recording honestly, because the tree that "did not work" and the tree that "works" may have differed
  only in the binary the user had rebuilt.

- **2026-10-22, s133 — the user asks for hover feedback on a link: "a visual feedback on the link to show it's
  clickable on hover … the links get underlined when a cursor is on it" (with a screenshot of another terminal doing
  exactly that).**
  **Objective.** The `http(s)://` run under the pointer is underlined while the pointer is on it, so the user can see
  a click will open something before they make it.
  **Measured starting point (headless, no window mapped).**
  * `grep -rn "ATTR_UNDERLINE\|ATTR_STRIKE" src/ include/` → the parser SETS both (`vt.c:2582`, `:2585`) and the
    renderer reads NEITHER: there is no underline drawing anywhere in `src/render.c`. So this is not a matter of
    setting an attribute on cells — a decoration quad has to be added, and the only existing one of that shape is the
    bar/underline CURSOR quad of pass 2b (`render_cursor()`'s `CUR_UNDERLINE` branch: `x=0, y=cell_h-w, w=cell_w,
    h=w`, w clamped to `cell_h/3` with a floor of 1).
  * There is no hover handler in this tree and no pointer-position state in `Render` (AGENT.md, s107: "there is no
    hover handler here and there should not be one" — that was about FOCUS, and it still holds: nothing about the
    pointer's position may take the keyboard). Adding a purely VISUAL hover is a different thing, and it must not
    touch focus, selection or the click path.
  * `Cell` must not be touched (step 7's rule: it is allocated per column per row and reflow moves it), so the hover
    run is a value the frame computes, not a flag painted into the grid.
  **Plan.** (1) `src/url.c`: `url_hover_shows(const UrlHover *)` — the hover gates, written as the SAME gates
  `url_refusal()` answers with (`released/single/no_drag` filled in as a hover implies), so the underline can never
  promise a click that the gate would refuse. Gates: `click_opens_url` on, the pointer over a terminal pane (not the
  bar, the band, a margin or the confirm box), no live selection drag, no `Shift`, the program does not own its
  clicks, and the run is one `term_url_text()` will hand over. No new config key: the underline promises exactly what
  `click_opens_url` already decides, so a second key could disagree with it. (2) `src/render.c`: pure
  `render_hover(cell_w, cell_h, HoverDraw *)` beside `render_cursor()` (same clamp shape, its own thickness rule),
  and a new pass 2c emitting one quad per cell of the hovered run — after the selection bands and after the cursor
  quad, before the glyphs, so it is over the paper and the highlight and never hides a character; colour is the
  cell's own ink, so a coloured address underlines in its own colour and a theme needs nothing. (3) `Render` gains
  `hover_tab`/`hover_span` (window state copied per frame, exactly like `cursor_on`/`bell_ms`), and `main.c`
  recomputes the run in `redraw()` — every frame, from the pointer and the layout — so a scroll, a repaint, a resize
  or a tab switch can never leave an underline on a run that is no longer there. `W.ptr_in` (new) records that the
  pointer is on the surface at all; `ptr_motion` sets `W.dirty` when the CELL under the pointer changes, which is the
  only new redraw source. (4) Checks: a `url_hover` group in `tests/test_vt.c` (every gate flipped, and the
  agreement invariant — same flags, hover shows ⟺ a plain click opens) and a `hover` group in `tests/test_render.c`
  (the rect invariants: never 0 px, never taller than a third of the cell, full cell width so neighbours join) plus
  the source scans for the `main.c`/`render.c` wiring. (5) Mutants in `.scratch/s133/` to prove those checks are not
  vacuous. (6) README (the click section + the `click_opens_url` row), this file, ISSUES; both builds, every suite,
  `make asan`, the headless CLI group, commit, push.
  **Expected outcome.** `test_vt` up by the hover gate table, `test_render` up by the rect invariants and the wiring
  scans, everything else unchanged, both builds at 0 warning lines, asan clean. What is owed to the user's eye: that
  the underline READS as an underline at their font size (thickness, position) — every check here pins the decision
  and the rect, none pins the pixel.
  **BASELINE, measured before the first edit of this session:** `make clean && make -j8` exit 0 / 0 warning lines, and
  `make bin/test_vt bin/dbg bin/test_fm bin/test_render` exit 0 / 0 warning lines; `test_vt` **1654/0**, `dbg` **MODEL
  OK (0 problems)**, `test_fm` **384/0**, `test_render` **578/0**. That is the s132 tree (uncommitted) and it is green.
  STEP 1 DONE — `include/kai-term/kai-term.h`: `UrlHover` + `url_hover_shows()` declared in the URL section (with the
  "no `url_hover =` key" decision written where the next reader will see it), `HoverDraw` + `render_hover()` declared
  beside `render_cursor()`, and `Render.hover_tab` / `Render.hover_span` added next to `bell_ms` with the reason they
  are window state and recomputed per frame. Not yet built.
  STEP 2 DONE — `src/url.c`: `url_hover_shows()` implemented as a call to `url_click_opens()` with the click-only
  gates filled in as a hover implies (`released`/`single` = 1, `no_drag = !dragging`), plus the `in_pane` gate that
  has no click equivalent. No second predicate was written, so the underline cannot drift from the click. Not yet
  built.
  STEP 3 DONE — `src/render.c`: `render_hover(cell_w, cell_h, HoverDraw *)` next to `render_cursor()` (full cell
  width, on the cell's bottom edge, thickness `cell_h/12` with a floor of 1 px and the same `cell_h/3` cap the
  underline cursor uses); a new **pass 2c** between the cursor quad and the glyphs, drawing one quad per cell of the
  run in the ONE pane `Render.hover_tab` names, in the cell's own ink; `render_init()` sets `hover_tab = -1` (the
  memset's 0 would read as "tab 0"); and the quad budget gained `+ URL_MAX` with the reason (a run longer than that
  is refused before it can reach the span, so it is the real ceiling). `url_row_range()` in src/url.c was made
  public as **`url_span_row()`** and the pass asks IT — a second reading of "which cells of a run sit on this row"
  would be a second answer about a soft-wrapped run, the same mistake step 7 refused to make twice with the wrap
  marks. It also gained a `r < r0 || r > r1` refusal so a row outside the run answers "none".
  STEP 4 DONE — `src/main.c`: `hover_cell()` (the pointer→pane→cell mapping, per pane, exact for the same reason
  `sel_cell()` is), `hover_motion()` (marks the window dirty only when the CELL changes — a compositor sends one
  event per pixel) called from `ptr_enter`, `ptr_motion` and `ptr_leave`, and `hover_update()` called from
  `redraw()` every frame: it clears `rend.hover_tab` to -1, maps the pointer, skips the confirm-quit box, asks
  `term_url_span()` + `term_url_text()` (the same two questions the click asks) and then `url_hover_shows()`. New
  `W.ptr_in` because `wl_pointer.leave` does not clear `ptr_x/ptr_y`. No new config key.
  STEP 5 PARTLY DONE — checks. `tests/test_vt.c`: the `url/hover` gate table (7 cases), the 32-combination
  agreement loop with the two non-vacuity counts (`shown == 1`, `off_pane == 0`), and the `url/row` group for
  `url_span_row()` including a real 40-column wrapped grid where the underlined cells (35 + 26) must equal the 61
  bytes that get opened. `tests/test_render.c`: `group_hover_shape()` (the rect invariants over cells 1..400 px,
  the measured thickness ladder, the empty-rect answers for a 0/negative cell) and 14 new wiring scans, three of
  which had to move because the tree legitimately changed (`g_layout.dialog.on` 2→3, `click_opens_url` 1→3,
  `term_url_span`/`term_url_text` 2→3 each); a new scanner `src_code_before()` pins the DRAW ORDER (bands →
  cursor quad → underline → glyphs), which no headless test can otherwise reach. `build/url.o` joined
  `bin/test_render` (and `src/url.c` the asan rule) because render.o now calls `url_span_row()`.
  MEASURED SO FAR: `make -j8` exit 0 / 0 warning lines; `make bin/test_vt bin/dbg bin/test_fm bin/test_render`
  exit 0 / 0 warning lines; `./bin/test_vt` **1673 checks, 0 failures** (was 1654, +19); `./bin/test_render`
  **604 checks, 0 failures** (was 578, +26). Every scan count was measured with `.scratch/s129/scancount.py`, not
  guessed. NOT yet run: `dbg`, `test_fm`, `make asan`, the CLI group, and the mutants.

- **2026-10-22, s134 — the user's eye on the hover underline: "it's over the text instead of under it. It would be
  better under".** The task block above holds the report, the measurement and the plan; the baseline was re-measured
  before the first edit: `make clean && make -j8` exit 0 / **0 warning lines**, `make bin/test_vt bin/dbg bin/test_fm
  bin/test_render` exit 0 / **0 warning lines**, `./bin/test_vt` **1673/0**, `./bin/dbg` **MODEL OK (0 problems)**,
  `./bin/test_fm` **384/0**, `./bin/test_render` **604/0** — the s133 tree is green, so the report is a wrong pixel,
  not a broken build.
  **The root cause, measured before touching anything (probes in `.scratch/s134/`):** the quad's y grows UP from the
  cell's bottom edge (the MVP is an unflipped pixel ortho; `pad_gl` is the GL y of the bottom row), and
  `render_hover()` copied `render_cursor()`'s `CUR_UNDERLINE` formula `y = cell_h - h`, which is the TOP of the cell
  in that space. The header comment that was copied from says "origin at the cell's top-left" — wrong — while the
  `HoverDraw` comment right below it says "y is measured UP from the cell's bottom edge" — right. Two comments, one
  file, one of them false, and the code followed the false one. The DECSCUSR underline cursor has had the same defect
  since s112, and `tests/test_render.c` had pinned it twice (`d.y == ch - 2`, `d.y + d.h == cell_h`) because a check
  written from the implementation's own assumption restates the assumption instead of testing it.
  **STEP 0 DONE — the font measured, so the fix has numbers instead of an opinion** (see the task block: cell_h /
  ascent / descent at 7 pixel sizes, the depth of every glyph of the URL in the screenshot, and the font's own OS/2
  underline position as the cross-check). Not yet edited: render.c, the header, the checks.
  STEP 1 DONE — `src/render.c`: the new static `underline_y(cell_h, ascent_px, descent_px, h)` with the coordinate
  space written out where the next reader will hit it, `render_cursor()`'s `CUR_UNDERLINE` branch and `render_hover()`
  both asking it, and `render_draw()` gaining `float descent = (float)f->descent * f->scale;` next to the existing
  `ascent`, passed to all three cursor passes and to the hover pass. `render_cursor()` and `render_hover()` both grew
  `ascent_px`/`descent_px` parameters.
  STEP 2 DONE — `include/kai-term/kai-term.h`: the `CursorDraw` comment's "origin at the cell's top-left" replaced
  with the truth (x from the left edge, y from the BOTTOM edge measured up, and why a bar cannot show the difference
  while an underline can), both declarations updated, and the `render_hover()` comment saying the position is asked of
  the same helper the cursor asks.
  STEP 3 DONE — `tests/test_render.c`: the two checks that had pinned the bug replaced. `group_cursor_shape()` now
  passes metrics everywhere (a 9x18 cell with ascent 13 / descent 5) and asserts the underline is at `y == 0` with its
  top edge never above the baseline; `group_hover_shape()` was rewritten around the measured font table (13
  (cell_w, cell_h, ascent, descent) rows: the installed face at px 10..32 plus three faces this machine has not got —
  leading under the baseline, no descender band, a descender deeper than the cell) with five invariants (never 0 px,
  never over the cell/3 cap, always inside the cell, never above the baseline when there is room, and at the bottom of
  the DESCENDER band when the cell has leading), the measured thickness-and-place table, the 3-px-below-the-baseline
  cross-check at font_size 14, and a new check that the cursor's underline and the hover underline come out the SAME
  rect at the same thickness. The wiring scans gained three: `underline_y` counted 3 (definition + both underlines),
  `cell_h - h` / `cell_h - w` pinned ABSENT in src/render.c, and the descent metric counted where it must reach. All
  counts measured with `.scratch/s134/scancount.py` (the s129 scanner replica), not guessed.
  MEASURED after the edits: `make -j8` exit 0 / **0 warning lines**; `make bin/test_vt bin/dbg bin/test_fm
  bin/test_render` exit 0 / **0 warning lines**; `./bin/test_render` **616 checks, 0 failures** (was 604, +12);
  `./bin/test_vt` **1673/0** (unchanged — the position is a render.c decision, as the plan expected); `./bin/dbg`
  **MODEL OK (0 problems)**; `./bin/test_fm` **384/0**. One check went red on the first run and it was the check that
  was wrong, not the code: the "never thinner as the cell gets taller" invariant compares neighbours, and the new
  metrics table was not sorted by cell_h — the table is sorted now and the reason is written above it.
  NOT yet run: the mutants, `make asan`, the CLI group, README/ISSUES.
  STEP 4 DONE — the mutant harness `.scratch/s134/mutate.py` (the s133 one, re-anchored for the changed source, plus
  11 new mutants: the old `cell_h - h` / `cell_h - w` formulas put back, each arm of `underline_y()` broken in turn
  (band ignored, always-cell-bottom, the two clamps dropped, the baseline not derived from the ascent), the metric
  faked at each call site (hover passes 0, one cursor pass passes 0/0, the descent unscaled, the descent never read).
  **The run found two s133 checks that were vacuous, and the reason is worth keeping: the s133 harness's CONTROL run
  was red** (3 checks of its copy could not read `config/kai-term.theme`), so every mutant "failed" and the summary's
  "26 mutants, 0 survived" meant nothing — the harness's own docstring warns about exactly that and the summary
  printed the claim anyway. With a green control, `no_ptr_in_flag` (deleting `if (!W.ptr_in)` from `hover_cell()`) and
  `chrome_ink` (feeding the underline a chrome colour instead of the cell's ink) survived. Both guards are pinned now
  by their own text (`if (!W.ptr_in)` counted 1 in main.c; `uint32_t ink = c->fg;` and `ink = c->bg;` counted 1 each
  in render.c), and `chrome_ink` was rewritten to a mutation that COMPILES — its first version used `chrome.fg`, which
  does not exist, so it was reported as a survivor when it had never run. The harness now separates "did not compile"
  from "survived". `test_render` is **617 checks, 0 failures** after those two scans.
  STEP 5 DONE — user docs, which s133 had not written at all: README's feature bullet, a paragraph in "Clicking a web
  address" (the underline appears exactly where a click would open, follows the pointer cell, takes the address's own
  colour, and has no key of its own), and the `click_opens_url` settings row; `config/kai-term.conf`'s
  `click_opens_url` block gained the same four lines. ISSUES' entry is written after the final measurement below.
- **2026-10-21, s135 — close-out of s132+s133+s134: the verification those entries claimed but had not run, then the
  commit and the push (CLOSED).** The tree found at session start: HEAD `124983c`, ALL of s132/s133/s134 uncommitted
  across 11 files, and ISSUES.md ALREADY carrying final-looking numbers (asan clean, CLI 99/0/13) that no journal entry
  recorded producing — claims written ahead of their runs, exactly the shape the rules forbid. The s135 task block
  above holds the plan; every number was re-measured in this session before anything was committed:
  `make clean && make -j8` exit 0 / **0 warning lines**; `make bin/test_vt bin/dbg bin/test_fm bin/test_render` exit 0
  / **0 warning lines**; `./bin/test_vt` **1673/0**, `./bin/dbg` **MODEL OK (0 problems)**, `./bin/test_fm` **384/0**,
  `./bin/test_render` **617/0**; `make asan` exit 0, **0 warning lines, 0 sanitizer reports**, same four counts;
  `KAI_TERM_TEST_NO_WINDOW=1 sh tests/cli_test.sh ./bin/kai-term` → **CLI: 99 checks, 0 failures, 13 skipped** (the
  13 windowed checks stayed skipped — the user is not at the machine to consent to ~15 mapped windows).
  `.scratch/s134/mutate.py` re-run whole: **CONTROL green** (1673/0, 617/0 — the s133 harness's failure mode checked
  FIRST), **36 mutants, 34 killed, 2 survived** (`hover_no_cap`, `renderer_adds_own_rule`), matching ISSUES exactly;
  no source changed during verification, so no re-run was owed afterwards and this run also covers the s133 mutants
  the s134 plan owed. README's new refusal-log sentence was grepped against the code before committing: `src/main.c`
  logs `url: the click at (%d,%d) opened nothing — %s` and `src/url.c` names all six reasons verbatim — the sentence
  is true of this tree.
  **Committed:** `24638cc` "URL hover: the address under the pointer underlines, under the letters, and a refused
  click says why" — src, include, tests, Makefile, config templates, README in ONE commit: s132/s133/s134 share
  `url.c`/`main.c`/`render.c`/`test_vt.c`/`test_render.c` hunk-for-hunk and splitting them after the fact would have
  manufactured boundaries the work never had; the message names each session's part. **Pushed** after `git fetch`;
  `git log --oneline origin/main -1` reads `24638cc`. `.scratch/` stayed untracked, never `git add`-ed.
  **Owed to the user's eye:** that the fixed underline now READS as an underline at their font size (the checks pin
  the rect against measured font metrics; no check pins the pixel) — plus the still-open step 2/3b/5/6 look items in
  "Current Status". Nothing else is in progress; the next thing is to ask the user what they want next.

## Next Steps

1. **The s102 plan is FINISHED — all seven steps are built, checked and committed — and the hover underline the user
   asked for afterwards is committed too (`24638cc`, pushed, `origin/main` re-read from git at s135).** Nothing is in
   progress. Step 7 (the plain-click web address) closed in s131 as `4610911`, pushed and re-verified from a fresh
   clone of that commit; s132's refusal log, s133's hover underline and s134's position fix landed together in
   `24638cc`. Steps 4, 5 and 6: step 4 is confirmed by the user in both forms; steps 5, 6 and 7 are committed and owed
   only their look to the user's eye (the list and the recipes are in "Current Status"). Do not re-open, re-ask
   about or re-derive steps 0-7 — each section under "Current Task" records the A/B and the mutants it was measured
   against, and the s120-s135 entries do the same for the sessions that built them.
   **So the next thing to do is either ask the user what they want next, or take an item from "Housekeeping" below
   (item 5) — not to invent a feature.** If a new feature is asked for, the standing survey rule applies: grep the
   tree for it first (0 hits means absent, not wanted), then check what the user's own setup actually asks for
   before ranking it.
2. **Owed to the user's eye** — the list is in "Current Status". The recipes: for step 2, open Kai-Term and look
   (a 2 px bar at the left edge of the cell, one second on / one second off while the window has focus, steady
   with `cursor_blink = 0`), then `printf '\033[1 q'` for a block, `'\033[5 q'` for an underline, `'\033[6 q'`
   for a steady one, and `vim`/`fish` should set their own shape as they always tried to; if the bar is too thin
   or too thick at their font size, `cursor_width` is the knob, and `cursor_blink` is the knob for the rhythm.
   For step 3b, open the band (`Ctrl+Shift+F`), select a text file, drag in the preview pane — the band should be
   the same colour as a terminal selection, `Ctrl`+`Shift`+`C` should paste exactly the file's own text with no
   leading indent, a double click should take the word and a triple the row, and scrolling or resizing the pane
   should drop the highlight. Nothing about these is a harness question.
3. **The windowed group of `tests/cli_test.sh` ran in s108 (141/0/0) at the user's request and passed.** It
   maps ~15 real windows, so it is still a run to ask about, not one to do unprompted while the user is at the
   machine. Check `nvidia-smi` first: a refused atlas upload fails EVERY window check for one unrelated reason
   after putting a dozen windows in front of the user. Measured s108: the LLM held 9632 MiB of 12227 and the
   atlases still fit in ~547 MiB free, so "the LLM is running" is NOT by itself a reason to skip — one probe
   launch (`-d -e true`, one window) decides, and that is what the script's own gate does.
4. **Closed — diagnosis recipe only, nothing owed.** Each of these is built, green and confirmed by the user in
   their own window; if a report ever arrives, start from the `-d` line named (all of them are listed under
   "Debugging this project"), not from the shader.
   - Config split: a line in the wrong file logs `is a key binding — those live in kai-term.bind now` or `is not
     a key binding — kai-term.bind holds bindings only`. Their own `~/.config/kai-term/` holds only
     `kai-term.conf` and there is no `/etc/kai-term`, so their bindings are the built-ins — a `kai-term.bind`
     they never wrote is valid, not a bug.
   - The `p` path key: a refusal logs `fm: not typing the path — the input line is not one I can describe`; the
     keys are config (`fm_key_path`, `fm_key_preview`, `fm_key_open`). The prompt follow: type a partial
     command, open the band, navigate TWICE — each must end with `fm: the prompt now holds the browser's
     directory and what you had typed`, and the text must appear once, not `text cd <dir>`; then diagnose
     headless with `.scratch/s86/follow2.c` against the prompt predicates before touching the model.
   - Mouse in full-screen programs: if the wheel scrolls nothing check `term_wants_key_mouse()`'s two conditions
     first — `alt_screen` and no `MOUSE_PRESS`/`MOUSE_DRAG` grabbed, so a program that asked for the mouse is
     deliberately excluded; both halves are config (`wheel_sends_keys`, `click_moves_cursor`).
   - Shell integration: `no integration ack` means the reader was not an integrated shell — expected for
     `env bash`, `sudo bash`, `/usr/bin/bash`; in **bash** a stale prompt string is expected behaviour, not a bug.
   - Images: `-p k` IS the supported way to show a picture ("The goal was image support, Not auto-detection by
     timg"). Do NOT re-add the graphics bits (4/16/18) to the DA1 answer or invent a `TERM` value — advertising
     sixel without implementing it makes the picture vanish entirely. Sixel/iTerm2 would be a NEW feature, only
     on an explicit request.
   - The stray `h`: the parser half is fixed; if it ever recurs, do not re-open the parser — the second mechanism
     is a paste flushed before the application asks for DEC mode 2004, which goes out RAW and the shell then
     interprets the escape inside it (`printf 'AA\e[?2004hBB' | wl-copy`, paste at a fresh bash prompt — with the
     delimiters the line reads `AA^[[?2004hBB`, inert; without them `AA2004hBB`). The checks are the `C0 inside
     CSI` group in `tests/test_vt.c`, and `grid_has_glyph()` is what proves a sequence was consumed rather than
     printed.
5. **Housekeeping, none a bug:**
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
6. **Scratch:** throwaway probes live in `/home/kaida/Project/.scratch/` (outside the project) and, since s112,
   in the repo's own untracked `.scratch/`. **What the user's "don't touch .scratch for now" means, settled with
   them 2026-10-20: do not DELETE or prune anything there; writing new files and editing existing ones is fine
   and expected** — that is where harnesses belong, and an agent that refuses to write there ends up putting
   throwaway work in `/tmp`, where it cannot be found again. Deleting is still the user's call. Harnesses worth
   keeping: `s70/timg_gate.c` (end-to-end image), `s74/inj_keys.sh` (the file-manager input-routing gate — the
   only coverage `make test` does not have) and `s74/argorder.c`, `s82/promptprobe.c`, `s86/follow2.c` (the
   prompt follow over a real pty), `s89/follow3` + `s89/bstoprobe` + `s89/leftprobe{,_old}`, `s90b/intgate.c`
   (the shell-integration gate — re-diff it against `main.c` before trusting it), `s97/csiC0.c` +
   `s97/fishdiag` + `s97/fishtime`, `s102/cube.c`, `s103/ab_ris` + `s103/ab_hex` (the mutation harnesses behind
   the hex-escape gate), `s104/ab_osc` + `s104/ab_nosweep` (the OSC sweep gates), `s105/importprobe.c` +
   `s105/corpus/` (the eight-format import corpus every importer number was copied from), `s107/ab_noimport/` +
   `s107/head_tree/` (the importer's A/B and what the new CLI checks were A/B'd against), `s107/ab_wheel/` +
   `s107/ab_tpl2/` (the wheel-focus rule and the template/README gate), `s112/qprobe.c` (DECSCUSR forms) +
   `s112/ab_model/` + `s112/ab_shape/` (which needs `old_cursor.c`, the pre-step-2 rule written as the function
   the test calls), `s113/wrapprobe.c` + `s114/wprobe2` + `s114/wprobe3` + `s114/ab/` + `s114/mutate.sh` (step
   3's grid layout, its A/B and its seven mutants), `s115/pvprobe` + `s115/selprobe{,2,3,4}` (the preview pane's
   own cell grid — every number in the `pvsel` group was printed by one of them before the check was written),
   `s116/mutate.py` + `s116/capzero.c` + `s116/longline` (the step-3b non-vacuity harness: the feature A/B plus
   12 mutants, the ASan probe that found the `out[-1]` write, and the 130-character line), and
   `s117/fgprobe2.c` (spawn a real shell on a pty, ANSWER its startup queries, prove a command ran by its own
   output, and print `tcgetpgrp` beside that evidence — the gate every confirm-quit decision rests on;
   `s117/fgprobe.c` is the version that got it wrong, kept to show why), `s120/fgprobe3.c` + `s120/msgprobe.c` +
   `s120/mutate.py` + `s120/mutate_render.py` (the hand-built process tree on a real pty, the exact wording the
   checks copy, and the 19 stage-A mutants plus the stage-B/C/D mutants — the `d_*` ones were only RUN in s121),
   and `s121/ab_stage_d.py` + `s121/ab_cli.sh` (stage D's wiring A/B against `git show HEAD:src/main.c`, and the
   whole-tree A/B for the headless config checks — the one that caught a mutant that never got built),
   `s126/mutate.py` (step 6's 32 mutants) + `s128/bellprobe4.c` (the real `less` in a real pty, drained to
   SILENCE, counting `0x07` bytes and parser rings per gesture and timing the BEL's reply — every number in the
   s128 entry came off it; `bellprobe.c` and `bellprobe3.c` are the two wrong revisions, kept to show what was
   wrong) + `s128/mutate.py` + `s128/scancount.py` (the 18 click-mute mutants, and a replica of the
   `tests/test_render.c` source scanners for MEASURING a scan count instead of guessing it),
   `s129/urlprobe.c` (the DRAFT run rule — kept because the shipped rule differs from it in exactly one case, the
   non-ASCII refusal, and the difference is a check) + `s129/realprobe.c` (the SHIPPED rule: every number in the
   `url` group was printed by this before the check was written) + `s129/mutate.py` + `s129/mutate_run.txt` (step
   7's 39-mutant run, both suites, and its docstring records the four vacuous-check findings) + `s129/fdprobe.c`
   (why a pty MASTER shows as `/dev/ptmx`, not `/dev/pts/N` — the reason the spawn check greps for both) +
   `s129/maskprobe.c` (why the signal-mask half needs a non-shell stand-in: `/bin/sh` normalises its own mask) +
   `s129/scancount.py` (the scan-count replica for the 14 URL wiring pins) + `s131/ab_nourl/` (the feature-absent
   A/B: the whole tree with `src/url.c` stubbed out) + `s131/verify_commit/` (a fresh `git clone` of the step-7
   commit, built and run to prove the commit is self-contained). One fact
   to keep in mind: this file
   cites those paths and a fresh clone cannot run them — so anything that is a real regression gate belongs in
   `tests/` eventually.
7. **Git:** keep committing verified work; `git fetch` before pushing.

## Project Files & Facts

```
Kai-Term/
├── .gitignore                   # bin/, build/, *.o/*.obj/*.d/*.so/*.a, editor leftovers, docs/ARCHITECTURE.md
├── LICENSE                      # MIT, "Copyright (c) 2026 Kaida"
├── AGENT.md                     # this journal
├── ISSUES.md                    # open items + Fixed + known limitations
├── README.md                    # features, deps, build, flags, the config files, shortcuts, exit status
├── Makefile                     # src/*.c -> build/*.o -> bin/kai-term; all/test/test-cli/asan/run/clean
├── config/kai-term.conf         # the shipped SETTINGS template: every default the parser accepts
├── config/kai-term.bind         # the shipped KEY-BINDING template: all 24 names at their built-in default
├── config/kai-term.theme        # the shipped THEME example: all 23 theme names at the built-in colours
├── include/kai-term/kai-term.h  # the single shared header: all types, prototypes, log levels, helpers
├── src/                         # config.c theme.c vt.c img.c mux.c layout.c fm.c shellint.c pty.c url.c font.c render.c main.c
├── tests/test_vt.c              # 1654 checks: parser, grid, graphics, config, themes, OSC colours, imports, prompt
│                                # machinery, click selection, confirm-quit (predicate, count, wording, notice
│                                # geometry), the visual bell (the BEL event, the fade, drawn-vs-hidden sessions),
│                                # the web-address click (the run rule, the gates, the wait, a real spawn)
├── tests/test_fm.c              # 384 checks: the file-manager model, previews, decoders, band geometry, pointer rules, the preview pane's text selection
├── tests/test_render.c          # 578 checks: the renderer's pure decisions — colour choices, cursor shape, the question
│                                # row's two colours, the box's six, the bell's colour — plus the source scans that cover
│                                # main.c's confirm-quit, visual-bell and URL wiring
├── tests/dbg.c                  # grid-model invariant checker (permutation + scroll_off/live_off bounds)
├── tests/cli_test.sh            # drives the REAL bin/kai-term: 99 checks / 13 skipped headless (the 13 are the window
│                                # groups; a display run was last measured at 149 in s124)
│                                # ONE probe launch gates the window checks; KAI_TERM_TEST_NO_WINDOW=1 skips them
├── bin/                         # kai-term, test_vt, dbg, test_fm, test_render (+ *_asan)  [generated, git-ignored]
└── build/                       # *.o + generated/xdg-shell-*                [generated, git-ignored]
```

What each file OWNS (the rules that bind them are in "Architecture & Invariants"; do not duplicate them here):

- `src/vt.c` — unit-tested core: VT/ANSI/UTF-8 parser (incl. the APC/DCS states the kitty graphics protocol
  needs), grid model, the image list (decode + placement + lifetime), selection text extraction, alt screen,
  `term_grid_geometry()`, evdev -> SGR button mapping (`mouse_sgr_button`, `mouse_wheel_button`), the
  process-wide palette and default ink/paper, the OSC colour handlers, the word/line click spans. No Wayland/GL
  dependency — the decoded PIXELS live here, so the whole protocol is testable headless.
- `src/img.c` — every image decoder in one place: PNG (libpng), JPEG (libjpeg), WebP (libwebp) and GIF (own LZW
  decoder, because giflib is not installed here), all returning premultiplied RGBA8, plus `img_sniff()` (magic
  bytes, never a file name) and `img_premultiply()`. Shared by the kitty graphics path in vt.c and the file
  manager's previews, so the two cannot drift. GIF decodes the FIRST image block only; an animated WebP is
  refused by libwebp's simple decode.
- `src/fm.c` — the file-manager model, no Wayland/EGL/threads: directory scan (`opendir`/`lstat`, sort, hidden
  filter, caps), navigation state, preview building (directory listing / image / text with syntax highlighting /
  metadata) and painting the two panes into `Cell` grids the existing renderer draws unchanged. Also
  `fm_image_fit()` (the preview box in cells AND in px, aspect kept in pixels, never upscaled, centred) — the
  renderer only draws the box it is given, which is what makes the box testable.
- `src/main.c` — all Wayland shell/input/clipboard/epoll logic: xkb keyboard, client-side key repeat, session
  shortcuts, pointer (bar clicks, pane focus, selection, mouse reporting, wheel — `ptr_button`, `ptr_motion`,
  `ptr_enter`), `layout_apply()`, frame-paced redraw, one epoll loop over every session's pty fd, SIGCHLD
  reaping by pid, CLI flags, data-device clipboard, the blink timer. Look here for pointer, keyboard, resize,
  frame pacing. **In no linked test binary.**
- `src/render.c` — EGL (platform Wayland) + GL 2.x: 2 glyph atlases + one GL texture per visible image, 1
  shader, 3 back-to-front passes emitted into ONE staging buffer but drawn as TWO batches with one draw per
  picture between them; instanced or expanded. Draws what `src/layout.c` says, owns no geometry.
  `render_chrome()` (colour), `render_cursor()` (shape) and `render_bell()` (the flash's colour) are the pure
  decisions a test can call with no display.
- `src/font.c` — fontconfig pattern -> FreeType face; alpha8 glyph atlas (shelf packer + hash by codepoint);
  fontconfig fallback faces; HarfBuzz-shaped color emoji (`font_color_glyph`) + RGBA8 color atlas;
  `font_resize()` re-rasterizes at a new pixel size.
- `src/pty.c` — `posix_openpt`/`grantpt`/`unlockpt`; child: `setsid` + `TIOCSCTTY` + `chdir` + exec `$SHELL` (or
  `-e COMMAND`); sets `TERM`/`COLORTERM`/`TERM_PROGRAM`; `pty_setsize`. With integration on, `tab_spawn()` hands
  it the string `shellint_argv()` built instead of the bare shell path. It does NOT touch termios — see the
  s75-s79 entry. Also the one tcgetpgrp call site: `pty_fg_busy()` (the wrapper) over the pure `fg_busy()` (the
  decision). No Wayland/EGL/font, so it links into the headless suites — see the Makefile comment.
- `src/url.c` — the whole of step 7, deliberately in a file the tests link rather than in the event handler that
  triggers it: the run rule (`term_url_span()` — ASCII-only cells, the scheme must be there, the trailing marks that
  are cut and the trailing slash that is kept, the non-ASCII/wide cell that makes the whole run REFUSED rather than
  shortened), the logical line walked with `term_line_span()` (one owner for the wrap marks, so a URL the WINDOW
  broke is one run and one the PROGRAM broke with a newline is two), `term_url_text()` (writes nothing when the
  bytes do not fit — half an address is never opened), `url_is_openable()` (the argument rules: scheme, no control
  character, no leading `-`, the `URL_MAX` ceiling), `url_click_opens()` (the gates as ONE ordered decision, so a
  test can flip each flag), `url_open_due()` (the double-click wait, strictly-greater boundary matching
  `term_click_count()`), and `url_spawn()` (fork + setsid + /dev/null + `closefrom` + the signal mask cleared +
  `execvp` with one argument — never a shell). No Wayland/GL, so all of it, the spawn included, is checked in
  `tests/test_vt.c`.
- `src/theme.c` — themes: a file of COLOURS only, in kai-term.conf's `key = value` syntax (and kitty's
  `key value`). `theme_find()`, `theme_apply_file()`, and the `--import-theme` importer (`theme_sniff`,
  `theme_parse_text`, `ThemeVals`, `theme_write_file`, `theme_dest_dir`, `theme_name_of_path`, `theme_import`).
  No Wayland/GL/Term dependency: all of it is testable headless, and `tests/test_vt.c` derives the accepted-name
  list from this file's `THEME_KEYS` table exactly as it derives the config names from config.c's ladders.
- `src/config.c` — the two config files, one `key = value` line reader serving both, the search order
  (`config_find`), defaults, clamping, the name-routing dispatcher `apply()` (`apply_bind_key` then
  `apply_setting`), the `#` comment rule (`config_strip_comment`), `cfg_parse_color` (the ONE colour parser,
  shared with theme.c and the OSC handlers), and the key-binding spec parser
  (`keybind_parse`/`keymods_parse`).
- `src/mux.c` — sessions of one window: dense `Tab` array, active index, open/close/goto/next/prev, `max_tabs`,
  unread-output flag, `mux_title()`, `mux_recolor_defaults()`, `mux_running()` + `confirm_message()` +
  `confirm_warning()` (step 4) and `mux_bell_live()` (step 6: which of the sessions the layout DRAWS still have a
  flash on screen). No Wayland/EGL/fork and no fd it owns — one read-only exception, `pty_fg_busy()`'s
  tcgetpgrp, which cannot block and answers "idle" for an `mfd` of -1, so the confirm-quit decision stays
  unit-testable.
- `src/layout.c` — pure geometry: in window px + cell px + padding + gap + mode + session list; out bar strip,
  `BarItem[]` rects + labels, `PaneGeom[]` rects (each with cols/rows), the file-manager band (`FmGeom`: browser
  rect + preview rect + their cols/rows) and `layout_hit_fm()`, `layout_grid_size()`, `layout_initial_size()`.
- `src/shellint.c` — the shell integration, deliberately dumb and testable: the three shell scripts as string
  constants, the private per-instance directory, one `cd-request.<id>` file per session, the ONE control
  sequence and the measurements that chose it, and the command line `tab_spawn()` hands `pty_spawn()`. Nothing
  here talks to the shell except through those files and the pty; the protocol's other half is `kai_apc()` in
  `src/vt.c` and the state machine in `src/main.c`.
- `tests/test_vt.c` — 1654 checks. Groups: parser (C0 inside a control sequence; a CSI sequence split across two
  bursts), clipboard text policy (`paste_repair`, `clipboard_mime_rank`), grid, selection, alt screen,
  `live_off`, emoji clusters, CSI 3 J, bracketed paste, `term_init` guard, reflow, `term_grid_geometry`,
  zoom/alt-screen row-shrink, `mouse_sgr_button`/`mouse_wheel_button` (the SGR order and the 64/65 wheel codes),
  DECCKM (`app_cursor`, `term_arrow_seq()` answering SS3 or CSI), the mouse-as-keystrokes helpers
  (`term_wants_key_mouse`, `term_wheel_arrow_keys`, `term_click_arrow_keys`), the graphics path
  (base64/chunking/PNG/zlib/raw/premultiply/placement/delete/scroll anchoring/id replacement/DA1-DA2-DSR
  answers), mux, keybind/keymods parsing, config keys and template name-coverage (the two config ladders, the
  theme table, `usage()` vs README's option table, and README's own settings and binding tables diffed against
  the parser in both directions), the `hex escape` gate, `layout_compute`/`layout_grid_size`/
  `layout_initial_size` (the tab bar, the panes and the band's SHARE, three startup round trips), the
  palette/cube/OSC-colour groups, the theme file and precedence, the theme importer (sniff, per-format mapping,
  dropped keys, the native round trip), the click-selection group (`term_word_span`, `term_line_span`,
  `term_click_count`, the `click_selects_word` key), the confirm-quit groups (`fg_busy` and its guards,
  `pty_fg_busy` against a REAL pty with a hand-built process tree, `mux_running`'s count and index list,
  `confirm_message`'s exact wording per branch plus its cap/NULL cases, the `confirm_quit` key and every
  spelling of its three values, and the notice row's geometry — one row given up at the bottom, the too-short
  window that drops the row, the clip at the row's columns, the band measured against the reduced grid area),
  the shell-integration scripts as they land on disk, and the
  prompt machinery the follow is built from (`term_at_prompt()`, `term_prompt_line()`,
  `term_prompt_line_typed()`, `term_prompt_line_is()`, the echo window and its transition rule, the clear ->
  `cd` -> re-populate sequence as the model sees it). The **`url` group** (step 7, 138 checks, sitting next to the
  pointer-precedence group so the click features stay together) covers the run rule and its spans, the trailing-mark
  cut/keep table, the scheme rules, the non-ASCII/wide refusal, the window-broke vs program-broke split, a tab's gap,
  the alternate screen (the scan FINDS the address; the gate declines the click), the `URL_MAX` boundary on all three
  sides, the delimiter corpus, the scrolled-away limit, `url_is_openable`, the 9-case `url_click_opens` gate table,
  the 16-case `url_refusal` table (every reason named, the reasons' precedence over each other, and
  `url_click_opens` pinned to `refusal == NONE`), the `url_open_due` boundary against `term_click_count`, the two
  header constants, the `click_opens_url` key and an
  end-to-end `url_spawn` through two stand-in openers that report their own argv, session id, signal mask and open
  descriptors. `main.c` itself is in no test binary — that machine is
  covered by `.scratch/s86/follow2.c` and the live harness. Two invariants with dedicated checkers: the `row[]`
  permutation (also `tests/dbg.c`) and the startup-size round trip.
- `tests/test_fm.c` — 384 checks against a real temporary directory tree (dirs, a dotfile, a dangling symlink, a
  FIFO, an unreadable dir, four image formats, a binary file): scan + sort + caps + truncation, the hidden filter
  and `fm_set_hidden`, selection and scrolling (`fm_row_pos`, `ensure_visible`), navigation, the request-id rule,
  every preview kind and its error path, the highlighter reaching the cells, the pane painters, `fm_image_fit`,
  the preview's row budget (`ink_in_box()`), the preview pane's own scroll (`pv_scroll` / `fm_preview_scroll` /
  `fm_preview_scroll_max`), the selection colour reaching `fm_paint()`, the band's rects inside
  `layout_compute()`, the wheel-over-the-band keyboard rule (`fm_wheel_focus`), and the **`pvsel` group** (step
  3b): the press/drag range, the reading-order normalisation, the clamping, the word and row spans read back as
  the TEXT they cover, the wide-glyph rules, the band read straight out of the pane cells cell-for-cell
  (`band_mismatches()`), the copy's buffer bounds including `cap == 0`, the invalidation on scroll / resize / a
  new preview, a directory preview's entry list being selectable too, and what a row wider than the pane can
  copy (28 cells of a 130-character line).
- `tests/test_render.c` — 578 checks, the only coverage of the renderer's decisions that needs no display. It
  links `render.o` (plus `fm.o font.o img.o vt.o shellint.o layout.o mux.o config.o`, which is what `render.o`
  needs to LINK) and calls only the pure functions — never `render_init()`, never a GL call. Colour: the
  configured `cursor_color` is what an active pane's cursor is drawn with, an inactive session's cursor is that
  colour mixed with the configured background, the built-in default still yields `CURSOR_COL`, two different
  values change both, a different background moves the dimmed colour but not the active one, window opacity
  never reaches these values, and the selection band / active bar item / focus border follow
  `Config.selection_color`. Shape: `render_cursor()`'s truth table — the default bar and its rect,
  invisible-means-nothing in all four shapes, both ends of the width clamp (40 px in a 9 px cell -> 3 px; a 1 px
  cell -> 1 px, never 0, which would draw no cursor at all), the underline's rect and its `cell_h/3` clamp, the
  BLOCK rules kept exactly as they were, the NEW rule that a bar/underline does NOT step aside for a band, and
  448 invariant checks over every (shape × visible × band × 7 cell sizes × 7 widths): never both a block and a
  quad, `invert` only with a block, a visible cursor always paints something in one of the three passes, and the
  rect never leaves its cell. The last checks of both kinds are SOURCE SCANS of `src/render.c`, `src/main.c` and
  `src/fm.c` — the honest way to cover the `render_init()` call, which lives in `main()` and so is in no linked
  test; every scan reads a probe file the test writes, never this file (a self-scan counts its own `CHECK`
  lines). The wiring pins (`conf.cursor_width` reaches `render_init()`, `rend.cursor_on = W.cursor_on` exists,
  `render_cursor` appears 4× in `src/render.c`, `cursor_on` 3×, and the six confirm-quit counts in `src/main.c`:
  the window gate 2×, the tab gate 3×, the answer 3×, `confirm_clear` 4×, the row handed to the layout 1+1×, the
  shared predicate 2+2×) are text counts — re-diff them, they are not executed. The 17 URL scans (s129's 14 plus
  s132's 3) pin that `src/main.c` CONSULTS `src/url.c` instead of keeping its own copy: the gate asked once and
  asked for a REASON, that reason logged and gated on the switch,
  `conf.click_opens_url`, the scan asked twice (click + deadline) with `strcmp(holds, clicked)` at the deadline,
  `url_tick` defined and called, the wait using `URL_OPEN_WAIT_MS` not a literal, the pending stamp cleared in both
  places that end a wait, one `url_spawn` site taking `URL_OPENER_PROGRAM`, and in `src/url.c` the presence of
  `closefrom`/`setsid`/`sigprocmask` beside the absence of `system`/`popen`/`execl`/`execlp`/`execve`.
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
  NULs at all is undetectable; bash's prompt string stays stale after an integrated `cd`; a word the window
  broke mid-word is selected per row; a preview row longer than the pane copies only what the pane holds; a job
  backgrounded with `&` reads idle to confirm-quit. Full list: ISSUES.md "Known limitations".

## Architecture & Invariants

Rules a change must not break. Why each exists: git history of fixed bugs (`git show d5afe4a:ISSUES.md`).

**Data flow.** PTY bytes -> `term_parse()` (vt.c) -> one `Grid` per session -> `Mux.tab[]` owns one `{Term, Pty}`
per session, one ACTIVE -> `layout_compute()` (window px + cell px + padding + mode + session list) -> bar strip,
item rects, pane rects -> `render_draw(&rend, &font, &g_layout, terms)` -> bar + one grid per pane + focus
border -> EGL -> `wl_surface`. Input: `kb_key`/`ptr_*` -> pty_write to the ACTIVE session, after `ptr_*` is
hit-tested bar then panes.

**Event loop (main.c).**

- epoll over: `wl_display` fd; one pty master fd per session (`tab_for_fd()` maps back; pty EOF closes that
  session, not the program); the 8 ms frame timerfd (`tfd`, `FRAME_INTERVAL_NS`); `signalfd(SIGCHLD)` (SIGCHLD
  blocked so only signalfd reads it; reap `waitpid(-1,…,WNOHANG)`, match pids via `tab_for_child()`, then zero
  `pty.child`); the paste idle timerfd (`pwfd`); the key-repeat timerfd (`krfd`); the blink timerfd (`cbfd`);
  the clipboard paste pipe read end joins transiently.
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
  xterm both do this). ESC is the one exception: it CANCELS the sequence and starts a new escape, so it is
  handled by the caller and never reaches the shared executor. ONE file-local `exec_c0()` is what both
  `PS_GROUND` and `PS_CSI` call — the rule used to exist in two copies that disagreed (CSI's copy was
  `state = PS_GROUND; i--;`, which ENDED the sequence), and that is why the states drifted. Ending a sequence at
  a control byte does two damages at once: the mode the sequence was setting is silently never applied, and the
  sequence's remaining bytes are painted as text. Because every mode-ENABLING sequence ends in `h` (`?2004h`,
  `?1049h`, `?25h`, `?1h`), the painted character is an `h` — a stray `h` on the user's line is the visible half
  of an invisible mode bug. Pinned by 14 checks, which also pin the OPPOSITE case: a CSI sequence split across
  two pty reads with NO control byte in it must stay open and finish in the second burst. Do not "simplify"
  either one.
- **A sequence is only complete when its final byte arrives; a burst boundary never ends one.** `term_parse()`
  re-enters with `ps->state` intact, and that is the whole mechanism — a probe or a fix that treats the end of a
  read as the end of a sequence invents a bug.

**Clipboard (main.c + the paste helpers in vt.c).**

- **A NUL byte must never reach the pty.** `paste_flush()` runs `paste_repair()` on the buffered clipboard block
  before writing any of it. A source may serve UTF-16 under a UTF-8 label (Firefox does, under
  `text/x-moz-url-priv`), and UTF-16 written verbatim into a bracketed paste is what the user saw as a stray `h`:
  bash's bracketed reader stops at the first NUL, zsh inserts every byte and paints each NUL as `^@`. Repair
  only when the block actually contains a NUL — a block with none is copied through byte-for-byte, unchanged.
- **Which mime type to ask for is a preference, not an enumeration accident.** `clipboard_mime_rank()` decides;
  `offer_offer()` keeps the highest rank seen. The old rule overwrote the recorded type for every `text/*` name,
  so the LAST type the source advertised won — with Firefox that is `text/x-moz-url-priv`, which is UTF-16.
  Never "fix" this back to last-wins or first-wins: both are at the source's mercy.
- **UTF-16 is decoded only when the evidence is unambiguous** — a BOM, or NULs that are dense (`nuls*2 >= len`)
  and one-sided. Sparse NULs are stripped, not decoded: guessing on ambiguous input turns good text into
  garbage, which is worse than removing the NUL. UTF-16 with no BOM and no NULs at all is undetectable and is a
  documented limitation, not a bug to chase.
- **`Ctrl`+`Shift`+`V` is Kai-Term's paste; plain `Ctrl`+`V` is the shell's.** Only the first is intercepted
  (`send_key()` gates on `shift_held`). The shell's path reads the clipboard itself and drops NUL bytes, so the
  two can differ from the same clipboard — when a user reports "one works and the other doesn't", that
  difference is the diagnosis, not a second bug.
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
- `cy` is a LIVE-view coordinate (the parser addresses `row[live_off + cy]`) -> `term_resize` re-anchors `cy`
  with `live_off`. The cursor is drawn only when `scroll_off == live_off`. Whole-viewport scrolls snap
  `scroll_off` back to `live_off` first.
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
  no-reflow variant for the ALTERNATE screen (a full-screen app repaints itself; re-wrapping would scramble it)
  — the parked main grid is still reflowed.
- **Images are anchored to an ABSOLUTE document row** (`Grid.row_base` + the row the cursor was on), never to a
  viewport row or a row pointer: a viewport row is invalidated by every scroll and a pointer by every reflow.
  `row_base` moves only when rows are evicted at the top or the window slides over its scrollback, so
  `image_prune()` can decide "this picture's rows no longer exist" from two numbers. The span in cells is
  computed in vt.c, not in the renderer, because it is also what `a=T` moves the cursor past — if the renderer
  scaled differently from the model the prompt would land on the wrong row.

**File manager band (fm.c + layout.c + render.c + main.c).**

- The band is geometry, model and drawing in the same three places as everything else: `layout_compute()` splits
  the grid area VERTICALLY first (a band taking `fm_size` PERCENT of its height on top, terminal pane(s) below
  at `pane_y = grid_y + band_h + gap`), then splits the band horizontally (browser | preview). Sessions stay
  ordinary `pane[]` entries, only shorter — that is what keeps `layout_hit_pane`, `layout_pane_of_tab`,
  `layout_grid_size()`, the focus border and `W.gx/W.gy` working with the band on AND off. `layout_hit_fm()`
  must be checked BEFORE `layout_hit_pane()`.
- The band is clamped, never trusted: `band_h = grid_h * pct / 100` (a `long long` multiply, so a 4K window
  cannot overflow) is reduced so the shell keeps `FM_TERM_MIN_ROWS` rows, and dropped entirely when even that
  leaves fewer than `FM_ROWS_MIN`. Clamped in layout.c, not config.c, so no caller can ask for a 1-row band —
  the tab bar's "the terminal wins" rule again. It is a SHARE and not a row count on purpose: the band then
  survives a resize or a zoom with the proportion the user asked for. `layout_initial_size()` is its inverse
  (`grid_h = ceil((rows*ch + gap) * 100 / (100 - pct))`) and the round-trip checks in `tests/test_vt.c` are what
  prove that arithmetic.
- **The preview pane's rows have ONE owner.** `preview_head_rows()` (2, +1 when the preview has an error line)
  and `preview_meta_rows()` (image previews only: `min(nmeta, prev.rows - head - 1)`, so the picture always keeps
  a row) are called by `paint_preview()` AND by `fm_image_fit()`; the picture box starts at `head + meta_rows`.
  Glyphs are emitted in the batch AFTER the picture, so any disagreement between the two about which rows text
  owns paints metadata across the image — that was the bug, and `ink_in_box()` in `tests/test_fm.c` keeps it
  from coming back.
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
  While the PREVIEW owns it, the band's `fm_key_pageup`/`pagedown`/`home`/`end` bindings scroll the previewed
  TEXT (`fm_preview_page`/`fm_preview_top`/`fm_preview_bottom` in fm.c, routed in `fm_keys()` in main.c) instead
  of moving the listing; `fm_key_up`/`down` deliberately keep moving the listing, because changing WHICH entry is
  previewed is the one thing the preview cannot do for itself. One page is `fm_preview_page_rows()` — the body
  rows the pane shows at once — because a Page key must move by exactly what the user can see; all three go
  through `fm_preview_scroll()`, so the clamp has one owner and a preview with no body (a picture, plain
  metadata) simply does not move. Same four bindings, no second set of names to keep in sync in
  `kai-term.bind`.
- **The pointer's POSITION never takes the keyboard — a click does** (`fm_focus_on_hover = 0`, the default).
  A scroll gesture over the band scrolls (it walks the listing, or scrolls the preview body) and leaves the
  keyboard where it was; `fm_focus_on_hover = 1` opts back into the pane under the pointer owning it. The rule
  is `fm_wheel_focus(hit, steps, on_hover)` in `src/fm.c`, not an `if` in `main.c`, for the standing reason
  that main.c is in no test binary; it also refuses `FM_HIT_NONE` itself rather than trusting the caller's
  filter, and returns -1 when `steps == 0` in BOTH modes. `steps` is `W.frame_discrete` or
  `(int)W.frame_axis_val / cell_h`. What it replaced is the shape worth remembering: `fm_wheel()` called
  `fm_focus_set(hit == FM_HIT_LIST ? … )` on its fifth line, BEFORE computing `steps`, and returned early when
  `steps == 0` — so a `wl_pointer.frame` that produced no whole-cell scroll (a trackpad frame with a sub-cell
  delta, a gesture under one row, a frame arriving without axis data) moved the keyboard into whichever pane the
  pointer happened to be over and did nothing else, which a user can only describe as "hovering steals the
  focus". **A state change placed before the guard that decides whether the event counts is a bug even when
  every later check is correct.** There is no hover handler in this project and there should not be one:
  `ptr_motion` and `ptr_enter` touch no focus state at all, every `hover` hit in `src/` is a comment naming this
  bug, and `render_draw()` draws the focus border from `fm->focus` alone — nothing on screen follows the pointer.
- `fm_send_cd()` is the only place the feature writes to the shell, never to a session on the alternate screen,
  and only when the directory actually changed (`fm_last_cd`, cleared by `tab_switch()`).
- **The prompt invariant (what the follow may never violate).** Kai-Term cannot see where a prompt ENDS — the
  tty echo arrives as ordinary output — so the model remembers the whole cursor row as the shell last printed it
  (`prompt_row`/`prompt_cx`/`prompt_cy`) and treats "does that row still match, cell for cell" as the only
  evidence available. Five rules, each with a check in `tests/test_vt.c`:
  1. **The `cd` is written only from ERASING/CDING behind `term_at_prompt()`** — i.e. only once the row is
     proven to be the bare remembered prompt. "Runs a mangled command" is closed by construction, not by
     trusting a key binding, so no capture rule may ever gate the write.
  2. **A steady state is not evidence.** The echo window closes only when the row CHANGED into the remembered
     prompt between the start and the end of one burst (`prompt_matched_before` in `term_parse()`). A burst that
     leaves the row exactly as it was — fish repainting OSC 7 / OSC 0 / DECSET after Kai-Term re-typed the user's
     text — must not close it, or the next burst re-takes the snapshot over Kai-Term's own echo and a line
     holding text starts answering "empty".
  3. **A burst boundary is not a prompt.** CDING additionally waits for the session to go quiet
     (`FMCD_QUIET_MS` 150, advanced from the frame timer) before believing the new prompt is drawn: fish answers
     a `cd` with the echo, a continuation row and prompt chrome in separate bursts, each of which re-takes the
     snapshot.
  4. **The snapshot's cursor anchor means "where the cursor sits when the line is EMPTY".** `prompt_cx`/
     `prompt_cy` are the column `term_at_prompt()` insists on AND the column `term_prompt_line()` measures typed
     text from, so `prompt_snapshot()` must not re-take them when the row is cell-for-cell the remembered prompt
     and only the cursor moved. `prompt_row_same_cells()` is that test and also the row half of
     `prompt_row_matches()` — one comparison, two callers.
  5. **Opening the echo window has to mean something.** `term_note_input()` opens it for any payload that can
     put a character on the line (text, Tab, Up/Down, Ctrl+R/S/T/V/Y/_, Insert, PageUp/PageDown, F-keys, an
     unknown or truncated sequence) and — only when the line is already provably the bare prompt — not for one
     that cannot (cursor movement, deletion, Ctrl+L, a bare Escape: `payload_cannot_insert()`). The row
     comparison still re-checks everything on every call, so a key that really moves the cursor or rewrites the
     row still makes `term_at_prompt()` answer "no".
  The three description predicates must not be confused: `term_at_prompt()` = "safe to WRITE here"; strict
  `term_prompt_line()` = "I can describe this line and nothing survives an erase"; `term_prompt_line_typed()` /
  `term_prompt_line_is()` = "ignore the shell's ghost text right of the cursor", legal only for the capture
  (which is followed by the proof) and for confirming Kai-Term's own typing. `input_pending` is deliberately NOT
  consulted by the description predicates — the row comparison is the evidence, and Kai-Term's own typed path
  sets that flag while leaving a perfectly describable line behind.

**Shell integration (shellint.c + the `K` APC channel in vt.c + main.c).**

- **One channel, so the data never rides with the control.** The pty input stream is the only thing Kai-Term can
  write to the shell, and a key binding is a fixed byte sequence — anything appended would land on the user's
  line. So the sequence carries NO data and the directory travels in a per-session file (`cd-request.<id>`,
  written as a temp + `rename()`d so the shell can never read a half path). A stray sequence can therefore never
  run a path, and no path can ever be smuggled into a sequence.
- **The fast path is used only after THIS session's shell announced itself** (`ESC _ K ; kai-term ; integration
  ; <kind> ST`, dispatched by `kai_apc()` — one more branch on the APC kind byte, no new parser state), and a
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
  COMMAND` is never wrapped — that command line is what the user asked to run. The private directory is `mkdtemp`
  under `$XDG_RUNTIME_DIR` else `/tmp`, `chmod 0700`, refused if the name contains a `'` (the command line
  quotes with `'`), and removed at exit.
- **`ESC c` (RIS) memsets `Term`, and the announcement must survive it** — the script announces once, at shell
  startup, so losing it would silently drop the feature for the rest of the session. The prompt snapshot is
  deliberately NOT preserved (a reset really does invalidate it).
- **An announcement is an EVENT, counted, not just a kind.** `Term.shell_int_gen` goes up on every accepted
  announcement and `Tab.int_gen` is the value main.c last saw; when they differ, `fm_int_try()` clears
  `Tab.int_dead` (and logs it). Without the counter the first shell that failed to answer — a plain `env bash`
  started inside an integrated fish — would keep that session on the typed path for the rest of the run. The kind
  cannot express this (fish -> a nested fish -> back to fish announces three times with one kind), which is why
  it is a counter and not a flag.
- **The scripts wrap a bare `fish`/`bash`/`zsh` and re-announce after every command.** That is what makes a
  shell the user starts at the prompt integrated too, and what feeds the counter above: fish `fish_postexec`,
  bash `PROMPT_COMMAND` (PREPENDED, string AND array form handled, guarded by a NON-exported variable so a
  nested bash still installs its own), zsh `precmd_functions+=`. Three limits, all deliberate: only a command
  with NO arguments is wrapped, a name the user already defined as a function or alias is never replaced, and
  the wrapper re-announces its own kind when the nested shell exits. A heartbeat was checked not to leak into a
  command substitution before it shipped.
- **The prompt is repainted only where the shell can be asked to.** fish: `commandline -f repaint` at the end of
  the handler (measured to re-expand the prompt with the typed text byte-identical). zsh: `zle reset-prompt`
  (verified in zsh 5.9 since s94 — the gate's zsh run repaints and answers in 54 ms; what has never been
  measured in zsh is the inert-when-unbound case). bash: NOTHING is attempted, because readline expands the
  prompt once per `readline()` call — `C-l` leaves the old directory and printing `${PS1@P}` over the line
  garbles it (both measured). Do not "fix" bash by pressing Enter for the user or by writing over the prompt
  line.

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
  and BEFORE the draw — glyph lookups run inside pass 3, so uploading at the top of `render_draw` shows every
  new glyph one frame late (and a first emoji as a black box). Application pictures are the other kind of
  texture: one per image the model shows, keyed by `KImage.serial` (unique per session, never reused, so a
  recycled pointer cannot hand back a stale texture), re-uploaded when `version` changes, and deleted after any
  frame in which no visible image claimed it (`ImgTex.seen` stamped with `Render.img_frame`) — the GPU never
  holds more than what is on screen.
- One shader; `a_glyph` selects the path: 0 background quad, 1 mono glyph (tinted by `v_fg`), 2 color emoji
  (premultiplied, not tinted), 3 an application picture (the same premultiplied composite against the picture's
  own texture; vt.c premultiplies every decoded pixel, so still no GL blending).
- Pass order back to front: all cell backgrounds, then selection bands, then glyph quads — a WIDE glyph quad
  spans into the next cell and the neighbour's background must not be emitted after it. Continuation cells
  (`ATTR_WIDE`) are skipped in pass 3.
- **A frame is TWO draw batches, not one call**, because a picture must be over the cell backgrounds and under
  the text and blending is never enabled: batch A = pass 1 + bar backgrounds (range `[0,nq_bg)` of the staging
  buffer), then one draw per visible picture, then batch B = bands + glyphs + labels + focus border. Both ranges
  live in ONE staging buffer (records are fixed-size, so a batch is just a range); `glClear` runs ONCE per frame
  — clearing between batches would erase batch A. `setup_draw_state()` is re-applied before each batch because
  an atlas texture can be created between them.
- Instanced path when both `GL_ARB_draw_instanced` and `GL_ARB_instanced_arrays` are advertised; otherwise the
  same 19-float records are expanded to 21 floats per vertex and drawn with `glDrawArrays`.
- No GL blending is ever enabled: the fragment shader composites `premul + bg*(1-a)` and a Wayland EGL ARGB
  buffer is PREMULTIPLIED, so writing raw colour with partial alpha reads washed out; `max(a, bg.a)` is wrong
  too (it painted the glyph's bounding box opaque). `Config.opacity` must be clamped with `fclamp_`, never
  `iclamp_` (an int clamp turns 0.5 into 0).
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
- Pass 2 skips a cell that was never written (`ch == 0`) so a drag past the end of a line stops at the last
  glyph, while real spaces and `ATTR_WIDE` continuation cells keep their band; pass 1's cursor block uses the
  SAME predicate.
- Band edges stay ABSOLUTE buffer pixels — correct across panes because panes never overlap in x.

**Config.**

- `key = value`, read once at startup, TWO files, each searched on its own: `-c FILE` / `-b FILE` >
  `$XDG_CONFIG_HOME/kai-term/` > `~/.config/kai-term/` > `/etc/kai-term/` — the first location holding a REGULAR
  file with that name wins and the rest are never opened, and nothing found anywhere leaves `config_find()`'s
  `out` empty, which is how the callers know to keep the built-in defaults. The search is per FILE NAME, not per
  directory: a `kai-term.conf` in `~/.config` and no `kai-term.bind` there still picks up the system bindings.
  `kai-term.bind` is read AFTER `kai-term.conf`, so for a name both set the bind file wins. CLI flags are
  applied AFTER both files so they win and never write back.
- The split is by NAME, not by which file is open: `apply_bind_key()` takes the 24 `key_*`/`fm_key_*` names,
  `apply_setting()` the others. A binding written in `kai-term.conf` is applied with a log line naming where it
  belongs now (files written before the split must not lose bindings silently); a setting written in
  `kai-term.bind` is refused by name, never treated as an unknown key. `tests/test_vt.c` reads both name lists
  out of the strcmp ladders in `src/config.c` and compares them with `config/kai-term.conf` +
  `config/kai-term.bind` AND with README's tables — a name added to a parser without a template line, a
  template line the parser does not know, and a deleted template line each fail a check.
- `#` starts a comment when ONLY whitespace precedes it, or when whitespace precedes AND follows it. The old
  rule (whitespace before is enough) was a bug: it cut `background = #1c1c22` at the `#`, and an empty value is
  legal, so a documented colour line set nothing and logged nothing. `working_dir = /home/me/my#project` keeps
  its value; `value # note` is stripped. Unknown keys log and are ignored; a missing file is not an error.
  Colours accept `#RRGGBB`, `0xRRGGBB`, bare `RRGGBB` and `AARRGGBB` (`cfg_parse_color` skips an optional
  `#`/`0x`, reads hex, and adds `0xff000000` when 6 digits or fewer were given) — ONE colour parser for the
  config file, the theme file and the OSC handlers. Ranges are clamped at parse time.
- **One value owns the default ink and paper.** `Config.fg`/`Config.bg` reach the grid through
  `term_set_default_colors()` (file-scope `g_def_fg`/`g_def_bg` in `src/vt.c`, read by `term_default_fg()` /
  `term_default_bg()`), called from `main.c` after the config readers and before the first `term_init()`. That
  one variable is BOTH the colour every blank/erased/scrolled-in/reflowed cell is painted with AND the
  `cell_pristine()` sentinel meaning "never written" — they must stay one value, or reflow's `row_used()` counts
  trailing blanks as content and selection bands appear over nothing. `DEF_FG`/`DEF_BG` are only the built-in
  defaults `config_default()` starts from; nothing in `src/vt.c` may read them again. And nothing may paint a
  background from a different source: `glClear` uses `Config.bg`, pass 1 uses the cell's `bg`, so a third value
  anywhere draws a visible frame around the grid (s101). `palette[0]` is deliberately NOT the configured
  background — `SGR 40` / `48;5;0` stay black, as every other terminal does it; README says so, because it is
  the next thing a user will report.
- **The palette is process-wide, never `Term` state.** `g_palette[256]` in `src/vt.c`, read through
  `term_palette()` and written through `term_set_palette_entry()` / `term_palette_reset()`; `term_init()` and
  `ESC c` must NOT refill it, because they run for every new tab and would
  throw the user's theme away (`term_palette_reset()` exists for OSC 104 only). Two consequences to remember: a
  `Cell` stores a RESOLVED colour, so changing an entry repaints nothing already on screen (a theme file is
  therefore applied before the first session is created, and an OSC colour change sweeps the grid), and entries
  16-255 are computed, so a theme that writes them is fighting the cube — the theme layer accepts 0-15 only.
- Defaults live in `config_default()` AND the `DEF_*` macros AND the files in `config/` — change one and the
  checks fail, which is the point. Do not hand-copy a name list into a test: derive it from the source.
  `CONFIG_SET_KEYS_MAX` (64) caps how many explicitly-set names `Config.set_keys` records; exceeding it logs and
  drops the record, which would silently let a theme override a key the file did set.

## Debugging this project

- `./bin/kai-term -d` (log level 3) prints: the font chosen + `color emoji: <path> (<n>px strike)`; the GL
  version + instancing state; `shell starts in <dir>`; `grid resized to CxR (window WxH px, cell wxh, padding
  P, unused ux x uy px, tab i/n, tab view|side-by-side view, N panes)`; one
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
  exercises the whole chain and prints those lines. For the model alone, `.scratch/s70/timg_gate.c` runs the
  real timg in a PTY with Kai-Term's winsize, feeds its bytes through `term_parse()` and writes back the answers
  the model queues — no window. The file-manager preview is counted separately and named — the line ends
  ` + the file-manager preview` when one is drawn, because it has its own texture (`Render.fm_tex`) and is not
  in the `M` count. `fm preview glTexImage2D failed: 0x... (WxH)` names a GL problem with it.
- **File manager.** `fm: opened on <dir>` / `fm: closed — the terminal is the whole window again`; a request
  `fm: listing <dir> (request N)` and its rejection `fm: dropping stale listing of <dir> (request X, newest Y)`
  (that pair is how you see the request-id rule working, and how a broken id shows up — s74's argument-order bug
  made every one of them read `request 0`); navigation `fm: open -> <dir>` / `fm: parent -> <dir>` /
  `fm: refresh -> <dir>`; the keyboard handover `fm: keyboard -> terminal|browser|preview`; a click
  `fm: click row N selects <name>`; what was typed at the prompt
  `fm: typed the selected path at the prompt: <path>`; the shell follow `fm: told the shell to follow: cd <dir>`
  (absent = the directory did not change, or the session is on the alternate screen, or `fm_follow = 0`); the
  clear -> `cd` -> re-populate sequence logs its steps — `fm: clearing the shell's input line to follow the
  browser into <dir>`, that `told the shell to follow` line, `fm: re-typing what was on the line: <text>`, and
  the completion `fm: the prompt now holds the browser's directory and what you had typed`. Anywhere it stops
  instead: `fm: dropped the follow while <erasing|cd|re-typing> (<why>)`, and when the captured text never made
  it back that line adds `— the shell's line does NOT hold what you had typed; it was: <text>` (the only way to
  recover it, so quote it in full in a report). A refusal to write at all is `fm: not telling the shell to follow
  — its input line is not one I can describe` or `fm: not typing the path — …`; a navigation arriving
  mid-sequence logs `fm: the follow already in flight now targets <dir>`; a failed read
  `fm: cannot open <dir>: <reason>`; `fm: worker started (result fd N)` — absent means the band was never
  opened, so no thread and no descriptor exist. The band's size is visible in the ordinary `grid resized to CxR
  …` line: with the band on at the default `fm_size = 75`, `R` is roughly a quarter of what it is with the band
  off (grid area 58 rows -> terminal 14 rows), and `layout: … grid CxR per session` is what `tests/cli_test.sh`
  compares between a band-off and a band-on run.
- **The bell.** `bell: session i/N rang — its pane flashes`, or `bell: session i/N rang (bell = off: nothing
  drawn)`, is logged at debug level for every `BEL` the parser sees. The line proves the program rang and says what
  the config asked for: absent means no `0x07` ever arrived (the program did not ring — check the program, not the
  renderer); present with no visible flash means the drawing half, not the parser. A session the layout does not
  draw logs the same line and deliberately flashes nothing — the tab bar's `*` is its mark.
- **The web-address click.** `url: the click at (col,row) holds <address> — waiting out the double-click window`
  (debug) is logged at the release and proves the scan found a run and the gate accepted the click; absent means the
  run rule or a gate declined it. `url: a click asked xdg-open for <address>` (info) is the open itself — present
  means Kai-Term handed it over, and if nothing appeared the desktop's opener is the thing to look at, not the
  terminal. A click that was accepted and then dropped says why: `url: the text under the click changed while the
  click waited — nothing opened` (the line was repainted, the pane scrolled or the session closed) or
  `url: the session the click was in is gone — nothing opened`. Over the ceiling is
  `url: a run of more than 2048 characters is not something to open`, and a failed `fork` is
  `url: could not start xdg-open: <reason>` — the child's own failure to exec is invisible by design (its output is
  `/dev/null`), so the log records what was ASKED for, not what happened.
- `./bin/dbg` — scripted scroll/resize/reflow/zoom stages; each prints `scroll_off/live_off/rows/rcap/used` plus
  one 2-character token per slot (`v` viewport / `s` scrollback, then the row's first cell), and asserts `row[]`
  is a NULL-free, duplicate-free permutation.
- `tests/cli_test.sh` drives the real binary — `main()` is in no linked test, so it is the only honest coverage
  of argv and of what a real launch does with the config it loads. (The config SEARCH order itself is not
  exclusive to it — the `cfgsearch` group in `tests/test_vt.c` covers that headless.) It makes ONE probe launch
  (`-d -e true`) first: if that cannot draw, every window-opening check is skipped with the probe's reason
  printed. `KAI_TERM_TEST_NO_WINDOW=1` skips them without mapping any window — use it unless the window path is
  what is under test.
- Headless harness: link `src/vt.c` + `src/config.c` (+ `src/theme.c`, `mux.c`, `layout.c`) into a throwaway
  program in `/home/kaida/Project/.scratch/` to exercise logic Wayland input events cannot reach. That is how
  the cube, the OSC colours, the sweep and the import corpus were all measured.

## Key Decisions & Pitfalls

- **"The parser knows this key" is never evidence that the key does anything.** `cursor_color =` was defaulted
  (`src/config.c`), parsed and stored in `Config`, and no line of code anywhere read it — `src/render.c` drew the
  cursor from the `CURSOR_COL` macro and `render_init()` was handed only `bg`/`fg`. Every test naming it asserted
  on the PARSED value, and the template parity check compares parsed values field by field, so the whole suite
  was green while the feature was absent. A config key is only implemented at its far end: trace the value from
  `apply_setting()` to the pixel that reads it (the s101 border bug was the same shape — a configured colour
  that reached `glClear` but not the cells). **When adding a key, write the check at the consumer, not at the
  parser.**
- **A check that passes on the buggy code pins nothing.** `ink_in_box()` — "no glyph may sit inside the box the
  picture is drawn into" — passed against the pre-fix `src/fm.c` when written around a small centred 8x8
  preview: that picture happened to land below the metadata. The overlap needs a picture whose box starts at the
  TOP of the picture area, and only the A/B — the new test file against `git show HEAD:src/fm.c` — showed that.
  When a check is written to pin a bug, run it against the code that had the bug and report the failure count
  (s88: 8 failures there, 0 here).
- **A computed constant is a claim — print it before you pin it or write prose about it.** The 256-colour cube's
  level formula `55 + 41 * level` looks plausible, is wrong at level 5 (260), and the shift hides the overflow in
  a way that leaves alpha looking perfect: 91 of 216 entries were near-black and no check had ever asserted on a
  cube value, so the suite was green for the whole life of the project. The probe took one minute
  (`.scratch/s102/cube.c`), found the bug, and stopped me from writing five checks around values I had assumed.
  Same shape as the OSC `rgb:` converter that "rounded" `0xffff` to 256 and painted white as `#010100`.
- **A check that walks a table must use the producer's index layout.** The cube entry for (r,g,b) is
  `16 + 36r + 6g + b`; a loop computing `16 + 36 * L * stride` looks like it is sweeping one channel's levels and
  is reading something else. Derive the stride from the layout (36 / 6 / 1), and confirm the check fails on the
  broken code — a check that never fails is decoration.
- **A self-scan cannot pin a token counter: the test is always part of the corpus.** The `test_render` scanner
  counted its own `CHECK` lines because the probe tokens were named inside the calls that counted them (3 hits
  for a "code" token: declaration, `(void)` cast, CHECK argument). Fix: the test writes a probe file it controls
  and scans that, and every mention of a probe token in its own source is split by string concatenation.
- **In C a hex escape takes every following hex digit — `"\x1bc"` is ONE byte-sized escape (0x1bc), not ESC +
  `c`.** The palette check named "ESC c / RIS does not wipe the palette" had never sent a reset; it passed for
  an unrelated reason. Write `"\x1b" "c"`. The durable gate is the `hex escape` group in `tests/test_vt.c`
  (`src_hexesc_count()` over every C file, skipping comments and quoted mentions), and its non-vacuity is pinned
  by mutation, not by argument.
- **Measure BOTH builds.** `make -j8` builds only `src/`; the test binaries are a separate build, and the
  `\x1bc` warning lived there unnoticed while the "0 warning lines" claim covered only `src`. The same class
  again in s105: two `-Wformat-truncation` warnings in `src/theme.c` that appear ONLY in the asan build, where
  the inlining lets the compiler bound the argument.
- **A guard that reads a field the change has not applied yet is a no-op that looks exactly like a guard.**
  `fm_set_size()` clamped `pv_scroll` to `fm_preview_scroll_max(fm)` on the line where it stored the REQUESTED
  pane size — but `fm_preview_scroll_max()` measures `fm->prev.rows`, the pane that actually EXISTS (the same
  field the painter reads), and the `realloc` that changes it runs 20 lines later. So the clamp measured the
  size the pane used to have and could never change anything; every check written as "scroll, resize, assert it
  is inside the new range" still passed, because the range it compared against was the old one. Moved after the
  allocation. Two traps made it easy to miss: (a) the natural test — shrink the pane, expect the scroll to
  shrink — is the direction where the range GROWS, so it pins nothing in either version; the clamp only bites
  when the pane GROWS. (b) The next check, `fm_preview_scroll(5)` expecting 5, was red for an unrelated reason:
  the previous check had left `pv_scroll` at its maximum. When a clamp/validate call takes a max from a getter,
  ask what state that getter reads and whether the change has landed in it yet (s96).
- **`eglSwapBuffers` blocks, and a blocked swap looks like a dead terminal.** It waits until the driver gets a
  buffer back and only the compositor releases buffers, so a client whose surface is not being rendered stalls
  inside swap: no keyboard, no PTY reads, no redraw (measured `wchan = futex_do_wait`; `eglSwapInterval(0)`
  changes nothing on this NVIDIA driver). The rule is about wayland READ — the earlier "never swap inside a
  listener" wording was BACKWARDS and caused a regression. Answering EVERY configure with a swap is also wrong
  (it spammed `eglSwapBuffers failed: 0x3003` every frame).
- **Geometry lives in `src/layout.c`, not main.c.** main.c is in no test binary, so arithmetic there is
  untestable, and N panes need N origins, which one global offset cannot express. The round trip
  `layout_initial_size()` -> `layout_compute()` -> `layout_grid_size()` is what proves startup height is right —
  it caught a `cell_w`/`cell_h` typo (s62). Same reasoning put `mouse_sgr_button()` in vt.c, the mouse-as-keys
  helpers in vt.c, the theme importer in theme.c, `fm_wheel_focus()` in fm.c, and `confirm_message()` in mux.c.
- **The evdev button order is not protocol order.** `BTN_LEFT` 0x110, `BTN_RIGHT` 0x111, `BTN_MIDDLE` 0x112, but
  SGR numbers them 0 left, 1 middle, 2 right. A switch written in kernel order silently swaps RIGHT and MIDDLE —
  in the bar's middle-click-close AND in every SGR report to vim/htop.
- **A log line that prints the value it was GIVEN can lie.** `tab_switch(idx)` logged `mux: tab idx/n visible`
  while nothing had changed, so the log looked like proof the click worked and the bug survived every reading of
  it. Log what the OWNER of the state REPORTS (`mux_active()`).
- **A log line that counts two different things as one number lies the same way.**
  `graphics: N image(s) drawn, M texture(s) held` began counting the file-manager preview in `N` while `M` still
  counted only the application-picture texture cache, so a preview alone logged `1 image(s) drawn, 0
  texture(s) held`. Count each thing with the counter that owns it (s74).
- **`Term` is not a place for settings.** `ESC c`/RIS calls `term_init()`, which memsets the whole `Term`, so
  process-wide settings are file-scope values with a setter (`kai_term_loglevel`, `g_clear_scrollback`,
  `g_def_fg`/`g_def_bg`, `g_palette`) applied from config in main.c. `bracketed_paste` is deliberately opposite:
  it is application-negotiated, so a reset SHOULD clear it.
- **A Kai-Term-to-Kai-Term clipboard test proves nothing** — it works even if the client never shares its
  selection. Test against a foreign client (`wl-copy` in, `wl-paste` out). With `copy_on_release = 0` (the
  default) a mouse drag does NOT call `set_selection`; only `Ctrl`+`Shift`+`C` does — that is what reads like
  "Kai-Term keeps a private clipboard".
- **A sanitizer target with an incomplete prerequisite list produces confident, wrong results.** `bin/dbg_asan`
  once kept running a stale binary and reported a 312-byte leak the current source did not have. When a
  sanitizer finding contradicts the tests, check the binary's mtime against the sources first.
- **A `(void)x;` line does not mean `x` is unused.** `(void)def1;` in `csi_dispatch` read like a leftover, but
  `def1` is used by `CSI J`, `CSI K` and `CSI 6 n`. Grep for uses, then let the compiler confirm.
- **Never pass an out-parameter and the call that fills it as arguments of the same call.**
  `fm_nav(fm_go_sel(&g_fm, &seq), seq, "open")` compiles clean under the whole `WARN` set and is broken: C
  evaluates arguments in an unspecified order and gcc reads the caller's `seq = 0` BEFORE `fm_go_sel()` writes
  it. Measured live (s74): every listing issued after a navigation carried request id 0, so `fm_install_scan()`
  discarded all of them and the browser kept showing the directory the user had left. Fill the out-param in its
  own statement. Reproduce with `.scratch/s74/argorder.c` (three lines).
- **One int return value cannot mean "not handled" and "handled, do not repeat" at the same time.**
  `send_key()`'s return says only "is this repeatable", and falling off the function is what sends the key to the
  PTY. `if (fm_keys(...)) return 0;` therefore delivered every one-shot file-manager key to the shell too:
  Return on a file typed its path AND a newline (the shell would have run it), BackSpace went to the parent AND
  deleted a character at the prompt. Three states (-1 / 0 / 1) fixed it. Symptom to look for: the action happens
  AND its key appears in the terminal.
- **A refused texture upload is a decision, not a log line.** Every `glTexImage2D` site used to print its error
  and draw with the texture anyway; on the NVIDIA driver a draw that samples a texture with no storage faults
  inside `libnvidia-eglcore` (SIGSEGV at 0x4 in `draw_quads`, 85 core dumps in one suite run). The rule now: the
  glyph atlas is worth exiting over (nothing can be drawn without it — `render_draw()` returns -1 and `main()`
  exits 1), the color atlas is worth downgrading to mono glyphs, a picture is worth skipping. Retrying an upload
  is fine; drawing the result is not.
- **A steady-state condition is not evidence of an event.** `else if (prompt_row_matches(t))
  t->input_pending = 0;` read as "the shell put the prompt back, so the line is empty" — but it was also true
  for a burst that had changed NOTHING, e.g. fish repainting only OSC 7 / OSC 0 / DECSET after Kai-Term had
  re-typed the user's text. The echo window closed over bytes that had not been echoed yet, every later burst
  re-took the prompt snapshot (including the one carrying the echo of Kai-Term's own typing), and a row holding
  `Some text` began answering `term_at_prompt()` as EMPTY — so the next file-manager navigation appended
  `cd '<dir>'` to the command and the shell ran it (user report). Snapshot the condition at the START of the
  burst and act only on the transition. Same class as "a log line that prints its own argument can lie": a
  predicate that is true before and after an event cannot report that event.
- **A burst boundary is not a semantic boundary.** Advancing a "wait for the shell's next prompt" state at the
  end of any pty read looks reasonable and is wrong: fish answers a `cd` with the command echo, then a
  continuation row, then prompt chrome, then the prompt — four separate reads, and the model re-takes the prompt
  snapshot at the end of each. Where the evidence is "the application has FINISHED printing", wait for silence
  (`FMCD_QUIET_MS`, driven from the frame timer) — no further burst is coming to trigger it.
- **An invariant held by accident is invisible until something else changes.** "A cursor moved off the prompt
  column means the line is not empty" was true for months because EVERY key Kai-Term sent set `input_pending`,
  and that is what stopped `prompt_snapshot()` from re-taking the anchor at the moved cursor. Making movement
  keys stop opening the echo window (the s89 BackSpace fix) turned the pinned check red and would otherwise have
  made a cursor sitting INSIDE the prompt text answer "safe to write here". When a change flips a check that has
  been green for ages, find the mechanism that was really holding the property —
  `.scratch/s89/leftprobe{,_old}` prints `at/pending/cx/cy/prompt_cx/prompt_cy` per step and shows it in one run
  — and state it explicitly instead of restoring the accident.
- **A red check can mean the test's setup is unrepresentative, not that the model is wrong.**
  `tests/test_vt.c` looked like "the prompt snapshot breaks after a scroll" and would have invited a change to
  correct snapshot logic. `.scratch/s82/promptprobe.c` replayed the sequence and printed the state at each step:
  the test wrote the whole line with `feed()` (application output), and `term_parse()` re-takes the snapshot at
  the end of every such burst, so the typed text legitimately was `""`. The realistic sequence (`feed("p ")`
  then `type(...)`) passes at the same scroll position. Print the state before editing the logic a check
  exercises.
- **Never write a check around an escape sequence without measuring what THIS parser does with it.** Assumed
  "Home"/"End", got three realities: `CSI H` moves the cursor to another ROW (not a Home key), `ESC O H` prints
  a literal `H` (SS3 is not implemented here), `ESC [ 1 ~` is ignored, and `CSI G` is the one that moves within
  the row. Same class of mistake in the probe itself: `"\\e[H"` in C sends a backslash and an `e`, which read
  for two minutes like a parser bug.
- **Never hand-count a length into `term_parse()` — use `strlen()`.** `.scratch/s97/csiC0.c` wrote
  `term_parse(&T, "\e[?2004", 8)`: that literal is **7** bytes, so the hand-counted 8 fed the NUL terminator as
  an extra byte. The probe then reported a perfectly good case as broken — a CSI sequence split across two pty
  reads came back as "mode not applied, an `h` painted". `strlen()` showed that case was never broken. The check
  invented the bug; the A/B against the real code is what caught it.
- **A harness that counts bytes by hand sends whatever follows the literal in memory.** `input_write(t, p,
  "PS1='%~> '\n", 13)` on an 11-byte literal also wrote the NUL and the next byte of `.rodata` — which happened
  to be the `S` of the following string literal, so the zsh prompt appeared to hold `SSome text` and the
  terminal looked like it was duplicating a keystroke. It was the probe. Always `strlen()` (or `sizeof(x)-1`)
  for a literal, and when a harness reports a character nobody typed, suspect the harness first (s94).
- **A harness that types into the user's real shell history pollutes its own later runs.** The s95 path gate ran
  real fish/bash/zsh in a pty, so its commands landed in `~/.config/fish/fish_history` and `~/.bash_history`.
  Two separate false greens followed: a later run's autosuggestion ghost was a command an EARLIER run had
  typed, and in ghost mode Enter printed the expected output because fish's own suggestion completed the
  command. Run shells with their history redirected (`fish --private`, `HISTFILE=/tmp/...`) and make the needle
  something no history can already contain (a pid in the file name). When a check passes on the code you expect
  it to fail, suspect the check before you believe the result.
- **A probe that hosts a shell must answer that shell's queries, or it is measuring a shell that never
  started.** A section of `.scratch/s97/hprobe2` reported "fish: bracketed paste NEVER within 3 s".
  `.scratch/s97/fishdiag` shows why that was the probe: fish's first 83 bytes are terminal QUERIES (`CSI ? u`,
  `CSI > 0 q`, `OSC 11 ; ?`, `DCS + q …`, `CSI 0 c`) and fish paints nothing until they are answered or time
  out. `.scratch/s97/fishtime` drains the response queue the way `src/main.c` does and measures 200 ms. Any
  headless measurement of shell STARTUP behaviour that never writes back to the pty is measuring a stalled
  shell, not a slow one. The same trap caught the s118 confirm-quit probe — see `s117/fgprobe.c` vs
  `s117/fgprobe2.c`.
- **When a bug is in what someone ELSE hands you, measure that someone before theorising about your own code.**
  The stray-`h` report got two confident wrong explanations (a leaked parser byte, then an escape sequence in
  the pasted text) before one command pair settled it: `wl-paste -l` and `wl-paste --type <name> | od -c`. Both
  wrong theories were internally consistent and neither had been tested against the actual clipboard. The user's
  own clues — "the source matters", "ctrl+v works but ctrl+shift+v does not" — pointed straight at it; a report
  that distinguishes two paths is telling you which path to instrument. Related: the s97 C0-inside-CSI fix was
  a real bug found while chasing this one, so "I found and fixed something" is not evidence that the reported
  bug is explained. Confirm the reported symptom is reproduced, then confirm it is gone.
- **A feature A/B cannot pin a negative check, and the failure count is only meaningful if you say which
  checks it counts.** Stubbing `--import-theme` out (nothing recognised, nothing parsed, nothing written) made
  44 of the 1127 checks fail — but 17 of the import group's 60 passed, because "a missing file is refused", "a
  file with no colour is refused" and "there is no destination without HOME" are all satisfied by a tree that
  refuses everything. Same shape in step 3 (a stub answering "one cell" satisfies "a click on a blank selects
  one cell") and in step 3b (a stub selecting nothing satisfies "nothing is selected before a gesture"). That is
  not a defect in those checks: they are pinned by MUTATION (make the refusal accept the file) and by the
  positive checks beside them, not by an absence A/B. Report both numbers and name the kind, or the next session
  reads "44 failures" as "all of them matter".
- **An object-file A/B cannot exercise the checks that read SOURCE TEXT.** `cfg_names_of()`,
  `readme_table_names_of()` and `src_word_count()` all open the working tree's files, so the settings-name count
  and the `render_cursor`-appears-4× pins passed in BOTH trees of the s112 A/B. Verify those the direct way
  instead: the count check went genuinely RED between adding the parser branches and updating the documents, and
  `grep -c render_cursor` on HEAD's `src/render.c` is 0. Also note the A/B has to use the CURRENT header or the
  new checks would not compile — which is the honest shape of it, and what makes the vocabulary available to a
  tree that does not implement it.
- **Safety invariants have limited A/B value by design.** All 448 `render_cursor()` invariants and its BLOCK
  rules stayed green against the pre-step-2 rule, because they pin "never both a block and a quad", not "a bar
  is drawn". The 12 failures were precisely the bar/underline claims. Say which kind a count covers.
- **A silent truncation is a decision someone already made for you.** `snprintf(v->name, sizeof v->name, "%s",
  val)` is memory-safe and cut a 236-byte theme name into a 128-byte field without a word — and the compiler
  only said so in the asan build, where inlining let it bound `val`. Writing the cap as one function that copies
  `sizeof field - 1` and logs the cut removes the warning AND makes the behaviour something the tests can assert
  on (the CLI group greps the log line; the C check asserts the exact 127-byte stem, with an alternating `ab`
  name so an off-by-one changes the last character too). Same shape as every other "the value was quietly
  changed" bug here: make the change visible at the place it happens.
- **A harness that mallocs must free, or LeakSanitizer reports YOUR test as the leak.** The option-pinning block
  read 9 names out of `usage()` and 9 out of README and never freed them: 134 bytes in 18 allocations that
  looked like a shipped-code leak and would have hidden a real one. The suite already had the pattern
  (`free((char *)set_names[i])`); the new block just did not copy it.
- **A failed build leaves the previous binary in place, and it will happily pass.** `make -j8` errored on a C
  mistake, the very next command ran `./bin/test_vt` and it printed `859 checks, 0 failures` — for code that had
  never been rebuilt. Check the build exit status (and that the check count actually moved) before believing any
  green run.
- **A test that maps a window is a change to the user's desktop.** `tests/cli_test.sh` ran the real binary 15
  times per `make test`, and one session looped it 25 more times to sample a failure — that is what filled the
  user's screen and `coredumpctl`. One probe launch decides whether the window checks run; the rest are skipped
  with the reason printed, and `KAI_TERM_TEST_NO_WINDOW=1` skips them all. Never loop launches to count
  failures: diagnose the first one (and see ENVIRONMENT.md "Launching real windows in a test").
- **Never run the live key-injection or window-mapping harnesses while the user is at the machine, and never
  assume a red screen came from your own test.** The user was disturbed mid-session. Measured after the fact
  rather than guessed: no `bin/kai-term` of mine was alive, no `.scratch/inject` process, no new core dump, and
  the CLI suite had mapped 0 windows — what actually happened was `noctalia` (the lock screen) SIGSEGVing and
  niri restarting the session, a recurring host fault. Default to headless coverage (`.scratch/s86/follow2.c`
  drives the real machine over the real pty with no window at all) and hand the in-window check to the user.
- **`pkill -f <name>` matches the shell that runs it.** `pkill -f clipowner` killed the whole command line,
  output and all. Use `pkill -x <name>`, or put the sequence in a script file.
- **A background job spawned with `&` makes this shell tool swallow the call's entire output** — even the
  `echo`s that follow it. Split "start the daemon" and "query it" into separate calls, or run the whole scenario
  from a script file.
- **`cat > file` is fully buffered when stdout is a file**, so killing the `cat` that captures a paste discards
  the capture and the run reports 0 bytes. Close its stdin (Ctrl+D) and let it exit normally.
- **A missing-protocol grep proves the code is absent, not that the user is missing anything.** The s102 survey
  grepped every candidate feature first (all 0 hits) and two of the proposals were still wrong, because the
  user's own setup did not ask for them (scaling at 1, and OSC 52 vs the local clipboard they already use).
  Check what their setup actually asks for before ranking a feature.
- **A sentence in README about a log line is a claim about the code, and it goes false the same way a feature claim
  does.** s131 found README promising that `-d` says "when the click was refused, why"; `grep -n klog src/main.c |
  grep url:` returns six lines and none of them is a refusal. There is a good reason none exists — a click on a cell
  with no address is what every ordinary click in a terminal is, so logging it would fill the log — but the sentence
  had been written as if the diagnostic did. Grep for the string before documenting it; when the honest answer is
  "nothing is logged here", replace the promise with the evidence that DOES answer the question (for the URL click:
  whether the `url: the click at (col,row) holds …` line appeared at all).
