#!/bin/sh
# Install the SSI-263 voices for speech-dispatcher (Orca, spd-say): the Braille Lite 2000 (English, Spanish), the
# Accent SA, and the Accent-mini and Speak-Out when the package has them.  Run from the unpacked package:
#
#   sudo ./install.sh              # add the voices; your default synthesizer stays as it is
#   sudo ./install.sh --default    # and make it the default
#   PREFIX=/opt/ssi263 sudo -E ./install.sh
#
# speech-dispatcher finds the module by itself: with no AddModule line in speechd.conf it loads every module in
# its modules folder (Garrett, issue #10: an AddModule line turns that off, leaving this the only synthesizer).  So
# speechd.conf is left alone, except: a config that already lists its modules with AddModule lines (autodiscovery
# off, as Raspberry Pi OS ships it) gets one for this voice too, or it would never load; an earlier install's line
# is removed; and --default sets DefaultModule.  Nothing else is removed or commented out.  ./uninstall.sh undoes it.
# SSI263_SPD_MODULES and SSI263_SPD_CONF point it at other folders (tools/speechd_install_test.sh).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${PREFIX:-/usr/local}"
DATA="$PREFIX/share/ssi263-speech"
MODBIN="${SSI263_SPD_MODULES:-}"
if [ -z "$MODBIN" ]; then             # Debian/Ubuntu, multiarch, Fedora/Arch
    for d in /usr/lib/speech-dispatcher-modules /usr/lib/*/speech-dispatcher-modules \
             /usr/libexec/speech-dispatcher-modules; do
        [ -d "$d" ] && { MODBIN="$d"; break; }
    done
    MODBIN="${MODBIN:-/usr/lib/speech-dispatcher-modules}"
fi
MAKE_DEFAULT=0
[ "$1" = "--default" ] && MAKE_DEFAULT=1

USER_HOME="$HOME"
[ -n "$SUDO_USER" ] && USER_HOME="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
if [ -n "$SSI263_SPD_CONF" ]; then
    CONF="$SSI263_SPD_CONF"
    MODCONF="$(dirname "$CONF")/modules"
elif [ -f "$USER_HOME/.config/speech-dispatcher/speechd.conf" ]; then
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

MARK='# --- the Braille Lite 2000 voice (ssi263-speech install.sh) ---'
# an earlier install's lines (0.7.0-0.7.6: an active AddModule, which turned autodiscovery off; #11's commented one)
if grep -q -e '^[[:space:]]*#\{0,1\}[[:space:]]*AddModule[[:space:]]*"ssi263"' -e "^$MARK\$" "$CONF"; then
    sed -i -e '/^$/{N;s/^\n\(# --- the Braille Lite 2000 voice (ssi263-speech install.sh) ---\)$/\1/}' \
           -e "\\|^$MARK\$|d" -e '/^[[:space:]]*#\{0,1\}[[:space:]]*AddModule[[:space:]]*"ssi263"/d' "$CONF"
    echo "  removed an earlier install's module line from $CONF"
fi
if [ "${SSI263_INSTALL_BREAK:-}" = 1 ] || grep -q '^[[:space:]]*AddModule[[:space:]]' "$CONF"; then
    # the config lists its modules itself (autodiscovery is off): this one must be listed too, or it never loads
    # (SSI263_INSTALL_BREAK=1, the test's control: 0.7.6's line, always)
    [ -n "$(tail -c1 "$CONF")" ] && echo >> "$CONF"
    printf '%s\nAddModule "ssi263" "sd_ssi263" "ssi263.conf"\n' "$MARK" >> "$CONF"
    echo "  $CONF lists its modules itself: added this one to the list"
else
    echo "  speech-dispatcher finds the module by itself ($CONF unchanged)"
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
