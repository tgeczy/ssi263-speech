#!/bin/sh
# Remove what install.sh put in PREFIX (~/.local by default) and its lines in ~/BTSpeak/user.menu.  The units' memory
# and settings (~/.config/ssi263-speech/blazie-emu) are kept: delete that folder too to forget them.
set -e
PREFIX="${PREFIX:-$HOME/.local}"
rm -f "$PREFIX/bin/blazie_emu" "$PREFIX/bin/blazie_files" "$PREFIX/bin/blazie_emu_bt" "$PREFIX/bin/blazie_bt"
rm -rf "$PREFIX/share/ssi263-speech"
UM="$HOME/BTSpeak/user.menu"
if [ -f "$UM" ] && grep -q "run $PREFIX/bin/blazie_emu --unit " "$UM"; then
    grep -v "run $PREFIX/bin/blazie_emu --unit " "$UM" > "$UM.new" && mv "$UM.new" "$UM"
    echo "user menu: the emulator's lines removed"
fi
echo "removed the Blazie emulator from $PREFIX"
