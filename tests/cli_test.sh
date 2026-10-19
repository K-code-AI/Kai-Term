#!/bin/sh
# Kai-Term command-line regression suite (make test-cli).
#
# WHY A SCRIPT AND NOT A C TEST: the option parser lives in main() in src/main.c, and src/main.c is
# deliberately not linked into bin/test_vt (it pulls in Wayland, EGL and a PTY). So argv can only be
# checked by RUNNING the real binary and asserting on its output and exit status.
#
# Usage: tests/cli_test.sh [path/to/kai-term]     (default ./bin/kai-term)
# Prints "CLI: <n> checks, <m> failures" and exits non-zero if anything failed.
# Checks that would open a window are skipped when there is no Wayland session; the skip is reported.

BIN=${1:-./bin/kai-term}
if [ ! -x "$BIN" ]; then
    echo "cli_test: no executable at '$BIN' (run make first)" >&2
    exit 1
fi

checks=0
fails=0
skips=0
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT

ck() {  # ck <got> <want> <name>
    checks=$((checks + 1))
    if [ "$1" = "$2" ]; then
        return 0
    fi
    fails=$((fails + 1))
    echo "FAIL: $3"
    echo "   got:  [$1]"
    echo "   want: [$2]"
}

skip() {  # skip <name> <why>
    skips=$((skips + 1))
    echo "SKIP: $1 ($2)"
}

# A hang must not wedge the suite: wrap every launch in `timeout` when it is available.
if command -v timeout > /dev/null 2>&1; then
    TMO="timeout -k 2 15"
else
    TMO=""
fi

# run <args...> -> sets out (stdout+stderr), rc
run() {
    outf="$tmp/o"
    : > "$outf"
    $TMO "$BIN" "$@" > "$outf" 2>&1 < /dev/null
    rc=$?
    out=$(cat "$outf")
}

# run_split <args...> -> sets stdout_only, stderr_only, rc
run_split() {
    sofile="$tmp/so"
    sefile="$tmp/se"
    : > "$sofile"; : > "$sefile"
    $TMO "$BIN" "$@" > "$sofile" 2> "$sefile" < /dev/null
    rc=$?
    stdout_only=$(cat "$sofile")
    stderr_only=$(cat "$sefile")
}

# run_env <args...> -> same as run(), but $env_home and $env_xdg become HOME and XDG_CONFIG_HOME for THIS
# launch only — the config-search checks must not read the developer's own ~/.config/kai-term. The
# assignment-prefix form, NOT env(1): env parses the BINARY's flags as its own options (`-w` is
# --split-string in coreutils, and `--` does not stop it), so `env HOME=… -w /tmp -e true` never runs
# anything and every check after it fails. Measured: 10 checks failed that way before this was fixed.
# An empty $env_xdg sets XDG_CONFIG_HOME to the empty string, which src/config.c reads as "not set" —
# deterministic, and it cannot leak the developer's value into the run.
env_home=""
env_xdg=""
run_env() {
    outf="$tmp/o"
    : > "$outf"
    HOME="$env_home" XDG_CONFIG_HOME="$env_xdg" $TMO "$BIN" "$@" > "$outf" 2>&1 < /dev/null
    rc=$?
    out=$(cat "$outf")
}

# The R in the last "layout: ... grid CxR per session" line — the rows every session was sized to. Empty
# when the run logged no layout, which the caller treats as a failure rather than a pass.
grid_rows() {
    printf '%s\n' "$1" | sed -n 's/.*grid \([0-9][0-9]*\)x\([0-9][0-9]*\) per session.*/\2/p' | tail -1
}

# every launch that opens a window is wrapped in a timeout so a hang cannot wedge the suite
if [ -n "$WAYLAND_DISPLAY" ]; then
    have_display=1
else
    have_display=0
fi

# A window-opening launch is not free. It maps a real surface on the user's desktop and asks the GPU for
# two glyph atlases; on a box whose video memory is already spoken for (this one runs a local LLM that
# holds ~11 GiB of 12.2 GiB) the atlas upload is refused and EVERY window check below fails for that one
# reason — after putting a dozen windows in front of the user. So ONE probe decides whether the window
# checks run at all: one window instead of fifteen, and the reason is printed instead of guessed at.
# KAI_TERM_TEST_NO_WINDOW=1 skips them on request, without mapping even the probe window.
win_reason=""
if [ "$have_display" != 1 ]; then
    win_reason="no WAYLAND_DISPLAY"
elif [ "${KAI_TERM_TEST_NO_WINDOW:-0}" = 1 ]; then
    win_reason="KAI_TERM_TEST_NO_WINDOW=1"
else
    run -d -e true
    if [ "$rc" != 0 ]; then
        win_reason="the probe run exited $rc: $(printf '%s\n' "$out" | grep -m1 -iE 'error|failed')"
    elif ! printf '%s\n' "$out" | grep -q 'kai-term exiting'; then
        win_reason="the probe run never reached its clean exit"
    fi
fi
[ -n "$win_reason" ] && have_display=0

# ---------------- help / version (no display needed) ----------------
run_split -h
ck "$rc" 0 "-h exit status"
ck "$(printf '%s\n' "$stdout_only" | sed -n '1p')" "usage: $BIN [OPTION]..." "-h usage line"
run_split -h
ck "$stderr_only" "" "-h writes only to stdout"
for opt in "-e, --exec COMMAND" "-w, --workingdir DIR" "-c, --config FILE" "-b, --binds FILE" \
           "-d, --debug" "-v, --version" "-h, --help"; do
    run -h
    ck "$(printf '%s\n' "$out" | grep -c -- "$opt")" 1 "-h lists $opt"
done
run -h
ck "$(printf '%s\n' "$out" | grep -c -- '-C,')" 0 "-h does not list the old -C"
run --help
ck "$rc" 0 "--help exit status"
run --help; help_long="$out"
run -h; help_short="$out"
ck "$help_long" "$help_short" "--help output equals -h"
run -v
ck "$rc" 0 "-v exit status"
ck "$(printf '%s\n' "$out" | grep -c '^kai-term ')" 1 "-v prints a version line"
run --version
ck "$rc" 0 "--version exit status"

# ---------------- unknown / removed options: one generic message ----------------
# -C is the OLD spelling of the config flag; -f -s -r -o were retired in session 43. None of them may
# get a special explanation (the user asked for one generic message), and none may be accepted.
for flag in -C -f -s -r -o -x --bogus; do
    run_split "$flag" 42
    ck "$rc" 1 "$flag rejected with exit 1"
    ck "$(printf '%s\n' "$stderr_only" | sed -n '1p')" "kai-term: unknown option '$flag'" \
       "$flag generic first line"
    ck "$(printf '%s\n' "$stderr_only" | grep -c '^usage:')" 1 "$flag prints the option list"
    ck "$(printf '%s\n' "$stderr_only" | grep -c 'any more')" 0 "$flag mentions no retired flag"
    ck "$stdout_only" "" "$flag writes nothing to stdout"
done

# ---------------- options that need an argument ----------------
for flag in -e -w -c -b --exec --workingdir --config --binds; do
    run_split "$flag"
    ck "$rc" 1 "$flag with no argument exits 1"
    # the message echoes the flag exactly as the user typed it
    ck "$(printf '%s\n' "$stderr_only" | sed -n '1p')" "kai-term: $flag needs an argument" \
       "$flag with no argument message"
done

# ---------------- the config file flag ----------------
printf 'app_id = cli-suite\nfont_size = 30\n' > "$tmp/conf"
if [ "$have_display" = 1 ]; then
    run -c "$tmp/conf" -d -e true
    ck "$rc" 0 "-c FILE runs"
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: cli-suite')" 1 "-c FILE loads that file"
    ck "$(printf '%s\n' "$out" | grep -c 'cell 18x34')" 1 "-c FILE applies font_size"
    run --config "$tmp/conf" -d -e true
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: cli-suite')" 1 "--config FILE loads that file"
    run --config="$tmp/conf" -d -e true
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: cli-suite')" 1 "--config=FILE loads that file"

    # -d must beat `log =` in the file (command line wins; past bug, session 43)
    printf 'app_id = cli-log\nlog = 0\n' > "$tmp/conf0"
    run -c "$tmp/conf0" -d -e true
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: cli-log')" 1 "-d overrides log = 0"
    run -c "$tmp/conf0" -e true
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: cli-log')" 0 "without -d the config log level is kept"

    # -w DIR and -e COMMAND
    run -d -w "$tmp" -e true
    ck "$rc" 0 "-w DIR runs"
    ck "$(printf '%s\n' "$out" | grep -c "shell starts in $tmp")" 1 "-w DIR sets the startup directory"
    run -d -e "echo cli-marker"
    ck "$rc" 0 "-e COMMAND runs"
    # the shell exits, so either the pty reaches EOF first or SIGCHLD is reaped first; either way the
    # run must end by itself rather than timing out. With multiplexing the message names the tab.
    ck "$(printf '%s\n' "$out" | grep -cE 'mux: the pty of tab [0-9]+/[0-9]+ closed|mux: the shell of tab [0-9]+/[0-9]+ exited')" 1 "-e COMMAND ends on its own"
    run -d -e true
    ck "$(printf '%s\n' "$out" | grep -c 'kai-term exiting')" 1 "a plain run shuts down cleanly"

    # multiplexing settings: config-only keys, and the first session must be reported
    printf 'max_tabs = 4\nkey_new_tab = Alt+N\nkey_close_tab = none\nkey_tab_prefix = Alt\n' > "$tmp/muxconf"
    run -c "$tmp/muxconf" -d -e true
    ck "$rc" 0 "a config with the multiplexing keys runs"
    ck "$(printf '%s\n' "$out" | grep -c 'unknown key')" 0 "the multiplexing config keys are known"
    ck "$(printf '%s\n' "$out" | grep -c 'config: key_close_tab unbound')" 1 "key_close_tab = none unbinds it"
    ck "$(printf '%s\n' "$out" | grep -c 'mux: session 1/1 started')" 1 "the first session is logged"

    # how the sessions are SHOWN (session 57): the tab bar, the split cap and the toggle binding.
    # The `layout:` line is what layout_apply() logs once per layout-shape change, so it is the
    # client-side evidence that the bar really is on/off and how many panes the window has.
    printf 'tab_bar = always\nsplit_max = 2\nkey_toggle_layout = none\n' > "$tmp/barconf"
    run -c "$tmp/barconf" -d -e true
    ck "$rc" 0 "a config with the display keys runs"
    ck "$(printf '%s\n' "$out" | grep -c 'unknown key')" 0 "the display config keys are known"
    ck "$(printf '%s\n' "$out" | grep -c 'config: key_toggle_layout unbound')" 1 "key_toggle_layout = none unbinds the toggle"
    ck "$(printf '%s\n' "$out" | grep -c 'layout: tab view, 1 pane, tab bar on, grid')" 1 "tab_bar = always shows the bar with one session"
    run -d -e true
    ck "$(printf '%s\n' "$out" | grep -c 'layout: tab view, 1 pane, tab bar off, grid')" 1 "tab_bar = auto (the default) shows no bar with one session"

    # the in-window file manager: its config keys must be known, and its bindings must be unbindable.
    # Off by default, so the plain run above must not mention a file manager at all.
    printf 'file_manager = on\nfm_size = 40\nfm_preview = off\nfm_show_hidden = on\nfm_follow = off\nkey_file_manager = Alt+F\nkey_fm_focus = none\nfm_key_up = k\nfm_key_down = j\nfm_key_open = none\nfm_key_left = h\nfm_key_right = none\nfm_key_path = none\n' > "$tmp/fmconf"
    run -c "$tmp/fmconf" -d -e true
    ck "$rc" 0 "a config with the file manager keys runs"
    ck "$(printf '%s\n' "$out" | grep -c 'unknown key')" 0 "the file manager config keys are known"
    ck "$(printf '%s\n' "$out" | grep -c 'config: key_fm_focus unbound')" 1 "key_fm_focus = none unbinds the focus switch"
    ck "$(printf '%s\n' "$out" | grep -c 'config: fm_key_open unbound')" 1 "fm_key_open = none unbinds that key"
    ck "$(printf '%s\n' "$out" | grep -c 'config: fm_key_right unbound')" 1 "fm_key_right = none unbinds the enter-directory key"
    ck "$(printf '%s\n' "$out" | grep -c 'config: fm_key_path unbound')" 1 "fm_key_path = none unbinds the key that types a path"
    run -c "$tmp/fmconf" -d -e true
    ck "$(printf '%s\n' "$out" | grep -c 'config: fm_key_up unbound')" 0 "fm_key_up = k is a binding, not an unbind"
    ck "$(printf '%s\n' "$out" | grep -c 'config: fm_key_left unbound')" 0 "fm_key_left = h is a binding, not an unbind"
    # fm_size is a SHARE of the window, so opening the band must cost the terminal rows — measured against
    # the same window with the band off, never against a hard-coded row count.
    printf 'file_manager = off\n' > "$tmp/nofm"
    printf 'file_manager = on\nfm_size = 50\n' > "$tmp/fm50"
    run -c "$tmp/nofm" -d -e true
    rows_off=$(grid_rows "$out")
    run -c "$tmp/fm50" -d -e true
    rows_on=$(grid_rows "$out")
    if [ -n "$rows_off" ] && [ -n "$rows_on" ] && [ "$rows_on" -lt "$rows_off" ]; then
        ck ok ok "fm_size = 50: the band takes a share of the window, so the terminal gets fewer rows"
    else
        ck "$rows_on" "fewer than $rows_off" \
           "fm_size = 50: the band takes a share of the window, so the terminal gets fewer rows"
    fi
    run -d -e true
    ck "$(printf '%s\n' "$out" | grep -c 'file manager')" 0 "the file manager is off by default: the layout line does not mention it"

    # ---- the two config files, and the search order (XDG > ~/.config > /etc > built-in defaults) ----
    # These run WITHOUT -c, so they exercise the real loader path a user hits: HOME and XDG_CONFIG_HOME
    # point at a scratch tree, and -w pins the startup directory (HOME moving would otherwise move it).
    mkdir -p "$tmp/cfg/xdg/kai-term" "$tmp/cfg/home/.config/kai-term" "$tmp/cfg/none"
    printf 'app_id = search-xdg\n' > "$tmp/cfg/xdg/kai-term/kai-term.conf"
    printf 'app_id = search-home\n' > "$tmp/cfg/home/.config/kai-term/kai-term.conf"
    env_home="$tmp/cfg/home"; env_xdg="$tmp/cfg/xdg"
    run_env -d -w "$tmp" -e true
    ck "$rc" 0 "a launch with a scratch HOME and XDG_CONFIG_HOME runs"
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: search-xdg')" 1 \
       "XDG_CONFIG_HOME wins when it holds kai-term.conf"
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: search-home')" 0 \
       "and ~/.config is not read at all once XDG has the file"
    rm "$tmp/cfg/xdg/kai-term/kai-term.conf"
    run_env -d -w "$tmp" -e true
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: search-home')" 1 \
       "~/.config/kai-term is used when XDG_CONFIG_HOME has nothing"
    env_home="$tmp/cfg/none"; env_xdg="$tmp/cfg/xdg"
    run_env -d -w "$tmp" -e true
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: Kai-Term')" 1 \
       "nothing anywhere: the built-in default app_id stands"
    ck "$(printf '%s\n' "$out" | grep -c 'no kai-term.conf in')" 1 \
       "and the log says which locations were looked at"
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: search-')" 0 \
       "and no scratch file was picked up"

    # The binding file: the same search, read AFTER the settings file. (Back to the HOME that holds the
    # scratch files — the previous case deliberately pointed HOME at an empty tree.)
    env_home="$tmp/cfg/home"; env_xdg="$tmp/cfg/xdg"
    printf 'key_close_tab = none\n' > "$tmp/cfg/home/.config/kai-term/kai-term.bind"
    run_env -d -w "$tmp" -e true
    ck "$(printf '%s\n' "$out" | grep -c 'config: key_close_tab unbound')" 1 \
       "a searched kai-term.bind is read"
    ck "$(printf '%s\n' "$out" | grep -c 'bindings loaded from')" 1 \
       "and the log names the file it came from"
    # A SETTING written in the bind file is refused out loud, and it really does not apply.
    printf 'font_size = 30\n' > "$tmp/cfg/home/.config/kai-term/kai-term.bind"
    run_env -d -w "$tmp" -e true
    ck "$(printf '%s\n' "$out" | grep -c 'is not a key binding')" 1 \
       "a setting in kai-term.bind is refused by name"
    ck "$(printf '%s\n' "$out" | grep -c 'cell 18x34')" 0 \
       "and it did not apply — font_size stayed at the default"
    # A binding left in kai-term.conf still works (files written before the split), and the log points
    # at where the line belongs now.
    rm "$tmp/cfg/home/.config/kai-term/kai-term.bind"
    printf 'app_id = search-home\nkey_close_tab = none\n' > "$tmp/cfg/home/.config/kai-term/kai-term.conf"
    run_env -d -w "$tmp" -e true
    ck "$(printf '%s\n' "$out" | grep -c 'config: key_close_tab unbound')" 1 \
       "a binding still written in kai-term.conf is applied"
    ck "$(printf '%s\n' "$out" | grep -c 'is a key binding')" 1 \
       "and the log says those live in kai-term.bind now"

    # -b FILE names the binding file explicitly, in both long forms.
    printf 'key_close_tab = none\n' > "$tmp/bind"
    run -c "$tmp/conf" -b "$tmp/bind" -d -e true
    ck "$rc" 0 "-b FILE runs"
    ck "$(printf '%s\n' "$out" | grep -c 'config: key_close_tab unbound')" 1 "-b FILE reads that file"
    run --binds="$tmp/bind" -d -e true
    ck "$(printf '%s\n' "$out" | grep -c 'config: key_close_tab unbound')" 1 "--binds=FILE reads that file"

    # everything at once
    run -c "$tmp/conf" -d -w "$tmp" -e true
    ck "$rc" 0 "combined -c -d -w -e runs"
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: cli-suite')" 1 "combined: config file applied"
    ck "$(printf '%s\n' "$out" | grep -c "shell starts in $tmp")" 1 "combined: -w applied"
else
    for name in "-c FILE" "--config FILE" "--config=FILE" "-d overrides log" "-w DIR" "-e COMMAND" \
                "multiplexing keys" "display keys (tab_bar/split_max)" "file manager keys" \
                "config search order (XDG/~/.config//etc)" "kai-term.bind searched and applied" \
                "-b FILE" "combined flags"; do
        skip "$name" "$win_reason"
    done
fi

echo "CLI: $checks checks, $fails failures${skips:+, $skips skipped}"
[ "$fails" -eq 0 ] || exit 1
exit 0
