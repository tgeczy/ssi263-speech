#!/bin/sh
# linux_ci_checks.sh [source tree] -- the Linux gate's checks that need no firmware, run in CI right after
# ./build_linux.sh (.github/workflows/linux.yml, in Debian 11).  The whole gate, these again among them, runs with the
# firmware on the Pi against the same binaries (tools/linux_tests.sh, by tools/release_from_ci.sh); the commands and
# the controls' marks here are linux_tests.sh's: keep them in step.
#
#   sh tools/linux_ci_checks.sh [source tree]      (default: this script's own tree; CI passes the commit it built,
#                                                   which may predate this script)
#
# Only here, where the binaries are built: no build path in what ships (the build's own root), and the GLIBC floor
# of everything that ships (2.29: tools/check_glibc_floor.sh).
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "${1:-$HERE/..}" && pwd)"
PLAT="$(python3 -c 'import sys, platform; print("%s-%s" % (sys.platform, platform.machine()))')"
LIB="$ROOT/src/ssi263/_bin/$PLAT/libssi263speech.so"
export SSI263_LIB="$LIB" PYTHONDONTWRITEBYTECODE=1 PYTHON_COLORS=0
cd "$ROOT" || exit 1
fail=0
check() {                          # name, then the command (as linux_tests.sh's)
    name="$1"; shift
    out="$("$@" 2>&1)"; got=$?
    if [ $got -eq 0 ]; then echo "ok    $name: $(echo "$out" | tail -1 | cut -c1-90)"
    else echo "FAIL  $name: $(echo "$out" | tail -1 | cut -c1-90)"; fail=1; fi
}
. "$HERE/linux_control.sh"         # control(): a must-fail control, judged by its marks
# the speech-dispatcher installer on scratch folders (Garrett, issue #10): a stock speechd.conf left alone (an
# AddModule line turns autodiscovery off), an earlier install's line removed, a listed config listed; its control
# is 0.7.6's always-added line
check "speech-dispatcher installer (speechd.conf)" sh tools/speechd_install_test.sh
control "speech-dispatcher installer CONTROL (0.7.6's AddModule line, must fail)" \
    "^FAIL stock config unchanged " "^FAIL earlier install's line removed " "^ok +listed config: this module listed too " \
    -- env SSI263_INSTALL_BREAK=1 sh tools/speechd_install_test.sh
B=build/linux
EMU=$B/blazie_emu
GTK_EMU=""; [ -x $B/blazie_emu_gtk ] && GTK_EMU=$B/blazie_emu_gtk
SHIPS="$LIB $B/sd_ssi263 $B/blazie_emu $B/blazie_files $B/blazie_bt $B/blazie_emu_bt $GTK_EMU"

check "CPU contract tests (MAME Z180 core)" ./$B/test_z180_contract
check "white-box tests (MAME Z180 core)" ./$B/test_z180_whitebox
check "CPU contract tests (MAME 8085 core)" ./$B/test_i8085_contract
check "CPU contract tests (MAME 8086 core)" ./$B/test_i86_contract
check "CPU contract tests (MAME V40 core)" ./$B/test_v40_contract
check "chip defaults" python3 src/csrc/gen_chip_defaults.py --check
# accented letters (src/csrc/translit.h): the module and every voice's bytes need no firmware (linux_tests.sh's)
check "accented letters: translit.h (C), the table, alone and in a word, case" python3 nvda/tools/translit_test.py module
control "accented letters: translit.h CONTROL (the pass off, must fail)" \
    '^FAIL lone: "\\xc3\\xa1" -> "\\xc3\\xa1", not "a acute"$' '^ok   ascii: all 128 characters pass unchanged' \
    '^translit: [0-9]+ of 83 FAILED \(TRANSLIT_BREAK=1: the pass is off\)$' \
    -- env TRANSLIT_BREAK=1 python3 nvda/tools/translit_test.py module
check "accented letters: the bytes every voice sends" python3 nvda/tools/translit_test.py bytes
control "accented letters: bytes CONTROL (the pass off, must fail)" '^FAIL blazie +bytes ' '^FAIL speakout +bytes ' \
    "^ +'t\\\\xfck\\\\xf6r' -> b't k r" '^translit bytes: 5 of 10 FAILED' \
    -- env TRANSLIT_BREAK=1 python3 nvda/tools/translit_test.py bytes
check "emulator: the keyboard (terminal, chords, letters, hold, Type 'n Speak)" ./$B/test_keys build
check "emulator: braille display wiring and latch" ./$B/test_display
control "emulator: keyboard CONTROL (dots 1 and 4 swapped, must fail)" "^FAIL +keys mode: o-chord, then t" \
    "^FAIL +letters mode: computer braille" "^ok +tns: y " "^ok +terminal: F12" "^test_keys: [0-9]+ of [0-9]+ FAILED$" \
    -- env BLAZIE_KEYS_BREAK=1 ./$B/test_keys build
check "emulator: the sound buffer (simulated card)" ./$B/test_audio
control "emulator: sound buffer CONTROL (the 0.7.0 draft's four blocks, must fail)" \
    "^FAIL busy machine: no gap after 10 s +[0-9]{2,} gaps after 10 s" \
    "^FAIL linux: busy machine: no gap after 10 s +[0-9]{2,} gaps after 10 s" \
    "^FAIL linux: autosave: no gap +[0-9]+ saves of 50 ms: [1-9][0-9]* gaps" \
    "^ok +linux: steady card: no gap after the first second" "^ok +card: the played position from the delay" \
    "^audio: 14 FAILED$" -- ./$B/test_audio --old
if [ -x "$EMU" ]; then
    check "emulator: libraries needed (libc, libm, libasound/libpulse; libstdc++ inside)" sh -c "! ldd $EMU | \
        grep -v -E 'linux-vdso|ld-linux|libc\.so|libm\.so|libpthread|libasound|libpulse|libdl' | grep -q . && \
        ! ldd $EMU | grep -q -E 'libstdc|libgcc_s' && echo 'only the C library and the sound library'"
else
    echo "FAIL  emulator: $EMU not built (libasound2-dev)"; fail=1
fi
if [ -n "$GTK_EMU" ]; then
    check "emulator (GTK): libraries needed (libc, libm, sound, GTK's own; libstdc++ inside)" sh -c "! readelf -d \
        $GTK_EMU | grep NEEDED | grep -v -E '\[lib(c|m|pthread|dl|asound|pulse|pulse-simple|gtk-3|gdk-3|glib-2\.0|\
gobject-2\.0|gio-2\.0|atk-1\.0|pango-1\.0|pangocairo-1\.0|cairo|cairo-gobject|gdk_pixbuf-2\.0|harfbuzz)\.so' | \
        grep -v -F '[ld-linux' | grep -q . &&! readelf -d $GTK_EMU | grep NEEDED | grep -q -E 'libstdc|libgcc_s' && \
        echo 'only the C library, the sound library and GTK (dynamic)'"
else
    echo "FAIL  emulator (GTK): $B/blazie_emu_gtk not built (libgtk-3-dev)"; fail=1
fi
# shellcheck disable=SC2086
check "no z180emu or Unicorn engine, no GPL notice in what ships" python3 tools/check_no_gpl.py $SHIPS
check "no-GPL audit: comments and MAME compatibility names are not evidence" python3 tools/check_no_gpl.py \
    --clean-sample
control "no-GPL audit CONTROL (genuine legacy payloads, must fail)" \
    "^FAIL control\.apk: control\.apk!lib/arm64-v8a/libssi263speech\.so: z180emu engine" \
    "^FAIL control\.apk: control\.apk!lib/x86/unicorn\.dll: a legacy payload by name" \
    "^no-GPL audit: 1 of 1 FAILED$" -- python3 tools/check_no_gpl.py --control
# shellcheck disable=SC2086
check "no build path in what ships" sh -c "! grep -a -q -F '$ROOT' $SHIPS && echo 'no $ROOT inside'"
# shellcheck disable=SC2086
check "GLIBC floor 2.29, no system C++ runtime" sh "$HERE/check_glibc_floor.sh" 2.29 $SHIPS
check "control guard" sh "$HERE/linux_control_guard.sh"
[ $fail -eq 0 ] && echo "Linux checks without firmware passed ($PLAT)" || echo "Linux checks without firmware FAILED ($PLAT)"
exit $fail
