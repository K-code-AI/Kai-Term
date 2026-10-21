# Kai-Term build
# Layout: src/*.c -> build/*.o -> bin/kai-term ; tests/*.c -> bin/test_vt, bin/dbg
# Generated Wayland bindings live in build/generated/ (created by wayland-scanner).

CC      ?= cc
# libpng + zlib are for the image protocol (src/vt.c decodes the PNG payloads applications send);
# libjpeg + libwebp are added for the file manager's image previews (src/img.c decodes PNG, JPEG, WebP
# and — in-tree, no library — GIF). See README "Requirements".
PKG     := wayland-client wayland-cursor wayland-egl egl gl xkbcommon freetype2 fontconfig harfbuzz libpng zlib libjpeg libwebp
# The whole tree (src/ and tests/) is warning-free under this set, on gcc 15 and on clang 22. -Wcast-qual is
# deliberately NOT enabled: the wayland-scanner-generated xdg-shell bindings in build/generated/ trigger it, and
# that file is not ours to fix. Measured with clang 22 (s93): the only hit in OUR sources is zinflate()'s
# `zs.next_in = (Bytef *)in` in src/vt.c, which is zlib's own signature (see the comment there), so enabling the
# flag would mean silencing one line to gain nothing else.
WARN    := -Wall -Wextra -Wshadow -Wwrite-strings -Wformat=2 -Wdiscarded-qualifiers
CFLAGS  ?= -O2 -std=gnu11 $(WARN)
CFLAGS  += -D_GNU_SOURCE $(shell pkg-config --cflags $(PKG)) -Iinclude/kai-term -Ibuild/generated
LDLIBS  += $(shell pkg-config --libs $(PKG)) -lm

PROTO   := /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
SRCS    := src/config.c src/theme.c src/vt.c src/img.c src/mux.c src/layout.c src/fm.c src/shellint.c src/pty.c src/url.c src/font.c src/render.c src/main.c
OBJS    := $(patsubst src/%.c,build/%.o,$(SRCS))
GENH    := build/generated/xdg-shell-client-protocol.h
GENC    := build/generated/xdg-shell-protocol.c
HDRS    := include/kai-term/kai-term.h $(GENH)

BIN     := bin/kai-term

all: $(BIN)

# order-only prerequisites: create output dirs without forcing rebuilds
bin build build/generated:
	mkdir -p $@

$(GENH): $(PROTO) | build/generated
	wayland-scanner client-header $< $@

$(GENC): $(PROTO) | build/generated
	wayland-scanner private-code $< $@

build/xdg-shell-protocol.o: $(GENC) $(GENH) | build
	$(CC) $(CFLAGS) -c -o $@ $(GENC)

build/%.o: src/%.c $(HDRS) | build
	$(CC) $(CFLAGS) -c -o $@ $<

$(BIN): $(OBJS) build/xdg-shell-protocol.o | bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

bin/test_vt: tests/test_vt.c build/vt.o build/img.o build/mux.o build/layout.o build/config.o build/theme.o build/shellint.o build/pty.o build/url.o | bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

bin/dbg: tests/dbg.c build/vt.o build/img.o build/mux.o build/layout.o build/config.o build/theme.o build/shellint.o build/pty.o | bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# The file manager model is testable on its own: fm.c needs only the image decoders (img.c) and
# klog/cp_is_wide (vt.c), so this binary pulls in exactly those three (plus shellint.c, which vt.c calls
# to name an integration kind). config.c joined the list in s104 because vt.c's OSC colour argument goes
# through cfg_parse_color — one colour syntax for the whole project, rather than a second parser that can
# drift from the first (the s99 bug was exactly that: a colour reader and a comment reader disagreeing).
# Still no Wayland, no GL, no font: config.c is pure string handling.
bin/test_fm: tests/test_fm.c build/fm.o build/vt.o build/img.o build/config.o build/shellint.o | bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# The renderer's colour decisions are testable without a display: render_chrome() reads a Render struct and
# nothing else (no EGL, no GL, no font, no Term). The binary links render.o plus everything render.o needs
# to LINK (fm.o font.o img.o vt.o shellint.o layout.o mux.o, and config.o because the test starts from a
# real config_default()), but calls nothing that touches a context — that is the seam the dead
# `cursor_color =` bug needed, see tests/test_render.c.
# build/pty.o joined the list with confirm-quit: mux_running() asks pty_fg_busy() whether a session has a
# foreground command, and that is the ONE place tcgetpgrp() is called. pty.c is pure libc (posix_openpt,
# fork, ioctl) — no Wayland, no EGL, no font — and the headless suites never call it: a test hands mux_running()
# a Tab whose pty.mfd is -1, which answers "idle" before any syscall. Linking it is cheaper than forking a
# second copy of the busy-check that the shipped code does not use.
# build/url.o joined it in s133 for the same reason in the opposite direction: the hover underline in render_draw()
# asks url_span_row() which cells of a run sit on a row, and a renderer that calls a function the test binary does
# not link is a renderer that does not build.
bin/test_render: tests/test_render.c build/render.o build/fm.o build/font.o build/img.o build/vt.o build/shellint.o build/layout.o build/mux.o build/config.o build/theme.o build/pty.o build/url.o $(HDRS) | bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: bin/test_vt bin/dbg bin/test_fm bin/test_render test-cli
	./bin/test_vt
	./bin/dbg
	./bin/test_fm
	./bin/test_render

# The option parser lives in main() in src/main.c, which is not linked into bin/test_vt, so the
# command-line surface can only be checked by running the real binary. tests/cli_test.sh does exactly
# that (it skips the window-opening checks when there is no Wayland session).
test-cli: $(BIN)
	sh tests/cli_test.sh ./$(BIN)

# address-sanitizer builds of the VT unit tests (make asan)
ASANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g
# These rules compile the sources directly instead of linking build/*.o, so EVERY source and
# header they consume must be a prerequisite. bin/dbg_asan used to list only tests/dbg.c, which
# left a stale binary on disk: an ASan leak kept being reported from an old src/vt.c that make
# never rebuilt.
bin/test_vt_asan: tests/test_vt.c src/vt.c src/img.c src/mux.c src/layout.c src/config.c src/theme.c src/shellint.c src/pty.c src/url.c $(HDRS) | bin
	$(CC) $(CFLAGS) $(ASANFLAGS) -o $@ tests/test_vt.c src/vt.c src/img.c src/mux.c src/layout.c src/config.c src/theme.c src/shellint.c src/pty.c src/url.c $(LDLIBS)

bin/dbg_asan: tests/dbg.c src/vt.c src/img.c src/mux.c src/layout.c src/config.c src/theme.c src/shellint.c src/pty.c $(HDRS) | bin
	$(CC) $(CFLAGS) $(ASANFLAGS) -o $@ tests/dbg.c src/vt.c src/img.c src/mux.c src/layout.c src/config.c src/theme.c src/shellint.c src/pty.c $(LDLIBS)

bin/test_fm_asan: tests/test_fm.c src/fm.c src/vt.c src/img.c src/config.c src/shellint.c $(HDRS) | bin
	$(CC) $(CFLAGS) $(ASANFLAGS) -o $@ tests/test_fm.c src/fm.c src/vt.c src/img.c src/config.c src/shellint.c $(LDLIBS)

bin/test_render_asan: tests/test_render.c src/render.c src/fm.c src/font.c src/img.c src/vt.c src/shellint.c src/layout.c src/mux.c src/config.c src/theme.c src/pty.c src/url.c $(HDRS) | bin
	$(CC) $(CFLAGS) $(ASANFLAGS) -o $@ tests/test_render.c src/render.c src/fm.c src/font.c src/img.c src/vt.c src/shellint.c src/layout.c src/mux.c src/config.c src/theme.c src/pty.c src/url.c $(LDLIBS)

asan: bin/test_vt_asan bin/dbg_asan bin/test_fm_asan bin/test_render_asan
	./bin/test_vt_asan
	./bin/dbg_asan
	./bin/test_fm_asan
	./bin/test_render_asan

run: $(BIN)
	./$(BIN)

clean:
	rm -rf bin build

.PHONY: all test test-cli asan run clean
