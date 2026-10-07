#!/bin/sh
# The BT Speak .deb (src/platforms/btspeak/deb) without installing it: its maintainer scripts run against a scratch root
# and scratch homes (BTE_ROOT, BTE_HOMES), the firmware "downloaded" from a fake release of this test's own (curl
# stubbed, BTE_FIRMWARE_URL/SHA), so no network, no root and no real User Menu are touched.
#
#   tools/btspeak_deb_test.sh           (after ./build_linux.sh)
#   BTSPEAK_DEB_BREAK=1 ...             the control: postrm never runs, so the menus-restored check must fail
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
fail=0
check() { if [ "$2" = 0 ]; then echo "ok    $1"; else echo "FAIL  $1"; fail=1; fi; }

"$ROOT/src/platforms/btspeak/deb/build_deb.sh" "0.0.0+test" >/dev/null
DEB="$(ls "$ROOT"/build/blazie-emu-btspeak_0.0.0+test_*.deb)"
dpkg-deb -c "$DEB" | grep -E -i '\.(bns|tns|state)$' >/dev/null && fw_in=0 || fw_in=1
check "no firmware inside the .deb" $((1 - fw_in))
dpkg-deb -x "$DEB" "$T/root"
dpkg-deb -e "$DEB" "$T/ctl"

# two fake releases, the pinned one and a later "latest" one: what the real one's share/ssi263-speech holds (the files'
# contents are not the firmware: each says which release it came from), and the latest's SHA256SUMS.txt
mkdir -p "$T/stub" "$T/web/pinned" "$T/web/latest"
for r in pinned latest; do
    mkdir -p "$T/rel-$r/blazie-emu-x/share/ssi263-speech/tns"
    for f in BL2ENG.BNS bl2_2003_warm.state tns/TNSENG.TNS; do echo "$r" > "$T/rel-$r/blazie-emu-x/share/ssi263-speech/$f"; done
    echo "not firmware" > "$T/rel-$r/blazie-emu-x/README-blazie-emu.md"
done
tar -czf "$T/web/pinned/blazie-emu-0.7.0-linux-aarch64.tar.gz" -C "$T/rel-pinned" blazie-emu-x
tar -czf "$T/web/latest/blazie-emu-9.9.0-linux-aarch64.tar.gz" -C "$T/rel-latest" blazie-emu-x
SHA="$(sha256sum "$T/web/pinned/blazie-emu-0.7.0-linux-aarch64.tar.gz" | cut -d' ' -f1)"
(cd "$T/web/latest" && sha256sum blazie-emu-9.9.0-linux-aarch64.tar.gz > SHA256SUMS.txt \
    && echo "0000000000000000000000000000000000000000000000000000000000000000  blazie-emu-9.9.0-windows.zip" >> SHA256SUMS.txt)
# curl stubbed: https://test.invalid/<path> is $T/web/<path>; NO_NETWORK fails everything, NO_PINNED the pinned one
cat > "$T/stub/curl" <<STUB
#!/bin/sh
[ -n "\$NO_NETWORK" ] && exit 6
out=""; url=""
while [ \$# -gt 0 ]; do case "\$1" in -o) out="\$2"; shift ;; https://*) url="\$1" ;; esac; shift; done
case "\$url" in */pinned/*) [ -n "\$NO_PINNED" ] && exit 22 ;; esac
f="$T/web/\${url#https://test.invalid/}"
[ -f "\$f" ] && cp "\$f" "\$out" || exit 22
STUB
chmod +x "$T/stub/curl"

# the homes: a menu, none, not a BT Speak user, no final newline, blank lines
mkdir -p "$T/h/a/BTSpeak" "$T/h/b/BTSpeak" "$T/h/c" "$T/h/d/BTSpeak" "$T/h/e/BTSpeak" "$T/orig"
printf 'User Menu\nPlay TV: run vlc-play\n' > "$T/h/a/BTSpeak/user.menu"
printf 'User Menu\nRadio: run radio' > "$T/h/d/BTSpeak/user.menu"
printf 'User Menu\n\nNotes: run voice-notes\n\n' > "$T/h/e/BTSpeak/user.menu"
for h in a e; do cp "$T/h/$h/BTSpeak/user.menu" "$T/orig/$h"; done
export BTE_ROOT="$T/root" BTE_HOMES="$T/h/*" BTE_FIRMWARE_URL="https://test.invalid/pinned/blazie-emu-0.7.0-linux-aarch64.tar.gz" \
    BTE_FIRMWARE_LATEST="https://test.invalid/latest" BTE_FIRMWARE_SHA="$SHA"
PATH="$T/stub:$PATH"; export PATH
FW="$T/root/usr/lib/blazie-emu-btspeak/share/ssi263-speech"

"$T/ctl/postinst" configure >/dev/null
"$T/ctl/postinst" configure >/dev/null
[ "$(cat "$FW/BL2ENG.BNS" 2>/dev/null)" = pinned ] && [ -f "$FW/tns/TNSENG.TNS" ] && [ ! -e "$FW/README-blazie-emu.md" ]
check "install: the firmware fetched from the pinned release, nothing else of it" $?
n=0; for h in a b d e; do [ "$(grep -c 'run /usr/bin/blazie-emu-btspeak --unit ' "$T/h/$h/BTSpeak/user.menu")" = 3 ] || n=1; done
check "install: three User Menu entries in each BT Speak home, once though installed twice" $n
[ ! -e "$T/h/c/BTSpeak" ]
check "install: a home that is not a BT Speak user's left alone" $?
grep -q '^Radio: run radio$' "$T/h/d/BTSpeak/user.menu"
check "install: a menu's last line without a newline kept whole" $?
head -n 1 "$T/h/b/BTSpeak/user.menu" | grep -q '^User Menu$'
check "install: a missing menu made with its title" $?

"$T/ctl/prerm" remove; [ -n "$BTSPEAK_DEB_BREAK" ] || "$T/ctl/postrm" remove
cmp -s "$T/h/a/BTSpeak/user.menu" "$T/orig/a" && cmp -s "$T/h/e/BTSpeak/user.menu" "$T/orig/e"
check "removal: the menus exactly as they were (blank lines too)" $?
[ ! -e "$T/root/usr/lib/blazie-emu-btspeak/share" ]
check "removal: the fetched firmware gone" $?

NO_NETWORK=1 "$T/ctl/postinst" configure >/dev/null
r=$?; [ $r = 0 ] && [ ! -e "$FW" ] && grep -q blazie-emu-btspeak "$T/h/a/BTSpeak/user.menu"
check "offline: installed all the same, the menu there, no half-fetched firmware" $?
"$T/ctl/prerm" remove; "$T/ctl/postrm" remove

NO_PINNED=1 "$T/ctl/postinst" configure >/dev/null
[ "$(cat "$FW/BL2ENG.BNS" 2>/dev/null)" = latest ]
check "the pinned release gone: the firmware from the latest release, by its SHA256SUMS.txt" $?
"$T/ctl/prerm" remove; "$T/ctl/postrm" remove

BTE_FIRMWARE_SHA=0000000000000000000000000000000000000000000000000000000000000000 "$T/ctl/postinst" configure >/dev/null
[ "$(cat "$FW/BL2ENG.BNS" 2>/dev/null)" = latest ]
check "the pinned download not the same file (checksum): never used, the latest release's instead" $?
"$T/ctl/prerm" remove; "$T/ctl/postrm" remove

cp "$T/web/latest/SHA256SUMS.txt" "$T/sums.good"
sed -i 's/^[0-9a-f]\{64\}  blazie-emu-9.9.0-linux/1111111111111111111111111111111111111111111111111111111111111111  blazie-emu-9.9.0-linux/' \
    "$T/web/latest/SHA256SUMS.txt"
NO_PINNED=1 "$T/ctl/postinst" configure >/dev/null
[ ! -e "$FW" ]
check "the latest release's download not what its SHA256SUMS.txt says: never installed" $?
"$T/ctl/prerm" remove; "$T/ctl/postrm" remove
grep -v linux-aarch64 "$T/sums.good" > "$T/web/latest/SHA256SUMS.txt"
NO_PINNED=1 "$T/ctl/postinst" configure >/dev/null
[ ! -e "$FW" ]
check "the latest release lists no blazie-emu linux-aarch64 download: nothing installed" $?
"$T/ctl/prerm" remove; "$T/ctl/postrm" remove

rm -f "$DEB"
[ $fail = 0 ] && echo "btspeak deb: all passed" || { echo "btspeak deb: FAILED"; exit 1; }
