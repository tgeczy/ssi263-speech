#!/bin/sh
# Install the Blazie emulator (blazie_emu) on a BT Speak or BT Braille -- from the unpacked Linux release package
# (where it is install-btspeak.sh, beside bin/ and share/), or from this source tree after ./build_linux.sh: the
# program and blazie_files into PREFIX/bin, the firmware into PREFIX/share/ssi263-speech (where blazie_emu looks
# beside itself), the Blazie menu (blazie.menu: the units the firmware can start, made each time it opens) into
# PREFIX/lib/blazie-emu, and -- with --menu -- its line in the BT Speak's User Menu.  README-btspeak.md says the rest.
#
#   ./install-btspeak.sh [--menu] [FIRMWARE_DIR]                  (the package)
#   src/platforms/btspeak/install.sh [--menu] [FIRMWARE_DIR]      (the source tree)
#
#   PREFIX        where to install: ~/.local by default, which needs no root (the menu's items name the program by
#                 its full path, so it need not be on the PATH); PREFIX=/usr/local with sudo for every user
#   FIRMWARE_DIR  the Blazie firmware, laid out as the repository's firmware/blazie (BL2ENG.BNS and its state,
#                 spanish/, tns/, bns2000/) or all in one folder -- or a release's blazie-emu-*.tar.gz, as downloaded
#                 (its share/ssi263-speech is used); default: the package's share/ssi263-speech, or the source tree's
#                 firmware/blazie
#   --menu        ~/BTSpeak/user.menu given "Blazie Emulator", opening the Blazie menu: the catalog's units, the
#                 language releases under More languages, Ctrl+C on any of them copying it to the User Menu; the
#                 three lines a unit each that this installer wrote before go (user-menu-entry)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -x "$HERE/bin/blazie_emu" ]; then         # the unpacked package
    BIN="$HERE/bin"; DEF_FW="$HERE/share/ssi263-speech"; DOCS="$HERE"
else                                           # the source tree
    ROOT="$(cd "$HERE/../../.." && pwd)"
    BIN="$ROOT/build/linux"; DEF_FW="$ROOT/firmware/blazie"; DOCS=""
fi
PREFIX="${PREFIX:-$HOME/.local}"
MENU=0
FW=""
for a in "$@"; do
    case "$a" in
        --menu) MENU=1 ;;
        -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
        *) FW="$a" ;;
    esac
done
FW="${FW:-$DEF_FW}"
case "$FW" in
    *.tar.gz|*.tgz)                            # a release download: its firmware, unpacked for the copy below
        TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
        tar -xzf "$FW" -C "$TMP" --wildcards '*/share/ssi263-speech/*' \
            || { echo "no firmware in $FW"; exit 1; }
        FW="$(dirname "$(find "$TMP" -name BL2ENG.BNS | head -n 1)")" ;;
esac

[ -x "$BIN/blazie_emu" ] || { echo "no $BIN/blazie_emu: build it first (./build_linux.sh)"; exit 1; }
[ -f "$FW/BL2ENG.BNS" ] || { echo "no Braille Lite firmware in $FW (BL2ENG.BNS: firmware/blazie/README.txt)"; exit 1; }
if [ ! -e /run/BTSpeak/keyboard-socket ] && [ ! -d /BTSpeak ]; then
    echo "note: this is not a BT Speak or BT Braille (no keyboard server); installing all the same"
fi

mkdir -p "$PREFIX/bin" "$PREFIX/share/ssi263-speech"
# A program put in place: copied beside the old one and renamed over it.  A plain cp onto a program that is running
# (the emulator left open while a newer one is installed) fails ("Text file busy") and, under set -e, stopped the
# install there; renamed, the running one keeps the file it has and the next start gets the new one.
# (BTSPEAK_INSTALL_BREAK=copy, tools/btspeak_deb_test.sh's control: the plain copy it was.)
put() {      # put PROGRAM: into PREFIX/bin under its own name
    if [ "${BTSPEAK_INSTALL_BREAK:-}" = copy ]; then
        cp "$BIN/$1" "$PREFIX/bin/$1"
    else
        cp "$BIN/$1" "$PREFIX/bin/$1.new.$$"
        mv -f "$PREFIX/bin/$1.new.$$" "$PREFIX/bin/$1"
    fi
    chmod 755 "$PREFIX/bin/$1"
}
put blazie_emu
if [ -x "$BIN/blazie_files" ]; then put blazie_files; fi
# the BT front end (#4: the device's own dialogs), which blazie_emu hands over to when it is beside it
if [ -x "$BIN/blazie_emu_bt" ] && [ -x "$BIN/blazie_bt" ]; then
    put blazie_emu_bt
    put blazie_bt
fi
# the firmware, as it is laid out (blazie_emu reads either layout); README.txt is the repository's own notes
(cd "$FW" && find . -type f \( -iname '*.BNS' -o -iname '*.TNS' -o -name '*.state' \)) | while read -r f; do
    mkdir -p "$PREFIX/share/ssi263-speech/$(dirname "$f")"
    cp -f "$FW/$f" "$PREFIX/share/ssi263-speech/$f"     # -f: a read-only copy from before replaced
done
if [ -n "$DOCS" ]; then
    cp "$DOCS/README-blazie-emu.md" "$DOCS/README-btspeak.md" "$PREFIX/share/ssi263-speech/"
else
    cp "$ROOT/src/apps/blazie/README-linux.md" "$PREFIX/share/ssi263-speech/README-blazie-emu.md"
    cp "$ROOT/src/platforms/btspeak/README-btspeak.md" "$PREFIX/share/ssi263-speech/README-btspeak.md"
fi
# the Blazie menu, naming this program; More languages is the same program by its other name.  Not for a PREFIX a
# menu line cannot carry (a menu's actions split at a space) or the menu's own text could not hold
case "$PREFIX" in
    /*) case "$PREFIX" in *[!A-Za-z0-9/._+-]*) SAFE=0 ;; *) SAFE=1 ;; esac ;;
    *) SAFE=0 ;;
esac
if [ "$SAFE" = 1 ]; then
    mkdir -p "$PREFIX/lib/blazie-emu"
    sed "s|^PROGRAM = .*\$|PROGRAM = \"$PREFIX/bin/blazie_emu\"|" "$HERE/blazie.menu" > "$PREFIX/lib/blazie-emu/blazie.menu"
    chmod 755 "$PREFIX/lib/blazie-emu/blazie.menu"
    ln -sf blazie.menu "$PREFIX/lib/blazie-emu/blazie-languages.menu"
fi
echo "installed $PREFIX/bin/blazie_emu, its firmware in $PREFIX/share/ssi263-speech"

if [ "$MENU" = 1 ] && [ "$SAFE" = 0 ]; then
    echo "no Blazie menu: PREFIX ($PREFIX) must be a full path of letters, digits and / . _ + - for a menu to name it"
elif [ "$MENU" = 1 ]; then
    mkdir -p "$HOME/BTSpeak"
    "$HERE/user-menu-entry" "$HOME/BTSpeak/user.menu" "$PREFIX/lib/blazie-emu/blazie.menu" \
        "run $PREFIX/bin/blazie_emu --unit" 2>&1
fi
