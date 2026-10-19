# Kai-Term — Known Issues

Current problems and current behaviour only. The history of what was broken and fixed is NOT kept here: the 54
root-cause entries and the 4 unproven reports cleared 2026-10-17 are in `git show d5afe4a:ISSUES.md`, and the full
investigations behind the `Fixed` entries below (the probes, the wrong theories, the measurements that settled them)
are AGENT.md's Progress Journal era entries. The rules those bugs produced are in AGENT.md "Architecture &
Invariants" — read those, not the history.

## Open

**No open bug.** The last two found were both colour plumbing: `cursor_color =` being dead config, and the 256-colour
cube's top level overflowing. Both fixed and A/B'd in s102 (the first two entries under "Fixed"). The shape worth
remembering: a config key is not implemented at the parser, it is implemented where the value is used — and a
computed colour must be measured, not assumed.

The frame around the grid — the `padding` margin and the blank cells reading two different background colours — was
fixed, A/B'd and confirmed in the user's own window in s101 (see "Fixed").

The four items this file was once holding for the user's eye were answered 2026-10-22 ("1. yes / 2. good /
3. works good"): the two-file config split is theirs (they asked for it), the background change their own
`background =` line now produces is accepted (they have since edited that file while testing — it now reads
`background = 1c1c22`, bare hex, and `opacity = 1`), and the s98 clipboard-source paste fix, the file manager's
preview-pane key scrolling and the 75 % band's look all work in their window.

Re-measured 2026-10-19 (s103, from `make clean`): `make -j8` exit 0 with **0 warnings** and the TEST build
(`make bin/test_vt bin/dbg bin/test_fm bin/test_render`) exit 0 with **0 warnings**; `./bin/test_vt` **977 checks,
0 failures**; `./bin/dbg` **MODEL OK (0 problems)**; `./bin/test_fm` **296 checks, 0 failures**; `./bin/test_render`
**25 checks, 0 failures**; `KAI_TERM_TEST_NO_WINDOW=1 make test` exit 0 (`CLI: 67 checks, 0 failures, 13 skipped` +
those four); `make asan` exit 0 with the same counts and no sanitizer report. The windowed group of
`tests/cli_test.sh` (**117 checks / 0 failures / 0 skipped**, last measured that way in s99) was skipped again —
the user at the machine. That leaves only its own half uncovered: `main()`'s argv handling (`-c/--config`,
`-b/--binds`, combined flags, `-d` beating `log =`, `-w`, `-e`) and that a real launch applies the multiplexing /
display / file-manager keys. The config search order and the settings-vs-binding file rules are covered headless in
`tests/test_vt.c` (13 `cfgsearch` checks + the `config split` group), so they are not a gap. No `TODO`/`FIXME`/`XXX`
and no `TEMPORARY`/`PROBE` marker anywhere in `src/`, `include/`, `tests/`.

Everything below "Fixed" is current behaviour, not a bug. What is implemented and how to use it is README's job;
what is verified and what is not is AGENT.md's "Current Status". This file carries only: what is broken, what was
broken and is now pinned, and the limits a user or a future agent must not "fix" by accident.

**To file a problem:** symptom, exact reproduction, and what `./bin/kai-term -d` logs (the log lines worth quoting
are listed under "Debugging this project" in AGENT.md). Not reproducible -> not filed; it is not a bug until it
reproduces.

## Fixed

One entry per bug: what the user saw, the cause in one line, what fixed it, and what now keeps it from coming back.
The full investigation of each — the probes, the wrong theories, the measurements that settled them — is AGENT.md's
Progress Journal (era entries `s81`, `s82-s86`, `s88`, `s89`, `s90-s92`, `s96`, `s97`, `s98`, `s99`, `s101`, `s102`);
the rule each one produced is under "Architecture & Invariants" there. Check counts are what the suite reported at
the time, not current totals.

**A test check that fed the wrong bytes and stayed green — the `\x1bc` hex-escape trap (2026-10-19, s103).** No user
report and no product symptom: the defect was in the suite, and it matters because it made a claim about behaviour
unverified. `tests/test_vt.c` wrote `feed("\x1bc")`; in C a hex escape consumes every following hex digit, so that is
ONE escape (0x1bc, out of range for `char`) and the parser was handed a single 0xbc byte — a printable character —
instead of ESC followed by `c`. The check named "ESC c / RIS rebuilds the pen but does NOT wipe the palette" therefore
never sent a reset, and it passed on the buggy pre-refactor code for an unrelated reason, so the A/B could not see it
either. Cause in one line: the warning that names it (`hex escape sequence out of range`) was never measured, because
the "0 warnings" claim covered `make -j8` (which builds only `src/`) and never the test build. Fix: write the two
escapes separately (`"\x1b" "c"`), and measure the warnings of BOTH builds. Kept out by a `hex escape` group in
`tests/test_vt.c` that scans every C file in the project for an `\x1b` followed by a hex digit (comments and quoted
mentions skipped, non-vacuity pinned with a probe file the test writes). Proven to bite by mutation: restoring the
old form fails that check, and making RIS wipe the palette fails the palette check — `.scratch/s103/ab_hex/` and
`.scratch/s103/ab_ris/`.

**The 256-colour cube's brightest level wrapped to near-black — 91 of the 216 entries (2026-10-22, s102).** No user
report: found by measuring the palette while writing checks for the theme work, because a check written around an
assumed value would have been red on arrival. `SGR 38;5;196` (bright red in every chart) painted `#040000`, `38;5;226`
(bright yellow) painted `#050400`, and `38;5;231` (the cube's white) painted `#050504` — so any application using the
top cube level (vim/neovim colorschemes, htop, bat, delta, pygments) drew near-black instead. Cause in one line: the
cube's level formula was `55 + 41 * level`, which is **260** at level 5, and `260 << 16` leaves `0x04` in the channel;
alpha stayed `0xff` (the carry landed on a bit already set), so no alpha check could ever have seen it. Fix: the six
levels are a table `{ 0, 95, 135, 175, 215, 255 }` — the set almost every terminal and almost every 256-colour chart
uses — and a table cannot overflow. Kept out by 5 checks: the entries applications use (196, 46, 21, 226, 231, 233)
and, for the class rather than one instance, each channel's six levels must strictly increase and end at 255 (the wrap
made them DROP). A/B against the pre-fix `src/vt.c`: **all 5 fail there**, 0 here. Probe: `.scratch/s102/cube.c`
prints the level, the raw channel value and the resulting ARGB for every entry with a level-5 channel.

**`cursor_color =` was dead config — parsed, stored, tested, and read by nothing (2026-10-22, s102).** Symptom, derived
from the code and never seen in a window before the fix: `cursor_color = ff0000` in `~/.config/kai-term/kai-term.conf`
changed nothing, the cursor stayed `#d8d8d8`. Cause in one line: the renderer took the colour from the compile-time
`CURSOR_COL` macro and `render_init()` was handed only `bg` and `fg`, so the value stopped in `Config`. The suite could
not see it because every check that named the key asserted on the PARSED value, and the template parity check compares
parsed values field by field. Fix: `Render.cursor_color`, passed by `src/main.c` from `conf.cursor_color`, and the six
chrome-colour expressions of a frame moved out of `render_draw()` into one pure `render_chrome(const Render *,
Chrome *)` — the seam a colour check can stand on. Kept out by checks, not by eye: `tests/test_render.c` (new binary,
15 checks) asserts the configured value is what an active pane's cursor is drawn with, that an inactive session's
cursor is that value mixed with the configured background, that the built-in default still yields `#d8d8d8`, and that
`src/render.c` contains zero `CURSOR_COL` tokens (the scan also counts `SEL_BG`, so the zero means something). A/B:
against the pre-fix renderer — same `Chrome` function, still reading the macro, with `main()` not passing the argument
— **5 of those 15 checks fail**; on the fixed tree, 0. The extraction itself was proven behaviour-free first:
`.scratch/s102/chrome_probe.c` recomputes the six expressions exactly as they were written inside `render_draw()` and
compares all eight `Chrome` fields over 25 bg×fg pairs — 200 comparisons, 0 differ. Not covered by any headless test:
the `render_init()` call itself, which lives in `main()` (in no linked test) — pinned there by a source scan instead.
**The look has not been confirmed by the user yet**: the value now reaches the renderer, but whether the configured
colour is what they want on screen is theirs to judge.

**A frame around the grid: the `padding` margin and the blank cells were two different background colours
(2026-10-22, s101).** The user's words: "Some border seems to have appeared recently in the recent change, It's
probably the padding to prevent cutting glyph corner, But would it be possible for it to be the exact same color as
the background ? they should use the same config line to set their background color." Their hypothesis and their
requested design were both right. `glClear` paints the `Config.padding` margin and the whole-cell remainder with
`Config.bg`, while `src/vt.c` filled every blank, erased, scrolled-in and reflowed cell with the compile-time
`DEF_BG` — with `background = 1c1c22` in their file that is `ff1c1c22` around `ff101014`, a frame. It "appeared
recently" because the built-in default for both is the same number: the s99 `#` fix made their `background =` line
take effect for the first time and revealed this second, older defect (niri's focus ring was ruled out — they have
`opacity = 1`). Fixed by making ONE runtime value own the default ink and paper: `term_set_default_colors()` /
`term_default_fg()` / `term_default_bg()` in `src/vt.c`, read by all 17 former `DEF_FG`/`DEF_BG` sites — row
blanking, `row_resize`, reflow's fresh rows, `grid_blank_slot` in the resize paths, `pen_default()` (`term_init`
and `ESC c`), `SGR 39/49`, and the `cell_pristine()` sentinel, deliberately included: the colour a blank cell is
painted with and the value that means "never written" must be one variable, or reflow's `row_used()` and the
no-band-over-nothing rule drift. `main.c` sets it once after the config readers, before the first Term, and the
three patch-overs that existed only because of the split (`pen.* = conf.*` after `mux_open`, and
`paint_blank_cells()` with its startup / `tab_open` / `zoom_apply` calls) are gone. 14 new checks; A/B'd in both
directions — 7 of them fail against the reported state (`HEAD`'s `vt.c` plus the new API) and 2 fail against a
deliberately divergent variant where the paint sites read the runtime value but `cell_pristine()` still compares
`DEF_BG`. **Confirmed in the user's window 2026-10-22** ("It's fixed, you can push").

**A `background = #1c1c22` line set nothing at all — the config reader cut the value at the `#` (2026-10-22, s99).**
The comment rule was "whitespace before a `#` is enough", which is exactly the shape of a colour value, so the line
became `background =` and an empty value is legal, so nothing was even logged. README documents `#RRGGBB` and the
shipped template writes it, so every colour line in a documented config file was silently ignored. Measured against
the user's own file: old `ff101014` (the built-in default), new `ff1c1c22` (what they wrote). A `#` now starts a
comment only when whitespace alone precedes it, or whitespace precedes AND follows it — `working_dir =
/home/me/my#project` keeps its value, `value # note` is still stripped. 8 checks in the `config hash` group.

**The config surface is two files, and the templates are pinned to the parser (2026-10-22, s99).** Not a bug — a
feature the user asked for. `kai-term.conf` holds the 29 settings, `kai-term.bind` the 24 key bindings, each
searched independently in `$XDG_CONFIG_HOME/kai-term` -> `~/.config/kai-term` -> `/etc/kai-term` (first regular file
wins), `-b/--binds` naming the binding file the way `-c` names the settings one. The split is by NAME, so an older
`kai-term.conf` with bindings in it keeps working and a setting written in `kai-term.bind` is refused by name rather
than treated as unknown. What was missing was a check that the templates STAY complete: comparing a loaded template
field-by-field with `config_default()` cannot see a deleted line, so deleting `padding = 8` reported **0 failures**
before the name-coverage check existed and 1 after; it also fails for an unknown name in a template and for a name
added to the parser without one.

**Pasting from Firefox put `h` on the bash prompt and `h^@t^@t^@p^@…` on the zsh prompt (2026-10-21, s98).**
Reported by the user, who localised it better than I had: the source matters, the paste key matters (Ctrl+V works,
Ctrl+Shift+V does not), the source is Firefox. All three clues were the diagnosis. `offer_offer()` overwrote the
recorded mime type for every advertised `text/*` name, so the LAST one won — Firefox advertises
`text/x-moz-url-priv` last and serves it as **UTF-16LE** — and those 42 bytes went verbatim into the bracketed
paste. bash's bracketed reader stops at the first NUL (one `h`); zsh inserts every byte and paints each NUL as
`^@`. Two fixes because two things were wrong: `clipboard_mime_rank()` (ask for the best type, never the last) and
`paste_repair()` (a NUL must never reach the pty; dense one-sided NULs or a BOM mean UTF-16, anything else is
NUL-stripped). A/B on the user's own clipboard: 42 UTF-16 bytes -> 6 clean UTF-8. On a source offering only UTF-16:
33 in -> 17 out, logging `paste: 33 clipboard bytes contained NUL, repaired to 17`. 19 checks. **Confirmed in the
user's window 2026-10-22.** The reason plain Ctrl+V worked: Kai-Term does not handle it, the shell's own paste path
drops NUL bytes, and Kai-Term's did not — same clipboard, one path sanitised.

**A stray `h` nobody typed, and a mode silently not applied: a C0 byte inside a control sequence ENDED it
(2026-10-21, s97).** `PS_CSI`'s handling of a C0 byte was `state = PS_GROUND; i--;`. That does two damages at once:
the mode being set is never applied, and the rest of the sequence is painted as text — and `h` is the final byte of
every mode-ENABLING sequence (`?2004h`, `?1049h`, `?25h`, `?1h`), which is why the leaked character was an `h`.
Measured: `ESC [ ? 2004 <TAB> h` left `bracketed_paste` at 0 and painted an `h`; `?1049` never entered the alternate
screen; `?1` left DECCKM off. Fix: one file-local `exec_c0()` shared by `PS_GROUND` and `PS_CSI` — the rule existed
twice and the two copies disagreed. ESC stays the exception (it cancels the sequence). 14 checks; A/B against the
old `src/vt.c`: **9 failures there, 0 here**. Note: this was a real bug found while chasing the user's report, but it
was NOT what they were hitting — the s98 entry above is. nano's `Flawed paste`, their other clue, is nano refusing a
paste whose text contains an escape sequence, not a Kai-Term fault — see "Known limitations" — "Paste".

**The mouse wheel did nothing inside `nano` / `less` / `man`, and was dead in every program that DID ask for the
mouse (2026-10-21, s96).** Asked for by the user ("When opening a file with nano or similar program it would be nice
to be able to scroll with the mouse … In kitty I can't click, but I can scroll"). Two causes, both measured first on
real nano 9.2 and less: such a program owns the alternate screen, whose grid has `scrollback_max = 0`, so the wheel
landed in `scrollback_nav()` and was thrown away; and the wheel report to a program that HAD grabbed the mouse used
button codes **4/5**, which ncurses reads as button 1 — on `nano --mouse`, `ESC[<5;…M` does nothing while
`ESC[<65;…M` scrolls (same in X10: `32+65` scrolls, `32+4` does not), so the wheel was broken in vim, htop, tmux and
`nano --mouse`. Fix: `term_wants_key_mouse()` / `term_wheel_arrow_keys()` / `term_click_arrow_keys()` in `src/vt.c`
(not `main.c`, which is in no test binary), plus `mouse_wheel_button()` for the 64/65 codes. A click becomes the
arrow-key walk to the clicked cell — **keystroke synthesis, not mouse support**, because a program with no mouse
protocol reads a report as typed text: one `ESC[<64;20;5M` put `64;20;5M` into the user's file and set `Modified`.
`wheel_sends_keys` / `click_moves_cursor` (default 1) switch each half off. 31 checks; user-confirmed in their window.

**The arrow keys did nothing at all in `less`, `man` and `htop` (2026-10-21, s96).** DECCKM (application cursor keys,
`ESC[?1h`) was not implemented — `send_key()` always sent the CSI form, while an ncurses program asks for the SS3
form. Measured, and the programs differ: nano answers either, **less answers ONLY the SS3 one**, so its arrows were
inert. `Term.app_cursor` is now set/cleared by `?1h`/`?1l`, saved and restored across the alternate screen, and every
cursor key — pressed or synthesized — goes through one `term_arrow_seq()`. Probe: 30 SS3 presses moved less from top
line 1 to 31; the CSI form moved it not at all.

**The wheel over the file manager's preview pane did nothing, and a resized pane could leave the preview scrolled
into blank rows (2026-10-21, s96).** `fm_wheel()` claimed the gesture over the preview, set focus and returned
without acting. Added `FileMgr.pv_scroll` with `fm_preview_scroll()` / `fm_preview_scroll_max()`, obeyed by the
painter for a text file and a directory listing (an image or metadata-only preview has no body, so it cannot move).
**A second defect, found by the new checks and in the model, not the test:** the clamp sat at the top of
`fm_set_size()`, but `fm_preview_scroll_max()` reads `fm->prev.rows` — the pane that actually EXISTS — and the
realloc had not run yet, so it reported the OLD height and the clamp could never change anything. Moved after the
allocation. 27 checks; the clamp check A/B'd against the old placement: 1 failure there, 0 here.

**A shell started at the prompt fell back to the typed `cd`, and the prompt kept showing the OLD directory
(2026-10-20, s92).** Reported by the user: "changing shell in the prompt make it go in fallback mode instead of
using the right shell integration", and "the prompt still show /home/kaida but it's only visual … is there a way to
refresh the prompt". (1) `Tab.int_dead` was one-way and a shell the user started never loaded the script at all — the
scripts now wrap a bare `fish`/`bash`/`zsh` (never over a name the user defined, never with an argument present) and
re-announce after every command, and `Term.shell_int_gen` + `Tab.int_gen` make an announcement an EVENT that revives
a dead integration. (2) fish and zsh repaint from inside the handler (`commandline -f repaint`, `zle reset-prompt`);
**bash cannot and nothing is pretended** — readline expands the prompt once per `readline()` call, `C-l` leaves it
stale and printing `${PS1@P}` over the line garbles it, both measured. 14 checks; the user confirmed all three shells
in their window.

**A BackSpace on an empty prompt disabled the shell follow for the rest of the session (2026-10-20, s89).** The log
said `fm: dropped the follow while erasing (the wait ran out)`. Measured: bash and sh answer BackSpace at an empty
prompt with a bell only and fish not at all, so no burst ever re-proved the line empty and the echo window stayed
shut until Enter. Now a payload that cannot put a character on the line (cursor movement, deletion, Ctrl+L, a bare
Escape) does not open the window when the line is already provably the bare prompt. Fixing that exposed an older hole
held only by accident: the remembered prompt's CURSOR position means "where the cursor sits when the line is EMPTY",
and a movement key used to leave `input_pending` set, which is what stopped the snapshot being re-taken at the moved
cursor. 20 checks, and an A/B of the real follow machine over real fish. User-confirmed.

**The file manager's metadata was painted across its own image preview (2026-10-19, s88).** Reported by the user:
"the metadata sometimes get written on the image preview". In the model, not the shader: the picture box started at a
hardcoded row 3 while `paint_preview_meta()` wrote every metadata line from row 2 — an image preview carries eight —
and glyphs are emitted in the batch AFTER the picture. It looked intermittent only because a small picture is centred
low enough to miss the text. Now one place owns the pane's rows (`preview_head_rows()` + `preview_meta_rows()`),
called by both the painter and `fm_image_fit()`. 12 checks that scan every cell inside the reported box for a painted
glyph, plus A/B against the previous `src/fm.c`: **8 failures there**, which is also what proved the first version of
the check passed by luck.

**The band is a share of the window, not a row count (2026-10-19, s88).** Requested: "Would it be possible for the
File manager part to occupy more space? like 75% per default and make it configurable" — and on the shape of it,
"don't keep fm_rows we don't need it". `fm_rows` is gone (key, fields, `FM_ROWS_MAX`, docs, checks); `fm_size`
(percent of the text area, default 75, clamped 10-95 at parse time and again in `layout.c`) replaces it, so the band
keeps its proportion through a resize or a zoom. `layout_initial_size()` solves the inverse so startup still gets the
configured `cols x rows` for the terminal pane. 7 checks + one `cli_test.sh` check comparing the real binary's
`grid CxR per session` with the band off and with `fm_size = 50`. Seen and accepted by the user 2026-10-22.

**A second file-manager navigation appended `cd <dir>` to the prompt instead of erasing it first (2026-10-19,
s82-s86).** Reported by the user: browse once and the line is cleared, the `cd` runs and their text comes back; browse
again and the prompt becomes `Some text cd '/'`, which the shell then runs. Cause, measured against real fish through
the real pty: after Kai-Term re-typed the text, the shell's next burst repainted only prompt chrome and touched the
input row not at all, so that row still equalled the remembered prompt — the end-of-burst rule read that **steady
state** as proof the application had put the prompt back, and closed the echo window over bytes not yet echoed. Now
the window closes only when the row CHANGED into the remembered prompt between the start and the end of one burst, and
the `cd` step additionally waits for the session to go quiet (150 ms) — a burst boundary is not a prompt. 11 checks,
a headless replay over real fish, and the user's confirmation in their window.

**Kai-Term segfaulted inside the graphics driver when the GPU refused a texture (2026-10-19, s81).** `atlas
glTexImage2D failed: 0x505` followed by SIGSEGV at address 0x4 inside `libnvidia-eglcore`, reached from
`draw_quads()` — 85 core dumps from one run of `tests/cli_test.sh` on a machine whose video memory was already full.
Every upload site logged the failure and then drew with a texture that had no texels. Now: the glyph atlas is retried
once after dropping every application-picture texture, and if still refused `render_draw()` returns -1 and the run
ends with exit 1; a refused color-emoji atlas falls back to monochrome glyphs and retries about once a second; a
refused picture or preview texture skips that picture. **A refused upload is a decision, not a log line.** Forced and
verified by A/B (`.scratch/s81/`); the natural trigger needs a GPU with no memory left, so it has not been re-run for
real since.

## Known limitations

Not bugs — current behaviour, each re-checked against the source 2026-10-17. Do not "fix" by accident.

**Grid / scrollback**

- **Scrollback capacity is fixed at startup.** `rcap = rows + scrollback` is allocated once, so every row
  the viewport gains by growing is a row of history lost: the bound is
  `sb_limit() = min(scrollback_max, rcap - rows)`, not `scrollback_max`. Shrinking does not restore it.
- **Narrowing can evict the OLDEST history lines.** Reflow re-wraps every retained line; if the result
  needs more rows than `rcap`, the oldest go, like ordinary overflow. Newest always survive; widening
  cannot bring evicted lines back. `-d` logs `reflow: 55 -> 58 rows at 26 cols (3 evicted)`.
- **Reflow is normal-screen only.** The alternate screen (`?1049h`: vim, htop, less) is truncated/grown in
  place by `grid_resize_plain()` — a full-screen app repaints after SIGWINCH and re-wrapping would
  scramble it. The parked main screen IS reflowed.
- **Whole-cell remainder stays at bottom/right.** `cols = w / cell_w` is floor division, so `w % cell_w`
  and `h % cell_h` px are left over, painted with the configured `background` like the `padding` margin
  (bottom margin = `padding` + vertical remainder). The grid is anchored top-left so `ptr / cell` names
  the right cell — anchoring elsewhere breaks every click, drag and SGR report.
- **Minimum window 200x100 px** (`xdg_toplevel_set_min_size`). A compositor may still configure smaller
  and Kai-Term handles it (110 px high -> 6 rows), but interactive resizing stops there.

**Selection / clipboard**

- **Selection is viewport-relative** — scrolling into history, resizing or zooming clears it
  (`term_select_clear`).
- **Selection highlights whole cells only** (by design): a range always starts/ends on a cell edge, so a
  partial glyph is impossible. The band skips cells never written (`ch == 0`), so a drag past end-of-line
  stops at the last glyph; real spaces and `ATTR_WIDE` continuation cells stay highlighted. Copied text is
  unaffected (blank cells copy as spaces, trailing spaces per line trimmed).
- **A mouse drag alone does not copy.** `copy_on_release` defaults to 0, so releasing only leaves the
  selection visible — pasting elsewhere gives whatever was there before. `Ctrl+Shift+C` (or
  `copy_on_release = 1`) is what puts it on the clipboard. Usual cause of a "Kai-Term has its own
  clipboard" report: the copy was never offered. The clipboard is the ordinary Wayland selection
  (`wl_data_device`), shared with every client — verified in both directions.
- **Only `text/plain` is offered on copy** (one mime type requested on paste), and
  `wl_data_offer_accept()` is never called before `wl_data_offer_receive()`. niri tolerates both
  (`wl-copy` text reaches the PTY byte-for-byte), but a compositor that matches mime types strictly could
  refuse. First things to check if pasting from other apps breaks. On the PASTE side the type is now chosen by
  preference (`clipboard_mime_rank()`), not by "whichever the source advertised last" — see "Paste" below.
- **A paste over `PASTE_BUF_MAX` (8 MiB) is sent unwrapped** — no `ESC[200~`/`ESC[201~` — with a log
  warning, because the whole block must be buffered before delimiters can surround it.
- **Bracketed paste is per application:** the delimiters are sent only when the app has DEC mode 2004 on.
  A shell/readline with bracketed paste off correctly keeps getting raw line-by-line pastes.

**Paste**

- **What a clipboard source advertises, and serves under each name, is entirely the source's business.** Kai-Term
  asks for the best text type it is offered (`clipboard_mime_rank()`: `text/plain;charset=utf-8` > `UTF8_STRING` >
  `text/plain` > other `text/plain` variants > other `text/*`) and repairs what comes back (`paste_repair()`: a NUL
  byte never reaches the pty; dense one-sided NULs or a BOM mean UTF-16, which is decoded to UTF-8). Firefox is the
  measured case — it advertises `text/x-moz-url-priv` last and serves it as UTF-16LE. Two things a source can do
  still get through:
  - **UTF-16 with no BOM and no NULs at all** (e.g. UTF-16 of CJK text) is undetectable and is pasted as mojibake.
    There is no reliable way to tell it from UTF-8 without a BOM; a source that labels it correctly is fine.
  - **A block whose NULs are sparse** (not at least half the bytes) is treated as text with stray NULs and only
    NUL-stripped, never decoded — guessing UTF-16 on ambiguous input would turn good text into garbage.
  If a paste ever arrives wrong, the first thing to measure is the SOURCE, not the parser: `wl-paste -l` for the
  types, and `wl-paste --type <one>` piped to `od -c` for the bytes it actually serves under each name.
- **A paste is wrapped in `ESC[200~`/`ESC[201~` only while the application holds DEC mode 2004, and at startup
  there is a window in which it does not.** Measured from the pty spawn (`.scratch/s97/fishtime`, and section B
  of `.scratch/s97/hprobe2`): bash and zsh turn the mode on ~20 ms after the shell starts, fish ~200 ms — and
  fish only once the terminal has answered its startup queries (a probe that leaves those queries unanswered
  watches fish stall and reports "never", which is a property of the probe, not of fish). A paste flushed before
  that moment goes out RAW. The same unwrapped path is taken for a paste over `PASTE_BUF_MAX` (8 MiB) — see
  "Selection / clipboard" above.
- **Raw-pasting text that itself contains an escape sequence puts characters on the line that nobody typed.**
  Measured (`.scratch/s97/hprobe`, a real shell in a pty): pasting `AA\e[?2004hBB` into bash WITH the delimiters
  leaves the inert literal `AA^[[?2004hBB` on the line; the same bytes WITHOUT them leave `AA2004hBB`, and Enter
  runs `AA2004hBB` (`bash: AA2004hBB: command not found`). The sequence is interpreted by the SHELL (readline), not
  by Kai-Term's parser: Kai-Term wrote the bytes to the pty and the shell echoed what it made of them. Text copied
  out of a terminal is the usual source of such clipboard content. Kai-Term cannot know that the clipboard holds
  escape bytes; the delimiters are the protection, and asking for them is the application's decision. **This was
  measured in s97 as a candidate explanation of the user's stray `h` and it is NOT what they were hitting** — the
  real cause was the clipboard mime type (see the s98 entry under "Fixed"). It is kept here as the real behaviour
  it is, not as their diagnosis.
- **nano answers a paste whose text contains an escape sequence with its own `[ Flawed paste ]`** and inserts
  nothing after the escape (measured, nano 9.2, `.scratch/s97/hprobe`). Kai-Term's write pattern is not the cause:
  nano accepts the same block when `paste_flush()` sends it as three separate writes (`ESC[200~`, the text,
  `ESC[201~`) and when chunked at 4096 bytes like the real code does (`.scratch/s97/flawed`, cases A/B/C/G2 clean).
- **Plain `Ctrl`+`V` is not Kai-Term's** — only `Ctrl`+`Shift`+`V` is. Plain Ctrl+V goes to the shell, which does its
  own clipboard read (`fish_clipboard_paste` -> `wl-paste`; bash/zsh via command substitution), and that path drops
  NUL bytes. The two paths can therefore give different results from the same clipboard; that difference is a
  diagnostic, not a bug (measured, s98 — it is how the user localised this to Kai-Term's own paste path).

**Sessions / layout**

- **The tab bar costs one text row, and the grid wins if both do not fit.** With `tab_bar = always`, or
  from the second session on under the default `auto`, one cell row at the top is chrome. If the window is
  too small for that row + its gap + one text row, the bar is dropped for that frame rather than shrinking
  the grid to nothing.
- **Bar labels are cut to their item's width, no ellipsis.** Items have equal width (grid width / session
  count), so with many sessions a label can be a few characters. The cut is at a UTF-8 codepoint edge,
  never inside a multi-byte sequence; the number and the `*` unread mark are part of the label, so a long
  OSC title is what gets truncated.
- **Only `split_max` sessions are drawn in side-by-side view.** The rest stay open, keep running and stay
  listed in the bar; their output reaches you only as the `*` mark and `!N` in the window title. The shown
  pane group slides to contain the active session, so first and last sessions are reachable.
- **All sessions of a window share one grid size.** Any layout change (resize, zoom, tab/side-by-side
  toggle, open/close) resizes EVERY session to the same cols x rows: every shell gets SIGWINCH and every
  session's scrollback re-wraps. That is what makes switching to a hidden session free, and why a toggle
  touches sessions you are not looking at.

**Graphics / images**

- **Only the kitty graphics protocol is decoded.** sixel and the iTerm2 protocol are not: a DCS string
  (what sixel arrives in) is discarded without a trace. The DA1 answer deliberately omits the graphics
  capability bits (4, 16, 18) so an application does not pick a protocol Kai-Term cannot receive —
  measured with timg 1.6.3: with `;4` in that answer it sends sixel and the picture disappears
  entirely, without it it falls back to drawing with block characters.
- **`timg` needs `-p k` here.** timg 1.6.3 auto-detects graphics only when `TERM` is `xterm-kitty` or
  `xterm-ghostty` (a list hardcoded in its binary — `TERM_PROGRAM` changes nothing) and the PTY reports
  a pixel geometry. Kai-Term reports the geometry (`ws_xpixel`/`ws_ypixel`, `CSI 14 t`, `CSI 16 t`) but
  sets `TERM=xterm-256color`, so auto-detect lands on block characters.
- **Parts of the protocol are not implemented**: cropping (`w=`, `h=`, `X=`, `Y=`), placement tweaks
  (`o=`, `e=`, `v=`), file-based transmission (`t=f`/`t=v`, which would need a file-read permission
  model), and the actions beyond transmit/place/delete — animation (`a=a`, `a=i`), cursor-only display
  (`a=C`) and the query (`a=q`). An unimplemented action logs `graphics: action a=X ignored` at debug
  level instead of drawing something wrong.
- **Hard caps, so a picture storm cannot exhaust the machine.** One session keeps at most 256 images /
  64 MiB of decoded pixels (oldest first), accepts at most 4096 px per side and 16 Mpx per image,
  buffers at most 32 MiB of APC payload per sequence (more is discarded with a warning) and inflates
  at most 64 MiB. The renderer considers at most 64 pictures per pane per frame and keeps a GL texture
  only for what is on screen (at most 512).
- **A picture composites against ONE cell background** — the one its top-left visible corner sits in —
  because the renderer never enables GL blending. A picture lying over cells with different
  backgrounds blends against only one of them.
- **A picture lives exactly as long as the text rows it is anchored to.** Scrolling up into history
  brings it back while those rows are retained; when they leave the grid (scrollback eviction, `clear`
  / `CSI 3 J`) the picture is freed with them.
- **The raw pixel formats (`f=24`/`f=32`) have never been exercised by a real client.** The protocol is
  ambiguous about what `s=` counts (byte size or column count); Kai-Term accepts whichever reading the
  payload length fits. timg only ever sends PNG, so nothing on this box can settle it.
- **Animation is only as good as the client's placement.** Re-transmitting to an existing id replaces
  that picture (measured: `timg -V` alternates two ids across four frames) rather than stacking a new
  one, but timg also moves the cursor for every frame, so the frames step down the screen instead of
  animating in one place.
- **A picture belongs to the screen it was placed on.** One placed on the alternate screen is drawn
  only while the alternate screen is active, and one from the main screen only while it is not.

**File manager**

- **The band costs the terminal rows, and the terminal wins.** `fm_size` (10–95, default 75) is a PERCENT of
  the grid area's height: the band keeps that share when the window is resized or the font zoomed. It is
  clamped in `layout.c` so the shell keeps at least 3 rows, and dropped entirely for a frame when even that
  leaves fewer than 3 rows of band — the same rule the tab bar follows. `-d` shows it: `grid resized to
  154x14 …` with the band on in a window whose grid area is 154x58 without it (75% of the rows, the gap and
  the whole-cell remainder taken off the terminal).
- **An image preview shows its picture, not its metadata, when the pane is too short for both.** The rows go
  header first, then as many metadata lines as fit, then the picture, which always keeps one row. A preview
  pane of 3 rows therefore shows the image with no metadata at all; 2 rows show no picture box at all.
- **One browser per window, shared by every session in it.** The file manager does not follow the active
  session's working directory, and it does not have one view per tab.
- **The `cd` follow is one directory deep.** Kai-Term remembers the last directory it sent and does not
  repeat it, so pressing the refresh key does not type a command at your prompt — but it also cannot know
  about a `cd` you typed yourself. Navigating away and back to a directory sends the line again. It is
  never sent to a session on the alternate screen (vim, htop, less would receive it as typing), and
  switching session clears the memory so the newly visible shell is re-synced on the next navigation.
- **The follow writes only where the model can prove the line is empty.** Kai-Term cannot see where a prompt
  ends — the tty echoes what you type, and that echo arrives as ordinary output — so it remembers the whole
  cursor row as the shell last printed it and compares. A navigation therefore captures your text, erases the
  line with Ctrl+U, and sends the `cd` **only after a burst is seen to CHANGE that row back into the
  remembered prompt**; a burst that leaves the row exactly as it was proves nothing. If the shell never shows
  that, the sequence times out after two seconds, nothing more is written, and the log prints what had been
  captured. Two shapes are refused or dropped rather than written through: a line the model cannot describe at
  all (wrapped onto a second row, scrolled back into history, the alternate screen, a prompt redrawn with
  different attributes, text longer than the buffer), and a cursor parked in the **middle** of a command —
  Ctrl+U erases backwards from the cursor, the right half would survive and the `cd` would run in front of it,
  so the proof fails and the sequence is dropped (the captured text goes to the log). A shell's autosuggestion
  ghost right of the cursor is deliberately NOT in that group: it is ignored, and the navigation goes through.
  Submitting the line (Enter) or discarding it (Ctrl+C) re-arms the gate, and so does any key that cannot put
  a character on the line — BackSpace, Delete, an arrow key — while the line is provably empty (see "Fixed",
  2026-10-20). A key that CAN insert characters (Tab, Up/Down, Ctrl+R, Ctrl+Y) keeps the gate shut until the
  shell proves the line empty again. The path key (`p`, see below) asks the LOOSER version of the same question
  — prompt prefix unchanged, text up to the cursor, whatever is right of the cursor ignored — because it erases
  nothing, and that is what lets it type after a half-finished command, through a ghost, and at a cursor parked
  mid-line. It still refuses the alternate screen, a wrapped line, a scrolled-back view and a repainted prompt;
  `-d` logs `fm: not typing the path — the input line is not one I can describe`.
- **The typed `cd` cannot be hidden from the screen — but for an integrated shell there is no typed `cd` at
  all.** For fish, bash and zsh started by Kai-Term (see "Shell integration" below) the directory change runs
  inside the shell's own line editor, so nothing is echoed and nothing is typed. For every other shell the old
  rule still holds: the tty echoes what Kai-Term writes, and discarding those bytes would leave the screen
  showing something readline believes is not there — its next redraw (`\r\x1b[K…`) would print the command
  anyway, and everything in between would be a lie. So `cd '<dir>'` appears at your prompt and runs, exactly as
  if you had typed it. Suppressing it needs the shell's cooperation, which is exactly what the integration is.
- **The follow only runs when the shell is at its prompt.** Kai-Term cannot tell a shell at a prompt from a
  shell running `sleep`, so with the typed path the line is queued in the tty input buffer: it becomes the next
  command when that program exits, or is consumed as stdin by one that reads stdin (`cat`, `dc`, …). Browsing
  while a command that reads stdin is in front of you is the one case where the line can land somewhere
  unexpected — `fm_follow = 0` avoids it. The integration path is safer here (its bytes are inert for anything
  that is not the integrated shell's line editor) but not free: the request is answered only by that shell, so
  a navigation during another program simply times out and falls back to the typed path.
- **Kai-Term never opens a file.** `Return` on a file does NOTHING at all, and the path key (`p`) types its
  quoted path at the cursor and stops there; there is no "default application", no shell-out, and no
  confirmation dialog. Opening things is a decision deliberately left to the user.
- **The band's key layout changed on 2026-10-20 (s95), by request.** `p` used to toggle the preview and `Return`
  used to type a file's path; now `p` types the path and `v` toggles the preview, and `Return` on a file does
  nothing. Muscle memory is the only cost, and every one of them is rebindable — `fm_key_path`, `fm_key_preview`,
  `fm_key_open` — with `none` to remove one. `fm_key_path = none` is the setting for "no file-manager key may
  ever write at my prompt".
- **Hard caps, so a huge directory or a huge file cannot stall or exhaust anything.** A listing keeps at
  most 8192 entries (the footer says `(listing capped)`); a directory preview shows at most 512; a text
  preview reads at most 256 KiB; an image preview reads at most 16 MiB of file and then meets the decoder's
  own caps (4096 px per side, 16 Mpx). Over any of them the pane says so and shows metadata instead.
  Directories and previews are read on a worker thread, so the UI never blocks — an answer about a
  directory or file the user has already moved away from is dropped (`-d` logs
  `fm: dropping stale listing/preview …`).
- **The text preview is not a viewer.** It shows the file from the beginning; the wheel over the pane scrolls it
  (2026-10-21, s96) and, while that pane owns the keyboard, `Page_Up`/`Page_Down` scroll by one pane-full and
  `Home`/`End` jump to the body's first and last line (2026-10-22, s99) — but there is no search and no line numbers,
  and a line longer than the pane is **cut at the right edge, not wrapped** (wrapping would push the rest of the file
  off the pane), which is why one file line is exactly one scrolled row. Highlighting is chosen from the file name —
  13 languages — and is a heuristic, not a parser.
- **Only the first image of a file is decoded — and an animated WebP is not decoded at all.** An animated
  GIF previews its first frame: the in-tree GIF decoder stops after the first image block, because later
  frames are deltas over what was already drawn. An animated WebP fails libwebp's simple decode, so the
  pane falls back to metadata with `webp file could not be decoded` (measured 2026-10-18 with
  `.scratch/s74/animprobe` on 4-frame GIF and WebP files: GIF 20x20 decoded, WebP refused). Reading that
  first frame would need `libwebpdemux`, which Kai-Term deliberately does not link.
- **Sorting and columns are fixed.** `..` is always the first row (except at `/`, which has no parent),
  then directories first, then case-insensitive by name (then by bytes, so the order is total). No sort
  options, no tree view, no multi-select, and no rename / copy / move / delete — the browser reads the
  filesystem and writes nothing to it.
- **One click selects, a double-click opens.** Two left clicks on the same row within 500 ms enter a directory;
  on a file they do nothing, exactly like `Return` (a path is typed by `p` and by no mouse action). The
  threshold is not configurable, and a click on the header, on the
  preview or on a different row breaks the pair. A single click never opens, and the band's clicks never
  reach an application that has grabbed the mouse. The wheel over the browser walks the listing and the wheel
  over the preview scrolls that pane's contents; neither scrolls the terminal's history.
- **While the band owns the keyboard, no other key reaches the shell** — that is the point of the feature,
  but it also means the band's own bindings are the only way to move there. The window's shortcuts (sessions,
  zoom, copy/paste, `Shift`+`PageUp`/`PageDown` scrolling, the band's toggle and focus keys) still work, and
  a click in the terminal pane hands the keyboard back.
- **The band is not part of the terminal's grid.** It is drawn from its own `Cell` arrays through the same
  renderer passes, so it uses the same font, colours and wide-glyph rules — but the terminal's selection,
  scrollback and mouse reporting do not reach it, and text in the preview pane cannot be selected.

**Shell integration (`shell_integration = 1`, the default)**

- **Only a shell Kai-Term started is integrated, and only three kinds are recognised:** fish, bash, zsh, by the
  program's name (`$SHELL`, or `shell =` in the config; a login shell's leading `-` is ignored). `-e COMMAND`
  is never wrapped — that command line is what the user asked to run. `sh`, `dash`, `csh`, a `shell =` value
  carrying arguments, and any shell you start yourself keep the typed path exactly as before.
- **Kai-Term takes one key sequence: `Ctrl+x Ctrl+]`.** It is unbound by default in fish and in bash (measured:
  `bind` reports nothing, `bind -p` has no `\C-x\C-]`), inert in both when nothing is bound to it, and free of
  the tty's own control characters — `C-\` would have been just as inert but `stty -a` reports `quit = ^\`, so a
  stray write would SIGQUIT whatever owns the terminal. If you bind that pair yourself, Kai-Term's script binds
  it last and wins; `shell_integration = 0` gives it back.
- **Kai-Term also defines `fish`, `bash` and `zsh` as shell functions** in an integrated shell, so a bare
  `bash` typed at an integrated prompt starts an integrated bash (the user asked for this on 2026-10-20:
  "changing shell in the prompt make it go in fallback mode instead of using the right shell integration").
  That is visible: `type bash` says it is a function. It is narrow — a command with ANY argument runs exactly
  as typed (`bash script.sh`, `fish -c …`), and a name the user already defined as a function or alias is
  never replaced — but `shell_integration = 0` is the way to have none of it. Measured
  (`.scratch/s90b/intgate nested`): the nested shell announces, the ack arrives in 55 ms, the user's line is
  untouched, and `pwd` prints the directory the browser moved into.
- **A shell started where the wrapper cannot see it (`env bash`, `/usr/bin/bash`, `sudo bash`) falls back, and
  the first attempt costs 400 ms.** It inherits the environment but not the key binding, so it never answers:
  Kai-Term waits `FM_INT_ACK_MS` (400 ms), logs `fm: no integration ack from … — using the typed path`, marks
  that session's integration dead, and runs the capture -> Ctrl+U -> `cd` -> re-type machine for that
  navigation. Later navigations there go straight to the typed path. Measured (`.scratch/s90b/intgate dead`):
  the user's text survives and the nested prompt reads `/var $ half typed` — no stray character from the
  sequence, which is what the one-inert-everywhere sequence buys.
- **A dead integration is revived by the next announcement, not remembered forever.** Each integrated shell
  re-announces after every command (fish `fish_postexec`, bash `PROMPT_COMMAND`, zsh `precmd_functions`), and
  the wrapper re-announces when a nested shell exits; `Term.shell_int_gen` counts those announcements and
  `Tab.int_gen` is what main.c last saw, so leaving the unwrapped shell puts the integration back (measured:
  `fm: a shell announced again — the integration is used for this session again`, then an ack in 74 ms). A
  heartbeat was checked not to leak into a command substitution (`.scratch/s92/hbcheck.c`: `set x (echo hi)`
  gives `x` = `hi`, 2 bytes).
- **zsh is verified, but only since 2026-10-20, and not by `make test`.** Its script (a ZLE widget +
  `bindkey '^X^]'`, the wrappers, `precmd_functions`) had never been run anywhere until the user tried it in
  their own window and confirmed it; the headless gate then ran it on zsh 5.9 (announce, ack in 54 ms, line
  byte-identical, prompt repainted). What is still unmeasured is what `C-x C-]` does in a zsh that has NO binding
  — the inertness that protects an unwrapped nested shell was measured in fish and bash only. And no zsh check
  lives in `make test` beyond the script-on-disk pins, so a zsh regression would show up as the ack timing out and
  the typed path taking over, not as a failing check. The safe direction to be wrong in, and the reason zsh must
  not be described as covered by the suite.
- **The prompt is repainted where the shell can be asked to repaint it.** fish: `commandline -f repaint` at the
  end of the handler, measured to re-expand the prompt (the user's report: "the prompt still show /home/kaida
  but it's only visual") — the prompt row then names the new directory and the typed text is byte-identical.
  zsh: `zle reset-prompt` (verified in zsh 5.9, 2026-10-20). **bash: it cannot be done, and nothing is pretended** —
  readline expands the prompt once per `readline()` call, so `C-l` leaves the old directory (measured) and
  printing `${PS1@P}` over the line garbles it (measured: readline's cached cursor position goes wrong and the
  old prompt text survives). The only thing that refreshes a bash prompt is submitting a line, which is not
  Kai-Term's to press. The shell's own directory is correct immediately in every shell (the ack proves it, and
  `pwd` says so); in bash the prompt string catches up on the next Enter.
- **The scripts and the request files live in a private directory** (`$XDG_RUNTIME_DIR` else `/tmp`,
  `mkdtemp`, mode 0700, removed at exit; one `cd-request.<id>` per session, written as a temp file and
  `rename()`d so the shell can never read a half path). The directory is NEVER part of the control sequence, so
  a stray sequence can never run a path, and a path can never be guessed into a sequence.

**Rendering / input**

- **Video memory is shared with everything else on the GPU, and Kai-Term gives up rather than draw
  garbage.** If the driver refuses the glyph atlas, the application-picture textures are dropped first
  (their pixels are still in the model) and the upload is retried once; if that still fails the run ends
  with exit status 1 and `kai-term: no video memory for the glyph atlas — exiting`. A refused color-emoji
  atlas is not fatal: emoji are drawn monochrome and the upload is retried about once a second. A refused
  picture or file-manager-preview texture skips that picture until an upload succeeds. Nothing is ever
  drawn with a texture the GPU refused — that used to fault inside the driver (see "Fixed").

- **Color emoji come from whatever color font fontconfig finds.** The installed NotoColorEmoji.ttf is
  bitmap-only (CBDT/CBLC, a single strike), so zoomed emoji are scaled from that one strike instead of
  re-rasterized; the scalable-color-font branch (`FT_IS_SCALABLE` -> `FT_Set_Pixel_Sizes` +
  `hb_ft_font_changed`) has never run here. At most `CLUSTER_MAX` = 8 modifier codepoints fold into one
  anchor cell (longer sequences split into an extra cell). Unicode text-presentation bases (heart, keycap)
  stay monochrome unless the sequence carries U+FE0F. Some glyphs are grayscale in the installed font —
  font data, not a rendering bug.
- **Transparency needs a compositor that blends ARGB surfaces.** `opacity` is verified on niri (screenshot
  A/B); on a non-compositing or RGB-only path the window just looks opaque.
- **Key repeat tracks one key at a time.** No Wayland compositor sends repeat events, so Kai-Term
  generates them (`repeat_delay` ms, then `repeat_rate`/s). Only the most recently pressed repeatable key
  repeats: hold `a`, press and release `b`, and `a` stops repeating though it is still held. The
  compositor's `wl_keyboard.repeat_info` is logged under `-d` but deliberately NOT used, so timings come
  from the config and feel identical on every compositor. `repeat_delay = 0` disables repeat entirely —
  a compositor never repeats for you.
- **The window title is Kai-Term's own, not the application's.** `update_title()` -> `mux_title()` ->
  `xdg_toplevel_set_title()`: `Kai-Term`, or `Kai-Term [active/total]`, plus `!N` when N background
  sessions produced output. An app's OSC 0/2 title goes to that session's tab bar label instead (or
  `shell` until it sets one), so a taskbar, window rule or script matching on the window title sees
  Kai-Term's text, not the shell's.
