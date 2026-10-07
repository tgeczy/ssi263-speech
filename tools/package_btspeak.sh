#!/bin/sh
# A tester's package for the BT Speak and BT Braille, without any firmware: blazie_emu and blazie_files as built here,
# install-btspeak.sh, the READMEs and the licences.  The tester gives the installer the firmware from the project's own
# release download (blazie-emu-<version>-linux-aarch64.tar.gz), which carries it:
#
#   ./build_linux.sh && tools/package_btspeak.sh [version]
#   then, on the device:  ./install-btspeak.sh --menu ~/Downloads/blazie-emu-0.7.0-linux-aarch64.tar.gz
#
# Build it on Debian 12 or older (a BT Speak's system: glibc 2.36).  Output: build/blazie-emu-btspeak-<version>-linux-<arch>.tar.gz
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${1:-test}"
NAME="blazie-emu-btspeak-$VERSION-linux-$(uname -m)"
STAGE="$ROOT/build/package/$NAME"
[ -x "$ROOT/build/linux/blazie_emu" ] || { echo "build it first: ./build_linux.sh"; exit 1; }
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/licenses"
cp "$ROOT/build/linux/blazie_emu" "$ROOT/build/linux/blazie_files" "$STAGE/bin/"
cp "$ROOT/build/linux/blazie_emu_bt" "$ROOT/build/linux/blazie_bt" "$STAGE/bin/"     # the BT front end (#4)
cp "$ROOT/src/platforms/btspeak/install.sh" "$STAGE/install-btspeak.sh"
cp "$ROOT/src/platforms/btspeak/uninstall.sh" "$STAGE/uninstall-btspeak.sh"
cp "$ROOT/src/platforms/btspeak/README-btspeak.md" "$STAGE/README-btspeak.md"
cp "$ROOT/src/apps/blazie/README-linux.md" "$STAGE/README-blazie-emu.md"
cp "$ROOT/src/platforms/btspeak/README.md" "$STAGE/README-blazie-bt.md"
cp "$ROOT/LICENSE" "$STAGE/LICENSE"
cp "$ROOT/src/csrc/cpu/mame_z180/LICENSE-BSD-3-Clause.txt" "$STAGE/licenses/MAME-Z180-core-BSD-3-Clause.txt"
cp "$ROOT/third_party/casso/LICENSE" "$STAGE/licenses/Casso-MIT.txt"
chmod +x "$STAGE/install-btspeak.sh" "$STAGE/uninstall-btspeak.sh" "$STAGE/bin/"*
if find "$STAGE" -iname '*.BNS' -o -iname '*.TNS' -o -name '*.state' | grep -q .; then
    echo "firmware found in the package: not made"; exit 1
fi
(cd "$ROOT/build/package" && tar -czf "$ROOT/build/$NAME.tar.gz" "$NAME")
echo "packaged build/$NAME.tar.gz (no firmware)"
