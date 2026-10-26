#!/bin/sh
# The Linux gate (x86_64 or aarch64), run on the Linux box itself -- separate from nvda/tools/run_tests.py, which is
# Windows and NVDA.  Needs a build (./build_linux.sh; LEGACY=1 adds the z180emu reference checks and the audit's
# real-GPL control) and the unit's firmware in a data folder: the built NVDA add-on's engine folder, or any folder
# with BL2ENG.BNS + bl2_2003_warm.state (and BL2SPA.BNS + bl2spa_fresh.state; the emulator's checks also need the
# Type 'n Speak's TNSENG.TNS, in its tns/ folder or beside them).
#
#   tools/linux_tests.sh [data folder]      (default: nvda/dist/blazie-build/synthDrivers/_ssi263_blazie)
#
# Everything here runs the shipping library: the Braille Lite on MAME's Z180, through the NATIVE host (bl_host.c, with
# its default cancel protection, cancel_settle 3: Astra, Replies 124-129).  The goldens (nvda/tools/golden/
# blazie_{en,es}.txt: the native host's, made on Windows with the MAME bl.dll) compare byte for byte: writes and their
# times, serial and the audio hash.  The pipe host (bl_live) has its own cancel path and baseline, not checked here.
# The z180emu goldens (blazie_*_legacy.txt, made before the cancel protection) are REFERENCE checks only, labelled so:
# English's spoken values still match them write for write; Spanish's do not, by the two writes the protection adds
# after the 3.9 s cancel (R1=40, R0=C0), so it is not compared; the times legitimately differ.  The module
# harness checks speech-dispatcher's protocol and every message's audio (run ahead too) on every voice it offers (the
# Braille Lite, the Accent SA, and the Accent-mini and Speak-Out when built in) and the Braille Lite's number words,
# and its four controls (the unit never cancelled, SSI263RunAhead ignored, SSI263BrailleLiteNumbers ignored,
# SSI263AccentInflection ignored) must fail; the no-GPL audit
# (tools/check_no_gpl.py) searches the library, the module, the package and the wheel, and its controls must fail.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA="$(cd "${1:-$ROOT/nvda/dist/blazie-build/synthDrivers/_ssi263_blazie}" && pwd)"
PLAT="$(python3 -c 'import sys, platform; print("%s-%s" % (sys.platform, platform.machine()))')"
LIB="$ROOT/src/ssi263/_bin/$PLAT/libssi263speech.so"
LEGACY="$ROOT/build/linux/legacy"
export SSI263_LIB="$LIB" PYTHONDONTWRITEBYTECODE=1 PYTHON_COLORS=0
cd "$ROOT" || exit 1
fail=0
check() {                          # name, then the command
    name="$1"; shift
    out="$("$@" 2>&1)"; got=$?
    if [ $got -eq 0 ]; then echo "ok    $name: $(echo "$out" | tail -1 | cut -c1-90)"
    else echo "FAIL  $name: $(echo "$out" | tail -1 | cut -c1-90)"; fail=1; fi
}
. "$ROOT/tools/linux_control.sh"          # control(): a must-fail control, judged by its marks
[ -f "$DATA/BL2ENG.BNS" ] || { echo "no firmware in $DATA"; exit 1; }
# bns_equiv finds the firmware where the add-on build puts it
if [ "$DATA" != "$ROOT/nvda/dist/blazie-build/synthDrivers/_ssi263_blazie" ]; then
    mkdir -p "$ROOT/nvda/dist/blazie-build/synthDrivers"
    ln -sfn "$DATA" "$ROOT/nvda/dist/blazie-build/synthDrivers/_ssi263_blazie"
fi
check "two units in one process (MAME Z180)" ./build/linux/test_bl_board "$DATA/BL2ENG.BNS" \
    "$DATA/bl2_2003_warm.state" "$DATA/BL2SPA.BNS" "$DATA/bl2spa_fresh.state"
check "CPU contract tests (MAME Z180 core)" ./build/linux/test_z180_contract
check "white-box tests (MAME Z180 core)" ./build/linux/test_z180_whitebox
check "CPU contract tests (MAME 8085 core)" ./build/linux/test_i8085_contract
check "CPU contract tests (MAME 8086 core)" ./build/linux/test_i86_contract
check "golden, native host (en)" python3 nvda/tools/bns_equiv.py --native "$LIB" \
    --against=nvda/tools/golden/blazie_en.txt
[ -f "$DATA/BL2SPA.BNS" ] && check "golden, native host (es)" python3 nvda/tools/bns_equiv.py --native --es "$LIB" \
    --against=nvda/tools/golden/blazie_es.txt
# the full comparison sees a different core: the shipping library against z180emu's golden must fail
control "golden CONTROL (the z180emu golden, must fail)" "^DIFFERS from blazie_en_legacy\.txt at line [0-9]+" \
    -- python3 nvda/tools/bns_equiv.py --native "$LIB" --against=nvda/tools/golden/blazie_en_legacy.txt
# REFERENCE (z180emu, not shipped): what English speaks, write for write, is z180emu's; only the times moved.  (Not
# Spanish: the cancel protection adds two writes after its 3.9 s cancel, R1=40 and R0=C0 -- Astra, Reply 126.)
check "reference: spoken values = z180emu's (en)" python3 nvda/tools/bns_equiv.py --native --values-only "$LIB" \
    --against=nvda/tools/golden/blazie_en_legacy.txt
control "reference CONTROL (one value flipped, must fail)" \
    "^write values DIFFER from blazie_en_legacy\.txt at write [0-9]+ of" \
    -- env BNS_EQUIV_FLIP=1 python3 nvda/tools/bns_equiv.py --native --values-only "$LIB" \
    --against=nvda/tools/golden/blazie_en_legacy.txt
if [ -x "$LEGACY/test_z180_legacy" ]; then
    check "reference: z180emu legacy path exceptions (development build)" "$LEGACY/test_z180_legacy"
else
    echo "skip  reference: the z180emu development build (LEGACY=1 ./build_linux.sh)"
fi
check "chip defaults" python3 src/csrc/gen_chip_defaults.py --check
# the Braille Lite release table: the header current; the library's list the text file's, each release whose firmware
# is here making its listed state by its row's recipe -- the same bytes as on Windows; the control answers n
check "release table" python3 src/csrc/blazie/gen_languages.py --check
check "releases: each one's state by its row's recipe" python3 src/csrc/blazie/test_releases.py --lib "$LIB" \
    --firmware "$DATA"
control "releases CONTROL (every setup answered n, must fail)" "^releases: [0-9]+ FAILED$" \
    -- env SSI263_RELEASES_TEST_BREAK=1 python3 src/csrc/blazie/test_releases.py --lib "$LIB" --firmware "$DATA"
check "release voices: each new release speaks through the voice" python3 src/csrc/blazie/test_release_voices.py \
    --lib "$LIB" --firmware "$DATA"
control "release voices CONTROL (the fast boot keys everywhere, must fail)" "^FAIL bl2_cs_1999_07: speaks: 0\.0 s" \
    "^release voices: [0-9]+ FAILED$" \
    -- env SSI263_RELEASE_VOICES_BREAK=fast python3 src/csrc/blazie/test_release_voices.py --lib "$LIB" --firmware "$DATA"
control "release voices CONTROL (the echo ignored, must fail)" "^FAIL bl2_hu_1999_07: speaks: .* done at [4-9]" \
    "^release voices: [0-9]+ FAILED$" \
    -- env SSI263_RELEASE_VOICES_BREAK=echo python3 src/csrc/blazie/test_release_voices.py --lib "$LIB" --firmware "$DATA"
# 0.8.5: a non-echoing unit is done when its firmware is (bl_host.c no_echo_finished); left out, a second later again
control "release voices CONTROL (a non-echoing unit's own end left out, must fail)" \
    "^FAIL bl2_hu_1999_07: speaks: .* (0\.9|1\.[0-9])[0-9] s after its last sound" "^release voices: [0-9]+ FAILED$" \
    -- env SSI263_RELEASE_VOICES_BREAK=slow python3 src/csrc/blazie/test_release_voices.py --lib "$LIB" --firmware "$DATA"
# ... and never early: past each first done the same unit runs on 1.5 s and must not speak again (Astra, Reply 248)
check "no-echo end: every done final (the units that never echo)" python3 src/csrc/blazie/test_no_echo_end.py \
    --lib "$LIB" --firmware "$DATA"
control "no-echo end CONTROL (done taken early, must fail)" \
    "^FAIL [^ ]+, 1 .*: done at 0\.80 s, then [0-9]+ spoken phonemes" "^no-echo end: 1 FAILED$" \
    -- env SSI263_NO_ECHO_END_BREAK=early python3 src/csrc/blazie/test_no_echo_end.py --lib "$LIB" --firmware "$DATA"
control "releases CONTROL (a release with no voice handed out as English, must fail)" "^releases: 1 FAILED$" \
    -- env SSI263_RELEASES_TEST_BREAK=voice python3 src/csrc/blazie/test_releases.py --lib "$LIB" --firmware "$DATA"
# double speed (issue #15): the firmware's own switch runs the CPU twice as fast when allowed; the control never allows it
check "double speed: the firmware's switch runs the CPU twice as fast" python3 src/csrc/blazie/test_double_speed.py \
    --lib "$LIB" --firmware "$DATA"
control "double speed CONTROL (never allowed, must fail)" "^FAIL faster " "^double speed: [0-9]+ FAILED$" \
    -- env SSI263_DOUBLE_SPEED_BREAK=1 python3 src/csrc/blazie/test_double_speed.py --lib "$LIB" --firmware "$DATA"
# the SSI-263's latch race (bl_board.h BL_LATCH_RACE_US): the speech untouched at double speed; BASIC's tones caught
# there when the data folder's games/ has Blazie's run.bns (and its control then: the latch too short, must fail)
check "latch race: BASIC's tones at double speed, speech untouched" python3 src/csrc/blazie/test_latch_race.py \
    --lib "$LIB" --firmware "$DATA" --files build/linux/blazie_files
[ -f "$DATA/games/run.bns" ] && control "latch race CONTROL (the latch below the closest writes, must fail)" \
    "^ok +speech " "^FAIL tones " "^latch race: 1 FAILED$" -- env SSI263_LATCH_RACE_BREAK=1 \
    python3 src/csrc/blazie/test_latch_race.py --lib "$LIB" --firmware "$DATA" --files build/linux/blazie_files
# the edges every 1.90 us, the candidate Tomi heard tear (2026-10-24): the tones check must fail
[ -f "$DATA/games/run.bns" ] && control "latch race CONTROL (1.90 us edges, the tearing, must fail)" \
    "^ok +params +Braille Lite 2000 " "^FAIL tones " "^latch race: 1 FAILED$" -- env SSI263_LATCH_RACE_BREAK=edges190 \
    python3 src/csrc/blazie/test_latch_race.py --lib "$LIB" --firmware "$DATA" --files build/linux/blazie_files
# and every gap under 2.15 us caught, as before issue #25: a speech-parameter answer left holding a vowel, must fail
[ -f "$DATA/BL2ENG.BNS" ] && control "latch race CONTROL (gaps up to 2.15 us caught, a held vowel, must fail)" \
    "^ok +speech " "^FAIL params +Braille Lite 2000 " "^latch race: [0-9]+ FAILED$" -- env SSI263_LATCH_RACE_BREAK=wide \
    python3 src/csrc/blazie/test_latch_race.py --lib "$LIB" --firmware "$DATA" --files build/linux/blazie_files
# the units' own accented letters (bl_voice.c's pages and folds: Hungarian, Slovak, Czech, Polish, Croatian, French,
# Danish, Norwegian, Swedish ... and the Arabic (UAE) unit's code page 864 letters and words): each one's byte and the
# unit's phonemes for it; the control sends what the voices did before
check "unit letters: each unit says its accented letters" python3 src/csrc/blazie/test_unit_letters.py \
    --lib "$LIB" --firmware "$DATA"
control "unit letters CONTROL (the encodings before, must fail)" "^FAIL letter +bns2_sk U\+00F4: S PA UH3 PA S PA" \
    "^FAIL word +bl2_ar_ae_1998_12 " "^unit letters: [0-9]+ FAILED$" \
    -- env SSI263_LETTERS_BREAK=1 python3 src/csrc/blazie/test_unit_letters.py --lib "$LIB" --firmware "$DATA"
# the trillion guard (bl_numbers.c bl_trillions, for the units that fold a trillion into billions): its text and the
# Hungarian unit's billion word said once at most; voices.c's voice cases are Windows' (libssi263speech.so has no
# voice table: it is built into sd_ssi263 alone), so they are skipped here and said so; the control leaves every
# number as it is
check "trillions: a folding unit's long numbers a digit at a time" python3 src/csrc/blazie/test_trillions.py \
    --lib "$LIB" --firmware "$DATA"
control "trillions CONTROL (every number left as it is, must fail)" "^FAIL text +'1001000000000' -> '1001000000000'" \
    "^FAIL unit +the Hungarian unit's billion word .* said 2 times" "^skip voice +this library has no voice table" \
    "^trillions: 7 FAILED$" \
    -- env SSI263_TRILLIONS_BREAK=1 python3 src/csrc/blazie/test_trillions.py --lib "$LIB" --firmware "$DATA"
# issue #8: the Accent SA's text done only when said (as_voice's settle between clauses), on the module's own engine
# objects (libsd_voices_ref.so: libssi263speech.so has no Accent SA); its control is the settle off (0.7.5's done)
# and must name the dialog's early texts
VOICES_REF="$ROOT/build/linux/libsd_voices_ref.so"
check "Accent SA: a text done only when said (issue #8)" python3 src/csrc/accentsa/test_as_complete.py "$VOICES_REF"
control "Accent SA completion CONTROL (the settle off, must fail)" "^EARLY  rate +60 job +'Pitch: slider 50 alt\+p'" \
    "^EARLY  rate +75 say +'Rate: slider 75 alt\+r'" \
    "^checks: done ok, complete [0-9]+ FAILED, audio ok, latency ok, phase [0-9]+ FAILED, replace ok, limit ok$" \
    "^accent sa completion: FAILED$" -- env AS_COMPLETE_BREAK=1 python3 src/csrc/accentsa/test_as_complete.py "$VOICES_REF"
control "Accent SA completion CONTROL (never done, must fail)" \
    "^NOT DONE after 3 render calls  rate +50 job +'Rate: slider 50 alt\+r'" \
    "^checks: done [0-9]+ FAILED, complete ok, audio ok, latency ok, phase ok, replace ok, limit ok$" \
    "^accent sa completion: FAILED$" -- env AS_COMPLETE_BREAK=never python3 src/csrc/accentsa/test_as_complete.py "$VOICES_REF"
control "Accent SA completion CONTROL (a wait kept across a cancel, must fail)" \
    "^HELD   rate +60 job +'Pitch: slider 50 alt\+p', cancel while held" \
    "^checks: done ok, complete ok, audio ok, latency ok, phase ok, replace [0-9]+ FAILED, limit ok$" \
    "^accent sa completion: FAILED$" -- env AS_COMPLETE_BREAK=held python3 src/csrc/accentsa/test_as_complete.py "$VOICES_REF"
control "Accent SA completion CONTROL (the cap as a plain done, must fail)" \
    "^SILENT LIMIT  rate +60 job +'Pitch: slider 50 alt\+p': done with [0-9]+ of its [0-9]+ samples, and no limit" \
    "^RECOVERY  rate +60 job +'Pitch: slider 50 alt\+p': the next text after the limit had [0-9]+ phonemes" \
    "^checks: done ok, complete ok, audio ok, latency ok, phase ok, replace ok, limit [0-9]+ FAILED$" \
    "^accent sa completion: FAILED$" -- env AS_COMPLETE_BREAK=limit python3 src/csrc/accentsa/test_as_complete.py "$VOICES_REF"
# the module, once, keeping its total: how many checks it makes depends on the voices whose files the data folder has
# under the module's own names (the German, Italian and Slovak units among them), so the controls below expect that
# total less the checks each one breaks, not a number fixed for one machine's folder
mod_out="$(python3 src/platforms/speechd/test_sd_ssi263.py build/linux/sd_ssi263 "$LIB" "$DATA" 2>&1)"; mod_rc=$?
if [ $mod_rc -eq 0 ]; then echo "ok    speech-dispatcher module: $(echo "$mod_out" | tail -1 | cut -c1-90)"
else echo "FAIL  speech-dispatcher module: $(echo "$mod_out" | tail -1 | cut -c1-90)"; fail=1; fi
N="$(echo "$mod_out" | sed -n 's/^\([0-9][0-9]*\) of \1 checks passed.*/\1/p' | head -n 1)"
[ -n "$N" ] || { echo "FAIL  speech-dispatcher module: its total not found (the controls need it)"; fail=1; N=0; }
# the speech-dispatcher installer on scratch folders (Garrett, issue #10): a stock speechd.conf left alone (an
# AddModule line turns autodiscovery off), an earlier install's line removed, a listed config listed; its control
# is 0.7.6's always-added line
check "speech-dispatcher installer (speechd.conf)" sh tools/speechd_install_test.sh
control "speech-dispatcher installer CONTROL (0.7.6's AddModule line, must fail)" \
    "^FAIL stock config unchanged " "^FAIL earlier install's line removed " "^ok +listed config: this module listed too " \
    -- env SSI263_INSTALL_BREAK=1 sh tools/speechd_install_test.sh
control "module CONTROL (no cancel, must fail)" "^speak +module .*identical" "^stop +module .*identical" \
    "^after +module .*DIFFER" "^set +module .*DIFFER" "^key +module .*DIFFER" "^spanish +module .*identical" \
    "^ra_stop +module .*identical" "^ra_after +module .*DIFFER" \
    "^as_stop +module .*identical" "^as_after +module .*DIFFER" "^as_bl +module .*identical" \
    "^num_off +module .*identical" "^$((N - 8)) of $N checks passed" -- env SD_SSI263_TEST_NO_CANCEL=1 \
    python3 src/platforms/speechd/test_sd_ssi263.py build/linux/sd_ssi263 "$LIB" "$DATA"
# the run-ahead key (SSI263RunAhead, EXPERIMENTAL) dropped on its way to the voice: the module speaks the lockstep, so
# every run-ahead check against the run-ahead reference fails, and this user's 0 over the module file's 1 still passes
control "module CONTROL (SSI263RunAhead ignored, must fail)" "^speak +module .*identical" \
    "^ra_speak +module .*DIFFER" "^ra_stop +module .*DIFFER" "^ra_after +module .*DIFFER" "^ra_sys +module .*DIFFER" \
    "^ra_user0 +module .*identical" "^as_speak +module .*identical" "^$((N - 4)) of $N checks passed" \
    -- env SD_SSI263_TEST_IGNORE_RUN_AHEAD=1 \
    python3 src/platforms/speechd/test_sd_ssi263.py build/linux/sd_ssi263 "$LIB" "$DATA"
# the Braille Lite's number words key (SSI263BrailleLiteNumbers) dropped on its way to the voice: the driver's default
# (on) reaches it, so this user's 0 is not heard -- English and Spanish -- while the default and this user's 1 still pass
control "module CONTROL (SSI263BrailleLiteNumbers ignored, must fail)" "^num_dflt +module .*identical" \
    "^num_es +module .*identical" "^num_user +module .*identical" "^num_off +module .*DIFFER" \
    "^num_es_off +module .*DIFFER" "^$((N - 2)) of $N checks passed" -- env SD_SSI263_TEST_IGNORE_BL_NUMBERS=1 \
    python3 src/platforms/speechd/test_sd_ssi263.py build/linux/sd_ssi263 "$LIB" "$DATA"
# the line-start lift's key (SSI263LineLift) dropped on its way to the voice: only lift fails
control "module CONTROL (SSI263LineLift ignored, must fail)" "^ra_sys +module .*identical" \
    "^lift +module .*DIFFER" "^$((N - 1)) of $N checks passed" -- env SD_SSI263_TEST_IGNORE_LINE_LIFT=1 \
    python3 src/platforms/speechd/test_sd_ssi263.py build/linux/sd_ssi263 "$LIB" "$DATA"
# the Accent's own key (SSI263AccentInflection) dropped on its way to the Accent SA: only as_infl fails
control "module CONTROL (SSI263AccentInflection ignored, must fail)" "^as_speak +module .*identical" \
    "^as_infl +module .*DIFFER" "^$((N - 1)) of $N checks passed" -- env SD_SSI263_TEST_IGNORE_ACCENT_INFLECTION=1 \
    python3 src/platforms/speechd/test_sd_ssi263.py build/linux/sd_ssi263 "$LIB" "$DATA"
# Accented letters (src/csrc/translit.h, first on every voice's text): the module, every voice's bytes, and the voices
# speaking on fresh units, from a firmware folder laid out as firmware/ (the data folder's Braille Lites, the
# repository's Aicom files, the Speak-Out's where test_sd_ssi263.py finds it; all five are required, a voice without
# its files fails).  Each control turns the pass off (TRANSLIT_BREAK=1) or makes no unit ever done
# (TRANSLIT_TEST_NEVER_DONE=1) and must fail as that.  The first two are linux_ci_checks.sh's.
check "accented letters: translit.h (C), the table, alone and in a word, case" python3 nvda/tools/translit_test.py module
control "accented letters: translit.h CONTROL (the pass off, must fail)" \
    '^FAIL lone: "\\xc3\\xa1" -> "\\xc3\\xa1", not "a acute"$' '^ok   ascii: all 128 characters pass unchanged' \
    '^translit: [0-9]+ of 83 FAILED \(TRANSLIT_BREAK=1: the pass is off\)$' \
    -- env TRANSLIT_BREAK=1 python3 nvda/tools/translit_test.py module
check "accented letters: the bytes every voice sends" python3 nvda/tools/translit_test.py bytes
control "accented letters: bytes CONTROL (the pass off, must fail)" '^FAIL blazie +bytes ' '^FAIL speakout +bytes ' \
    "^ +'t\\\\xfck\\\\xf6r' -> b't k r" '^translit bytes: 5 of 10 FAILED' \
    -- env TRANSLIT_BREAK=1 python3 nvda/tools/translit_test.py bytes
TFW=build/translit-firmware
rm -rf $TFW && mkdir -p $TFW/blazie/spanish $TFW/gw-micro-speakout $TFW/aicom-accent-mini $TFW/aicom-accent-sa
ln -sf "$DATA/BL2ENG.BNS" "$DATA/bl2_2003_warm.state" $TFW/blazie/
[ -f "$DATA/BL2SPA.BNS" ] && ln -sf "$DATA/BL2SPA.BNS" "$DATA/bl2spa_fresh.state" $TFW/blazie/spanish/
for so in "$DATA/gw-micro-speakout" "$ROOT/firmware/gw-micro-speakout" "$DATA/../speakout-firmware"; do
    [ -f "$so/SPEAKOUT.HEX" ] && { ln -sf "$so/SPEAKOUT.HEX" $TFW/gw-micro-speakout/; break; }
done
ln -sf "$ROOT/firmware/aicom-accent-mini/SPKEMS.DVC" $TFW/aicom-accent-mini/
ln -sf "$ROOT/firmware/aicom-accent-sa/u2.BIN" "$ROOT/firmware/aicom-accent-sa/u3.BIN" \
    "$ROOT/firmware/aicom-accent-sa/u4.BIN" $TFW/aicom-accent-sa/
check "accented letters: the voices speak them" python3 nvda/tools/translit_test.py voices --firmware $TFW
control "accented letters: voices CONTROL (the pass off, must fail)" \
    "^FAIL blazie +audio +'\\\\xe1' alone: 0\.00 s, 0 loud samples$" "^FAIL blazie +audio +'t\\\\xfck\\\\xf6r' is not" \
    '^ok   blazie +ascii +8 of 8 ASCII texts give the same PCM' '^translit voices: [0-9]+ of [0-9]+ FAILED' \
    -- env TRANSLIT_BREAK=1 python3 nvda/tools/translit_test.py voices --firmware $TFW
control "accented letters: voices CONTROL (never done, must fail)" \
    "^FAIL blazie +audio +'\\\\xe1': never done after 1000 blocks" "^FAIL sa +audio +'\\\\xe1': never done after 1000 blocks" \
    '^translit voices: 5 of 5 FAILED \(TRANSLIT_TEST_NEVER_DONE=1: no unit ever done\)$' \
    -- env TRANSLIT_TEST_NEVER_DONE=1 python3 nvda/tools/translit_test.py voices --firmware $TFW
# The Blazie emulator in a terminal (src/apps/blazie/README-linux.md), on MAME's Z180: its keyboard without a unit
# (test_keys), the unit headless as on Windows (test_emu_unit, test_clock), and the whole program headless
# (test_emu_linux.py: boot and a chord answered, the clock from the system time typed as keys and as computer-braille
# letters, the Type 'n Speak from cold, its memory saved and started from, a chord held through a restart).  The
# Type 'n Speak's firmware: $DATA/tns/TNSENG.TNS or $DATA/TNSENG.TNS.  The controls swap dots 1 and 4
# (BLAZIE_KEYS_BREAK) and drop the held keys (TEST_CLOCK_HOLD_BREAK): each must fail its own checks.
EMU=build/linux/blazie_emu
TNS="$DATA/tns/TNSENG.TNS"; [ -f "$TNS" ] || TNS="$DATA/TNSENG.TNS"
check "emulator: the keyboard (terminal, chords, letters, hold, Type 'n Speak)" ./build/linux/test_keys build
check "emulator: braille display wiring and latch" ./build/linux/test_display
# the BT frontend runs on the BT Speak's own Python (3.11+); an older build host (Debian 11 has 3.9) can't run its
# tests -- named as a skip, never a pass
if python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)' 2>/dev/null; then
    check "emulator: BT frontend, launcher and native worker" env BLAZIE_TEST_FIRMWARE="$DATA" \
        python3 -m unittest discover -s src/platforms/btspeak -p 'test_*.py'
    # Controls mutate serve() only inside the mocked test fixture, never in the shipping helper.
    control "emulator: legacy BT recovery CONTROL (marker after stop, must fail)" \
        "^FAIL test_restore_marker_precedes_first_stop:" "^ok test_owner_exit_returns_and_closes_resources$" \
        "^legacy serve: 1 of 2 FAILED$" -- env BLAZIE_BT_LEGACY_BREAK=late-marker \
        python3 src/platforms/btspeak/test_legacy_bridge.py --control
    control "emulator: legacy BT recovery CONTROL (owner death ignored, must fail)" \
        "^ok test_restore_marker_precedes_first_stop$" "^FAIL test_owner_exit_returns_and_closes_resources:" \
        "^legacy serve: 1 of 2 FAILED$" -- env BLAZIE_BT_LEGACY_BREAK=ignore-owner \
        python3 src/platforms/btspeak/test_legacy_bridge.py --control
else
    echo "skip  emulator: BT frontend: needs Python 3.11, the BT Speak's (this host has $(python3 -V 2>&1))"
fi
control "emulator: keyboard CONTROL (dots 1 and 4 swapped, must fail)" "^FAIL +keys mode: o-chord, then t" \
    "^FAIL +letters mode: computer braille" "^ok +tns: y " "^ok +terminal: F12" "^test_keys: [0-9]+ of [0-9]+ FAILED$" \
    -- env BLAZIE_KEYS_BREAK=1 ./build/linux/test_keys build
check "emulator: the unit headless (Braille Lite)" ./build/linux/test_emu_unit bl "$DATA/BL2ENG.BNS" \
    "$DATA/bl2_2003_warm.state"
check "emulator: the unit headless (Type 'n Speak)" ./build/linux/test_emu_unit tns "$TNS" -
check "emulator: the clock controller" ./build/linux/test_clock unit
check "emulator: the clock and keys held at a restart (Braille Lite)" ./build/linux/test_clock bl "$DATA/BL2ENG.BNS" \
    "$DATA/bl2_2003_warm.state"
check "emulator: the clock (Type 'n Speak)" ./build/linux/test_clock tns "$TNS"
control "emulator: clock year CONTROL (0.7.5's mapping, must fail)" "^FAIL the weekday after New Year +2016-02-29" \
    "^FAIL a unit saved on the old year " "^ok +a new year " -- env TEST_CLOCK_BREAK=4 ./build/linux/test_clock unit
control "emulator: clock year CONTROL, Braille Lite (0.7.5's mapping, must fail)" \
    "^FAIL switched off on New Year's Eve 2026 " "^FAIL saved by 0.7.5 in September 2026 " \
    -- env TEST_CLOCK_BREAK=4 ./build/linux/test_clock bl "$DATA/BL2ENG.BNS" "$DATA/bl2_2003_warm.state" year
control "emulator: clock alarm CONTROL (a dated alarm left on the old year, must fail)" \
    "^FAIL a migrated unit's alarm +dated alarm \(Astra's case\): 2015-10-03, silent" "^ok +a unit saved on the old year " \
    -- env TEST_CLOCK_BREAK=5 ./build/linux/test_clock unit
control "emulator: held keys CONTROL (never reported held, must fail)" "^FAIL i-chord held through the restart" \
    "^FAILED$" -- env TEST_CLOCK_HOLD_BREAK=1 ./build/linux/test_clock bl "$DATA/BL2ENG.BNS" \
    "$DATA/bl2_2003_warm.state" restart
# one-handed mode (dot 6 held as the unit is switched on, dot 3 leaves it: every shell's emu_hold_at_start); its
# control lets the keys up at once
control "emulator: one-handed mode CONTROL (keys let up at once, must fail)" \
    "^ok +switched on, no key held: two-handed " "^FAIL switched on with dot 6 held: one-handed " \
    "^FAIL saved one-handed, switched on again: still one-handed " "^FAILED$" \
    -- env TEST_CLOCK_HOLD_BREAK=4 ./build/linux/test_clock bl "$DATA/BL2ENG.BNS" "$DATA/bl2_2003_warm.state" onehand
# the Braille Lite's braille display and its two advance bars (bl_board.h bl_display, bl_bars; test_emu_unit checks
# them in every Braille Lite run above): the Spanish unit too, and the Braille 'n Speak 2000 has no display -- each when
# its firmware is there.  The control sends the bars as port 40h bit 7, where the shells used to (the firmware never
# reads it): the panning checks must fail, the display's must still pass
SPA="$DATA/spanish/BL2SPA.BNS"; SPA_ST="$DATA/spanish/bl2spa_fresh.state"
[ -f "$SPA" ] || { SPA="$DATA/BL2SPA.BNS"; SPA_ST="$DATA/bl2spa_fresh.state"; }
[ -f "$SPA" ] && check "emulator: the display and the advance bars (Spanish Braille Lite)" ./build/linux/test_emu_unit \
    bl "$SPA" "$SPA_ST"
BNS="$DATA/bns2000/BS03ENG.BNS"
[ -f "$BNS" ] && check "emulator: the Braille 'n Speak 2000, no display" ./build/linux/test_emu_unit bl "$BNS" \
    "$DATA/bns2000/bs03eng_fresh.state"
# the Braille 'n Speak 640 (the OKI M6242B clock, bl_rtc6242.c), as on Windows: the chip on its own with its controls,
# then each unit whose firmware and factory state are in $DATA/bns640 headless, and the English one's clock (its
# control: the chip left out of the saved state)
check "emulator: the Braille 'n Speak 640's clock chip" ./build/linux/test_rtc6242
control "emulator: 640 clock chip CONTROL 1 (no leap years, must fail)" "^FAIL leap year " "^rtc6242: FAILED$" \
    -- ./build/linux/test_rtc6242 1
control "emulator: 640 clock chip CONTROL 2 (HOLD's carry dropped, must fail)" "^FAIL HOLD's carry given after " \
    "^rtc6242: FAILED$" -- ./build/linux/test_rtc6242 2
control "emulator: 640 clock chip CONTROL 4 (the time off not counted, must fail)" "^FAIL kept time while off " \
    "^rtc6242: FAILED$" -- ./build/linux/test_rtc6242 4
B640="$DATA/bns640"
unit640() {                          # firmware, state, key-to-sound limit (ms: the Polish rules answer slower)
    [ -f "$B640/$1" ] && [ -f "$B640/$2" ] && check "emulator: the Braille 'n Speak 640 ($1), headless" \
        ./build/linux/test_emu_unit bl "$B640/$1" "$B640/$2" "$3"
}
unit640 june2003/BSPENG.BNS june2003/bspeng_fresh.state 320
unit640 issue15/BSPENG.BNS issue15/bspeng_2000_fresh.state 320
unit640 issue15/bsppla.bns issue15/bsppla_fresh.state 480
if [ -f "$B640/june2003/BSPENG.BNS" ]; then
    check "emulator: the Braille 'n Speak 640's clock" ./build/linux/test_clock bl "$B640/june2003/BSPENG.BNS" \
        "$B640/june2003/bspeng_fresh.state" clock
    check "emulator: the Braille 'n Speak 640's one-handed mode" ./build/linux/test_clock bl \
        "$B640/june2003/BSPENG.BNS" "$B640/june2003/bspeng_fresh.state" onehand
    control "emulator: 640 clock CONTROL (the chip not saved, must fail)" "^ok +the clock goes on " \
        "^FAIL switched off and on +off 120 s" "^FAILED$" -- env TEST_RTC_BREAK=3 ./build/linux/test_clock bl \
        "$B640/june2003/BSPENG.BNS" "$B640/june2003/bspeng_fresh.state" clock
fi
# the Android app's Braille Lite unit screen (src/platforms/android/app/src/main/cpp/ssa_unit.c), built and run here
# as on Windows: keys to chords and Type 'n Speak events, sample for sample; its control (dots 1 and 4 swapped) inside
check "Android: the Braille Lite unit screen's native part (and its control)" \
    python3 src/platforms/android/test/test_unit_native.py --firmware "$DATA"
control "emulator: advance bars CONTROL (port 40h bit 7, must fail)" "^ok +braille display" "^FAIL advance bar" \
    "^FAIL back bar" "^FAILED$" -- env TEST_EMU_BARS_BREAK=1 ./build/linux/test_emu_unit bl "$DATA/BL2ENG.BNS" \
    "$DATA/bl2_2003_warm.state"
# the double-speed upgrade (emu_set_double_speed; the English unit's run above keys its switch): never installed, the
# unit's own switch must change nothing, so the check must fail
control "emulator: double-speed upgrade CONTROL (never installed, must fail)" "^ok +quick key response " \
    "^FAIL double speed +after" "^FAILED$" -- env TEST_EMU_DOUBLE_BREAK=1 ./build/linux/test_emu_unit bl \
    "$DATA/BL2ENG.BNS" "$DATA/bl2_2003_warm.state"
# the BT Speak's and BT Braille's keyboard server (btkb_linux.c) against a server of the test's own, the menu gesture by
# time, the panning keys from BRLTTY's tables, the display's layout (brl_linux.c); the program with it is
# test_emu_linux.py's btspeak.  Its control: dots 7 and 8 never wait for a chord's other keys, so the gesture typed 7
# first must fail
check "emulator: the BT keyboard server and the display layout" ./build/linux/test_btkb
control "emulator: BT keyboard CONTROL (dots 7 and 8 never wait, must fail)" "^FAIL gesture: dot 7 first, then M-chord" \
    "^ok +gesture: M-chord's keys first" "^ok +server: every key consumed" "^FAIL: 1 failure$" \
    -- env BTKB_BREAK=1 ./build/linux/test_btkb
# the BT Speak .deb (src/platforms/btspeak/deb): its maintainer scripts against a scratch root and scratch homes, the
# firmware from a fake release of the test's own; its control never runs postrm, so the menus must not come back
check "BT Speak .deb: install and removal (scratch root, User Menus, firmware fetch)" sh tools/btspeak_deb_test.sh
control "BT Speak .deb CONTROL (postrm never runs, must fail)" "^FAIL +removal: the menus exactly as they were" \
    "^ok +install: the Blazie menu's line in each BT Speak home" "^btspeak deb: FAILED$" -- env BTSPEAK_DEB_BREAK=1 sh tools/btspeak_deb_test.sh
# a release's .deb pins its own release's emulator archive (build_deb.sh); its control keeps the source's pin
control "BT Speak .deb CONTROL (a release's version keeps the source's pin, must fail)" \
    "^FAIL +a release's .deb: its fetch-firmware pins that release's own emulator archive" \
    "^FAIL +a release's version without its emulator archive in build/ is refused" \
    "^ok +removal: the menus exactly as they were" "^btspeak deb: FAILED$" -- env BTSPEAK_DEB_BREAK=pin sh tools/btspeak_deb_test.sh
# the Type 'n Speak's real cold reset (Timothy, Jayson): a unit the previews saved without its file system or folders,
# told apart and set up anew with its files (test_rescue); its control leaves the old cold start on through the rescue
# the sound buffer (audio_pace.c; Tomi: the emulator's speech stutters): the queue against a simulated card, for the
# Windows shell's thread and for this one's (audio_linux.c: the card asked how much is queued and how far it has
# played), and that reading alone; its control puts the 0.7.0 draft's four blocks back and must fail on exactly those
check "emulator: the sound buffer (simulated card)" ./build/linux/test_audio
control "emulator: sound buffer CONTROL (the 0.7.0 draft's four blocks, must fail)" \
    "^FAIL busy machine: no gap after 10 s +[0-9]{2,} gaps after 10 s" \
    "^FAIL linux: busy machine: no gap after 10 s +[0-9]{2,} gaps after 10 s" \
    "^FAIL linux: autosave: no gap +[0-9]+ saves of 50 ms: [1-9][0-9]* gaps" \
    "^ok +linux: steady card: no gap after the first second" "^ok +card: the played position from the delay" \
    "^audio: 14 FAILED$" -- ./build/linux/test_audio --old
check "emulator: a Type 'n Speak never set up, rescued" ./build/linux/test_rescue "$TNS"
control "emulator: rescue CONTROL (the old cold start left on, must fail)" \
    "^FAIL rescued: set up anew, its files carried +the unit's own setup did not complete" "^FAILED$" \
    -- env TEST_RESCUE_BREAK=1 ./build/linux/test_rescue "$TNS"
# Blazie's games from their disks (test_games.c; RetroBunn, PR #9: Simon locked the unit up at its first tone), when
# the data folder's games/ has them (simon.bns, hangman.bns: never in the repo; absent, named as a skip): Simon's first
# tone and its time-out, Hangman a guess answered; its control answers the bus FFh again (bl_board.h bl_bus_break)
for game in simon hangman; do
    if [ ! -f "$DATA/games/$game.bns" ]; then echo "skip  emulator: $game: no $DATA/games/$game.bns"; continue; fi
    check "emulator: $game (Braille Lite)" ./build/linux/test_games $game bl "$DATA/BL2ENG.BNS" \
        "$DATA/bl2_2003_warm.state" "$DATA/games/$game.bns"
    check "emulator: $game (Type 'n Speak)" ./build/linux/test_games $game tns "$TNS" - "$DATA/games/$game.bns"
done
if [ -f "$DATA/games/simon.bns" ]; then
    control "emulator: Simon CONTROL (the bus answered FFh, must fail)" "^ok +the game runs: its welcome and prompt" \
        "^ok +to the start key, the same with the bus FFh" "^FAIL the first tone after the start key +none in 2 s" \
        "^FAIL the game goes on: its time-out answered" "^FAILED$" \
        -- env TEST_GAMES_BREAK=1 ./build/linux/test_games simon bl "$DATA/BL2ENG.BNS" "$DATA/bl2_2003_warm.state" \
        "$DATA/games/simon.bns"
    # ... and from the flash startup folder, put there as "Load programs into flash" puts it (with a text file, one
    # batch); its control writes the entry in folder 0 (bl_files.h blf_break 7): the unit cannot find the game
    check "emulator: Simon from flash (Braille Lite)" ./build/linux/test_games simon bl "$DATA/BL2ENG.BNS" \
        "$DATA/bl2_2003_warm.state" "$DATA/games/simon.bns" --flash
    control "emulator: Simon from flash CONTROL (entry in folder 0, must fail)" \
        "^FAIL the game in flash startup, a program +simon\.bns: the unit's directory has no such file" \
        "^FAIL the first tone after the start key +none in 2 s" "^FAILED$" \
        -- env TEST_GAMES_FILES_BREAK=7 ./build/linux/test_games simon bl "$DATA/BL2ENG.BNS" \
        "$DATA/bl2_2003_warm.state" "$DATA/games/simon.bns" --flash
fi
# blazie_files, a saved unit's files from the command line: export, unpack, a new file, pack, import, on a copy of
# the shipped state; its control leaves a new flash file's blocks unmarked (bl_files.h blf_break 2)
check "emulator: blazie_files round trip (export, unpack, pack, import)" sh tools/blazie_files_roundtrip.sh \
    build/linux/blazie_files "$DATA/bl2_2003_warm.state"
control "emulator: blazie_files round trip CONTROL (flash blocks not marked used, must fail)" \
    "^NOT saved: .*not marked used" "^FAIL import$" "^round trip: FAILED$" \
    -- env TEST_FILES_BREAK=2 sh tools/blazie_files_roundtrip.sh build/linux/blazie_files "$DATA/bl2_2003_warm.state"
# the units a firmware folder can run, one list for every shell (emu_catalog.c): the old eight unchanged, a release
# unit only when its firmware is there; its control lists one without its firmware
check "emulator: the units a firmware folder can run (the catalog)" ./build/linux/test_catalog "$DATA"
control "emulator: the catalog CONTROL (a unit without its firmware, must fail)" \
    "^FAIL a folder with two release files" "^catalog: 2 FAILED$" \
    -- env CATALOG_BREAK=1 ./build/linux/test_catalog "$DATA"
# blazie_files add (the menu's "Load programs into flash"): one batch, all or nothing; its control writes the new
# entries in folder 0 (bl_files.h blf_break 7)
check "emulator: programs into flash (blazie_files add)" python3 nvda/tools/files_add.py build/linux/blazie_files \
    "$DATA/bl2_2003_warm.state"
control "emulator: programs into flash CONTROL (entries in folder 0, must fail)" \
    "^FAIL 1\. the batch added .*marked used but no file's" "^FAIL 1\. the program in flash startup, byte for byte +no file" \
    "^ok +4\. too big for the flash" "^FAILED$" \
    -- env TEST_FILES_BREAK=7 python3 nvda/tools/files_add.py build/linux/blazie_files "$DATA/bl2_2003_warm.state"
if [ -x "$EMU" ]; then
    check "emulator: the program headless" python3 src/apps/blazie/test_emu_linux.py "$EMU" "$DATA"
    control "emulator: program CONTROL (dots 1 and 4 swapped, must fail)" \
        "^ok +boot greeting" "^FAIL +the clock, from the system time \(keys\)" \
        "^FAIL +the clock, from the system time \(letters\)" "^emulator: [23] of 4 FAILED$" \
        -- env BLAZIE_KEYS_BREAK=1 python3 src/apps/blazie/test_emu_linux.py "$EMU" "$DATA" \
        --only boot,clock-keys,clock-letters
    # one emulator everywhere (Tomi): on a BT Speak or BT Braille blazie_emu hands over to blazie_emu_bt
    # (bt_handover.c), or with [input] bt = native uses the device itself -- the detection against a stand-in BTSpeak
    # library, this machine's own answer (no), the hand-over's options to a stand-in frontend, and --no-bt,
    # bt = off, bt = native, no device and a missing frontend keeping this program; its control ignores --no-bt and
    # the setting and loses --unit
    check "emulator: a BT Speak or BT Braille found, handed over to blazie_emu_bt (bt = native: blazie_emu itself)" \
        python3 src/apps/blazie/test_bt_handover.py "$EMU"
    control "emulator: BT hand-over CONTROL (--no-bt and the setting ignored, --unit lost, must fail)" \
        "^ok +detection: the BTSpeak library and its keyboard service" "^ok +detection: no BTSpeak library" \
        "^FAIL +hand-over: the device found, its options" "^FAIL +--no-bt: the terminal emulator" \
        "^FAIL +\[input\] bt = off: the terminal emulator" "^FAIL +\[input\] bt = native: no hand-over" \
        "^ok +no device: the terminal emulator" \
        "^bt hand-over: 4 of 14 FAILED$" -- env BLAZIE_BT_BREAK=1 python3 src/apps/blazie/test_bt_handover.py "$EMU"
    check "emulator: libraries needed (libc, libm, libasound/libpulse; libstdc++ inside)" sh -c "! ldd $EMU | \
        grep -v -E 'linux-vdso|ld-linux|libc\.so|libm\.so|libpthread|libasound|libpulse|libdl' | grep -q . && \
        ! ldd $EMU | grep -q -E 'libstdc|libgcc_s' && echo 'only the C library and the sound library'"
else
    echo "FAIL  emulator: build/linux/blazie_emu not built (sudo apt install libasound2-dev, then ./build_linux.sh)"
    fail=1
fi
# The same emulator in a GTK window, for Orca (main_gtk.c; README-linux.md, "The desktop app"): run in a virtual X
# display (Xvfb) with its own session and accessibility buses, its keys typed through the X server (xdotool), its
# window read as Orca reads it (AT-SPI): the menu bar's items, the keyboard area's name and focus, a chord answered,
# F11 and Alt+Shift+F, the dialogs' text, the announcements, the Type 'n Speak's keys, i-chord held through a
# restart, the braille display field for Orca.  Its controls leave the keyboard area unnamed (BLAZIE_GTK_BREAK=noname),
# freeze the braille display field (brlfrozen), make it read-only and named (brlreadonly) and swap dots 1 and 4.  Skipped,
# saying why, where GTK, Xvfb, xdotool or the accessibility bus is missing (the BTSpeak, a minimal build).
GTK_EMU=build/linux/blazie_emu_gtk
GTK_SHIP=""
XRUN="dbus-run-session -- xvfb-run -a -s"
if [ ! -x "$GTK_EMU" ]; then
    echo "skip  emulator (GTK): $GTK_EMU not built (no GTK 3 headers: sudo apt install libgtk-3-dev)"
else
    GTK_SHIP="$GTK_EMU"
    # the loader is NEEDED too on x86-64 built against glibc 2.31 (the static libstdc++'s __tls_get_addr lives there;
    # aarch64 uses TLS descriptors): every program's interpreter anyway, so it is allowed
    check "emulator (GTK): libraries needed (libc, libm, sound, GTK's own; libstdc++ inside)" sh -c "! readelf -d \
        $GTK_EMU | grep NEEDED | grep -v -E '\[lib(c|m|pthread|dl|asound|pulse|pulse-simple|gtk-3|gdk-3|glib-2\.0|\
gobject-2\.0|gio-2\.0|atk-1\.0|pango-1\.0|pangocairo-1\.0|cairo|cairo-gobject|gdk_pixbuf-2\.0|harfbuzz)\.so' | \
        grep -v -F '[ld-linux' | grep -q . &&! readelf -d $GTK_EMU | grep NEEDED | grep -q -E 'libstdc|libgcc_s' && \
        echo 'only the C library, the sound library and GTK (dynamic)'"
    if ! command -v xvfb-run >/dev/null 2>&1; then
        echo "skip  emulator (GTK): no Xvfb (sudo apt install xvfb)"
    elif ! command -v dbus-run-session >/dev/null 2>&1; then
        echo "skip  emulator (GTK): no dbus-run-session (sudo apt install dbus-bin)"
    elif ! command -v xdotool >/dev/null 2>&1; then
        echo "skip  emulator (GTK): no xdotool to type its keys (sudo apt install xdotool)"
    elif [ ! -f /usr/share/dbus-1/services/org.a11y.Bus.service ]; then
        echo "skip  emulator (GTK): no accessibility bus (sudo apt install at-spi2-core)"
    elif ! python3 -c "import gi; gi.require_version('Atspi', '2.0')" >/dev/null 2>&1; then
        echo "skip  emulator (GTK): no AT-SPI for Python (sudo apt install python3-gi gir1.2-atspi-2.0)"
    else
        check "emulator (GTK): the window as Orca reads it, keys through X" $XRUN "-screen 0 1024x768x24" \
            python3 src/apps/blazie/test_emu_gtk.py "$GTK_EMU" "$DATA"
        control "emulator (GTK): accessibility CONTROL (the keyboard area unnamed, must fail)" \
            "^ok +menu bar: items by name" "^FAIL +keyboard area: named, focused +role panel, focused, name ''" \
            "^ok +status bar" "^gtk emulator: 1 of 4 FAILED$" \
            -- env BLAZIE_GTK_BREAK=noname $XRUN "-screen 0 1024x768x24" \
            python3 src/apps/blazie/test_emu_gtk.py "$GTK_EMU" "$DATA" --only settings,tree
        # the braille display field (Alt+Shift+B, for Orca): frozen after its first cells, the advance bar must fail
        control "emulator (GTK): braille display CONTROL (the field frozen, must fail)" \
            "^ok +braille display field shown and focused" "^FAIL +braille display field: Alt\+Shift\+Right is the advance bar" \
            "^gtk emulator: 1 of 5 FAILED$" \
            -- env BLAZIE_GTK_BREAK=brlfrozen $XRUN "-screen 0 1024x768x24" \
            python3 src/apps/blazie/test_emu_gtk.py "$GTK_EMU" "$DATA" --only braille
        # 0.8's first preview: the field read-only (Orca 49 and later then never refresh braille from it) and named
        # (its caption before the cells on the display) -- that check must fail, and only it
        control "emulator (GTK): braille display CONTROL (read-only and named, must fail)" \
            "^FAIL +braille display field: editable to a screen reader" "^ok +braille display field: Alt\+Shift\+Right is the advance bar" \
            "^gtk emulator: 1 of 5 FAILED$" \
            -- env BLAZIE_GTK_BREAK=brlreadonly $XRUN "-screen 0 1024x768x24" \
            python3 src/apps/blazie/test_emu_gtk.py "$GTK_EMU" "$DATA" --only braille
        control "emulator (GTK): held keys CONTROL (dots 1 and 4 swapped, must fail)" \
            "^FAIL +i-chord held through the restart +the unit asked \"initialize file system\": no" \
            "^gtk emulator: 1 of 1 FAILED$" \
            -- env BLAZIE_KEYS_BREAK=1 $XRUN "-screen 0 1024x768x24" \
            python3 src/apps/blazie/test_emu_gtk.py "$GTK_EMU" "$DATA" --only held
    fi
fi
# the Python wheel: built from build/linux, installed into a fresh venv, the Braille Lite as the library directly
check "Python wheel" python3 python/test_wheel.py --build "$DATA"
control "Python wheel CONTROL (rate 70, must fail)" "^ok +the chip alone:" "^FAIL +the Braille Lite:" \
    "^wheel: 1 FAILED$" -- env WHEEL_TEST_BREAK=1 python3 python/test_wheel.py --build "$DATA"
# the no-GPL audit: what ships -- the library, the module, the package (built here, firmware included) and a wheel
rm -rf build/audit && mkdir -p build/audit
# The English Braille 'n Speak 2000's firmware ships in the emulator's own downloads only, never in this package
# (Tomi); the Slovak unit's ships in it since 0.8, as its voice's (BS2SLL.BNS, flat in share/).  A package made from
# a firmware folder that has them (the data folder's files, and stand-ins in bns2000/ when it has none) must carry
# no bns2000/ folder and no English image; the control, a package holding the English one, must fail
no_bns() {                         # a package: no English Braille 'n Speak 2000 firmware in it
    hits="$(tar -tzf "$1" | grep -i -E '(^|/)bns2000/|bs03eng')"   # the Slovak unit ships (0.8)
    if [ -n "$hits" ]; then
        echo "$hits" | sed "s/^/FAIL Braille 'n Speak 2000 firmware in the package: /"
        echo "no-BNS check: FAILED"
        return 1
    fi
    echo "no Braille 'n Speak 2000 firmware in $(basename "$1")"
}
BNS_FW=build/audit/firmware-with-bns
mkdir -p "$BNS_FW"
for f in "$DATA"/*; do ln -s "$f" "$BNS_FW/"; done
if [ ! -e "$BNS_FW/bns2000" ]; then
    mkdir "$BNS_FW/bns2000"
    for f in BS03ENG.BNS bs03eng_fresh.state BS2SLL.BNS bs2sll_fresh.state; do echo stand-in > "$BNS_FW/bns2000/$f"; done
fi
rm -rf build/ssi263-speech-bns-check-linux-* build/package/ssi263-speech-bns-check-linux-*
check "package from a firmware folder with the Braille 'n Speak 2000's" sh tools/package_linux.sh "$BNS_FW" bns-check
check "no Braille 'n Speak 2000 firmware in the Linux package" \
    no_bns build/ssi263-speech-bns-check-linux-"$(uname -m)".tar.gz
rm -rf build/ssi263-speech-bns-check-linux-* build/package/ssi263-speech-bns-check-linux-*
mkdir -p build/audit/bns-control/pkg/share/ssi263-speech/bns2000
echo stand-in > build/audit/bns-control/pkg/share/ssi263-speech/bns2000/BS03ENG.BNS
tar -czf build/audit/bns-control.tar.gz -C build/audit/bns-control pkg
control "no-BNS CONTROL (a package with the Braille 'n Speak 2000's firmware, must fail)" \
    "^FAIL Braille 'n Speak 2000 firmware in the package: pkg/share/ssi263-speech/bns2000/BS03ENG\.BNS$" \
    "^no-BNS check: FAILED$" -- no_bns build/audit/bns-control.tar.gz
# one package only: the checks below read build/ssi263-speech-*-linux-<arch>.tar.gz, and an earlier version's left
# in build/ (0.7.7 beside 0.8.0) made that two files and tar fail
rm -f build/ssi263-speech-*-linux-"$(uname -m)".tar.gz
check "package" sh tools/package_linux.sh "$DATA"
# Every voice on every platform (Tomi: a green suite that hides a broken integration is worse than a red one): voices.c's
# voices by id, here from the built module (sd_ssi263 --voices) and the package's ssi263.conf just made (the Braille
# Lite's SSI263RunAhead and SSI263BrailleLiteNumbers), and Android from its sources; NVDA and SAPI are Windows's
# (nvda/tools/run_tests.py requires them) and say "skip" here.  The control drops one Linux cell and must name it.
check "uniform: every voice on Linux and Android" python3 tools/check_uniform.py --require linux,android \
    --sd-binary build/linux/sd_ssi263
control "uniform CONTROL (Linux lacks the Spanish Braille Lite, must fail)" \
    "^Braille Lite 2000 \(Spanish\) +skip +skip +NO +yes +$" \
    "^MISSING  Braille Lite 2000 \(Spanish\) \[blazie:blazie_es\] on Linux$" \
    "^gap +Linux: [0-9]+ of [0-9]+ voices, from build/linux/sd_ssi263 --voices" "^ok +Android: " "^uniform: 1 gap$" \
    -- env SSI263_UNIFORM_DROP=linux:blazie:blazie_es python3 tools/check_uniform.py --require linux,android \
    --sd-binary build/linux/sd_ssi263
# ... and the module's order: the Sounding Board right under the Speak-Out (Tomi, 2026-10-25)
control "uniform CONTROL (Linux lists the Sounding Board last, must fail)" \
    "^ORDER  speakout:soundingboard is not listed right after speakout:speakout on Linux \(after it: mockingboard:mockingboard\)$" \
    "^ok +Android: " "^uniform: 1 gap$" \
    -- env SSI263_UNIFORM_ORDER_BREAK=linux python3 tools/check_uniform.py --require linux,android \
    --sd-binary build/linux/sd_ssi263
check "wheel for the audit" python3 python/build_wheel.py --lib-dir build/linux --plat "linux_$(uname -m)" \
    --out build/audit
check "no z180emu or Unicorn engine, no GPL notice in what ships" python3 tools/check_no_gpl.py "$LIB" \
    build/linux/sd_ssi263 build/linux/blazie_emu build/linux/blazie_files build/linux/blazie_bt \
    build/linux/blazie_emu_bt $GTK_SHIP build/ssi263-speech-*-linux-"$(uname -m)".tar.gz \
    build/audit/ssi263speech-*.whl
# the licences that must ship: MIT (ours, Casso's) and MAME's BSD-3-Clause for the Z180, in the package and the wheel
check "licences in the package and the wheel" sh -c "tar -tzf build/ssi263-speech-*-linux-$(uname -m).tar.gz | \
    grep -c -E '/(LICENSE|licenses/Casso-MIT\.txt|licenses/MAME-Z180-core-BSD-3-Clause\.txt)\$' | grep -qx 3 && \
    python3 -c 'import sys, zipfile; n = zipfile.ZipFile(sys.argv[1]).namelist(); sys.exit(sum(x.endswith(( \
    \".dist-info/LICENSE\", \".dist-info/Casso-MIT.txt\", \".dist-info/MAME-Z180-core-BSD-3-Clause.txt\")) \
    for x in n) != 3)' build/audit/ssi263speech-*.whl && echo 'MIT, Casso MIT, MAME Z180 BSD-3-Clause: in both'"
# the Accent SA in the package (0.7.1, every voice on Linux): Aicom's three ROMs where the module looks for them, with
# Aicom's notice and MAME's 8085 notice; the Accent-mini and Speak-Out, when the module has them, are checked the same
check "the Accent SA in the package, with its notices" sh -c "tar -tzf build/ssi263-speech-*-linux-$(uname -m).tar.gz | \
    grep -c -E '/(share/ssi263-speech/aicom-accent-sa/u[234]\.BIN|licenses/Aicom-notice\.txt|licenses/MAME-8085-core-BSD-3-Clause\.txt)\$' | \
    grep -qx 5 && echo 'u2, u3, u4, the Aicom notice, MAME 8085 BSD-3-Clause: in the package'"
if ./build/linux/sd_ssi263 --voices | grep -q '^accent-mini'; then
    check "the Accent-mini in the package, with its notices" sh -c "tar -tzf build/ssi263-speech-*-linux-$(uname -m).tar.gz | \
        grep -c -E '/(share/ssi263-speech/aicom-accent-mini/SPKEMS\.DVC|licenses/MAME-8086-core-BSD-3-Clause\.txt)\$' | \
        grep -qx 2 && echo 'SPKEMS.DVC, MAME 8086 BSD-3-Clause: in the package'"
fi
if ./build/linux/sd_ssi263 --voices | grep -q '^speakout'; then
    check "the Speak-Out in the package, with its notices" sh -c "tar -tzf build/ssi263-speech-*-linux-$(uname -m).tar.gz | \
        grep -c -E '/(share/ssi263-speech/gw-micro-speakout/SPEAKOUT\.HEX|licenses/MAME-V40-core-BSD-3-Clause\.txt|licenses/Speak-Out-firmware-notice\.txt)\$' | \
        grep -qx 3 && echo 'SPEAKOUT.HEX, its notice, MAME V40 BSD-3-Clause: in the package'"
fi
# GW Micro's Sounding Board drivers, bundled as the Speak-Out's firmware is (Tomi, 2026-10-18), when this build host
# has them (firmware/gw-micro-sounding-board, never in the repository)
if ./build/linux/sd_ssi263 --voices | grep -q '^soundingboard' && [ -f "$ROOT/firmware/gw-micro-sounding-board/SB.COM" ]; then
    check "the Sounding Board in the package, with its notices" sh -c "tar -tzf build/ssi263-speech-*-linux-$(uname -m).tar.gz | \
        grep -c -E '/(share/ssi263-speech/gw-micro-sounding-board/(SBLOAD|SB)\.COM|licenses/MAME-8086-core-BSD-3-Clause\.txt|licenses/Sounding-Board-drivers-notice\.txt)\$' | \
        grep -qx 4 && echo 'SBLOAD.COM, SB.COM, their notice, MAME 8086 BSD-3-Clause: in the package'"
fi
# the SmarTalk as a voice (tools/check_st_voice.py; S_TALK.EXE is in the repository, so it always runs), through the
# voice table and the NVDA binding on libssi263_voices.so (build_linux.sh: the speech-dispatcher module's own objects
# as a library, for the tests, never shipped); its controls, as nvda/tools/run_tests.py's
check "SmarTalk voice: settings, cancel, parts, text (voice API and binding)" python3 tools/check_st_voice.py \
    build/linux/libssi263_voices.so
control "SmarTalk voice CONTROL (the tone pinned to A, must fail)" "^FAIL  tone: A lower, Z higher than I " \
    "^ok    volume: the gain only " "^st voice: 1 FAILED$" -- \
    env ST_VOICE_BREAK=1 python3 tools/check_st_voice.py build/linux/libssi263_voices.so
control "SmarTalk voice CONTROL (the rate's old mapping, 50 = ESC >5, must fail)" \
    "^FAIL  rate back at 50: a fresh unit's speed " "^ok    rate: 0 slower, 100 faster " "^st voice: 1 FAILED$" -- \
    env ST_RATE_BREAK=1 python3 tools/check_st_voice.py build/linux/libssi263_voices.so
control "SmarTalk voice CONTROL (the bank's last slot kept on a boot change, must fail)" \
    "^FAIL  a bank's unit made again on a boot change " "^ok    speaks through the table " "^st voice: 1 FAILED$" -- \
    env SSV_BANK_BREAK=1 python3 tools/check_st_voice.py build/linux/libssi263_voices.so
control "SmarTalk voice CONTROL (the contractions as they are, must fail)" "^FAIL  said as: don't +\"don't\"$" \
    "^FAIL  said as: I'm " "^FAIL  don't: said as one word, no letter T " "^ok    said as: rock 'n roll " \
    "^st voice: 6 FAILED$" -- env ST_TEXT_BREAK=1 python3 tools/check_st_voice.py build/linux/libssi263_voices.so
# letters typed at rate 0 on the Speak-Out's front end, both boxes (tools/check_so_tail.py, as run_tests.py's): no
# letter starts with the last one's phoneme; the control leaves the held phoneme out of busy().  The two boxes' files
# from where the translit check above looks for SPEAKOUT.HEX, gathered in one folder
SOFW=build/so-tail-firmware
rm -rf $SOFW && mkdir -p $SOFW/gw-micro-speakout $SOFW/gw-micro-sounding-board
for so in "$DATA/gw-micro-speakout" "$ROOT/firmware/gw-micro-speakout" "$DATA/../speakout-firmware"; do
    [ -f "$so/SPEAKOUT.HEX" ] && { ln -sf "$so/SPEAKOUT.HEX" $SOFW/gw-micro-speakout/; break; }
done
for sb in "$DATA/gw-micro-sounding-board" "$ROOT/firmware/gw-micro-sounding-board" "$DATA/../speakout-firmware"; do
    [ -f "$sb/SB.COM" ] && [ -f "$sb/SBLOAD.COM" ] && { ln -sf "$sb/SB.COM" "$sb/SBLOAD.COM" $SOFW/gw-micro-sounding-board/; break; }
done
if [ -f $SOFW/gw-micro-speakout/SPEAKOUT.HEX ] && [ -f $SOFW/gw-micro-sounding-board/SB.COM ]; then
    check "Speak-Out and Sounding Board at rate 0: no letter starts with the last" python3 tools/check_so_tail.py \
        build/linux/libssi263_voices.so $SOFW
    control "Rate 0 letters CONTROL (the held phoneme left out, must fail)" \
        "^FAIL  soundingboard rate 0 to its end +a: held S, " "^FAIL  speakout rate 0 cut at 0\.12 s +e: held E, " \
        "^so tail: 4 FAILED$" -- env SSI263_SO_TAIL_BREAK=1 python3 tools/check_so_tail.py build/linux/libssi263_voices.so $SOFW
else
    echo "skip  rate 0 letters: no SPEAKOUT.HEX with SBLOAD.COM and SB.COM (tools/check_so_tail.py)"
fi
# Automated Functions' SmarTalk: S_TALK.EXE is in the repository, so the package always carries it with its notice
if ./build/linux/sd_ssi263 --voices | grep -q '^smartalk'; then
    check "the SmarTalk in the package, with its notices" sh -c "tar -tzf build/ssi263-speech-*-linux-$(uname -m).tar.gz | \
        grep -c -E '/(share/ssi263-speech/automated-functions-smartalk/S_TALK\.EXE|licenses/MAME-8086-core-BSD-3-Clause\.txt|licenses/SmarTalk-software-notice\.txt)\$' | \
        grep -qx 3 && echo 'S_TALK.EXE, its notice, MAME 8086 BSD-3-Clause: in the package'"
fi
check "no-GPL audit: comments and MAME compatibility names are not evidence" python3 tools/check_no_gpl.py \
    --clean-sample
check "no build path in what ships" sh -c "! grep -a -q -F '$ROOT' '$LIB' build/linux/sd_ssi263 build/linux/blazie_emu \
    build/linux/blazie_files build/linux/blazie_bt build/linux/blazie_emu_bt $GTK_SHIP"
control "no-GPL audit CONTROL (genuine legacy payloads, must fail)" \
    "^FAIL control\.apk: control\.apk!lib/arm64-v8a/libssi263speech\.so: z180emu engine" \
    "^FAIL control\.apk: control\.apk!lib/arm64-v8a/libssi263speech\.so: Unicorn engine" \
    "^FAIL control\.apk: control\.apk!lib/x86/unicorn\.dll: a legacy payload by name" \
    "^FAIL control\.apk: control\.apk!hosts/ucmini\.py: Unicorn import" \
    "^FAIL control\.apk: control\.apk!hosts/i8085\.py: a legacy payload by name" \
    "^FAIL control\.apk: control\.apk!assets/licenses/third-party\.txt: GPL notice" \
    "^FAIL control\.apk: control\.apk!bin/blazie_emu_bt!frontend\.py: Unicorn import" "^no-GPL audit: 1 of 1 FAILED$" \
    -- python3 tools/check_no_gpl.py --control
if [ -f "$LEGACY/libssi263speech_legacy.so" ]; then
    # the real thing, stripped as the APK's library is: what stripping keeps must still give z180emu away
    strip --strip-unneeded -o build/audit/libssi263speech_legacy.so "$LEGACY/libssi263speech_legacy.so"
    control "no-GPL audit CONTROL (the stripped z180emu library, must fail)" \
        "^FAIL libssi263speech_legacy\.so: libssi263speech_legacy\.so: z180emu engine" \
        "^no-GPL audit: 1 of 1 FAILED$" \
        -- python3 tools/check_no_gpl.py build/audit/libssi263speech_legacy.so
else
    echo "skip  no-GPL audit CONTROL on the real z180emu library (LEGACY=1 ./build_linux.sh)"
fi
# and the judgement itself: wrong failures, crashes and silent exits never pass as controls
check "control guard" sh tools/linux_control_guard.sh
[ $fail -eq 0 ] && echo "all Linux checks passed ($PLAT)" || echo "Linux checks FAILED ($PLAT)"
exit $fail
