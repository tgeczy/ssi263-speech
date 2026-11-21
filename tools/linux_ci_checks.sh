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
control "speech-dispatcher installer CONTROL (a plain copy onto a running module, must fail)" \
    "^ok +stock config unchanged " "^FAIL install over a running module " "^FAIL library replaced, not rewritten " \
    -- env SSI263_INSTALL_BREAK=copy sh tools/speechd_install_test.sh
# every firmware folder the package carries, installed (#44: three were, and 21 voices never appeared); its control
# copies the three it named
control "speech-dispatcher installer CONTROL (only the three folders it named, must fail)" \
    "^ok +stock config unchanged " "^FAIL every firmware folder installed +not installed: .*gw-micro-sounding-board" \
    "^speechd install: FAILED$" -- env SSI263_INSTALL_BREAK=folders sh tools/speechd_install_test.sh
# --default run twice, then uninstalled, gives the config back (#45: a DefaultModule ssi263 was left); its control is
# the second run commenting out its own line, and uninstall.sh restoring it
control "speech-dispatcher installer CONTROL (--default twice left our DefaultModule, must fail)" \
    "^ok +--default " "^FAIL --default twice " "^FAIL uninstall \(--default twice\) " \
    "^FAIL uninstall \(earlier --default twice\) " -- env SSI263_INSTALL_BREAK=default sh tools/speechd_install_test.sh
B=build/linux
EMU=$B/blazie_emu
GTK_EMU=""; [ -x $B/blazie_emu_gtk ] && GTK_EMU=$B/blazie_emu_gtk
SHIPS="$LIB $B/sd_ssi263 $B/blazie_emu $B/blazie_files $B/blazie_bt $B/blazie_emu_bt $GTK_EMU"

check "CPU contract tests (MAME Z180 core)" ./$B/test_z180_contract
check "white-box tests (MAME Z180 core)" ./$B/test_z180_whitebox
check "CPU contract tests (MAME 8085 core)" ./$B/test_i8085_contract
check "CPU contract tests (MAME 8086 core)" ./$B/test_i86_contract
check "CPU contract tests (MAME V40 core)" ./$B/test_v40_contract
check "CPU contract tests (MAME 6809 core)" ./$B/test_m6809_contract
check "CPU contract tests (MAME Z80 core)" ./$B/test_z80_contract
# the Accent SA's board on that core, and its must-fail controls (no firmware: linux_tests.sh's)
check "Accent SA board (MAME Z80): its rules" ./$B/test_az_board
check "Accent SA board CONTROLS (each rule undone, must fail its tests)" python3 src/csrc/accentz80/az_controls.py
check "chip defaults" python3 src/csrc/gen_chip_defaults.py --check
# #37: the emulator apps save a unit's state whole or not at all (tools/check_emu_saves.py); its control reads the
# Windows app's first save as it was
check "emulator apps: every save of a unit's state whole or not at all (#37)" python3 tools/check_emu_saves.py
control "emulator apps: saves CONTROL (the Windows app's first save as it was, must fail)" \
    "^FAIL main_win\.c +saves in place with emu_save\(\) at line [0-9]+$" "^ok +main_linux\.c " \
    "^emulator saves: 1 FAILED$" -- env EMU_SAVES_BREAK=1 python3 tools/check_emu_saves.py
# the Braille Lite's text preparation on runs of 64 digits and more (tools/check_bl_digit_run.py: bl_voice.c built alone
# with AddressSanitizer; amounts() wrote one int past a 64-entry stack array); its control puts the old bound back
check "Braille Lite text preparation: long digit runs under AddressSanitizer" python3 tools/check_bl_digit_run.py
control "Braille Lite digit runs CONTROL (the bound put back, must fail)" \
    "^FAIL  Braille Lite digit runs: AddressSanitizer: stack-buffer-overflow" " in amounts .*bl_voice\.c:4[0-9][0-9]" \
    -- env BL_DIGIT_RUN_BREAK=1 python3 tools/check_bl_digit_run.py
# engine v0.21's F4, and its control (linux_tests.sh's)
check "F4: tuned by the F3 code on its own bank, retuned with F3 (Python, C)" python3 tools/check_f4_law.py
control "F4 CONTROL (v0.20's fixed F4, must fail)" "^FAIL  LAW py R " "^FAIL  FOLLOWS C through PA" \
    "^ok    PARITY: Python = C" "^F4 law: 14 FAILED$" -- env F4_LAW_BREAK=fixed python3 tools/check_f4_law.py
# accented letters (src/csrc/translit.h): the module and every voice's bytes need no firmware (linux_tests.sh's)
check "accented letters: translit.h (C), the table, alone and in a word, case" python3 nvda/tools/translit_test.py module
control "accented letters: translit.h CONTROL (the pass off, must fail)" \
    '^FAIL lone: "\\xc3\\xa1" -> "\\xc3\\xa1", not "a acute"$' '^ok   ascii: all 128 characters pass unchanged' \
    '^translit: [0-9]+ of 91 FAILED \(TRANSLIT_BREAK=1: the pass is off\)$' \
    -- env TRANSLIT_BREAK=1 python3 nvda/tools/translit_test.py module
control "accented letters: translit.h CONTROL (ligatures and zero-width as before, must fail)" \
    '^FAIL ligatures: ' '^FAIL zero width: ' '^translit: 8 of 91 FAILED$' \
    -- env TRANSLIT_BREAK=2 python3 nvda/tools/translit_test.py module
check "accented letters: the bytes every voice sends" python3 nvda/tools/translit_test.py bytes
# the SmarTalk as a voice (tools/check_st_voice.py; S_TALK.EXE is in the repository, so it always runs), through the
# voice table and the NVDA binding on libssi263_voices.so (build_linux.sh: the speech-dispatcher module's own objects
# as a library, for the tests, never shipped); its controls, as nvda/tools/run_tests.py's
check "SmarTalk voice: settings, cancel, parts, text (voice API and binding)" python3 tools/check_st_voice.py \
    $B/libssi263_voices.so
control "SmarTalk voice CONTROL (the tone pinned to A, must fail)" "^FAIL  tone: A lower, Z higher than I " \
    "^ok    volume: the gain only " "^st voice: 1 FAILED$" -- \
    env ST_VOICE_BREAK=1 python3 tools/check_st_voice.py $B/libssi263_voices.so
control "SmarTalk voice CONTROL (the rate's old mapping, 50 = ESC >5, must fail)" \
    "^FAIL  rate back at 50: a fresh unit's speed " "^ok    rate: 0 slower, 100 faster " "^st voice: 1 FAILED$" -- \
    env ST_RATE_BREAK=1 python3 tools/check_st_voice.py $B/libssi263_voices.so
control "SmarTalk voice CONTROL (the bank's last slot kept on a boot change, must fail)" \
    "^FAIL  a bank's unit made again on a boot change " "^ok    speaks through the table " "^st voice: 1 FAILED$" -- \
    env SSV_BANK_BREAK=1 python3 tools/check_st_voice.py $B/libssi263_voices.so
control "SmarTalk voice CONTROL (the contractions as they are, must fail)" "^FAIL  said as: don't +\"don't\"$" \
    "^FAIL  said as: I'm " "^FAIL  don't: said as one word, no letter T " "^ok    said as: rock 'n roll " \
    "^st voice: 6 FAILED$" -- env ST_TEXT_BREAK=1 python3 tools/check_st_voice.py $B/libssi263_voices.so
# index marks (src/csrc/marks.h, docs/index-marks.md), what needs no firmware of them: the bytes the Braille Lite voice
# would send its unit for a marked text, and the voice table's marks on the voices whose files are in the repository
# (the Accents, the SmarTalk: estimated); controls as linux_tests.sh's
check "bl_voice: index marks, the bytes for the unit" python3 nvda/tools/voice_marks.py
control "bl_voice marks CONTROL (a Ctrl-F for every mark on one word, must fail)" \
    "^FAIL same +three marks on one word: ..3 Ctrl-F before 'two'" "^voice marks: 2 FAILED$" \
    -- env VOICE_MARKS_BREAK=2 python3 nvda/tools/voice_marks.py
# ... and the Accent-PC's (as_voice.h): Ctrl-F at the front of each marked word, the unit's prompts on its serial
# line, the marks following the audio the voice holds back (its ROMs are in the repository: it always runs)
check "as_voice: index marks by the unit's own Ctrl-F" python3 nvda/tools/as_marks.py $B/libssi263_voices.so
control "as_voice marks CONTROL (every mark with the first block, must fail)" "^FAIL place " "^FAIL twins " \
    "^FAIL end " "^FAIL dense " "^FAIL cancel " "^ok +frames " "^as marks: 5 FAILED$" \
    -- env AS_MARKS_BREAK=1 python3 nvda/tools/as_marks.py $B/libssi263_voices.so
control "as_voice marks CONTROL (a Ctrl-F for every mark on a word, must fail)" \
    "^FAIL same +three marks on one word: ..3 Ctrl-F before 'two'" "^as marks: 1 FAILED$" \
    -- env AS_MARKS_BREAK=2 python3 nvda/tools/as_marks.py $B/libssi263_voices.so
control "as_voice marks CONTROL (the marks left after a cancel, must fail)" "^FAIL cancel .* [1-9] reported after it" \
    "^as marks: 1 FAILED$" -- env AS_MARKS_BREAK=3 python3 nvda/tools/as_marks.py $B/libssi263_voices.so
control "as_voice marks CONTROL (the Ctrl-F inside its word, must fail)" "^FAIL frames .*phonemes DIFFER" \
    "^FAIL bytes " "^as marks: 5 FAILED$" -- env AS_MARKS_BREAK=4 python3 nvda/tools/as_marks.py $B/libssi263_voices.so
# the Accent-mini speaks a long text item whole (#67: cut at 30 s as a stall); its control counts from the call's start
check "am_voice: a 3,000-character item spoken whole (#67)" python3 nvda/tools/am_long_text.py $B/libssi263_voices.so
control "am_voice long text CONTROL (30 s from the call's start, must fail)" "^FAIL whole +default: 3000 characters, 29" \
    "^accent-mini long text: 2 FAILED$" -- env AM_LONG_TEXT_BREAK=1 python3 nvda/tools/am_long_text.py $B/libssi263_voices.so
# a cancel while the driver still takes a long text stops at once (the stack slot), at a changed pitch too (#74)
check "am_voice: a cancel during a long text stops at once, at a changed pitch too (#74)" python3 nvda/tools/am_cancel_capital.py $B/libssi263_voices.so
control "am_voice cancel CONTROL (the count slot in the driver's segment, must fail)" "^FAIL plain " "^FAIL capital " \
    "^accent-mini cancel: 2 FAILED$" -- env AM_CANCEL_CAPITAL_BREAK=ss python3 nvda/tools/am_cancel_capital.py $B/libssi263_voices.so
control "am_voice cancel CONTROL (the pitch restored before the cancel, must fail)" "^ok   plain " "^FAIL capital " \
    "^accent-mini cancel: 1 FAILED$" -- env AM_CANCEL_CAPITAL_BREAK=restore python3 nvda/tools/am_cancel_capital.py $B/libssi263_voices.so
# ... and the SmarTalk's (st_voice.h): Ctrl-F after the word before each marked word, the parts cut where they are
# without marks, the TSR's own count of marks passed (S_TALK.EXE is in the repository: it always runs)
check "st_voice: index marks by the TSR's own Ctrl-F" python3 nvda/tools/st_marks.py $B/libssi263_voices.so
control "st_voice marks CONTROL (every mark with the first block, must fail)" "^FAIL place " "^FAIL twins " \
    "^FAIL end " "^FAIL dense " "^FAIL cancel " "^ok +frames " "^st marks: 5 FAILED$" \
    -- env ST_MARKS_BREAK=1 python3 nvda/tools/st_marks.py $B/libssi263_voices.so
control "st_voice marks CONTROL (a Ctrl-F for every mark on a word, must fail)" \
    "^FAIL same +three marks on one word: ..3 Ctrl-F before 'two'" "^st marks: 1 FAILED$" \
    -- env ST_MARKS_BREAK=2 python3 nvda/tools/st_marks.py $B/libssi263_voices.so
control "st_voice marks CONTROL (the marks left after a cancel, must fail)" "^FAIL cancel .* [1-9] reported after it" \
    "^st marks: 1 FAILED$" -- env ST_MARKS_BREAK=3 python3 nvda/tools/st_marks.py $B/libssi263_voices.so
control "st_voice marks CONTROL (the Ctrl-F inside the marked word, must fail)" "^FAIL frames .*phonemes DIFFER" \
    "^FAIL bytes " "^st marks: 4 FAILED$" -- env ST_MARKS_BREAK=4 python3 nvda/tools/st_marks.py $B/libssi263_voices.so
# ... and the Accent-mini's (am_voice.h): Ctrl-F after the word before each marked word, the driver's own count of
# marks passed read from its memory (SPKEMS.DVC is in the repository: it always runs)
check "am_voice: index marks by the driver's own Ctrl-F" python3 nvda/tools/am_marks.py $B/libssi263_voices.so
control "am_voice marks CONTROL (every mark with the first block, must fail)" "^FAIL place " "^FAIL clause " \
    "^FAIL job " "^FAIL twins " "^FAIL end " "^FAIL dense " "^FAIL cancel " "^ok +frames " "^am marks: 8 FAILED$" \
    -- env AM_MARKS_BREAK=1 python3 nvda/tools/am_marks.py $B/libssi263_voices.so
control "am_voice marks CONTROL (a Ctrl-F for every mark on a word, must fail)" \
    "^FAIL same +three marks on one word: ..3 Ctrl-F before 'two'" "^FAIL dash .*2 Ctrl-F after 'No'" \
    "^am marks: 2 FAILED$" -- env AM_MARKS_BREAK=2 python3 nvda/tools/am_marks.py $B/libssi263_voices.so
control "am_voice marks CONTROL (the marks left after a cancel, must fail)" "^FAIL cancel .* [1-9] reported after it" \
    "^am marks: 1 FAILED$" -- env AM_MARKS_BREAK=3 python3 nvda/tools/am_marks.py $B/libssi263_voices.so
control "am_voice marks CONTROL (the Ctrl-F inside the word before, must fail)" "^FAIL frames .*phonemes DIFFER" \
    "^FAIL bytes " "^am marks: 8 FAILED$" -- env AM_MARKS_BREAK=4 python3 nvda/tools/am_marks.py $B/libssi263_voices.so
check "voice table: index marks, on the voices in the repository" python3 tools/check_ssv_marks.py \
    $B/libssi263_voices.so --firmware "$ROOT/firmware"
check "voice table: index marks, estimated, on the voices in the repository" env SSV_MARKS_ESTIMATE=1 \
    python3 tools/check_ssv_marks.py $B/libssi263_voices.so --firmware "$ROOT/firmware"
control "voice table marks CONTROL (the estimate reports every mark at once, must fail)" \
    "^FAIL accentmini:mini +place " "^FAIL smartalk:smartalk +place " "^ssv marks: [0-9]+ FAILED$" \
    -- env SSV_MARKS_ESTIMATE=1 SSV_MARKS_BREAK=1 python3 tools/check_ssv_marks.py $B/libssi263_voices.so --firmware "$ROOT/firmware"
control "voice table marks CONTROL (the audio rendered ahead returned from its start, must fail)" \
    "^FAIL accentmini:mini +audio " "^FAIL smartalk:smartalk +audio " "^ssv marks: [0-9]+ FAILED$" \
    -- env SSV_MARKS_ESTIMATE=1 SSV_MARKS_BREAK=queue python3 tools/check_ssv_marks.py $B/libssi263_voices.so --firmware "$ROOT/firmware"
control "voice table marks CONTROL (the unit's own mark inside a word, must fail)" \n    "^FAIL accentmini:mini +audio " "^FAIL smartalk:smartalk +audio " "^ok +accentmini:sa +once " \n    "^ssv marks: [0-9]+ FAILED$" \n    -- env SSV_MARKS_SAME_BREAK=1 python3 tools/check_ssv_marks.py $B/libssi263_voices.so --firmware "$ROOT/firmware"
control "accented letters: bytes CONTROL (the pass off, must fail)" '^FAIL blazie +bytes ' '^FAIL speakout +bytes ' \
    "^ +'t\\\\xfck\\\\xf6r' -> b't k r" '^translit bytes: 5 of 10 FAILED' \
    -- env TRANSLIT_BREAK=1 python3 nvda/tools/translit_test.py bytes
check "emulator: the keyboard (terminal, chords, letters, hold, Type 'n Speak)" ./$B/test_keys build
check "emulator: braille display wiring and latch" ./$B/test_display
control "emulator: keyboard CONTROL (dots 1 and 4 swapped, must fail)" "^FAIL +keys mode: o-chord, then t" \
    "^FAIL +letters mode: computer braille" "^ok +tns: y " "^ok +terminal: F12" "^test_keys: [0-9]+ of [0-9]+ FAILED$" \
    -- env BLAZIE_KEYS_BREAK=1 ./$B/test_keys build
# the BT Speak's and BT Braille's keyboard server and display layout (test_btkb), and the BT Speak .deb's maintainer
# scripts on a scratch root with a fake release of the test's own (no network); a commit from before #5 has neither
if [ -f src/apps/blazie/test_btkb.c ]; then
    check "emulator: the BT keyboard server and the display layout" ./$B/test_btkb
    control "emulator: BT keyboard CONTROL (dots 7 and 8 never wait, must fail)" \
        "^FAIL gesture: dot 7 first, then M-chord" "^ok +gesture: M-chord's keys first" "^ok +server: every key consumed" \
        "^FAIL: 1 failure$" -- env BTKB_BREAK=1 ./$B/test_btkb
    check "BT Speak .deb: install and removal (scratch root, User Menus, firmware fetch)" sh tools/btspeak_deb_test.sh
    control "BT Speak .deb CONTROL (postrm never runs, must fail)" "^FAIL +removal: the menus exactly as they were" \
        "^ok +install: the Blazie menu's line in each BT Speak home" "^btspeak deb: FAILED$" \
        -- env BTSPEAK_DEB_BREAK=1 sh tools/btspeak_deb_test.sh
    control "BT Speak .deb CONTROL (a release's version keeps the source's pin, must fail)" \
        "^FAIL +a release's .deb: its fetch-firmware pins that release's own emulator archive" \
        "^FAIL +a release's version without its emulator archive in build/ is refused" \
        "^ok +removal: the menus exactly as they were" "^btspeak deb: FAILED$" \
        -- env BTSPEAK_DEB_BREAK=pin sh tools/btspeak_deb_test.sh
    control "BT Speak installer CONTROL (a plain copy onto a running blazie_emu, must fail)" \
        "^FAIL +install.sh over a running blazie_emu" "^ok +install.sh: its Blazie menu, an item per unit" \
        "^ok +removal: the menus exactly as they were" "^btspeak deb: FAILED$" \
        -- env BTSPEAK_INSTALL_BREAK=copy sh tools/btspeak_deb_test.sh
fi
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
