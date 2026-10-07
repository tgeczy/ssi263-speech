#!/bin/sh
# Build the one native library for Linux: the SSI-263 chip, the Braille Lite board on MAME's Z180 core and the
# Braille Lite host, as libssi263speech.so -- plus the speech-dispatcher module and the test programs.
#
#   ./build_linux.sh                 # build/linux/libssi263speech.so, sd_ssi263, the tests
#   LEGACY=1 ./build_linux.sh        # also the DEVELOPMENT references on z180emu (GPL), in build/linux/legacy/:
#                                    # never copied into src/ssi263/_bin, the package or the wheel
#   CC=clang CXX=clang++ ./build_linux.sh
#
# On Windows the same sources make ssi263.dll and bl.dll (src/csrc/build_native.py, src/csrc/blazie/build_board.py);
# the flags here are theirs: -ffp-contract=off keeps the Python reference's arithmetic (no fused multiply-adds),
# so the golden vectors (nvda/tools/golden) hold on every platform.  The .so is also copied to
# src/ssi263/_bin/<platform>-<machine>/, where ssi263/dsp.py loads it.
#
# Licences: the project's code is MIT; MAME's extracted CPU cores keep their BSD-3-Clause notices
# (src/csrc/cpu/mame_*/LICENSE-BSD-3-Clause.txt).  No z180emu (GPL) and no Unicorn in anything that ships
# (tools/check_no_gpl.py checks it).  No firmware is built, fetched or shipped by this script.
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="$ROOT/build/linux"
CC="${CC:-cc}"
CXX="${CXX:-c++}"
SRC="$ROOT/src/csrc"

# a fresh object folder for the shipping library: it is linked from an explicit list, and an object left by an
# older build (z180emu's bl_unity.o) must never come back in
rm -rf "$OUT/obj" "$OUT/obj_mame"
mkdir -p "$OUT/obj"

# the chip: plain C99, as build_native.py
CHIP="-O2 -std=c99 -ffp-contract=off -fPIC -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter"
# the board and host: gnu89 with the board on MAME's Z180 (-DBL_Z180_MAME), as build_board.py's bl.dll
BOARD="-O3 -std=gnu89 -ffp-contract=off -fPIC -fvisibility=hidden -DBL_Z180_MAME -w -I$SRC/blazie -I$SRC/cpu -I$SRC"
# MAME's cores: C++17 without exceptions or RTTI.  -fPIC and hidden for the shared library (its classes stay inside)
MAME="-O3 -std=c++17 -fno-exceptions -fno-rtti -ffp-contract=off -fPIC -fvisibility=hidden -Wall -I$SRC/cpu -I$SRC"
# libstdc++ and libgcc inside the .so, their symbols kept there: the library needs only libc and libm on any distro
SHARED_CXX="-static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL"

$CC $CHIP -c -o "$OUT/obj/ssi263.o" "$SRC/ssi263.c"
$CC $CHIP -c -o "$OUT/obj/ssi263dsp.o" "$SRC/ssi263dsp.c"
$CXX $MAME -c -o "$OUT/obj/z180_mame.o" "$SRC/cpu/z180_mame.cpp"
$CXX $MAME -c -o "$OUT/obj/z180_asci.o" "$SRC/cpu/z180_asci.cpp"
for f in bl_board flash29 bl_serial bl_idle bl_clock bl_host bl_voice bl_firmware bl_state; do
    $CC $BOARD -c -o "$OUT/obj/$f.o" "$SRC/blazie/$f.c"
done
CHIP_OBJS="$OUT/obj/ssi263.o $OUT/obj/ssi263dsp.o"
# the Braille Lite driver's number words (bl_numbers.h, for blv_set_numbers: English, and Spain's Spanish), as in
# ssi263speech.dll (build_ssi263speech.py): plain C99
for f in blazie/bl_numbers numwords numwords_es; do
    $CC -O2 -std=c99 -ffp-contract=off -fPIC -fvisibility=hidden -Wall -I$SRC -I$SRC/blazie -c -o "$OUT/obj/${f##*/}.o" "$SRC/$f.c"
done
NUM_OBJS="$OUT/obj/bl_numbers.o $OUT/obj/numwords.o $OUT/obj/numwords_es.o"
# the board alone (test_bl_board), and the board with its host and voice (the library)
BOARD_OBJS="$OUT/obj/bl_board.o $OUT/obj/flash29.o $OUT/obj/bl_serial.o $OUT/obj/bl_idle.o $OUT/obj/bl_clock.o $OUT/obj/z180_mame.o $OUT/obj/z180_asci.o"
LIB_OBJS="$CHIP_OBJS $BOARD_OBJS $OUT/obj/bl_host.o $OUT/obj/bl_voice.o $OUT/obj/bl_firmware.o $OUT/obj/bl_state.o $NUM_OBJS"

# only the API is exported (SSI263_API / BL_API mark it); the Z180 core and the C++ runtime stay inside
$CXX -shared $SHARED_CXX -o "$OUT/libssi263speech.so" $LIB_OBJS -lm
# two units in one process, on the shipping board
$CC -O3 -std=gnu89 -ffp-contract=off -I$SRC/blazie -I$SRC/cpu -I$SRC -c -o "$OUT/test_bl_board.o" "$SRC/blazie/test_bl_board.c"
$CXX -o "$OUT/test_bl_board" "$OUT/test_bl_board.o" $BOARD_OBJS -lm
# (the speech-dispatcher module is linked below, after the other engines' objects)
rm -f "$OUT/test_bl_board_mame"                     # the old name of test_bl_board on MAME

# The Blazie emulator in a terminal (src/apps/blazie, README-linux.md): the Braille Lite 2000 and the Type 'n Speak
# running their own firmware, on MAME's Z180 only (the same objects as the library: no z180emu).  One program,
# libstdc++ inside: it needs libc, libm, libpthread and libasound.  Its sound is ALSA's (libasound2-dev to build), or
# PulseAudio's simple API when only that is there (libpulse-dev; or BLAZIE_AUDIO=pulse).  Without either it is not
# built, and tools/linux_tests.sh says so.  test_keys: its keyboard without a unit (no sound, no firmware).
APP="$ROOT/src/apps/blazie"
APPF="-O2 -std=gnu99 -ffp-contract=off -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -I$APP -I$SRC -fmacro-prefix-map=$ROOT=."
rm -rf "$OUT/obj_emu" "$OUT/blazie_emu" "$OUT/blazie_bt" "$OUT/blazie_emu_bt"; mkdir -p "$OUT/obj_emu"
$CC $BOARD -c -o "$OUT/obj_emu/tns_board.o" "$SRC/blazie/tns_board.c"
for f in emu_unit chords keys term_keys bl_keys tns_term ini tns_setup tns_rescue audio_pace; do
    $CC $APPF -c -o "$OUT/obj_emu/$f.o" "$APP/$f.c"
done
# the units' files (src/csrc/blazie/bl_files*.c, fat_img.c: portable C99, no unit runs)
for f in bl_files bl_files_state bl_files_xfer fat_img; do
    $CC $APPF -c -o "$OUT/obj_emu/$f.o" "$SRC/blazie/$f.c"
done
FILES_OBJS="$OUT/obj_emu/bl_files.o $OUT/obj_emu/bl_files_state.o $OUT/obj_emu/bl_files_xfer.o $OUT/obj_emu/fat_img.o"
KEY_OBJS="$OUT/obj_emu/chords.o $OUT/obj_emu/keys.o $OUT/obj_emu/term_keys.o $OUT/obj_emu/bl_keys.o $OUT/obj_emu/tns_term.o $OUT/obj_emu/ini.o"
# the unit, and the Type 'n Speak's factory setup (its cold reset's questions answered: the tests, the rescue)
EMU_OBJS="$CHIP_OBJS $BOARD_OBJS $OUT/obj/bl_host.o $OUT/obj_emu/tns_board.o $OUT/obj_emu/emu_unit.o $OUT/obj_emu/tns_setup.o"
# a saved Type 'n Speak that was never set up (the previews' first start), told apart and set up anew
RESCUE_OBJS="$OUT/obj_emu/tns_rescue.o $OUT/obj_emu/bl_files.o $OUT/obj_emu/bl_files_state.o"
$CC $APPF -o "$OUT/test_keys" "$APP/test_keys.c" $KEY_OBJS
$CC $APPF -o "$OUT/test_display" "$APP/test_display.c"
# the BT Speak's and BT Braille's keyboard server (btkb_linux.c) and the braille display (brl_linux.c): test_btkb runs
# the keys against a keyboard server of its own (no device, no BRLTTY needed)
for f in btkb_linux brl_linux; do
    $CC $APPF -c -o "$OUT/obj_emu/$f.o" "$APP/$f.c"
done
$CC $APPF -c -o "$OUT/obj_emu/evdev_keys.o" "$APP/evdev_linux.c"
BT_OBJS="$OUT/obj_emu/btkb_linux.o $OUT/obj_emu/brl_linux.o"
$CC $APPF -o "$OUT/test_btkb" "$APP/test_btkb.c" $BT_OBJS "$OUT/obj_emu/evdev_keys.o" $KEY_OBJS -lpthread -ldl
# blazie_files: a saved unit's files from the command line (export, import, extract, pack, unpack), as on Windows
$CC $APPF -o "$OUT/blazie_files" "$APP/blazie_files.c" $FILES_OBJS
# the unit's own headless tests, as on Windows (build_app.py's test_emu_unit, test_clock, test_rescue), on MAME's Z180
$CC $APPF -c -o "$OUT/obj_emu/test_emu_unit.o" "$APP/test_emu_unit.c"
$CXX $SHARED_CXX -o "$OUT/test_emu_unit" "$OUT/obj_emu/test_emu_unit.o" $EMU_OBJS -lm
$CC $APPF -c -o "$OUT/obj_emu/test_clock.o" "$APP/test_clock.c"
$CXX $SHARED_CXX -o "$OUT/test_clock" "$OUT/obj_emu/test_clock.o" $EMU_OBJS -lm
$CC $APPF -c -o "$OUT/obj_emu/test_rescue.o" "$APP/test_rescue.c"
$CXX $SHARED_CXX -o "$OUT/test_rescue" "$OUT/obj_emu/test_rescue.o" $RESCUE_OBJS $EMU_OBJS -lm
# the sound buffer (audio_pace.c, portable) against a simulated sound card, and the Linux shells' reading of the
# device's queue and played position; --old: its must-fail control (the 0.7.0 draft's queue), as on Windows
$CC $APPF -o "$OUT/test_audio" "$APP/test_audio.c" "$OUT/obj_emu/audio_pace.o"
AUDIO_DEF=""; AUDIO_LIBS=""; SOUND=""
if [ "${BLAZIE_AUDIO:-alsa}" != pulse ] && pkg-config --exists alsa 2>/dev/null; then
    AUDIO_LIBS="$(pkg-config --libs alsa)"; SOUND=ALSA
elif pkg-config --exists libpulse-simple 2>/dev/null; then
    AUDIO_DEF="-DBLAZIE_AUDIO_PULSE"; AUDIO_LIBS="$(pkg-config --libs libpulse-simple)"; SOUND=PulseAudio
fi
if [ -n "$AUDIO_LIBS" ]; then
    # bt_handover: on a BT Speak or BT Braille, blazie_emu hands over to blazie_emu_bt (built below)
    for f in audio_linux serial_linux evdev_linux bt_handover main_linux; do
        $CC $APPF $AUDIO_DEF -c -o "$OUT/obj_emu/$f.o" "$APP/$f.c"
    done
    # bt_handover: [input] bt = frontend hands over to blazie_emu_bt; otherwise on a BT Speak or BT Braille this
    # program reads the device's keyboard server and BRLTTY's display itself (BrlAPI, loaded when it runs: -ldl)
    $CXX $SHARED_CXX -o "$OUT/blazie_emu" "$OUT/obj_emu/main_linux.o" "$OUT/obj_emu/bt_handover.o" "$OUT/obj_emu/audio_linux.o" \
        "$OUT/obj_emu/audio_pace.o" "$OUT/obj_emu/serial_linux.o" "$OUT/obj_emu/evdev_linux.o" $BT_OBJS $KEY_OBJS $RESCUE_OBJS $EMU_OBJS $AUDIO_LIBS \
        -lpthread -ldl -lm
    echo "built $OUT/blazie_emu (sound: $SOUND)"
    # Native worker for the BT Speak / BT Braille Python frontend. Shares the same board and audio code.
    $CC $APPF $AUDIO_DEF -c -o "$OUT/obj_emu/bt_backend.o" "$ROOT/src/platforms/btspeak/backend.c"
    $CXX $SHARED_CXX -o "$OUT/blazie_bt" "$OUT/obj_emu/bt_backend.o" "$OUT/obj_emu/audio_linux.o" \
        "$OUT/obj_emu/audio_pace.o" "$OUT/obj_emu/keys.o" "$OUT/obj_emu/tns_term.o" $EMU_OBJS $AUDIO_LIBS -lm
    echo "built $OUT/blazie_bt (BT Speak / BT Braille worker)"
    python3 "$ROOT/tools/build_bt_frontend.py" "$OUT/blazie_emu_bt"
    echo "built $OUT/blazie_emu_bt (BT Speak / BT Braille frontend, Python 3.11+)"
else
    echo "NOT built: blazie_emu -- no ALSA (sudo apt install libasound2-dev) nor PulseAudio (libpulse-dev) headers"
fi
# The same emulator in a GTK 3 window (main_gtk.c), for a desktop and Orca: built when GTK 3's headers are there
# (libgtk-3-dev), skipped without them -- the terminal blazie_emu above never needs GTK (the BTSpeak has no desktop).
# GTK (LGPL) is linked dynamically, as the system's library; libstdc++ inside as above.  --as-needed: only the
# libraries it calls are listed (tools/linux_tests.sh checks them).
rm -f "$OUT/blazie_emu_gtk"
if [ -n "$AUDIO_LIBS" ] && pkg-config --exists gtk+-3.0 2>/dev/null; then
    $CC $APPF $AUDIO_DEF $(pkg-config --cflags gtk+-3.0) -Wno-cast-function-type -c -o "$OUT/obj_emu/main_gtk.o" \
        "$APP/main_gtk.c"
    $CXX $SHARED_CXX -Wl,--as-needed -o "$OUT/blazie_emu_gtk" "$OUT/obj_emu/main_gtk.o" "$OUT/obj_emu/audio_linux.o" \
        "$OUT/obj_emu/audio_pace.o" "$OUT/obj_emu/serial_linux.o" "$OUT/obj_emu/evdev_linux.o" $KEY_OBJS $RESCUE_OBJS \
        "$OUT/obj_emu/bl_files_xfer.o" "$OUT/obj_emu/fat_img.o" $EMU_OBJS $AUDIO_LIBS $(pkg-config --libs gtk+-3.0) \
        -lpthread -lm
    echo "built $OUT/blazie_emu_gtk (GTK $(pkg-config --modversion gtk+-3.0), sound: $SOUND)"
elif [ -n "$AUDIO_LIBS" ]; then
    echo "skipped: blazie_emu_gtk -- no GTK 3 headers (sudo apt install libgtk-3-dev); the terminal blazie_emu is built"
fi

# MAME's Z180 core's own tests (CONTRACT.md's clauses, and white-box)
$CC -O2 -std=gnu89 -I$SRC/cpu -I$SRC -c -o "$OUT/test_z180_contract.o" "$SRC/cpu/test_z180_contract.c"
$CXX -o "$OUT/test_z180_contract" "$OUT/test_z180_contract.o" "$OUT/obj/z180_mame.o" "$OUT/obj/z180_asci.o"
$CXX $MAME -o "$OUT/test_z180_whitebox" "$SRC/cpu/test_z180_whitebox.cpp" "$OUT/obj/z180_asci.o"
# MAME's 8085 core (src/csrc/cpu/i8085_mame.cpp, the Accent SA's; its board below): the CPU contract's tests.  Its own
# object folder.
MAMET="-O3 -std=c++17 -fno-exceptions -fno-rtti -ffp-contract=off -Wall -I$SRC/cpu -I$SRC"
mkdir -p "$OUT/obj_i8085"
$CXX $MAMET -Wno-sign-compare -c -o "$OUT/obj_i8085/i8085_mame.o" "$SRC/cpu/i8085_mame.cpp"
$CC -O2 -std=gnu89 -I$SRC/cpu -I$SRC -c -o "$OUT/obj_i8085/test_i8085_contract.o" "$SRC/cpu/test_i8085_contract.c"
$CXX -o "$OUT/test_i8085_contract" "$OUT/obj_i8085/test_i8085_contract.o" "$OUT/obj_i8085/i8085_mame.o"
# MAME's V40 core (src/csrc/cpu/v40_mame.cpp, the Speak-Out's): the CPU contract's tests, the Speak-Out board's tests
# (src/csrc/speakout) and the board as a shared library for src/hosts/speakout_v40.py.
rm -rf "$OUT/obj_v40"; mkdir -p "$OUT/obj_v40"
$CXX $MAMET -fPIC -Wno-sign-compare -c -o "$OUT/obj_v40/v40_mame.o" "$SRC/cpu/v40_mame.cpp"
for f in so_board so_icu so_scu so_hex; do
    $CC -O2 -std=gnu89 -ffp-contract=off -fPIC -Wall -I$SRC/cpu -I$SRC/speakout -c -o "$OUT/obj_v40/$f.o" "$SRC/speakout/$f.c"
done
$CC -O2 -std=gnu89 -I$SRC/cpu -c -o "$OUT/test_v40_contract.o" "$SRC/cpu/test_v40_contract.c"
$CXX -o "$OUT/test_v40_contract" "$OUT/test_v40_contract.o" "$OUT/obj_v40/v40_mame.o"
$CC -O2 -std=gnu89 -I$SRC/cpu -I$SRC/speakout -c -o "$OUT/test_so_board.o" "$SRC/speakout/test_so_board.c"
$CXX -o "$OUT/test_so_board" "$OUT/test_so_board.o" "$OUT"/obj_v40/*.o
$CXX -shared -o "$OUT/libspeakout_v40.so" "$OUT"/obj_v40/*.o
# The Speak-Out's host and voice in C (so_host.h, so_voice.h: speakout.py and the NVDA driver's front end), compiled
# only, into their own folder (the board's library and test above link obj_v40/*.o): a front end links them with
# obj_v40's board and core, numwords.o and the chip's objects (so_voice.h lists them).  -fvisibility=hidden: only
# SO_API is exported from a library they go into.
rm -rf "$OUT/obj_sovoice"; mkdir -p "$OUT/obj_sovoice"
$CC -O2 -std=gnu89 -ffp-contract=off -fPIC -fvisibility=hidden -Wall -I$SRC/cpu -I$SRC/speakout -I$SRC -c -o "$OUT/obj_sovoice/so_host.o" "$SRC/speakout/so_host.c"
for f in speakout/so_voice numwords; do
    $CC -O2 -std=c99 -ffp-contract=off -fPIC -fvisibility=hidden -Wall -I$SRC/cpu -I$SRC/speakout -I$SRC -c -o "$OUT/obj_sovoice/$(basename $f).o" "$SRC/$f.c"
done

# MAME's 8086 core (src/csrc/cpu/i86_mame.cpp, the Accent-mini's PC): the CPU contract's tests, and libpc86.so
# (src/csrc/pc86) for the Accent-mini host (src/hosts/pc86.py).  Own folder.
rm -rf "$OUT/obj_i86"; mkdir -p "$OUT/obj_i86"
$CXX $MAMET -fPIC -Wno-sign-compare -c -o "$OUT/obj_i86/i86_mame.o" "$SRC/cpu/i86_mame.cpp"
$CC -O2 -std=gnu89 -I$SRC/cpu -I$SRC -c -o "$OUT/obj_i86/test_i86_contract.o" "$SRC/cpu/test_i86_contract.c"
$CXX -o "$OUT/test_i86_contract" "$OUT/obj_i86/test_i86_contract.o" "$OUT/obj_i86/i86_mame.o"
$CC -O3 -std=gnu89 -fPIC -I$SRC/cpu -c -o "$OUT/obj_i86/pc86.o" "$SRC/pc86/pc86.c"
$CXX -shared -o "$OUT/libpc86.so" "$OUT/obj_i86/pc86.o" "$OUT/obj_i86/i86_mame.o"

# The Accent SA (src/csrc/accentsa: its board on MAME's 8085 and accent_sa.py's host in C): the board's tests,
# as_render (the C API alone, the chip built in) and libaccent_sa.so for src/hosts/accent_sa_c.py.  The .so leaves
# the chip's functions undefined: they come from the libssi263speech.so that ssi263/native.py loaded, made global by
# accent_sa_c.py first, so the host drives the caller's chip.  Own folder.
rm -rf "$OUT/obj_accentsa"; mkdir -p "$OUT/obj_accentsa"
$CXX $MAMET -fPIC -Wno-sign-compare -c -o "$OUT/obj_accentsa/i8085_mame.o" "$SRC/cpu/i8085_mame.cpp"
for f in as_board as_usart as_host; do
    $CC -O2 -std=gnu89 -ffp-contract=off -fPIC -fvisibility=hidden -Wall -I$SRC/cpu -I$SRC/accentsa -I$SRC -c -o "$OUT/obj_accentsa/$f.o" "$SRC/accentsa/$f.c"
done
$CC -O2 -std=gnu89 -I$SRC/cpu -I$SRC/accentsa -c -o "$OUT/test_as_board.o" "$SRC/accentsa/test_as_board.c"
$CXX -o "$OUT/test_as_board" "$OUT/test_as_board.o" "$OUT/obj_accentsa/as_board.o" "$OUT/obj_accentsa/as_usart.o" "$OUT/obj_accentsa/i8085_mame.o"
$CC -O2 -std=gnu89 -I$SRC/accentsa -I$SRC -c -o "$OUT/as_render.o" "$SRC/accentsa/as_render.c"
$CXX -o "$OUT/as_render" "$OUT/as_render.o" "$OUT"/obj_accentsa/*.o $CHIP_OBJS -lm
$CXX -shared -o "$OUT/libaccent_sa.so" "$OUT"/obj_accentsa/*.o -lm

# The speech-dispatcher module (src/platforms/speechd): one program with every voice's engine inside (no .so to
# install beside it) -- the Braille Lite (the library's objects), the Accent SA (as_voice on the board above), and
# the Accent-mini (am_voice, MAME's 8086) and the Speak-Out (so_voice, MAME's V40) once their voices' sources are in
# the tree: voices.c (src/csrc/voices.h, the voice table the SAPI engine and Android share) gets SSV_HAVE_ACCENTMINI and
# SSV_HAVE_SPEAKOUT for them.  libsd_voices_ref.so: the same engine objects with the chip and the number words, the
# test's reference (test_sd_ssi263.py drives the voices directly); never shipped.
rm -rf "$OUT/obj_voices"; mkdir -p "$OUT/obj_voices"
VOICE="-O2 -std=gnu89 -ffp-contract=off -fPIC -fvisibility=hidden -Wall -I$SRC -I$SRC/cpu -I$SRC/accentsa -I$SRC/pc86 -I$SRC/speakout"
VOICE_SRCS="$SRC/accentsa/as_voice.c"            # numwords.o: the library's ($NUM_OBJS)
[ -f "$SRC/accent_text.c" ] && VOICE_SRCS="$VOICE_SRCS $SRC/accent_text.c"   # the Accents' shared text rules
SD_DEFS=""
ENGINE_OBJS="$OUT/obj_accentsa/i8085_mame.o $OUT/obj_accentsa/as_board.o $OUT/obj_accentsa/as_usart.o $OUT/obj_accentsa/as_host.o"
if [ -f "$SRC/accentmini/am_voice.c" ]; then
    VOICE_SRCS="$VOICE_SRCS $SRC/accentmini/am_voice.c $SRC/accentmini/am_host.c"
    ENGINE_OBJS="$ENGINE_OBJS $OUT/obj_i86/pc86.o $OUT/obj_i86/i86_mame.o"
    SD_DEFS="$SD_DEFS -DSSV_HAVE_ACCENTMINI"
fi
if [ -f "$SRC/speakout/so_voice.c" ]; then
    VOICE_SRCS="$VOICE_SRCS $SRC/speakout/so_voice.c $SRC/speakout/so_host.c"
    ENGINE_OBJS="$ENGINE_OBJS $OUT/obj_v40/so_board.o $OUT/obj_v40/so_icu.o $OUT/obj_v40/so_scu.o $OUT/obj_v40/so_hex.o $OUT/obj_v40/v40_mame.o"
    SD_DEFS="$SD_DEFS -DSSV_HAVE_SPEAKOUT"
fi
for f in $VOICE_SRCS; do
    $CC $VOICE -I"$(dirname "$f")" -c -o "$OUT/obj_voices/$(basename "$f" .c).o" "$f"
done
ENGINE_OBJS="$ENGINE_OBJS $OUT/obj_voices/*.o"
$CC $BOARD -c -o "$OUT/sd_ssi263.o" "$ROOT/src/platforms/speechd/sd_ssi263.c"
$CC $BOARD -c -o "$OUT/sd_voices.o" "$ROOT/src/platforms/speechd/sd_voices.c"
$CC $VOICE $SD_DEFS -c -o "$OUT/sd_ssv.o" "$SRC/voices.c"
$CXX $SHARED_CXX -o "$OUT/sd_ssi263" "$OUT/sd_ssi263.o" "$OUT/sd_voices.o" "$OUT/sd_ssv.o" $LIB_OBJS $ENGINE_OBJS -lm
$CXX -shared $SHARED_CXX -o "$OUT/libsd_voices_ref.so" $CHIP_OBJS $NUM_OBJS $ENGINE_OBJS -lm
echo "built $OUT/sd_ssi263: $("$OUT/sd_ssi263" --voices | cut -f3 | tr '\n' ',' | sed 's/,$//; s/,/, /g')"

# The Accent-mini in C (src/csrc/accentmini: SPKEMS.DVC on MAME's 8086 above, accent.py's host and the NVDA driver's
# front end): its objects, and am_render (the C API alone, the chip built in).  Own folder; nothing else links it.
rm -rf "$OUT/obj_accentmini"; mkdir -p "$OUT/obj_accentmini"
for f in accentmini/am_host accentmini/am_voice accent_text numwords; do
    $CC -O2 -std=gnu89 -ffp-contract=off -fPIC -fvisibility=hidden -Wall -I$SRC/cpu -I$SRC/pc86 -I$SRC -c -o "$OUT/obj_accentmini/${f##*/}.o" "$SRC/$f.c"
done
$CC -O2 -std=gnu89 -ffp-contract=off -Wall -I$SRC -c -o "$OUT/am_render.o" "$SRC/accentmini/am_render.c"
$CXX -o "$OUT/am_render" "$OUT/am_render.o" "$OUT"/obj_accentmini/*.o "$OUT/obj_i86/pc86.o" "$OUT/obj_i86/i86_mame.o" $CHIP_OBJS -lm

# DEVELOPMENT ONLY (LEGACY=1): the board on z180emu's Z180 (GPL-2.0-or-later, third_party/z180emu), as references
# for comparing the cores and as tools/check_no_gpl.py's must-fail control.  Its own folder, build/linux/legacy;
# nothing below is copied to src/ssi263/_bin, packaged (tools/package_linux.sh) or put in a wheel.
rm -rf "$OUT/legacy" "$OUT/test_z180_legacy"
if [ "${LEGACY:-0}" = 1 ]; then
    Z180="${Z180EMU:-$ROOT/third_party/z180emu}"
    [ -f "$Z180/z180/z180.c" ] || { echo "LEGACY=1: z180emu not found at $Z180 (set Z180EMU)"; exit 1; }
    L="$OUT/legacy"
    mkdir -p "$L/obj"
    # gnu89 and -fcommon for z180emu's MAME-era C; initial-exec TLS for the adapter's instance pointer
    # (src/csrc/cpu/z180_legacy.c); the prefix map keeps the checkout's path out of z180.c's __FILE__
    LB="-O3 -ftls-model=initial-exec -fcommon -std=gnu89 -ffp-contract=off -fPIC -fvisibility=hidden -w -I$Z180 -I$Z180/z180 -fmacro-prefix-map=$Z180=."
    $CC $LB -c -o "$L/obj/bl_unity.o" "$SRC/blazie/bl_unity.c"
    for f in bl_host bl_voice bl_firmware bl_state; do $CC $LB -c -o "$L/obj/$f.o" "$SRC/blazie/$f.c"; done
    $CC -shared -o "$L/libssi263speech_legacy.so" $CHIP_OBJS "$L"/obj/*.o -lm
    $CC $LB -o "$L/test_bl_board_legacy" "$SRC/blazie/test_bl_board.c" "$L/obj/bl_unity.o" -lm
    # the legacy path's exceptions (CONTRACT.md 3) on z180emu
    $CC $LB -I$SRC/cpu -o "$L/test_z180_legacy" "$SRC/cpu/test_z180_legacy.c" "$SRC/cpu/z180_legacy.c" -lm
    echo "built the z180emu development references in $L (GPL; never shipped)"
fi

PLAT="$(python3 -c 'import sys, platform; print("%s-%s" % (sys.platform, platform.machine()))')"
mkdir -p "$ROOT/src/ssi263/_bin/$PLAT"
cp "$OUT/libssi263speech.so" "$ROOT/src/ssi263/_bin/$PLAT/"
echo "built $OUT/libssi263speech.so ($PLAT, MAME Z180)"
