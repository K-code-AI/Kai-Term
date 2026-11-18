#!/usr/bin/env bash
# Closing the real binary under a real Wayland compositor.
#
# The close prompt used to be checked by calling the window's own close from a test, which
# is not how anything closes a window: a desktop sends `xdg_toplevel.close` to the top
# level, and the terminal — wrapped inside a QMainWindow as its central widget at the time
# — never saw it, so every window closed silently whatever was running inside it. Nothing
# in the headless suite noticed, because the headless suite asked the widget nicely.
#
# This runs sway with no display at all and asks it to close the window the way a
# compositor does. It brings its own runtime directory, so it starts its own session
# daemon rather than joining one already running, and leaves nothing behind in either.
set -u

BIN="${KAI_TERM:?KAI_TERM must point at the kai-term binary}"
ROOT=$(mktemp -d "${TMPDIR:-/tmp}/kai-wayland-close.XXXXXX")
RT="$ROOT/runtime"
mkdir -m 0700 "$RT"
# Everything hangs off XDG_RUNTIME_DIR: the compositor's socket and, more importantly, the
# session daemon's — so this runs its own daemon rather than joining one already running,
# and leaves nothing in the real one either way.
export XDG_RUNTIME_DIR="$RT"
LOG="$ROOT/run.log"
TRACE="$RT/kai-term-close.log"
SWAY_PID=""
KAI_PID=""

fail() { echo "FAIL: $1"; cleanup; exit 1; }
cleanup() {
    [ -n "$KAI_PID" ] && kill -9 "$KAI_PID" 2>/dev/null
    [ -n "$SWAY_PID" ] && kill "$SWAY_PID" 2>/dev/null
    wait 2>/dev/null
    rm -rf "$ROOT"
}
trap 'cleanup; exit 1' INT TERM

start_sway() {
    # A config of our own, because sway would otherwise read the one this machine has —
    # which on a developer's box starts whatever they have it start. A headless test has no
    # business launching their applications.
    printf 'output * bg #000000 solid_color\n' > "$ROOT/sway.conf"
    local before after socket try
    before=$(ls "$RT" 2>/dev/null | sort)
    WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 \
        sway --config "$ROOT/sway.conf" > "$ROOT/sway.log" 2>&1 &
    SWAY_PID=$!
    for try in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
        sleep 1
        after=$(ls "$RT" 2>/dev/null | sort)
        socket=$(comm -13 <(echo "$before") <(echo "$after") | grep '^wayland-' | grep -v lock | head -1)
        [ -n "$socket" ] && break
    done
    [ -n "$socket" ] || fail "sway would not start: $(tail -5 "$ROOT/sway.log")"
    export WAYLAND_DISPLAY="$socket"
    export SWAYSOCK=$(ls -t "$RT"/sway-ipc.*.sock 2>/dev/null | head -1)
}

# Close the window the way a compositor does and report whether the program is still
# standing. A window that asks is a window that is still there: the question is modal.
close_window_and_report() {
    swaymsg "[pid=$KAI_PID] kill" > /dev/null 2>&1 || fail "the compositor refused to close the window"
    sleep 3
    if kill -0 "$KAI_PID" 2>/dev/null; then
        echo "asked"
    else
        echo "closed"
    fi
}

start_sway

# 1. A close that is worth asking about. `KAI_CONFIRM_QUIT=always` is used rather than a
#    running program because putting a program in the foreground needs somebody to type,
#    and whether the window can be stopped at all does not depend on what is in it.
: > "$TRACE"
KAI_DEBUG_CLOSE=1 KAI_CONFIRM_QUIT=always "$BIN" > "$LOG" 2>&1 &
KAI_PID=$!
sleep 5
[ "$(close_window_and_report)" = "asked" ] || fail "the compositor closed the window and nobody was asked
--- what the program said ---
$(cat "$LOG")
--- what the close said ---
$(cat "$TRACE" 2>/dev/null || echo "(nothing at all: the close never reached the window)")"
grep -q "would ask: Quit Kai-Term?" "$TRACE" || fail "the window stayed but the trace does not say it asked:
$(cat "$TRACE")"
kill -9 "$KAI_PID" 2>/dev/null; wait "$KAI_PID" 2>/dev/null

# 2. A close with nothing to ask about still goes straight through, so the first result is
#    not simply "Kai-Term never closes".
: > "$TRACE"
KAI_DEBUG_CLOSE=1 "$BIN" > "$LOG" 2>&1 &
KAI_PID=$!
sleep 5
[ "$(close_window_and_report)" = "closed" ] || fail "a window with nothing running and nothing kept stayed open"
grep -q "nothing to ask" "$TRACE" || fail "the quiet close left no trace of why it was quiet:
$(cat "$TRACE" 2>/dev/null || echo "(nothing)")"
kill -9 "$KAI_PID" 2>/dev/null; wait "$KAI_PID" 2>/dev/null

cleanup
echo "PASS: 2 wayland close checks passed"
