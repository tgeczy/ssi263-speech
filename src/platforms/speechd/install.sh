#!/bin/sh
# Install the SSI-263 voices for speech-dispatcher (Orca, spd-say): the Braille Lite 2000 (English, Spanish), the
# Accent SA, and the Accent-mini and Speak-Out when the package has them.  Run from the unpacked package:
#
#   sudo ./install.sh              # add the voices; your default synthesizer stays as it is
#   sudo ./install.sh --default    # and make it the default
#   PREFIX=/opt/ssi263 sudo -E ./install.sh
#
# Adds one module line to speech-dispatcher's config (the user's own speechd.conf if there is one, else the
# system's) and never removes or comments out anything else.  ./uninstall.sh undoes it.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${PREFIX:-/usr/local}"
DATA="$PREFIX/share/ssi263-speech"
MODBIN="/usr/lib/speech-dispatcher-modules"
MAKE_DEFAULT=0
[ "$1" = "--default" ] && MAKE_DEFAULT=1

USER_HOME="$HOME"
[ -n "$SUDO_USER" ] && USER_HOME="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
if [ -f "$USER_HOME/.config/speech-dispatcher/speechd.conf" ]; then
    CONF="$USER_HOME/.config/speech-dispatcher/speechd.conf"
    MODCONF="$USER_HOME/.config/speech-dispatcher/modules"
elif [ -f /etc/speech-dispatcher/speechd.conf ]; then
    CONF=/etc/speech-dispatcher/speechd.conf
    MODCONF=/etc/speech-dispatcher/modules
else
    echo "speech-dispatcher's speechd.conf was not found (is speech-dispatcher installed?)"
    exit 1
fi
[ -d "$MODBIN" ] || { echo "$MODBIN not found: where does your speech-dispatcher keep its modules?"; exit 1; }

echo "Installing the SSI-263 voices"
mkdir -p "$DATA" "$PREFIX/lib" "$MODCONF"
cp "$HERE"/share/ssi263-speech/*.BNS "$HERE"/share/ssi263-speech/*.state "$DATA/"
for d in aicom-accent-sa aicom-accent-mini gw-micro-speakout; do   # the other voices' firmware (sd_voices.h)
    if [ -d "$HERE/share/ssi263-speech/$d" ]; then
        mkdir -p "$DATA/$d"
        cp "$HERE/share/ssi263-speech/$d"/* "$DATA/$d/"
    fi
done
if [ -d "$HERE/licenses" ]; then
    mkdir -p "$DATA/licenses"
    cp "$HERE"/licenses/* "$DATA/licenses/"
fi
cp "$HERE/lib/libssi263speech.so" "$PREFIX/lib/"
cp "$HERE/bin/sd_ssi263" "$MODBIN/sd_ssi263"
chmod 755 "$MODBIN/sd_ssi263"
if [ -f "$HERE/bin/blazie_emu" ]; then   # the Blazie emulator (README-blazie-emu.md), its firmware in $DATA
    mkdir -p "$PREFIX/bin"
    cp "$HERE/bin/blazie_emu" "$PREFIX/bin/blazie_emu"
    chmod 755 "$PREFIX/bin/blazie_emu"
    if [ -d "$HERE/share/ssi263-speech/tns" ]; then
        mkdir -p "$DATA/tns"
        cp "$HERE"/share/ssi263-speech/tns/* "$DATA/tns/"
    fi
    echo "  emulator: $PREFIX/bin/blazie_emu"
fi
if [ -f "$HERE/bin/blazie_emu_bt" ] && [ -f "$HERE/bin/blazie_bt" ]; then
    mkdir -p "$PREFIX/bin"
    cp "$HERE/bin/blazie_emu_bt" "$HERE/bin/blazie_bt" "$PREFIX/bin/"
    chmod 755 "$PREFIX/bin/blazie_emu_bt" "$PREFIX/bin/blazie_bt"
    echo "  emulator (BT Speak / BT Braille): $PREFIX/bin/blazie_emu_bt"
fi
if [ -f "$HERE/bin/blazie_emu_gtk" ]; then   # the emulator in a GTK window, for Orca, and its desktop menu entry
    mkdir -p "$PREFIX/bin" "$PREFIX/share/applications"
    cp "$HERE/bin/blazie_emu_gtk" "$PREFIX/bin/blazie_emu_gtk"
    chmod 755 "$PREFIX/bin/blazie_emu_gtk"
    sed "s|^Exec=.*|Exec=$PREFIX/bin/blazie_emu_gtk|" "$HERE/share/applications/ssi263-blazie-emu.desktop" \
        > "$PREFIX/share/applications/ssi263-blazie-emu.desktop"
    echo "  emulator (window): $PREFIX/bin/blazie_emu_gtk, in the desktop's menu as Blazie emulator"
fi
if [ -f "$HERE/bin/blazie_files" ]; then   # a saved unit's files from the command line (README-blazie-emu.md)
    mkdir -p "$PREFIX/bin"
    cp "$HERE/bin/blazie_files" "$PREFIX/bin/blazie_files"
    chmod 755 "$PREFIX/bin/blazie_files"
    echo "  files tool: $PREFIX/bin/blazie_files"
fi
{
    cat "$HERE/share/ssi263-speech/speech-dispatcher/ssi263.conf"
    echo "SSI263DataDir \"$DATA\""
} > "$MODCONF/ssi263.conf"
echo "  module:   $MODBIN/sd_ssi263"
echo "  settings: $MODCONF/ssi263.conf"
echo "  firmware: $DATA"

if grep -q '^AddModule "ssi263"' "$CONF"; then
    echo "  $CONF already lists the voice"
else
    # exactly these two lines, which uninstall.sh removes again (a missing final newline is added first)
    #Comment out the AddModule line to avoid breaking speech-dispatcher's automatic discovery mechanism.
    [ -n "$(tail -c1 "$CONF")" ] && echo >> "$CONF"
    printf '# --- the Braille Lite 2000 voice (ssi263-speech install.sh) ---\n#AddModule "ssi263" "sd_ssi263" "ssi263.conf"\n' >> "$CONF"
    echo "  added to $CONF"
fi
if [ $MAKE_DEFAULT -eq 1 ]; then
    if grep -q '^DefaultModule' "$CONF"; then
        sed -i 's/^DefaultModule/# (before ssi263) DefaultModule/' "$CONF"
    fi
    echo "DefaultModule ssi263" >> "$CONF"
    echo "  made the default synthesizer"
fi
if [ -n "$SUDO_USER" ] && [ "$CONF" != "/etc/speech-dispatcher/speechd.conf" ]; then
    chown "$SUDO_USER" "$CONF" "$MODCONF/ssi263.conf"
fi

cat <<EOF

Done.  Restart speech-dispatcher so it sees the new voice (Orca reconnects by itself):
    killall speech-dispatcher
Try it:
    spd-say -o ssi263 "Hello from the Braille Lite"
    spd-say -o ssi263 -y "Accent SA" "Hello from the Accent"
    spd-say -o ssi263 -L          (the voices)
In Orca: Preferences, Speech, Speech synthesizer: ssi263, then the voice.
Settings (sample rate; the Braille Lite's inflection, hiss, tone, number words, run ahead; the Accents' inflection;
the Speak-Out's tone) are explained in $MODCONF/ssi263.conf; your own copy of any line in ~/.config/ssi263-speech/sd_ssi263.conf wins
over it.
EOF
