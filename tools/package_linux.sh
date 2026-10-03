#!/bin/sh
# Package the Linux release for this machine's architecture, as the NVDA add-ons are packaged: the speech-dispatcher
# module with every voice built into it, the library, each voice's firmware, the installer, and the licences: the
# project's MIT, Casso's MIT (third_party/casso: the chip model draws on it), MAME's BSD-3-Clause notices for the
# cores the voices run on (src/csrc/cpu/mame_*), and the firmware's notices.  No GPL code is inside
# (tools/check_no_gpl.py checks the package).
#
#   ./build_linux.sh && tools/package_linux.sh <firmware folder> [version]
#
# The firmware:
#   the Braille Lite's (shared with permission, never in the repository): from the firmware folder, BL2ENG.BNS +
#     bl2_2003_warm.state, and BL2SPA.BNS + bl2spa_fresh.state when the Spanish unit is to ship (the NVDA add-on's
#     engine folder has them);
#   the Accents' (Aicom's, in the repository with firmware/AICOM.txt): firmware/aicom-accent-sa, and
#     firmware/aicom-accent-mini when the module has the Accent-mini;
#   the Speak-Out's (GW Micro's, never in the repository), when the module has the Speak-Out: SPEAKOUT.HEX from the
#     firmware folder, its gw-micro-speakout/ or ../speakout-firmware/, or firmware/gw-micro-speakout/.
# They go into share/ssi263-speech as the repository's firmware/ folders are laid out (sd_voices.h).
# Output: build/ssi263-speech-<version>-linux-<arch>.tar.gz
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FW="$(cd "$1" && pwd)"
VERSION="${2:-0.7.6}"
ARCH="$(uname -m)"
NAME="ssi263-speech-$VERSION-linux-$ARCH"
STAGE="$ROOT/build/package/$NAME"

for f in "$ROOT/build/linux/sd_ssi263" "$ROOT/build/linux/libssi263speech.so" "$FW/BL2ENG.BNS" "$FW/bl2_2003_warm.state"; do
    [ -f "$f" ] || { echo "missing: $f"; exit 1; }
done
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/lib" "$STAGE/share/ssi263-speech/speech-dispatcher" "$STAGE/licenses"
cp "$ROOT/build/linux/sd_ssi263" "$STAGE/bin/"
cp "$ROOT/build/linux/libssi263speech.so" "$STAGE/lib/"
cp "$FW/BL2ENG.BNS" "$FW/bl2_2003_warm.state" "$STAGE/share/ssi263-speech/"
for dir in "$FW" "$FW/spanish"; do
    if [ -f "$dir/BL2SPA.BNS" ] && [ -f "$dir/bl2spa_fresh.state" ]; then
        cp "$dir/BL2SPA.BNS" "$dir/bl2spa_fresh.state" "$STAGE/share/ssi263-speech/"
        break
    fi
done
# The Braille 'n Speak 2000's firmware ships in the emulator's own downloads only, never in this package (Tomi);
# the emulators offer it when the person supplies it.
# the other voices' firmware, for the voices the module has (sd_ssi263 --voices), and their notices
VOICES="$("$ROOT/build/linux/sd_ssi263" --voices | cut -f1)"
has() { echo "$VOICES" | grep -qx "$1"; }
cp "$ROOT/src/csrc/cpu/mame_i8085/LICENSE-BSD-3-Clause.txt" "$STAGE/licenses/MAME-8085-core-BSD-3-Clause.txt"
cp "$ROOT/firmware/AICOM.txt" "$STAGE/licenses/Aicom-notice.txt"
for v in accent-sa accent-mini; do
    has "$v" || continue
    mkdir -p "$STAGE/share/ssi263-speech/aicom-$v"
    cp "$ROOT/firmware/aicom-$v"/* "$STAGE/share/ssi263-speech/aicom-$v/"
done
has accent-mini && cp "$ROOT/src/csrc/cpu/mame_i86/LICENSE-BSD-3-Clause.txt" \
    "$STAGE/licenses/MAME-8086-core-BSD-3-Clause.txt"
if has speakout; then
    HEX=""
    for src in "$FW/SPEAKOUT.HEX" "$FW/gw-micro-speakout/SPEAKOUT.HEX" "$FW/../speakout-firmware/SPEAKOUT.HEX" \
               "$ROOT/firmware/gw-micro-speakout/SPEAKOUT.HEX"; do
        [ -f "$src" ] && { HEX="$src"; break; }
    done
    if [ -n "$HEX" ]; then
        mkdir -p "$STAGE/share/ssi263-speech/gw-micro-speakout"
        cp "$HEX" "$STAGE/share/ssi263-speech/gw-micro-speakout/SPEAKOUT.HEX"
        cp "$ROOT/src/csrc/cpu/mame_nec/LICENSE-BSD-3-Clause.txt" "$STAGE/licenses/MAME-V40-core-BSD-3-Clause.txt"
        cat > "$STAGE/licenses/Speak-Out-firmware-notice.txt" <<'EOF'
The Speak-Out's firmware -- notice

This package carries the Speak-Out's own firmware (share/ssi263-speech/gw-micro-speakout/SPEAKOUT.HEX; hardware
Daniel Weirich, software Douglas Geoffray, GW Micro). It is not ours; it is here so the box can speak again, and it
will be removed if its rights holders ask. It is not covered by this package's MIT license.
EOF
    else
        echo "note: the module has the Speak-Out, but no SPEAKOUT.HEX was found, so the voice is not packaged"
    fi
fi
# the Blazie emulator (src/apps/blazie/README-linux.md): bin/blazie_emu finds the firmware in ../share/ssi263-speech,
# the Type 'n Speak's in its tns folder (from the firmware folder's tns/, or beside the Braille Lite's)
if [ -f "$ROOT/build/linux/blazie_emu" ]; then
    cp "$ROOT/build/linux/blazie_emu" "$STAGE/bin/"
    chmod +x "$STAGE/bin/blazie_emu"
    cp "$ROOT/src/apps/blazie/README-linux.md" "$STAGE/README-blazie-emu.md"
    for t in TNSENG.TNS TNSSPA.TNS; do
        for src in "$FW/tns/$t" "$FW/$t"; do
            if [ -f "$src" ]; then
                mkdir -p "$STAGE/share/ssi263-speech/tns"
                cp "$src" "$STAGE/share/ssi263-speech/tns/"
                break
            fi
        done
    done
else
    echo "note: build/linux/blazie_emu not built, so not packaged"
fi
# The BT frontend is an executable Python zipapp plus its native worker; BTSpeak itself is supplied by the device.
if [ -f "$ROOT/build/linux/blazie_emu_bt" ] && [ -f "$ROOT/build/linux/blazie_bt" ]; then
    cp "$ROOT/build/linux/blazie_emu_bt" "$ROOT/build/linux/blazie_bt" "$STAGE/bin/"
    chmod +x "$STAGE/bin/blazie_emu_bt" "$STAGE/bin/blazie_bt"
    cp "$ROOT/src/platforms/btspeak/README.md" "$STAGE/README-blazie-bt.md"
fi
# the same emulator in a GTK window, for Orca (README-blazie-emu.md, "The desktop app"), when it was built (GTK 3's
# headers there), with a menu entry for the desktop; GTK is the system's own library, not packaged
if [ -f "$ROOT/build/linux/blazie_emu_gtk" ]; then
    cp "$ROOT/build/linux/blazie_emu_gtk" "$STAGE/bin/"
    chmod +x "$STAGE/bin/blazie_emu_gtk"
    mkdir -p "$STAGE/share/applications"
    cat > "$STAGE/share/applications/ssi263-blazie-emu.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Blazie emulator
GenericName=Braille Lite 2000 and Type 'n Speak
Comment=Blazie's Braille Lite 2000 or Type 'n Speak running its own firmware, with its SSI-263 voice
Exec=blazie_emu_gtk
Terminal=false
Categories=Utility;Accessibility;
Keywords=braille;notetaker;speech;Blazie;
EOF
else
    echo "note: build/linux/blazie_emu_gtk not built (no GTK 3 headers), so not packaged"
fi
# blazie_files: a saved unit's files from the command line (README-blazie-emu.md, "Files in and out"); no sound needed
if [ -f "$ROOT/build/linux/blazie_files" ]; then
    cp "$ROOT/build/linux/blazie_files" "$STAGE/bin/"
    chmod +x "$STAGE/bin/blazie_files"
fi
cat > "$STAGE/share/ssi263-speech/speech-dispatcher/ssi263.conf" <<'EOF'
# The SSI-263 voices (sd_ssi263): the Braille Lite 2000 (English, Spanish), the Accent SA, the Accent-mini and the
# Speak-Out -- each one's own firmware speaking through an emulated SSI-263, as in the NVDA add-ons.  Choose one in
# Orca (Preferences, Speech, Voice) or with spd-say -o ssi263 -y "Accent SA".  Rate, pitch and volume come from
# Orca or spd-say for every voice; the keys below are each voice's own.
#
# This file:      speech-dispatcher's modules folder (install.sh put it there and wrote SSI263DataDir below).
# Your own copy:  ~/.config/ssi263-speech/sd_ssi263.conf -- any line there wins over this file, needs no root,
#                 and survives reinstalling.  Same keys, same format.
#
# Uncomment a line and change it to override the default.
# After editing:  killall speech-dispatcher     (Orca reconnects by itself)

# Output sample rate in Hz, for every voice: 11025, 22050 or 44100 (default 22050).  22 kHz keeps everything the chip
# produces; 44 kHz also keeps the clock images and the brightest hiss; 11 kHz sounds like the unit's own speaker.
# SSI263SampleRate 22050

# ---- the Braille Lite 2000 ----

# The unit's voice inflection, its own status-menu setting: 1 on (default), 0 off (questions stay flat).
# SSI263Inflection 1

# The faint sound a real unit makes under its speech: off (default), hiss (even volumes, the factory setting),
# or whine (odd volumes).
# SSI263Whine off

# The unit's tone, 0-26 (factory 7).
# SSI263Tone 7

# Sentences packed onto one line from the second on, for shorter pauses (1, default) or not (0).
# SSI263ShortPauses 1

# EXPERIMENTAL: run the unit ahead of its chip (1) or not (0, default), as the NVDA add-on's "Run the unit ahead".
# The unit reads each message ahead and the chip plays what it wrote at the unit's own answer times: the same
# phonemes, without the pauses where the unit reads its next line.  Only with SSI263ShortPauses 1.
# SSI263RunAhead 0

# Numbers read as words (1, default), as the NVDA add-on's "Custom number processing": "1,234,567" as one million two
# hundred thirty four thousand..., and the Spanish unit in Spain's way ("1.234.567", "3,5" as tres coma cinco).  0: the
# unit's own firmware reads them (digit by digit above its limit).
# SSI263BrailleLiteNumbers 1

# ---- the Accent SA and the Accent-mini (the NVDA Accent add-on's settings) ----

# Inflection, 0-100 (default 100): 100 full intonation, 75, 50, 25, 0 monotone (the nearest of the five is taken).
# SSI263AccentInflection 100

# The add-on's custom number processing (1, default): numbers read as the add-on reads them, not digit by digit.
# SSI263AccentNumbers 1

# The Accent-mini's voice characteristic, 0-9 (default 5; the add-on's variant).
# SSI263AccentMiniVoice 5

# ---- the Speak-Out (the NVDA Speak-Out add-on's settings) ----

# The box's tone, A-Z (default I; the add-on's variant).
# SSI263SpeakOutTone I

# Join phrases: fewer pauses between words (1, default) or the box's own (0).
# SSI263SpeakOutJoin 1

# Shorten pauses between sentences (1, default) or not (0).
# SSI263SpeakOutShortPauses 1
EOF
cp "$ROOT/src/platforms/speechd/install.sh" "$ROOT/src/platforms/speechd/uninstall.sh" "$STAGE/"
cp "$ROOT/src/platforms/speechd/README-linux.md" "$STAGE/README.md"
cp "$ROOT/LICENSE" "$STAGE/LICENSE"
cp "$ROOT/src/csrc/cpu/mame_z180/LICENSE-BSD-3-Clause.txt" "$STAGE/licenses/MAME-Z180-core-BSD-3-Clause.txt"
cp "$ROOT/third_party/casso/LICENSE" "$STAGE/licenses/Casso-MIT.txt"
chmod +x "$STAGE/install.sh" "$STAGE/uninstall.sh" "$STAGE/bin/sd_ssi263"
(cd "$ROOT/build/package" && tar -czf "$ROOT/build/$NAME.tar.gz" "$NAME")
echo "packaged build/$NAME.tar.gz"
