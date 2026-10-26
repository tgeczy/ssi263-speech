#!/bin/sh
# A .deb of the Blazie emulator for the BT Speak and BT Braille: installed with apt, it fetches the firmware from the
# project's own release and adds the Blazie menu to the User Menu; removing it takes both away again.  No firmware is
# inside the .deb (the project's releases are the only place it is published from): fetch-firmware takes it from the
# pinned release, else from the latest one by its SHA256SUMS.txt.
#
#   ./build_linux.sh && src/platforms/btspeak/deb/build_deb.sh [version]
#   Output: build/blazie-emu-btspeak_<version>_arm64.deb
#
# A version is a release's: the package pins that release's own emulator archive, build/blazie-emu-<release>-linux-
# aarch64.tar.gz (tools/package_emu_linux.sh's, where tools/release_from_ci.sh leaves it), by its SHA-256 there, so a
# release's .deb never needs its pin set by hand (0.9.0's was, and the source's still named 0.7.0's).  <release> is the
# version up to a + or ~ (0.9.0+btspeak2: 0.9.0).  Without that archive the build is refused.  No version: a
# development build (0.0.0+dev), with fetch-firmware's own pin.
#
# A preview hosted elsewhere (Tomi's eurpod.com/synths for 0.8's): BTE_PIN_URL and BTE_PIN_SHA, both or neither, are
# the pinned download and its SHA-256 written into this package's fetch-firmware in place of the release's; the
# latest-release fallback stays the project's.
#   BTE_PIN_URL=https://eurpod.com/synths/blazie-emu-0.8.0-preview1-linux-aarch64.tar.gz BTE_PIN_SHA=<sha256> \
#       src/platforms/btspeak/deb/build_deb.sh 0.8.0~preview1
set -e
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
HERE="$ROOT/src/platforms/btspeak/deb"
VERSION="${1:-0.0.0+dev}"
ARCH="$(dpkg --print-architecture)"
STAGE="$ROOT/build/deb/blazie-emu-btspeak"
[ -x "$ROOT/build/linux/blazie_emu" ] || { echo "build it first: ./build_linux.sh"; exit 1; }
rm -rf "$STAGE"
LIB="$STAGE/usr/lib/blazie-emu-btspeak"
DOC="$STAGE/usr/share/doc/blazie-emu-btspeak"
mkdir -p "$LIB/bin" "$STAGE/usr/bin" "$DOC" "$STAGE/DEBIAN"
install -m 755 "$ROOT/build/linux/blazie_emu" "$ROOT/build/linux/blazie_files" "$LIB/bin/"
# the BT front end (#4: the device's own dialogs), which blazie_emu hands over to; without it, blazie_emu's own route
[ -x "$ROOT/build/linux/blazie_emu_bt" ] && [ -x "$ROOT/build/linux/blazie_bt" ] \
    || { echo "no build/linux/blazie_emu_bt and blazie_bt: build them first (./build_linux.sh)"; exit 1; }
install -m 755 "$ROOT/build/linux/blazie_emu_bt" "$ROOT/build/linux/blazie_bt" "$LIB/bin/"
install -m 755 "$HERE/fetch-firmware" "$ROOT/src/platforms/btspeak/user-menu-entry" "$LIB/"
# the Blazie menu (the User Menu's line opens it), listing what the launcher lists; More languages is itself again
sed 's|^PROGRAM = .*$|PROGRAM = "/usr/bin/blazie-emu-btspeak"|' "$ROOT/src/platforms/btspeak/blazie.menu" > "$LIB/blazie.menu"
chmod 755 "$LIB/blazie.menu"
grep -q -x 'PROGRAM = "/usr/bin/blazie-emu-btspeak"' "$LIB/blazie.menu" || { echo "blazie.menu: PROGRAM not written"; exit 1; }
ln -s blazie.menu "$LIB/blazie-languages.menu"
# a release's version: its own emulator archive pinned (BTE_RELEASES: the tests' fake releases; BTSPEAK_DEB_BREAK=pin,
# tools/btspeak_deb_test.sh's control, keeps fetch-firmware's own pin as before)
if [ -n "${1:-}" ] && [ -z "${BTE_PIN_URL:-}${BTE_PIN_SHA:-}" ] && [ "${BTSPEAK_DEB_BREAK:-}" != pin ]; then
    REL="${VERSION%%[+~]*}"
    ARCHIVE="$ROOT/build/blazie-emu-$REL-linux-aarch64.tar.gz"
    [ -f "$ARCHIVE" ] || { echo "no $ARCHIVE: a release's .deb pins its own emulator archive" \
        "(tools/package_emu_linux.sh makes it; no version for a development build)"; exit 1; }
    BTE_PIN_URL="${BTE_RELEASES:-https://github.com/tgeczy/ssi263-speech/releases/download}/v$REL/blazie-emu-$REL-linux-aarch64.tar.gz"
    BTE_PIN_SHA="$(sha256sum "$ARCHIVE" | cut -d' ' -f1)"
fi
if [ -n "${BTE_PIN_URL:-}${BTE_PIN_SHA:-}" ]; then
    case "${BTE_PIN_URL:-}" in https://*) ;; *) echo "BTE_PIN_URL must be an https:// URL"; exit 1 ;; esac
    echo "${BTE_PIN_SHA:-}" | grep -q -x -E '[0-9a-f]{64}' || { echo "BTE_PIN_SHA must be a SHA-256 (64 hex digits)"; exit 1; }
    sed -i -e "s|^URL=https://.*\$|URL=$BTE_PIN_URL|" -e "s|^SHA=[0-9a-f]*\$|SHA=$BTE_PIN_SHA|" "$LIB/fetch-firmware"
    grep -q -x "URL=$BTE_PIN_URL" "$LIB/fetch-firmware" && grep -q -x "SHA=$BTE_PIN_SHA" "$LIB/fetch-firmware" \
        || { echo "the pin was not written into fetch-firmware"; exit 1; }
    echo "pinned download: $BTE_PIN_URL ($BTE_PIN_SHA)"
fi
install -m 755 "$HERE/blazie-emu-btspeak" "$STAGE/usr/bin/"
ln -s ../lib/blazie-emu-btspeak/bin/blazie_files "$STAGE/usr/bin/blazie_files"
install -m 644 "$ROOT/src/platforms/btspeak/README-btspeak.md" "$DOC/README-btspeak.md"
install -m 644 "$ROOT/src/apps/blazie/README-linux.md" "$DOC/README-blazie-emu.md"
install -m 644 "$ROOT/src/platforms/btspeak/README.md" "$DOC/README-blazie-bt.md"
install -m 644 "$ROOT/LICENSE" "$DOC/LICENSE"
install -m 644 "$ROOT/src/csrc/cpu/mame_z180/LICENSE-BSD-3-Clause.txt" "$DOC/MAME-Z180-core-BSD-3-Clause.txt"
install -m 644 "$ROOT/third_party/casso/LICENSE" "$DOC/Casso-MIT.txt"
install -m 755 "$HERE/postinst" "$HERE/prerm" "$HERE/postrm" "$STAGE/DEBIAN/"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: blazie-emu-btspeak
Version: $VERSION
Architecture: $ARCH
Maintainer: Stephen Clower <steve@steve-audio.net>
Depends: libc6 (>= 2.34), libasound2, python3 (>= 3.11), curl, ca-certificates
Recommends: libbrlapi0.8
Section: misc
Priority: optional
Installed-Size: $(du -sk "$STAGE/usr" | cut -f1)
Description: Blazie Braille Lite 2000, Braille 'n Speak 2000 and Type 'n Speak emulator for the BT Speak
 The original Blazie firmware on an emulated SSI-263 voice, played with the BT Speak's own
 braille keys, with the device's own dialogs for its menu; on a BT Braille the Braille Lite's
 display appears on the BT Braille's display and its L2/R2 and L3/R3 keys are the advance bars.  Installing fetches the firmware from
 the ssi263-speech project's release and adds a Blazie menu of its units to the User Menu
 (Ctrl+C there copies a unit to the User Menu of its own); removing takes
 both away.  M-chord with dot 7 opens the emulator's menu; Z-chord with dot 7 saves and
 quits.
EOF
if find "$STAGE" -iname '*.BNS' -o -iname '*.TNS' -o -name '*.state' | grep -q .; then
    echo "firmware found in the package: not made"; exit 1
fi
mkdir -p "$ROOT/build"
dpkg-deb --root-owner-group --build "$STAGE" "$ROOT/build/blazie-emu-btspeak_${VERSION}_${ARCH}.deb"
