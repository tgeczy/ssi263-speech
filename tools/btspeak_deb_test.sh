#!/bin/sh
# The BT Speak .deb (src/platforms/btspeak/deb) without installing it: its maintainer scripts run against a scratch root
# and scratch homes (BTE_ROOT, BTE_HOMES), the firmware "downloaded" from a fake release of this test's own (curl
# stubbed, BTE_FIRMWARE_URL/SHA), so no network, no root and no real User Menu are touched.
#
#   tools/btspeak_deb_test.sh           (after ./build_linux.sh)
#   BTSPEAK_DEB_BREAK=1 ...             the control: postrm never runs, so the menus-restored check must fail
#   BTSPEAK_DEB_BREAK=pin ...           the control: build_deb.sh keeps fetch-firmware's own pin for a release's version,
#                                       so the release-pin checks must fail
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
fail=0
check() { if [ "$2" = 0 ]; then echo "ok    $1"; else echo "FAIL  $1"; fail=1; fi; }

# a development build (no version): fetch-firmware's own pin, which the tests below override
"$ROOT/src/platforms/btspeak/deb/build_deb.sh" >/dev/null
DEB="$(ls "$ROOT"/build/blazie-emu-btspeak_0.0.0+dev_*.deb)"
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

# the homes: a menu, none, not a BT Speak user, no final newline, blank lines, an older package's three lines a unit
# each (one renamed by its user)
mkdir -p "$T/h/a/BTSpeak" "$T/h/b/BTSpeak" "$T/h/c" "$T/h/d/BTSpeak" "$T/h/e/BTSpeak" "$T/h/f/BTSpeak" "$T/h/g/BTSpeak" \
    "$T/orig"
printf 'User Menu\nPlay TV: run vlc-play\n' > "$T/h/a/BTSpeak/user.menu"
printf 'User Menu\nRadio: run radio' > "$T/h/d/BTSpeak/user.menu"
printf 'User Menu\n\nNotes: run voice-notes\n\n' > "$T/h/e/BTSpeak/user.menu"
R="run /usr/bin/blazie-emu-btspeak --unit"
printf 'User Menu\nMy Lite: %s bl-en\nBraille '"'"'n Speak 2000: %s bns-en\nType '"'"'n Speak: %s tns-en\nRadio: run radio\n' \
    "$R" "$R" "$R" > "$T/h/f/BTSpeak/user.menu"
# an earlier build's line a unit, by the catalog's names (a release's too), in a menu with Windows line ends
printf 'User Menu\r\nBraille Lite 2000 (English): %s bl-en\r\nBraille Lite 2000 (Czech): %s bl2_cs_1999_07\r\nRadio: run radio\r\n' \
    "$R" "$R" > "$T/h/g/BTSpeak/user.menu"
for h in a e; do cp "$T/h/$h/BTSpeak/user.menu" "$T/orig/$h"; done
export BTE_ROOT="$T/root" BTE_HOMES="$T/h/*" BTE_FIRMWARE_URL="https://test.invalid/pinned/blazie-emu-0.7.0-linux-aarch64.tar.gz" \
    BTE_FIRMWARE_LATEST="https://test.invalid/latest" BTE_FIRMWARE_SHA="$SHA"
PATH="$T/stub:$PATH"; export PATH
FW="$T/root/usr/lib/blazie-emu-btspeak/share/ssi263-speech"

"$T/ctl/postinst" configure >/dev/null
"$T/ctl/postinst" configure >/dev/null
[ "$(cat "$FW/BL2ENG.BNS" 2>/dev/null)" = pinned ] && [ -f "$FW/tns/TNSENG.TNS" ] && [ ! -e "$FW/README-blazie-emu.md" ]
check "install: the firmware fetched from the pinned release, nothing else of it" $?
# one line in each User Menu, opening the Blazie menu (which lists the units itself, each time it opens)
B="Blazie Emulator: menu /usr/lib/blazie-emu-btspeak/blazie.menu"
n=0; for h in a b d e; do
    um="$T/h/$h/BTSpeak/user.menu"
    [ "$(grep -c -x -F "$B" "$um")" = 1 ] && ! grep -q -F "$R " "$um" || n=1
done
check "install: the Blazie menu's line in each BT Speak home, once though installed twice" $n
um="$T/h/f/BTSpeak/user.menu"
grep -q -x "My Lite: $R bl-en" "$um" && ! grep -q -e "bns-en" -e "tns-en" "$um" && grep -q -x -F "$B" "$um" \
    && grep -q -x "Radio: run radio" "$um"
check "install over an older one: its three lines go (the menu lists them), but one its user renamed stays" $?
um="$T/h/g/BTSpeak/user.menu"
[ "$(cat "$um")" = "$(printf 'User Menu\r\nRadio: run radio\r\n%s' "$B")" ]
check "install over an earlier build's line a unit (releases too), CR LF line ends: those go, the rest as it was" $?
printf '%s\r\n' "$B" > "$T/crlf"; cp "$T/crlf" "$T/h/g/BTSpeak/user.menu"
"$T/ctl/postinst" configure >/dev/null
cmp -s "$T/crlf" "$T/h/g/BTSpeak/user.menu"
check "install: a Blazie line ending CR LF is the Blazie line (never a second)" $?
[ ! -e "$T/h/c/BTSpeak" ]
check "install: a home that is not a BT Speak user's left alone" $?
grep -q '^Radio: run radio$' "$T/h/d/BTSpeak/user.menu"
check "install: a menu's last line without a newline kept whole" $?
head -n 1 "$T/h/b/BTSpeak/user.menu" | grep -q '^User Menu$'
check "install: a missing menu made with its title" $?

# the Blazie menu, as packaged: it names the launcher, and More languages is itself again; run here with a stand-in
# for the launcher that lists two catalog units and two releases
M="$T/root/usr/lib/blazie-emu-btspeak"
grep -q -x 'PROGRAM = "/usr/bin/blazie-emu-btspeak"' "$M/blazie.menu" && [ -x "$M/blazie.menu" ] \
    && [ "$(readlink "$M/blazie-languages.menu")" = blazie.menu ]
check "the Blazie menu: executable, naming the launcher, its More languages beside it" $?
mkdir -p "$T/menu"
printf '#!/bin/sh\nprintf "bl-en\\tBraille Lite 2000 (English)\\tcatalog\\ntns-en\\tType '"'"'n Speak (English)\\tcatalog\\nbl2_cs_1999_07\\tBraille Lite 2000 (Czech)\\trelease\\nbl2_ar_ae_1998_12\\tBraille Lite 2000 (Arabic: UAE)\\trelease\\n"\n' \
    > "$T/menu/lister"
chmod +x "$T/menu/lister"
sed "s|^PROGRAM = .*|PROGRAM = \"$T/menu/lister\"|" "$M/blazie.menu" > "$T/menu/blazie.menu"
chmod +x "$T/menu/blazie.menu"; ln -s blazie.menu "$T/menu/blazie-languages.menu"
printf 'Blazie Emulator\n!Braille Lite 2000 (English): run %s --unit bl-en\n!Type '"'"'n Speak (English): run %s --unit tns-en\n!More Languages: menu %s\n' \
    "$T/menu/lister" "$T/menu/lister" "$T/menu/blazie-languages.menu" > "$T/menu/want-top"
printf 'More Languages\n!Braille Lite 2000 (Czech): run %s --unit bl2_cs_1999_07\n!Braille Lite 2000 (Arabic, UAE): run %s --unit bl2_ar_ae_1998_12\n' \
    "$T/menu/lister" "$T/menu/lister" > "$T/menu/want-more"
"$T/menu/blazie.menu" | cmp -s - "$T/menu/want-top" && "$T/menu/blazie-languages.menu" | cmp -s - "$T/menu/want-more"
check "the Blazie menu: the catalog's units, then More languages with the releases (a label's colon not its end)" $?
printf '#!/bin/sh\nprintf "bl-en\\tBraille Lite 2000 (English)\\tcatalog\\n"\n' > "$T/menu/lister"
"$T/menu/blazie.menu" | grep -q "More Languages" && no_more=0 || no_more=1
"$T/menu/blazie-languages.menu" | grep -q "^!No more languages are installed: run say-message" && empty_said=0 || empty_said=1
printf '#!/bin/sh\nexit 1\n' > "$T/menu/lister"
"$T/menu/blazie.menu" | grep -q "^!The Blazie emulator could not list its firmware: run say-message" && told=0 || told=1
[ $no_more = 1 ] && [ $empty_said = 0 ] && [ $told = 0 ]
check "the Blazie menu: no releases, no More Languages (opened all the same: an item saying so); nothing listed, an item that says so" $?
# the launcher, before any firmware is fetched (none in the system's folder or the user's): the Braille Lite alone
[ -d /usr/lib/blazie-emu-btspeak ] && skip=1 || skip=0
if [ $skip = 0 ]; then
    [ "$(HOME="$T/nohome" XDG_DATA_HOME= "$T/root/usr/bin/blazie-emu-btspeak" --list-units)" \
        = "$(printf 'bl-en\tBraille Lite 2000 (English)\tcatalog')" ] && [ ! -e "$T/nohome" ]
    check "the launcher with no firmware anywhere: lists the Braille Lite (whose item fetches it), fetches nothing" $?
fi

"$T/ctl/prerm" remove; [ "$BTSPEAK_DEB_BREAK" = 1 ] || "$T/ctl/postrm" remove
cmp -s "$T/h/a/BTSpeak/user.menu" "$T/orig/a" && cmp -s "$T/h/e/BTSpeak/user.menu" "$T/orig/e"
check "removal: the menus exactly as they were (blank lines too)" $?
[ ! -e "$T/root/usr/lib/blazie-emu-btspeak/share" ]
check "removal: the fetched firmware gone" $?

NO_NETWORK=1 "$T/ctl/postinst" configure >/dev/null
r=$?; [ $r = 0 ] && [ ! -e "$FW" ] && [ "$(grep -c -x -F "$B" "$T/h/a/BTSpeak/user.menu")" = 1 ]
check "offline: installed all the same, the Blazie menu in the User Menu, no half-fetched firmware" $?
# a unit copied from the Blazie menu (Ctrl+C), and the Blazie line renamed: a reconfigure keeps both, adds no second
printf "Type 'n Speak (English): %s tns-en\n" "$R" >> "$T/h/a/BTSpeak/user.menu"
sed -i "s|^Blazie Emulator: menu |Blazie: menu |" "$T/h/a/BTSpeak/user.menu"
NO_NETWORK=1 "$T/ctl/postinst" configure >/dev/null
grep -q -x "Type 'n Speak (English): $R tns-en" "$T/h/a/BTSpeak/user.menu" \
    && [ "$(grep -c ": menu /usr/lib/blazie-emu-btspeak/blazie.menu" "$T/h/a/BTSpeak/user.menu")" = 1 ]
check "reconfigure: a unit copied from the Blazie menu stays, and a renamed Blazie line is not doubled" $?
"$T/ctl/prerm" remove; "$T/ctl/postrm" remove
cmp -s "$T/h/a/BTSpeak/user.menu" "$T/orig/a"
check "removal: the copied unit and the renamed Blazie line go too" $?

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

cp "$T/sums.good" "$T/web/latest/SHA256SUMS.txt"
rm -f "$DEB"

# install.sh --menu (the package's install-btspeak.sh) uses the same User Menu code, and its own Blazie menu, naming
# the blazie_emu it installed; that menu run here lists what the installed firmware can start
mkdir -p "$T/hi/BTSpeak"
printf 'User Menu\nBraille Lite 2000: run %s/prefix/bin/blazie_emu --unit bl-en\nRadio: run radio' "$T" > "$T/hi/BTSpeak/user.menu"
FWDIR="$T/rel-pinned/blazie-emu-x/share/ssi263-speech"
IR="run $T/prefix/bin/blazie_emu --unit"
IM="$T/prefix/lib/blazie-emu/blazie.menu"
HOME="$T/hi" PREFIX="$T/prefix" "$ROOT/src/platforms/btspeak/install.sh" --menu "$FWDIR" >/dev/null 2>&1
um="$T/hi/BTSpeak/user.menu"
[ "$(cat "$um")" = "$(printf 'User Menu\nRadio: run radio\nBlazie Emulator: menu %s' "$IM")" ]
check "install.sh --menu: the Blazie menu's line, the line a unit its older self wrote gone" $?
"$IM" > "$T/im"
grep -q -x "!Braille Lite 2000 (English): $IR bl-en" "$T/im" && grep -q -x "!Type 'n Speak (English): $IR tns-en" "$T/im" \
    && [ "$(grep -c "$IR " "$T/im")" = 2 ] && [ -L "$T/prefix/lib/blazie-emu/blazie-languages.menu" ]
check "install.sh: its Blazie menu, an item per unit its firmware can start" $?
rm -r "$T/prefix/share/ssi263-speech/tns" "$FWDIR/tns"
"$IM" > "$T/im"
[ "$(grep -c "$IR " "$T/im")" = 1 ] && ! grep -q tns-en "$T/im"
check "install.sh: its Blazie menu, without the Type 'n Speak's firmware: that unit gone, nothing to reinstall" $?
printf "Braille Lite 2000 (English): %s bl-en\n" "$IR" >> "$um"
HOME="$T/hi" PREFIX="$T/prefix" "$ROOT/src/platforms/btspeak/uninstall.sh" >/dev/null
[ "$(cat "$um")" = "$(printf 'User Menu\nRadio: run radio')" ] && [ ! -e "$T/prefix/lib/blazie-emu" ]
check "uninstall.sh: the menu as it was (a copied unit gone too), the Blazie menu gone" $?
HOME="$T/hi" PREFIX="$T/a b" "$ROOT/src/platforms/btspeak/install.sh" --menu "$FWDIR" >/dev/null 2>&1
[ "$(cat "$um")" = "$(printf 'User Menu\nRadio: run radio')" ] && [ ! -e "$T/a b/lib/blazie-emu" ] && [ -x "$T/a b/bin/blazie_emu" ]
check "install.sh --menu, a PREFIX with a space: installed, but no Blazie menu and the User Menu untouched" $?
HOME="$T/hi" PREFIX="$T/a b" "$ROOT/src/platforms/btspeak/uninstall.sh" >/dev/null

# a preview built with its own pinned download (BTE_PIN_URL, BTE_PIN_SHA: Tomi's eurpod.com for 0.8's): the package's
# fetch-firmware carries it, and an install with no test overrides of the pin fetches from it
BTE_PIN_URL="https://test.invalid/pinned/blazie-emu-0.7.0-linux-aarch64.tar.gz" BTE_PIN_SHA="$SHA" \
    "$ROOT/src/platforms/btspeak/deb/build_deb.sh" "0.0.0+pin" >/dev/null
PDEB="$(ls "$ROOT"/build/blazie-emu-btspeak_0.0.0+pin_*.deb)"
rm -rf "$T/proot" "$T/pctl"; dpkg-deb -x "$PDEB" "$T/proot"; dpkg-deb -e "$PDEB" "$T/pctl"
grep -q -x "URL=https://test.invalid/pinned/blazie-emu-0.7.0-linux-aarch64.tar.gz" \
    "$T/proot/usr/lib/blazie-emu-btspeak/fetch-firmware" && grep -q -x "SHA=$SHA" "$T/proot/usr/lib/blazie-emu-btspeak/fetch-firmware"
check "a pinned preview: its fetch-firmware carries the given download and SHA-256" $?
(unset BTE_FIRMWARE_URL BTE_FIRMWARE_SHA; BTE_ROOT="$T/proot" "$T/pctl/postinst" configure >/dev/null)
[ "$(cat "$T/proot/usr/lib/blazie-emu-btspeak/share/ssi263-speech/BL2ENG.BNS" 2>/dev/null)" = pinned ]
check "a pinned preview: installed, the firmware from its own pinned download" $?
BTE_PIN_URL="http://plain.invalid/x.tar.gz" BTE_PIN_SHA="$SHA" "$ROOT/src/platforms/btspeak/deb/build_deb.sh" \
    "0.0.0+bad" >/dev/null 2>&1 && bad_made=0 || bad_made=1
check "a pin that is not https:// is refused" $((1 - bad_made))
rm -f "$PDEB" "$ROOT"/build/blazie-emu-btspeak_0.0.0+bad_*.deb

# a release's .deb (its version, and that release's emulator archive in build/, as package_emu_linux.sh leaves it):
# pinned to the release's own archive by its SHA-256, so no one sets the pin by hand (0.9.0's was; the source's still
# named 0.7.0's); the release is the version up to a + or ~.  Installed, it fetches from that archive
mkdir -p "$T/rel-release/blazie-emu-x/share/ssi263-speech/tns" "$T/web/download/v9.9.1"
for f in BL2ENG.BNS bl2_2003_warm.state tns/TNSENG.TNS; do echo release > "$T/rel-release/blazie-emu-x/share/ssi263-speech/$f"; done
RA="$ROOT/build/blazie-emu-9.9.1-linux-aarch64.tar.gz"
tar -czf "$RA" -C "$T/rel-release" blazie-emu-x
cp "$RA" "$T/web/download/v9.9.1/"
RSHA="$(sha256sum "$RA" | cut -d' ' -f1)"
BTE_RELEASES="https://test.invalid/download" "$ROOT/src/platforms/btspeak/deb/build_deb.sh" "9.9.1+btspeak1" >/dev/null
RDEB="$(ls "$ROOT"/build/blazie-emu-btspeak_9.9.1+btspeak1_*.deb)"
rm -rf "$T/rroot" "$T/rctl"; dpkg-deb -x "$RDEB" "$T/rroot"; dpkg-deb -e "$RDEB" "$T/rctl"
grep -q -x "URL=https://test.invalid/download/v9.9.1/blazie-emu-9.9.1-linux-aarch64.tar.gz" \
    "$T/rroot/usr/lib/blazie-emu-btspeak/fetch-firmware" && grep -q -x "SHA=$RSHA" "$T/rroot/usr/lib/blazie-emu-btspeak/fetch-firmware"
check "a release's .deb: its fetch-firmware pins that release's own emulator archive, by its SHA-256" $?
(unset BTE_FIRMWARE_URL BTE_FIRMWARE_SHA; BTE_ROOT="$T/rroot" "$T/rctl/postinst" configure >/dev/null)
[ "$(cat "$T/rroot/usr/lib/blazie-emu-btspeak/share/ssi263-speech/BL2ENG.BNS" 2>/dev/null)" = release ] \
    && from_release=0 || from_release=1
check "a release's .deb: installed, the firmware from its own release" $from_release
rm -f "$RA" "$RDEB"
BTE_RELEASES="https://test.invalid/download" "$ROOT/src/platforms/btspeak/deb/build_deb.sh" "9.9.2" >/dev/null 2>&1 \
    && unpinned_made=0 || unpinned_made=1
check "a release's version without its emulator archive in build/ is refused" $((1 - unpinned_made))
rm -f "$ROOT"/build/blazie-emu-btspeak_9.9.2_*.deb
[ $fail = 0 ] && echo "btspeak deb: all passed" || { echo "btspeak deb: FAILED"; exit 1; }
