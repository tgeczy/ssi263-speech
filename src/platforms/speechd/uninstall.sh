#!/bin/sh
# Remove what install.sh added: the module, its settings, the firmware folder, the library and the Blazie emulator,
# and the voice's lines in speechd.conf (a DefaultModule install.sh commented out is restored).  The emulator's own
# settings and units' memory (~/.config/ssi263-speech/blazie-emu) are the user's and stay.
#
#   sudo ./uninstall.sh            (PREFIX as at install time)
set -e
PREFIX="${PREFIX:-/usr/local}"
USER_HOME="$HOME"
[ -n "$SUDO_USER" ] && USER_HOME="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
for CONF in ${SSI263_SPD_CONF:-"$USER_HOME/.config/speech-dispatcher/speechd.conf" /etc/speech-dispatcher/speechd.conf}; do
    [ -f "$CONF" ] || continue
    if grep -q 'ssi263' "$CONF"; then
        # the blank line an earlier installer put before the comment line, then the comment and module lines
        # (active, as install.sh adds to a config that lists its modules, or commented out, as #11's did)
        sed -i -e '/^$/{N;s/^\n\(# --- the Braille Lite 2000 voice (ssi263-speech install.sh) ---\)$/\1/}' "$CONF"
        sed -i -e '/^# --- the Braille Lite 2000 voice (ssi263-speech install.sh) ---$/d' \
               -e '/^[[:space:]]*#\{0,1\}[[:space:]]*AddModule[[:space:]]*"ssi263"/d' -e '/^DefaultModule ssi263$/d' \
               -e 's/^# (before ssi263) DefaultModule/DefaultModule/' "$CONF"
        echo "cleaned $CONF"
    fi
    rm -f "$(dirname "$CONF")/modules/ssi263.conf"
done
for d in ${SSI263_SPD_MODULES:-/usr/lib/speech-dispatcher-modules /usr/lib/*/speech-dispatcher-modules \
         /usr/libexec/speech-dispatcher-modules}; do
    rm -f "$d/sd_ssi263"
done
rm -f "$PREFIX/lib/libssi263speech.so" "$PREFIX/bin/blazie_emu" \
    "$PREFIX/bin/blazie_files" "$PREFIX/bin/blazie_emu_gtk" "$PREFIX/share/applications/ssi263-blazie-emu.desktop" \
    "$PREFIX/bin/blazie_emu_bt" "$PREFIX/bin/blazie_bt"
rm -rf "$PREFIX/share/ssi263-speech"
echo "Removed.  Restart speech-dispatcher: killall speech-dispatcher"
