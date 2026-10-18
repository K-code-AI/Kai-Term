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

## Current Status (re-verified 2026-10-19, session 86 — the clear -> `cd` -> re-populate follow is implemented, idempotent, and green; the one open item is still the user's eye on the band)

- `make -j8` exit 0, **0 warnings** under `WARN` (`-Wall -Wextra -Wshadow -Wwrite-strings
  -Wformat=2 -Wdiscarded-qualifiers`). Headless suites: `./bin/test_vt` **719 checks, 0 failures**,
  `./bin/dbg` **MODEL OK (0 problems)**, `./bin/test_fm` **235 checks, 0 failures**;
  `KAI_TERM_TEST_NO_WINDOW=1 make test` exit 0 (`CLI: 62 checks, 0 failures, 10 skipped` + those three);
  `make asan` exit 0 with the same counts and 0 sanitizer reports.
- **The follow is proven headless against real fish, not yet in the user's window.**
  `.scratch/s86/follow2.c` runs the machine from `src/main.c` over the real `src/pty.c` + real
  `term_parse()`: two navigations on top of each other both end with
  `the prompt now holds the browser's directory and what you had typed` and the line holds `Some text ` —
  not the `Some text cd /etc` the user reported. `g.log` = the fish-autosuggestion case now follows;
  `m.log` = the mid-line-cursor case fails safe (no `cd` ever sent, the captured text logged).
  The live key-injection harness was NOT re-run: the user is at this machine and asked not to be disturbed
  (s86 entry 2) — hand them the command instead of injecting keys.
- `git status -sb` -> **seven modified files**: `src/main.c` (the follow machine + the quiet-period step),
  `src/vt.c` (the echo window needs a transition; the resize keeps a dirty prompt snapshot),
  `include/kai-term/kai-term.h` (`term_prompt_line_typed`), `tests/test_vt.c`, `README.md`, `ISSUES.md`,
  `AGENT.md`. HEAD is the s75-s82 docs commit; the s75-s81 range is committed in three pieces (s83 entry 2).
  Read the remote state with `git status -sb` / `git log --oneline origin/main -1` — never write a hash or an
  ahead-count here.
- The file manager IS reachable from the UI (`Ctrl+Shift+F`) and its band is drawn. Its input routing
  (toggle, band keys, focus handover, `cd` follow, close/reopen) was verified live by injecting keys through
  niri and reading the bytes the PTY received — three real defects found and fixed that way (s74 entries
  2-4). **No human has seen how it looks yet**; that verdict is the user's and is the one open item.
- **Image support (kitty graphics protocol) is in the tree and user-confirmed by eye** ("The image display
  properly. The feature works as expected", 2026-10-17, session 70, `timg -p k` with a PNG): APC/DCS parser
  states, chunk assembly, base64 + zlib + PNG (libpng simplified API) + raw f=24/32 decode, premultiplied
  pixels held in the model, images anchored to absolute grid rows, winsize pixel geometry +
  `CSI 14 t`/`CSI 16 t`, and a renderer that draws each picture between the background batch and the text
  batch. Also verified headless (real timg bytes through the real parser) and by log line
  (`graphics: 1 image(s) drawn, 1 texture(s) held`, no GL warning).
- No probe/experiment left in tree: `grep -rnE "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/ include/ tests/` ->
  empty (the s85 RETYPING probe was removed in s86 entry 1; the line had been stale since it was written).
- README flag table (`-e -w -c -d -v -h`) and its **49** settings rows match `src/main.c` / `src/config.c`
  exactly — re-checked by set difference, not by eye, this session: the 49 keys `apply()` in `src/config.c`
  compares (`!strcmp(key, …)`, `font` included) and the 49 rows of the "Available settings" table are the same
  set, `diff` empty both ways. README
  now also has a "File manager" usage section, the 20 file-manager settings rows, the shortcut tables, and
  libjpeg + libwebp in Requirements.
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
- Git: `origin = https://github.com/K-code-AI/Kai-Term`, branch `main`. As of s70 close **everything on main
  is pushed** (`main` == `origin/main`, 0 behind / 0 ahead, empty `git diff`). Do not write a hash or an
  ahead-count here — the commit recording it makes it wrong. Read the truth with `git status -sb` and
  `git log --oneline origin/main -1`. `git fetch` before push — user commits on GitHub too. Never commit
  `bin/` or `build/`.

## Current Task (s86 — fix the second-navigation append bug, make the follow tolerate fish's autosuggestion, then docs + push)

**User report (2026-10-19):** typing `Some text` at the prompt and navigating once works (erase -> `cd` ->
re-type). Navigating AGAIN without touching the prompt does not erase first: the prompt ends up
`Some text cd '/'` and fish runs it (`fish: Unknown command: Some`). Also: the user answered the open
question from s85 entry 1 — **make the follow happen even when the line shows fish's autosuggestion ghost.**
The user has run `make test`; what is left is this bug, the docs, and the push.

**s86 measured start (measured this session, not inherited):**
- `git status -sb` -> `main` == `origin/main`, five modified files: `AGENT.md`,
  `include/kai-term/kai-term.h`, `src/main.c`, `src/vt.c`, `tests/test_vt.c`.
- `make -j8` -> exit 0, `grep -ciE "warning|error"` over the build log -> **0**.
  `make -j8 bin/test_vt bin/dbg bin/test_fm` -> 0 warnings; `./bin/test_vt` **708 checks, 0 failures**,
  `./bin/dbg` **MODEL OK (0 problems)**, `./bin/test_fm` **235 checks, 0 failures**.
- **Current Status was wrong about the probes:** `grep -rnE "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/
  include/ tests/` -> **2 hits**, `src/main.c:1063` and `src/main.c:1125` — the s85 RETYPING probe is
  still in the tree. It must come out (it logs a line per burst) and the status line corrected.
- Reproduced headless, no window: `/home/kaida/Project/.scratch/s86/follow2.c` — the real machine from
  `src/main.c` (its five functions copied verbatim) driving REAL fish through the real `src/pty.c` and the
  real `term_parse()`, two navigations, printing the state after every burst. Output in
  `.scratch/s86/f2b.log`. It reproduces the report exactly.

**Root cause (measured, not inferred).** After the re-type the machine has sent `Some text ` with no CR, so
`input_pending` is 1 and the snapshot must stay the clean prompt. The very next burst is fish repainting
the prompt chrome (`ESC]7;file://…`, `ESC]0;…`, `ESC[?2004h`, `ESC[K`, a cursor move) — it paints NOTHING
on the input row, so the row still equals the snapshot. The end-of-burst rule
(`src/vt.c:2901-2904`) is `else if (prompt_row_matches(t)) t->input_pending = 0;` — a STEADY STATE, not a
change — so it declares "the application put the prompt back, the line is provably empty" and closes the
echo window while our own bytes are still outstanding. Trace:
`[burst] state=re-typing pending=0 cx=0 cy=3 p_cx=0 p_cy=3` — and with `input_pending` now 0, every later
burst RE-TAKES the snapshot, including the burst carrying the echo of the text we typed, so the snapshot
becomes `prompt + Some text ` (`p_cx` 14 -> 24). From then on `term_at_prompt()` is TRUE with text on the
line, `term_prompt_line()` describes the typed text as `""`, `FMCD_RETYPING` can never confirm (it times
out), and the next navigation takes the FAST PATH and appends `cd '<dir>'\r` after the user's text —
which is what the user saw. **A match that was already a match is not evidence.**

**s86 objective:** the follow must be idempotent — a second navigation on top of a line Kai-Term itself
re-populated erases it, `cd`s and re-populates again; and a line showing fish's autosuggestion ghost is
followed instead of refused.

**s86 planned steps (each one: edit -> AGENT.md -> build/test; never commit red):**
1. `src/vt.c`: the echo window closes only on a TRANSITION — compute `prompt_row_matches()` at the START of
   `term_parse()` and close the window only when it was 0 before the burst and is 1 after. A burst that
   paints nothing, or repaints the same dirty line, is not proof. Then the snapshot is never re-taken over
   our own echo, so `term_at_prompt()` cannot go true with text on the line.
2. `src/vt.c` + header: `term_prompt_line_typed()` — the non-strict description (the text between the end of
   the prompt and the cursor, ignoring whatever the shell paints right of the cursor). `src/main.c`:
   `fm_send_cd()`'s capture uses it instead of `term_prompt_line()`. The safety property is untouched: the
   `cd` is still sent only from ERASING/CDING behind `term_at_prompt()`, i.e. only once the whole line is
   PROVABLY empty — a cursor parked mid-line erases only its left half, the proof then fails, the sequence
   drops and the log prints what had been captured. That is the accepted cost of "make it follow".
3. `src/main.c`: remove the two s85 TEMPORARY PROBE blocks.
4. `tests/test_vt.c`: pin the transition rule (a burst that changes nothing must NOT close the window; the
   burst that repaints the remembered prompt must) and the ghost case (strict refuses, `term_prompt_line_typed`
   describes the typed text; the erase proof still refuses the mid-line cursor).
5. Live harness `.scratch/s84/inj_follow.sh` — ONE window (GPU is full), fish, TWO navigations on top of
   each other; require the completion line twice and no timeout drop, and the filesystem proof.
6. `make test` (`KAI_TERM_TEST_NO_WINDOW=1`) + `make asan`, exact counts.
7. Docs: README's file-manager prompt paragraph (clear -> `cd` -> re-populate, idempotent, the ghost now
   followed, the mid-line limit), ISSUES.md (the not-implemented note is gone; the limitation rewritten),
   AGENT.md's prompt invariant + the new pitfall. Then commit and push.

**s86 expected outcome:** two navigations in a row both end with
`fm: the prompt now holds the browser's directory and what you had typed` and no append; a ghosted line is
followed; `make test`/`make asan` green at a higher check count; no probe left in the tree; docs matching;
one commit pushed.

### s86 entry 1 — steps 1-3 done: the echo window now needs a TRANSITION, CDING waits for the shell to go quiet, the ghost is walked through, the s85 probe is out

Four edits, each followed by a build. All measured against `.scratch/s86/follow2.c`, which reproduces the
user's report exactly before the change and passes it after.

**1. `src/vt.c` — the echo window closes only on a change, not on a steady state.** `term_parse()` now
computes `int prompt_matched_before = prompt_row_matches(t);` at its start, and the end-of-burst rule is
`else if (prompt_matched_before == 0 && prompt_row_matches(t)) t->input_pending = 0;`. A burst that leaves
the row exactly as it was (fish repainting only OSC 7 / OSC 0 / DECSET / `ESC[K` after the re-type) is no
longer accepted as proof that the line became empty. That premature close was the reported bug: it let
every later burst re-take the snapshot, including the burst carrying the echo of Kai-Term's own re-typed
text, so the snapshot became `prompt + the user's text`, `term_at_prompt()` answered EMPTY with text on the
line, `FMCD_RETYPING` could never confirm, and the next navigation took the fast path and appended
`cd '<dir>'` to the command. **All 708 existing checks still pass** — none of them depended on a match that
was already a match.

**2. `src/main.c` — CDING waits for the session to go quiet (new `FMCD_QUIET_MS` 150, `fm_cd_last_burst`).**
With (1) fixed, the probe exposed a second, PRE-EXISTING hazard it had been masking: the cursor moving off
the row the `cd` was typed on is not the prompt. fish answers a `cd` with the echo, then a continuation row,
then prompt chrome, and only then the prompt — each in its own burst, and the model re-takes the snapshot at
the end of every one (our `cd` ends with CR, so nothing is outstanding). The machine left CDING on a BLANK
row, typed the user's text there, and then compared against that blank-row snapshot forever. Now CDING also
requires `mono_ms() - fm_cd_last_burst >= FMCD_QUIET_MS`: the shell is at its prompt when it has stopped
printing, and after that silence the snapshot the model holds IS the prompt by construction. The frame timer
(`fm_cd_tick()`) now advances CDING when the quiet period expires — that step's evidence is the END of the
output, and no further burst is coming to trigger it. `fm_cd_advance()` gained a `burst` argument so only
real output restarts the period.

**3. The ghost is now followed (the user's answer to the s85 question).** New `term_prompt_line_typed()`
(header + `vt.c`: the same `prompt_line_text()` body with `strict_right = 0`) and `fm_send_cd()`'s capture
uses it. Measured with real fish: typing `git com` paints `mit -m "Initial commit"` in grey right of the
cursor, the strict predicate refuses that row, the new one describes `git com` — and both navigations now
end with `DONE — the prompt holds the dir and what you had typed`.
**The accepted cost, measured, not assumed:** with the cursor parked mid-command (`git commit -m x` + two
Left arrows) the capture takes the left half, Ctrl+U erases only that half, the whole-row proof fails, and
the machine drops the sequence with `DROPPED the follow while erasing (the wait ran out) — LOST the text`
plus the captured text in the log. The `cd` is NEVER sent — the safety property is untouched, so a mangled
command that runs is still impossible; what is possible is losing the left half of a command whose cursor the
user had moved, and the log line is how it is recovered.

**4. The two s85 TEMPORARY PROBE blocks in `src/main.c` are removed** — `grep -rnE
"TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/ include/ tests/` -> **empty** again, which is what Current Status
had claimed while they were still there.

**Measured after these edits:** `make -j8` exit 0, `grep -ciE "warning|error"` -> **0**; `make -j8
bin/test_vt bin/dbg bin/test_fm` -> 0 warnings; `./bin/test_vt` **708 checks, 0 failures**, `./bin/dbg`
**MODEL OK (0 problems)**, `./bin/test_fm` **235 checks, 0 failures**. Probe evidence:
`.scratch/s86/f2d.log` (two navigations, both complete, the line holds `Some text ` — not
`Some text cd /etc`), `g.log` (the ghost case), `m.log` (the mid-line case fails safe).

**Next action (step 4):** `tests/test_vt.c` — pin the transition rule (a burst that changes nothing must not
close the echo window; the burst that repaints the remembered prompt must) and the ghost case (strict
refuses, `term_prompt_line_typed` describes the typed text, the erase proof still refuses a mid-line
cursor). Then `make test` + `make asan`, the live harness, the docs, commit, push.

### s86 entry 2 — step 4-6 done (719 checks green, asan clean); the LIVE run is cancelled — the user is at the machine and asked not to be disturbed

**User report (2026-10-19): "Why did you make my whole screen go red ... You know I use the computer and
you're tests are kinda disturbing".** Measured before answering, not guessed:
- Nothing of mine maps a window or injects keys this session: `pgrep -af kai-term` -> one process, the
  user's own (`bin/kai-term`, pid 470531, started 22:35:09 by the session restart below); `pgrep -af
  "scratch/inject"` -> nothing; `coredumpctl list` -> **no kai-term core after 17:27:30**. Every command I
  ran was headless (`make`, the three test binaries, `.scratch/s86/follow2` over its own pty,
  `KAI_TERM_TEST_NO_WINDOW=1 make test`, `make asan`) — the CLI suite mapped 0 windows (10 SKIP lines).
- What DID happen: `journalctl --user` shows **`noctalia` (the lock screen) SIGSEGV at 22:35:04** and
  **niri restarting the whole session at 22:35:09** (`niri: starting version 26.04`, then `locking session`
  at 22:35:09, `unlocking` at 22:35:14, with `pam_unix(login:account): setuid failed` from noctalia in
  between). That is where a full-screen red/lock flash comes from. The same noctalia SIGSEGV is in
  `coredumpctl` at **17:43:10, 18:06:37 and 20:59:42** — the first two are the s81-era session restarts the
  journal already records, and 20:59:42 is before this session started, so it is a recurring host fault, not
  a Kai-Term defect. Not filed in ISSUES.md (the file's rule: not a reproducible Kai-Term problem); kept
  here as a host constraint.
- **Standing rule for this project, restated because I broke its spirit:** the user works on this machine.
  No live key-injection or window-mapping run without asking first, even with the focus guards in
  `inj_follow.sh`. Headless coverage is the default; the live check is handed to the user to run.

**Steps 4-6 measured (exact commands):**
- `tests/test_vt.c`: 11 new checks in three blocks — the reported bug (a chrome-only burst after the re-type
  must NOT close the echo window, the snapshot must not be re-taken over our own echo, the line still reads
  NOT empty, and a second erase is proven by the burst that repaints the prompt); the ghost (strict refuses,
  `term_prompt_line_typed` returns `git com`, `term_prompt_line_is` is not fooled, and the erase proof still
  gates the write); the price (mid-line cursor: the capture sees the left half, the whole-row proof fails,
  the sequence drops — a `cd` is never written in front of the surviving half).
- `make -j8 bin/test_vt` -> exit 0, 0 warnings; `./bin/test_vt` **719 checks, 0 failures** (708 -> 719).
- `KAI_TERM_TEST_NO_WINDOW=1 make test` -> exit 0: `CLI: 62 checks, 0 failures, 10 skipped`, test_vt **719/0**,
  `dbg` **MODEL OK (0 problems)**, test_fm **235/0**, 0 warnings in the whole run.
- `make asan` -> exit 0: **719/0** + MODEL OK + **235/0**, and `grep -cE
  "AddressSanitizer|runtime error|LeakSanitizer|SUMMARY|ERROR"` over the run -> **0**.
- `grep -rnE "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/ include/ tests/` -> **empty**.

**Step 5 (the live harness) is NOT run.** The fix is proven headless against real fish through the real
`src/pty.c` (`.scratch/s86/f2d.log`, `g.log`, `m.log`) and by the 11 new checks; the in-window confirmation
is the user's to run, and I will ask rather than inject keys into their desktop.

**Next action (step 7):** README's file-manager prompt paragraph + ISSUES.md (the "not implemented yet" note
is now false; the limitation becomes the mid-line case) + AGENT.md's prompt invariant and the new pitfall, then
commit and push, then hand the user the command to confirm it themselves.

### s86 entry 3 — step 7 done: docs brought in line, clean-tree re-verification green, committed and pushed

**Docs (every quoted log string read back out of `src/main.c`, not from memory):**
- `README.md` — the Features bullet now says the shell follows the browse directory "keeping whatever you had
  already typed at the prompt". The "File manager" usage section's old paragraph ("neither line is sent unless
  the terminal is sitting at an untouched prompt", with the log line
  `…its input line is not empty`, which no longer exists) is replaced by the clear -> `cd` -> re-populate
  description: what it does, that each step waits for proof, that no newline is ever sent, that repeating the
  navigation re-erases rather than appends, the four `-d` lines it logs, and the two cases it will not write
  through (undescribable line; cursor parked mid-command) with the ghost explicitly called out as NOT one of
  them, plus the two-second drop.
- `ISSUES.md` — `## Open`: the "one behaviour the user asked for is not implemented yet" note is gone (it is
  implemented), the counts are this session's, and the s82-era explanation of the two old red checks was cut
  (they have been green since s83; the history is in git). New `## Fixed` entry for the append bug with the
  symptom, the measured cause and exactly what was verified — including the honest "not re-run in the user's
  window". The "File manager" limitation bullet was rewritten from "go only to a prompt the model can describe
  as untouched" to "writes only where the model can PROVE the line is empty", folding in the transition rule,
  the timeout, the mid-line case and the ghost exception; the duplicate bullet that edit exposed was merged
  back into one.
- `AGENT.md` — Current Status re-measured (719 / MODEL OK / 235, `make test` + `make asan` green, seven
  modified files); the stale "47 settings rows" claim corrected to **49** by re-running the set difference
  (`!strcmp(key, …)` in `src/config.c` vs the "Available settings" table -> `diff` empty both ways); a new
  "The prompt invariant" block in Architecture & Invariants (the three rules + the three predicates and what
  each may be used for); three new pitfalls in Key Decisions (a steady-state condition is not evidence of an
  event; a burst boundary is not a semantic boundary; never run the live harnesses while the user is at the
  machine — with the measured fact that the red screen was `noctalia` SIGSEGVing + niri restarting the session,
  not anything of mine); the `fm:` debug-line list extended with the whole follow sequence including the drop
  and refusal lines; Next Steps item 3 replaced (the red-checks item is long closed) with the in-window check
  to ask the user for; the file tree and the `test_vt`/`test_fm` descriptions updated to 719/235 and given the
  prompt machinery they now cover.

**Clean-tree re-verification (exact commands, from `make clean`):**
- `make clean && make -j8` -> exit 0; `grep -ciE "warning|error"` over the build log -> **0**.
- `grep -rnE "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/ include/ tests/` -> **0**.
- `KAI_TERM_TEST_NO_WINDOW=1 make test` -> exit 0: `CLI: 62 checks, 0 failures, 10 skipped`, `./bin/test_vt`
  **719 checks, 0 failures**, `./bin/dbg` **MODEL OK (0 problems)**, `./bin/test_fm` **235 checks,
  0 failures**; 0 warnings in the whole run. (No window mapped — the GPU is at 11.8/12.2 GiB and the user is
  working at this machine.)
- `make asan` -> exit 0: 719/0 + MODEL OK + 235/0, and `grep -cE
  "AddressSanitizer|runtime error|LeakSanitizer|SUMMARY|ERROR"` over the run -> **0**.

**Committed** the seven files (`src/main.c`, `src/vt.c`, `include/kai-term/kai-term.h`, `tests/test_vt.c`,
`README.md`, `ISSUES.md`, `AGENT.md`) as one commit naming the s84/s85 work it carries; `bin/` and `build/`
confirmed git-ignored (`git check-ignore bin build` -> both). `git fetch origin` first -> nothing on GitHub to
integrate, so `git push origin main` was a fast-forward (exit 0). Re-fetched and verified: `git status -sb`
shows `main...origin/main` with no ahead/behind marker and `git diff main origin/main` is empty.
Read the truth with `git status -sb` / `git log --oneline origin/main -1` — no hash or ahead-count is written
here, because the commit recording it makes it wrong.

**STILL OPEN — the one thing this session cannot do:** the follow has not been seen working in the user's own
window, and the file manager's appearance has never been confirmed by a human eye. Both belong to the user.
Ask them to run `./bin/kai-term -d`, type a partial command, `Ctrl+Shift+F`, navigate twice, and report the
`fm:` lines — do NOT inject keys into their desktop to find out.

## Previous Task (s85 — close the clear -> `cd` -> re-populate machine out: prove the completion live, document it, commit it)

**User request (2026-10-19):** "Let's continue the work on Kai-Term. If you can't suppress the command
echo while changing dir it's fine. it was not an important request." So the plan being executed is still
s84's (under "Previous Task (s84)"), and one requirement is now explicitly relaxed by the user: the
cd the machine types is echoed by the tty and is visible at the prompt. That stays as it is — it was
measured to be impossible without the shell's cooperation (ISSUES.md "The injected command cannot be hidden
from the screen") — and the user has accepted it, so it is not to be chased again.

**s85 measured start (measured this session, not inherited):**
- `git status -sb` -> `main` == `origin/main`, **four modified files**: `src/main.c` (+288/-34 of the
  diff), `src/vt.c` (+44), `tests/test_vt.c` (+101), `AGENT.md`. HEAD is the s75-s82 docs commit.
- `make -j8` -> exit 0, `grep -ciE "warning|error"` over the build log -> **0**.
- `make -j8 bin/test_vt bin/dbg bin/test_fm` -> exit 0, 0 warnings; `./bin/test_vt` **702 checks,
  0 failures**, `./bin/dbg` **MODEL OK (0 problems)**, `./bin/test_fm` **235 checks, 0 failures**.
- `nvidia-smi` -> **11840 / 12227 MiB used** — the user's local LLM still owns the GPU, so at most ONE
  window may be opened (ENVIRONMENT.md / s81 rule).
- The last change in the tree is s84 entry 5's fix: `term_docrow()` (src/main.c:953) and `fm_cd_docrow`,
  which stop the CDING step from believing the echo of our own `cd` is a new prompt. It has never been run.

**s85 objective:** take the machine from "works, but never notices it finished" to "works and says so",
then document exactly what it does and commit the whole range.

**s85 planned steps (each one: edit -> AGENT.md -> build/test; never commit red):**
1. Re-run the live harness `.scratch/s84/inj_follow.sh bin/kai-term new3` — ONE window, fish, a partial
   command typed at the prompt, `Right` into `sub/`. Require the completion line
   `fm: the prompt now holds the browser's directory and what you had typed` and NO
   `dropped the follow … (the wait ran out)`. Ground truth stays the line buffer + the filesystem +
   `fish_preexec`, not the screen. `coredumpctl list` afterwards.
2. If the completion line still does not appear, diagnose with a temporary `L_DEBUG` probe (add, run once,
   remove) rather than by widening the timeout — a timeout is not a fix for a predicate that never fires.
3. Docs, only for what the runs prove: README's file-manager prompt paragraph (the refuse rule becomes
   clear -> `cd` -> re-populate, with its limits: a line the model cannot describe is still refused, a
   shell that prints a line of its own between the erase and its next prompt makes the sequence time out
   and the log prints what had been typed, the echo is visible), ISSUES.md's "File manager" limitation and
   its "Open" note, AGENT.md's prompt invariant.
4. `make test` (with `KAI_TERM_TEST_NO_WINDOW=1` — the GPU is full) and `make asan` green, exact counts.
5. Commit the four files; then ask the user for the eye verdict (the band's appearance and the new
   behaviour) — still the one open item for the whole feature.

**s85 expected outcome:** one live run whose log ends with the completion line and whose filesystem proof
still lands in `tree/sub`; README/ISSUES describing only what is implemented; `make test`/`make asan` green
at 702 checks; one commit on `main`.

## Previous Task (s84 — finish the clear -> `cd` -> re-populate machine: review it, cover the predicates, run it live, document it)

**User request (2026-10-19):** "Continue the work from the previous session in Kai-Term." The plan being
executed is s82's (under "Previous Task (s82)"), carried forward by s83; s83 closed steps 1-2.

**s84 measured start (measured this session, not inherited):**
- `git status -sb` -> `main` == `origin/main`, **one modified file: `src/main.c`** (+234/-18). HEAD is the
  s83 docs commit; `1a0eb97` (model half) and `378f34e` (window half) are below it.
- **That `main.c` diff is s83 step 3, implemented but never journaled.** It is the whole machine the s82
  design called for: `FMCD_IDLE/ERASING/CDING/RETYPING`, `fm_cd_line/dir/sent/tab`, a `CLOCK_MONOTONIC`
  deadline (`FMCD_TIMEOUT_MS` 2000), `mono_ms()`, `fm_cd_write()` (the shared quoting + log line), the
  rewritten `fm_send_cd()` (fast path, capture path, re-target path), `fm_cd_abort()`, `fm_cd_advance()`
  called after every `term_parse()` of the owning session, `fm_cd_tick()` on the 8 ms frame timer, the
  `tab_switch()`/`tab_close()` resets, the `send_str()`/`paste_flush()` aborts, and the `fm_send_path()`
  gate changed from `term_at_prompt()` to `term_prompt_line()`. Treat it as work in progress whose result
  is known from the runs below, exactly as s73/s76/s78 each did for an un-journaled half-step.
- `touch src/main.c && make -j8` -> exit 0, `grep -ciE "warning|error"` over the build log -> **0**.
- `make -j8 bin/test_vt bin/dbg bin/test_fm` -> exit 0, 0 warnings; `./bin/test_vt` **680 checks, 0
  failures**, `./bin/dbg` **MODEL OK (0 problems)**, `./bin/test_fm` **235 checks, 0 failures**.
- `nvidia-smi` -> **11803 / 12227 MiB used** — the user's local LLM still owns the GPU, so at most ONE
  window may be opened (ENVIRONMENT.md / s81 rule).

**s84 objective:** take the machine from "written and compiles" to "reviewed, covered where it can be
covered, run live through the user's own shell, and documented" — and commit it.

**s84 planned steps (each one: edit -> AGENT.md -> build/test; never commit red):**
1. Read the whole diff against the s82 design and against what `term_at_prompt()` / `term_prompt_line()`
   actually promise (src/vt.c:756-925), and fix what is wrong. Two defects found by reading, both real:
   (a) `tab_close()` aborts only when `fm_cd_tab == idx`, but `mux_close()` shifts every LATER tab down,
   so closing a session below the sequence leaves `fm_cd_tab` naming a different shell — the machine would
   then erase, `cd` and re-type in a session that never asked for it; (b) `fm_send_cd()` returns early on
   `!strcmp(fm_last_cd, dir)` BEFORE its in-flight branch, so navigating back to the directory the shell is
   already in during a sequence leaves `fm_cd_dir` on the older directory and the shell ends up somewhere
   the browser is not — the exact invariant the user asked for.
2. `make -j8` + the three headless binaries after each fix.
3. `tests/test_vt.c`: headless checks for the predicates the machine leans on — `term_prompt_line()` after
   a Ctrl+U erase, the `input_pending` window Ctrl+U opens and the burst that closes it, and a line the
   machine must refuse (cursor mid-line). No test binary links `main.c`, so say plainly that the machine
   itself is covered by the live harness, not here.
4. `make test` (with `KAI_TERM_TEST_NO_WINDOW=1` — the GPU is full) and `make asan` green.
5. Live run, ONE window, through fish: type a partial command, navigate the band, and read the bytes the
   shell received (`.scratch/s74/inj_keys.sh` shape, uinput -> niri -> the real window). Ground truth is
   the bytes plus the `fm:` log lines, not the screen. `coredumpctl list` afterwards.
6. Docs: README's file-manager prompt paragraph (the refuse rule becomes clear -> `cd` -> re-populate, with
   its limits), ISSUES.md's "File manager" limitation and its "Open" note, AGENT.md's prompt invariant.
7. Commit; then ask the user for the eye verdict (the band's appearance and the new behaviour).

**s84 expected outcome:** the machine reviewed and its two defects fixed with 0 warnings; `make test` /
`make asan` green with a higher `test_vt` count; one live run whose byte stream shows the erase, the `cd`
and the re-typed text arriving in that order in the right session; README/ISSUES describing only what is
implemented; one commit on `main`.

### s84 entry 1 — step 1-2 done: the machine reviewed; two real defects fixed; build still 0 warnings

Read the whole `src/main.c` diff against the s82 design and against what the model actually promises
(`term_at_prompt()` = `!input_pending && prompt_row_matches()`, src/vt.c:846; `term_prompt_line()`
= "the row is the remembered prompt, then typed text ending exactly at the cursor", src/vt.c:856 — it
deliberately ignores `input_pending`, refuses when the cursor is left of where the prompt ended, when
anything right of the cursor differs, when scrolled back, on the alternate screen, and rather than
truncating). The machine matches that design: the `cd` is sent only from ERASING/CDING behind
`term_at_prompt()`, the re-type sends no CR, every advance happens after that session's `term_parse()`,
and the deadline is also checked on the 8 ms frame timer so a shell that never answers cannot hang it.

**Defect (a) — `tab_close()` left `fm_cd_tab` naming a different shell.** It aborted only when
`fm_cd_tab == idx`, but `mux_close()` keeps the array dense: every tab AFTER the closed one slides down a
slot (src/mux.c:76-77, and it re-arms `term.ps.t` for exactly that reason). Closing session 1 while a
follow belonged to session 2 left `fm_cd_tab` = 2, which after the shift is a THIRD session — the machine
would then have erased that shell's line, `cd` it and typed another session's captured text into it.
Fixed to `if (fm_cd_tab >= 0 && idx <= fm_cd_tab) fm_cd_abort("a session closed");`.

**Defect (b) — a mid-sequence navigation back to the shell's own directory was ignored.** `fm_send_cd()`
returned on `!strcmp(fm_last_cd, dir)` BEFORE its in-flight branch, so `Right` into a directory and `Left`
back out again inside the sequence window left `fm_cd_dir` on the first directory: the shell ended up in a
directory the browser had already left, which is the one invariant the user asked for ("the prompt part
should ALWAYS be in the same directory as the File Manager"). The in-flight re-target now runs first and
fires even when `dir` IS `fm_last_cd` — the line has already been erased, so the sequence still has to
finish; it simply has no `cd` left to send.

**One simplification that came out of (b):** the new `fm_cd_sent` ("the directory the last `cd` was sent
for") is removed. It and `fm_last_cd` are set together in `fm_cd_write()` and differed only in the transient
window before a `cd` was sent, and only `fm_last_cd` knows the other case that (b) exposed — the shell was
already in that directory, so no `cd` is needed. Two variables for one fact is how they drift; the machine
now compares `fm_last_cd` with `fm_cd_dir` everywhere (CDING's step, and the re-check after a re-type).

**Measured:** `make -j8` -> exit 0, `grep -ciE "warning|error"` over the build log -> **0**;
`grep -n fm_cd_sent src/main.c` -> no hits.

**Next action (step 3):** `tests/test_vt.c` checks for the predicates the machine leans on — `term_prompt_line()`
after a Ctrl+U erase, the echo window Ctrl+U opens and the output burst that closes it, and the mid-line
cursor case it must refuse.

### s84 entry 2 — step 3-4 done: 16 new `test_vt` checks pin the sequence's contract; `make test` + `make asan` green

Added a "the clear -> `cd` -> re-populate sequence, as the model sees it" block to `tests/test_vt.c`, right
after s79's `term_prompt_line()` checks. It replays **the machine's own writes** (`send_in("\025")`,
`send_in("cd /tmp/x\r")`, `send_in("git com")` with no CR) against the real parser and asserts the answer of
`term_at_prompt()` / `term_prompt_line()` at every step, so a change to the snapshot rules that would break
the feature fails a check instead of breaking it silently. `main.c` itself is in no test binary — said in the
block's comment, so nobody reads these 16 checks as coverage of the state machine.

What they pin, in order: the text is captured before anything is erased; the line is not empty so the `cd`
cannot go yet; Ctrl+U opens an echo window; the burst that puts the remembered prompt back on the SAME row
closes it (that IS the proof); the line is then empty; the CR inside the `cd` submits the line and leaves no
window open; the shell's next burst makes the new prompt the known line; the new prompt holds nothing; the
re-typed text makes the line non-empty again and reads back exactly as it was.

**Two facts that came out of writing them, both worth knowing before debugging a live run:**
- A **partially** erased line (a line editor that binds Ctrl+U to delete-word rather than erase-to-start)
  stays describable — `term_prompt_line()` happily returns `"git "` — while `term_at_prompt()` stays 0.
  That asymmetry is the safety property in one check: the `cd` is gated on `term_at_prompt()`, never on
  `term_prompt_line()`. Had it been the other way, a partial erase would have run `cd <dir>` on the half
  command left on the line.
- The snapshot names a **row**. If the shell prints a line of its own between the erase and its next prompt
  (a background job finishing), the redrawn prompt sits on another row, `prompt_cy` no longer matches, the
  erase is never proven, and the machine times out and drops the sequence — logging what had been typed.
  Pinned as its own check rather than left to be discovered in the field; it goes to ISSUES.md as a
  limitation in step 6.

**Measured (exact commands):**
- `make -j8 bin/test_vt bin/dbg bin/test_fm` -> exit 0, `grep -ciE "warning|error"` -> **0**;
  `./bin/test_vt` **696 checks, 0 failures** (680 -> 696), `./bin/dbg` **MODEL OK (0 problems)**,
  `./bin/test_fm` **235 checks, 0 failures**.
- `KAI_TERM_TEST_NO_WINDOW=1 make test` -> exit 0: `CLI: 62 checks, 0 failures, 10 skipped`, 696/0,
  MODEL OK, 235/0. (No window mapped — the GPU is at 11.8/12.2 GiB, s81's rule.)
- `make asan` -> exit 0: 696/0 + MODEL OK + 235/0, and `grep -cE
  "AddressSanitizer|runtime error|LeakSanitizer|SUMMARY|ERROR"` over the run -> **0**.

**Next action (step 5):** the live run — ONE window, fish, a partial command typed at the prompt, a browser
navigation, and the bytes the PTY received as ground truth.

### s84 entry 3 — step 5 first run: the machine REFUSED in the real window, and the reason is a resize

Live harness `.scratch/s84/inj_follow.sh` (one window, real niri, keys through uinput, fish as the shell,
`-d` log). Ground truth deliberately not the screen: fish's `fish_preexec` logs every command that RAN with
its cwd, a `Ctrl+T` bind dumps the command-line BUFFER without executing it, and kai-term's own `fm:` lines
record the machine. (Two fish-4 traps hit on the way, both harness bugs: `[(cmd)]` inside double quotes is
NOT substituted in fish 4.6 — `fish -c 'echo "x [(echo hi)]"'` prints it literally — and `$CMDLINE` in
`fish_preexec` came back empty, so the honest proof is a side effect the command has on the filesystem.)

**Result: `fm: not telling the shell to follow — its input line is not one I can describe`.** The capture
step refused, so nothing was erased and nothing was `cd`'d — the feature does not work in the very case the
user asked for.

**Root cause, measured headless with `.scratch/s84/pdiag.c`** (real `src/pty.c` + real fish + real
`term_parse()`, printing the snapshot state and a cell-by-cell diff of the remembered row against the row
under the cursor; it also had to answer the terminal queries like main.c does, because fish otherwise sits
waiting for its DA1/OSC-11 answer and never paints a prompt — worth remembering for any pty probe):
- With NO resize in between, everything works: prompt → `at_prompt=1 line=""`; type `echo restored:` →
  `term_prompt_line` returns exactly `echo restored:` (13 differing cells, first at x=30 where the prompt
  ends); Ctrl+U → the erase is PROVEN (`pending` goes 1 → 0, the row matches again, `at_prompt=1`).
- The live run differs in one step: **opening the band resizes the terminal grid** (`grid resized to 154x61`
  → `154x49`, the band costs 12 rows). `term_resize()` (src/vt.c:926-958) reads `was_clean` BEFORE the rows
  move and then either re-takes the snapshot (clean) or sets `prompt_valid = 0` (dirty). So the moment the
  user opened the band with text on the line, the snapshot was destroyed, and the capture step could only
  refuse. The old design could live with that (it refused anyway); the new one cannot — the snapshot is now
  load-bearing, and a band open / window resize / zoom is not an edge case, it is step one of the flow.

**Fix planned (next edit, `src/vt.c` only):** a resize that does not re-wrap anything must not destroy the
snapshot of a dirty line. `cols` unchanged means no line was re-broken; the cursor's row is mapped through
the resize by `grid_resize()` already, so the row under the cursor afterwards IS the line it was on before —
but that is VERIFIED, not assumed: the row's cells are copied before the resize and compared cell-for-cell
after it, and the snapshot is kept (with `prompt_cy` moved to the cursor's new row, `prompt_cx` unchanged)
only if they are identical. A width change, a row that did not survive, or a cursor that was not on the
snapshot row (a wrapped input line) still invalidates — the safe direction, unchanged.

**Next action:** make that fix, re-run `pdiag` with a band-open-shaped resize in the middle, then re-run the
live harness.

### s84 entry 4 — the resize fix is in `src/vt.c`; 6 more checks; `pdiag` now walks the whole flow against real fish

`term_resize()`: a resize no longer throws away the snapshot of a **dirty** line when it did not touch that
line. It saves the row under the cursor before the rows move and, afterwards, compares the row that ended up
under the cursor with it cell for cell (`prompt_cell_same`). Kept — with `prompt_cy` moved to the cursor's new
row and `prompt_cx` unchanged — only when the width is unchanged, the cursor was ON the snapshot row, and the
comparison is clean. A width change, a row that did not survive, a wrapped input line, or a failed
allocation still answer `prompt_valid = 0` ("unknown" → "do not write here"). The clean case is unchanged:
the snapshot is simply re-taken.

**Verified headless against real fish, not just the unit model** — `.scratch/s84/pdiag` now does
prompt → type `echo restored:` → **`term_resize(80x24 -> 80x12)`, the band opening** → Ctrl+U:
- after typing: `term_prompt_line -> 1 [echo restored:]`, `at_prompt=0`;
- after the resize: **still `1 [echo restored:]`** (it was 0 before the fix — that was the live failure);
- after Ctrl+U: `pending 1 -> 0`, the row matches again, `at_prompt=1` — the erase is proven, so the machine
  would now send the `cd`.

**New `tests/test_vt.c` checks (696 -> 702, 0 failures):** the line is describable before the band opens; a
height-only resize moves the snapshot with its row instead of discarding it; the typed text is still exactly
describable afterwards; a WIDTH change still invalidates it; a wrapped input line stays refused across a
resize (the cursor was not on the snapshot row). No existing check contradicted the change — the whole suite
still passes, which is the point of having pinned the old rule.

**Measured:** `make -j8` exit 0, `grep -ciE "warning|error"` -> **0**; `./bin/test_vt` **702 checks,
0 failures**; `./bin/dbg` MODEL OK; `./bin/test_fm` 235/0.

**Next action:** re-run the live harness (one window) with the fish config fixed — `set -l buf
(commandline)` instead of the quoted `[(commandline)]`, and a filesystem side effect (`touch
restored_marker`) as the proof that the re-typed command ran in the directory the browser moved to.

### s85 entry 1 — the timeout's root cause is the shell's GHOST TEXT, not the timeout; `term_prompt_line_is()` fixes the confirmation

Re-ran the harness (`inj_follow.sh bin/kai-term new3`, one window, fish). The behaviour is right and the
filesystem proves it: `restored_marker` was created in `tree/sub`, `fish_preexec` shows `cd …` in `tree`
then `touch restored_marker` in `tree/sub`, and the line buffer after the navigation reads
`pwd=…/tree/sub buf=[touch restored_marker]`. **But the machine still logged
`fm: dropped the follow while re-typing (the wait ran out)`** — so s84 entry 5's `fm_cd_docrow` fix did
land (the sequence now reaches RETYPING, which it did not before) and RETYPING's own predicate is what
never fires.

**Measured headless, no window** — `.scratch/s84/pdiag` extended to walk the whole machine (prompt → type
→ band-open resize → Ctrl+U → `cd` → re-type) against real fish, printing `term_prompt_line`, the new
`term_prompt_line_is`, and EVERY differing cell between the remembered row and the live one (the
first-difference-only view had been hiding the answer). The re-type step:

```
cx=35 cy=5 p_cx=14 p_cy=5   differing cells: 21
snapshot: ?..21:08:29.?.|...............................................
current : ?..21:08:29.?.touch.restored_marker|..........................
differ x=35: current ch=32 fg=ff777777 | snapshot ch=0 fg=ffe5e5e5
```

All 21 differences are the typed text itself — except the one at x=35, exactly the cursor column: a cell
fish paints in grey (`ff777777`) that the snapshot holds blank. **That is fish's autosuggestion**, and
`term_prompt_line()` requires every cell right of the cursor to equal the snapshot, so the confirmation
can never fire in fish. Measured how common that is (same probe, four texts): `touch restored_marker` and
`touch restored` → no ghost; `git com` → fish draws `mit -m "Initial commit"` in grey right of the cursor;
`ls -la /tmp` → a grey `/`. Two of four.

**Why the fix is a second predicate and not a relaxed one.** The right-of-cursor requirement exists for
the caller that is about to ERASE the line: if the user parked the cursor mid-command, Ctrl+U erases only
the left half, the right half survives, and a `cd` typed there would run on it. A ghost is indistinguishable
from that at the cell level, so `term_prompt_line()` must keep refusing — it did refuse for `git com` and
`ls -la /tmp` at the capture step, measured above. But RETYPING is not about to erase anything: it asks
"did the text I typed land?", and the text it compares lives between the end of the prompt and the cursor,
which is where typed text goes and never where a ghost is drawn.

**Changed (three files, one edit each, in this order):**
1. `include/kai-term/kai-term.h` — new `int term_prompt_line_is(Term *t, const char *text)`, documented as
   the same question with exactly one difference, and why a caller about to erase must still use the strict
   one.
2. `src/vt.c` — the body of `term_prompt_line()` becomes `static int prompt_line_text(t, out, cap,
   strict_right)` so the two entry points cannot drift; `term_prompt_line()` = strict (behaviour
   unchanged), `term_prompt_line_is()` = non-strict + `strcmp`. The right-of-cursor loop is now behind
   `if (strict_right)`; every other rule (prompt prefix must match, cursor not left of the prompt end, same
   row, not scrolled back, not the alternate screen, refuse rather than truncate) applies to both.
3. `src/main.c` — `FMCD_RETYPING` calls `term_prompt_line_is(t, fm_cd_line)` instead of describing the
   line into a buffer and comparing it by hand.

**Measured after the change:** `make -j8` exit 0, `grep -ciE "warning|error"` -> **0**; and the same probe
now answers, at the re-type step, `term_prompt_line -> 0` (still strict, still refuses a ghost line —
correct for the capture step) with `term_prompt_line_is("git com") -> 1`,
`term_prompt_line_is("ls -la /tmp") -> 1`, `term_prompt_line_is("touch restored_marker") -> 1`. At every
earlier step `term_prompt_line_is` answers 0, so it is not simply a predicate that says yes.

**Trap hit while measuring, worth recording:** the probe was recompiled WITHOUT `-D_GNU_SOURCE`, which
makes `src/pty.c` fail to compile (`implicit declaration of posix_openpt`), and `cc` left the PREVIOUS
binary in place — so the run that appeared to reproduce the bug was running the old probe. The tell: the
new `differ x=` lines were missing from the output while the old ones were there. Always check the compile
exit status before believing a probe's output (same class as the stale-sanitizer-binary trap).

**Open design question this measurement raised, for the user:** the capture step is (deliberately) still
strict, so a navigation whose line shows a fish autosuggestion is refused outright — the browser moves and
the shell is not told at all. That is the safe answer, but it means "the prompt ALWAYS follows" is not
true in fish for a partially typed command that matches a longer history entry. Making it follow would
mean erasing a line whose right half Kai-Term cannot classify, and putting it back when the erase proof
fails. Ask before building that.

**Next action:** `tests/test_vt.c` checks for the new predicate (a ghost right of the cursor: strict
refuses, `term_prompt_line_is` accepts; a mid-line cursor whose described text differs: both refuse), then
`make test` + `make asan`, then the live harness again — this time requiring
`fm: the prompt now holds the browser's directory and what you had typed`.

### s84 entry 5 — the live run WORKS, and it also caught the machine believing its own command echo

Second live run (`.scratch/s84/inj_follow.sh bin/kai-term new2`, one window, fish, ground truth = the
command-line buffer dumped by a `Ctrl+T` bind + the filesystem + `fish_preexec`'s cwd):

| what was checked | result |
|---|---|
| line buffer before the band opens | `pwd=…/tree buf=[touch restored_marker]` |
| machine's own trace | `fm: clearing the shell's input line to follow the browser into …/sub` → `fm: told the shell to follow: cd …/sub` → `fm: re-typing what was on the line: touch restored_marker` |
| line buffer after the navigation | `pwd=…/tree/sub buf=[touch restored_marker]` — **the prompt holds the browser's directory AND what the user had typed** |
| what actually ran | `cd …` in `…/tree`, then `touch restored_marker` in `…/tree/sub`, then `pwd` in `…/tree/sub` |
| filesystem | `restored_marker` exists in `tree/sub`, not in `tree` — the re-typed command ran in the new directory |

So the user's requirement is met end to end. **But the run also logged
`fm: dropped the follow while re-typing (the wait ran out)`** — the machine never noticed it had finished.

**Diagnosed with a temporary `L_DEBUG` probe in the RETYPING branch** (added, run once, removed —
`grep -rn "TEMPORARY\|PROBE\|EXPERIMENT" src/ include/ tests/` is empty again). The first probe line explains
everything:
`pr=1 now=['/home/…/tree/sub'] want=[touch restored_marker] cx=76 cy=2 pcx=33 pcy=2`
— at the moment CDING decided the new prompt had arrived, the row was still the row the `cd` had been typed
on, and the "typed text" the model described was **the quoted path of our own command**.

**Cause:** the `cd` write ends with CR, and `term_note_input()` treats a CR as "the line was submitted", so
the echo window was already closed when the tty echoed `cd '<dir>'`. The end-of-burst rule then re-took the
prompt snapshot at the end of that echo, and `term_at_prompt()` is trivially true against a snapshot taken at
the cursor. The headless probe had not caught it because there the echo, the newline and the new prompt all
arrived in ONE burst; live, they arrive in two.

**Fix (`src/main.c`):** the new prompt is only believed once the cursor has moved off the row the `cd` was
typed on — `fm_cd_docrow`, recorded in `fm_cd_write()`, compared with the new `term_docrow()` helper
(`g.row_base + g.live_off + g.cy`, the grid's own document numbering, the same identity the image anchors
use). A viewport row would not do: a prompt on the last row keeps its viewport row while the grid rotates
under it. `-1` means "this sequence sent no `cd`" (the browser moved back to the directory the shell was
already in) and carries no row requirement, so that path still completes instead of timing out and losing
the erased text. The command Kai-Term sends ends with a newline, so a shell that echoes it necessarily ends
up on a later document row; a shell that does not echo at all now fails safe (timeout → drop → the log prints
what had been typed).

**Measured:** `make -j8` exit 0, `grep -ciE "warning|error"` -> **0**; `./bin/test_vt` **702 checks,
0 failures**, `./bin/dbg` MODEL OK, `./bin/test_fm` 235/0.

**Next action:** re-run the live harness and require the completion line
`fm: the prompt now holds the browser's directory and what you had typed` with no timeout drop.

## Previous Task (s83 — executing the s82 plan: green tests, commit the verified range, then the clear -> `cd` -> re-populate machine)

**User request (2026-10-19):** "We need to continue the work on Kai-Term, Previous session should've drafted
the plan." The plan is s82's, below under "Previous Task (s82 — PLAN ONLY…)". This session implements it.

**s83 measured start (measured this session, not inherited):**
- `make -j8` -> exit 0, `grep -ciE "warning|error"` over the build log -> **0**.
- `./bin/test_vt` **678 checks, 2 failures** (`tests/test_vt.c:2410`, `:2416`); `./bin/dbg` **MODEL OK
  (0 problems)**; `./bin/test_fm` **235 checks, 0 failures**.
- `git status -sb` -> 13 modified files, HEAD `2985ee4`, `main` == `origin/main`. s75-s81 are all verified
  work sitting uncommitted.
- Re-ran `.scratch/s82/promptprobe` myself: `TEST 2410: feed 'p emoji' … pcx=4 … prompt_line=1 []` (the
  whole row was application output, so the snapshot legitimately moved to cx=4 and the typed text is `""`),
  and the realistic variant passes at the same scroll position — `variant: type emoji … live=17 pcx=2 inp=1
  prompt_line=1 [😀]`, `scrolled back -> 0`, `back at live -> 1 [😀]`. The model is right; the test setup is
  the bug. Confirmed before touching anything.

**s83 objective:** land s79's requirement — "it should first empty the prompt, cd into the new dir, then
repopulate the prompt with what was in it previously. Basically the prompt part should ALWAYS be in the same
directory as the File Manager" — without ever running a mangled command and without ever losing what the user
typed, and get the whole s75-s81 range committed.

**s83 planned steps (each one: edit -> AGENT.md -> build/test; never commit red):**
1. `tests/test_vt.c` only: rebuild the scrolled case as `feed("p ")` + `type("😀")`, and add one check pinning
   the rule the probe exposed (a row written entirely by application output IS the prompt, so the describable
   typed text is `""`). Expect ~680 checks, 0 failures.
2. Commit the verified work in three honest pieces: (a) s75-s78 file-manager/pty fixes, (b) s79's model half
   (`vt.c` + header + `test_vt.c`), (c) s81's renderer/CLI-suite/docs. `git fetch` first.
3. `src/main.c`: the `IDLE -> ERASING -> CDING -> RETYPING -> IDLE` state machine from the s82 design, the
   `fm_send_path()` gate change, the `tab_switch()` reset. Build stays 0 warnings.
4. Headless checks in `tests/test_vt.c` for the predicates the machine leans on; `make test` + `make asan`
   green (with `KAI_TERM_TEST_NO_WINDOW=1` unless the window path is under test).
5. Live run, ONE window, through fish: partial command typed, navigate the band, read the bytes the shell
   received. Ground truth is the bytes, not the screen. `coredumpctl list` afterwards.
6. Docs: README:211-217 (the refuse rule becomes clear -> `cd` -> re-populate, with its limits), README:194
   and :360 (`Return` on a file), the ISSUES.md limitation, AGENT.md's prompt invariant.
7. Ask the user for the eye verdict (the band's appearance and the new clear/re-populate behaviour).

**s83 expected outcome:** `make test` green at ~680 checks / 0 failures and `make asan` clean; navigating with
a half-typed command leaves that command intact on the new prompt in the new directory; a stuck or
interrupted sequence drops with a log line instead of typing anything; the whole s75-s81 range on
`origin/main`; README/ISSUES describe only what is implemented.

### s83 entry 1 — step 1 done: `tests/test_vt.c` is GREEN (680 checks, 0 failures); the model was never touched

Read AGENT.md + ISSUES.md first, then re-measured the tree instead of trusting the journal (numbers in the
"s83 measured start" block above), and re-ran `.scratch/s82/promptprobe` myself before editing anything — its
output is quoted there and it settles the question: the model is right, the test's setup was unrepresentative.

**Trap found while measuring, worth recording:** `make -j8` builds only `bin/kai-term` (the default target is
`all` -> `$(BIN)`); `bin/test_vt`, `bin/dbg` and `bin/test_fm` are prerequisites of `make test`, not of `make`.
So the first run after editing `tests/test_vt.c` printed the OLD 678/2 result from a stale binary
(`bin/test_vt` mtime 18:26 vs the source at 19:32). Same class as the sanitizer-prerequisite trap already in
Key Decisions. After a test-source edit, build the test targets explicitly (`make -j8 bin/test_vt bin/dbg
bin/test_fm`) or use `make test`.

**Step 1 — `tests/test_vt.c` only, no `src/` change.** The scrolled case at :2405-2416 was rebuilt the way the
check describes it: the shell prints the prompt (`feed("p ")`) and the USER types the emoji (`type("😀")`),
instead of one `feed("p 😀")` that wrote the whole row as application output. Two new checks pin the rule that
broken setup tripped over, so nobody "fixes" the snapshot logic over it next time:
`T.input_pending == 0 && T.prompt_cx == T.cx && T.prompt_cy == T.cy` after an output burst with nothing
outstanding (the snapshot re-takes at its own end), and `term_prompt_line() == 1` with `pline[0] == 0` for that
same row (a row the application wrote whole IS the prompt, and it holds no typed text — correctly).

**Measured (exact commands):**
- `make -j8` -> exit 0, `grep -ciE "warning|error"` over the build log -> **0**.
- `make -j8 bin/test_vt bin/dbg bin/test_fm` -> exit 0, 0 warnings.
- `./bin/test_vt` -> **680 checks, 0 failures** (678 -> 680: the two new checks; the 2 failures are gone).
- `./bin/dbg` -> **MODEL OK (0 problems)**; `./bin/test_fm` -> **235 checks, 0 failures**.
- `KAI_TERM_TEST_NO_WINDOW=1 sh tests/cli_test.sh ./bin/kai-term` -> `CLI: 62 checks, 0 failures, 10 skipped`,
  rc 0, no window mapped.
- `make -j8 asan` -> rc **0**: `test_vt_asan` 680/0, `dbg_asan` MODEL OK, `test_fm_asan` 235/0, and
  `grep -cE "AddressSanitizer|runtime error|LeakSanitizer|SUMMARY|ERROR"` over the whole run -> **0**.

`make test` is therefore no longer red. ISSUES.md's "Open" section still describes the red state and is
updated with the commit step.

**Next action (step 2):** commit the verified range in three honest pieces — (a) s75-s78 file-manager/pty
fixes, (b) s79's model half, (c) s81's renderer/CLI-suite/docs — after `git fetch`.

### s83 entry 2 — step 2 done: the whole s75-s81 range is committed in three verified pieces

`git fetch origin` first: `main` == `origin/main`, nothing on GitHub to integrate.

**The plan's three-way split (s75-s78 / s79-model / s81) is not expressible by file**, because four files
carry more than one session: `include/kai-term/kai-term.h` (s75 fields + s79 prototype + s81 `Render` counters),
`src/main.c` (s75/s76 routing + s81 fatal-renderer), `src/vt.c` (s75/s76/s77 gate + s79
`term_prompt_line`), `tests/cli_test.sh` (s76 config checks + s81 probe). Hunk-level staging across those was
rejected: it is exactly the splicing that has damaged files here before, and an intermediate hunk state need
not even compile. The split is therefore by file group, and each commit message names the sessions it carries:

1. `1a0eb97` — header, `src/config.c`, `src/fm.c`, `src/vt.c`, `tests/test_vt.c`, `tests/test_fm.c` (the model
   half: the `..` row, `fm_key_left`/`fm_key_right`, the prompt gate, `term_prompt_line()`, the test fixes).
2. `378f34e` — `src/main.c`, `src/pty.c`, `src/render.c`, `tests/cli_test.sh` (the window half: the key
   swallow, Left/Right, the `term_at_prompt()` gate, double-click, the pty raw-mode fix, the refused-texture
   paths, the CLI probe).
3. the docs commit — `README.md`, `ISSUES.md`, `AGENT.md`.

**Each committed tree was built and run, not assumed:** `git write-tree` -> `git archive` into
`/home/kaida/Project/.scratch/s83/c1` and `c2`, then `make -j8` there.
- `c1` (commit 1's tree): `make -j8` exit 0, 0 warnings; `test_vt` **680/0**; `dbg` **MODEL OK**; `test_fm`
  **235/0**. So the model half stands on its own — the header changes are additive and the still-uncommitted
  `main.c`/`render.c` compile against them.
- `c2` (commit 2's tree): `make -j8` exit 0, 0 warnings; `test_vt` 680/0; `dbg` MODEL OK; `test_fm` 235/0;
  `KAI_TERM_TEST_NO_WINDOW=1 sh tests/cli_test.sh` -> **62 checks, 0 failures, 10 skipped**, no window mapped.

**Damage done while verifying, recorded because it is a trap:** in `c1` I ran the CLI suite with
`KAI_TERM_TEST_NO_WINDOW=1` — and it mapped ~15 windows anyway, because that env var is honoured only by
s81's `cli_test.sh`, which lives in commit 2. The run reported `CLI: 92 checks, 0 failures, 0 skipped`.
`coredumpctl list` afterwards shows **no new core dump** (the newest `kai-term` entry is still the 17:27:30
one from s79), and the GPU was at 11807/12227 MiB, so nothing failed — but windows did appear on the user's
screen. Rule: **never run `tests/cli_test.sh` from a pre-s81 tree expecting the no-window opt-out**; check
which version of the script is on disk first.

**ISSUES.md `## Open` corrected** — it still claimed the tree was red on 2 checks. It now states the measured
green counts, keeps the explanation of why those 2 were test bugs (so the next reader does not re-diagnose
them), and records plainly that the user's clear -> `cd` -> re-populate request is **not implemented yet** —
today such a navigation is refused, which is the limitation already listed under "File manager".

**Next action (step 3):** `src/main.c` — the `IDLE -> ERASING -> CDING -> RETYPING -> IDLE` machine, the
`fm_send_path()` gate, and the `tab_switch()` reset.

## Previous Task (s82 — PLAN ONLY: finish the s75-s79 work. Planned, not implemented.)

**User request (2026-10-19):** "Prepare the plan for finishing s75-s79 work but don't do the work itself."
Nothing in this plan is implemented. The planning step itself changed only the journals — no `src/`, no
`tests/`, no README (the `src/render.c`, `src/main.c`, header, `tests/cli_test.sh` and README edits in
`git status` are s81's, already described under "Previous Task (s81)").

**s82 measured start (all of it measured, not inherited):**
- `make -j8` -> exit 0, 0 warnings. `./bin/test_vt` **678 checks, 2 failures** (`tests/test_vt.c:2410`,
  `:2416`); `./bin/dbg` MODEL OK; `./bin/test_fm` 235/0; `KAI_TERM_TEST_NO_WINDOW=1 sh tests/cli_test.sh
  ./bin/kai-term` -> 62 checks, 0 failures, 10 skipped. `make test` / `make asan` stop at those 2 checks.
- `git status -sb` -> 13 modified files, HEAD `2985ee4`. **s75, s76, s77, s78 and s81 are all verified work
  sitting uncommitted.**
- `grep -n "term_at_prompt" src/main.c` -> `fm_send_cd()` (:909) and `fm_send_path()` (:1036) still use the
  OLD gate. There is no deferred-follow state machine, no captured-line buffer, no deadline: **s79 step 3 was
  never started.** `term_prompt_line()` exists in `src/vt.c` (:856) and in the header, and `tests/test_vt.c`
  has 16 calls to it — s79 steps 1-2 are in the tree.

**Root cause of the 2 red checks — the TEST is wrong, the model is right.** Measured with
`/home/kaida/Project/.scratch/s82/promptprobe.c` (throwaway, replays the test's exact sequence and prints
`prompt_valid/prompt_cx/prompt_cy/input_pending/live_off` plus the answer of `term_at_prompt()` and
`term_prompt_line()` at every step):
- The test builds the scrolled case with `feed("p \xf0\x9f\x98\x80")` — one burst of **application output**.
  `term_parse()` re-takes the prompt snapshot at the end of every burst when `input_pending` is clear
  (src/vt.c:2839), so the whole `p 😀` row legitimately BECOMES the prompt and the typed text is `""`. The
  check expects `"😀"`, so it fails. Probe line: `TEST 2410: feed 'p emoji' … pcx=4 … prompt_line=1 []`.
- The sequence the earlier passing checks use — application prints the prompt, then the **user** types
  (`feed("p ")` then `type("😀")`) — works correctly at the scrolled bottom: probe line
  `variant: type emoji … live=17 pcx=2 inp=1 prompt_line=1 [😀]`, then `scrolled back -> 0`, then
  `back at live -> 1 [😀]`. That is exactly what `:2410`/`:2416` claim to test.
- So `vt.c`'s snapshot logic needs **no** change; the fix is the test's setup (`feed("p ")` + `type("😀")`
  instead of one `feed`). s79 step 2 is complete.

**What s75-s79 left unfinished, item by item:**
| Item | State | Evidence |
|---|---|---|
| s75/s76 band keys, `..` row, click-to-open, focus gate | done, verified | 235 `test_fm` checks, s76 live run |
| s77 prompt gate not depending on the app echoing (`input_pending`) | done, verified | s77 `.scratch/s77/livegate` + `echochk`, test_vt checks |
| s78 pty no longer forcing raw mode on every child | done, verified | s78 live Ctrl+C run |
| s79 step 1-2: `term_prompt_line()` + header + 16 checks | done, verified | probe above; the 2 red checks are test bugs |
| s79 step 3: the clear -> `cd` -> re-populate machine in `main.c` | **not started** | `fm_send_cd`/`fm_send_path` still gate on `term_at_prompt()` |
| s79 step 4: green `make test` / `make asan` | **blocked** by the 2 test checks | measured above |
| s79 step 5: README + ISSUES for the new prompt rule | **not done** | README:215 and the ISSUES limitation still describe the old refuse-the-touched-line rule |
| s79 step 6: live run through fish | **not done** | no harness, no journal entry |
| s75-s79 + s81 commits | **not done** | `git status -sb` |

**s82 objective:** land the user's s79 requirement — "it should first empty the prompt, cd into the new
dir, then repopulate the prompt with what was in it previously. Basically the prompt part should ALWAYS be
in the same directory as the File Manager" — without ever running a mangled command and without ever losing
what the user typed, and get the whole s75-s79 range committed.

**s82 design (decided before writing code):**
1. `main.c` gets a small state machine, one per window, keyed to the tab that started it:
   `IDLE -> ERASING -> CDING -> RETYPING -> IDLE`, with `fm_cd_line[FM_LINE_MAX]` (the captured text),
   `fm_cd_dir[FM_PATH_MAX]`, `fm_cd_tab`, and a `CLOCK_MONOTONIC` deadline.
   - `fm_send_cd()` keeps the fast path (`term_at_prompt()` -> send now). Otherwise
     `term_prompt_line()` captures the typed text; on 1, send `\x15` (Ctrl+U — measured s79 to be
     `backward-kill-line` in fish 4.6, `unix-line-discard` in readline and in zsh emacs mode) and go to
     ERASING. On 0, the old refusal log stays.
   - The machine advances **after each `term_parse()` of that tab's pty output**, never on a timer alone:
     ERASING -> `term_at_prompt()` true means the erase is PROVEN to have worked -> send `cd <dir>\r`;
     CDING -> the model shows a new prompt and `term_at_prompt()` is true -> retype `fm_cd_line` with no CR.
     **Safety property: the `cd` is never sent unless the erase is proven, so "runs a mangled command" is
     closed by construction, not by trusting the key binding.**
   - Abort (log + `IDLE`, `fm_last_cd` untouched): any keystroke the user sends while a sequence is in
     flight, `tab_switch()`, the tab closing, entering the alternate screen, the deadline expiring.
     Never retype captured text without re-checking `term_at_prompt()` — stale text over a new line is
     exactly the mangling this must not do.
   - `fm_send_path()` (Return on a file) stops refusing a line that merely has text: the gate becomes
     `term_prompt_line()`, and it appends at the cursor (no CR is sent, so nothing can run).
   - No new config key — the user asked for this to always happen.
2. `main.c` is in no test binary, so the honest coverage is the live harness, not a unit test. Say that in
   the journal rather than implying the machine is tested.

**s82 planned steps (each one: edit -> AGENT.md -> build/test; never commit red):**
1. `tests/test_vt.c` only: rebuild the scrolled case as `feed("p ")` + `type("😀")` at `:2405-2416`, and ADD
   one check pinning the rule the probe exposed — a line written entirely by application output is the
   prompt, so `term_prompt_line()` returns 1 with `""`. Expect `678 -> ~680 checks, 0 failures`.
2. Commit the verified work in three pieces so each is honest: (a) s75-s78 file-manager/pty fixes,
   (b) s79's model half (`vt.c` + header + `test_vt.c`), (c) s81's renderer/CLI-suite/docs. `git fetch`
   first — the user commits on GitHub too.
3. `src/main.c`: the state machine above, plus the `fm_send_path()` gate and the `tab_switch()` reset
   (next to `fm_last_cd[0] = 0` at :694). Build must stay 0 warnings.
4. Headless checks for the predicates the machine leans on (`term_prompt_line` after an erase, the
   `input_pending` interaction) in `tests/test_vt.c`; then `make test` and `make asan` green.
5. Live run, one window only (GPU rule from s81): `./bin/kai-term -d` with fish, type a partial command,
   navigate the band, and read what the shell actually received — `fc -l` inside the shell, the way
   `.scratch/s80/cwdinj.c` did — plus the `-d` lines `fm: told the shell to follow: …`. Ground truth is the
   bytes, not the screen. Check `coredumpctl list` afterwards.
6. Docs: README:211-217 (the "never sent unless at an untouched prompt" paragraph becomes clear -> `cd` ->
   re-populate, with its limits), README:194/360 (`Return` on a file), ISSUES.md limitation "The `cd` and
   the typed path go only to a prompt the model can describe as untouched" (the "queued in the tty input
   buffer" and "cannot be hidden from the screen" limits stay true), AGENT.md prompt invariant.
7. Ask the user for the eye verdict — the clear/re-populate is visible behaviour, and the file manager's
   appearance is still unconfirmed by a human.

**s82 expected outcome:** `make test` green at ~680 checks / 0 failures and `make asan` clean; navigating
with a half-typed command leaves that command intact on the new prompt in the new directory; a stuck or
interrupted sequence drops with a log line instead of typing anything; the whole s75-s81 range is on
`origin/main`; README/ISSUES describe only what is implemented.

**Explicitly NOT done in this step:** no source, test or README change. The only files written are
AGENT.md and one sentence of ISSUES.md whose status claim the probe disproved.

### s82 entry 1 — the plan, plus the root cause of the 2 red checks; no implementation

Read AGENT.md and ISSUES.md first, then measured the tree rather than trusting the journal:
`make -j8` 0 warnings; `test_vt` 678/2, `dbg` MODEL OK, `test_fm` 235/0, `KAI_TERM_TEST_NO_WINDOW=1` CLI
62/0 with 10 skipped; `git status -sb` 13 modified files at HEAD `2985ee4`.

Wrote `/home/kaida/Project/.scratch/s82/promptprobe.c` (throwaway, outside the repo; links `src/vt.c`,
`src/img.c`, `src/config.c`, no window) to replay the failing checks' sequence and print the snapshot state
at every step. Result — the model is right and the test is wrong, as recorded in the table and the root-cause
block above. It also proved the two behaviours `:2410`/`:2416` ask for already work at `live_off = 17`
(`prompt_line=1 [😀]` at the live bottom, `0` while scrolled back, `1 [😀]` again on return).

Confirmed by reading `src/main.c` that s79 step 3 does not exist: `fm_send_cd()` (:909) and `fm_send_path()`
(:1036) still gate on `term_at_prompt()`, and there is no captured-line buffer or deadline anywhere.

**Files changed this entry:** `AGENT.md` (the s82 plan, s81 moved to "Previous Task", Current Status and
Next Steps item 3 corrected, one new pitfall) and `ISSUES.md` (the Open section's claim that the red checks
were a model problem — disproved). `git status` shows `src/` and `tests/` untouched since the s81 edits
(mtimes 18:20-18:22 vs the journals at 18:54-18:55).

**Next action, for whoever picks this up:** step 1 of the plan — fix the two `tests/test_vt.c` setups, then
commit the three verified pieces before writing any new `main.c` code.

## Previous Task (s81 — refused texture uploads: fixed, documented, NOT yet committed)

**User report (2026-10-19):** "I don't know what kind of test you keep doing, but it'd be fun to stop
crashing my compositor."

**s81 measured start (before any edit this session):**
- `make -j8` -> exit 0, 0 warnings. `./bin/test_vt` **678 checks / 2 failures** (s79's in-progress
  `term_prompt_line` checks, `tests/test_vt.c:2410` and `:2416`); `./bin/dbg` MODEL OK; `./bin/test_fm`
  235/0. `git status -sb` -> 12 modified files, HEAD `2985ee4` (s75-s79 uncommitted).
- **What recent sessions put on the user's screen:** `tests/cli_test.sh` launches the real binary **15
  times** (`-e true`), and every one of those launches maps a real xdg-toplevel on the user's niri.
  s79 ran `./bin/kai-term -e true` another 20 times for the current tree plus 5 times for a
  `git archive HEAD` A/B.
- **Evidence of the damage:** `coredumpctl list` holds **85 `bin/kai-term` SIGSEGV dumps** timestamped
  17:27:14-17:27:30 — the suite plus the A/B loops (`bin/kai-term` and `.scratch/s79/head/bin/kai-term`).
- **Why it segfaults, read off the source:** `render_draw()` logs `atlas glTexImage2D failed: 0x505`
  (src/render.c:1371-1375), sets no flag, and draws anyway (`draw_quads()` at :1431). Drawing with a
  texture the GPU refused is what faults inside `libnvidia-eglcore` — s79's ASan backtrace: SEGV at
  address 0x4 in `draw_quads` <- `redraw` <- `xdg_surface_configure`. The same "log it and draw anyway"
  shape is in `img_tex()` (:516-520) and `fm_preview_tex()` (:945-948).
- **Why the upload fails at all:** `nvidia-smi` -> **11751 / 12227 MiB used**. `/Data/LocalAI/Strata/engine/strata`
  (pid 4552) holds 9740 MiB and `strata-vision` (4540) 1306 MiB — the user runs a local LLM on this GPU, so
  ~470 MiB is left for the whole desktop and even the 512x512 alpha8 atlas (256 KB) can be refused.
- **What the compositor did:** `journalctl --user` shows niri exiting cleanly twice — `niri::input: quitting
  after confirming exit dialog` at 17:43:09 and 18:06:36 — i.e. the user restarted the session twice,
  immediately after the 17:27 core-dump storm. niri logged no panic and left no core dump, so do NOT write
  here that niri crashed. What IS on record: 85 dead clients and their windows, a GPU at its limit, and a
  session the user chose to restart.

**s81 objective:** Kai-Term must never draw with a texture the GPU refused (that is the crash), and the
suite must never launch a storm of windows when there is no room for them.

**s81 planned steps (each one: edit -> AGENT.md -> build/test):**
1. `src/render.c`: mono-atlas upload failure -> free the picture caches and retry once, then report a fatal
   renderer failure instead of drawing; color-atlas failure -> fall back to mono glyphs and skip the frame;
   picture/preview upload failure -> skip that picture (both callers already treat tex 0 as "skip").
2. `src/main.c`: `render_draw()` returning -1 -> stop the loop and exit with status 1 instead of SIGSEGV.
3. `tests/cli_test.sh`: ONE preflight launch decides whether the window checks run at all; if it fails they
   are reported as skipped with the reason instead of launching 14 more windows. `KAI_TERM_TEST_NO_WINDOW=1`
   skips them on request.
4. `make`, the headless suites, `make asan`, then ONE live launch to confirm the new failure path.
5. ISSUES.md (open item, then Fixed once verified), README (the behaviour as it is), ENVIRONMENT.md (host
   fact: this GPU is shared with a local LLM — never launch a window storm).

**s81 expected outcome:** a run that cannot get video memory exits with a clear error and status 1; the CLI
suite costs one window instead of fifteen and says why it skipped; no new core dumps.

### s81 entry 1 — steps 1-2 done: no texture the GPU refused is ever drawn; a refused glyph atlas exits cleanly

Changed `src/render.c`, `include/kai-term/kai-term.h`, `src/main.c` — one rule applied at all four upload
sites: **a texture the GPU refused to give storage to must never be sampled by a draw.**

- `render_draw()` now returns **-1** when the mono atlas upload fails *after* one retry that first drops
  every picture texture (`free_picture_textures()`: the `ImgTex` cache and `fm_tex` — the model still holds
  those pixels, a glyph has no such backup). New `Render` fields `atlas_upload_fails`,
  `catlas_upload_fails`, `img_upload_fails`, `fm_upload_fails` feed `log_rate()`, which prints the first
  failure and then about one per 120 frames, because these paths are retried every frame while the GPU
  stays full.
- Color-atlas failure is **not** fatal: the texture id is deleted, `Font.catlas_broken` is set (the emit
  path in `emit_cell_glyph()` then takes the monochrome fallback that already exists for a box with no
  color font), the atlas stays dirty, and `Render.catlas_wait` paces the retry to ~1 Hz. The frame that
  failed is skipped (`return 0`) rather than drawn, because the color quads for it were already emitted
  with texcoords into the texture that does not exist.
- `img_tex()` and `fm_preview_tex()` return 0 on a failed upload instead of returning the empty texture.
  Both callers (`draw_images()`, `draw_fm_preview()`) already treat tex 0 as "skip this picture", and
  neither cache is updated, so the next frame retries.
- `redraw()` (src/main.c) turns -1 into `W.fatal = 1; W.running = 0` plus one error line
  (`kai-term: no video memory for the glyph atlas — exiting`); `main()` now returns 1 in that case and its
  final log line reads `kai-term exiting — renderer failure`. The shells still get SIGHUP from the normal
  exit path, so the process leaves an exit status instead of a core dump.

**Verified:** `make -j8` exit 0, `grep -ciE "warning|error"` over the build log -> **0**. Headless counts
unchanged: `./bin/test_vt` 678 checks / 2 failures (s79's, untouched by this change), `./bin/dbg` MODEL OK,
`./bin/test_fm` 235/0. **Not yet verified live** — that is step 4, and it needs one real window.

**Next:** step 3, `tests/cli_test.sh` — one preflight launch gates the 15 window-opening checks.

### s81 entry 2 — step 3 done: the CLI suite probes once instead of mapping fifteen windows

`tests/cli_test.sh`: before any window check, ONE probe run (`-d -e true`) decides whether they run.
- Probe exits non-zero → `win_reason="the probe run exited <rc>: <first error/failed line>"`.
- Probe never reaches `kai-term exiting` → `win_reason="the probe run never reached its clean exit"`.
- `KAI_TERM_TEST_NO_WINDOW=1` → skip without mapping even the probe window.
- No `WAYLAND_DISPLAY` → the old behaviour, now reported through the same `win_reason` variable.

The skip list at the end of the file prints `$win_reason` instead of the hardcoded "no WAYLAND_DISPLAY",
so a GPU-starved run says so out loud instead of failing 26 checks.

**Verified:** `KAI_TERM_TEST_NO_WINDOW=1 sh tests/cli_test.sh ./bin/kai-term` -> `CLI: 62 checks, 0
failures, 10 skipped`, rc 0, and **no window was mapped** (10 SKIP lines naming the reason).

**One live launch measured after the code change** (`timeout 15 ./bin/kai-term -d -e true`, GPU at
11708/12227 MiB): rc **0**, clean run, `configure answered: buffer 1253x1002 attached (grid 154x61)`,
`mux: the last session is gone, exiting`, `kai-term exiting`. So the atlas upload is NOT being refused at
this moment — the 17:27 storm happened at 11843/12227 with 20 back-to-back launches. The new failure path
therefore has to be forced, not waited for: next entry.

### s81 entry 3 — step 4 done: the new failure paths forced in an A/B build, and the suite re-measured

The GPU has room for one window right now, so the refused-upload path cannot be waited for. It was forced,
the project's A/B way, in `/home/kaida/Project/.scratch/s81/` (throwaway, outside the repo):
`render_new_forced.c` (current tree) and `render_old_forced.c` (`git archive HEAD` tree) with the mono
atlas `glTexImage2D` **deleted** and `terr = GL_OUT_OF_MEMORY` — i.e. the texture really has no texels, and
the error is reported exactly as a real refusal would be. Both built warning-free against the same sources.

- `kt_old -d -e true` -> rc **0**: logs `atlas glTexImage2D failed: 0x505 (512x512)` and **draws anyway**.
  It did not segfault in this run, so this A/B does NOT re-prove the driver fault — that evidence stays the
  85 core dumps of 17:27 plus s79's ASan backtrace (`SEGV at 0x4` inside `libnvidia-eglcore`, reached from
  `draw_quads`). What the A/B does prove is that the old code samples a texture with no texels and calls it
  a day.
- `kt_new -d -e true` -> rc **1**: `atlas glTexImage2D failed: 0x505 (512x512) — no video memory for the
  glyph atlas`, `kai-term: no video memory for the glyph atlas — exiting`, `kai-term exiting — renderer
  failure`. The session teardown still ran (`mux: the last session is gone, exiting`), so the shell was not
  orphaned.
- `sh tests/cli_test.sh ../.scratch/s81/kt_new` -> `CLI: 62 checks, 0 failures, 10 skipped`, and every SKIP
  line carries the real reason: `the probe run exited 1: kai-term error: atlas glTexImage2D failed: 0x505
  …`. One probe window, not fifteen.
- Color-atlas branch, forced the same way (`kt_catlas`, `-e 'printf "😀\n"; sleep 3'`) -> rc **0**, one
  `color atlas glTexImage2D failed: 0x505 (1024x512) — falling back to monochrome glyphs`, one
  `swap failed (1 in a row), retrying on the next frame` (the frame deliberately skipped), then a normal
  run and a clean exit. The terminal survives a refused color atlas.

**Suite state measured after all of this:**
- `make -j8` -> exit 0, 0 warnings.
- `make test` -> **rc 2**: `CLI: 94 checks, 0 failures, 0 skipped` (the probe passed, so the window checks
  ran — 15 short-lived windows, all clean), then `./bin/test_vt` 678 checks / **2 failures** (s79's
  `term_prompt_line` checks) and make stops there. `./bin/dbg` MODEL OK, `./bin/test_fm` 235/0 when run
  directly.
- `make asan` -> stops for the same 2 failures; run directly, `./bin/dbg_asan` MODEL OK and
  `./bin/test_fm_asan` 235/0 with **no** AddressSanitizer/UBSan/LeakSanitizer report.
- **Standing rule for this box, learned the hard way:** iterate with
  `KAI_TERM_TEST_NO_WINDOW=1 make test` (or `sh tests/cli_test.sh ./bin/kai-term` with that env set) unless
  the window path is the thing under test. `make test` maps a probe window plus ~14 more when the probe
  passes.

**Next:** step 5 — ISSUES.md (the refused-upload crash as a Fixed entry, the VRAM situation as a
limitation), README (the exit status and the env var the suite honours), ENVIRONMENT.md (host fact: this GPU
is shared with a local LLM; never launch a window storm).

### s81 entry 4 — step 5 done: the docs say what the code now does

- `ISSUES.md`: new **Fixed** entry for the driver-fault crash (symptom, the 85 core dumps, the cause at all
  four upload sites, what each path does now, and exactly how it was verified — forced A/B, not a real GPU
  exhaustion). The "Open" section no longer claims "None": it states the measured red state (test_vt 678/2,
  the two `term_prompt_line` checks s79 left) and the green parts. New limitation under "Rendering / input":
  video memory is shared, and what Kai-Term does at each refused upload.
- `README.md`: the Build section explains the CLI suite's single probe launch and
  `KAI_TERM_TEST_NO_WINDOW=1`; a new "Exit status" subsection documents 0 vs 1, including the two log lines
  a renderer failure ends with.
- `ENVIRONMENT.md` (outside the repo, host facts): the GPU bullet now records that the user's local LLM holds
  ~11 GiB of 12.2 GiB, how to check before opening GL windows, and what a full GPU does to a client that
  draws anyway; a new "Launching real windows in a test" section forbids launch loops, asks for one probe +
  printed skip reason, and tells the next agent to read `coredumpctl list` at the end of a GUI session.
- `AGENT.md`: the file-tree counts, the `test_vt`/`test_fm` descriptions, the `cli_test.sh` debugging bullet
  and Next Steps item 3 are brought in line with what is in the tree now.

**Nothing is committed yet this session** — `git status -sb` will show the s75-s79 work plus s81's
`src/render.c`, `src/main.c`, `include/kai-term/kai-term.h`, `tests/cli_test.sh` and the three docs.
Committing is the next action, but the tree is red on 2 checks that are not mine to fix silently.

## Previous Task (s71-s79 — integrated file manager with live previews; s79 unfinished)

**s71 objective (from `/home/kaida/Project/TODO.md`, user request 2026-10-18):** an interactive file
manager inside the terminal window — left pane browser with a highlighted selection, right pane
context-sensitive live preview (directory contents, image, text with syntax highlighting, metadata for
anything else), bottom pane the ordinary terminal whose shell follows the directory the file manager
navigates into. Arrow keys (configurable) and mouse navigation, `Ctrl+Shift+Up/Down` to move the keyboard
between the file-manager panes and the terminal pane, Enter opens, Backspace goes to the parent, plus
shortcuts for open / refresh / toggle preview / exit. No new dependencies beyond what the formats need,
no blocking UI on directory or preview loads, resource caps for big files/images, safe on permission
errors, missing files, symlinks and unsupported formats, and closing it must leave the terminal exactly
as it was.

**s71 measured start (before any edit):** `git status -sb` -> `main` == `origin/main`, no modified files.
Tree verified in s70: `make` 0 warnings, `make test` 553 checks 0 failures + MODEL OK + CLI 86/0, `make asan`
553/0 + MODEL OK. Deps present here (pkg-config, measured 2026-10-18): libpng 1.6.58, zlib 1.3.2,
**libjpeg 3.1.4.1 (libjpeg-turbo), libwebp 1.6.0, giflib MISSING** — so GIF must be decoded in-tree and
JPEG/WebP can use the system libraries. PIL 12.3.0 is available in `.scratch/` harnesses to GENERATE test
images (never to render the UI).

**s71 design (decided before writing code, follows the project's existing split):**
1. `src/img.c` (new) — image decoding in one place: PNG (libpng simplified API, moved out of vt.c),
   JPEG (libjpeg), WebP (libwebp), GIF (own LZW decoder, because giflib is absent here). Output is always
   premultiplied RGBA8, the one format the renderer already knows. vt.c's `decode_png`/`premultiply`
   become calls into it so the kitty path and the preview path cannot drift.
2. `src/fm.c` (new) — the file-manager model, no Wayland/EGL: directory scan (opendir/lstat, sort, hidden
   filter, caps), navigation state (dir, selection, top row), preview building (text + syntax highlight,
   directory listing, metadata, image), and painting the two panes into `Cell` grids. The Cell grids are
   what makes it look native: the existing renderer draws them with the existing font, colours and passes.
   The synchronous core is unit-testable; the non-blocking part is a single worker thread that fills a
   result slot and wakes the main loop through a pipe fd added to epoll.
3. `src/layout.c` — the geometry stays here (project rule): with the file manager on, the grid area splits
   into an upper band (browser | preview) and the terminal pane below it. The terminal session keeps living
   in `Layout.pane[]` so every existing hit test, focus border and `W.gx/W.gy` mapping keeps working; the
   two file-manager rects go in a new `FmLayout` inside `Layout`.
4. `src/render.c` — draw the file-manager cell grids in the SAME two batches (backgrounds, then glyphs) and
   draw the preview picture between them with the existing per-image texture path (`img_tex`), clipped to the
   preview rect.
5. `src/main.c` — input routing (FM keys only while the file manager has the keyboard, so the terminal is
   untouched otherwise), mouse click/wheel over the FM panes, focus switching, the `cd <path>` the shell gets
   when the file manager navigates, and the fm result fd in the epoll loop.
6. `src/config.c` — new settings + bindings. FM-internal keys are plain keys (`Up`, `r`, `Escape`), which the
   existing `keybind_parse` refuses on purpose, so a second entry point that allows an unmodified binding is
   needed; the tab bindings keep the refusal.
7. `tests/test_fm.c` (new, wired into `make test` + `make asan`) — dir scan, navigation, preview kinds,
   image decode for all four formats, caps and error paths, and the FM layout rects.

**s71 planned steps (each one: edit -> AGENT.md -> build/test):**
1. AGENT.md plan (this entry).
2. `src/img.c` + Makefile + vt.c switch to it; build, `make test`, `make asan` must stay clean.
3. `src/fm.c` core (scan/nav/preview/cells) + header types; `tests/test_fm.c` against it.
4. layout: FM rects + hit test + tests.
5. render: FM panes + preview image.
6. main: input, focus, mouse, cd-follow, worker thread + epoll fd.
7. config keys + README + ISSUES.md, then a live run for the user to confirm by eye.

**s71 expected outcome:** a file manager that reads as part of the terminal (same cells, font, colours,
borders), a warning-free build, a test count that rises with real coverage of the model/layout/decoders,
no open issues, and the visual verdict from the user (this project's rule: anything visual is confirmed
by their eye, not by a screenshot harness).

### s71 entry 1 — step 2 done: `src/img.c` exists, vt.c uses it, decoders measured

Created `src/img.c` (PNG via libpng, JPEG via libjpeg, WebP via libwebp, GIF decoded in-tree) and moved
`decode_png` + `premultiply` out of `src/vt.c` into it, so the kitty path and the coming preview path share
one decoder. `IMG_DIM_MAX`/`IMG_PIX_MAX` moved to `kai-term.h` (the old `IMAGE_DIM_MAX`/`IMAGE_PIX_MAX`
defines in vt.c are gone) because both paths must refuse the same picture. Makefile: `src/img.c` in `SRCS`,
`libjpeg libwebp` in `PKG`, and `build/img.o` / `src/img.c` added to the `test_vt`, `dbg` and both `_asan`
prerequisite lists (the incomplete-prerequisite trap is recorded in Key Decisions — an asan target that
does not list a source runs a stale binary).

**Measured (exact commands, this session):**
- `make clean && make` -> exit 0, 0 warnings.
- `./bin/test_vt` -> **553 checks, 0 failures** (unchanged: the graphics tests still pass through the moved
  PNG decoder); `make asan` -> **553 checks, 0 failures** + `MODEL OK (0 problems)`, no sanitizer report.
- Decoder evidence, `.scratch/s71/`: PIL 12.3.0 generated `t.png t.jpg t.webp t.gif tr.gif`, ffmpeg generated
  `ni.gif ni2.gif`; `ppm_probe.c` (links the real `src/img.c`) wrote a PPM per file and numpy compared every
  pixel against ffmpeg's own decode: **PNG, JPEG, WebP, interlaced GIF and non-interlaced GIF all maxdiff 0
  (byte-exact)**. `tr.gif` (transparent index 1) matches PIL's alpha pattern exactly — alpha 0 exactly where
  PIL says transparent.
- **Two real bugs found by that probe, both fixed:** (1) `img_decode_jpeg` used `ci.output_scanline` as the
  row index AFTER `jpeg_read_scanlines()`, which bumps it — every row landed one line low and the last row
  wrote past the end of the buffer (`free(): invalid next size`, i.e. heap corruption). The index is now
  captured before the read. (2) `gif_interlaced_row()` had the wrong pass starts/steps (0/8, 8/8, 4/4, 2/2);
  GIF's four passes are 0/8, 4/8, 2/4, 1/2 — with the wrong mapping 22 of 23 rows differed from ffmpeg.
  A Python LZW decoder written independently agreed with the C index stream, which is what proved the
  indices were right and the ROW MAPPING was wrong — PIL's own `P`-mode array is palette-remapped and is
  NOT a valid index reference (it matched only on row 0, which is how it misled at first).
- Malformed input, ASan+UBSan build of the probe: truncated GIF, `GIF89a` + garbage, a non-image file, a GIF
  header claiming 8000x8000, and a corrupt LZW stream all fail cleanly (log line, NULL return), no sanitizer
  report, no crash. The garbage header is caught by the size cap (`gif 65535x65535 px is over the limit`).

**Next action (step 3):** `src/fm.c` — the file-manager model (directory scan, navigation, preview kinds,
cell painting) plus `tests/test_fm.c`. Nothing in main.c/layout.c/render.c has been touched yet, so the
window still looks exactly as it did in s70.

### s71 entry 2 — step 3a: the file-manager types are in the header (fm.c itself is not written yet)

Added the `---------- file manager (src/fm.c) ----------` section to `include/kai-term/kai-term.h`, between
the layout section and the font section: caps (`FM_PATH_MAX` 4096, `FM_NAME_MAX` 256, `FM_ENTRIES_MAX`
8192, `FM_PREVIEW_BYTES` 256 KiB, `FM_PREVIEW_DIR_MAX` 512, `FM_PREVIEW_FILE_MAX` 16 MiB), the kind enums
(`FMK_*` what an entry is, `FMP_*` what the preview shows, `FM_FOCUS_*` which region the keyboard drives,
`FM_LANG_*` the text-preview languages), and the types `FmEntry`, `FmScan`, `FmPreview`, `FmPane`,
`FileMgr`, `FmHl`, plus every fm_* prototype step 3 will implement.

Decisions the types encode, each because the alternative was measured or reasoned out:
- **`FmEntry` stores the NAME, not the path.** 8192 entries x a 4096-byte path is 32 MB for one listing;
  `fm_entry_path()` rebuilds the full path into a caller's buffer in one snprintf.
- **A listing keeps hidden entries and marks them (`FmEntry.hidden`), and `FileMgr.vis[]` is the view
  after the filter.** Toggling dotfiles then costs a rebuild of an int array, not a second scan of the
  disk, and selection is a position in `vis[]` so it survives the toggle.
- **`FmScan`/`FmPreview` carry a `seq` request id and `fm_install_*` drops a stale answer.** Without it a
  slow scan of a directory the user already left overwrites the listing on screen.
- **`fm_scan_build_n(dir, seq, max_entries)` is the capped form; `fm_scan_build` is it with
  `FM_ENTRIES_MAX`.** So a test can pin truncation without creating 8192 files.
- **The panes are `FmPane` = a flat `Cell` array + cols/rows**, walked like a session grid, so the renderer
  needs no new drawing path — "a grid with no cursor and no selection".
- **`fm_image_fit()` returns the picture box in CELLS and lives in fm.c**, because the box is what a test
  can check; the renderer only draws the quad it is given.

Two mistakes the compiler caught while the header was still being written, both fixed in place:
`FmPreview` declared `truncated` twice (once for the directory case, once for "every kind") -> one field
whose comment names all three things it can mean; and `mode_t` was used without `sys/types.h` being
included — it compiles today only because glibc's `string.h` pulls it in, so the include is now explicit
(`.scratch/s71/modechk.c` is the check: a file that includes ONLY kai-term.h and names a `mode_t`).

**Measured:** `make` exit 0, 0 warnings (header change rebuilds the whole tree); `make test` -> 553 checks
0 failures + `MODEL OK (0 problems)` + CLI 86/0/0. No behaviour change is possible yet: no fm.c exists,
so no code calls any of this.

**Next action:** write `src/fm.c` (scan, navigation, preview builders, highlighter, cell painting), then
`tests/test_fm.c` + Makefile wiring.

### s71 entry 3 — step 3b: `src/fm.c` exists (1560 lines, compiles 0 warnings); not yet built into any binary, no tests yet

Created `src/fm.c`: the whole file-manager model, no Wayland/EGL/threads. Sections: colours (accents moved
to the header so a test can pin them; base ink/paper come from Config), formatters (`fm_human_size`,
`fm_human_time`, `fm_mode_string`), paths (`norm_path` collapses `//`, `/.`, `/a/../`; `parent_path`;
`join_path`), listing (`fm_scan_build_n` = opendir + lstat + qsort dirs-first/case-insensitive, entry cap +
`truncated`, per-entry `lstat` failure -> `FMK_MISSING` instead of dropping the name), the `FileMgr` state
(`vis[]` view after the hidden filter, `ensure_visible` scroll, `fm_go`/`fm_parent`/`fm_go_sel`/`fm_refresh`,
select/move/page/home/end), previews (`fm_preview_build`: lstat -> dir scan / image sniff+decode / text
with NUL-in-first-512-bytes = binary / non-regular file = metadata; `meta_add` builds the label-value
lines with owner/group names and the mode string), the text highlighter (14 languages, keyword/type/string/
number/comment/preproc colours, state carried across lines for C block comments and markdown fences), the
cell painters (`pane_puts` decodes UTF-8 strictly - overlongs, surrogates and out-of-range become U+FFFD
consuming one byte - and gives a `cp_is_wide()` codepoint the same `ATTR_WIDE` continuation cell vt.c
uses, so the renderer's wide rules apply to the file manager unchanged), and `fm_image_fit` (aspect
preserved in PIXELS, never upscaled, centred, returned in cells).

Request-id design, corrected while writing: one monotonic issuer counter (`FileMgr.seq`) plus
per-kind newest ids (`scan_req`, `pv_req`). The first draft bumped a single `seq` from both
`fm_go()` and `fm_preview_request()`, which made every preview request invalidate an in-flight
listing answer (`fm_install_scan` compares `s->seq == fm->scan_req`). Ids are shared so they can never
collide; the newest-of-each-kind ids are what the install functions compare against.

Five defects found by the compiler and by re-reading my own code before the first test run:
- `FmPreview` had `truncated` twice -> one field, comment names all three meanings.
- `mode_t` used with no `sys/types.h` include (worked only because glibc's `string.h` pulls it in).
- `lang_tab` initialised from `const char *kw_c[]` -> `error: initializer element is not constant`;
  the keyword lists are now `static const char kw_C[]` ARRAYS, because only an array name is an
  address constant in a static initializer.
- A `/* ... /* ... */` nested comment inside the `Lang` struct comment silently ended early.
- `fm_human_size` printed 12345 bytes as `1.2 KiB`: the old code divided by 1024 and then treated the
  result as if it were in tenths. Now the unit loop uses a scratch copy and the value is split into
  `whole = n / base` and a rounded `frac = ((n % base) * 10 + base/2) / base` -> `12.1 KiB`.
- `fm_mode_string` had an unreachable second ternary branch for the permission bits.
- `fm_set_size` compared `fm->list_cols != lc` AFTER assigning it, so a resize never reallocated; now
  the sizes are assigned first and the realloc decision is explicit (and the old grid is freed if the
  grow fails, so an OOM resize cannot leak).
- `paint_preview_text` bounded a line by `sizeof hl` (bytes) instead of the element count.

**Measured:** `cc ... -c src/fm.c` -> exit 0, **0 warnings** under the project `WARN` set (four
`-Wformat-truncation` warnings were fixed by giving the path `%s` an explicit precision in the error
lines rather than by growing the buffers - a path longer than the error line is cut, which is the
honest behaviour). `make` exit 0, 0 warnings; `make test` -> 553 checks 0 failures + MODEL OK + CLI
86/0/0 (fm.c is not in any binary yet, so the counts cannot have moved - they are re-run to prove it).

**Next action:** `tests/test_fm.c` (real temp directory tree: dirs, dotfile, dangling symlink, FIFO,
unreadable dir, 4 image formats, binary file, truncation, stale-answer drop, navigation, painting,
`fm_image_fit`) + Makefile wiring into `test` and `asan`.

### s72 entry 1 — session start: measured the real state, `make test` is RED on 2 fm checks

User request: "Continue the work on Kai-Term latest feature" -> the integrated file manager (TODO.md),
picking up at s71's step 3/4 boundary. Read AGENT.md + ISSUES.md first, then measured instead of trusting
the journal:

- `git status -sb` -> `main` == `origin/main`; modified: `AGENT.md Makefile include/kai-term/kai-term.h
  src/vt.c`; untracked: `src/fm.c src/img.c tests/test_fm.c`. So s71's steps 1-3b are in the tree,
  uncommitted, exactly as its entries say.
- `make clean && make` -> exit 0, **0 warnings** (the link line now carries `-ljpeg -lwebp` too).
- `make test` -> `test_vt` 553/0, `dbg` MODEL OK, then **`./bin/test_fm`: 188 checks, 2 failures** —
  `tests/test_fm.c:732` "paint: syntax highlighting reaches the cells (int/return)" and `:733` "and so does
  the string literal". `make test` stops there (exit 2), so `test-cli` never ran this session.

**Root cause of both failures, measured — the code is right, the test's geometry is wrong.**
`.scratch/s72/hl.c` calls `fm_hl_line()` directly on the two lines of the test tree's `code.c`: the
highlighter colours line 1 fully as a preprocessor line, and line 2 `int main(void) { /* greet */
printf("hi"); return 0; }` as TYPE(`int`,`void`) CMT(`/* greet */`) STR(`"hi"`, 4 cells) KEY(`return`,
6 cells) NUM(`0`). So highlighting works. `.scratch/s72/paint.c` then runs the model's real
request -> build -> install -> paint path and dumps the preview pane as colour letters: the pane is 40
columns wide, `paint_preview_text` starts text at x=2 and **clips a line at the pane width instead of
wrapping it** (correct behaviour for a preview pane), and `"hi"` starts at line column 33 -> x=35, so only
2 of its 4 cells fit, while `return` at line column 43 -> x=45 is off-pane entirely. Hence KEY=0, STR=2.

**Decision: fix the test, not the code.** The check's purpose is "highlighting reaches the cells"; making it
pass by asserting fewer cells would delete the claim. Give the text-preview check a preview pane wide
enough for the line, and pin the clipping behaviour with its own check so the next reader knows the cut is
deliberate.

**s72 objective:** finish the file manager — green tests, then s71's steps 4-7 (layout rects, render, main
input/focus/mouse/cd-follow/worker thread, config keys, README + ISSUES.md).

**s72 planned steps:** (1) fix the two paint checks + add the clipping check; re-run `make test` and
`make asan` and record counts. (2) `src/layout.c`: the FM band split + hit tests, `tests/test_fm.c` or
`test_vt.c` checks on the rects. (3) `src/render.c`: draw the two FM cell grids in the existing batches +
the preview picture clipped to the preview rect. (4) `src/main.c`: FM key routing, focus switching, mouse,
`cd` follow, worker thread + result fd in epoll. (5) `src/config.c` settings/bindings + README + ISSUES.md,
then a live run for the user to confirm by eye.

**s72 expected outcome:** `make test` green with the fm checks included, `make asan` green including
`bin/test_fm_asan`, and a file manager that reads as part of the terminal (same cells, font, colours,
borders) — visual verdict from the user, per this project's rule.

### s72 entry 2 — step 1 done: `make test` and `make asan` are GREEN (fm tests included); 4 test bugs fixed

The two red checks turned out to be **four separate defects in `tests/test_fm.c`**, none in `src/fm.c`:

1. **The test tree's `code.c` was written truncated.** `wfile("code.c", <literal>, 63)` — the literal is 74
   bytes, so the file on disk ended mid-word: `... printf("hi"); ` and the `return` the check looked for
   was never in the file. Fixed by a `wstr()` helper that writes `strlen(s)`; no hand-typed byte count is
   left for a text file.
2. **Two of those hand-typed counts were LARGER than their literal** — `notes.md` (14-byte literal, asked
   for 16) and `Makefile` (23, asked for 24) — i.e. `fwrite` read past the end of the string literal. UB,
   silently writing whatever rodata followed. Same fix. `binary.bin` (15-byte literal, 16 asked) only
   wrote the literal's own NUL terminator; it is now an explicit `unsigned char elf[]` with `sizeof`.
3. **The highlight check ran in a 40-column preview pane** while `code.c`'s second line is 54 columns.
   `paint_preview_text()` starts text at x=2 and CUTS at the pane edge, so `"hi"` (line column 33) lost 2
   of its 4 cells and `return` (line column 43) was off-pane. Measured with `.scratch/s72/paint2.c`, which
   runs the real request -> build -> install -> paint path in an 80-column pane and prints the pane as
   colour letters: `..TTTbbbbbbTTTTbbbbCCCCCCCCCCCbbbbbbbbSSSSbbbKKKKKKbNbbb` = TYPE `int`/`void`, CMT
   `/* greet */`, STR 4, KEY 6, NUM `0`. The check now uses an 80-column pane and pins the exact counts
   (6 and 4) instead of `>= 3`.
4. **The preview request was never re-issued in that check** — the selection had not moved, so
   `fm_preview_request()` returned 0 and the build reused the previous id (it happened to still be the
   newest, so the install passed for the wrong reason). The check now MOVES the selection to `code.c`
   (`find_pos` + `fm_select`) and asserts the request returns 1 — the path `main.c` will use.

New checks added while fixing them (188 -> 193): the tree has `code.c`; moving the selection asks for a new
preview; its answer installs; **a text line longer than the pane fills it to the right edge and does NOT
wrap onto the next row** (that cut is deliberate — wrapping would push the metadata and the rest of the file
off the pane). One existing check was corrected honestly: "a pane too short for its chrome still shows the
entries" compared row 0 against the SELECTED entry, which only held while the selection was also the top of
the view; it now compares against `vis_at(fm, fm.top)` (new test helper) because the painter starts at
`top`, and the earlier move of the selection exposed that assumption.

**Measured (exact commands, this session):**
- `make clean && make` -> exit 0, 0 warnings.
- `make test` -> exit 0: `CLI: 86 checks, 0 failures, 0 skipped`, `test_vt` **553 checks 0 failures**,
  `dbg` **MODEL OK (0 problems)**, `test_fm` **193 checks 0 failures**.
- `make asan` -> exit 0: `test_vt_asan` 553/0, `dbg_asan` MODEL OK, `test_fm_asan` **193/0**, and
  `grep -cE "AddressSanitizer|runtime error|LeakSanitizer|SUMMARY|ERROR"` over the whole run -> **0**.

**Next action (step 2):** `src/layout.c` — with the file manager on, split the grid area into the upper
band (browser | preview) plus the terminal pane below, keep the terminal session in `Layout.pane[]` so every
existing hit test / focus border / `W.gx,W.gy` mapping keeps working, and put the two file-manager rects in a
new `FmLayout` inside `Layout`. Then tests for the rects.

### s72 entry 3 — step 2 done: the file-manager band is in `src/layout.c` (geometry only, nothing visible yet)

`include/kai-term/kai-term.h` (layout section): new `FmGeom` (band `on`, list rect + `list_cols/rows`,
`prev_on` + preview rect + `prev_cols/rows`), `FM_HIT_NONE/LIST/PREVIEW`, `FM_ROWS_MIN` 3 / `FM_ROWS_MAX` 64 /
`FM_TERM_MIN_ROWS` 3, `FmGeom fm` inside `Layout`, and `fm_on` / `fm_preview_on` / `fm_rows` inside
`LayoutReq`. `layout_initial_size()` gained `fm_on, fm_rows` before the two out-params.
`src/layout.c`: `layout_compute()` now splits the grid area HORIZONTALLY first — a band of `fm_rows` cell
rows at the top (browser left half, preview right half, one `gap` between, remainder on the right like every
other whole-cell remainder), then the terminal pane(s) get `pane_y = grid_y + band_h + gap` and
`pane_h = grid_h - band_h - gap`. The sessions stay ordinary `pane[]` entries, only shorter, which is what
keeps `layout_hit_pane`, `layout_pane_of_tab`, `layout_grid_size`, the focus border and `W.gx/W.gy` working
unchanged with the band on AND off. New `layout_hit_fm()` (must be checked BEFORE `layout_hit_pane()`).
The band is clamped so the shell keeps `FM_TERM_MIN_ROWS` rows, and dropped entirely when even that leaves
less than `FM_ROWS_MIN` rows — the tab bar's "the terminal wins" rule, applied again. Bounds are clamped in
layout.c, not config.c, so no caller can ask for a 1-row band.

**Measured:** `make` exit 0, 0 warnings. `make test` -> CLI 86/0/0, `test_vt` **576 checks 0 failures**
(553 -> 576: 23 new fm-layout checks), `dbg` MODEL OK, `test_fm` 193/0. One check failed first and the TEST
was wrong: it probed `list_y + list_h + 1`, which is inside the 8 px gap between the band and the terminal
pane, and expected `layout_hit_pane` to say "pane" — it correctly says -1. Split into two checks: the gap
belongs to neither, and a point at `pane[0].y + 1` is the terminal and only the terminal.

What the new checks pin: band starts at the top margin and is `fm_rows` rows tall; the half-split plus the
gap fills the band exactly; each pane's cols x rows come from `term_grid_geometry()` on its own rect; the
terminal pane keeps the FULL width (the band only costs height) and every session is sized to the terminal
pane (115x45 in the 936x1002 example, not 115x58); hit tests for list / preview / the gap between them /
below the band / the top margin; preview off -> the browser owns the band; an oversized `fm_rows` shrinks so
the terminal keeps exactly `FM_TERM_MIN_ROWS`; a 120 px window drops the band and the terminal gets the whole
grid area (6 rows) back; and the startup round trip `layout_initial_size(..., fm_on=1, fm_rows=12, ...)` ->
`layout_compute()` returns the configured 80x30 for the terminal pane.

`src/main.c` passes `0, 0` for the two new `layout_initial_size()` arguments for now — the file manager is
off at startup and `main.c` has no fm state yet. That line is part of the main.c wiring step.

**Next action (step 3):** `src/config.c` — the settings (`file_manager`, `fm_rows`, `fm_preview`, arrow-key
bindings, the toggle/focus/quit/open/refresh/hidden bindings) and the second keybind entry point that allows
unmodified keys for FM-internal shortcuts, since `keybind_parse` deliberately refuses them for terminal keys.

### s72 entry 4 — step 3 done: the file manager's settings and bindings are in `src/config.c`

`include/kai-term/kai-term.h`: `FmKeys` struct (up, down, pageup, pagedown, home, end, open, parent,
refresh, preview, hidden, quit), `Config.fm_on/fm_rows/fm_preview/fm_show_hidden/fm_follow`,
`Config.key_file_manager/key_fm_focus/key_fm_release`, `Config.fm_key`, and the prototype
`keybind_parse_plain()`.
`src/config.c`: `keybind_parse` is now a wrapper over a new static `keybind_parse_into(spec, out,
allow_plain)` — one parser, two policies, so the FM keys cannot drift from the strict one's other rules
(unknown key name, two keys, no key). `keybind_parse_plain` allows `mods == 0`, which is safe ONLY because
those bindings are matched just while the file manager owns the keyboard. `set_fm_keybind()` mirrors
`set_keybind()` (empty/none/off/no unbinds; an unparseable value keeps the current binding).
Defaults: FM off, `fm_rows = 12`, preview on, dotfiles hidden, `fm_follow = 1` (navigating sends
`cd <path>` to the shell), `Ctrl+Shift+F` toggle, `Ctrl+Shift+Up` / `Ctrl+Shift+Down` focus switch, and
bare `Up Down Page_Up Page_Down Home End Return BackSpace r p . Escape` for the FM itself.
15 new config keys: `file_manager, fm_rows, fm_preview, fm_show_hidden, fm_follow, key_file_manager,
key_fm_focus, key_fm_release, fm_key_up, fm_key_down, fm_key_pageup, fm_key_pagedown, fm_key_home,
fm_key_end, fm_key_open, fm_key_parent, fm_key_refresh, fm_key_preview, fm_key_hidden, fm_key_quit`
(20 counting every fm_key_*).

**Measured:** `make` exit 0, 0 warnings. `make test` -> `test_vt` **613 checks 0 failures** (576 -> 613: 8
`keybind_parse_plain` checks + 29 file-manager config checks), `dbg` MODEL OK, `test_fm` 193/0.
`make test-cli` -> **CLI: 92 checks, 0 failures, 0 skipped** (86 -> 92: the FM keys are known, two `none`
bindings log their unbind, and a default run never mentions a file manager).

**Next action (step 4):** `src/render.c` — draw `Layout.fm.list` / `Layout.fm.prev` cell grids in the same
two batches the terminal uses, and the preview picture between them clipped to the preview rect, using
`fm_image_fit()` for the box. Then `src/main.c`: the `FileMgr` instance, the worker thread + result fd, key
routing, mouse, `cd` follow, and `fm_set_size()` from `layout_apply()`.

### s73 entry 1 — session start: measured the real state, the tree does NOT build (step 4 was half-applied)

User request: "Continue adding the new feature the Kai-Term" + "Don't stop until you have a question or are
done with the feature integration." -> continue the integrated file manager (TODO.md), picking up at s72's
step 4 (render.c).

**s73 measured start (before any edit this session):**
- `git status -sb` -> `main` == `origin/main`; modified: `AGENT.md Makefile include/kai-term/kai-term.h
  src/config.c src/layout.c src/main.c src/vt.c tests/cli_test.sh tests/test_vt.c`; untracked: `src/fm.c
  src/img.c tests/test_fm.c`. So s72's steps 1-4 (img.c, fm.c, test_fm.c, layout band, config keys) are in
  the tree, uncommitted, as its entries say.
- `make -j8` -> **FAILS, 2 errors**: `src/render.c:766: error: conflicting types for 'render_draw'` (the
  header at `include/kai-term/kai-term.h:1057` already declares the 5-argument form with
  `const FileMgr *fm`) and `src/main.c:409: error: too few arguments to function 'render_draw'`. So the
  header half of step 4 (render_draw prototype + `Render.fm_tex/fm_tex_uid/fm_tex_w/fm_tex_h/fm_px/
  fm_px_cap`) is already applied, but `src/render.c` has **zero** fm references (`grep -n fm src/render.c`
  -> empty) and `src/main.c` has no fm state. That is an interrupted step 4, not recorded by s72's entries —
  treat it as work in progress whose result is known: the build is red until render.c and main.c catch up.
- Nothing else is broken: `src/layout.c` and `src/config.c` diffs match s72 entries 3 and 4, `src/main.c`
  carries only the `layout_initial_size(..., 0, 0, ...)` placeholder line.

**s73 objective:** finish the file-manager integration — make the tree build again, draw the band, wire the
UI (keys, mouse, focus, `cd` follow, worker thread + result fd), keep `make test`/`make asan` green with
real coverage, and bring README/ISSUES.md in line with what actually exists.

**s73 planned steps (each one: edit -> AGENT.md -> build/test):**
1. `src/render.c`: accept `const FileMgr *fm`; emit the two FM cell grids through the existing three passes
   (backgrounds into batch A, glyphs into batch B) and the preview picture between the batches, clipped to
   `Layout.fm.prev`, sized by `fm_image_fit()`; one texture for the preview keyed on `FmPreview.uid`.
2. `src/main.c`: the `FileMgr` instance, `fm_set_size()` from `layout_apply()`, `LayoutReq.fm_*`, the
   toggle/focus/release bindings, FM key routing while the FM owns the keyboard, pointer click + wheel over
   the two panes, the `cd <path>` follow, and the worker thread + result fd in the epoll loop.
3. Tests for whatever is testable headlessly (the render/main halves are not in any test binary, so the
   honest coverage is `tests/cli_test.sh` for the new config surface + `test_fm`/`test_vt` for the model).
4. README (feature, new config keys, libjpeg/libwebp in Requirements) + ISSUES.md limitations.
5. `make clean && make`, `make test`, `make asan` from a clean tree; exact counts recorded; then a live run
   for the user to confirm by eye (this project's rule for anything visual).

**s73 expected outcome:** `make` 0 warnings, `make test` + `make asan` green including `bin/test_fm_asan`, a
file manager that reads as part of the terminal (same cells, font, colours, borders), docs that match the
tree, and the visual verdict from the user.

### s73 entry 2 — step 1 done: `src/render.c` draws the band (compiles 0 warnings; the link is still red until main.c catches up)

`render_draw` now takes the 5th argument the header already declared (`const FileMgr *fm`; NULL or
`fm->on == 0` draws no band). What was added:

- **`emit_cell_glyph()`** — the pass-3 glyph body (color-emoji cluster path + mono path + `ATTR_DIM`
  + `cluster_is_wide()` span) lifted out of `render_draw` into one function, called by the session panes
  AND by the two band panes. Reason: a file name can hold any codepoint, so the band needs the same
  emoji/wide rules; a second copy is how they would drift. The session call site keeps its own
  inverse/cursor/selection colour resolution and passes `fg/bg` in already resolved.
- **`emit_border()`** — the 4-quad focus frame, factored for the same reason; the side-by-side pane
  border now calls it (identical geometry, 2 px, drawn in the gap outside the rect).
- **Band pass 1 + pass 3** (`fm_pane_rect()` / `fm_pane_prepare()` / `FmPaneDraw`): the two `FmPane`
  grids go through the SAME two batches as a session grid — backgrounds into batch A, glyphs into
  batch B — with the same remainder rule (`pad_gl = vh - rect_y - nrows*ch`) and the same defensive
  clamp of cols/rows to what the rect can hold. Indexing uses the grid's own stride (`FmPane.stride =
  FmPane.cols`), not the clamped column count.
- **`fm_preview_tex()` + `draw_fm_preview()`** — one texture for the preview picture (at most one
  preview exists, so no cache), re-uploaded when `FmPreview.uid`/`w`/`h` change; `fm.c` hands out plain
  RGBA so the copy in `Render.fm_px` is premultiplied with `img_premultiply()` before upload. The quad
  is placed by `fm_image_fit()` — the CELL box positions it, the PX box is what is drawn, centred in it,
  so the ceil()-into-cells slack cannot stretch the aspect — clipped to `Layout.fm.prev`, composited
  against the preview pane's own cell background, drawn between the batches like any other picture
  (counted into the existing `graphics: N image(s) drawn` line).
- **Focus border**: with the band on, exactly one region gets the 2 px frame — the browser, the preview,
  or (when the file manager has the band but not the keyboard) the terminal pane. With the file manager
  off the rule is unchanged: side-by-side only.
- `render_destroy` deletes `Render.fm_tex` while the context is current and frees `Render.fm_px`.

One defect found by re-reading the edit before compiling: the band pass-3 background colour first came
out as a nonsense triple `col2f` (an unfinished inverse branch). Replaced with the same swap pass 1 does.

**Measured:** `make build/render.o` -> exit 0, **0 warnings** under the project `WARN` set. The full link is
still red: `src/main.c:409` calls `render_draw` with 4 arguments. `make test` cannot pass until main.c is
wired (it does not link render.c at all, so the fm/vt/mux/layout tests are unaffected by this edit — they
were green at s72 entry 4 and nothing they link changed).

**Next action (step 2):** `src/main.c` — the `FileMgr` instance, the worker thread + result fd, key and
pointer routing, focus, `cd` follow, `fm_set_size()`/`fm_paint()` from the layout/frame path, and the
`layout_initial_size()` arguments.

### s73 entry 3 — step 2 done: `src/main.c` wires the file manager; the binary links and `make test` is green

**`src/fm.c` + header — two functions main.c needs rather than re-derives:**
- `fm_set_hidden(fm, show)` — dotfiles on/off by rebuilding `vis[]` from the listing already in memory
  (no second `opendir`), clamping the selection into the new view and marking the model dirty. Without it
  the `.` shortcut would have had to call `fm_refresh()` and re-read the disk.
- `fm_row_pos(fm, row)` — the list pane's row -> view position, using the painter's own `list_metrics()`.
  A click must select the entry drawn under it, including in a pane too short for its header/footer where
  the entries start at row 0; that rule belongs next to the painter, not in main.c.

**`src/main.c` — the wiring (all new code is in one section, "the in-window file manager: the wiring"):**
- `static FileMgr g_fm;` plus the worker state: two request slots (`FMQ_SCAN`/`FMQ_PREVIEW`, each holding
  only the NEWEST request of that kind — a superseded request is overwritten, so the disk is not even
  read for it), a mutex + condvar, a quit flag, and a pipe whose read end joins epoll.
- `fm_worker()` runs **only** `fm_scan_build()` / `fm_preview_build()` — the two fm.c functions that touch
  nothing but malloc, the filesystem and their argument — so it never reads or writes `FileMgr`. The main
  thread keeps ownership and installs every answer, which is why the request-id rule in `fm_install_*()`
  is the only synchronisation the model needs. Answers cross the pipe as one 24-byte `FmResult` (atomic,
  far under `PIPE_BUF`); a write that does not land is freed on the spot rather than leaked.
- `fm_worker_start()` is **lazy**: no thread and no descriptor exist until the band is first opened, so a
  session that never uses the feature runs exactly the code it ran before. `fm_shutdown()` sets quit,
  broadcasts, joins, then drains the pipe (draining AFTER the join is what makes it leak-free) and frees
  the model.
- `layout_apply()` now fills `LayoutReq.fm_on/fm_preview_on/fm_rows` and calls `fm_set_size()` with the
  band's rects — the one place that knows them. `redraw()` calls `fm_paint(conf.fg, conf.bg)` when
  `g_fm.dirty`, after the sizing and before `render_draw(..., &g_fm)`.
- Keyboard: `key_file_manager` (toggle), `key_fm_focus` (Ctrl+Shift+Up -> browser), `key_fm_release`
  (Ctrl+Shift+Down -> terminal) are matched with the tab bindings and work from anywhere. `fm_keys()` is
  matched just before the PTY paths and only while the band owns the keyboard — that gate is what makes
  the bare `Up`/`r`/`Escape` defaults safe. Movement keys return 1 (holding one walks the listing, and the
  existing key-repeat machinery does it); open/parent/refresh/preview/hidden/quit return 0.
- Pointer: `layout_hit_fm()` is checked in `ptr_button()` BEFORE the mouse-reporting branch (the bar's
  rule — an application that grabbed the mouse must not eat the browser). A click in the browser selects
  the row under the pointer via `fm_row_pos()`; a click in the preview takes the keyboard; a click in a
  terminal pane hands the keyboard back (`fm_focus_set(FM_FOCUS_TERM)`), so pointer and keyboard never
  disagree. `fm_wheel()` runs first in `ptr_scroll_done()`: over the band the wheel walks the listing
  instead of scrolling scrollback.
- Shell follow: `fm_send_cd()` writes `cd '<path>'\r` to the active session, single-quoted with the
  shell's `'\\''` escape (shared `shell_quote()` helper with the path-typing action), and never to a
  session on the alternate screen — vim/htop/less would receive it as typing.
- **Enter on a file types its quoted path at the prompt WITHOUT a newline.** Kai-Term does not guess what
  "open" means for a file and never runs one. (Flagged for the user: if they want Enter to actually
  launch something, that is a one-line change with different safety implications.)
- Startup: `fm_init(&g_fm, wdir)` — the browser starts where the shell starts; `layout_initial_size()`
  gets `conf.fm_on, conf.fm_rows`; with `file_manager = 1` the first listing is requested right before
  the loop, once the worker and its fd exist.

**Makefile:** `src/fm.c` was missing from `SRCS` — the first link of the real binary failed with ~30
`undefined reference to fm_*`. It had been compiled only through the `bin/test_fm` pattern rule, which is
why s72's green `make test` never noticed. Added to `SRCS` (order: after `src/layout.c`). No `-lpthread`
was needed: glibc 2.43 has pthread in libc (measured with a standalone `pthread_create` probe compiled
without `-lpthread`).

**Two mistakes made and fixed in this session, both recorded because they are traps:**
1. An `edit_file` anchored on the literal text `int main(void)` and matched the **string literal** for
   `code.c` inside `make_tree()` (line 226) instead of the real `main()` at line 875, injecting a whole
   function into a test fixture. Repaired with a Python pass that cut the block out and re-inserted it
   before the real `main()`; the fixture literal was restored byte-for-byte. Rule: anchor on something
   unique (`\nint main(void)\n{\n    kai_term_loglevel`), never on a bare identifier that also appears in
   test data.
2. A new check asserted that toggling dotfiles back ON restores the previous selection. It does not, and
   the model is right: the selection is a position in the view, so it stays where the clamp left it. The
   check now pins that behaviour instead of the one I had assumed.
3. Also fixed a pre-existing `-Wparentheses` warning in `test_fit` (`A || B && C` — the meaning was
   already right, the parentheses were missing). It was in the tree at s72's close despite the
   "0 warnings" entry, because `make test` output was being filtered when the count was read.

**Measured (this session, after the wiring):** `make -j8` -> exit 0, `bin/kai-term` links. `make test` ->
CLI 92/0/0, `test_vt` 613/0, `dbg` MODEL OK, `test_fm` **220 checks 0 failures** (198 -> 220: the 22 new
`fm_set_hidden`/`fm_row_pos` checks). Note `test_fm` was already 198 at session start, not the 193 s72
entry recorded — the tree carried 5 more checks than the journal said (the interrupted step-4 session),
which is another reason to measure rather than trust.

**Next action:** clean-tree re-verification (`make clean && make`, `make test`, `make asan` with exact
counts), then README + ISSUES.md, then a live run for the user's eye.

### s74 entry 1 — session start: measured the real state; the feature is wired and green, the docs and the user's eye are what is left

User request: "continue working on the Kai-Term Project" -> finish the integrated file manager (TODO.md),
picking up at s73's handoff ("clean-tree re-verification, then README + ISSUES.md, then a live run").

**s74 measured start (before any edit this session):**
- `git status -sb` -> `main` == `origin/main` (HEAD `f75be90`); modified: `AGENT.md Makefile
  include/kai-term/kai-term.h src/config.c src/layout.c src/main.c src/render.c src/vt.c
  tests/cli_test.sh tests/test_vt.c`; untracked: `src/fm.c src/img.c tests/test_fm.c`. Matches s73's
  description — steps 1-4 (img.c, fm.c, test_fm.c, layout band, config keys, render band, main wiring)
  are all in the tree, uncommitted.
- `make clean && make -j8` -> exit 0, **0 warnings** (link line carries `-lpng16 -lz -ljpeg -lwebp`).
- `make test` -> exit 0: `CLI: 92 checks, 0 failures, 0 skipped`, `test_vt` **613 checks 0 failures**,
  `dbg` **MODEL OK (0 problems)**, `test_fm` **220 checks 0 failures**.
- `make asan` -> exit 0: `test_vt_asan` 613/0, `dbg_asan` MODEL OK, `test_fm_asan` 220/0, and 0 lines
  matching AddressSanitizer|runtime error|LeakSanitizer|SUMMARY|ERROR over the whole run.
- **The feature runs for real** (log evidence, not a screenshot): `./bin/kai-term -c <conf with
  file_manager = 1> -d -w .scratch/s74/tree -e 'sleep 4'` logs `fm: worker started (result fd 35)`,
  `fm: dropping stale preview of … (request 2, newest 3)` (the request-id rule firing on a real race),
  and `graphics: 1 image(s) drawn, 0 texture(s) held` — the PNG in that directory reached the preview
  texture and the draw path. The `-d` grid line proves the band costs the terminal its rows: 154x51 with
  `fm_rows = 10` in a 1253x1002 window (61 rows without the band).

**s74 objective:** close the feature out — the two things s73 left undone are (a) documentation that
matches the tree (README has no file-manager section, no `fm_*` settings, no libjpeg/libwebp in
Requirements; ISSUES.md has no file-manager limitations) and (b) the user's eye on it. Also close the
evidence gap that the *interactive* half (toggle binding, FM keys, focus handover, mouse) has never been
exercised: nothing in any test binary links `main.c`, so the routing gate — "the terminal is untouched
unless the band owns the keyboard" — is currently unverified.

**s74 planned steps (each one: edit -> AGENT.md -> build/test):**
1. Add the missing debug log line for the `cd <path>` the shell gets (`fm_send_cd()` performs a
   `pty_write` and logs nothing, so the follow-the-directory behaviour has no client-side evidence).
2. Live key-injection run (`.scratch/inject`, uinput -> niri -> the real window) with `-d`: toggle the
   band open, walk the listing, Enter on a directory (navigation + `cd`), `Ctrl+Shift+Down` to release
   the keyboard, then prove the bare `Up`/`Down` keys reach the PTY again (ground truth = the bytes the
   PTY received, per ENVIRONMENT.md, not the screen), then close it. This is the A/B of the routing gate.
3. README: a "File manager" feature bullet + a usage section (keys, mouse, what Enter does), the 20
   `fm_*`/`file_manager`/`fm_rows`/`fm_preview`/`fm_show_hidden`/`fm_follow`/`key_*` settings rows, and
   libjpeg + libwebp in Requirements.
4. ISSUES.md: a "File manager" limitations group (what is and is not implemented, the caps), and refresh
   the stale "Open" verification line with this session's counts.
5. `make clean && make`, `make test`, `make asan` from a clean tree with exact counts; commit; hand the
   user a live command and record their verdict.

**s74 expected outcome:** docs that describe only what exists, a green clean-tree run, one commit on
`main`, and the visual verdict from the user — this project's rule for anything visual.

### s74 entry 2 — two log lines made honest, and a live injection run found a REAL input-routing bug

**Edits made (both first, then measured):**
- `src/main.c` `fm_send_cd()`: it performed a `pty_write` and logged nothing, so the
  follow-the-directory behaviour had no client-side evidence at all. Now logs
  `fm: told the shell to follow: cd <dir>` AFTER the write, naming the directory the model actually holds
  (the project's "a log line that prints its own argument can lie" rule — the alt-screen gate can refuse).
- `src/render.c`: the `graphics: N image(s) drawn, M texture(s) held` line was folding the file-manager
  preview into the application-picture count, while `M` counts only the `ImgTex` cache and the preview has
  its own texture (`Render.fm_tex`). Measured: a preview alone logged
  `graphics: 1 image(s) drawn, 0 texture(s) held` — a number that contradicted itself. The two are now
  counted and logged separately: `... 0 image(s) drawn, 0 texture(s) held + the file-manager preview`.

**Live run (`.scratch/s74/inj_test.sh`, the project's own technique): real `bin/kai-term` in the real
niri session, keys injected through uinput (`.scratch/inject`), ground truth = the bytes the PTY received
(`-e 'stty raw -echo; cat > REC'`), not the screen.** `stty raw` was needed: in canonical mode the tty
line discipline holds every line until Enter, so the arrow bytes would not have appeared in order.

PTY received, in order: `ab` `'/home/kaida/…/tree/b.jpg'` `^M` `^[[A^[[B` `z^M` `'/home/kaida/…/tree/a.png'` `^M`
Log: `fm: opened on …`, `fm: typed the selected path at the prompt: …b.jpg`, `fm: keyboard -> terminal`,
`fm: closed — the terminal is the whole window again`, `fm: opened on …` (reopening keeps the listing).

**What that proves (all good):** `Ctrl+Shift+F` opens and closes the band; while the band owns the keyboard
`Down` `Down` never reach the PTY; the selection really moved (Enter typed `b.jpg`, two entries down from
`sub`); `Ctrl+Shift+Down` hands the keyboard back and the very next `Up`/`Down` arrive as `ESC[A ESC[B`;
ordinary typing works after closing; the band survives close/reopen.

**The bug it found — every one-shot file-manager key ALSO reached the shell.** `send_key()`'s return value
means "is this press repeatable" (1 = arm the key repeat, 0 = handled one-shot), and falling off the
function is what sends the key to the PTY. The call site was `if (fm_keys(base, kbmods)) return 0;`, and
`fm_keys` returned 0 for BOTH "not one of mine" and "handled, do not repeat" — so the one-shots fell
through. Measured consequences: **Enter on a file typed its quoted path and then sent `^M`, i.e. the shell
would have EXECUTED it** — the exact thing the design said it would never do; `BackSpace` would go to the
parent AND delete a character at the prompt; `r`/`p`/`.` would type themselves at the prompt; `Escape`
would close the band AND send ESC to the shell. Two meanings in one int, and the compiler could not see it.

**Fix:** `fm_keys()` now has three states — `-1` not the band's key, `0` handled and must not repeat,
`1` handled and repeatable — and the call site is `int fmk = fm_keys(...); if (fmk >= 0) return fmk;`.
The movement keys keep auto-repeat (holding an arrow walks the listing); the one-shots are now consumed.

**Also found in the same run (a test-script bug, not a product bug):** step 10 pressed `Up` once expecting
to land on the directory `sub`, but the selection was on `b.jpg`, so it landed on `a.png` — the `cd`
follow was therefore not exercised. The re-run presses `Up` twice and adds BackSpace / `r` / `p` / `.` /
`Escape` checks, because those are exactly the keys this bug affected.

**Next action:** rebuild, re-run the injection test against the fixed binary (A/B against the pre-fix
`src/main.c` copy in `.scratch/s74/` if the byte stream alone is not convincing), then README + ISSUES.md.

### s74 entry 3 — the second live run found a WORSE bug: every listing after a navigation was thrown away

Re-ran with a script aimed at exactly the keys the first bug affected
(`.scratch/s74/inj_keys.sh`: Return, `r`, `p`, `.`, Escape, BackSpace, plus the focus handover both ways).

**The one-shot routing fix works.** PTY bytes for the whole run:
`ab` `'/home/kaida/…/tree/b.jpg'` `cd '…/tree'^M` `cd '…/tree/sub'^M` `cd '…/tree'^M` `^[[A^[[B` `z^M`
— no `^M` after a typed path (Enter no longer executes it), no `r`, no `p`, no `.`, no `ESC`, no `^H`.
The `^[[A` after Escape and the `^[[A^[[B` after `Ctrl+Shift+Down` prove the keyboard really returns to the
shell. `fm: open -> …/sub`, `fm: parent -> …/tree`, `fm: told the shell to follow: cd …` all appear.

**But the log exposed a second, more serious defect:** every listing issued after a navigation carried
request id 0 — `fm: dropping stale listing of …/tree/sub (request 0, newest 9)`. Cause:
`fm_nav(fm_go_sel(&g_fm, &seq), seq, "open")`. C evaluates function arguments in an **unspecified
order**, and gcc reads the caller's `unsigned long seq = 0` BEFORE `fm_go_sel()` fills it. Proved in
isolation with `.scratch/s74/argorder.c` (same shape, `-O2` and `-O0`): prints `seq=0`. Consequence:
`fm_install_scan()` compares the answer's id with `fm->scan_req`, they never match, so **the listing of
every directory the user navigated into was discarded and the browser kept showing the directory they had
left** — while the model's own `dir` had moved, so the header, the preview and the `cd` were all for a
directory whose entries were never displayed. Compiles with zero warnings under the project's `WARN` set.
Fixed at all three sites (`open`, `parent`, `refresh`): the model call now happens in its own statement
and `seq` is read afterwards. `grep` over `src/` finds no other out-param-shared-with-arguments call.

**Also visible in that byte stream, and wrong:** pressing `r` (refresh) typed `cd '<current dir>'\r` at
the prompt — `fm_nav()` sends the `cd` unconditionally, and a refresh is a navigation that does not change
the directory. Typing and executing a command at the user's prompt because they asked to re-read a listing
is not acceptable; the `cd` must only go out when the directory actually changed.

**Next action:** make `fm_send_cd()` fire only on a real directory change, rebuild, re-run
`inj_keys.sh` and require: no `cd` line for `r`, and no `dropping stale listing` for a navigation.

### s74 entry 4 — all three fixes verified by the same harness; the suite is still green

Third fix: `fm_send_cd()` now keeps `fm_last_cd` (the directory the shell was last sent to, declared with
the other file-manager state because `tab_switch()` resets it) and returns early when the directory has
not changed. `fm_last_cd` is seeded from `g_fm.dir` at startup — the shell already starts there — and
cleared by `tab_switch()`, so the session that becomes visible is re-synced by its next navigation
instead of being skipped because some other session had been told.

**Re-run of `.scratch/s74/inj_keys.sh` against the rebuilt binary (exact PTY bytes received):**
`ab` `'/home/kaida/…/tree/b.jpg'` `^[[A` `cd '/home/kaida/…/tree/sub'` `^M` `cd '/home/kaida/…/tree'` `^M` `^[[A^[[B` `z^M`

| Requirement | Result |
|---|---|
| Enter on a file types the path and sends **no** newline | ✓ no `^M` after the quoted path |
| `r` / `p` / `.` / Escape / BackSpace never reach the PTY | ✓ no `r`, `p`, `.`, `ESC`, `^H` anywhere |
| A navigation's listing is installed, not dropped | ✓ **no `dropping stale listing` line at all**; `fm: listing …/sub (request 9)` and `(request 10)` both land |
| Refresh does not type a `cd` at the prompt | ✓ the only two `told the shell to follow` lines are the two real navigations |
| The keyboard returns to the shell | ✓ `^[[A` right after Escape, `^[[A^[[B` after `Ctrl+Shift+Down` |
| Closing and reopening works | ✓ `fm: closed …` / `fm: opened on …`, listing kept |

The one remaining `dropping stale preview … (request 2, newest 3)` at open is the designed rule working:
`fm_open()` previews the directory before any listing exists, and `fm_drain_results()` re-asks once the
listing lands (a listing request always invalidates an in-flight preview, deliberately, because a refresh
means the file may have changed).

**Measured:** `make -j8` exit 0, 0 warnings. `make test` -> exit 0: `CLI: 92 checks, 0 failures, 0 skipped`,
`test_vt` **613 checks 0 failures**, `dbg` **MODEL OK (0 problems)**, `test_fm` **220 checks 0 failures**.
`make asan` -> exit 0: 613/0 + MODEL OK + 220/0, no sanitizer report. (Neither `main.c` nor `render.c` is
linked into any test binary, so these counts cannot move from these edits — they are re-run to prove that,
not to claim coverage.)

**Coverage gap, stated honestly:** the input-routing contract (`send_key`'s 0/1 return, `fm_keys`'s
-1/0/1) lives in `main.c`, which is in no test binary, so these three bugs are covered only by the
injection harness in `/home/kaida/Project/.scratch/s74/` (`inj_keys.sh` + `inj_test.sh`), not by
`make test`. Adding key injection to `tests/cli_test.sh` was rejected: it would make the test suite depend
on a live compositor and an injected keyboard, which the suite deliberately does not.

**Next action:** README (feature section, the 20 file-manager settings, libjpeg/libwebp in Requirements,
the shortcut table) and ISSUES.md (a "File manager" limitations group + refresh the stale "Open"
verification line), then a clean-tree re-verification, a commit, and the live run for the user's eye.

### s74 entry 5 — README now documents the file manager (every claim read back off the source)

`README.md`:
- **Features**: a "File manager" bullet between "Side-by-side view" and "Font zoom".
- **Requirements**: `libjpeg` and `libwebp` added (the Makefile's `PKG` has carried them since s71; the
  list was stale). GIF needs no library — it is decoded in `src/img.c` because giflib is not installed here.
- **Usage → "### File manager"** (new, after "### Images"): what the band looks like, what the browser's
  header/footer/rows show (name, `@` for a symlink, size for a file — read off `paint_entry()`), a table of
  what the preview shows for a directory / image / text / anything else, the key table, the mouse rules,
  the focus handover, and the `cd` follow. Every number in it is the source's own: 512 directory entries
  (`FM_PREVIEW_DIR_MAX`), 256 KiB of text (`FM_PREVIEW_BYTES`), the 13 highlighter languages and the
  extension map (`fm_lang_for_path()`), magic-byte sniffing rather than file names (`img_sniff`).
- **Available settings**: 20 rows added — `file_manager`, `fm_rows`, `fm_preview`, `fm_show_hidden`,
  `fm_follow`, `key_file_manager`, `key_fm_focus`, `key_fm_release` and the 12 `fm_key_*` — each with the
  default `config_default()` really sets. The clamping paragraph now names the band height, and a new
  paragraph explains why `fm_key_*` may be unmodified when no other binding may.
- **Keyboard shortcuts**: `Ctrl+Shift+F` and `Ctrl+Shift+Up`/`Down` rows, a "File-manager keys" subsection
  at the end of the section (first placed mid-section, which orphaned the selection/repeat prose under it —
  moved), and the key-repeat paragraph now says the movement keys repeat and the one-shots do not.

Nothing is documented that the tree does not do: no "open with default application", no thumbnails, no
multi-select, no tabs in the browser.

**Next action:** ISSUES.md — a "File manager" limitations group and a refresh of the stale "Open"
verification line (it still quotes the pre-file-manager 553-check counts).

### s74 entry 6 — ISSUES.md updated; one new limitation found by probing, not by reading

`ISSUES.md`:
- `## Open` verification line refreshed to this session's numbers (it still quoted the pre-file-manager
  553 / 86 counts). States plainly that the file manager's **appearance has not been confirmed by a human
  eye** — that is the one open item, and it belongs to the user, not to the code.
- New `**File manager**` limitations group (10 bullets), each checked against the source: the band costs
  the terminal rows and the terminal wins (`FM_ROWS_MIN/MAX`, `FM_TERM_MIN_ROWS`, the 154x51 vs 154x61
  `-d` line); one browser per window shared by every session; the `cd` follow is one directory deep and
  never reaches the alternate screen; Kai-Term never opens a file; the caps (8192 entries, 512 preview
  entries, 256 KiB of text, 16 MiB of image file, then the decoder's 4096 px / 16 Mpx) and the
  worker-thread + stale-answer rule; the text preview is not a viewer and cuts long lines instead of
  wrapping them; fixed sorting; a click selects but does not open; the band is not part of the terminal's
  grid, so selection/scrollback/mouse-reporting do not reach it.

**A claim I nearly wrote wrong, caught by measuring.** I had written "an animated GIF or a multi-frame
WebP shows frame one". `.scratch/s74/animprobe.c` (links the real `src/img.c`) on 4-frame files made by
PIL says otherwise: `anim.gif: sniff='gif' decoded=yes 20x20` but
`anim.webp: sniff='webp' decoded=NO 0x0` + `image: webp: decode failed (20x20)` — libwebp's simple decode
refuses an animated container entirely. The preview still behaves (metadata + `webp file could not be
decoded`), so this is a documented limitation, not a bug; decoding that first frame would need
`libwebpdemux` (1.6.0 is installed here), which the project's "no unnecessary dependencies" rule keeps
out. Same probe confirms PNG / JPEG / static WebP / GIF all decode (60x40 each). ISSUES.md now says what
was measured.

**Next action:** refresh AGENT.md "Project Files & Facts" (it still describes 9 sources and no fm.c /
img.c) and "Next Steps", then `make clean && make`, `make test`, `make asan` from a clean tree with exact
counts, commit, and hand the user a live command for the visual verdict.

### s74 entry 7 — AGENT.md's standing sections brought in line with the tree

- **Project Files & Facts**: the tree block now lists `src/img.c`, `src/fm.c`, `tests/test_fm.c` and the
  current check counts (613 / 220 / 92); new per-file entries for `src/img.c` (one decoder set for both
  the kitty path and the previews, GIF first-block-only, animated WebP refused) and `src/fm.c` (model only,
  the two worker-callable builders, `fm_image_fit`); `src/layout.c` now names `FmGeom` and `layout_hit_fm()`.
- **Architecture & Invariants**: new "**File manager band**" block — layout splits the grid area vertically
  first and the band horizontally, sessions stay ordinary `pane[]` entries (that is what keeps every
  existing hit test working), the band is clamped in layout.c and the terminal wins, `FmPane` is a `Cell`
  grid so the renderer needs no new path and shares `emit_cell_glyph()`, the worker may call exactly two
  functions and the request-id rule is the only synchronisation, and the keyboard gate is the feature's
  safety.
- **Key Decisions & Pitfalls**: three new traps, each from this session — an out-parameter passed as an
  argument next to the call that fills it (unspecified evaluation order, zero warnings, `.scratch/s74/
  argorder.c` reproduces it); one int return meaning both "not handled" and "handled, do not repeat"; and a
  log line that counts two different things with one number.
- **Debugging this project**: a "**File manager**" bullet listing every `fm:` line and what its absence
  means, plus the `+ the file-manager preview` suffix on the graphics line.
- **Next Steps**: the file manager is now item 1 with exactly one open item (the user's eye) and the two
  decisions worth their sign-off (`Return` types instead of runs; `fm_follow` sends `cd` unprompted).

**Next action:** `make clean && make`, `make test`, `make asan` from a clean tree with exact counts, then
commit, then hand the user a live run for the visual verdict.

### s74 entry 8 — clean-tree re-verification, all green; committed

Exact commands and results, from `make clean`:
- `make clean && make -j8` -> exit 0; `grep -ciE "warning|error"` over the whole build log -> **0**.
- `grep -rnE "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/ include/ tests/` -> **0** (no probe left in tree).
- `grep -rn "TODO|FIXME|XXX|HACK" src/ include/ tests/` -> **1** hit, and it is the test payload
  `"\x1b[41mXXXXXXXXXX\x1b[0m"` at `tests/test_vt.c:788`, not a task marker.
- `make test` -> exit 0: `CLI: 92 checks, 0 failures, 0 skipped`, `test_vt` **613 checks 0 failures**,
  `dbg` **MODEL OK (0 problems)**, `test_fm` **220 checks 0 failures**.
- `make asan` -> exit 0: 613/0 + MODEL OK + 220/0, and `grep -cE
  "AddressSanitizer|runtime error|LeakSanitizer|SUMMARY|ERROR"` over the run -> **0**.

**Session's code changes, all in `main.c` and `render.c` (the two files no test binary links), all verified
by the live injection harness rather than by the unit tests:**
1. `fm_keys()` three-state return — a one-shot file-manager key no longer also reaches the shell (Enter on
   a file used to type its path AND a newline, i.e. run it).
2. Out-parameter evaluation order at the three `fm_nav()` call sites — a navigation's listing is now
   installed instead of dropped as stale.
3. `fm_send_cd()` fires only on a real directory change (`fm_last_cd`, cleared by `tab_switch()`) — the
   refresh key no longer types a command at the prompt.
4. Two log lines made honest: `fm: told the shell to follow: cd …` added; the `graphics:` line counts the
   file-manager preview separately.

**Committed** (one commit: `src/fm.c`, `src/img.c`, `tests/test_fm.c` new; `Makefile`,
`include/kai-term/kai-term.h`, `src/{config,layout,main,render,vt}.c`, `tests/{test_vt.c,cli_test.sh}`,
`README.md`, `ISSUES.md`, `AGENT.md` modified). `bin/` and `build/` stay git-ignored (verified with
`git check-ignore`).

**STILL OPEN — the one thing this session cannot do:** the file manager has never been seen by a human eye.
Per this project's rule the visual verdict belongs to the user. Ask them to run it and record what they
say; until then ISSUES.md "Open" and AGENT.md Current Status both say so explicitly.

**Next action:** give the user the live command (`./bin/kai-term -d`, `Ctrl+Shift+F`), record their verdict,
then `git fetch` + push. If they report anything about how it looks, start from the `fm:` log lines listed
under "Debugging this project", not from the shader.

**Remote synced:** `git fetch origin` first — 0 behind, nothing on GitHub to integrate — so the push was a
fast-forward. Re-fetched and verified `main` == `origin/main` (no ahead/behind marker). Read the truth with
`git status -sb` / `git log --oneline origin/main -1`; the commit recording this line is itself pushed.

### s74 entry 9 — swept the remaining stale status lines out of AGENT.md

Three bullets in Current Status still described the pre-s74 tree ("README does NOT yet mention the file
manager", "`make test` is red on the new fm tests", "27 settings rows"). Replaced with measured facts:
- The settings-table claim is now checkable rather than eyeballed: `diff` of the 47 keys `apply()` in
  `src/config.c` handles (`font` at the top of the chain plus 46 `else if` branches) against the 47 rows of
  README's "Available settings" table -> **the same set, no difference either way**.
- "`make test` is red" removed — it has been green since s72 entry 2, and the bullet contradicted the
  counts three lines above it.
- "Uncommitted work in the tree" replaced with "committed and pushed", with no hash and no ahead-count
  (the commit recording it makes those wrong; the rule is to read `git status -sb`).

**Next action:** the user's live run. Everything the code side can prove is proven and recorded.

### s75 entry 1 — session start: measured the real state; the task is the user's four file-manager fixes

User request: "read and apply the fix in TODO.md". TODO.md (2026-10-19) lists four file-manager items:
1. while the file manager has focus the terminal must receive NO input — today every key that is not one
   of the band's bindings is written into the terminal;
2. Left arrow = go to the parent directory, Right arrow = enter the highlighted entry, and Right must do
   nothing on a non-directory;
3. the `cd` follow and the typed path must only go to a terminal whose input line is EMPTY, and the user
   would like the injected command not to be shown at all ("so it feels more seamless");
4. a mouse click should open a directory, and the listing should carry a `..` entry at the top so the
   pointer can go back a directory.

**s75 measured start (before any edit this session):**
- `git status -sb` -> `main` == `origin/main`, clean tree (no modified, no untracked files). HEAD is the
  file-manager commit + the AGENT.md sweep; read the hash with `git log --oneline -1`.
- `make clean && make -j8` -> exit 0, `grep -ciE "warning|error"` over the whole build log -> **0**.
- `make test` on that fresh tree -> **3 CLI failures** (`-c FILE runs` rc 1, `-c FILE applies font_size`,
  `combined -c -d -w -e runs`). Re-running the SAME binaries: `make test-cli` -> 92/0/0, then `make test`
  -> `CLI: 92 checks, 0 failures, 0 skipped`, `test_vt` **613 checks 0 failures**, `dbg` **MODEL OK
  (0 problems)**, `test_fm` **220 checks 0 failures**, and 5 further `tests/cli_test.sh` runs in a row ->
  92/0/0 every time. So the tree is green and the 3 failures were a one-off flake in the display-dependent
  CLI checks; the failing run's own log showed `eglSwapBuffers failed: 0x3000` then `0x3003`, i.e. a window
  the compositor was not showing. Not reproducible in 6 runs -> not filed (ISSUES.md rule), but watched.

**s75 objective:** fix all four items, keep the build warning-free, extend the headless coverage for
everything that is testable, and re-verify the interactive half with the existing key-injection harness.

**s75 planned steps (each one: edit -> AGENT.md -> build/test):**
1. Header: `left`/`right` in `FmKeys`; the `Term` fields + `term_at_prompt()` prototype the empty-line
   gate needs.
2. `src/config.c`: `fm_key_left` / `fm_key_right` (defaults `Left` / `Right`) — 2 new settings.
3. `src/fm.c`: keep `..` as a real first entry of every listing (it is really in `readdir`; only `.` stays
   dropped), sorted first, never hidden, `lstat` of the parent for its metadata; hidden at `/`.
4. `src/vt.c`: the empty-prompt gate. Kai-Term cannot know where a prompt ends (the shell's echo arrives as
   ordinary output), so the rule is a snapshot: at the end of an output burst that started a new line, the
   cursor's row and column are copied; the line counts as untouched while it still matches. Typing, a
   history recall or a paste change the row without starting a new line, so they are caught; backspacing
   everything away or arrowing back to an empty prompt restores the match.
5. `src/main.c`: swallow every key the band does not use while it owns the keyboard; Left/Right actions;
   `fm_send_cd()` and `fm_send_path()` refuse when the line is not untouched (and log why); a click on a
   directory row enters it.
6. Tests: `test_fm` for the `..` entry (order, visibility, entering it, preview), `test_vt` for
   `term_at_prompt()` (prompt / typed / recalled / cleared), `cli_test.sh` for the two new config keys.
7. README (the two new settings, the mouse rule, the `..` entry, the empty-line rule) + ISSUES.md
   (the echo cannot be suppressed — say so plainly, with the reason) + the live injection run.

**s75 expected outcome:** the four reported behaviours fixed and verified, `make test`/`make asan` green
with a higher check count, docs that match the tree, and the user's eye on the result.

### s75 entry 2 — items 2 and 4 (partly): Left/Right bindings, and `..` is now a real first row

Edits, in the order made:

1. `include/kai-term/kai-term.h` — `FmKeys` gains `left` and `right` (the struct comment now says
   fourteen actions, not twelve). Also added, for the empty-line gate that comes next: the `Term` fields
   `prompt_row` / `prompt_cols` / `prompt_cx` / `prompt_valid` / `prompt_newline` and the
   `int term_at_prompt(Term *t)` prototype.
2. `src/config.c` — defaults `fm_key_left = Left`, `fm_key_right = Right` (both unmodified, like every
   other `fm_key_*`), and the two `apply()` branches `fm_key_left` / `fm_key_right` using
   `set_fm_keybind()`. That is 2 new settings (49 total).
3. `src/fm.c` — **`..` is a real entry of every listing except the root's.** It is SYNTHESIZED in
   `fm_scan_build_n()` (slot 0, before `readdir` runs) rather than taken from `readdir`, so the row is
   guaranteed on every filesystem and has the same shape; its `stat()` is the parent's own, so the
   metadata the preview shows for it is real. `entry_cmp()` pins `..` before everything (it is navigation,
   not content), `hidden` stays 0 so the dotfile filter can never hide it, and at `/` the row is left out
   because `fm_parent()` refuses that move too — the two must agree.
   Two follow-on decisions, both because the row is now position 0:
   - `fm_go()` picks the selection for every navigation (`fm_go_sel`/`fm_parent` no longer set `sel = 0`
     themselves): entering a directory selects its first REAL row, otherwise the first Right/Enter after
     a navigation would go straight back out. `fm_install_scan()`'s clamp still lands on `..` in a
     directory that holds nothing else, which is right.
   - `fm_entry_path()` returns the parent's own path for `..`, not `"<dir>/.."` text — the preview header
     prints that string, and `/home/user/src/..` reads worse there.
4. `tests/test_fm.c` — 21 checks were legitimately moved by the new row and are re-pinned to the new
   behaviour, plus 11 new checks: `..` is first / is a directory / is never hidden / carries the parent's
   stat; a capped listing still starts with it; `/` has no `..` row at all (new `has_entry()` helper);
   Home lands on it and its path is the parent; the directory entered starts with its own `..`; an empty
   directory still offers the row and Enter on it goes up; a model with NO listing (unreadable directory)
   is the one that has no selection — that is what the "nothing selected" checks now use. The painting
   checks now DERIVE the selected row (`1 + sel - top`) instead of hard-coding row 1, which is the same
   class of mistake as the s72 geometry failure: the painter owns the row layout.

**Measured:** `make -j8` exit 0, 0 warnings. `make test` -> exit 0: `CLI: 92 checks, 0 failures,
0 skipped`, `test_vt` **613 checks 0 failures**, `dbg` **MODEL OK (0 problems)**, `test_fm` **231 checks
0 failures** (220 -> 231). Before the test updates the same tree failed exactly 21 `test_fm` checks —
measured, listed, and every one of them a test expectation about "the first entry", not a defect.

**Next action (step 4):** `src/vt.c` — the prompt snapshot and `term_at_prompt()`, then `src/main.c` for
the key swallow, the Left/Right actions, the empty-line gate on `cd`/typed path, and click-to-open.

### s76 entry 1 — session start: measured the real state; s75's step 4 is in the tree, RED on 5 checks

User request: "proceed with the Fixes" — the four file-manager items in `/home/kaida/Project/TODO.md`.
Read AGENT.md + ISSUES.md first, then measured instead of trusting the journal:

- `git status -sb` -> `main` == `origin/main`; modified: `AGENT.md include/kai-term/kai-term.h
  src/config.c src/fm.c src/vt.c tests/test_fm.c tests/test_vt.c`. So s75's steps 1-3 (header `left`/
  `right` + the prompt-snapshot fields, `fm_key_left`/`fm_key_right`, the `..` row) AND its step 4
  (`src/vt.c` prompt snapshot + `term_at_prompt()` + `term_note_input()`, and the new `test_vt.c` checks)
  are in the tree, uncommitted. `src/main.c` is NOT modified — s75's step 5 (the key swallow, the
  Left/Right actions, the empty-line gate, click-to-open) was never started. The vt.c half of step 4 is
  not described by any journal entry: s75 entry 2 ends at "next action: step 4", so treat the vt.c diff as
  work in progress whose result is known from the run below.
- `make clean && make -j8` -> exit 0, `grep -ciE "warning|error"` over the build log -> **0**.
- `make test` -> exit 2: `CLI: 92 checks, 0 failures, 0 skipped`, then `./bin/test_vt` **637 checks,
  5 failures** — `:2188` "answering DSR leaves no echo window open", `:2205` "back on the main screen the
  prompt is known again", `:2209` "the session has history to scroll into", `:2213` "back at the live
  bottom it is", `:2217` "a resize while clean stays clean". `test_fm` on the same tree -> **231 checks,
  0 failures** (it never runs under `make test` because test_vt fails first).

**Root cause of all five, measured with `.scratch/s76/promptdbg.c`** (the whole check sequence replayed
against the real `src/vt.c`, printing `at_prompt/valid/cx/cy/prompt_cx/input_pending/resp_len/alt/so/lo`
after every step): `input_pending` is set by every byte `term_note_input()` is given and cleared only by
CR/LF/Ctrl+C **in what the terminal itself sent**, so the first arrow key opens an echo window that never
closes — the trace reads `input_pending=1` from `type Up` to the end of the run. Consequences, in the
order the trace shows them: (1) the DSR check's precondition was already dirty, so it fails for a reason
unrelated to DSR; (2) `alt_screen_leave()` sets `prompt_valid = 0` and the end-of-burst snapshot is then
suppressed forever, so **after vim/less the file manager could never type at the prompt again**; (3) the
scroll checks cascade from (2) — and the test itself is also wrong there, it feeds 2 newlines into a
10-row grid that has only 6 rows of content, so `live_off` is still 0 and there is no history to scroll
into; (4) `term_resize` then correctly reports "was not clean" and keeps the snapshot invalid.

**Decision, and why the freeze is NOT the bug to remove:** keeping the reference row frozen while an echo
is outstanding is the conservative half of the design — it is what stops a readline redraw (`\r\x1b[Kls -l`
after Up) from becoming "the prompt". Every real submission (Enter) or discard (Ctrl+C) clears the flag,
so the freeze only persists while nothing has been submitted, which is exactly when the line is dirty.
What must be fixed is that the reference can be LOST and never re-taken: leaving the alternate screen
restores the main grid byte-for-byte, so the row under the restored cursor IS the prompt — `alt_screen_leave`
must re-take the snapshot, not clear it. Two of the five failures are test-side (the DSR precondition,
the scrollback setup) and get fixed in the test, not by weakening the model.

**s76 objective:** finish s75's four TODO.md items — (1) no key at all reaches the terminal while the band
owns the keyboard, (2) Left = parent / Right = enter the highlighted directory and nothing on a file,
(3) the `cd` follow and the typed path go only to a session sitting at an untouched prompt, (4) a
double-click opens and the listing carries `..` (already in the tree from s75) — with the model fixed so
the empty-line rule survives the alternate screen, headless coverage for everything testable, the live
injection harness re-run for the half no test binary links, and README/ISSUES.md brought in line.

**s76 planned steps (each one: edit -> AGENT.md -> build/test):**
1. `src/vt.c`: `alt_screen_leave()` re-takes the snapshot instead of clearing it; add `prompt_cy` so a
   cursor on a DIFFERENT row that happens to look like the remembered one cannot count as the prompt.
2. `tests/test_vt.c`: fix the two test-side defects (honest precondition for the DSR check; enough output
   to actually create scrollback) and re-run — 637/0 required.
3. `src/main.c`: route every INPUT write through one helper that calls `term_note_input()` (keystrokes,
   paste, the file manager's own `cd`/path) while query answers and SGR mouse reports keep calling
   `pty_write()` directly; swallow every key the band does not use while it owns the keyboard; the
   Left/Right actions; `term_at_prompt()` gate on `fm_send_cd()`/`fm_send_path()` with a log line naming
   the refusal; double-click opens in the band.
4. Tests: `test_fm` for whatever the model can pin (Right-on-a-file is `fm_go_sel` returning 0),
   `cli_test.sh` for `fm_key_left`/`fm_key_right`.
5. README (the two settings, the `..` row, click-to-open, the empty-line rule) + ISSUES.md (the empty-line
   rule and why the echo cannot be suppressed, the running-command caveat).
6. `make clean && make`, `make test`, `make asan` with exact counts; the live injection harness
   (`.scratch/s74/inj_keys.sh` shape) re-run against the new binary for items 1-4; commit.

### s76 entry 2 — steps 1-2 done: the model fix + the test fixes; `make test` is GREEN (645 / 231 / 92)

**`src/vt.c` (one behaviour change, one new field):**
- `alt_screen_leave()` now calls `prompt_snapshot(t)` instead of `prompt_valid = 0`. Leaving the alternate
  screen restores the main grid and the cursor byte-for-byte, so the row under that cursor IS the prompt the
  full-screen app interrupted. Invalidating there was the bug: the end-of-burst snapshot in `term_parse()`
  is deliberately skipped while an echo is outstanding, and an arrow key pressed before the app ran leaves
  `input_pending` set with nothing to clear it — so the shell follow would have been dead for the rest of
  the session after every vim/less run. Nothing a user can type opens a full-screen app without submitting
  a line first, so that burst is always the application's own output and re-taking there is safe.
- `prompt_cy` added (header + `prompt_snapshot` + `term_at_prompt`): the row content alone cannot say which
  row the prompt is, so a cursor parked on a DIFFERENT row that happens to look identical (two blank rows)
  no longer passes for an untouched prompt. Refusing is the safe direction.

**`tests/test_vt.c` — four test-side defects, one of them a silent compile warning the "0 warnings" line had
never covered because it is in `make test`, not `make`:**
1. The DSR check asserted `input_pending == 0` on a state the previous checks had already dirtied (the last
   thing sent was `x`). Now the line is submitted first, and the check gained a companion that the probe
   leaves the prompt known.
2. The scrollback checks fed 2 newlines into a 10-row grid with 6 rows of content — `live_off` was still 0,
   so "the session has history to scroll into" could never be true. Now a submitted `ls -l\r` followed by
   12 output lines, which is what actually reaches the bottom margin.
3. `feed("\x1bc")` is ONE hex escape (`0x1bc`, out of range for a char), not ESC + `c` — the reset check was
   feeding a single garbage byte and passing by accident. gcc said so
   (`tests/test_vt.c:2229: warning: hex escape sequence out of range`). Now `"\x1b" "c"`, plus a check that
   the cursor really went home, which is what distinguishes RIS from a printed byte.
4. `term_note_input(&T, "cd /tmp\r", 9)` — the literal is 8 bytes, so the 9th byte read was its NUL, which
   is not a CR and therefore RE-OPENED the echo window the CR had just closed. That is why
   "the cd it ran leaves the line clean again" failed once the surrounding state was honest. Fixed with a
   `send_in()` helper that takes `strlen()`; no hand-typed byte count is left in the prompt checks (same
   class as s72's truncated `wfile(..., 63)` literals).

**Measured (exact commands):** `make -j8` -> exit 0, `grep -ciE "warning|error"` over the build log -> 0.
`make test` -> exit 0: `CLI: 92 checks, 0 failures, 0 skipped`, `./bin/test_vt` **645 checks, 0 failures**
(637 -> 645: the DSR companion, the arrow-key echo-window, the alt-screen-with-open-window, the prompt-after-
output, the two identical rows x3, the reset-is-RIS check), `dbg` **MODEL OK (0 problems)**, `./bin/test_fm`
**231 checks, 0 failures**, and `grep -niE warning /tmp/t3.log` -> **0** (the hex-escape warning is gone).
Evidence for the root cause and for every step of the fixed sequence:
`/home/kaida/Project/.scratch/s76/promptdbg.c` (the s75 sequence, shows `input_pending=1` stuck from the
first arrow key to the end) and `.scratch/s76/promptdbg2.c` (the fixed sequence, every step reads the
expected state, including `alt leave -> at=1` with the echo window still open).

### s76 entry 3 — step 3 done: `src/main.c` now enforces all four TODO items (build 0 warnings)

One `edit_file` call, thirteen edits, then a build — no un-journaled change left in the tree.

1. **`input_write(Term *, Pty *, buf, len)`** (prototype in the helpers block because the file manager's
   `cd`/path are further up the file than its definition; definition next to `send_str`). Every byte Kai-Term
   sends to the application **as input** goes through it: `send_str` (every keystroke), `paste_flush` (the
   bracketed-paste delimiters and every 4096-byte chunk), `fm_send_cd`, `fm_send_path`. It calls
   `term_note_input()` then `pty_write()`. Deliberately NOT used for the two other pty writers, which are
   the terminal's own output and would open an echo window no keystroke ever closes: `mouse_send()`
   (SGR/X10 reports) and the queued query answers (`term.resp` — DA1/DA2/DSR/CPR). `grep -n "pty_write(" src/main.c`
   afterwards -> exactly those two plus `input_write` itself.
2. **TODO item 1 — the swallow.** In `send_key()`, immediately after the `fm_keys()` gate and before
   `snap_to_live()`/the PTY paths: `if (g_fm.on && g_fm.focus != FM_FOCUS_TERM) { klog(...); return 0; }`.
   Before this, any key that was not one of the band's bindings fell through to the shell, which is exactly
   the reported symptom. Everything matched above it still works while the band has the keyboard — tabs,
   zoom, `Ctrl+Shift+C/V`, Shift+PageUp scrollback, the band's own toggle/focus/release — because those are
   window chrome, not text. Returns 0 so a swallowed held key arms no repeat.
3. **TODO item 2 — Left/Right.** `fm_keys()` gained `left` (parent, same action BackSpace always had) and
   `right` (descend, and **nothing** on a non-directory: it never types a path, so holding Right through a
   listing cannot write at the prompt). The "can I enter this" question is answered by the model's own
   `fm_go_sel()` (it stats a symlink; returns 0 for a plain file or a dangling link) rather than by a second
   classification invented in main.c. `open` (Return) keeps its type-the-path behaviour, now via
   **`fm_open_sel()`**, one function shared by the `open` key and the second click of a double-click so the
   two cannot drift.
4. **TODO item 3 — the empty-line gate.** `fm_send_cd()` and `fm_send_path()` now refuse unless
   `term_at_prompt(t)`, each logging its own refusal (`fm: not telling the shell to follow — its input line
   is not empty` / `fm: not typing the path — the input line is not empty`). `fm_last_cd` is NOT updated on
   a refusal, so the next navigation still gets through once the line is empty again. The "suppress the
   echo" half of the request is NOT implemented and the reason is recorded in ISSUES.md: the tty echoes
   what it is given, and Kai-Term discarding those bytes would leave the screen showing something readline
   believes is not there — the next redraw (`\r\x1b[K…`) would print the command anyway.
5. **TODO item 4 — double-click.** `fm_pointer_select(hit, btn, time)`: one click selects (unchanged), a
   second left click on the SAME view position within `FM_DCLICK_MS` (500) calls `fm_open_sel()`. The pair
   is reset by a click on the preview, on the header/footer/empty space, on a different row, or after it
   fires — so a third click cannot re-fire without a fresh pair. `ptr_button` no longer discards `time`.
   (`..` at the top of the listing was already in the tree from s75.)

**Measured:** `make -j8` -> exit 0, `grep -iE "warning|error"` over the build log -> **0**. `grep -n
"pty_write(" src/main.c` -> 1320 (`input_write`), 1623 (`mouse_send`), 2594 (query answers).

### s76 entry 4 — step 4 done: the new surface is covered headlessly; 651 / 231 / 94, all green

**`tests/test_vt.c` (config section, +6 checks):** the fourteen `fm_key_*` defaults now include
`fm_key_left = Left` and `fm_key_right = Right` (both bare, mods 0); a config file that sets
`fm_key_left = h` / `fm_key_right = l` rebinds them to those bare keys; `fm_key_left = none` unbinds it
(BackSpace stays the way up); and an unparseable `fm_key_right = Ctrl+Shift+Nope` keeps the Right default
rather than disabling "enter a directory" — the same rule already pinned for `fm_key_open`.

**`tests/cli_test.sh` (+2 checks, real binary):** `fm_key_right = none` logs its unbind, `fm_key_left = h`
logs no unbind, and the whole file-manager config block still produces no `unknown key` line.

**`tests/test_fm.c`: no new checks, because the model already pins what the new keys rely on** — verified by
reading the file rather than by adding duplicates: `fm_go_sel()` on a plain file returns 0 and nothing moves
(line 581 — that is exactly "Right does nothing on a non-directory"), `fm_go_sel()` on the `..` row goes up
one level (601-602), `..` is the first entry, is `FMK_DIR`, is never hidden and carries the parent's own stat
(428-431), `/` has no `..` row (479-480), and the `..` row is painted as the first entry row (772-774). The
Left/Right *bindings* live in `main.c`, which is in no test binary — that half is covered by the live
injection run, not here.

**Measured (exact commands):** `make test` -> exit 0: `CLI: 94 checks, 0 failures, 0 skipped`
(92 -> 94), `./bin/test_vt` **651 checks, 0 failures** (645 -> 651), `dbg` **MODEL OK (0 problems)**,
`./bin/test_fm` **231 checks, 0 failures**, 0 compile warnings in the whole run.

**Next action (step 5):** README (the two new settings, the `..` row, one click selects / two open, Left and
Right, the empty-line rule) and ISSUES.md (the empty-line rule, why the echo cannot be suppressed, the
running-foreground-command caveat, and refresh the "Open" verification counts). Then the live injection run
for items 1-4, then `make clean && make && make test && make asan` and a commit.

### s77 entry 1 — session start: measured the real state, and a live pty probe found a REAL core bug

User request: "continue to work from previous session on Kai-Term" -> finish the integrated file manager
(s76's steps 5-6: docs, live verification of the four TODO.md items, clean re-verification, commit).
Read AGENT.md + ISSUES.md first, then measured instead of trusting the journal:

- `git status -sb` -> `main` == `origin/main`; modified: `AGENT.md ISSUES.md README.md
  include/kai-term/kai-term.h src/config.c src/fm.c src/main.c src/vt.c tests/cli_test.sh
  tests/test_fm.c tests/test_vt.c`. Nothing untracked (the file manager is committed since s74).
- s76's step 5 (README + ISSUES.md) IS in the tree although no journal entry records it: the README diff
  carries the `..` row, `Left`/`Right`, the double-click rule, the two new settings and the empty-prompt
  paragraph, and ISSUES.md carries the empty-line rule, the "the echo cannot be suppressed" paragraph and
  the refreshed counts. Verified the settings claim by set difference, not by eye: the 49 keys `apply()`
  handles in `src/config.c` and the 49 rows of README's "Available settings" table are the same set.
- `make clean && make -j8` -> exit 0, `grep -ciE "warning|error"` over the build log -> **0**.
- `make test` -> exit 0: `CLI: 94 checks, 0 failures, 0 skipped`, `test_vt` **651 checks 0 failures**,
  `dbg` **MODEL OK (0 problems)**, `test_fm` **231 checks 0 failures**.
- The s76 session also left an un-journaled live run (`.scratch/s76/inj_s76.sh`, `log`, `rec`). Its byte
  stream shows the `cd` follow going out **after** the user had typed `rm -rf` at the prompt — i.e. TODO
  item 3 appeared broken. Its expectations were also wrong (the band opens with the selection on the `..`
  row, so its first `Right` went UP a level, and the preview of every navigation was built from the
  previous directory's entry names -> `fm: preview: cannot stat <newdir>/<oldname>` noise).

**Measured from scratch, headless, against the real sources (`.scratch/s77/`):**
- `gate.c` replays the s76 sequence through the real `term_parse()`/`term_at_prompt()`: with the echo
  bytes fed in, the model **does** refuse (`at_prompt=0` after `rm -rf`, and `1` again after Enter).
- `livegate.c` does the same over the real `src/pty.c` — and the tty **echoes nothing at all**. Cause
  found with `termios_probe.c`: `pty_spawn()` runs `cfmakeraw()` on the **master** fd, and on Linux a pty
  master and slave share one termios, so the child's terminal starts with `-echo -icanon -isig -iexten
  -icrnl -opost`. `stty -a` inside a Kai-Term pty proves it.
- Consequences, each measured, not inferred:
  - `echochk.c`/`echochk2.c`/`echochk3.c`: interactive **bash** in a Kai-Term pty never echoes what is
    typed (readline believes the terminal echoes; the terminal does not). fish is fine — it draws its own
    line (`fishecho.c`: `l` -> `l<CR><ESC>[15C`), which is why the user has never seen this.
  - `sigchk.c` + A/B (`pty_sane.c` = the same pty.c with the cfmakeraw block removed): Ctrl+C to a plain
    child does nothing with the shipped pty; with the block removed the tty echoes `^C`.
  - `sigfish.c` A/B, the user's real workflow: `sleep 30` + Ctrl+C under **fish** -> shipped pty: the
    command keeps running (no prompt marker returns); pty without cfmakeraw: **the prompt comes back**.
    Ctrl+D is not EOF either (ICANON off), and `ls` under bash emits a bare `<LF>` because OPOST is off
    (`lfchk.c`: Kai-Term's parser puts the next line at the SAME column — a staircase).
  - So the empty-prompt gate is untestable through bash (nothing changes on screen) and works through
    fish. That is why the s76 live run looked like a broken gate.

**s77 objective:** close the file manager out honestly — (1) make the empty-prompt gate hold even when the
application does not echo, (2) fix the two file-manager warts the s76 run exposed (the selection opening on
the `..` row; a navigation previewing a path built from the previous listing), (3) fix the pty raw-mode bug
that breaks Ctrl+C/Ctrl+D/echo for every child program, (4) re-verify the four TODO items live through the
user's own shell (fish), (5) docs + clean re-verification + commit.

**s77 planned steps (each one: edit -> AGENT.md -> build/test):**
1. `src/vt.c`: `term_at_prompt()` also refuses while `input_pending` is set (Kai-Term knows it sent bytes
   that have not been submitted, even when the application shows nothing), and the end-of-burst rule closes
   that window when the application shows the remembered prompt row again — so backspacing a line away or
   an arrow-key redraw re-arms the gate, while a program that never echoes keeps it shut.
2. `src/fm.c`: the first selection of a directory is its first REAL row (the `..` row is row 0 but must not
   be what Enter/Right acts on when the band opens); `have_listing` cleared by `fm_go()` so
   `fm_preview_request()` cannot build `<new dir>/<entry of the old listing>`.
3. `src/pty.c`: stop putting the child's terminal into raw mode; leave the kernel's cooked defaults.
4. Tests: `test_vt` for the gate rule, `test_fm` for the selection and the preview path.
5. Live injection run through fish (`.scratch/s77/inj_fish.sh`, ground truth = the PTY bytes) for TODO
   items 1-4 plus the gate; then `make clean && make`, `make test`, `make asan`, README/ISSUES, commit.

**s77 expected outcome:** the four TODO items verified through the shell the user actually runs, a file
manager that never writes at a prompt it cannot prove is empty, Ctrl+C working again for every child
program, `make test`/`make asan` green with a higher check count, and the user's eye on the band.

### s77 entry 2 — steps 1-2 done: the prompt gate no longer depends on the application echoing; two file-manager warts fixed

**`src/vt.c` — the empty-prompt gate.** `term_at_prompt()` was the row comparison only. That is enough for
an application that echoes (fish draws its own line) and useless for one that does not (measured: bash in a
Kai-Term pty returns nothing at all for typed characters). Now:
- the comparison lives in a new `prompt_row_matches()`;
- `term_at_prompt()` = `!input_pending && prompt_row_matches()` — Kai-Term knows it sent bytes that have not
  been submitted, whether or not the application shows them;
- the end-of-burst rule gained the other half: while an echo window is open, an output burst that leaves the
  row matching the remembered prompt again CLOSES the window (`\r\x1b[Kprompt$ ` — readline's redraw, an
  arrow-key recall that comes back to an empty line). An application that shows nothing keeps it shut.

That keeps every documented recovery path working (backspacing a line away, arrowing back to an empty
prompt, Enter, Ctrl+C) and closes the hole where a non-echoing program made a half-typed command look empty.

**`src/fm.c` — two defects the s76 live run exposed:**
- `fm_init()` left the selection at 0, which is the `..` row, so the band opened ON the way out: the first
  `Right`/`Return` went up a level (that is why the s76 harness navigated somewhere it did not intend).
  New `first_row_of(dir)` helper (row 1, or 0 at the root, which has no `..` row) is now used by both
  `fm_init()` and `fm_go()` — one rule, so opening and navigating cannot drift.
- `fm_go()` changed `fm->dir` but left `have_listing` set, so `fm_preview_request()` built a path out of the
  PREVIOUS directory's entry names: every navigation asked the worker to stat `<new dir>/<old name>` and
  logged `fm: preview: cannot stat …`. `fm_go()` now clears `have_listing`; the preview falls back to the
  directory itself until the new listing lands (the old rows stay painted while loading — an empty browser
  for a moment reads as a broken window — but nothing may build a path out of them).

**Measured:** `make -j8` exit 0, `grep -ciE "warning|error"` over the build log -> 0. `make test` -> exit 0:
`CLI: 94 checks, 0 failures, 0 skipped`, `test_vt` **656 checks 0 failures** (651 -> 656: typed text with NO
echo, an empty line is not the remembered prompt, the remembered prompt redrawn closes the window, dirty
again after that, Ctrl+C re-arms), `dbg` **MODEL OK (0 problems)**, `test_fm` **235 checks 0 failures**
(231 -> 235: opens on the first real row, `/` opens on row 0, the old listing is not a description of the new
directory, the preview asked for during a load is the directory itself). One existing check was re-pinned
honestly rather than left to pass by accident: "back on the main screen the prompt is known again, echo
window open or not" now also asserts `input_pending == 0` — leaving the alternate screen restores the prompt
row byte-for-byte, which IS the proof the line is empty, so the window closes there now.

**Next action (step 3):** `src/pty.c` — stop putting the child's terminal into raw mode.

### s78 entry 1 — session start: measured the real state; s77's step 3 (the pty raw-mode bug) is next, and one s77 measurement was wrong

User request: "Continue the work from previous Sessions on Kai-Term" -> finish the file-manager work
(s77's steps 3-5), i.e. the pty raw-mode bug, the live verification through the user's own shell, the docs,
and the clean re-verification + commit.

**s78 measured start (before any edit this session):**
- `git status -sb` -> `main` == `origin/main`; modified: `AGENT.md ISSUES.md README.md
  include/kai-term/kai-term.h src/config.c src/fm.c src/main.c src/vt.c tests/cli_test.sh
  tests/test_fm.c tests/test_vt.c`; nothing untracked. So s75/s76/s77's work is in the tree uncommitted,
  exactly as the journal says. `src/pty.c` is NOT modified -> s77 step 3 was never started.
- `make clean && make -j8` -> exit 0, `grep -ciE "warning|error"` over the build log -> **0**.
- `make test` -> exit 0: `CLI: 94 checks, 0 failures, 0 skipped`, `test_vt` **656 checks 0 failures**,
  `dbg` **MODEL OK (0 problems)**, `test_fm` **235 checks 0 failures** — matches s77 entry 2's counts.
- `/home/kaida/Project/TODO.md` no longer exists (the four file-manager items it carried were s75's).

**s77's pty claims re-measured myself (`.scratch/s78/`, A/B: the shipped `src/pty.c` vs `pty_sane.c` =
the same file with the `cfmakeraw()` block deleted, both linked against the current vt.c/img.c/config.c):**
- `termios` probe: with the shipped pty the child's terminal is `-isig -icanon -iexten -echo -opost
  -icrnl`; with the block gone it is the kernel default `isig icanon iexten echo opost icrnl`. So the
  master-side `cfmakeraw()` really does set the CHILD's terminal (one termios per pty pair).
- `ls` in a Kai-Term pty: shipped -> `... src<LF>ISSUES.md ...` (bare LF); sane -> `<CR><LF>`. Kai-Term's
  parser does NOT treat a bare LF as CR+LF (`lfchk`: row 1 starts at col 8, row 2 at col 17 — a
  staircase). So every program that does not set its own termios prints staircases today.
- Ctrl+C to `sleep 30`: shipped -> the child keeps running, nothing echoed; sane -> the child becomes a
  zombie (SIGINT landed) and the pty's foreground pgrp goes to 0. Through the user's real shell (fish,
  `sleep 30` + Ctrl+C): shipped -> the prompt never comes back; sane -> it does.
- Ctrl+D to `cat`: shipped -> `cat` prints the `^D` byte and keeps running (VEOF needs ICANON);
  sane -> canonical behaviour (the line is echoed, `hi` then `hi`, and EOF works on an empty line).
- **One s77 measurement was WRONG and is corrected here:** `.scratch/s77/sigchk.c` wrote `\003` to the
  master IMMEDIATELY after `pty_spawn()` returned, before the child had run `setsid`/`TIOCSCTTY`, so the
  pty had no foreground process group yet and NO pty could have signalled anything — it reported
  "Ctrl+C did nothing" for the SANE build too. `.scratch/s78/sigwhy.c` waits 500 ms first and prints
  `TIOCGPGRP` + the process tree: sane -> `^C` echoed, child dead, foreground pgrp 0; shipped -> no echo,
  child alive, foreground pgrp unchanged. The conclusion (raw mode breaks Ctrl+C) stands; the s77 A/B
  evidence for the plain-child case does not, and the fixed harness is what s78 cites.

**s78 objective:** finish the file-manager integration honestly — fix the pty raw-mode bug (it is the
reason the empty-prompt gate could not be verified through bash, and it breaks Ctrl+C/Ctrl+D/echo for
every child program), verify the four TODO.md file-manager items live through the user's own shell (fish),
bring README/ISSUES.md in line, re-verify from a clean tree and commit.

**s78 planned steps (each one: edit -> AGENT.md -> build/test):**
1. `src/pty.c`: delete the `cfmakeraw()` block; leave the kernel's cooked defaults, and say why in the
   comment (one termios per pty pair; applications that want raw mode set it themselves — readline, vim,
   fish all do).
2. Re-run the A/B harness against the real binary's pty (the probes now link the shipped pty.c) and record
   the byte-level evidence: `ls` -> CR+LF, Ctrl+C -> SIGINT, Ctrl+D -> EOF, typed characters echoed.
3. `make test` + `make asan` — expect no count change (no test links pty.c); record that honestly.
4. Live injection run through fish (`.scratch/inject` -> niri -> the real window, ground truth = the bytes
   the PTY received) for the four TODO items + the empty-prompt gate, now that the tty echoes: typing at
   the prompt must make the gate refuse, Enter must re-arm it.
5. README (nothing new to document unless behaviour changed — check the "File manager" and "Input" text
   against the new pty) + ISSUES.md (the echo paragraph is now true by measurement, not by argument).
6. `make clean && make`, `make test`, `make asan` with exact counts; commit; `git fetch` + push; hand the
   user the live command for the file-manager visual verdict (still the one open item).

**s78 expected outcome:** Ctrl+C, Ctrl+D and echo work for every child program, `ls` no longer staircases,
the empty-prompt gate verified through the shell the user actually runs, `make test`/`make asan` green, docs
that match the tree, one commit on `main`, and the user's eye on the band.

### s78 entry 2 — step 1-3 done: the pty no longer puts every child program into raw mode

`src/pty.c`: deleted the `cfmakeraw()` + `tcsetattr()` block on the master fd (and the now-unused
`#include <termios.h>`), replacing it with a comment that names each broken flag and the measurement that
shows it. No other file changed — nothing in Kai-Term reads or writes termios, and nothing else in the tree
assumed the raw pty (`grep -n "tcset\|cfmake\|termios" src/ include/` -> only that comment).

Why this is the right fix rather than "raw but re-enable ISIG/OPOST": a terminal emulator's job is to give
the child a normal terminal. Programs that want raw mode set it themselves (readline, vim, fish, less all
do) and restore it afterwards; that only works if the starting state is the kernel's cooked default. The
same reasoning is why foot/alacritty/kitty do not touch the pty termios.

**Measured after the change (exact commands, `.scratch/s78/`, probes relinked against the now-fixed
`src/pty.c`):**
- `./termios_ship` (child runs `stty -a`): `isig icanon iexten echo opost icrnl` — the kernel defaults,
  where before the same probe printed `-isig -icanon -iexten -echo -opost -icrnl`.
- `./bytes_ship 'ls'`: `... build\t src<CR><LF>ISSUES.md ...` — ONLCR is back, so the staircase the
  `lfchk` probe measured (row 1 at col 8, row 2 at col 17) cannot happen any more.
- `./sigwhy_ship` (waits for the child to set up, prints `TIOCGPGRP` + the process tree): `^C` echoes as
  `^C`, the child becomes `Zs sleep <defunct>`, the pty's foreground pgrp goes 155079 -> 0, `waitpid`
  reaps it. Ctrl+C is a signal again.
- `./sigfish_sane` (fish, `sleep 30`, Ctrl+C) already showed the prompt coming back; the shipped build now
  links the same code path.
- `./bytes_sane 'cat' 'hi' <Ctrl+D>`: `hi` echoed by the tty, then `hi` again from `cat` — canonical mode,
  which is what makes VEOF work; the shipped build printed the `^D` byte itself and `cat` kept running.
- `./livegate <tree> cat` and `./livegate <tree> bash` (the real `src/pty.c` + the real `term_parse()` +
  `term_at_prompt()` after every step) — the empty-prompt gate now holds over a real tty echo, in both
  shells: after `rm -rf` typed without Enter, `at_prompt=0 pending=1` -> "the cd would be REFUSED"; after
  Enter, `at_prompt=1 pending=0` -> "the cd WOULD be sent". s77 could not show this for bash at all,
  because nothing was echoed.

**Measured (project commands, exact):** `make -j8` -> exit 0, `grep -ciE "warning|error"` over the build
log -> **0**. `make test` -> exit 0: `CLI: 94 checks, 0 failures, 0 skipped`, `test_vt` **656 checks
0 failures**, `dbg` **MODEL OK (0 problems)**, `test_fm` **235 checks 0 failures**, 0 warnings in the run.
`make asan` -> exit 0: 656/0 + MODEL OK + 235/0, and `grep -cE
"AddressSanitizer|runtime error|LeakSanitizer|SUMMARY|ERROR"` over the run -> **0**. The counts did not
move because no test binary links `src/pty.c` — re-run to prove that, not to claim coverage. The pty change
is covered by the `.scratch/s78/` A/B harness only.

**Next action (step 4):** the live injection run through the user's own shell (fish) — `.scratch/inject`
-> niri -> the real `bin/kai-term`, ground truth = the bytes the PTY received — for the four file-manager
items plus the gate, now that the tty echoes what is typed.

### s78 entry 3 — step 4a: Ctrl+C verified live in the real window, and two harness traps found on the way

Live A/B in the real niri session (`.scratch/s78/inj_loop.sh` + `pgrploop.c`, keys injected through
uinput): the child is a program with the **default** SIGINT disposition that prints one line every 400 ms
(and its own pid/pgrp + `TIOCGPGRP`). One injected `Ctrl+Shift+F`-class chord was proven to reach the
window in the same run (the `fm: opened on …` log line), so "the key arrived" is never assumed.

| binary | the loop's line count before ^C / after 1.6 s / after a 2nd ^C + 1.6 s |
|---|---|
| `bin/kai-term` (fixed pty) | 5 -> **7 -> 7** — the program died at the first Ctrl+C |
| `.scratch/s78/kai-term_old` (the committed `cfmakeraw` pty.c) | 5 -> **12 -> 18** — it kept printing through both |

`kai-term_old` is the current tree's objects linked against `git show HEAD:src/pty.c` — the A/B build the
project's method calls for, and it is what turns "the comment says it works" into evidence.

Supporting live measurements, all with the fixed binary:
- `inj_stty.sh` + `stty -a` written to a file from inside the real window: the child's terminal is
  `isig icanon iexten echo opost icrnl` (the old binary: `-isig -icanon -iexten -echo -opost -icrnl`).
- `pgrpchk` inside the real window: `fg pgrp = <the shell's pid>`, and the child's own pgrp equals it —
  so a generated SIGINT has somewhere to go.
- `sigcatch2` (installs a handler): `start: fg_pgrp=184395 pid=184396 pgrp=184395` then
  `caught signal 2` — SIGINT reaches a forked child of the shell, not only the pgrp leader.
- `inj_reckey.sh`: with `stty raw -echo` in the child, the PTY receives `a^Cb` — Kai-Term really writes
  `\003` for the injected chord.

**Two harness traps, both of which produced a FALSE "Ctrl+C still broken" and both worth remembering:**
1. **A background job of a non-interactive shell has SIGINT and SIGQUIT set to SIG_IGN**
   (`SigIgn: 0000000000000006` in `/proc/<pid>/status`), and that disposition is inherited across
   `fork`+`exec` — so every program inside a terminal started with `binary &` from a script ignores
   SIGINT, whatever the pty does. `trap - INT` in `sh -c` does NOT undo it (a POSIX shell cannot reset a
   signal that was ignored on entry). Fix: `.scratch/s78/runfg.c` — `signal(SIGINT, SIG_DFL)` before the
   exec. Measured: the child's `SigIgn` goes `0x6` -> `0x4` (only SIGQUIT left).
2. **`sleep 30; echo done > FILE` is not a valid interrupt test**: the shell catches SIGINT
   (`SigCgt: …00010002`) and aborts the script instead of running the next command, so the marker file
   never appears even when the signal landed. A program that reports its own state (a loop, or a handler
   that writes a file) is the honest probe. `sleep` itself was checked separately: inside the terminal its
   `SigIgn` is 0 and an explicit `kill -INT` kills it (`slept rc=130`).

Also corrected: the s77 `sigchk.c` race (recorded in entry 1) — writing to the master before the child has
run `setsid`/`TIOCSCTTY` cannot produce a signal for ANY pty, so that A/B proved nothing.

**Next action (step 4b):** the file-manager live run through fish — the four TODO.md items plus the
empty-prompt gate, with the tty now echoing what is typed.

### s79 entry 1 — session start: measured the real state; NEW USER REQUIREMENT changes the prompt rule

User request, two parts. **Confirmed as-is, no change:** `Return` on a file types the path instead of
running it, and `fm_follow` sends `cd <dir>` unprompted — both stay. **Changed:** the rule "the `cd` follow
and the typed path only go to an untouched prompt" is too strict. The user wants

> "it should first empty the prompt, cd into the new dir, then repopulate the prompt with what was in it
> previously. Basically the prompt part should ALWAYS be in the same directory as the File Manager."

So a navigation must never be refused because the line already has text: Kai-Term clears the line, sends
the `cd`, and puts the user's text back on the new prompt.

**s79 measured start (before any edit this session):**
- `git status -sb` -> `main` == `origin/main`; modified: `AGENT.md ISSUES.md README.md
  include/kai-term/kai-term.h src/config.c src/fm.c src/main.c src/pty.c src/vt.c tests/cli_test.sh
  tests/test_fm.c tests/test_vt.c`; nothing untracked. s75-s78's work is in the tree uncommitted, as the
  journal says. HEAD `2985ee4`.
- `make -j8` -> exit 0, `grep -ciE "warning|error"` over the build log -> **0**.
- The headless suites are green: `./bin/test_vt` **656 checks 0 failures**, `./bin/dbg` **MODEL OK
  (0 problems)**, `./bin/test_fm` **235 checks 0 failures**.
- **`make test` is RED — 6 to 11 `cli_test.sh` failures, and the reason is a SEGFAULT, not the display
  flake s75 recorded.** `./bin/kai-term -e true` -> rc 139, 20/20 runs. The ASan build of the whole
  binary (`.scratch/s79/kai-term_asan`) reports `SEGV on unknown address 0x4`, READ, inside
  `libnvidia-eglcore`, called from `draw_quads` (src/render.c:565, the non-instanced `glDrawArrays`)
  from `redraw` <- `xdg_surface_configure` <- `wayland_setup`. Every crashing run prints
  `kai-term error: atlas glTexImage2D failed: 0x505 (512x512)` first.
- **A/B against the committed tree proves it is NOT a regression:** `git archive HEAD` into
  `.scratch/s79/head/`, built clean, fails 5/5 with the same `atlas glTexImage2D failed: 0x505` line.
  Cause, measured: `nvidia-smi` -> **11843 / 12227 MiB of GPU memory in use**, one process (pid 4552)
  holding 9740 MiB. The texture allocation fails and the driver then faults in its own draw path.
  `niri msg focused-window` answers normally, so the compositor is fine — it is VRAM exhaustion.
  Two consequences recorded honestly: (a) `tests/cli_test.sh` cannot be trusted while the GPU is full, so
  the headless counts are the reliable signal this session; (b) Kai-Term **segfaults instead of exiting**
  when an atlas upload fails — a real robustness gap, but it reproduces only under VRAM pressure, so it
  goes to ISSUES.md as a limitation with this reproduction rather than being chased as a new bug.
- `fish 4.6.0` binds `ctrl-u` -> `backward-kill-line` (`fish -c 'bind \cu'`); readline's `C-u` is
  `unix-line-discard`; zsh emacs mode binds `^U` to `unix-line-discard` too. So `0x15` is the one
  sequence every line editor on this box uses to erase the line backwards to its start — and it needs no
  character count, which a `\b`-per-cell approach would (a tab advances several columns, a wide glyph
  occupies two cells, so cells != keystrokes).

**s79 objective:** make the shell's input line follow the browser unconditionally — clear it, `cd`, put the
user's text back — without ever running a mangled command and without ever losing what the user typed.

**s79 design (decided before writing code):**
1. `src/vt.c` — new `int term_prompt_line(Term *t, char *out, size_t cap)`: 1 when the input line can be
   described EXACTLY as "the remembered prompt, then typed text ending exactly at the cursor", with the
   text written to `out`; 0 otherwise. It does NOT consult `input_pending` (the row comparison is the
   evidence, and Kai-Term's own typed path must be describable too). Refuses when the prompt prefix itself
   changed, when anything right of the cursor differs from the snapshot (the cursor is mid-line), when the
   view is scrolled back, on the alternate screen, or when the text does not fit in `cap` — never silently
   truncates a command.
2. `src/main.c` — `fm_send_cd()` keeps the old fast path (`term_at_prompt()` -> send now) and gains the
   three-step path: capture the text, send `\x15`, and **defer** the `cd` until the model proves the line
   is empty again (`term_at_prompt()` becomes true — the end-of-burst rule already closes the echo window
   when the application puts the remembered prompt row back). Then send `cd <dir>\r` and defer the retype
   until the NEW prompt has been drawn. A deadline drops a stuck sequence with a log line.
   **Safety property this buys: the `cd` is never sent unless the erase is proven to have worked**, so the
   "runs a mangled command" failure mode is closed by construction, not by trusting the key binding.
3. `fm_send_path()` (Return on a file) stops refusing a line that merely has text: it appends at the
   cursor, which is exactly what typing does and cannot run anything (no CR is sent). Gate becomes
   `term_prompt_line()`.
4. No new config key — the user asked for this to always happen.

**s79 planned steps (each one: edit -> AGENT.md -> build/test):**
1. Header: `term_prompt_line()` prototype + `FM_LINE_MAX`.
2. `src/vt.c`: implement it; `tests/test_vt.c` checks (empty line, typed line, cursor mid-line, scrolled
   back, alt screen, wide glyph + emoji cluster, a line that does not fit).
3. `src/main.c`: the deferred-cd state machine, the `fm_send_path` gate, reset on `tab_switch()`.
4. `make`, `make test` (headless counts), `make asan`.
5. README + ISSUES.md: the new behaviour, its limits, and the VRAM-pressure segfault.
6. Live run through fish once the GPU has room (ground truth = the bytes the PTY received).

**s79 expected outcome:** navigating with a half-typed command at the prompt leaves the command intact on
the new prompt in the new directory, `make test`/`make asan` green with a higher check count, and docs
that describe what actually happens.

(Previous task, s70 — image support close-out; its entry follows.)

## Old Current Task (s70 — CLOSED: image support done, user-confirmed by eye)

**s70 objective:** close out the image-support work (s68 model/parser + s69 renderer/tests). What was
left per the s69 entry: README (feature entry + libpng/zlib in Requirements), ISSUES.md limitations,
a clean-tree re-verification of build/test/asan, a commit, and a live `timg` run for the user to
confirm by eye.

**s70 measured start (before any edit this session):** `git status -sb` -> `main` == `origin/main`,
modified-uncommitted: `AGENT.md Makefile include/kai-term/kai-term.h src/main.c src/pty.c src/render.c
src/vt.c tests/test_vt.c` (+1860/-56). So the whole s68+s69 implementation is in the tree, uncommitted.
README.md and ISSUES.md are NOT yet touched by it — README still lists no libpng/zlib dependency and
no image feature; ISSUES.md "Open" still quotes the pre-image 501-check counts.

**s70 MEASURED — the s68 auto-detect claim was WRONG, and `timg foo.png` shows nothing today.**
Probes in `/home/kaida/Project/.scratch/s70/` (`capture.py`, `gate.py`): timg 1.6.3 run in a PTY with
Kai-Term's winsize (80x24, ws_xpixel 384 / ws_ypixel 640), its queries answered from the harness, and
the strings of `/usr/bin/timg` read directly. Results:
- timg's probe sequence is `ESC[>q ESC[5n` (250 ms wait), then `ESC[c` (250 ms), then
  `ESC]11;?ESC\` (1501 ms). It prints `Query: ... Response: '' (250ms)` for each under `--verbose`.
- Answering `ESC[>q` with a kitty-style DA2 (`ESC[>1;1030;0c`) AND `ESC[5n` with `ESC[0n` AND `ESC[c`
  with a DA1 that has NO `;4`: still `Effective pixelation: Using quarter block.` — **no combination of
  query answers makes timg choose kitty graphics.** The s68 entry's "with 640x384 at 80x24 it prints
  cell-pixels: 4x26 and emits kitty" is false; the captured `k_raw.ans`/`big_k0.ans` came from the
  forced `-p k` runs in `.scratch/s68/probe.py`. `cell-pixels: 4x26` was real, the kitty emission was not.
- What DOES gate kitty: `TERM=xterm-kitty` or `TERM=xterm-ghostty` (measured: APC=1, "Using kitty
  graphics") — and only together with non-zero pixel geometry (`TERM=xterm-kitty` with ws 0x0 ->
  quarter block). `TERM_PROGRAM=kitty|ghostty|WezTerm|konsole` does nothing. The list is hardcoded in
  the binary (strings at 262921: `TERM`, `xterm-kitty`, `xterm-ghostty`, `TERM_PROGRAM`, `WarpTerminal`,
  plus known-DA1 signatures `iTerm2`, `Konsole 2`, `WezTerm`, `mlterm`, `XTerm`, `foot`, `tmux`,
  `WindowsTerminal`).
- What gates SIXEL: the DA1 answer containing `;4`. Answering `ESC[c` with
  `ESC[?64;1;2;4;6;7;12;16;18;22c` -> "Using sixel graphics"; the same answer without `;4` -> quarter
  block. **Kai-Term's DA1 answer (src/vt.c, `case 'c'`) is exactly the `;4` one**, and s68 made vt.c
  DISCARD DCS — so today `timg foo.png` in Kai-Term emits sixel that is thrown away: the user sees
  nothing at all, where before the image work they saw quarter blocks.

**s70 CONSEQUENCE (next action, in this order):**
1. `src/vt.c` DA1 answer must stop claiming graphics Kai-Term does not have: drop `4` (sixel), `16`
   (sixel+ReGIS) and `18` (wide sixel) from the response. Verified by re-running the harness: timg must
   fall back to quarter blocks instead of sixel. No test pinned the old string (grep for `64;1;2` in
   tests/ -> nothing), so add one that pins the new answer.
2. Answer `CSI 5 n` (DSR, "terminal OK") with `CSI 0 n` — Kai-Term never answered it, and it is the
   second half of timg's first probe (it currently burns the full 250 ms timeout). Test it.
3. README must NOT promise `timg foo.png` auto-detects graphics: document `timg -p k foo.png` and say
   why (measured above). ISSUES.md gets the limitation.
4. Then the live check for the user is `timg -p k <png>`.

**s70 IN PROGRESS — README examples must be verified, not assumed.** The first draft of the README
"Images" section showed `timg --grid=2x2` and `timg -V animation.gif`; neither has been run through
Kai-Term. Before documenting them: capture what timg 1.6.3 really emits for plain / `--grid=2x2` /
`-V` in a PTY with Kai-Term's winsize (80x24, cell 8x16 -> ws 640x384) into
`/home/kaida/Project/.scratch/s70/`, then feed each capture through the existing headless harness
(`.scratch/s68/gfx_probe.c`, rebuilt against the current sources) and read image count / span /
cursor. Anything that does not behave gets dropped from the README or moved to ISSUES.md.

**s70 LIVE LOG EVIDENCE (renderer path reached, not yet seen by a human):**
`timeout 10 ./bin/kai-term -d -e 'timg -p k /home/kaida/Project/.scratch/s70/big.png; sleep 4'` ->
`grid resized to 154x61 (window 1253x1002 px, cell 8x16, padding 8, ...)`, then
`graphics: 0 image(s) drawn, 0 texture(s) held` -> `graphics: 1 image(s) drawn, 1 texture(s) held`, no
GL warning, clean exit (`the last session is gone, exiting`). So the picture really went through decode
-> texture -> draw on the real NVIDIA/GL 4.6 context; whether it LOOKS right (aspect, position, text on
top) was still open at that point — closed by the user's verdict below.

**s70 remote synced:** `git fetch` first — nothing on GitHub to integrate, so `git push origin main` was a
fast-forward (exit 0). Re-fetched: `main` == `origin/main`, `git status -sb` shows no ahead/behind marker.
Read the truth with `git status -sb` / `git log --oneline origin/main -1` — the commit recording this line
is itself pushed.

**s70 CLOSED — user confirmation received:** "The image display properly. The feature works as expected"
(2026-10-17, their own window, `timg -p k` on a PNG). So the picture, its aspect, its placement at the
cursor and the text over it are confirmed by eye, not only by log line. Nothing about the feature is
open; the remaining limits are the documented ones in ISSUES.md "Graphics / images" (no sixel/iTerm2,
no crop/file/animation actions, one background per picture, raw-format `s=` unverified).

**s70 STATE at session close:** image support (s68 model + s69 renderer/tests + s70 fixes and docs) is
implemented, documented (README "Images", ISSUES.md "Graphics / images"), committed and pushed — read the
hash with `git log --oneline -1`, not from here. If a picture ever looks wrong (aspect, position, text
under it), start from the `-d` line `graphics: N image(s) drawn` and the span math in
`image_span_rows/cols`, not from the shader.

**s70 DONE — three source fixes, each measured, tests 542 -> 553.**
1. `src/vt.c` DA1 answer: `ESC[?64;1;2;4;6;7;12;16;18;22c` -> `ESC[?64;1;2;6;7;12;22c`. Kai-Term has no
   sixel/ReGIS (the parser discards DCS) and was claiming it. A/B PROVED the behaviour change
   (`.scratch/s70/timg_gate.c` + `vt_old.c` = the same vt.c with the old string, both linked against the
   real parser and fed the real timg output): old -> `Using sixel graphics`, 8015 bytes of DCS, model
   images=0, i.e. nothing drawn; new -> `Using quarter block`, 26620 bytes of block glyphs, visible
   output. Test pins the exact new string and asserts `;4;`/`;16;`/`;18;` are absent.
2. `src/vt.c` answers `CSI 5 n` (DSR) with `ESC[0n` — never answered before; it is the second half of
   timg's first probe, which then burns its whole 250 ms timeout. Test pins it, and pins that `CSI 6 n`
   still answers the cursor position.
3. `src/vt.c` `kitty_complete()`: a transmission naming an id that already exists now REPLACES that
   image instead of appending another. Found by feeding the real `timg -p k -V anim.gif` bytes through
   the model: timg alternates between TWO ids for a four-frame gif, and the old code kept 4 pictures
   (crow 0,2,4,6) where 2 belong. Now: 2 images, `version` 2 each. `a=T` re-places at the cursor and
   moves it; `a=t`/`a=f` replace the pixels without moving anything. 6 new checks pin all of that.
   Also fixed a brittle absolute count in the `a=d` test (`== 9` -> `n_alive - 1`).

**s70 MEASURED — what timg 1.6.3 actually does against Kai-Term** (`.scratch/s70/timg_gate.c`: real
`timg` in a real PTY with Kai-Term's winsize, its bytes through Kai-Term's real `term_parse()`, and
Kai-Term's own queued answers written back; no display): with `TERM=xterm-256color` (what pty.c sets)
and ws 384x640 px: `timg foo.png` -> quarter blocks; `timg -p k foo.png` -> kitty graphics, model holds
1 image 64x40 px, span 8x3 cells, cursor below it. With `TERM=xterm-kitty` even plain `timg foo.png`
-> kitty graphics. `--grid=RxC` arranges MULTIPLE files (measured: `--grid=2x2` with one file sends one
image; `--grid=1x2 t64.png big.png` sends two, the second in 9 chunks) — so the README draft's
`timg --grid=2x2 foo.png` example was wrong and is dropped.

**s70 VERIFIED myself from a clean tree (not inherited from s69), exact commands:**
- `make clean && make` -> exit 0, 0 warnings, 0 errors (WARN set unchanged; link line now carries
  `-lpng16 -lz`).
- `make test` -> exit 0, **542 checks, 0 failures** + `MODEL OK (0 problems)` + `CLI: 86 checks, 0
  failures, 0 skipped`. Note: 542, not the 540 the s69 entry recorded — the tree is what counts.
- `make asan` -> exit 0, **542 checks, 0 failures** + `MODEL OK (0 problems)`, and 0 lines matching
  AddressSanitizer|runtime error|LeakSanitizer|SUMMARY|ERROR.

**s70 planned steps:** (1) re-verify from a clean tree myself instead of trusting the s69 entry:
`make clean && make`, `make test`, `make asan` — record exact counts. (2) README: add the image
feature to Features, `libpng` + `zlib` to Requirements, and a limitations note where usage describes
what an app can do. (3) ISSUES.md: refresh the "Open" verification line with the new counts and add
the image limitations (raw-format `s=` semantics unverified against a real client, byte budget /
capacity behaviour, anything else the source shows). (4) commit the whole s68+s69 change set, `git
fetch`, push. (5) hand the user a live `timg` command to confirm by eye.

**s70 expected outcome:** docs match the code (dependency list builds, feature list only claims what
is implemented), a clean-tree build/test/asan run with recorded counts, one commit on `main` pushed,
and a live check the user can run.

(Previous task, s69 — image support renderer/tests half; its entry follows and is the source for what
was changed and why.)

## Older Current Task (s69 — image support / timg, continuing s68)

**s69 objective:** finish the remaining halves of the s68 plan — `src/render.c` (image batch +
texture cache), `src/main.c` (push cell px into `Term`/`Pty`), `tests/test_vt.c` checks, README +
ISSUES wording — then re-verify build/test/asan and hand the live run to the user.

**s69 planned steps:** (1) render.c: `u_img` sampler + `a_glyph == 3` path, split the frame into
batch A (backgrounds) -> images -> batch B (bands + glyphs + chrome), one GL texture per image keyed
by `serial`, clip the quad to its pane. (2) main.c: `term_set_cell_px()` + `pty_set_cells()` from
`layout_apply()`. (3) tests: parser/chunking/base64/placement/delete/cursor-move/scroll-anchor/
DCS-discard/`CSI 16 t`. (4) README feature + libpng/zlib in Requirements; ISSUES limitations.
(5) `make clean && make`, `make test`, `make asan` from a clean tree; exact counts recorded.

**s69 expected outcome:** `timg foo.png` shows the picture at the right aspect with the prompt under
it, text stays readable on top of it, build stays warning-free, test count rises, no open issues.

**s69 state at start (measured, before any edit this session):** `git status -sb` -> `main` ==
`origin/main`, modified-uncommitted: `AGENT.md Makefile include/kai-term/kai-term.h src/pty.c
src/vt.c` (the s68 work below). `make` exit 0; `make test` -> 501 checks 0 failures + MODEL OK +
CLI 86/0/0. So the s68 model/parser work is in the tree, uncommitted, and the renderer/main/tests
half is not started.

**s68 objective:** make `timg <image>` display an image properly in Kai-Term.

MEASURED FIRST (probes in `/home/kaida/Project/.scratch/s68/`: `probe.py`, `probe2.py`, captured
`timg` output `k_raw.ans`, `k_zlib.ans`, `big_k0.ans`, `big_k9.ans`) — timg 1.6.3 at `/usr/bin/timg`:

- Pixelation choices `-p [h|q|s|k|i]`: half/quarter blocks, sixel, **kitty**, iterm2. Auto-detect
  picks **kitty graphics iff the PTY reports pixel geometry** (`ws_xpixel`/`ws_ypixel` in
  `TIOCSWINSZ`, or an answer to `ESC[16t`). With `ws_xpixel=ws_ypixel=0` it prints
  `cell-pixels: -1x-2` and falls back to quarter blocks; with 640x384 at 80x24 it prints
  `cell-pixels: 4x26` and emits kitty. `TERM_PROGRAM`/`LC_TERMINAL` did NOT change the choice here.
- `src/pty.c:pty_setsize()` leaves `ws_xpixel`/`ws_ypixel` zero, so timg never picks kitty today.
- timg's kitty emission is one APC per chunk:
  `ESC_G a=T,i=<id>,q=2,f=100,m=<more> ; <base64> ESC\` — first chunk carries the whole control
  block, continuation chunks carry only `q=2,m=0` (no `i=`). `m=1` = more chunks follow, `m=0` =
  last. Chunk size 3072 raw bytes (4096 base64 chars). Payload is **PNG** (`f=100`, magic
  `8950 4e47 0d0a 1a0a`) even with `--compress=9` — no zlib seen, so `m` is the more-flag here.
  No `s=`/`r=`/`c=` keys: timg sizes the image in PIXELS and relies on the terminal's cell pixel
  size (from winsize) to know how many cells it covers.
- After the image timg emits only `\r\n`, so it relies on `a=T` moving the cursor down past the
  image rows.
- `pkg-config`: zlib 1.3.2 and libpng 1.6.58 are installed (headers + .so present).

PLANNED STEPS:
1. `src/pty.c` — report `ws_xpixel = cols*cell_w`, `ws_ypixel = rows*cell_h` in `TIOCSWINSZ` so
   timg auto-detects graphics and gets the right cell aspect. Needs cell px passed in.
2. `src/vt.c` — APC `ESC _ G` state (growable buffer, ST or BEL terminator, like the OSC path),
   kitty control-block parser (`k=v;k=v;` + base64 payload), chunk assembly, PNG/raw decode to
   RGBA, per-`Term` image store, actions `t`/`T`/`f`/`d`/`m`/`c`, cursor move for `a=T`,
   `q=` query responses.
3. Image placement model: anchor each image to an ABSOLUTE grid row so it scrolls with the text,
   clips at the viewport edges and comes back in scrollback (must not break the `row[]`
   permutation invariant).
4. `src/render.c` — RGBA texture per image, drawn between cell backgrounds and text (text stays
   on top, as kitty does).
5. Tests: extend `tests/test_vt.c` (headless: parser, chunking, base64, placement, delete, cursor
   move, scroll anchoring). `make test`/`make asan` must stay green.
6. Docs: README (feature + any new config key), ISSUES.md limitations, this journal.

EXPECTED OUTCOME: `timg foo.png` in Kai-Term shows the image at the correct aspect ratio, the
prompt lands below it, and it scrolls with the text. Build stays warning-free; test counts go up.

DESIGN DECIDED after reading vt.c/render.c/main.c (all line refs re-read this session):
- **Anchor images by ABSOLUTE grid row, not by viewport row or row pointer.** `Grid.row_base` =
  the document row number of slot 0; slots always hold a contiguous document window (oldest at
  slot 0), so document row A is viewport row `A - row_base - scroll_off`. row_base only moves when
  a row is evicted at the top (`rot_left` of the used region in scroll_up / grid_resize_rows) or
  when the window slides over its scrollback (erase_scrollback) — no per-row bookkeeping, and it
  survives rotation, eviction and reflow. Row pointers were rejected (reflow frees every buffer,
  row_resize reallocs); a per-row ref array parallel to `wrap[]` was rejected because reflow
  rebuilds rows and would have to move refs with the cells.
- **Pixels live in the model** (`KImage.rgba`, premultiplied, same rule as the color-emoji atlas),
  the renderer keeps one GL texture per image keyed by `KImage.serial` (unique per Term, never
  reused) and drops textures for images not on screen. An image atlas was rejected: images are
  megabytes each and most of them sit in scrollback.
- **Two vertex batches instead of one** (backgrounds, then images, then bands+glyphs+chrome) so
  text stays on top of images the way kitty draws it; one extra draw call per visible image.
  The staging buffer is reused between batches (batch A is already on the GPU when B is built).
- **vt.c needs the cell size in px** (the protocol sizes images in pixels, the grid is in cells):
  `Term.cell_w/cell_h` pushed from main.c, and `Pty.cell_w/cell_h` so `pty_setsize()` can fill
  `ws_xpixel/ws_ypixel` — that is what makes timg pick kitty at all. `CSI 16 t` (cell px) and
  `CSI 14 t` (text-area px) answers added for the same reason over ssh.
- **APC parser**: `ESC _` -> PS_APC with a growable buffer + a cap (garbage stream cannot OOM);
  `ESC P` -> PS_DCS discarded, so sixel output no longer leaks into the grid as text.

DONE so far:
- `include/kai-term/kai-term.h`: added `KImage`, `ImgTex`, `ImgView`, `KittyTx`; `PS_APC`/
  `PS_DCS`; Parser fields `apc/apc_len/apc_cap/apc_drop/apc_kind/tx`; `Grid.row_base`;
  Term fields `images/image_bytes/image_serial/image_last_id/cell_w/cell_h`; Pty fields
  `cell_w/cell_h` + `pty_set_cells()`; Render fields `u_img/imgtex/nimgtex/imgtex_cap`;
  prototypes `term_set_cell_px/term_visible_images/term_image_count`.
- `src/pty.c`: `pty_setsize()` fills `ws_xpixel/ws_ypixel` from `Pty.cell_w/cell_h`; new
  `pty_set_cells()` stores them and re-issues `TIOCSWINSZ` (zoom changes the cell without
  changing cols x rows).
- `src/vt.c`: graphics section (base64, zlib-inflate, PNG via libpng simplified API, raw f=24/32,
  premultiply; image list with doc-row anchors, span math, prune, byte budget, delete, place;
  control-block parser; APC/DCS parser states; `CSI 14 t`/`CSI 16 t` answers; `term_respond()`
  replacing the `TERM_RESP` macro body; `term_init`/`term_destroy` free images + parser buffers
  behind the `GRID_VALID` guard; `row_base` maintained in `grid_init`, `scroll_up`,
  `grid_resize_rows`, `grid_reflow`, `erase_scrollback`; `image_prune()` at the end of
  `term_parse`).
- `Makefile`: `libpng zlib` added to `PKG` (pkg-config gives `-I/usr/include/libpng16 -lpng16 -lz`).

MEASURED while wiring this (three protocol facts the captured bytes corrected):
1. **timg separates the control keys with `,` and the payload with `;`**, not `:` as the protocol
   documents: `\033_Ga=T,i=%u,q=2,f=100,m=%d` + `;` + base64 (format strings found in the timg
   binary at offset 261549: `Ga=T,i=%u,q=2,f=100,m=%d`, `,U=1,c=%d,r=%d`, `Gq=2,m=%d;`). The first
   implementation split only at `:` and reported "control block too long (4128 bytes)". Now the
   control block is walked as `letter=value` elements separated by `;` or `,` and the payload is
   whatever follows the last one (base64 contains none of `:`, `;`, `,`).
2. **The APC kind byte must not be part of the payload**: buffering `G` from `ESC _ G` made the
   whole string look like one non-control element, so every chunk was dropped ("APC chunk with no
   action"). `ESC _` now consumes the kind byte separately.
3. timg never emits `a=d` (no `a=d` string in its binary), so image lifetime is entirely the
   terminal's job — the doc-row prune + byte budget are not optional cleanup, they are the only
   thing that reclaims memory.

VERIFIED headless (`.scratch/s68/gfx_probe.c`, links src/vt.c + src/config.c and feeds the
CAPTURED timg bytes through term_parse; cell px 8x16, grid 24x80):
- `k_raw.ans` (single chunk): 1 image, 64x40 px, span 8x3 cells, cursor ends at (0,4) = 3 rows of
  image + timg's own CR LF, first texel `01 01 81 ff` (premultiplied) — PNG decode works.
- `big_k0.ans` (two chunks, `m=1` then `m=0`): 240x180 px image, span 30x12 — chunk assembly works.
- `grid.ans` (4 images): 6 images total, 5 visible, one at crow=-6 (partly above the viewport,
  still drawn) — placement and z/arrival ordering work.
- After 40 more lines: `row_base` unchanged (scrollback had room), `scroll_off` 51, `visible=0`
  while all 6 images are still retained — i.e. the picture scrolls out with its text and stays in
  scrollback instead of being dropped early.
`make` after the vt.c edits: exit 0, 0 warnings.

STILL TO DO: render.c (u_img sampler + a_glyph 3, two batches, per-image texture cache), main.c
(push cell px into Term and Pty), tests/test_vt.c checks, README/ISSUES, then a live run for the
user to confirm by eye.

s69 DONE (renderer half, all four files compiled clean — `make` exit 0, 0 warnings):
- `src/render.c`: `u_img` sampler (texture unit 2) + `a_glyph > 2.5` branch in `fs_src` (same
  premultiplied composite as the color-emoji path, so still no GL blending); `img_tex()` cache of one
  GL texture per `KImage` keyed by `serial`, re-uploaded when `version`/`w`/`h` change, LINEAR +
  CLAMP_TO_EDGE (NPOT is legal without mipmaps on GL 2.1); `img_tex_sweep()` drops textures no image
  claimed this frame; `draw_quads()` (one batch = one upload + one draw, instanced or expanded);
  `setup_draw_state()` (viewport/program/3 samplers/selection colour/mvp, called before each batch);
  `draw_images()` per pane. `render_draw` now: emit all three passes into ONE staging buffer, record
  `nq_bg` after pass 1 + bar chrome, then draw batch A `[0,nq_bg)` -> pictures -> batch B
  `[nq_bg,nquads)`. `glClear` stays once per frame (clearing between batches would erase batch A).
  `render_destroy` deletes the image textures while the context is still current, then frees the array.
  Measured: the two-batch split needed NO duplication of the pass 2/3 emission code — the records are
  fixed-size, so a batch is just a range of the same buffer (an earlier draft of this edit duplicated
  ~200 lines; rejected before it was applied).
- `include/kai-term/kai-term.h`: `Render.img_frame` (the `ImgTex.seen` stamp); `render_draw` comment
  no longer claims one draw call.
- `src/vt.c`: `image_span_rows/cols` now derive the missing dimension from the picture's aspect ratio
  when only one of `c=`/`r=` was given. Reason found while writing the renderer: the span is also what
  `a=T` moves the cursor past and what `image_prune()` measures, so if the renderer scaled one way and
  the model another, the prompt would land on the wrong row. Integer ceil, no math.h.
- `src/main.c`: `layout_apply()` pushes `font.cell_w/cell_h` into every session with
  `term_set_cell_px()` + `pty_set_cells()` in its own loop BEFORE the resize loop — a zoom can change
  the cell without changing cols x rows, so the existing `if (cols == ... ) continue;` would have
  skipped the PTY update.

STILL TO DO (s69): tests/test_vt.c graphics checks, README (feature + libpng/zlib in Requirements),
ISSUES.md limitations, then `make clean && make` + `make test` + `make asan` and a live `timg` run for
the user to confirm by eye.

s69 DONE (tests + three defects the tests found). `tests/test_vt.c` gained a graphics section (39 new
checks, 501 -> 540) with its own base64 ENCODER (the decoder under test is static in vt.c, so a bug in
one cannot hide a bug in the other), a `gfx_feed()` that emits timg's exact chunk shape, and two
embedded byte arrays: an 8x8 RGBA PNG and the same PNG inside a zlib stream (generated with
python3/zlib, so the test needs no image file). Covered: a=T creates + marks shown + moves the cursor;
span from px; premultiply (2 checks); m=1/m=0 chunking incl. the continuation chunk's bytes; a=t not
drawn until a=p; q=1 OK response; PNG and zlib-PNG decode; `c=` without `r=`; z ordering incl. a
negative z; a=d by id and d=A for all; scroll out -> kept, scroll up -> same history row, eviction ->
freed; CSI 3 J drops anchored pictures; reflow keeps the picture on the row after its line; DCS and
non-graphics APC discarded and the parser recovers; `CSI 16 t`/`CSI 14 t` answers and no answer when
the cell size is unknown. The last check leaves 2 images alive so `term_destroy` frees them under
LeakSanitizer.

THREE DEFECTS FOUND BY THE NEW TESTS (all in the s68 code, none visible in the s68 probe):
1. `premultiply()` used `(x*a + 255)/255`: for x=255,a=255 that is 256 and the `(uint8_t)` cast made a
   fully opaque WHITE pixel BLACK. Now `+128` (proper rounding; opaque white stays white, 255x128 ->
   128). The probe's sample texel was `01 01 81 ff` — never white — which is why it passed.
2. `b64_decode()` accumulated into a plain `int` without masking the emitted bits, so by the 5th
   character `acc << 6` overflowed int — UBSan reported it (`src/vt.c:1705: runtime error: left shift
   of 1071648831 by 6 places`) on the first real payload, not on the probe's short one. Now `unsigned
   acc` masked after every emitted byte.
3. `kitty_ctl_parse()` rejected any value with a leading '-', so `z=-5` silently became z=0 and the
   z-ordering test would have failed. Now a leading '-' is part of the number.

ALSO s69: `decode_raw()` no longer assumes what `s=` means. The kitty spec says s= is the BYTE size of
the raw data (width = s/(r*bpp)); s68 assumed it was the WIDTH (payload = s*r*bpp bytes). Nothing on
this box can settle it — timg only ever emits PNG — so the payload length now decides which reading
fits, and both are accepted. UNVERIFIED against a real raw-format client: recorded as a limitation.

VERIFIED (exact commands, this session): `make` exit 0, 0 warnings; `make test` -> 540 checks 0
failures + MODEL OK (0 problems) + CLI 86 checks 0 failures 0 skipped; `make asan` -> 540/0 + MODEL OK,
0 lines matching AddressSanitizer|runtime error|LeakSanitizer|SUMMARY|ERROR.

(Previous task, closed in s67: both source loose ends done — see the s67 journal entry.)

- 6 dangling `ISSUES.md` citations removed from kai-term.h / vt.c / dbg.c / test_vt.c / main.c; the comments
  state the mechanism themselves now. `grep -rn ISSUES src/ include/ tests/` finds no dead pointer.
- Emoji-strike comments in font.c + render.c now attribute 136x128 / 128px to the installed font, and the
  hard-coded `color_px = 128` fallback is `f->px`. **The premise that these were stale was false**: the strike
  really is 136x128 px and `color_px == 128` here (FreeType probe + the app's own `-d` line). The bad "56px"
  figure came from reading CBLC's `bitmapSize` (a byte count) as a pixel size; ENVIRONMENT.md carried it and
  has been corrected.

NEXT ACTION: pick from "Next Steps" (housekeeping — no bug is open, image support is confirmed), or file a
problem with a reproduction.

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
- **2026-10-16, s63 — docs cleanup.** AGENT.md had grown to ~2070 lines, mostly per-session detail
  duplicating era journal + ISSUES.md root causes; `docs/ARCHITECTURE.md` mixed project design with host
  facts, which must not ship inside project. Created `/home/kaida/Project/ENVIRONMENT.md` outside repo
  (host/toolchain/library/tool inventory + every debug technique that works here + traps; project-agnostic;
  name matches content). `docs/ARCHITECTURE.md` deleted (`git rm`, path git-ignored so it cannot return);
  invariants -> "Architecture & Invariants", debug entry points -> "Debugging this project". AGENT.md
  2067 -> 491 lines (189 KB -> 42 KB). README + ISSUES refs and `.gitignore` updated. Committed `3351214`;
  pushed in s64.
- **2026-10-17, s64 — user confirmation + issue clear-out + push.** User viewed tab bar + side-by-side
  view: "it works as expected and looks great" -> multiplexing confirmed by eye, not only by log lines. Asked
  to clear 4 ISSUES.md "Open" items (never reproduced here, not established Kai-Term defects):
  `## Open` (62 lines) -> `## Cleared (2026-10-17)`, one line each, kept as unproven rather than moved to
  "Fixed", `Open #1`-`#4` labels kept so dated entries resolve; 705 -> 673 lines. Dangling cross-refs fixed
  ("Fixed: Fullscreen" entry, Current Status, Next Steps, file tree, README Documentation line). Committed
  `dbcc597`, `a2a5a54`, `1ab9dc8`; `git fetch` first (0 behind -> fast-forward push).
- **2026-10-17, s65 — AGENT.md refactor, 4 passes.** Measured first: 526 lines / 45.8 KB; sentence-level diff
  against ISSUES.md -> only 3 near-duplicate pairs, so size was density, not copy-paste from ISSUES.md.
  P1 structure (`867d50e`): 10 prose walls -> bulleted invariants per module; project facts (deps, naming,
  layout, license, git) moved Key Decisions -> "Project Files & Facts", leaving Key Decisions as bug traps +
  working rules; journal eras merged; s63/s64 cut from blow-by-blow to era level (their host inventory dropped
  only after `pkg-config --modversion` re-checks put harfbuzz/freetype2/fontconfig versions into
  ENVIRONMENT.md); 3 rules were stated twice (once as invariant, once as pitfall) -> merged into the
  invariant, which gained 2 failure modes it lacked (reflow traps; `GRID_VALID` + `32511` measurement;
  empty-commit frame callback + `solicit_frame()` deletion note).
  P2 words: telegraphic style — connectives + narrative framing removed; `->`/`=` for causality; one fact per
  line.
  P3: journal + per-file descriptions cut to minimum; deps/license trimmed to what README does not already
  carry (README "Requirements"/"License").
  P4: mechanical article strip — `the ` removed outside code spans, quoted log strings and fenced blocks
  (152 lines changed), indentation preserved; verified protected strings intact (`the pty of tab i/n closed`,
  `the last session is gone, exiting`); one line left reading badly was rewritten by hand.
  RESULT: 526 -> 469 lines, 45.8 -> 37.2 KB (-18.7%), 6679 -> 5232 words (-21.7%). Line count is a poor proxy
  for context cost — bytes/words are what matter.
  Fact check vs pre-refactor text: 174 identifiers, 5 absent — 3 live in ISSUES.md/ENVIRONMENT.md, 2 are dead
  `/tmp` log paths; 0 project facts lost. Numbers dropped are refactor bookkeeping only. Docs only — no
  source, header, Makefile or test file touched; `make`/`make test`/`make asan` re-run from a clean tree after
  each pass; numbers in Current Status are those runs. A `DESIGN.md` split (Architecture + Debugging are ~40%
  of file) was considered and NOT done — user chose word-level compression.
- **2026-10-17, s66 — ISSUES.md gutted to current + verified info only.** User directive: keep only
  up-to-date relevant info; the file should be nearly empty unless a real verified problem exists.
  VERIFIED FIRST from a clean tree: `make` exit 0 / 0 warnings; `make test` 501/0 + MODEL OK + CLI 86/0/0;
  `make asan` 501/0 + MODEL OK, 0 sanitizer reports; `grep TODO|FIXME|XXX|HACK` over src/ include/ tests/ ->
  only a test payload string. So no verified open problem exists.
  BEFORE: 673 lines / 9059 words — 78.9% of words in 54 historical "Fixed" root-cause entries, 15.2% Known
  limitations, 4.0% the 4 unproven "Cleared" reports.
  AFTER: 100 lines / 1149 words. `## Open` = none, with the verification evidence and the rule for filing
  (symptom + exact reproduction + what `-d` logs; not reproducible -> not filed). `## Known limitations` =
  all 19 limitations kept, grouped Grid/scrollback, Selection/clipboard, Sessions/layout, Rendering/input,
  each re-checked against the source rather than trusted: `rcap` fixed at startup (vt.c:255) and
  `sb_limit()` (vt.c:245); `term_select_clear` on scroll (main.c:307) and resize (vt.c:728);
  `copy_on_release = 0` default (config.c:48); `CLUSTER_MAX 8` (header:34); color atlas 1024 -> 8192
  (font.c:22,24); only `text/plain` offered (main.c:1379) and no `wl_data_offer_accept` call anywhere;
  window title from `mux_title()` (main.c:451-452) with OSC title kept in `t->title` (vt.c:1615); alt screen
  via `grid_resize_plain()` (vt.c:572,713); floor-division remainder (vt.c:689-693); min size 200x100
  (main.c:1598); `PASTE_BUF_MAX` 8 MiB sent raw on overflow (main.c:155,1980,1403); DEC 2004 gate
  (vt.c:1397,1414); `repeat_info` logged but deliberately unused (main.c:160-164,1448); `split_max` clamp +
  `*` mark in `bar_label` (layout.c:52,67).
  DROPPED: the 54 "Fixed" entries and the 4 "Cleared" reports — history, not current info, and neither was a
  verified current problem. Recoverable: `git show d5afe4a:ISSUES.md`. The rules those bugs produced stay in
  "Architecture & Invariants", which is what an agent should read.
  Dangling refs fixed: AGENT.md preamble, journal header, Architecture header, Next Steps #1, Project Files
  tree, Current Status; README Documentation line. Historical journal entries that mention the old sections
  were left alone — they record what was really done.
  STALE FACT FOUND while re-checking: ISSUES.md and `src/font.c` comments (529, 794) claimed a single
  **128px** CBDT strike for the installed NotoColorEmoji.ttf. Reading the CBLC size table directly: exactly
  one strike, `numSizes=1`, `strikeSize=56`; `fc-scan` gives `pixelsize=109`, `unitsPerEm=2048`. Behaviour
  (bitmap-only -> one strike -> zoom scales, never re-rasterizes) is real; the px number is a host fact, so
  ISSUES.md now states the behaviour with no hard-coded number and the measurement went to ENVIRONMENT.md
  ("Fonts", outside the repo). The two stale `font.c` comments + the 128 fallback constant (font.c:574) are
  a source change — left for the user to approve.
  **CORRECTED IN s67 — the 56px figure above is wrong.** It came from reading CBLC's `bitmapSize` (a BYTE
  count) as a pixel size. FreeType and the running app both report the strike as 136x128 px /
  `color_px = 128`, so font.c was right and ENVIRONMENT.md was polluted with the bad number.
  Docs only — no source, header, Makefile or test file touched. `make`/`make test`/`make asan` re-run from a
  clean tree afterwards; numbers in Current Status are those runs.

- **2026-10-17, s66b — s66 work committed, verification re-run independently.** `git status` showed AGENT.md,
  ISSUES.md and README.md modified but uncommitted (the s66 work above); that was the outstanding commit.
  Re-ran the verification myself from a clean tree instead of trusting the entry: `make clean && make` exit 0
  with 0 warnings; `make test` -> 501 checks 0 failures + MODEL OK + CLI 86/0/0; `make asan` -> 501/0 +
  MODEL OK, no sanitizer report; probe grep empty; only `TODO|FIXME|XXX|HACK` hit is the `XXXXXXXXXX` payload
  at tests/test_vt.c:667. Counts the entry claims also confirmed: `git show d5afe4a:ISSUES.md` = 673 lines
  with 54 `## Fixed` entries and 4 `Cleared` items; new ISSUES.md = 100 lines / 1149 words with 19 limitation
  bullets (19 before -> 19 after, none lost).
  Committed `a59e7ae` (3 files, +148/-686). NOT pushed — user asked for the commit only; `main` is 3 ahead.
  NEW FINDING while checking cross-refs: 6 code comments still cite ISSUES.md sections this commit deleted
  (listed under Current Task). s66's "dangling refs fixed" covered the docs, which is what it claimed, but the
  code side is still dangling — a source change, so left for the user.
  Dating note: `date` on this host reports 2026-10-16 while sessions 64-66 are dated 2026-10-17 — journal
  convention and the host clock disagree. Keep the journal's convention; do not rewrite past entries.
- **2026-10-17, s67 — objective: clear the two source loose ends, then make origin/main match local main.**
  User approved both source fixes (font.c emoji-stale claims + the 6 dangling ISSUES.md refs in code) and
  asked for the remote to fit the local tree (= push, `git fetch` first).
  MEASURED BEFORE TOUCHING CODE — and the first loose end turned out to be based on a FALSE premise:
  `.scratch/s67/facts.c` (FreeType, the API the code actually uses) on
  `/usr/share/fonts/noto-emoji/NotoColorEmoji.ttf`: `FT_IS_SCALABLE=0`, `num_fixed_sizes=1`,
  `available_sizes[0] = 136x128`, after `FT_Select_Size(0)`: `metrics.height/64 = 128`, `y_ppem = 109`,
  `unitsPerEm = 0`; loaded color bitmaps really are 136x128 px (U+1F600 gid 883, U+1F1FA also 136x128).
  The app agrees: `./bin/kai-term -d -e true` logs `color emoji: … (128px strike)`, i.e. `f->color_px == 128`.
  So font.c's "128px strike" / "136x128 px per glyph" comments and its 128 fallback are CORRECT for this
  box, and the s66 entry's "single 56px strike" is WRONG. Where 56 comes from: a hand-rolled CBLC parse read
  a **byte-count** field as a pixel size. Re-parsing CBLC properly (header is 8 bytes: major 3, minor 0,
  numSizes 1; BitmapSize record at +8) gives `bitmapSize = 56` BYTES of index data, `bitmapOffset = 16000`,
  `indexFormat = 3` — 56 is a size in bytes, never a px dimension. Evidence: `.scratch/s67/facts.c`.
  CONSEQUENCE: the stale host fact is in ENVIRONMENT.md ("Fonts", outside the repo) and in the s66 journal
  entry, not in font.c. Both get corrected; font.c keeps its numbers but stops asserting them as constants.
  PLANNED STEPS: (1) font.c — attribute the px figures to the installed font + replace the magic 128 fallback
  with `f->px` (font_size is clamped 6..256 at config.c:288, so it is always > 0; the branch is unreachable
  here since metrics.height/64 = 128 and y_ppem = 109 both > 0). (2) render.c:761 — same host-specific
  "128px on this box" claim. (3) the 6 dangling ISSUES.md citations in kai-term.h/vt.c/dbg.c/test_vt.c/main.c
  -> comments state the mechanism (they already do) and stop pointing at deleted sections. (4) ENVIRONMENT.md
  Fonts section corrected. (5) `make`/`make test`/`make asan` from a clean tree + the `-d` emoji line re-read.
  (6) commit, `git fetch`, push so origin/main == local main.
  EXPECTED OUTCOME: no behaviour change (comments + one unreachable-branch fallback), 0 warnings, same test
  counts, and a remote that matches the local tree.
  DONE (1) src/font.c: 4 comment sites now attribute the figures to the installed font instead of stating
  them as properties of "Noto Color Emoji" (18, 528-530, 568-571, 796-800), and the magic `f->color_px = 128`
  fallback became `f->px` with the reason written next to it (color_px is the `cell_h / color_px` divisor, so
  it must stay > 0; font_size is clamped 6..256 at config.c:288, so f->px is; measured unreachable here).
  Mistake made and fixed in-session: the font.c edit first produced `(int)face->size->metrics.y_ppem);` — a
  stray paren — caught by compiling right after the edit (`make` exit 0, 0 warnings afterwards).
  DONE (2) include/kai-term/kai-term.h:192 — dead `(ISSUES.md #4)` citation dropped; the comment already
  states the mechanism, so it now ends `(the zoom leftover-lines bug)`.
  (Second in-session slip: this very append replaced the paragraph above instead of following it — the journal
  edit anchored on the wrong line. Re-added. Anchor on the LAST line of the entry, not on a line you mean to
  keep above it.)
  DONE (3) src/render.c:761 — "(128px strike on this box)" -> the strike's own pixel height as the font
  reports it, 128 named as the installed font's value.
  DONE (4) the 6 dangling ISSUES.md citations removed: kai-term.h:192, src/vt.c:494, tests/dbg.c:158,
  tests/test_vt.c:438 (all four just drop the dead `ISSUES.md #4` tag — each comment already states the
  mechanism, so the citation added nothing), src/main.c:187 and src/main.c:647 (now say the root causes are in
  git history, not in ISSUES.md). No git SHAs put in source comments — they age; the mechanism is stated
  inline instead.
  CHECKED after: `grep -rn ISSUES src/ include/ tests/ Makefile` -> only the two new "not in ISSUES.md"
  sentences; `grep -n 128 src/font.c src/render.c` -> every hit attributed to the installed font.
  DONE (5) `/home/kaida/Project/ENVIRONMENT.md` "Fonts" (outside the repo) rewritten with the measured
  numbers and a new trap entry: do not parse CBLC by hand, `bitmapSize` is a byte count; read the font through
  FreeType or read what the program logs. This is where the false 56px fact lived and it is what would have
  re-infected the next session.
  VERIFIED from a clean tree (`make clean` first), exact commands and results:
  `make` -> exit 0, 0 warnings, 0 errors; `make test` -> exit 0, 501 checks 0 failures + MODEL OK (0 problems)
  + CLI: 86 checks 0 failures 0 skipped; `make asan` -> exit 0, 501/0 + MODEL OK, 0 lines matching
  AddressSanitizer|runtime error|LeakSanitizer|SUMMARY. Behaviour unchanged, and proven rather than assumed:
  `timeout 6 ./bin/kai-term -d -e true` still logs `color emoji: … (128px strike)` with 0 warn/error lines,
  so the primary metrics path is taken and the rewritten fallback is not reached on this box.
  DONE (6) remote synced, on request ("make sure the git version fits with the local version"). `git fetch
  origin` first: 0 behind / 6 ahead, nothing on GitHub to integrate -> fast-forward `git push origin main`
  pushed `1ab9dc8..eb7ae75` (exit 0). Re-fetched and verified: `main` == `origin/main` == `eb7ae75`,
  0 behind / 0 ahead, `git diff main origin/main` empty. The journal commit recording this push is itself
  pushed, so the two stay equal.

- **2026-10-17, s70 — image support finished: docs, three defects found by testing the docs.** Started from
  the s69 handoff (s68+s69 code in the tree, uncommitted; README/ISSUES untouched). Re-verified the tree
  myself first (clean `make` 0 warnings; `make test` 542/0 + MODEL OK + CLI 86/0/0; `make asan` 542/0 +
  MODEL OK, no reports) instead of trusting the s69 numbers.
  **The README draft was the thing that caught the bugs.** Writing an honest example meant proving what
  `timg` really does against Kai-Term, and the s68 "auto-detect picks kitty when the PTY reports pixel
  geometry" claim turned out to be false (the s68 captures came from forced `-p k` runs). Measured with
  `.scratch/s70/{capture,gate}.py` + strings of `/usr/bin/timg`: timg probes `ESC[>q ESC[5n`, then `ESC[c`,
  then `ESC]11;?`; it chooses kitty ONLY for `TERM=xterm-kitty|xterm-ghostty` plus non-zero pixel geometry,
  and it chooses SIXEL whenever the DA1 answer contains `;4` — which Kai-Term's answer did. Combined with
  s68's DCS discard, `timg foo.png` emitted sixel that was thrown away: no picture at all, worse than the
  pre-image quarter blocks. Fixed by dropping the graphics capability bits (4/16/18) from the DA1 answer;
  A/B against a copy of vt.c with the old string proves it (old: `Using sixel graphics`, 8015 bytes of DCS,
  model images=0; new: `Using quarter block`, 26620 bytes of blocks). Also answered `CSI 5 n` with `ESC[0n`
  (never answered; it is half of timg's first probe). Feeding the real `timg -p k -V anim.gif` bytes through
  the model showed a third defect: timg reuses two ids across four frames and `kitty_complete()` appended a
  new image every time, so an animation left a trail — a transmission to an existing id now replaces that
  image (4 pictures -> 2, `version` bumped). Tests 542 -> 553, all green, `make asan` clean; README gained a
  verified "Images" section (libpng/zlib in Requirements, `timg -p k`, no false auto-detect claim) and
  ISSUES.md a "Graphics / images" limitations group. Live log evidence the renderer path runs:
  `graphics: 1 image(s) drawn, 1 texture(s) held`, no GL warning. Visual confirmation was still pending
  then (closed below).
  Rule this session produced: **a documented example is a claim — run it before writing it.** Two of the
  three README examples I drafted were wrong (`--grid=RxC` arranges multiple FILES, not tiles of one image;
  `-V` does not animate in place because timg moves the cursor per frame).
  CLOSED by the user's own verdict in their window: "The image display properly. The feature works as
  expected" — image display is now user-confirmed by eye, joining the other visually confirmed features in
  Current Status. AGENT.md Current Status / Current Task / Next Steps and the ISSUES.md "Open" note were
  updated to say confirmed rather than pending; no source change, so no rebuild was needed (the tree that
  was verified in s70 is the tree that is on `main`).

## Next Steps

1. **File manager: the one open item is the user's eye.** Built, wired into the window, documented and
   green (220 model/layout/config checks, plus the s74 live key-injection run that proves the routing and
   the `cd` follow), but no human has seen the band yet. Hand the user a live run
   (`./bin/kai-term -d`, then `Ctrl+Shift+F`) and record their verdict — this project's rule for anything
   visual. If something looks wrong, start from the `-d` lines `fm: opened on …`,
   `fm: listing <dir> (request N)`, `fm: dropping stale …`, `fm: keyboard -> …` and
   `graphics: … + the file-manager preview`, not from the shader. Two decisions the user may want to
   revisit, both flagged and both one-line changes with different safety implications:
   `Return` on a file types its path instead of running it, and `fm_follow` sends `cd <path>` unprompted.
2. **Image support is done and confirmed by eye** (2026-10-17), and the user has settled its scope:
   "The goal was image support, Not auto-detection by timg so it work fine using `-p k`". So `-p k` IS the
   supported way to show a picture and nothing more is wanted here. Do NOT "improve" this by re-adding the
   graphics bits (4/16/18) to the DA1 answer or by inventing a `TERM` value: measured A/B (s70), advertising
   sixel without implementing it makes the picture vanish entirely, and timg's auto-detect list is hardcoded
   in its binary. Sixel/iTerm2 support would be a NEW feature — only on an explicit request.
3. **The prompt follow is implemented, idempotent and green (s86) — what is owed to the user is the in-window
   confirmation, not more code.** Ask them to run `./bin/kai-term -d`, type a partial command, open the band
   with `Ctrl+Shift+F` and navigate TWICE: the `-d` log must end each navigation with
   `fm: the prompt now holds the browser's directory and what you had typed`, with no
   `dropped the follow …` line, and the prompt must hold the text once, not `text cd <dir>`. If it misbehaves
   in their window, start from the s86 entries and the three predicates under "The prompt invariant" in
   Architecture & Invariants — and diagnose headless (`.scratch/s86/follow2.c`) before touching the model.
   The old "tree is red on 2 checks" item is closed: those were test bugs, fixed in s83 entry 1, and
   `vt.c`'s snapshot logic was never changed for them.
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
7. Scratch cleanup: throwaway probes live in `/home/kaida/Project/.scratch/` (outside project). Tab bar /
   side-by-side is confirmed, so `s61/` + `s61_apinject*` need no longer be kept; keep `s45_old/` +
   `s47_before/` as A/B trees in case another draw-path regression is suspected. `s70/timg_gate.c` is the
   reusable end-to-end image harness — keep it. `s74/inj_keys.sh` + `s74/inj_test.sh` are the file-manager
   input harness and `s74/animprobe.c` the decoder probe — keep those until the feature is confirmed, then
   keep `inj_keys.sh` as the regression harness for the routing gate. Ask before deleting anything outside
   repo — not done.
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
├── tests/test_vt.c              # 719 checks, 0 failures (parser, grid, graphics, config, prompt machinery)
├── tests/test_fm.c              # 235 checks: the file-manager model, previews, decoders, band geometry
├── tests/dbg.c                  # grid-model invariant checker (permutation + scroll_off/live_off bounds)
├── tests/cli_test.sh            # 94 checks driving the REAL bin/kai-term; ONE probe launch gates the
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
- `tests/test_vt.c` — 719 checks: parser, grid, selection, alt screen, `live_off`, emoji clusters, CSI 3 J,
  bracketed paste, `term_init` guard, reflow, `term_grid_geometry`, zoom/alt-screen row-shrink,
  `mouse_sgr_button`, the graphics path (base64/chunking/PNG/zlib/raw/premultiply/placement/delete/scroll
  anchoring/id replacement/DA1-DA2-DSR answers), mux, keybind/keymods parsing, multiplexing config keys,
  `layout_compute` / `layout_grid_size` / `layout_initial_size`, and the prompt machinery the file manager's
  follow is built from (`term_at_prompt()` / `term_prompt_line()` / `term_prompt_line_typed()` /
  `term_prompt_line_is()`, the echo window and its transition rule, the clear -> `cd` -> re-populate sequence
  as the model sees it). `main.c` itself is in no test binary — the machine is covered by
  `.scratch/s86/follow2.c` and the live harness, not here. 2 invariants with dedicated checkers are `row[]`
  permutation (also `tests/dbg.c`) and startup-size round trip.
- `tests/test_fm.c` — 235 checks against a real temporary directory tree (dirs, a dotfile, a dangling
  symlink, a FIFO, an unreadable dir, four image formats, a binary file): scan + sort + caps + truncation,
  the hidden filter and `fm_set_hidden`, selection and scrolling (`fm_row_pos`, `ensure_visible`),
  navigation, the request-id rule (a stale answer is dropped), every preview kind and its error path, the
  highlighter reaching the cells, the pane painters, `fm_image_fit`, and the band's rects inside
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
  splits the grid area VERTICALLY first (a band of `fm_rows` rows on top, terminal pane(s) below at
  `pane_y = grid_y + band_h + gap`), then splits the band horizontally (browser | preview). Sessions stay
  ordinary `pane[]` entries, only shorter — that is what keeps `layout_hit_pane`, `layout_pane_of_tab`,
  `layout_grid_size()`, the focus border and `W.gx/W.gy` working with the band on AND off. `layout_hit_fm()`
  must be checked BEFORE `layout_hit_pane()`.
- The band is clamped, never trusted: `fm_rows` is reduced so the shell keeps `FM_TERM_MIN_ROWS` rows, and
  dropped entirely when even that leaves fewer than `FM_ROWS_MIN`. Clamped in layout.c, not config.c, so no
  caller can ask for a 1-row band — the tab bar's "the terminal wins" rule again.
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
  descriptor exist. The band's size is visible in the ordinary `grid resized to CxR …` line: with the band
  on, `R` is `fm_rows` + gap smaller than without it.
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
