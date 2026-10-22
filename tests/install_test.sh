#!/bin/sh
# Kai-Term install/uninstall regression suite (make test-install).
#
# WHY A SCRIPT: install and uninstall are Makefile recipes, so the only way to check them is to run
# them. They run against a throwaway DESTDIR and never against the host: /etc/kai-term,
# /usr/local/bin and /usr/share are exactly what the targets write, and exactly what must stay
# untouched by a test.
#
# Usage: tests/install_test.sh          (from the project root)
# Prints "INSTALL: <n> checks, <m> failures" and exits non-zero if anything failed.

MAKE_CMD=${MAKE:-make}
if [ ! -f Makefile ] || [ ! -d config ]; then
    echo "install_test: run from the Kai-Term project root" >&2
    exit 1
fi

checks=0
fails=0
skips=0
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

cklist() {  # cklist <file-of-got> <file-of-want> <name>
    checks=$((checks + 1))
    if cmp -s "$1" "$2"; then
        return 0
    fi
    fails=$((fails + 1))
    echo "FAIL: $3"
    diff "$2" "$1" | sed 's/^/   /'
}

skip() {  # skip <name> <why>
    skips=$((skips + 1))
    echo "SKIP: $1 ($2)"
}

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT
stage="$tmp/stage"
S="$stage/usr/local"
CONF="$stage/etc/kai-term"

# Four files install did NOT make and must therefore still be there afterwards: a neighbour in the
# bin dir, a sysadmin's own file in the config dir, an icon size a PREVIOUS install left behind (the
# .ico can change), and another application's icon in a size directory we also fill.
mkdir -p "$stage/usr/local/bin" "$stage/etc/kai-term" \
         "$stage/usr/local/share/icons/hicolor/22x22/apps" \
         "$stage/usr/local/share/icons/hicolor/48x48/apps" || exit 1
: > "$stage/usr/local/bin/other-tool"
printf 'font_size = 9\n' > "$stage/etc/kai-term/site.conf"
: > "$stage/usr/local/share/icons/hicolor/22x22/apps/kai-term.png"
: > "$stage/usr/local/share/icons/hicolor/48x48/apps/other.png"

$MAKE_CMD install DESTDIR="$stage" > "$tmp/install.log" 2>&1
rc=$?
ck "$rc" 0 "make install DESTDIR=... exits 0"
[ "$rc" -eq 0 ] || sed 's/^/   /' "$tmp/install.log"

# ---- the binary -------------------------------------------------------------------------
ck "$([ -f "$S/bin/kai-term" ] && echo yes || echo no)" yes "the binary lands in \$(PREFIX)/bin"
ck "$(cmp -s bin/kai-term "$S/bin/kai-term" && echo same || echo differ)" same \
   "the installed binary is the one this tree built"
ck "$(stat -c %a "$S/bin/kai-term" 2>/dev/null)" 755 "installed binary mode"
"$S/bin/kai-term" -v > "$tmp/ver" 2>&1
ck "$?" 0 "the installed binary runs"
ck "$(grep -c '^kai-term ' "$tmp/ver")" 1 "and it reports itself as kai-term"

# ---- the config templates ---------------------------------------------------------------
for f in kai-term.conf kai-term.bind; do
    ck "$(cmp -s "config/$f" "$CONF/$f" && echo same || echo differ)" same \
       "$f installed where cfg_find() searches"
    ck "$(stat -c %a "$CONF/$f" 2>/dev/null)" 644 "$f mode"
done
ck "$(cmp -s config/kai-term.theme "$CONF/themes/kai-term.theme" && echo same || echo differ)" same \
   "the theme template lands in themes/, which is how theme_find() looks it up"

# The Makefile's CONFDIR must be the path the BINARY has hard-coded, or the templates install into a
# directory nothing ever reads. Derived from both sides rather than written out here.
src_conf=$(grep -o '/etc/kai-term' src/config.c | head -1)
mk_conf=$(sed -n 's/^CONFDIR *: *= *//p' Makefile | head -1)
ck "$mk_conf" "$src_conf" "Makefile CONFDIR is the path src/config.c searches"

# ---- the desktop entry ------------------------------------------------------------------
desk="$S/share/applications/kai-term.desktop"
ck "$([ -f "$desk" ] && echo yes || echo no)" yes "the .desktop file lands in share/applications"
dval() { sed -n "s/^$1=//p" "$desk" | head -1; }
ck "$(head -1 "$desk")" "[Desktop Entry]" "the entry starts with its section header"
ck "$(dval Type)" "Application" "Type"
ck "$(dval Terminal)" "false" "Terminal (a terminal must not be started inside a terminal)"
ck "$(printf '%s' "$(dval Categories)" | grep -c 'TerminalEmulator;')" 1 \
   "Categories names TerminalEmulator"
ck "$(dval Name)" "Kai-Term" "Name"
# Exec must name the binary this target installed, and Icon must name an icon it installed.
ck "$(basename "$(dval Exec)")" "kai-term" "Exec names the installed binary"
icon=$(dval Icon)
ck "$icon" "kai-term" "Icon is an icon-theme name, not a path"
ck "$([ -f "$S/share/icons/hicolor/48x48/apps/$icon.png" ] && echo yes || echo no)" yes \
   "that name resolves in the icon theme install into"
# StartupWMClass is how a shell groups a window with its entry; it must equal the app_id the binary sets.
app_id=$(sed -n 's/^#define DEF_APP_ID[[:space:]]*"\([^"]*\)".*/\1/p' include/kai-term/kai-term.h)
ck "$(dval StartupWMClass)" "$app_id" "StartupWMClass equals DEF_APP_ID"
if command -v desktop-file-validate > /dev/null 2>&1; then
    desktop-file-validate "$desk" > "$tmp/dfv" 2>&1
    ck "$?" 0 "desktop-file-validate accepts the entry"
    [ -s "$tmp/dfv" ] && sed 's/^/   /' "$tmp/dfv"
else
    skip "desktop-file-validate" "not installed on this host"
fi

# ---- the icons --------------------------------------------------------------------------
nsrc=$(find icons -name 'kai-term.png' -type f | wc -l)
nico=$(od -An -j4 -N2 -tu2 Kai-Term.ico | tr -d ' ')  # the .ico's own image count
ck "$nsrc" "$nico" "one committed PNG per image in Kai-Term.ico"
same=0
for p in icons/*/apps/kai-term.png; do
    d="$S/share/icons/hicolor/${p#icons/}"
    [ -f "$d" ] && cmp -s "$p" "$d" && same=$((same + 1))
done
ck "$same" "$nsrc" "every icon PNG is installed byte-for-byte at its hicolor size"
ck "$(stat -c %a "$S/share/icons/hicolor/48x48/apps/kai-term.png" 2>/dev/null)" 644 "icon mode"

# ---- install is idempotent, and it writes nothing else ----------------------------------
$MAKE_CMD install DESTDIR="$stage" > "$tmp/install2.log" 2>&1
ck "$?" 0 "a second make install over its own files exits 0"
{
    echo "$S/bin/kai-term"
    echo "$S/bin/other-tool"
    echo "$CONF/kai-term.bind"
    echo "$CONF/kai-term.conf"
    echo "$CONF/site.conf"
    echo "$CONF/themes/kai-term.theme"
    echo "$S/share/applications/kai-term.desktop"
    echo "$S/share/icons/hicolor/22x22/apps/kai-term.png"
    echo "$S/share/icons/hicolor/48x48/apps/other.png"
    for p in icons/*/apps/kai-term.png; do echo "$S/share/icons/hicolor/${p#icons/}"; done
} | sort > "$tmp/want"
find "$stage" -type f | sort > "$tmp/got"
cklist "$tmp/got" "$tmp/want" "install created exactly those files and no others"

# ---- uninstall --------------------------------------------------------------------------
$MAKE_CMD uninstall DESTDIR="$stage" > "$tmp/uninstall.log" 2>&1
rc=$?
ck "$rc" 0 "make uninstall DESTDIR=... exits 0"
[ "$rc" -eq 0 ] || sed 's/^/   /' "$tmp/uninstall.log"

# Only the four planted files may survive. The 22x22 kai-term.png is the one an OLD install left at a
# size the current .ico no longer ships; globbing the destination is what removes it.
{
    echo "$S/bin/other-tool"
    echo "$CONF/site.conf"
    echo "$S/share/icons/hicolor/48x48/apps/other.png"
} | sort > "$tmp/want2"
find "$stage" -type f | sort > "$tmp/got2"
cklist "$tmp/got2" "$tmp/want2" "uninstall removed every file install made, and only those"

ck "$([ -d "$CONF" ] && echo yes || echo no)" yes "the config dir survives, because it still holds site.conf"
ck "$([ -d "$CONF/themes" ] && echo yes || echo no)" no "the themes dir it made and emptied is gone"
ck "$([ -d "$S/share/icons/hicolor/22x22" ] && echo yes || echo no)" no \
   "the empty icon size dir it made is gone"
ck "$([ -d "$S/share/icons/hicolor/48x48" ] && echo yes || echo no)" yes \
   "a size dir still holding another app's icon survives"
ck "$([ -d "$S/bin" ] && echo yes || echo no)" yes "shared \$(PREFIX)/bin is left alone"
ck "$([ -d "$S/share/applications" ] && echo yes || echo no)" yes "shared share/applications is left alone"

# ---- a relocated install still puts the config where the binary reads it -----------------
stage2="$tmp/stage2"
$MAKE_CMD install DESTDIR="$stage2" PREFIX=/usr > "$tmp/install3.log" 2>&1
ck "$?" 0 "make install PREFIX=/usr exits 0"
ck "$([ -f "$stage2/usr/bin/kai-term" ] && echo yes || echo no)" yes "PREFIX moves the binary"
ck "$([ -f "$stage2/etc/kai-term/kai-term.conf" ] && echo yes || echo no)" yes \
   "PREFIX does NOT move /etc/kai-term, because the binary asks for that path"
$MAKE_CMD uninstall DESTDIR="$stage2" PREFIX=/usr > "$tmp/uninstall2.log" 2>&1
ck "$?" 0 "make uninstall PREFIX=/usr exits 0"
ck "$([ -f "$stage2/usr/bin/kai-term" ] && echo yes || echo no)" no "and it removed that binary"
ck "$([ -d "$stage2/etc/kai-term" ] && echo yes || echo no)" no \
   "a config dir install filled and then emptied is removed"
ck "$([ -d "$stage2/etc" ] && echo yes || echo no)" yes "but /etc itself is left alone"

echo "INSTALL: $checks checks, $fails failures${skips:+, $skips skipped}"
[ "$fails" -eq 0 ] || exit 1
exit 0
