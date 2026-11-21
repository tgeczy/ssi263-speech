#!/bin/sh
# Install the SSI-263 voices for speech-dispatcher (Orca, spd-say): the Braille Lite 2000 (English, Spanish), the
# Accent-PC, and the Accent-mini and Speak-Out when the package has them.  Run from the unpacked package:
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
# every other voice's firmware, each folder as the package has it (sd_voices.h names them): it named three once, and
# the Sounding Board, the Mockingboards, the SmarTalk and the bl2000 languages never appeared (#44).  Not the
# speech-dispatcher settings, which go to speech-dispatcher's own folder below.  (SSI263_INSTALL_BREAK=folders, the
# test's control: the three it named.)
if [ "${SSI263_INSTALL_BREAK:-}" = folders ]; then
    FOLDERS="aicom-accent-sa aicom-accent-mini gw-micro-speakout"
else
    FOLDERS="$(cd "$HERE/share/ssi263-speech" && for d in */; do [ -d "$d" ] && printf '%s ' "${d%/}"; done)"
fi
for d in $FOLDERS; do
    [ "$d" = speech-dispatcher ] && continue
    if [ -d "$HERE/share/ssi263-speech/$d" ]; then
        mkdir -p "$DATA/$d"
        cp -R "$HERE/share/ssi263-speech/$d"/. "$DATA/$d/"
    fi
done
if [ -d "$HERE/licenses" ]; then
    mkdir -p "$DATA/licenses"
    cp "$HERE"/licenses/* "$DATA/licenses/"
fi
# A program or the library, put in place: copied beside the old one and renamed over it.  A plain cp onto a program
# that is running fails ("Text file busy": speech-dispatcher has the module open whenever a screen reader is talking)
# and, under set -e, stopped the install half done; onto a loaded library it rewrites the file under the programs
# using it.  Renamed, what is running keeps the file it has, and the next start gets the new one.
# (SSI263_INSTALL_BREAK=copy, the test's control: the plain copy it was.)
put() {      # put FILE DESTINATION-FILE [MODE]
    if [ "${SSI263_INSTALL_BREAK:-}" = copy ]; then
        cp "$1" "$2"
    else
        cp "$1" "$2.new.$$"
        mv -f "$2.new.$$" "$2"
    fi
    if [ -n "${3:-}" ]; then chmod "$3" "$2"; fi
}
put "$HERE/lib/libssi263speech.so" "$PREFIX/lib/libssi263speech.so"
put "$HERE/bin/sd_ssi263" "$MODBIN/sd_ssi263" 755
if [ -f "$HERE/bin/blazie_emu" ]; then   # the Blazie emulator (README-blazie-emu.md), its firmware in $DATA
    mkdir -p "$PREFIX/bin"
    put "$HERE/bin/blazie_emu" "$PREFIX/bin/blazie_emu" 755
    if [ -d "$HERE/share/ssi263-speech/tns" ]; then
        mkdir -p "$DATA/tns"
        cp "$HERE"/share/ssi263-speech/tns/* "$DATA/tns/"
    fi
    echo "  emulator: $PREFIX/bin/blazie_emu"
fi
if [ -f "$HERE/bin/blazie_emu_bt" ] && [ -f "$HERE/bin/blazie_bt" ]; then
    mkdir -p "$PREFIX/bin"
    put "$HERE/bin/blazie_emu_bt" "$PREFIX/bin/blazie_emu_bt" 755
    put "$HERE/bin/blazie_bt" "$PREFIX/bin/blazie_bt" 755
    echo "  emulator (BT Speak / BT Braille): $PREFIX/bin/blazie_emu_bt"
fi
if [ -f "$HERE/bin/blazie_emu_gtk" ]; then   # the emulator in a GTK window, for Orca, and its desktop menu entry
    mkdir -p "$PREFIX/bin" "$PREFIX/share/applications"
    put "$HERE/bin/blazie_emu_gtk" "$PREFIX/bin/blazie_emu_gtk" 755
    sed "s|^Exec=.*|Exec=$PREFIX/bin/blazie_emu_gtk|" "$HERE/share/applications/ssi263-blazie-emu.desktop" \
        > "$PREFIX/share/applications/ssi263-blazie-emu.desktop"
    echo "  emulator (window): $PREFIX/bin/blazie_emu_gtk, in the desktop's menu as Blazie emulator"
fi
if [ -f "$HERE/bin/blazie_files" ]; then   # a saved unit's files from the command line (README-blazie-emu.md)
    mkdir -p "$PREFIX/bin"
    put "$HERE/bin/blazie_files" "$PREFIX/bin/blazie_files" 755
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
    # every other DefaultModule commented out, for uninstall.sh to give back; our own line left as it is, so a second
    # --default (an upgrade) neither comments it out nor adds another (#45: it did both, and uninstall.sh then gave
    # back ours as the default).  (SSI263_INSTALL_BREAK=default, the test's control: every one commented, ours added.)
    if [ "${SSI263_INSTALL_BREAK:-}" = default ]; then
        sed -i 's/^DefaultModule/# (before ssi263) DefaultModule/' "$CONF"
    else
        sed -i '/^DefaultModule ssi263$/!s/^DefaultModule/# (before ssi263) DefaultModule/' "$CONF"
    fi
    if grep -q '^DefaultModule ssi263$' "$CONF"; then
        echo "  the default synthesizer already"
    else
        # on a line of its own: a config whose last line has no newline (a hand-edited one) glued it to the commented
        # old default, which uninstall.sh then gave back as a DefaultModule naming no module (Brailab's audit, B25).
        # (SSI263_INSTALL_BREAK=newline, the test's control: appended as it was)
        [ "${SSI263_INSTALL_BREAK:-}" != newline ] && [ -n "$(tail -c1 "$CONF")" ] && echo >> "$CONF"
        echo "DefaultModule ssi263" >> "$CONF"
        echo "  made the default synthesizer"
    fi
fi
if [ -n "$SUDO_USER" ] && [ "$CONF" != "/etc/speech-dispatcher/speechd.conf" ]; then
    # the user's own: the config, the module's settings and the modules folder, which mkdir made as root when it was
    # not there (Brailab's audit, B29: it stayed root's, and the user could not change their own settings there).
    # (SSI263_INSTALL_BREAK=chown, the test's control: the two files alone, as it was)
    if [ "${SSI263_INSTALL_BREAK:-}" = chown ]; then
        chown "$SUDO_USER" "$CONF" "$MODCONF/ssi263.conf"
    else
        chown "$SUDO_USER" "$CONF" "$MODCONF/ssi263.conf" "$MODCONF"
    fi
fi

cat <<EOF

Done.  Restart speech-dispatcher so it sees the new voice (Orca reconnects by itself):
    killall speech-dispatcher
Try it:
    spd-say -o ssi263 "Hello from the Braille Lite"
    spd-say -o ssi263 -y "Accent-PC" "Hello from the Accent"
    spd-say -o ssi263 -L          (the voices)
In Orca: Preferences, Speech, Speech synthesizer: ssi263, then the voice.
Settings (sample rate; the Braille Lite's inflection, hiss, tone, number words, run ahead; the Accents' inflection;
the Speak-Out's tone) are explained in $MODCONF/ssi263.conf; your own copy of any line in ~/.config/ssi263-speech/sd_ssi263.conf wins
over it.
EOF
