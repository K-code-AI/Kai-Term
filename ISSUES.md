# Kai-Term — Known Issues

Current problems and current behaviour only. The history of what was broken and fixed is NOT kept here:
54 root-cause entries and the 4 unproven reports cleared 2026-10-17 are in `git show d5afe4a:ISSUES.md`.
The rules those bugs produced are in AGENT.md "Architecture & Invariants" — read those, not the history.

## Open

**No open bug.** The user's report that opened this — the mouse wheel doing nothing inside `nano` — is fixed and
confirmed in their own window; see the three s96 entries at the top of "Fixed".

Re-measured 2026-10-21 (s96, after the mouse-in-full-screen work): `make -j8` exit 0 with **0 warnings**;
`./bin/test_vt` **845 checks, 0 failures**; `./bin/dbg` **MODEL OK
(0 problems)**; `./bin/test_fm` **274 checks, 0 failures**; `KAI_TERM_TEST_NO_WINDOW=1 make test` exit 0
(`CLI: 62 checks, 0 failures, 10 skipped` + those three); `make asan` exit 0 with the same counts and no
sanitizer report; `tests/cli_test.sh` 95 checks / 0 failures / 0 skipped when the probe launch can draw (the
window-opening group is skipped on a box whose video memory is spoken for, so it was not re-run here).
No `TODO`/`FIXME`/`XXX` and no `TEMPORARY`/`PROBE` marker anywhere in `src/`, `include/`, `tests/`.

**Fixed and confirmed by the user in their own window (2026-10-19):** a second file-manager navigation on top
of a prompt Kai-Term had just re-populated did not erase it — the prompt ended up `Some text cd '/'` and the
shell ran that. The cause (the echo window closed on a steady state instead of on a change), the fix and the
11 checks that now pin it are in the "Fixed" entry below and the s82-s86 journal entry of AGENT.md.

The behaviour the user asked for on 2026-10-19 — clear the prompt, `cd`, put the text back — **is implemented**
(README "File manager"). Its limits are under "Known limitations" below.

Everything else below is current behaviour, not a bug.

The integrated file manager (README "File manager") is built, wired into the window and covered by 247
model/layout/config checks plus the 814 passing ones in the VT/config suite; its input routing was verified
live by injecting keys through the compositor and reading the bytes the PTY received. The four reported
file-manager problems (keys reaching the terminal while the band is focused, Left/Right navigation, writing
at a prompt that is already in use, click-to-open and the `..` row) are fixed — see the s71-s79 journal
entries in AGENT.md. The user has run the band in their own window and confirmed its behaviour
(2026-10-19); **no verdict has been given on how it LOOKS** — that is the one open item, and it belongs to
the user. Its limits are under "Known limitations" — "File manager".

**Shell integration (s90-s92) is implemented, wired, and CONFIRMED BY THE USER IN THEIR OWN WINDOW for fish, bash
and zsh** ("I verified the shell integration, Fish Bash and Zsh are working as expected now. Perfect",
2026-10-20). A file-manager navigation in a shell Kai-Term started asks the SHELL to change its own directory:
no erase, no re-type, no visible `cd`, and the user's half-typed line is byte-identical before and after
(`.scratch/s90b/intgate.c`: ack in 54-76 ms, `pwd` prints the new directory). zsh 5.9 joined that gate on
2026-10-20 — its script had never been run anywhere before that. The two defects the user reported on
2026-10-20 are fixed and now user-confirmed: see "Fixed" below. Every other shell — and any shell the user starts
in a way the wrapper cannot see — keeps the typed path unchanged. Its limits are under "Known limitations" —
"Shell integration".

Image support is confirmed by eye in the user's window (`timg -p k <file.png>`, 2026-10-17) as well as by
the model tests and the `graphics: N image(s) drawn` log line. Its limits are listed under
"Known limitations" — "Graphics / images".

To file a problem: symptom, exact reproduction, and what `./bin/kai-term -d` logs. Not reproducible ->
not filed; it is not a bug until it reproduces.

## Fixed

**The mouse wheel did nothing inside `nano` / `less` / `man`, and was dead in every program that DID ask for the
mouse (2026-10-21, s96).** Symptom, reported by the user: "When opening a file with nano or similar program it
would be nice to be able to scroll with the mouse", and, comparing terminals, "In kitty I can't click, but I can
scroll. So I want scrolling in Kai-Term too when using nano and clicking could be a nice bonus." Two separate
causes, both measured first (`.scratch/s96/nanoprobe`, `.scratch/s96/clickprobe` — real nano 9.2 and less in a
real Kai-Term pty, no window): (1) such a program owns the alternate screen, whose grid is built with
`scrollback_max = 0`, so the wheel landed in `scrollback_nav()` and was thrown away — nothing of the terminal's
to scroll; (2) the wheel report sent to a program that HAD grabbed the mouse used button codes **4/5**, which
ncurses reads as button 1: on `nano --mouse`, `ESC[<5;20;12M` did nothing at all while `ESC[<65;20;12M` scrolled
the text (same in the legacy X10 form — `32+65` scrolls, `32+4` does not), so the wheel was broken in vim, htop,
tmux and `nano --mouse`. Fix: `term_wants_key_mouse()` / `term_wheel_arrow_keys()` / `term_click_arrow_keys()` in
`src/vt.c` (the state they read lives there, and main.c is linked into no test binary), wired in `ptr_scroll_done()`
and `ptr_button()`, plus `mouse_wheel_button()` for the 64/65 codes. A click is turned into the arrow-key presses
that walk the program's own cursor to the clicked cell — **keystroke synthesis, not mouse support**, because a
program with no mouse protocol reads a report as typed text: one `ESC[<64;20;5M` put `64;20;5M` into the user's
file and marked it Modified. `wheel_sends_keys` / `click_moves_cursor` (both default 1) switch each half off.
Verified: 31 new checks in `tests/test_vt.c` (814 -> 845, 0 failures), and **the user confirmed it in their own
window** — scrolling in nano works, and `nano --mouse` behaves as expected.

**The arrow keys did nothing at all in `less`, `man` and `htop` (2026-10-21, s96).** DECCKM (DEC private mode 1,
application cursor keys) was not implemented: `send_key()` always sent the CSI form (`ESC [ A`..`D`), while an
ncurses program asks for the SS3 form (`ESC O A`..`D`) with `ESC[?1h`. Measured, and the two programs differ —
nano answers either form, **less answers ONLY the SS3 one**, so in less the arrow keys were inert. `Term.app_cursor`
is now set by `ESC[?1h` / cleared by `ESC[?1l`, saved and restored across the alternate screen the way xterm does,
and every cursor key — the ones the user presses and the ones the wheel and a click are turned into — goes out
through one `term_arrow_seq()`, so they cannot disagree about the form. Verified by the same probe (30 SS3 presses
moved less from top line 1 to 31; the CSI form moved it not at all) and by checks on both forms.

**The wheel over the file manager's preview pane did nothing, and a resized pane could leave the preview scrolled
into blank rows (2026-10-21, s96).** `fm_wheel()` claimed the gesture over the preview, set focus and returned
without acting — so a long file could not be read in the pane that exists to read it. Added `FileMgr.pv_scroll`
with `fm_preview_scroll()` / `fm_preview_scroll_max()` and made the painter obey it for a text file and for a
directory listing; an image or metadata-only preview has no body, so the call is a harmless no-op there.
**A second defect, found by the new checks and in the model rather than the test:** the clamp that keeps
`pv_scroll` inside what the pane can show sat at the TOP of `fm_set_size()`, but `fm_preview_scroll_max()`
measures `fm->prev.rows` — the pane that actually EXISTS — and the realloc had not run yet, so it reported the
OLD height and the clamp could never change anything (a 40-line file at the bottom of a 12-row pane, `pv_scroll`
30, grown to a 40-row pane, stayed at 30: 28 rows of nothing). Moved after the allocation. Verified: 27 new
checks in `tests/test_fm.c` (247 -> 274, 0 failures), and the clamp check A/B'd against `fm.c` with the clamp in
its old place — 1 failure there, 0 here (`.scratch/s96/clampab/`).

**A shell started at the prompt fell back to the typed `cd`, and the prompt kept showing the OLD directory
(2026-10-20).** Symptom, reported by the user: "changing shell in the prompt make it go in fallback mode instead
of using the right shell integration", and "The dir change works as expected, but we have a visual bug, If I
navigate from /home/kaida to /home/kaida/test/ in the file manager, then the prompt still show /home/kaida … is
there a way to refresh the prompt when navigating using the filemanager to not have that wrong visual?" Cause and
fix: (1) `Tab.int_dead` was one-way and a shell the user started at the prompt never loaded the script at all —
the three scripts now wrap a bare `fish`/`bash`/`zsh` (never over a name the user defined, never with any argument
present) and re-announce after every command, and `Term.shell_int_gen` + `Tab.int_gen` make an announcement an
EVENT that revives an integration the session had given up on; (2) fish and zsh now repaint the prompt from inside
the handler (`commandline -f repaint`, `zle reset-prompt`), so it names the directory the shell is really in —
**bash cannot and nothing is pretended**: readline expands the prompt once per `readline()` call, `C-l` leaves the
old directory and printing `${PS1@P}` over the line garbles it (both measured), so bash is documented and the
prompt catches up on the next Enter. Verified: 14 new checks in `tests/test_vt.c` (789 -> 803, 0 failures), the
gate's `nested` / `dead` / `off` modes over real fish and bash, and **the user's own confirmation in their window
for all three shells (2026-10-20)**.

**A BackSpace on an empty prompt disabled the shell follow for the rest of the session (2026-10-20).**
Symptom, reported by the user: after pressing BackSpace at an empty prompt, browsing into a directory no
longer moved the shell — the log said `fm: dropped the follow while erasing (the wait ran out)` and nothing
was written, until Enter was pressed. Cause: every byte Kai-Term sent opened the "an echo may still be on its
way" window, and that window closes only on output seen to CHANGE the row back into the remembered prompt.
Measured with `.scratch/s89/bstoprobe` (real shells in a real Kai-Term pty): bash and sh answer BackSpace at
an empty prompt with nothing but a bell (`0x07`) and fish answers NOT AT ALL, so no burst ever proved the line
empty and the window stayed shut. Now a payload that cannot put a character on the line — cursor movement
(Left/Right/Home/End, Ctrl+A/B/E/F), deletion (BackSpace, Delete, Ctrl+D/K/U/W), Ctrl+L, a bare Escape — does
not open the window **when the line is already provably the bare prompt**; everything else (text, Tab,
Up/Down, Ctrl+R/S/T/V/Y/_, Insert, PageUp/PageDown, F-keys, any unknown or truncated sequence) opens it
exactly as before, because any of those can insert characters the screen may never show. Fixing that exposed a
second, older hole that had been covered by accident: the remembered prompt's CURSOR position is where the
cursor sits when the line is EMPTY, and a movement key used to leave `input_pending` set, which is what kept
the snapshot from being re-taken at the moved cursor. `prompt_snapshot()` now leaves the snapshot alone when
the row is cell-for-cell the remembered prompt and only the cursor moved, so a cursor parked inside the
prompt text still answers "do not write here". Verified: 20 new checks in `tests/test_vt.c` (726 -> 746, 0
failures) and an A/B of the real follow machine over real fish (`.scratch/s89/follow3{,_old}`) — the old code
drops both navigations, the new one takes the fast path twice; with text on the line both navigations still
end with the prompt holding the directory and the text once, and the documented mid-command-cursor refusal is
unchanged. The user confirmed the behaviour in their own window (2026-10-20).

**The file manager's metadata was painted across its own image preview (2026-10-19).** Symptom, reported by
the user: "the metadata sometimes get written on the image preview". Cause, in the model, not in the shader:
the preview pane's picture box started at a hardcoded row 3 while `paint_preview_meta()` wrote every metadata
line it had starting at row 2 — an image preview carries eight of them (path, type, size, mode, owner, group,
modified, format) — and the renderer emits the pane's glyphs in the batch AFTER the picture, so every line
from row 3 down landed on top of it. It only looked intermittent because a small picture is centred low
enough to miss the text. Now one place owns the pane's rows (`preview_head_rows()` + `preview_meta_rows()`),
used by both the painter and `fm_image_fit()`: header, then the metadata lines that fit, then the picture,
which always keeps at least one row. Verified by 12 new checks in `tests/test_fm.c` (235 -> 247, 0 failures)
that scan every cell inside the box `fm_image_fit()` reports for a painted glyph, and by A/B: the same new
test file against the previous `src/fm.c` gives **8 failures**, including `fit: a picture that fills the area
has no metadata line drawn across it`. Not yet re-run in the user's window.

**The band is now a share of the window, not a row count (2026-10-19).** Requested by the user: "Would it be
possible for the File manager part to occupy more space? like 75% per default and make it configurable in the
config file" — and, on the shape of it, "don't keep fm_rows we don't need it". So `fm_rows` is GONE (config
key, `Config`/`LayoutReq` field, `FM_ROWS_MAX`, its README row and its checks) and `fm_size` (percent of the
text area, default **75**, clamped 10–95 at parse time and again in `layout.c`) replaces it. The band now
keeps its proportion through a resize or a zoom instead of becoming huge or tiny next to the terminal, and
`layout_initial_size()` solves the inverse (`grid_h = ceil((rows*ch + gap) * 100 / (100 - pct))`) so a startup
still gets exactly the configured `cols x rows` for the terminal pane. Verified by 7 new checks in
`tests/test_vt.c` (719 -> 726, 0 failures: the share, both clamps, the terminal's floor, the share surviving
a resize and a zoom, three startup round trips) and by one new `tests/cli_test.sh` check comparing the real
binary's `grid CxR per session` line with the band off and with `fm_size = 50` on. Not yet seen by the user.

**A second file-manager navigation appended `cd <dir>` to the prompt instead of erasing it first
(2026-10-19).** Symptom, reported by the user: type `Some text`, browse once — the line is cleared, the `cd`
runs and `Some text` comes back, as intended; browse again without touching the line and the prompt becomes
`Some text cd '/'`, which the shell then runs (`fish: Unknown command: Some`). Cause, measured against real
fish through the real pty (`.scratch/s86/follow2`): after Kai-Term re-typed the text, the shell's next burst
repainted only prompt chrome (OSC 7, OSC 0, DECSET) and touched the input row not at all, so that row still
equalled the remembered prompt. The end-of-burst rule read that **steady state** as "the application put the
prompt back, so the line is provably empty" and closed the echo window over bytes that had not been echoed
yet; every later burst then re-took the prompt snapshot — including the one carrying the echo of Kai-Term's
own text — so a row holding `Some text` began answering `term_at_prompt()` as EMPTY, the re-type could never
be confirmed, and the next navigation took the fast path. Now the window closes only when the row **changed**
into the remembered prompt between the start and the end of one burst, and the `cd`-step additionally waits
for the session to go quiet (150 ms) before believing the new prompt is drawn — a burst boundary is not a
prompt, and fish answers a `cd` with the echo, a continuation row and prompt chrome before drawing it.
Verified: 11 new checks in `tests/test_vt.c` (708 -> 719, 0 failures) and a headless replay of the machine
over real fish — two navigations in a row both end with the completion line and the line holds `Some text `,
not the appended command. The user then confirmed it in their own window ("I confirm it works", 2026-10-19).

**Kai-Term segfaulted inside the graphics driver when the GPU refused a texture (2026-10-19).** Symptom:
`kai-term error: atlas glTexImage2D failed: 0x505 (512x512)` followed by SIGSEGV at address 0x4 inside
`libnvidia-eglcore`, reached from `draw_quads()` — 85 core dumps from one run of `tests/cli_test.sh` on a
machine whose video memory was already full. Cause: every texture-upload site (`render_draw()`'s two
atlases, `img_tex()`, `fm_preview_tex()`) logged the failure and then drew with a texture that had no
texels. Now: the glyph atlas is retried once after dropping every application-picture texture (the model
still holds those pixels), and if it is still refused `render_draw()` returns -1 and the process exits 1
with `kai-term: no video memory for the glyph atlas — exiting`; a refused color-emoji atlas falls back to
monochrome glyphs and retries about once a second; a refused picture or preview texture skips that picture
until an upload succeeds. Verified by forcing the failure in an A/B build (`.scratch/s81/`): the old tree
draws with an unbacked texture and exits 0, the new one exits 1 with the message; the color-atlas branch
was forced the same way and the terminal ran on and exited 0. The natural trigger needs a GPU with no
memory left, so it has not been re-run for real since the fix.

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
  refuse. First things to check if pasting from other apps breaks.
- **A paste over `PASTE_BUF_MAX` (8 MiB) is sent unwrapped** — no `ESC[200~`/`ESC[201~` — with a log
  warning, because the whole block must be buffered before delimiters can surround it.
- **Bracketed paste is per application:** the delimiters are sent only when the app has DEC mode 2004 on.
  A shell/readline with bracketed paste off correctly keeps getting raw line-by-line pastes.

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
- **The text preview is not a viewer.** It shows the file from the beginning and the wheel over the pane
  scrolls it (since 2026-10-21, s96), but there is no search and no line numbers, and a line longer than the
  pane is **cut at the right edge, not wrapped** (wrapping would push the rest of the file off the pane) — which
  is why one file line is exactly one scrolled row. Highlighting is chosen from the file name — 13 languages — and
  is a heuristic, not a parser.
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
