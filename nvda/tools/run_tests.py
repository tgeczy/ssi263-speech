"""Every add-on check at once, across all cores, in under 3 minutes (Tomi: a test run takes 3 minutes at most, 5 is
pushing it).  Each check is its own process with a hard time limit; one line per check, then the total.

    python run_tests.py ["C:\\Program Files\\NVDA"]

A control check must FAIL: complete_fuzz with 0.5.0's cancel put back proves the fuzz can still see a cut-off
utterance (a fix claimed without a test that fails on the bug is how 0.6.0 shipped one).
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

# a check's own output may carry characters the console lacks ("£", U+FFFD): never let the report crash the run
sys.stdout.reconfigure(errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
NVDA = sys.argv[1] if len(sys.argv) > 1 else r"C:\Program Files\NVDA"
PY = sys.executable
WIN7 = os.path.join(HERE, "win7")
PY37 = os.path.join(WIN7, "py37", "python.exe")
LIMIT = 240          # seconds, per check


def check(name, argv, env=None, cwd=HERE, ok=None, expect_fail=False, fail_marks=(), fail_codes=(1,)):
    """A must-fail control (expect_fail) names the failure it must show (after Astra, Reply 97): an exit code from
    fail_codes AND every regex in fail_marks found in its output (the intended diagnostic, and the completed
    inventory where it runs several cases).  A crash, a silent exit, an empty output or a timeout is not that."""
    assert not expect_fail or fail_marks, "a must-fail control needs fail_marks: %s" % name
    return dict(name=name, argv=argv, env=env or {}, cwd=cwd, ok=ok, expect_fail=expect_fail,
                fail_marks=tuple(fail_marks), fail_codes=tuple(fail_codes))


CHECKS = []
for w in ("speakout", "blazie", "accent", "accentsa"):
    CHECKS.append(check("driver_sim %s (NVDA 64-bit)" % w, [PY, "-S", "driver_sim.py", w, NVDA, "rt"]))
if os.path.isfile(PY37):
    for app in ("nvda2023app", "nvda2021app"):
        if os.path.isdir(os.path.join(WIN7, app)):
            for w in ("speakout", "blazie", "accent"):
                CHECKS.append(check("driver_sim %s (%s, 32-bit)" % (w, app), [PY37, "run37.py", "../driver_sim.py", w, app, "rt"],
                                    env={"NVDA_APP": app}, cwd=WIN7))
# (early in the list: three ~20 s runs, started first so they overlap the rest)
# am_voice (src/csrc/accentmini: the Accent-mini's SPKEMS.DVC on MAME's 8086, accent.py's host and the NVDA driver's
# front end in C, for speech-dispatcher and Android): the real driver's "mini" voice, byte for byte -- numbers,
# punctuation, a capital, the settings' extremes, every sample rate, a cancel mid-utterance and the next utterance.
# Each control breaks the C side one way and must show exactly that: a cancel one block late, number processing
# flipped.  Built by src/csrc/accentmini/build_am.py (the test refuses a DLL older than its sources).
AM_LIB = os.path.join(os.path.dirname(HERE), "dist", "accentmini-lib", "x64", "accent_mini.dll")
if os.path.isfile(AM_LIB):
    CHECKS.append(check("am_voice = the NVDA driver's Accent-mini, byte for byte", [PY, "am_voice_equiv.py"]))
    CHECKS.append(check("am_voice CONTROL (cancel one block late, must fail)", [PY, "am_voice_equiv.py"],
                        env={"AM_EQUIV_BREAK": "cancel"}, expect_fail=True,
                        fail_marks=[r"^DIFF  cancel ", r"^DIFF  after-cancel ", r"^DIFF  cancel-capital ",
                                    r"^same  hello ", r"^same  numbers ", r"^same  4000 of 4000 random texts",
                                    r"^11 of 15 utterances byte-identical to the NVDA driver$"]))
    CHECKS.append(check("am_voice CONTROL (numbers flipped, must fail)", [PY, "am_voice_equiv.py"],
                        env={"AM_EQUIV_BREAK": "numbers"}, expect_fail=True,
                        fail_marks=[r"^DIFF  numbers ", r"^DIFF  numbers-off ", r"^same  hello ",
                                    r"^DIFF  (?!4000 )\d+ of 4000 random texts give the driver's text$",
                                    r"^2 of 15 utterances byte-identical to the NVDA driver$"]))
# 0.7.5's native Speak-Out and Accent drivers (ssi263speech.dll: no Python host or front end) against 0.7.0's Python
# drivers over this tree's chip (legacy_drivers.py: the old-driver/current-chip integration reference), byte for byte
# with the index and done order: NVDA's sequences (texts with indexes between, pieces, capitals alone, inside a sequence and over an index), numbers, money, punctuation, the settings' extremes,
# every sample rate, both Accents with their variants, inflection and numbers, cancels after a block and at an index.
# Controls: the job re-begun before every text (the voices' one-text API) must fail the multi-text cases and pass the
# first one-text case; the native cancels one block late must fail the block cuts.
NATIVE_EQ_SUM = r"^(?!99 )\d+ of 99 sequences byte-identical to 0\.7\.0's drivers"
# Only when the built add-ons are the native ones (nvda/dist/<addon>-build with ssi263speech.py, built by
# build_speakout.py / build_accent.py once they package ssi263speech.dll).
NATIVE_BUILT = all(os.path.isfile(os.path.join(os.path.dirname(HERE), "dist", "%s-build" % a, "synthDrivers",
                                               "_ssi263_%s" % a, "ssi263speech.py")) for a in ("speakout", "accent"))
if NATIVE_BUILT:
    CHECKS.append(check("native drivers = the old-driver/current-chip reference, byte for byte",
                        [PY, "native_driver_equiv.py"]))
    CHECKS.append(check("native drivers CONTROL (the job re-begun per text, must fail)", [PY, "native_driver_equiv.py"],
                        env={"NATIVE_EQUIV_BREAK": "per-text"}, expect_fail=True,
                        fail_marks=[r"^same  speakout plain ", r"^DIFF  speakout indexes ", r"^DIFF  mini indexes ",
                                    r"^DIFF  sa indexes ", r"^DIFF  speakout capital over an index ", NATIVE_EQ_SUM]))
    CHECKS.append(check("native drivers CONTROL (cancel one block late, must fail)", [PY, "native_driver_equiv.py"],
                        env={"NATIVE_EQUIV_BREAK": "cancel"}, expect_fail=True,
                        fail_marks=[r"^same  speakout plain ", r"^DIFF  speakout cancel in a sequence .*cut after",
                                    r"^DIFF  mini cancel in a sequence .*cut after",
                                    r"^DIFF  sa cancel in a sequence .*cut after", NATIVE_EQ_SUM]))
    # Issue #8: the Accent SA called a text done in the firmware's quiet between two clauses ("Rate:" | "slider 55
    # alt+r"), and the rest came out with the next speech.  as_voice now waits while the 8085 still runs its rules.
    # test_as_complete.py: the library every front end shares (the job API and asv_speak), the host run on after each
    # done -- no phoneme may come, and a text that never paused keeps its audio and its done's chip time; blocks shifted
    # 0-27 ms; a cancel or new text while blocks are held; the cap reported, never a plain done (Astra, Reply 152);
    # dialog_stall.py: the built add-on's driver tabbed through NVDA's voice settings dialog.  Controls: the settle off
    # (0.7.5's done); a render budget no text can finish in (an exhausted loop is not a completion); a wait kept across
    # a cancel or new text; the cap as a plain done -- each must fail its own check and only that.
    AS_COMPLETE = os.path.join(os.path.dirname(os.path.dirname(HERE)), "src", "csrc", "accentsa", "test_as_complete.py")
    AS_CHECKS = r"^checks: done %s, complete %s, audio ok, latency ok, phase %s, replace %s, limit %s$"
    CHECKS.append(check("Accent SA: a text done only when said (issue #8)", [PY, AS_COMPLETE]))
    CHECKS.append(check("Accent SA completion CONTROL (the settle off, must fail)", [PY, AS_COMPLETE],
                        env={"AS_COMPLETE_BREAK": "1"}, expect_fail=True,
                        fail_marks=[r"^EARLY  rate +60 job +'Pitch: slider 50 alt\+p'",
                                    r"^EARLY  rate +75 say +'Rate: slider 75 alt\+r'",
                                    AS_CHECKS % ("ok", r"\d+ FAILED", r"\d+ FAILED", "ok", "ok"),
                                    r"^accent sa completion: FAILED$"]))
    CHECKS.append(check("Accent SA completion CONTROL (never done, must fail)", [PY, AS_COMPLETE],
                        env={"AS_COMPLETE_BREAK": "never"}, expect_fail=True,
                        fail_marks=[r"^NOT DONE after 3 render calls  rate +50 job +'Rate: slider 50 alt\+r'",
                                    AS_CHECKS % (r"\d+ FAILED", "ok", "ok", "ok", "ok"),
                                    r"^accent sa completion: FAILED$"]))
    CHECKS.append(check("Accent SA completion CONTROL (a wait kept across a cancel, must fail)", [PY, AS_COMPLETE],
                        env={"AS_COMPLETE_BREAK": "held"}, expect_fail=True,
                        fail_marks=[r"^HELD   rate +60 job +'Pitch: slider 50 alt\+p', cancel while held",
                                    r"^HELD   rate +60 say +'Pitch: slider 50 alt\+p', new text while held",
                                    AS_CHECKS % ("ok", "ok", "ok", r"\d+ FAILED", "ok"),
                                    r"^accent sa completion: FAILED$"]))
    CHECKS.append(check("Accent SA completion CONTROL (the cap as a plain done, must fail)", [PY, AS_COMPLETE],
                        env={"AS_COMPLETE_BREAK": "limit"}, expect_fail=True,
                        fail_marks=[r"^SILENT LIMIT  rate +60 job +'Pitch: slider 50 alt\+p': done with \d+ of its \d+ "
                                    r"samples, and no limit reported",
                                    r"^RECOVERY  rate +60 job +'Pitch: slider 50 alt\+p': the next text after the limit "
                                    r"had \d+ phonemes \(\d+ alone\)",
                                    AS_CHECKS % ("ok", "ok", "ok", "ok", r"\d+ FAILED"),
                                    r"^accent sa completion: FAILED$"]))
    CHECKS.append(check("dialog_stall accentsa (issue #8, the NVDA driver)", [PY, "dialog_stall.py", "accentsa", "2"],
                        env={"SIM_SPEED": "4"}))
    CHECKS.append(check("dialog_stall accentsa CONTROL (the settle off, must fail)",
                        [PY, "dialog_stall.py", "accentsa", "2"], env={"SIM_SPEED": "4", "DIALOG_STALL_BREAK": "1"},
                        expect_fail=True,
                        fail_marks=[r"^rate +50: +0 of +8 ", r"^rate +100: +[1-8] of +8 announcements complete too early",
                                    r"more still in the unit", r"^accent: [1-9]\d* of 40 announcements complete too "
                                    r"early -- STALL$"]))
    CHECKS.append(check("dialog_stall accent (the Accent-mini)", [PY, "dialog_stall.py", "accent", "2"],
                        env={"SIM_SPEED": "4"}))
    CHECKS.append(check("dialog_stall speakout", [PY, "dialog_stall.py", "speakout", "2"], env={"SIM_SPEED": "4"}))
for seed in (1, 2, 3, 4):
    CHECKS.append(check("complete_fuzz seed %d" % seed, [PY, "complete_fuzz.py", "150", str(seed)], env={"SIM_SPEED": "10"}))
for synth in ("speakout", "accent"):
    CHECKS.append(check("complete_fuzz %s" % synth, [PY, "complete_fuzz.py", "150", "1"],
                        env={"SIM_SPEED": "10", "COMPLETE_FUZZ_SYNTH": synth}))
# The 0.8 add-on's driver (synthDrivers/ssi263.py: every unit in one, by firmware type) against each unit's own driver
# (the 0.7-style builds, the Mockingboard's own), byte for byte, with the notifications the same and every one naming
# the 0.8 driver (NVDA drops the others).  Controls: the units reporting as themselves must fail every case on the
# synth; the variant not forwarded must fail the tone cases and pass the first.  Built by nvda/build_ssi263.py.
UNIFIED_SUM = r"^(?!(\d+) of \1 )\d+ of \d+ sequences byte-identical to the units' own drivers"
if os.path.isdir(os.path.join(os.path.dirname(HERE), "dist", "ssi263-build", "synthDrivers")):
    CHECKS.append(check("0.8 driver = each unit's own driver, byte for byte", [PY, "unified_driver_equiv.py"],
                        ok=lambda out: bool(re.search(r"^(\d+) of \1 sequences byte-identical", out, re.M))))
    CHECKS.append(check("0.8 driver CONTROL (units report as themselves, must fail)", [PY, "unified_driver_equiv.py"],
                        env={"UNIFIED_EQUIV_BREAK": "notify"}, expect_fail=True,
                        fail_marks=[r"^DIFF  speakout plain +a notification NOT from the wrapper",
                                    r"^DIFF  braillelite2000 plain +a notification NOT from the wrapper",
                                    r"^0 of \d+ sequences byte-identical"]))
    CHECKS.append(check("0.8 driver CONTROL (the variant not forwarded, must fail)", [PY, "unified_driver_equiv.py"],
                        env={"UNIFIED_EQUIV_BREAK": "memory"}, expect_fail=True,
                        fail_marks=[r"^same  speakout plain ", r"^DIFF  speakout tone a +PCM differs",
                                    r"^DIFF  braillelite2000 tone 0 +PCM differs", UNIFIED_SUM]))
    # its settings under NVDA's own load and save order, profiles and Cancel (unified_settings_test.py): each type's
    # values only in its own keys (Reply 163, 1-2), the firmware type loaded first, a unit taken only once booted
    # (Reply 163, 4); one control per rule
    CHECKS.append(check("0.8 driver settings: memory, order, Cancel, profiles, boot", [PY, "unified_settings_test.py"],
                        ok=lambda out: bool(re.search(r"^unified settings: all passed$", out, re.M))))
    for brk, marks in (("plain", [r"^FAIL ring_type_only ", r"^ok   profile_one_key ", r"^unified settings: 1 FAILED$"]),
                       ("sametype", [r"^FAIL profile_one_key ", r"^ok   memory ", r"^unified settings: 5 FAILED$"]),
                       ("cancel", [r"^FAIL cancel ", r"^ok   order ", r"^unified settings: 2 FAILED$"]),
                       ("ready", [r"^FAIL worker_boot ", r"^ok   boot_failure ", r"^unified settings: 1 FAILED$"]),
                       ("announce", [r"^FAIL ring_return ", r"^FAIL ring_startup ", r"^ok   ring_first_use ",
                                     r"^unified settings: 3 FAILED$"])):
        CHECKS.append(check("0.8 driver settings CONTROL (%s, must fail)" % brk, [PY, "unified_settings_test.py"],
                            env={"UNIFIED_SETTINGS_BREAK": brk}, expect_fail=True, fail_marks=marks))
    # the release archive's own check (build_ssi263.py verify_archive): clean as built, and a changed DLL, a missing
    # Mockingboard file, a changed driver and an added disk image each caught by name (addon_archive_test.py)
    CHECKS.append(check("0.8 archive check sees a changed, missing or added file", [PY, "addon_archive_test.py"],
                        ok=lambda out: bool(re.search(r"^addon archive: all passed$", out, re.M))))
    # its global plugin (unified_plugin_test.py): the manual update check and its SHA256SUMS check, the 0.7 add-ons'
    # settings migrated profile by profile, the Voice panel refreshed after a firmware change; one control per rule
    CHECKS.append(check("0.8 plugin: updates, migration, panel refresh", [PY, "unified_plugin_test.py"],
                        ok=lambda out: bool(re.search(r"^unified plugin: all passed$", out, re.M))))
    for brk, case, n in (("hash", "wrong_hash", 1), ("fill", "kept_existing", 1), ("ledger", "second_run", 1),
                         ("options", "stale_choices", 1), ("revert", "switch_failed", 2),
                         ("inherit", "inherited_accent", 1), ("rollback", "save_retry", 2), ("pending", "start_retry", 1),
                         ("ready", "manual_removal", 4), ("ringkey", "ring_key", 1),
                         ("version", "preview_ledger", 1), ("context", "late_profile", 1), ("active", "other_synth", 2)):
        CHECKS.append(check("0.8 plugin CONTROL (%s, must fail)" % brk, [PY, "unified_plugin_test.py"],
                            env={"UNIFIED_PLUGIN_BREAK": brk}, expect_fail=True,
                            fail_marks=[r"^FAIL %s " % case, r"^ok   newer ", r"^unified plugin: %d FAILED$" % n]))
# its must-fail control: 0.5.0's cancel put back on four seeds in parallel; it passes when a seed catches it (one seed
# alone missed it under this suite's load about 1 run in 6)
CHECKS.append(check("complete_fuzz CONTROL (0.5.0 cancel, must be caught)", [PY, "complete_fuzz_control.py", "150"]))
# the control's own guard: fake children that crash, mis-summarise, under-cover or hang must never count as a catch
CHECKS.append(check("complete_fuzz CONTROL guard", [PY, "complete_fuzz_control_guard.py"]))
# complete_fuzz's completion is owned (Astra, Reply 104): an utterance is complete at ITS OWN index and the done after
# it, never at the previous utterance's done.  Each control breaks that one way and must end as that error (exit 2),
# neither a pass nor an incomplete count; utterance 7 is past the five references, 0 is the first reference.  stale
# done = the old rule put back, with a done forged after each speak (a previous utterance's, landing late); timeout =
# the worker stuck inside the unit.
for what, env, marks in (
        ("stale done", {"COMPLETE_FUZZ_STALE": "1"},
         [r"^reference #0 'Yes, it works\.': CompletionError: STALE DONE accepted for index 900000: the done at "
          r"notification \d+ follows no owned index$", r"^complete_fuzz: COMPLETION NOT IDENTIFIED at reference #0 "]),
        ("missing index", {"COMPLETE_FUZZ_NO_INDEX": "7"},
         [r"CompletionError: MISSING INDEX: index 9000\d\d never reached, though \d+ done\(s\) came",
          r"^complete_fuzz: COMPLETION NOT IDENTIFIED at step \d+, #\d+ .*\(MISSING INDEX\)"]),
        ("missing done", {"COMPLETE_FUZZ_NO_DONE": "7"},
         [r"CompletionError: MISSING DONE: index 9000\d\d reached, no done after it",
          r"^complete_fuzz: COMPLETION NOT IDENTIFIED at step \d+, #\d+ .*\(MISSING DONE\)"]),
        ("reference: missing done", {"COMPLETE_FUZZ_NO_DONE": "0"},
         [r"^reference #0 'Yes, it works\.': CompletionError: MISSING DONE: index 900000 reached",
          r"^complete_fuzz: COMPLETION NOT IDENTIFIED at reference #0 "]),
        ("timeout", {"COMPLETE_FUZZ_HANG": "7"},
         [r"CompletionError: TIMEOUT: neither index 9000\d\d nor any done in 5\.0 s",
          r"^complete_fuzz: COMPLETION NOT IDENTIFIED at step \d+, #\d+ .*\(TIMEOUT\)"]),
        ("worker death", {"COMPLETE_FUZZ_KILL_WORKER": "7"},
         [r"CompletionError: WORKER DIED: the driver's worker thread",
          r"^complete_fuzz: COMPLETION NOT IDENTIFIED at step \d+.*\(WORKER DIED\)"])):
    CHECKS.append(check("complete_fuzz CONTROL (%s, must error)" % what, [PY, "complete_fuzz.py", "150", "1"],
                        env=dict({"SIM_SPEED": "10", "COMPLETE_FUZZ_WAIT_S": "5"}, **env), expect_fail=True,
                        fail_marks=marks, fail_codes=(2,)))
# the late check, apart from the phonemes at completion: the unit told idle at once from utterance 5, the Braille
# Lite's open channel speaks the rest after the done -- it must be counted late (exit 1), not pass as complete
CHECKS.append(check("complete_fuzz CONTROL (early done, must count late)", [PY, "complete_fuzz.py", "150", "1"],
                    env={"SIM_SPEED": "10", "COMPLETE_FUZZ_EARLY_DONE": "5"}, expect_fail=True,
                    fail_marks=[r"^#\d+ '.*': LATE, \d+ phonemes loaded after its completion: ",
                                r"^\d+ finished utterances checked, \d+ incomplete, [1-9]\d* late$"]))
# ... and the positive half: the same forged dones under the owned rule change nothing
CHECKS.append(check("complete_fuzz, forged dones (owned rule)", [PY, "complete_fuzz.py", "150", "1"],
                    env={"SIM_SPEED": "10", "COMPLETE_FUZZ_FORGE_DONE": "1"}))
# driver_sim.py's own copy of the rule, the same way: the old rule and forged dones put back must fail (every scenario
# then ends at once, with no audio), not pass
CHECKS.append(check("driver_sim CONTROL (stale done, must fail)", [PY, "-S", "driver_sim.py", "speakout", NVDA, "rt"],
                    env={"DRIVER_SIM_STALE": "1"}, expect_fail=True,
                    fail_marks=[r"^sample rate: .*11025 0\.00 s rms 0.*: FAILED$", r"^speakout rt: .*all ok False"]))
# the Accent-mini on MAME's 8086 core (its default since 0.7, named here explicitly; not gated before Reply 104
# because the old rule failed it on seed 3)
PC86_FUZZ = os.path.join(os.path.dirname(HERE), "dist", "blazie-lib", "x64", "pc86.dll")
if os.path.isfile(PC86_FUZZ):
    CHECKS.append(check("complete_fuzz accent on the MAME 8086 core", [PY, "complete_fuzz.py", "150", "3"],
                        env={"SIM_SPEED": "10", "COMPLETE_FUZZ_SYNTH": "accent", "SSI263_ACCENT_CORE": "mame",
                             "SSI263_PC86_DLL": PC86_FUZZ}))
# the premature completion replayed (complete_fuzz seed 4's miscount; on MAME the seed-4 record no longer makes it, so
# the replay is golden/premature_history_mame.jsonl, the same miscount found on MAME: premature_record.py): the cut
# line must be spoken whole on both hosts; with 0.6.0's busy() put back it must end at its comma again
CHECKS.append(check("premature completion replay", [PY, "premature_replay.py"]))
CHECKS.append(check("premature completion replay CONTROL (0.6.0 busy, must be cut)", [PY, "premature_replay.py"],
                    env={"PREMATURE_REPLAY_OLD": "1"}, expect_fail=True,
                    fail_marks=[r"^FAIL +native +30 ms blocks +CUT 3 of 13 phonemes at done, 8 MORE after: W UH1 N$",
                                r"^FAIL +pipe +30 ms blocks +CUT 3 of 13 phonemes at done, 8 MORE after: W UH1 N$",
                                r"^premature replay: 2 FAILED$"]))
# ... and a busy() that is never false must fail every case, not pass on the phonemes it collected (Astra, Reply 100)
CHECKS.append(check("premature completion replay CONTROL (never done, must fail)", [PY, "premature_replay.py"],
                    env={"PREMATURE_REPLAY_NEVER": "1"}, expect_fail=True,
                    fail_marks=[r"^FAIL +native +30 ms blocks +NEVER DONE", r"^FAIL +pipe +0.5 ms polling +NEVER DONE",
                                r"^premature replay: 6 FAILED$"]))
# ... and clean runs that never complete must not feed the latency comparison or the two-line reference (Astra, 101)
CHECKS.append(check("premature completion replay CONTROL (clean never done, must fail)", [PY, "premature_replay.py"],
                    env={"PREMATURE_REPLAY_NEVER": "clean"}, expect_fail=True,
                    fail_marks=[r"^FAIL +native +a clean utterance NEVER DONE",
                                r"^FAIL +pipe +the two-line reference NEVER DONE", r"^premature replay: 4 FAILED$"]))
# and this runner's own must-fail judgement: silent exits, native crashes, tracebacks, missing marks never count
CHECKS.append(check("run_tests control judgement", [PY, "run_tests_guard.py"]))
CHECKS.append(check("slider_fuzz", [PY, "slider_fuzz.py", "150", "7"], env={"SIM_SPEED": "10"}))
CHECKS.append(check("cut_test", [PY, "cut_test.py"], env={"CUTS": "0.1", "CUT_REPS": "2"},
                    ok=lambda out: re.search(r"tail bug in 0 of", out) is not None))
# Every voice on every platform (Tomi: a green suite that hides a broken integration is worse than a red one -- 0.7.0
# passed while Linux and Android lacked three voices): voices.c's voices, by id, on NVDA (the built add-ons' drivers),
# SAPI (the stage's voices.txt and ssi263speech.dll), Linux (sd_voices.c and build_linux.sh's engines; the binary on
# Linux, tools/linux_tests.sh) and Android (SsiEngine.kt through ssa_engine.c and build_android.sh's engines), and the
# Braille Lite's run ahead and number words on each.  Every platform is required here: the add-ons and the SAPI stage
# are built for this suite, the Linux and Android sides read from their sources.  Each control drops one cell from one
# platform's discovered list and must fail naming exactly that cell.
UNIFORM = [PY, os.path.join(os.path.dirname(os.path.dirname(HERE)), "tools", "check_uniform.py"),
           "--require", "nvda,sapi,linux,android"]
CHECKS.append(check("uniform: every voice on every platform", UNIFORM))
CHECKS.append(check("uniform CONTROL (Android lacks the Speak-Out, must fail)", UNIFORM,
                    env={"SSI263_UNIFORM_DROP": "android:speakout:speakout"}, expect_fail=True,
                    fail_marks=[r"^Speak-Out +yes +yes +yes +NO +$",
                                r"^MISSING  Speak-Out \[speakout:speakout\] on Android$",
                                r"^ok +NVDA: (\d+) of \1 voices", r"^gap +Android: (\d+) of (?!\1)\d+ voices",
                                r"^uniform: 1 gap$"]))
CHECKS.append(check("uniform CONTROL (Linux lacks the Spanish number words, must fail)", UNIFORM,
                    env={"SSI263_UNIFORM_DROP": "linux:blazie:blazie_es:numbers"}, expect_fail=True,
                    fail_marks=[r"^  number words: Braille Lite 2000 \(espa.ol\) +yes +yes +NO +yes +$",
                                r"^MISSING  number words for Braille Lite 2000 \(espa.ol\) \[blazie:blazie_es\] on "
                                r"Linux: dropped by SSI263_UNIFORM_DROP$", r"^uniform: 1 gap$"]))
SAPI_SERVE_TEST = os.path.join(os.path.dirname(os.path.dirname(HERE)), "sapi", "test_serve.py")
CHECKS.append(check("SAPI pipe server", [PY, SAPI_SERVE_TEST]))
# The three SAPI tests' reference server runs 0.7.0's Python drivers over this tree's chip (legacy_drivers.py: the
# old-driver/current-chip integration reference), not nvda/dist's, which since
# 0.7.5 are the native ones (Astra, Reply 141); each test checks that first (sapi/reference_drivers.py).  Pointed at
# nvda/dist, each must fail there, on the modules and the native library it then loads.
REF_MARK = r"^FAIL reference: NOT 0\.7\.0's Python drivers: .*modules not from the legacy folder.*ssi263speech\.dll"
CHECKS.append(check("SAPI pipe server CONTROL (on nvda/dist's native drivers, must fail)",
                    [PY, SAPI_SERVE_TEST, "--run-ahead-only"], env={"SSI263_SAPI_REF_BREAK": "dist"}, expect_fail=True,
                    fail_marks=[REF_MARK, r"^serve: 1 FAILED$"]))
# its run-ahead checks (the dialog's "Run the unit ahead"): the setting dropped on its way to the driver must fail the
# "reaches the unit" and "changes the audio" checks; the unit never cancelled must fail the cancel checks (the
# cancelled text runs on into the next line)
if os.path.isdir(os.path.join(os.path.dirname(HERE), "dist", "blazie-build")):
    CHECKS.append(check("SAPI pipe server run ahead CONTROL (the setting dropped, must fail)",
                        [PY, SAPI_SERVE_TEST, "--run-ahead-only"], env={"SSI263_SAPI_IGNORE_RUN_AHEAD": "1"},
                        expect_fail=True,
                        fail_marks=[r"^FAIL run ahead reaches the Braille Lite unit: run_ahead 0 at its says",
                                    r"^FAIL blazie:blazie +run ahead changes the audio",
                                    r"^FAIL blazie:blazie_es +run ahead changes the audio",
                                    r"^serve \(run ahead only\): 3 FAILED$"]))
    CHECKS.append(check("SAPI pipe server run ahead CONTROL (the unit never cancelled, must fail)",
                        [PY, SAPI_SERVE_TEST, "--run-ahead-only"], env={"SSI263_SAPI_NO_UNIT_CANCEL": "1"},
                        expect_fail=True,
                        fail_marks=[r"^FAIL blazie:blazie +run ahead, cancel .* its phonemes NOT its own",
                                    r"^FAIL blazie:blazie_es +run ahead, cancel .* its phonemes NOT its own",
                                    # 2 or more: how much of the cancelled line runs on depends on the load, so its
                                    # phonemes with and without run ahead can differ too
                                    r"^serve \(run ahead only\): \d+ FAILED$"]))
# The SAPI engine's native voices (0.7.5: no Python): ssi263speech.dll (src/csrc/voices.h) through the native serve
# host, which maps settings with the SAPI DLL's own code (sapi/ssi_native.c), against the pipe server above -- the
# 0.7.0 engine -- byte for byte over the same wire, 64- and 32-bit: texts with numbers and money, SAPI's rates and
# pitches, the voices in turn, the dialog's settings, every sample rate, a cancel and the utterance after it, the
# Braille Lite's number words with no value, on and off; the Mockingboard, which 0.7.0 never had, held to itself (both
# widths byte-identical, voiced, its rates, pitches, cancel and sample rates, the dialog leaving it alone).  Its
# controls: the dialog's settings dropped, English and Spanish swapped, the drivers' number words off, the Braille
# Lite's number-words setting ignored, the Mockingboard's firmware missing.
SAPI_NATIVE = os.path.join(os.path.dirname(os.path.dirname(HERE)), "sapi", "test_native.py")
if os.path.isfile(os.path.join(os.path.dirname(os.path.dirname(HERE)), "build", "win", "x86", "ssi263_serve.exe")):
    CHECKS.append(check("SAPI native voices = the Python server, byte for byte", [PY, SAPI_NATIVE]))
    CHECKS.append(check("SAPI native voices CONTROL (the reference on nvda/dist's native drivers, must fail)",
                        [PY, SAPI_NATIVE, "--only", "rate11", "--arch", "x64"], env={"SSI263_SAPI_REF_BREAK": "dist"},
                        expect_fail=True, fail_marks=[REF_MARK, r"^native: 1 FAILED$"]))
    CHECKS.append(check("SAPI native voices CONTROL (the dialog's settings dropped, must fail)",
                        [PY, SAPI_NATIVE, "--only", "dialog", "--arch", "x64"], env={"SSI263_SERVE_BREAK": "setting"},
                        expect_fail=True,
                        fail_marks=[r"^DIFF dialog  x64 blazie:blazie text 0 ", r"^DIFF dialog  x64 blazie:blazie_es text 0 ",
                                    r"^DIFF dialog  x64 accentmini:sa text 0 ", r"^native: \d+ FAILED$"]))
    CHECKS.append(check("SAPI native voices CONTROL (English and Spanish swapped, must fail)",
                        [PY, SAPI_NATIVE, "--only", "default", "--arch", "x64"], env={"SSI263_SERVE_BREAK": "voice"},
                        expect_fail=True,
                        fail_marks=[r"^DIFF default x64 blazie:blazie text 0 ", r"^DIFF default x64 blazie:blazie_es text 0 ",
                                    r"^native: \d+ FAILED$"]))
    CHECKS.append(check("SAPI native voices CONTROL (the number words off, must fail)",
                        [PY, SAPI_NATIVE, "--only", "default", "--arch", "x64"], env={"SSI263_SERVE_BREAK": "numbers"},
                        expect_fail=True,
                        fail_marks=[r"^DIFF default x64 blazie:blazie text 1 ", r"^DIFF default x64 blazie:blazie_es text 1 ",
                                    r"^DIFF default x64 accentmini:sa text 1 ", r"^native: \d+ FAILED$"]))
    # the dialog's "Read numbers as words" (BrailleLiteNumbers 0, --bl-numbers 0) ignored by the native host: both
    # Braille Lite voices keep their number words and differ from the reference with them off; the Accent stays the same
    CHECKS.append(check("SAPI native voices CONTROL (BrailleLiteNumbers ignored, must fail)",
                        [PY, SAPI_NATIVE, "--only", "blnum0", "--arch", "x64"], env={"SSI263_SERVE_BREAK": "bl-numbers"},
                        expect_fail=True,
                        fail_marks=[r"^DIFF blnum0  x64 blazie:blazie text 4 ", r"^DIFF blnum0  x64 blazie:blazie_es text 2 ",
                                    r"^native: 1 of 3 utterances byte-identical", r"^native: 2 FAILED$"]))
    # every voice listed on both widths, the Mockingboard (no Python reference: held to itself, test_native's "own")
    # among them: its firmware left out of a copy of the folder, the listing must fail naming it on each width -- a
    # request for it would otherwise quietly speak the first voice there
    CHECKS.append(check("SAPI native voices CONTROL (the Mockingboard's firmware missing, must fail)",
                        [PY, SAPI_NATIVE, "--only", "own"], env={"SSI263_NATIVE_WITHOUT": "mockingboard:mockingboard"},
                        expect_fail=True,
                        fail_marks=[r"^FAIL listing x64: mockingboard:mockingboard is not offered ",
                                    r"^FAIL listing x86: mockingboard:mockingboard is not offered ",
                                    r"; native only: mockingboard:early$", r"^native: 2 FAILED$"]))
    # ... and the early one's (disk 1's text-to-speech, mockingboard-tts-early.bin): named alone, 1.1 still checked
    CHECKS.append(check("SAPI native voices CONTROL (the early Mockingboard's firmware missing, must fail)",
                        [PY, SAPI_NATIVE, "--only", "own"], env={"SSI263_NATIVE_WITHOUT": "mockingboard:early"},
                        expect_fail=True,
                        fail_marks=[r"^FAIL listing x64: mockingboard:early is not offered ",
                                    r"^FAIL listing x86: mockingboard:early is not offered ",
                                    r"; native only: mockingboard:mockingboard$",
                                    r"^native only mockingboard:mockingboard: (\d+) of \1 checks right",
                                    r"^native: 2 FAILED$"]))
    # the number words alone, on random texts: the driver's _numbers against bl_numbers (English, and Spain's Spanish)
    CHECKS.append(check("bl_voice number words = the driver's, 5000 random texts",
                        [PY, "voice_text_equiv.py", "5000", "1", "--numbers"]))
    CHECKS.append(check("bl_voice number words CONTROL (the driver's _numbers skipped, must fail)",
                        [PY, "voice_text_equiv.py", "2000", "1", "--numbers"], env={"VOICE_TEXT_BREAK": "numbers"},
                        expect_fail=True,
                        fail_marks=[r"^DIFF ", r"^(?!2000 )\d+ of 2000 texts give the unit the same bytes"]))
# ... and the SAPI engine DLL itself, driven through SAPI's own interface (ISpTTSEngine, an engine site of the test's)
# with nothing registered (sapi\build.ps1 -Dev's harness): every case byte-identical to the Python server, the
# declared rate, bookmarks, SAPI's abort, BrailleLiteNumbers with no value, 1 and 0 (the Mockingboard against the
# native host instead); its controls (English and Spanish swapped, the Mockingboard spoken by the Braille Lite, the
# dialog's settings dropped, BrailleLiteNumbers 0 ignored) run in the same check and must be caught
SAPI_ENGINE = os.path.join(os.path.dirname(os.path.dirname(HERE)), "sapi", "test_sapi_engine.py")
if os.path.isfile(os.path.join(os.path.dirname(HERE), "dist", "sapi-dev", "x86", "sapi_harness.exe")):
    CHECKS.append(check("SAPI engine DLL = the Python server, through SAPI's interface", [PY, SAPI_ENGINE]))
    CHECKS.append(check("SAPI engine DLL CONTROL (the reference on nvda/dist's native drivers, must fail)",
                        [PY, SAPI_ENGINE, "--arch", "x64"], env={"SSI263_SAPI_REF_BREAK": "dist"},
                        expect_fail=True, fail_marks=[REF_MARK, r"^sapi engine: 1 FAILED$"]))
# the golden vectors: every SSI-263 write with its time, the serial output and the audio hash of a fixed scenario, on
# MAME's Z180 (0.7).  Two hosts, two baselines: the in-process host (bl.dll, the add-on's) has the lockstep cancel
# protection on by default (blazie_{en,es}.txt, Astra's Replies 124-128); the pipe host (bns_live.exe / bl_live.exe,
# the fallback) has its own cancel path without it (blazie_{en,es}_pipe.txt, Astra's Reply 123).  The z180emu
# signatures are kept as blazie_{en,es}_legacy.txt, a reference only.
BNS = os.path.join(os.path.dirname(HERE), "dist", "blazie-build", "synthDrivers", "_ssi263_blazie", "bns_live.exe")
for lang in ("en", "es"):
    CHECKS.append(check("golden Braille Lite, pipe host (%s)" % lang, [PY, "bns_equiv.py", BNS,
                        "--against=" + os.path.join(HERE, "golden", "blazie_%s_pipe.txt" % lang)] + (["--es"] if lang == "es" else [])))
# 0.7's library board (src/csrc/blazie): the same golden vectors through bl_live.exe, and two units in one process
LIB = os.path.join(os.path.dirname(HERE), "dist", "blazie-lib")
ENG = os.path.dirname(BNS)
if os.path.isfile(os.path.join(LIB, "bl_live.exe")):
    for lang in ("en", "es"):
        CHECKS.append(check("library board golden, pipe host (%s)" % lang, [PY, "bns_equiv.py", os.path.join(LIB, "bl_live.exe"),
                            "--against=" + os.path.join(HERE, "golden", "blazie_%s_pipe.txt" % lang)] + (["--es"] if lang == "es" else [])))
    # the in-process host (bl.dll + hosts/native_blazie.py): the golden vectors bit for bit, 64-bit and 32-bit
    for arch, py in (("x64", PY), ("x86", PY37)):
        dll = os.path.join(LIB, arch, "bl.dll")
        if os.path.isfile(dll) and os.path.isfile(py):
            for lang in ("en", "es"):
                CHECKS.append(check("in-process host %s golden (%s)" % (arch, lang), [py, "bns_equiv.py", dll, "--native",
                                    "--against=" + os.path.join(HERE, "golden", "blazie_%s.txt" % lang)] + (["--es"] if lang == "es" else [])))
    CHECKS.append(check("library board: two units in one process", [os.path.join(LIB, "test_bl_board.exe"),
                        os.path.join(ENG, "BL2ENG.BNS"), os.path.join(ENG, "bl2_2003_warm.state"),
                        os.path.join(ENG, "BL2SPA.BNS"), os.path.join(ENG, "bl2spa_fresh.state")]))
    # CONTRACT.md 3: the legacy path's own exceptions (src/csrc/cpu/test_z180_legacy.c) -- a development reference
    # since 0.7, built only by build_board.py --legacy-tests (with a z180emu checkout): run when it is there
    if os.path.isfile(os.path.join(LIB, "test_z180_legacy.exe")):
        CHECKS.append(check("z180emu legacy path: its exceptions (development reference)",
                            [os.path.join(LIB, "test_z180_legacy.exe")]))
# the Blazie emulator app (src/apps/blazie): its chord logic, and the unit headless (boot greeting heard, a chord
# answered against the no-chord control, faster than real time)
EMU = os.path.join(os.path.dirname(HERE), "dist", "blazie-emu")
if os.path.isfile(os.path.join(EMU, "test_emu_unit.exe")):
    CHECKS.append(check("Blazie emulator: chords", [os.path.join(EMU, "test_chords.exe")]))
    # its sound queue (src/apps/blazie/audio_pace.c; Tomi: the emulator's speech stutters, the add-on's doesn't)
    # against a simulated sound card calibrated on this desktop's waveOut: a busy machine and a remote card heard
    # without gaps once the automatic queue has grown, a slow save without gaps; the control puts the 0.7.0 draft's
    # queue back (four blocks of 10 ms, the save on the window's thread) and must fail on exactly those -- for the
    # Windows shell's sound thread and for the Linux shells' (audio_linux.c, asking the card how much is queued)
    if os.path.isfile(os.path.join(EMU, "test_audio.exe")):
        CHECKS.append(check("Blazie emulator: the sound queue", [os.path.join(EMU, "test_audio.exe")]))
        CHECKS.append(check("Blazie emulator: the sound queue CONTROL (the 0.7.0 draft's, must fail)",
                            [os.path.join(EMU, "test_audio.exe"), "--old"], expect_fail=True,
                            fail_marks=[r"^FAIL busy machine: no gap after 10 s +\d{2,} gaps after 10 s",
                                        r"^FAIL remote card: no gap after 10 s +\d{2,} gaps after 10 s",
                                        r"^FAIL autosave: no gap +\d+ saves of 50 ms: [1-9]\d* gaps",
                                        r"^ok +steady card: no gap after the first second",
                                        r"^FAIL linux: busy machine: no gap after 10 s +\d{2,} gaps after 10 s",
                                        r"^FAIL linux: autosave: no gap +\d+ saves of 50 ms: [1-9]\d* gaps",
                                        r"^ok +linux: steady card: no gap after the first second",
                                        r"^ok +card: the played position from the delay",
                                        r"^audio: 14 FAILED$"]))
    CHECKS.append(check("Blazie emulator: the unit, headless", [os.path.join(EMU, "test_emu_unit.exe"), "bl",
                        os.path.join(ENG, "BL2ENG.BNS"), os.path.join(ENG, "bl2_2003_warm.state")]))
    CHECKS.append(check("Blazie emulator: the Spanish unit, headless", [os.path.join(EMU, "test_emu_unit.exe"), "bl",
                        os.path.join(ENG, "BL2SPA.BNS"), os.path.join(ENG, "bl2spa_fresh.state")]))
    # MIT since 0.7 (Tomi: the emulator MIT): the program, its tests and their objects on MAME's Z180, no z180emu;
    # the audit's must-fail control is the no-GPL audit CONTROL below (and a z180emu build of this folder fails it)
    CHECKS.append(check("Blazie emulator: no z180emu, no GPL (tools/check_no_gpl.py)",
                        [PY, os.path.join(os.path.dirname(os.path.dirname(HERE)), "tools", "check_no_gpl.py"), EMU],
                        ok=lambda out: bool(re.search(r"^ok +blazie-emu: no z180emu or Unicorn engine, no GPL notice "
                                                      r"\((\d{2,}) files searched\)$", out, re.M))))
    # its idle channel against Tomi's unit (src/csrc/blazie/bl_idle.c, src/hosts/blazie_idle.py): the noise's level at
    # every volume, keep open off/until/always, the pop after a click-off and none before, the click, the 10 Hz tick;
    # each control puts one bug back and must fail on exactly the checks that see it
    IDLE = [os.path.join(EMU, "test_idle.exe"), os.path.join(ENG, "BL2ENG.BNS"), os.path.join(ENG, "bl2_2003_warm.state")]
    CHECKS.append(check("Blazie emulator: the idle channel against the unit", IDLE))
    for brk, marks in (
            ("old-level", [r"^FAIL hiss level at volume 6 +-6\d\.\d dB", r"^idle sounds: 3 FAILED$"]),
            ("follow-volume", [r"^FAIL hiss the same at volume 1 and 15 ", r"^ok +hiss level at volume 6 ",
                               r"^idle sounds: 1 FAILED$"]),
            ("until", [r"^FAIL always: open 12 s after speech ", r"^FAIL off: silent after speech ",
                       r"^idle sounds: 2 FAILED$"]),
            ("no-pop", [r"^FAIL pop on reopening after the click-off ", r"^FAIL click at the click-off ",
                        r"^idle sounds: 2 FAILED$"]),
            ("pop-every-line", [r"^FAIL no pop on reopening before the click-off ", r"^idle sounds: 1 FAILED$"]),
            ("no-tick", [r"^FAIL the 10 Hz tick while open, none after +folded at 99\.98 ms: 0\.0 x",
                         r"^idle sounds: 1 FAILED$"]),
            ("tick-after-off", [r"^FAIL the 10 Hz tick while open, none after .*after the click-off -\d",
                                r"^idle sounds: 1 FAILED$"])):
        CHECKS.append(check("Blazie emulator: idle channel CONTROL (%s, must fail)" % brk, IDLE + ["--break=" + brk],
                            expect_fail=True, fail_marks=marks))
    # the Type 'n Speak from cold (firmware/blazie/tns/, when the builder has it): its cold reset's first question
    # heard, y answered; from its factory setup (its seven questions answered) a key's latency and the options menu;
    # its memory kept
    TNS_DIR = os.path.join(os.path.dirname(os.path.dirname(HERE)), "firmware", "blazie", "tns")
    for name in ("TNSENG.TNS", "TNSSPA.TNS"):
        if os.path.isfile(os.path.join(TNS_DIR, name)):
            CHECKS.append(check("Blazie emulator: Type 'n Speak %s, headless" % name[3:6],
                                [os.path.join(EMU, "test_emu_unit.exe"), "tns", os.path.join(TNS_DIR, name), "-"]))
# the units' clock controller (src/csrc/blazie/bl_clock.c; Jayson: the clock asked to be reset, did not move, lost
# the year) and keys held while the unit starts (Jayson: i-chord held through p-chord l's restart): the controller
# alone; the English Braille Lite and Type 'n Speak setting, reading and keeping time through their own commands; and
# the controls, each with one bug put back (a clock that never moves, year fields dropped, the clock left out of the
# saved state; keys never reported held, a chord read at the start sent again)
CLOCK = os.path.join(EMU, "test_clock.exe")
if os.path.isfile(CLOCK):
    BL_ENG = [os.path.join(ENG, "BL2ENG.BNS"), os.path.join(ENG, "bl2_2003_warm.state")]
    CHECKS.append(check("Blazie emulator: clock controller", [CLOCK, "unit"]))
    CHECKS.append(check("Blazie emulator: clock and held keys, Braille Lite ENG", [CLOCK, "bl"] + BL_ENG))
    CHECKS.append(check("Blazie emulator: clock, Braille Lite SPA", [CLOCK, "bl", os.path.join(ENG, "BL2SPA.BNS"),
                                                                     os.path.join(ENG, "bl2spa_fresh.state"), "start"]))
    TNS_ENG = os.path.join(os.path.dirname(os.path.dirname(HERE)), "firmware", "blazie", "tns", "TNSENG.TNS")
    if os.path.isfile(TNS_ENG):
        CHECKS.append(check("Blazie emulator: clock, Type 'n Speak ENG", [CLOCK, "tns", TNS_ENG]))
    for brk, marks in (("1", [r"^FAIL the clock goes on +a minute on: 12:34:00", r"^FAIL switched off and on "]),
                       ("2", [r"^FAIL set through the unit's commands +the clock holds 2012-09-30",
                              r"^FAIL the year read back ", r"^ok +the clock goes on "]),
                       ("3", [r"^ok +the clock goes on ", r"^FAIL switched off and on .*12:37: no$"])):
        CHECKS.append(check("Blazie emulator: clock CONTROL (break %s, must fail)" % brk, [CLOCK, "bl"] + BL_ENG
                            + ["clock"], env={"TEST_CLOCK_BREAK": brk}, expect_fail=True, fail_marks=marks))
    CHECKS.append(check("Blazie emulator: held keys CONTROL (never held, must fail)", [CLOCK, "bl"] + BL_ENG
                        + ["restart"], env={"TEST_CLOCK_HOLD_BREAK": "1"}, expect_fail=True,
                        fail_marks=[r"^FAIL i-chord held through the restart "]))
    CHECKS.append(check("Blazie emulator: held keys CONTROL (sent again, must fail)", [CLOCK, "bl"] + BL_ENG
                        + ["restart"], env={"TEST_CLOCK_HOLD_BREAK": "2"}, expect_fail=True,
                        fail_marks=[r"^ok +i-chord held through the restart ", r"^FAIL the held chord is not sent again "]))
    CHECKS.append(check("Blazie emulator: held keys CONTROL (quick response through the restart, must fail)",
                        [CLOCK, "bl"] + BL_ENG + ["restart"], env={"TEST_CLOCK_HOLD_BREAK": "3"}, expect_fail=True,
                        fail_marks=[r"^ok +i-chord held through the restart ",
                                    r"^FAIL held through the restart, quick "]))
    CHECKS.append(check("Blazie emulator: clock controller CONTROL (never moves, must fail)", [CLOCK, "unit"],
                        env={"TEST_CLOCK_BREAK": "1"}, expect_fail=True,
                        fail_marks=[r"^FAIL time passes \(a leap day\) ", r"^ok +set and read over its bytes "]))
    # Jayson's issue #3: the year 28 back, so the controller's own New Years keep the host's calendar; units saved on
    # 0.7.0-0.7.5's year moved.  The control puts that mapping back (2026 -> 2015): 1 March 2027 is 29 February 2016
    CHECKS.append(check("Blazie emulator: clock year CONTROL (0.7.5's mapping, must fail)", [CLOCK, "unit"],
                        env={"TEST_CLOCK_BREAK": "4"}, expect_fail=True,
                        fail_marks=[r"^FAIL the weekday after New Year +2016-02-29",
                                    r"^FAIL a unit saved on the old year ", r"^ok +a new year "]))
    CHECKS.append(check("Blazie emulator: clock year CONTROL, Braille Lite ENG (0.7.5's mapping, must fail)",
                        [CLOCK, "bl"] + BL_ENG + ["year"], env={"TEST_CLOCK_BREAK": "4"}, expect_fail=True,
                        fail_marks=[r"^FAIL switched off on New Year's Eve 2026 ",
                                    r"^FAIL saved by 0.7.5 in September 2026 "]))
    # ... and a migrated unit's dated alarm moves with its clock (Astra, Reply 155); the control leaves it on 2015
    CHECKS.append(check("Blazie emulator: clock alarm CONTROL (a dated alarm left on the old year, must fail)",
                        [CLOCK, "unit"], env={"TEST_CLOCK_BREAK": "5"}, expect_fail=True,
                        fail_marks=[r"^FAIL a migrated unit's alarm +dated alarm \(Astra's case\): 2015-10-03, silent",
                                    r"^ok +a unit saved on the old year "]))
    # the file flash (src/apps/blazie/test_flash.c; Jayson, Timothy): the Type 'n Speak's ID check passes and its flash
    # is initialised, the erase takes a 29F016's 32 s plus its 14.4 s preprogramming (every byte to 00h first) with the
    # firmware's chirps through the chip, the initialised flash
    # kept across a save and restart; the Braille Lite's reset erases with the same chirps, and a file moved to flash
    # lands in the 2 MB chip the firmware manages and survives a restart.  Controls: each must fail on its checks.
    FLASH = os.path.join(EMU, "test_flash.exe")
    if os.path.isfile(FLASH):
        BL_FLASH = ["bl", os.path.join(ENG, "BL2ENG.BNS"), os.path.join(ENG, "bl2_2003_warm.state")]
        CHECKS.append(check("Blazie emulator: file flash, Braille Lite", [FLASH] + BL_FLASH))
        CHECKS.append(check("Blazie emulator: file flash CONTROL (Braille Lite, erase at once, must fail)",
                            [FLASH] + BL_FLASH + ["--break=instant"], expect_fail=True,
                            fail_marks=[r"^FAIL Braille Lite reset: erase and chirps .* ran 0\.0 s.* 0 chirps",
                                        r"^ok +a flash file in the fourth 512 KB", r"^FAILED$"]))
        CHECKS.append(check("Blazie emulator: file flash CONTROL (Braille Lite, banks on 512 KB, must fail)",
                            [os.path.join(EMU, "test_flash_break.exe")] + BL_FLASH, expect_fail=True,
                            fail_marks=[r"^ok +Braille Lite reset: erase and chirps",
                                        r"^FAIL a flash file in the fourth 512 KB +top of the 2 MB: FF FF",
                                        r"^FAIL the flash file kept across a restart +saved 786432 bytes", r"^FAILED$"]))
        # the erase without its embedded preprogramming (flash29.c FLASH29_NO_PREPROGRAM: 32 s and 18 chirps, as
        # before; users: the flash initialization beeps)
        CHECKS.append(check("Blazie emulator: file flash CONTROL (Braille Lite, erase without preprogramming, must fail)",
                            [os.path.join(EMU, "test_flash_old.exe")] + BL_FLASH, expect_fail=True,
                            fail_marks=[r"^FAIL Braille Lite reset: erase and chirps .* ran 3[12]\.\d s.* 18 chirps",
                                        r"^ok +a flash file in the fourth 512 KB", r"^FAILED$"]))
        for name in ("TNSENG.TNS", "TNSSPA.TNS"):
            if os.path.isfile(os.path.join(TNS_DIR, name)):
                CHECKS.append(check("Blazie emulator: file flash, Type 'n Speak %s" % name[3:6],
                                    [FLASH, "tns", os.path.join(TNS_DIR, name)]))
        TNS_FLASH = ["tns", os.path.join(TNS_DIR, "TNSENG.TNS")]
        if os.path.isfile(TNS_FLASH[1]):
            CHECKS.append(check("Blazie emulator: file flash CONTROL (Type 'n Speak, erase at once, must fail)",
                                [FLASH] + TNS_FLASH + ["--break=instant"], expect_fail=True,
                                fail_marks=[r"^ok +flash ID accepted", r"^FAIL erase takes the chip's time +the erase ran 0\.0",
                                            r"^FAIL erase chirps +0 sounds", r"^FAILED$"]))
            CHECKS.append(check("Blazie emulator: file flash CONTROL (Type 'n Speak, saved before initialising, must fail)",
                                [FLASH] + TNS_FLASH + ["--break=persist"], expect_fail=True,
                                fail_marks=[r"^ok +erase chirps", r"^FAIL flash kept across save and restart .* 1 chip erases",
                                            r"^FAILED$"]))
            CHECKS.append(check("Blazie emulator: file flash CONTROL (Type 'n Speak, a 29F040's ID, must fail)",
                                [os.path.join(EMU, "test_flash_break.exe")] + TNS_FLASH, expect_fail=True,
                                fail_marks=[r"^FAIL flash ID accepted, flash initialised +0 chip erase", r"^FAILED$"]))
            CHECKS.append(check("Blazie emulator: file flash CONTROL (Type 'n Speak, erase without preprogramming, "
                                "must fail)", [os.path.join(EMU, "test_flash_old.exe")] + TNS_FLASH, expect_fail=True,
                                fail_marks=[r"^ok +flash ID accepted", r"^FAIL erase takes the chip's time +the erase ran "
                                            r"3[12]\.\d s", r"^ok +flash kept across save and restart", r"^FAILED$"]))
# files in and out of the units without the serial cable (src/apps/blazie/test_files.c, src/csrc/blazie/bl_files.c;
# Tomi, for Jage and Jayson): files the firmware made with its own commands exported exactly (the open file with the
# text typed since it was opened); an image imported (RAM, flash, a new folder, PC line ends, a file before the open
# one dropped) and then listed with sizes, typed into and moved RAM<->flash by the unit's own commands, byte for
# byte, its flash allocator agreeing with our free space; export-import-export the same image, on the unit and on a
# fresh one.  Every unit (English and Spanish).  Controls, each one bug put back: 1 the open file's live pointers
# ignored, 2 new flash blocks not marked used, 3 a RAM file's end of text one short, 4 dates dropped, 5 the open
# file's number not followed when an earlier file goes, 6 a binary file's line ends converted (issue #16: a compiled
# BASIC program's 0Ah bytes made 0Dh, so it stopped after its first statement).
FILES = os.path.join(EMU, "test_files.exe")
FW_BLAZIE = os.path.join(os.path.dirname(os.path.dirname(HERE)), "firmware", "blazie")
if os.path.isfile(FILES):
    FILES_BL = [FILES, "bl", os.path.join(FW_BLAZIE, "BL2ENG.BNS"), os.path.join(FW_BLAZIE, "bl2_2003_warm.state")]
    CHECKS.append(check("Blazie emulator: files in and out, Braille Lite ENG", FILES_BL))
    CHECKS.append(check("Blazie emulator: files in and out, Braille Lite SPA",
                        [FILES, "bl", os.path.join(FW_BLAZIE, "spanish", "BL2SPA.BNS"),
                         os.path.join(FW_BLAZIE, "spanish", "bl2spa_fresh.state")]))
    for name in ("TNSENG.TNS", "TNSSPA.TNS"):
        if os.path.isfile(os.path.join(FW_BLAZIE, "tns", name)):
            CHECKS.append(check("Blazie emulator: files in and out, Type 'n Speak %s" % name[3:6],
                                [FILES, "tns", os.path.join(FW_BLAZIE, "tns", name)]))
    for brk, marks in (
            ("1", [r"^ok +export: a file the unit made", r"^FAIL export: the open file, typed into since opened .*\"abc\"",
                   r"^FAILED$"]),
            ("2", [r"^FAIL import: done, the file system's rules hold .*not marked used", r"^FAILED$"]),
            ("3", [r"^FAIL the unit lists the imported files, with sizes .*imported 23 not listed",
                   r"^FAIL the unit reads an imported RAM file \(moved to flash\)", r"^FAILED$"]),
            ("4", [r"^ok +export, import, export: the same image",
                   r"^FAIL export, import into a fresh unit, export: the same", r"^FAILED$"]),
            ("5", [r"^ok +import: done", r"^FAIL the open file goes on after the import", r"^FAILED$"]),
            ("6", [r"^ok +import: done", r"^FAIL import: a binary file byte for byte .*differs at byte 1: 0D, not 0A",
                   r"^FAILED$"])):
        CHECKS.append(check("Blazie emulator: files CONTROL (break %s, must fail)" % brk, FILES_BL + ["--break=" + brk],
                            expect_fail=True, fail_marks=marks))
    if os.path.isfile(os.path.join(FW_BLAZIE, "tns", "TNSENG.TNS")):   # the Type 'n Speak's open-file number (51 back)
        CHECKS.append(check("Blazie emulator: files CONTROL (Type 'n Speak, break 5, must fail)",
                            [FILES, "tns", os.path.join(FW_BLAZIE, "tns", "TNSENG.TNS"), "--break=5"], expect_fail=True,
                            fail_marks=[r"^FAIL the open file goes on after the import", r"^FAILED$"]))
        # the Type 'n Speak's real cold reset (Timothy, Jayson): the old cold start's keys put back (tns_board.h
        # tns_cold_break) reach only the warm reset -- no file system, no folders: the first file loses its first
        # character and the unit cannot move it to flash
        CHECKS.append(check("Blazie emulator: files CONTROL (Type 'n Speak, the old cold start, must fail)",
                            [FILES, "tns", os.path.join(FW_BLAZIE, "tns", "TNSENG.TNS"), "--break=cold"],
                            expect_fail=True,
                            fail_marks=[r"^FAIL the factory start set the unit up +RAM file system NOT set up, flash "
                                        r"set up, folders NOT set up",
                                        r"^FAIL the unit's first file, moved to flash, whole +notes \"\?ello world"
                                        r"\\rline two\" in RAM", r"^FAILED$"]))
        # ... and a unit the previews saved that way (src/apps/blazie/tns_rescue.c): told apart, set up anew with its
        # RAM files, the flash file it lost named; its control leaves the old cold start on through the rescue
        RESCUE = [os.path.join(EMU, "test_rescue.exe"), os.path.join(FW_BLAZIE, "tns", "TNSENG.TNS")]
        if os.path.isfile(RESCUE[0]):
            CHECKS.append(check("Blazie emulator: a Type 'n Speak never set up, rescued", RESCUE))
            CHECKS.append(check("Blazie emulator: rescue CONTROL (the old cold start left on, must fail)", RESCUE,
                                env={"TEST_RESCUE_BREAK": "1"}, expect_fail=True,
                                fail_marks=[r"^ok +the old unit told apart, its files found",
                                            r"^FAIL rescued: set up anew, its files carried +the unit's own setup did "
                                            r"not complete", r"^FAILED$"]))
    # ... and the images in another program: 7-Zip (when this machine has it) extracts a packed image and a unit's
    # export exactly; its control writes the long names with a wrong checksum (7-Zip then shows 8.3 aliases)
    SEVEN = shutil.which("7z") or os.path.join(os.environ.get("ProgramFiles", ""), "7-Zip", "7z.exe")
    if os.path.isfile(SEVEN) and os.path.isfile(os.path.join(EMU, "blazie_files.exe")):
        Z7 = [PY, "files_7zip.py", os.path.join(EMU, "blazie_files.exe"),
              os.path.join(FW_BLAZIE, "bl2_2003_warm.state"), SEVEN]
        CHECKS.append(check("Blazie emulator: disk images in 7-Zip", Z7))
        CHECKS.append(check("Blazie emulator: disk images in 7-Zip CONTROL (long names broken, must fail)", Z7,
                            env={"TEST_FILES_FAT_BREAK": "1"}, expect_fail=True,
                            fail_marks=[r"^FAIL 7-Zip extracts a packed image exactly .*extra .*~1",
                                        r"^FAIL 7-Zip reads a unit's export as the unit holds it", r"^FAILED$"]))
# the Braille 'n Speak 2000 (Tomi: the Slovak firmware, a Braille 'n Speak 2000), when firmware/blazie/bns2000/ has
# its English and Slovak firmware and their factory states (src/apps/blazie/make_state.c): the Braille Lite's board
# with no braille display (bl_board.h bl_model).  The units told apart, the voice's import still refusing it; its
# words through the chip's register writes (Slovak at power-on and o-chord t, read in Slovak), with the control (the
# Slovak words asked of the English unit, must fail); the factory states remade by the recipe, byte for byte; and
# the emulator's own checks on both: boot and a chord answered, the clock and keys held through a restart, files in
# and out, the file flash's 2 MB, the serial port.  Model control: the two units swapped must fail.
BNS_DIR = os.path.join(FW_BLAZIE, "bns2000")
BNS_UNITS = (("English", "BS03ENG.BNS", "bs03eng_fresh.state", "en"),
             ("Slovak", "BS2SLL.BNS", "bs2sll_fresh.state", "sk"))
TEST_BNS = os.path.join(EMU, "test_bns.exe")
if os.path.isfile(TEST_BNS) and all(os.path.isfile(os.path.join(BNS_DIR, fw)) and
                                    os.path.isfile(os.path.join(BNS_DIR, st)) for _, fw, st, _ in BNS_UNITS):
    BL_FW =os.path.join(FW_BLAZIE, "BL2ENG.BNS")
    BNS_FWS = [os.path.join(BNS_DIR, fw) for fw in ("BS03ENG.BNS", "BS2SLL.BNS", "BS2ENG.BNS", "BS2ENG99.BNS")
               if os.path.isfile(os.path.join(BNS_DIR, fw))]
    OTHER = [p for p in (os.path.join(FW_BLAZIE, "tns", "TNSENG.TNS"),) if os.path.isfile(p)]
    CHECKS.append(check("Braille 'n Speak 2000: the units told apart", [TEST_BNS, "models", BL_FW] + BNS_FWS
                        + ["--"] + OTHER))
    CHECKS.append(check("Braille 'n Speak 2000: the units told apart CONTROL (swapped, must fail)",
                        [TEST_BNS, "models", BNS_FWS[0], BL_FW], expect_fail=True,
                        fail_marks=[r"^FAIL a Braille Lite 2000 +.*BS03ENG\.BNS: Braille 'n Speak 2000$",
                                    r"^FAIL a Braille 'n Speak 2000 +.*BL2ENG\.BNS: Braille Lite 2000$", r"^FAILED$"]))
    for label, fw, st, lang in BNS_UNITS:
        FW, ST = os.path.join(BNS_DIR, fw), os.path.join(BNS_DIR, st)
        CHECKS.append(check("Braille 'n Speak 2000 %s: its words through the chip" % label,
                            [TEST_BNS, "speech", FW, ST, lang]))
        with open(ST, "rb") as f:
            want = hashlib.sha256(f.read()).hexdigest()
        CHECKS.append(check("Braille 'n Speak 2000 %s: the factory state is the recipe's" % label,
                            [os.path.join(EMU, "make_state.exe"), FW, "english",
                             os.path.join(HERE, "out", "bns_recipe_%s.state" % lang)],
                            ok=lambda out, want=want: (" sha256 %s" % want) in out))
        # the Slovak unit's key-to-sound is its own (~443 ms: its letter-to-sound rules are slower)
        CHECKS.append(check("Braille 'n Speak 2000 %s: the unit, headless" % label,
                            [os.path.join(EMU, "test_emu_unit.exe"), "bl", FW, ST] + (["480"] if lang == "sk" else [])))
        # the Slovak unit takes the date in its own order (the English digits set another date) and says it in
        # Slovak: only the clock read at the start, as for the Spanish Braille Lite
        CHECKS.append(check("Braille 'n Speak 2000 %s: clock%s" % (label, " and held keys" if lang == "en" else ""),
                            [CLOCK, "bl", FW, ST] + (["start"] if lang == "sk" else [])))
        CHECKS.append(check("Braille 'n Speak 2000 %s: files in and out" % label, [FILES, "bl", FW, ST]))
        CHECKS.append(check("Braille 'n Speak 2000 %s: serial port" % label,
                            [os.path.join(EMU, "test_serial.exe"), "bl", FW, ST]))
    CHECKS.append(check("Braille 'n Speak 2000: the Slovak words CONTROL (asked of the English unit, must fail)",
                        [TEST_BNS, "speech", os.path.join(BNS_DIR, "BS03ENG.BNS"),
                         os.path.join(BNS_DIR, "bs03eng_fresh.state"), "sk"], expect_fail=True,
                        fail_marks=[r"^ok +the unit +Braille 'n Speak 2000$",
                                    r"^FAIL Slovak words at power-on +\[B R A E L EH N S P E K T U U TH",
                                    r"^FAIL o-chord t answered in Slovak +\[AH P SCH UH2 N R E S EH T", r"^FAILED$"]))
    # the Slovak unit's names are code page 852 (its "fles subory" folder): put back as 850, they must fail
    CHECKS.append(check("Braille 'n Speak 2000: Slovak names CONTROL (code page 850, must fail)",
                        [FILES, "bl", os.path.join(BNS_DIR, "BS2SLL.BNS"), os.path.join(BNS_DIR, "bs2sll_fresh.state"),
                         "--break=cp"], expect_fail=True,
                        fail_marks=[r"^ok +export: a file the unit made",
                                    r"^FAIL export: the Slovak unit's folder names +.*: no$", r"^FAILED$"]))
    CHECKS.append(check("Braille 'n Speak 2000 English: file flash", [os.path.join(EMU, "test_flash.exe"), "bl",
                        os.path.join(BNS_DIR, "BS03ENG.BNS"), os.path.join(BNS_DIR, "bs03eng_fresh.state")]))
# Blazie's games from their disks (src/apps/blazie/test_games.c; RetroBunn, PR #9: Simon locked the unit up at its
# first tone), when firmware/blazie/games/ has them (simon.bns, hangman.bns: never in the repo; absent, no check):
# run from the unit's files with its own command on every unit here.  Simon's first tone and its time-out; Hangman a
# guess answered; the same samples with the bus answered FFh wherever the game does not read it.  Controls: the bus
# answered FFh again (bl_board.h bl_bus_break): Simon silent from its start key, the unit locked up
GAMES = os.path.join(FW_BLAZIE, "games")
TEST_GAMES = os.path.join(EMU, "test_games.exe")
if os.path.isfile(TEST_GAMES):
    GAME_UNITS = [("Braille Lite ENG", ["bl", os.path.join(FW_BLAZIE, "BL2ENG.BNS"),
                                        os.path.join(FW_BLAZIE, "bl2_2003_warm.state")])]
    if all(os.path.isfile(os.path.join(BNS_DIR, f)) for f in ("BS03ENG.BNS", "bs03eng_fresh.state")):
        GAME_UNITS.append(("Braille 'n Speak 2000 English", ["bl", os.path.join(BNS_DIR, "BS03ENG.BNS"),
                                                             os.path.join(BNS_DIR, "bs03eng_fresh.state")]))
    if os.path.isfile(os.path.join(FW_BLAZIE, "tns", "TNSENG.TNS")):
        GAME_UNITS.append(("Type 'n Speak ENG", ["tns", os.path.join(FW_BLAZIE, "tns", "TNSENG.TNS"), "-"]))
    for game in ("simon", "hangman"):
        GAME = os.path.join(GAMES, game + ".bns")
        if not os.path.isfile(GAME):
            continue
        for label, unit in GAME_UNITS:
            CHECKS.append(check("Blazie games: %s, %s" % (game.capitalize(), label), [TEST_GAMES, game] + unit + [GAME]))
            if game == "simon" and label != "Braille 'n Speak 2000 English":
                CHECKS.append(check("Blazie games: Simon CONTROL (%s, the bus answered FFh, must fail)" % label,
                                    [TEST_GAMES, game] + unit + [GAME], env={"TEST_GAMES_BREAK": "1"},
                                    expect_fail=True,
                                    fail_marks=[r"^ok +the game runs: its welcome and prompt",
                                                r"^ok +to the start key, the same with the bus FFh",
                                                r"^FAIL the first tone after the start key +none in 2 s",
                                                r"^FAIL the game goes on: its time-out answered", r"^FAILED$"]))
# the emulator's serial port plugged in (src/apps/blazie/test_serial.c; Tomi: WinDisk to the emulated unit): the
# storage handshake WinDisk and PCDISK answer -- XON ENQ out at 19200 8N1, ACK answered with 'C' and NAK not, input
# paced at the baud rate, the directory command out -- on every unit; the Windows COM side (serial_win.c) end to end
# through a named pipe; and the controls, built with the receive path cut: "ACK answered" must FAIL
if os.path.isfile(os.path.join(EMU, "test_serial.exe")):
    SERIAL = os.path.join(EMU, "test_serial.exe")
    for label, fw, state in (("Braille Lite ENG", "BL2ENG.BNS", "bl2_2003_warm.state"),
                             ("Braille Lite SPA", "BL2SPA.BNS", "bl2spa_fresh.state")):
        CHECKS.append(check("Blazie emulator: serial port, %s" % label, [SERIAL, "bl", os.path.join(ENG, fw),
                                                                         os.path.join(ENG, state)]))
    for name in ("TNSENG.TNS", "TNSSPA.TNS"):
        TNS_FW = os.path.join(os.path.dirname(os.path.dirname(HERE)), "firmware", "blazie", "tns", name)
        if os.path.isfile(TNS_FW):
            CHECKS.append(check("Blazie emulator: serial port, Type 'n Speak %s" % name[3:6],
                                [SERIAL, "tns", TNS_FW, "-"]))
    BL_UNIT = [os.path.join(ENG, "BL2ENG.BNS"), os.path.join(ENG, "bl2_2003_warm.state")]
    CHECKS.append(check("Blazie emulator: serial port through Windows (a named pipe)",
                        [os.path.join(EMU, "test_serial_win.exe")] + BL_UNIT))
    CHECKS.append(check("Blazie emulator: serial port, receive path cut (control)",
                        [os.path.join(EMU, "test_serial_cut.exe"), "bl"] + BL_UNIT, expect_fail=True,
                        fail_marks=[r"^ok +storage: XON ENQ out +sent \[11 05\]$",
                                    r"^FAIL ACK answered +ACK -> nothing back$",
                                    r"^ok +NAK not answered \(control\)"]))
    CHECKS.append(check("Blazie emulator: serial port through Windows, receive path cut (control)",
                        [os.path.join(EMU, "test_serial_win_cut.exe")] + BL_UNIT, expect_fail=True,
                        fail_marks=[r"^ok +XON ENQ reach the far end", r"^FAIL ACK answered +ACK -> nothing back$"]))
# head-of-speech latency (Tomi, 2026-09-30: "it feels like a virtual machine"), in emulated time where it can be: the
# emulator's key-to-sound (inside the headless checks above: "key latency", and "quick key response", the opt-in), with
# its control (quick response never switched on must fail); and the Braille Lite driver's open channel, which must not
# queue the speech still playing as idle audio in front of new speech, with its control (0.6.0's pacing put back)
if os.path.isfile(os.path.join(EMU, "test_emu_unit.exe")):
    CHECKS.append(check("Blazie emulator: quick key response CONTROL (never on, must fail)",
                        [os.path.join(EMU, "test_emu_unit.exe"), "bl", os.path.join(ENG, "BL2ENG.BNS"),
                         os.path.join(ENG, "bl2_2003_warm.state")], env={"TEST_EMU_QUICK_BREAK": "1"}, expect_fail=True,
                        fail_marks=[r"^ok +key latency ", r"^FAIL quick key response +key to first sound", r"^FAILED$"]))
CHECKS.append(check("open channel: new speech not queued behind it", [PY, "tail_latency.py"]))
CHECKS.append(check("open channel CONTROL (0.6.0 pacing, must fail)", [PY, "tail_latency.py"],
                    env={"TAIL_LATENCY_BREAK": "1"}, expect_fail=True,
                    fail_marks=[r"^FAIL A whine on, keep open, no cancel ", r"^ok +B whine off",
                                r"^tail latency: \d FAILED$"]))   # 0.6.0 whole can delay D and E too, under load
# Tomi (0.7 test build): with keep open, a follow-up utterance paused longer -- the idle audio still queued (up to
# IDLE_AHEAD_S and a block) played before it; now dropped once the last speech has played (the player's onDone)
CHECKS.append(check("open channel CONTROL (idle audio left queued, must fail)", [PY, "tail_latency.py"],
                    env={"TAIL_LATENCY_BREAK": "queued"}, expect_fail=True,
                    fail_marks=[r"^FAIL A whine on, keep open, no cancel .*B \+ 40 ms", r"^ok +B whine off",
                                r"^tail latency: [12] FAILED$"]))   # D may fail too: the queue delays it as well
# ... and while the last speech's onDone is still due (Astra, Reply 109): NVDA's player calls it only from feed/sync on
# the feeding thread, so the wait must service it (an empty feed) and a cancel must end it; the plain wait put back
# stalled ~0.5 s, even after a cancel -- D and E must fail
CHECKS.append(check("open channel CONTROL (plain wait on the last onDone, must fail)", [PY, "tail_latency.py"],
                    env={"TAIL_LATENCY_BREAK": "plainwait"}, expect_fail=True,
                    fail_marks=[r"^ok +A whine on", r"^FAIL D next speech while the last onDone is due",
                                r"^FAIL E cancel inside that wait", r"^tail latency: 2 FAILED$"]))
# ... and the opt-in, EXPERIMENTAL "run ahead" mode (src/csrc/blazie/run_ahead.h; Astra, Reply 107).  Its contract on a
# synthetic board (test_run_ahead.exe: completion, limits, allocation failures, the 5 ms classification), each control
# putting one 0.7-draft behaviour back; then against today's lockstep through bl.dll (run_ahead_equiv.py): values
# whole-phrase identical (cancel-prefix matches counted apart), every say observed done, no audio or speech after done,
# the replay = the capture, head latency; each guard with its control
RA_TEST = os.path.join(LIB, "test_run_ahead.exe")
if os.path.isfile(RA_TEST):
    CHECKS.append(check("run ahead contract (synthetic board)", [RA_TEST]))
    # (Astra, Reply 112) a chip's request is not silence: the final load spoken and held is never complete, its
    # cleanup pause is (old-request: complete at the request, 4410 of 4410 samples still audible after); an error is
    # sticky until reset (old-sticky)
    for arg, marks in (
            ("old-completion", [r"^FAIL last load, no cleanup \(held: not complete\): complete while it sounded 1, "
                                r"ended held 0, audible after its end 4410 of 4410 samples$",
                                r"^FAIL final load never ends: ended as state 5 ",
                                r"^run ahead contract: 4 of 13 FAILED$"]),
            ("old-limit", [r"^FAIL owed, first load at 3\.1 s: ended as state 5 \(limit = 6\)",
                           r"^run ahead contract: 1 of 13 FAILED$"]),
            ("old-alloc", [r"^FAIL allocation fails \(acknowledgements\): ended as state 5 .* after 300 of 300 loads$",
                           r"^FAIL allocation fails \(writes\): ended as state 6 .* after 1024 of 1050 writes$",
                           r"^run ahead contract: 3 of 13 FAILED$"]),
            ("old-reading", [r"^FAIL 5 ms threshold .*first wrong: load 5 answered 0\.0\d\d ms after the request$",
                             r"^FAIL slow service \(50 ms answers in speech\): worst answer off by 49\.9\d\d ms",
                             r"^run ahead contract: 2 of 13 FAILED$"]),
            ("old-settle", [r"^FAIL cancel while capturing: the unit settles first: mid-routine at the stop 1, waits "
                            r"for an interrupt at the cancel 0, 0 of its writes dropped",
                            r"^run ahead contract: 1 of 13 FAILED$"]),
            ("old-request", [r"^FAIL last load, no cleanup \(held: not complete\): complete while it sounded 1, ended "
                             r"held 0, audible after its end 4410 of 4410 samples$",
                             r"^ok +last load, then a cleanup pause \(complete\)",
                             r"^run ahead contract: 1 of 13 FAILED$"]),
            ("old-sticky", [r"^FAIL an error is sticky until reset: ended as state 7 \(error = 7\), a new start over it "
                            r"refused 0", r"^run ahead contract: 1 of 13 FAILED$"])):
        CHECKS.append(check("run ahead contract CONTROL (%s, must fail)" % arg, [RA_TEST, arg], expect_fail=True,
                            fail_marks=marks))
if os.path.isfile(os.path.join(LIB, "x64", "bl.dll")):
    CHECKS.append(check("run ahead = lockstep, English (values; done; after done; head latency)",
                        [PY, "run_ahead_equiv.py"]))
    CHECKS.append(check("run ahead = lockstep, Spanish (values; done; after done; head latency)",
                        [PY, "run_ahead_equiv.py", "--es"]))
    RA_SUM = r"^run ahead vs lockstep \(English\): \d+ utterances, .*, %d FAILED$"
    for what, env, args, marks in (
            ("a value flipped", {"RUN_AHEAD_EQUIV_FLIP": "1"}, ["--quick"],
             [r"^FAIL case 1 block 1: write values DIFFER at write \d+$",
              r"^FAIL 1\.1 Hello\. +REPLAY DIFFERS FROM CAPTURE", RA_SUM % 2]),
            ("left off, the head", {"RUN_AHEAD_EQUIV_OFF": "1"}, ["--quick"],
             # left off, "run ahead" is the lockstep: its head the same, over the 100 ms limit (MAME: 120.8 ms; the
             # z180emu core's was 123.0)
             [r"^FAIL 1\.3 Select synthesizer dialog +head (1\d\d\.\d) -> \1 ms", RA_SUM % 3]),
            ("busy never false", {"RUN_AHEAD_EQUIV_NEVER": "1", "RUN_AHEAD_EQUIV_SAY_LIMIT": "3"}, ["--cases=1"],
             [r"^FAIL 1\.1 Hello\. +NEVER DONE \(run ahead, 3 s\)$", RA_SUM % 3]),
            ("audio silenced", {"RUN_AHEAD_EQUIV_MUTE": "1"}, ["--cases=1"],
             [r"^FAIL 1\.1 Hello\. +NO FIRST AUDIO \(run ahead\)$", RA_SUM % 3]),
            ("last spoken load dropped", {"RUN_AHEAD_EQUIV_DROP_SPOKEN": "1"}, ["--cases=1"],
             [r"^FAIL case 1 block 1: a SPOKEN LOAD \(2B\) in the \d+-write suffix that only the lockstep has$",
              RA_SUM % 2]),
            ("0.7 draft end rule", {"RUN_AHEAD_EQUIV_OLD_SUFFIX": "1"}, ["--selftest"],
             [r"^FAIL the end rule: a missing final spoken load ACCEPTED \(Astra's case\)",
              r"^run ahead vs lockstep \(end rule\): 1 FAILED$"]),
            ("0.7 draft completion", {"RUN_AHEAD_EQUIV_BREAK": "completion"}, ["--cases=1"],
             [r"^FAIL 1\.1 Hello\. +AUDIO AFTER DONE \(run ahead\): \d+ samples", RA_SUM % 2]),
            ("a spoken load flipped where a setting came mid-line", {"RUN_AHEAD_EQUIV_FLIP": "send_mid"},
             ["--cases=9"], [r"^FAIL case 9 block 1: write values DIFFER at write \d+$", RA_SUM % 2])):
        CHECKS.append(check("run ahead CONTROL (%s, must fail)" % what, [PY, "run_ahead_equiv.py"] + args, env=env,
                            expect_fail=True, fail_marks=marks))
    # at 44.1 kHz: a block's rounding sliver once ran a lockstep slice in the middle of the script, and the unit
    # stalled (found by lane 2); its control puts that back
    CHECKS.append(check("run ahead = lockstep, English at 44.1 kHz", [PY, "run_ahead_equiv.py", "--rate=44100"]))
    CHECKS.append(check("run ahead CONTROL (the 0.7 draft's sliver slice at 44.1 kHz, must fail)",
                        [PY, "run_ahead_equiv.py", "--rate=44100", "--cases=1"],
                        env={"RUN_AHEAD_EQUIV_BREAK": "sliver", "RUN_AHEAD_EQUIV_SAY_LIMIT": "5"}, expect_fail=True,
                        fail_marks=[r"^FAIL 1\.3 Select synthesizer dialog +NEVER DONE \(run ahead, 5 s\)", RA_SUM % 2]))
    # the two lanes (run_ahead_lanes.py): lane 1, the lockstep's own schedule replayed from the capture -- times,
    # values, request boundaries and audio identical; lane 2, the deliberate retiming classified and bounded against
    # a finer and finer lockstep.  Their controls: the schedule shifted a sample, a boundary moved, answers misjudged
    CHECKS.append(check("run ahead lanes 1 and 2, English", [PY, "run_ahead_lanes.py"]))
    # (0.7, after the MAME switch; not user-facing) the TEST-ONLY paced replay (bh_pace, run_ahead.c's `if (r->pace)`)
    # rounds to whole samples and overshoots by one when under half a sample remains.  Spanish lane 1 case 2 hit it at
    # writes 2040-2045, 22.7 us late, and was held as a KNOWN failure; since 0.7.6's hard G (hold_release) the case's
    # "luego" brings everything after its G 0.63 ms earlier, no write lands under half a sample from a pacing step and
    # the case passes.  The rounding is still in run_ahead.c (its fix after the release), so it keeps its own fixture:
    CHECKS.append(check("run ahead lanes 1 and 2, Spanish", [PY, "run_ahead_lanes.py", "--es"]))
    # KNOWN FAILURE, held as a fixture (Astra, Reply 151: a newly passing scenario is not a repaired bug): the same
    # Spanish lanes with the chip's hard G off (--hard-g-off, params hold_release False = 0.7.5's chip, bit for bit)
    # put the writes back at their old times, where the paced replay overshoots by one sample at writes 2040-2045.
    # It must fail in exactly this way; it fails this check if it ever passes or fails anywhere else -- the day
    # run_ahead.c's rounding is fixed, this becomes a plain check
    CHECKS.append(check("run ahead lanes, Spanish, hard G off (KNOWN: test-only pacing rounding, after 0.7)",
                        [PY, "run_ahead_lanes.py", "--es", "--hard-g-off"], expect_fail=True,
                        fail_marks=[r"^ok +lane 1 case 1: ", r"^FAIL lane 1 case 2: TIME differs at write 2044: 7\.316882 -> 7\.316905 s",
                                    r"^ok +lane 1 case 3: ", r"^run ahead lanes \(Spanish\): 1 FAILED$"]))
    LANES_SUM = r"^run ahead lanes \(English\): %d FAILED$"
    for what, brk, args, marks in (
            ("lane 1, schedule a sample late", "pace", ["--lane=1"],
             [r"^FAIL lane 1 case 1: TIME differs at write \d+: .*; AUDIO differs from sample \d+", LANES_SUM % 2]),
            ("lane 1, a boundary moved", "segment", ["--lane=1"],
             [r"^FAIL lane 1 case 1: SEGMENT boundary at write \d+ in the capture only$", LANES_SUM % 2]),
            ("lane 2, answers misjudged", "class", ["--lane=2"],
             [r"^FAIL lane 2 case 1 step 0\.5000 ms: UNCLASSIFIED 40 ", LANES_SUM % 4])):
        CHECKS.append(check("run ahead lanes CONTROL (%s, must fail)" % what, [PY, "run_ahead_lanes.py", "--quick"] + args,
                            env={"RUN_AHEAD_LANES_BREAK": brk}, expect_fail=True, fail_marks=marks))
    # the firmware and board state at semantic checkpoints (run_ahead_state.py), and the same continuation after them,
    # without and with a cancel mid-utterance: every checkpoint must agree but for the named, justified differences
    # (per-checkpoint timing, the stack below SP, the firmware's seconds counters; after a cancel, what the unit read
    # ahead of the listener).  Its controls: one RAM byte changed; the cancel's leak put back (RA_BRK_SETTLE: the
    # respoken utterance led by cancelled text, write 12 of 175/181, as it was)
    for lang in ([], ["--es"]):
        what = "Spanish" if lang else "English"
        CHECKS.append(check("run ahead state = lockstep at checkpoints, no cancel, %s" % what,
                            [PY, "run_ahead_state.py", "--no-cancel"] + lang))
        CHECKS.append(check("run ahead state = lockstep at checkpoints, a cancel then respeech, %s" % what,
                            [PY, "run_ahead_state.py"] + lang))
    CHECKS.append(check("run ahead state CONTROL (a RAM byte changed, must fail)",
                        [PY, "run_ahead_state.py", "--no-cancel"], env={"RUN_AHEAD_STATE_BREAK": "1"}, expect_fail=True,
                        fail_marks=[r"^FAIL setting .*FINDING: RAM 1 cells beyond timing's: FFF00 00/5A$",
                                    r"^run ahead state \(English\): 1 FAILED$"]))
    CHECKS.append(check("run ahead state CONTROL (the cancel's leak put back, must fail)",
                        [PY, "run_ahead_state.py"], env={"RUN_AHEAD_STATE_BREAK": "settle"}, expect_fail=True,
                        fail_marks=[r"^FAIL cancelled .*FINDING: RAM 4 cells beyond timing's: 433AB 0D/61 433AC FF/61 ",
                                    r"^FAIL respoken .*writes DIFFER at 12 of 175/181",
                                    r"^run ahead state \(English\): \d+ FAILED$"]))
    # a cancel, then at once a new say (run_ahead_cancel.py): the new utterance begins with its own phonemes, over
    # sweeps of the cancel time (with and without history, and in the driver's 30 ms blocks); the lockstep's own rarer
    # line-boundary race is reported, not gated.  Its control puts the leak back
    for lang in ([], ["--es"]):
        CHECKS.append(check("run ahead cancel: no cancelled text at the next head, %s" % ("Spanish" if lang else "English"),
                            [PY, "run_ahead_cancel.py"] + lang))
    CHECKS.append(check("run ahead cancel CONTROL (the leak put back, must fail)", [PY, "run_ahead_cancel.py", "--quick"],
                        env={"RUN_AHEAD_CANCEL_BREAK": "settle"}, expect_fail=True,
                        # on MAME 2 of 25 (the z180emu core's: 4): 0.50 s led by the resumed copier's "A" (10),
                        # 0.80 s by "And a t", the copy gone on (12 56 37 8 2 54)
                        fail_marks=[r"^FAIL state history, rate 14: run ahead 2 of 25 led by other phonemes; 0\.50 s: "
                                    r"\[10, 29, 10, .*; 0\.80 s: \[12, 56, 37, 8, 2, 54\]",
                                    r"^run ahead cancel \(English\): 1 FAILED$"]))
    # the lockstep's own cancel race (lockstep_cancel.py; Astra, Reply 112 item 3, Replies 114 and 124-129): the
    # shipping default (bl_host.c cancel_settle 3: settle, then A/R held over ^X) must respeak cleanly at all 8 MAME
    # leak times retained from Astra's sweep; its controls put the race back through the host's override
    # (SSI263_BLAZIE_CANCEL_SETTLE): 0 must bring back all 8 leaks exactly (phonemes and line buffer), the settle
    # alone (1) the two copier-resumed ones it cannot clear
    CHECKS.append(check("lockstep cancel race: the default clean at every retained leak", [PY, "lockstep_cancel.py"]))
    LC_SUM = r"^lockstep cancel race \(English, cancel_settle %d, SSI263_BLAZIE_CANCEL_SETTLE=%d; .*\): %d of 10 FAILED$"
    CHECKS.append(check("lockstep cancel race CONTROL (cancel_settle 0, the race put back, must fail)",
                        [PY, "lockstep_cancel.py"], env={"SSI263_BLAZIE_CANCEL_SETTLE": "0"}, expect_fail=True,
                        fail_marks=[r"^FAIL 1\.0380 s, cancel_settle 0 .*led by \[10\]; .* the retained leak, exactly$",
                                    r"^FAIL 1\.0410 s, cancel_settle 0 .*begins \[7, 28, 10, 48\] .* the retained leak, "
                                    r"exactly$",
                                    r"^FAIL 1\.0480 s, cancel_settle 0 .*led by \[12, 56, 37, 8, 2\]; .* the retained "
                                    r"leak, exactly$", LC_SUM % (0, 0, 8)]))
    CHECKS.append(check("lockstep cancel race CONTROL (the settle alone, must fail)", [PY, "lockstep_cancel.py"],
                        env={"SSI263_BLAZIE_CANCEL_SETTLE": "1"}, expect_fail=True,
                        fail_marks=[r"^FAIL 1\.0380 s, cancel_settle 1 .*led by \[10\]; .* the retained leak, exactly$",
                                    r"^FAIL 1\.0385 s, cancel_settle 1 .*led by \[56\]; .* the retained leak, exactly$",
                                    r"^ok +1\.0480 s, cancel_settle 1 ", LC_SUM % (1, 1, 2)]))
    # faults are never silent (run_ahead_fault.py; Astra, Reply 112 item 2): a run-ahead capture failing mid-utterance
    # with a say or a setting waiting, or at its start with a second say at once (her error_priority_probe.c: busy 1
    # with input held), a board event, a transmitted byte and a logged write lost -- each seen first
    # (bh_busy -1, raising), no input delivered or taken over it, cancel() the recovery.  Its controls put the error
    # behind held input (sticky) and the losses back to silent (drop)
    CHECKS.append(check("run ahead faults: seen first, sticky until cancel, never silent", [PY, "run_ahead_fault.py"]))
    FAULT_SUM = r"^run ahead faults: %d FAILED$"
    for what, brk, marks in (
            ("the error behind held input, delivered over", "sticky",
             [r"^FAIL run-ahead script, a say waiting: .*the held say was DELIVERED over the error",
              r"^FAIL run-ahead script, a send waiting: .*the held send was DELIVERED over the error",
              r"^FAIL run-ahead script failed at its start, a second say: RA_ERROR with the second say: C bh_say 1, "
              r"bh_busy 1 with 1 input held", FAULT_SUM % 3]),
            ("losses silent", "drop",
             [r"^FAIL board event lost \(lockstep\): a lost board event passed SILENTLY",
              r"^FAIL transmit record: a transmitted byte lost SILENTLY", r"^FAIL write log: a logged write lost SILENTLY",
              FAULT_SUM % 3])):
        CHECKS.append(check("run ahead faults CONTROL (%s, must fail)" % what, [PY, "run_ahead_fault.py"],
                            env={"RUN_AHEAD_FAULT_BREAK": brk}, expect_fail=True, fail_marks=marks))
    # ... and through the real driver (run_ahead_driver.py): Tab held in Windows' Run dialog, NVDA's cancel and speak
    # every 30 and 50 ms; its control never cancels the unit
    if os.path.isdir(os.path.join(os.path.dirname(HERE), "dist", "blazie-build")):
        CHECKS.append(check("run ahead through the driver: Tab in the Run dialog", [PY, "run_ahead_driver.py"]))
        CHECKS.append(check("run ahead through the driver CONTROL (the unit never cancelled, must fail)",
                            [PY, "run_ahead_driver.py"], env={"RUN_AHEAD_DRIVER_BREAK": "nocancel"}, expect_fail=True,
                            fail_marks=[r"^FAIL Tab every 30 ms: .* [1-9]\d* led by anything but their own label",
                                        r"^run ahead through the driver, Run dialog \(run ahead\): 2 FAILED$"]))
# MAME's Z180 core (src/csrc/cpu/z180_mame.cpp; the shipped core since 0.7, bl_live.exe and bl.dll are built on it):
# the same spoken values as the goldens -- its timing legitimately differs (src/csrc/cpu/README.md) -- and two units
# in one process
MAME_LIVE = os.path.join(LIB, "bl_live_mame.exe")
if os.path.isfile(MAME_LIVE):
    for lang in ("en", "es"):
        CHECKS.append(check("MAME Z180 core: spoken values, pipe host (%s)" % lang, [PY, "bns_equiv.py", MAME_LIVE,
                            "--values-only", "--against=" + os.path.join(HERE, "golden", "blazie_%s_pipe.txt" % lang)]
                            + (["--es"] if lang == "es" else [])))
    CHECKS.append(check("MAME Z180 core: spoken values CONTROL (one value flipped, must fail)",
                        [PY, "bns_equiv.py", MAME_LIVE, "--values-only",
                         "--against=" + os.path.join(HERE, "golden", "blazie_en.txt")],
                        env={"BNS_EQUIV_FLIP": "1"}, expect_fail=True,
                        fail_marks=[r"^write values DIFFER from blazie_en\.txt at write \d+ of"]))
    CHECKS.append(check("MAME Z180 core: two units in one process", [os.path.join(LIB, "test_bl_board_mame.exe"),
                        os.path.join(ENG, "BL2ENG.BNS"), os.path.join(ENG, "bl2_2003_warm.state"),
                        os.path.join(ENG, "BL2SPA.BNS"), os.path.join(ENG, "bl2spa_fresh.state")]))
    # CONTRACT.md's clauses (src/csrc/cpu/test_z180_contract.c; its must-fail controls: cpu/contract_controls.py)
    CHECKS.append(check("MAME Z180 core: CPU contract tests", [os.path.join(LIB, "test_z180_contract.exe")]))
    CHECKS.append(check("MAME Z180 core: white-box tests", [os.path.join(LIB, "test_z180_whitebox.exe")]))
# ---- the Android engine (src/platforms/android), one block --------------------------------------------------------
# Its native part, built for the desktop: the Braille Lite as bl.dll speaks (lockstep, and with the experimental run
# ahead on), the Aicom Accent SA (the built-in default voice, Tomi 2026-09-30) as the NVDA Accent driver itself speaks
# it on the desktop's C host (accent_reference.py), the GW Micro Speak-Out (imported; with the firmware) as so_voice
# driven directly speaks it, and the Aicom Accent-mini (built in) as am_voice driven directly does, byte for byte; a capital's 150/120/75 % differ from 100 % (the Accent's 100 %
# again is byte-identical); GW Micro's SPEAKOUT.HEX recognised by content, a damaged or other HEX refused (Tomi,
# 0.7.5: every voice on Android); the Braille Lite's number words on and off, English and Spain's Spanish, as bl_voice
# with bl_numbers in ssi263speech.dll speaks them (Tomi, 0.7.5 uniform).  Each control puts one bug back and must show
# it: the request's rate dropped; the request's pitch dropped; the pitch glided to (no snap_pitch); one Accent unit kept
# across utterances; the plain pitch mapping (120 % on 100 %'s step); the Speak-Out's request pitch dropped; its own
# settings dropped; run ahead dropped; the number words dropped; the import's sha256 dropped.
# The default volume keeps headroom for both voices (Tomi: under TalkBack's sounds at the desktop level), with a
# control per voice.  The APK's firmware check, on an APK-like zip made here: only Aicom's three ROMs pass, by sha256;
# a Blazie image beside them, the Speak-Out's HEX and a changed Aicom ROM must each fail.
ANDROID_TEST = os.path.join(os.path.dirname(os.path.dirname(HERE)), "src", "platforms", "android", "test",
                            "test_android_native.py")
if os.path.isfile(ANDROID_TEST):
    ANDROID_FW = os.path.join(os.path.dirname(os.path.dirname(HERE)), "firmware")
    ANDROID_SO = os.path.isfile(os.path.join(ANDROID_FW, "gw-micro-speakout", "SPEAKOUT.HEX"))
    ANDROID_MINI = os.path.isfile(os.path.join(ANDROID_FW, "aicom-accent-mini", "SPKEMS.DVC"))
    # the Mockingboard's firmware is imported by the user, never committed: its cases run where the file is
    ANDROID_MB = os.path.isfile(os.path.join(ANDROID_FW, "sweet-micro-mockingboard", "mockingboard-tts-1.1.bin"))
    # ... and its disk images, from MOCKINGBOARD_DISKS (paths.local; never committed): the toolkit's, and another
    sys.path.insert(0, os.path.dirname(os.path.dirname(HERE)))
    from tools import repo_paths as _rp      # noqa: E402
    _MBD = _rp.lookup("MOCKINGBOARD_DISKS") or ""
    ANDROID_DSK = bool(_MBD) and os.path.isfile(
        os.path.join(_MBD, "Sweet Micro Systems Mockingboard Developers toolkit 1984.dsk"))
    ANDROID_DSK2 = bool(_MBD) and os.path.isfile(os.path.join(_MBD, "mockingboard1.dsk"))
    # the number words' reference (bl.dll has none): ssi263speech.dll, src/csrc/build_ssi263speech.py
    ANDROID_NUM = os.path.isfile(os.path.join(os.path.dirname(os.path.dirname(HERE)), "build", "win",
                                              "x64" if sys.maxsize > 2 ** 32 else "x86", "ssi263speech.dll"))
    # ... and the Mockingboard (imported, 0.8) as mb_voice driven directly, with its import by sha256 and two controls
    CHECKS.append(check("Android engine: native part as bl.dll, the NVDA Accent driver, so_voice, am_voice and mb_voice",
                        [PY, ANDROID_TEST]))
    for brk, what, marks in (
            ("1", "rate dropped",
             [r"^FAIL +desktop +fast ", r"^FAIL +desktop +a-fast ", r"^ok +desktop +spanish ",
              r"^FAILED: 5 case\(s\) differ$"]),
            ("accent-pitch", "Accents: the request's pitch dropped",
             [r"^FAIL +desktop +a-pitch-150 sounds different from a-pitch-100$",
              r"^ok +desktop +a-pitch-100-again = a-pitch-100, byte for byte",
              r"^FAILED: %d case\(s\) differ$" % (8 + 4 * ANDROID_MINI)]),
            ("accent-glide", "Accent SA: the pitch glided to, no snap",
             [r"^FAIL +desktop +a-pitch-150 +got ", r"^ok +desktop +a-pitch-150 sounds different from a-pitch-100$",
              r"^FAILED: 5 case\(s\) differ$"]),
            ("accent-reuse", "Accent SA: one unit kept across utterances",
             [r"^FAIL +desktop +a-pitch-100-again = a-pitch-100, byte for byte", r"^ok +desktop +a-default ",
              r"^FAILED: 15 case\(s\) differ$"]),
            ("accent-step", "Accent SA: 120 % on the 100 % step",
             [r"^FAIL +desktop +a-pitch-120 sounds different from a-pitch-100$", r"^ok +desktop +a-pitch-150 ",
              r"^FAILED: 2 case\(s\) differ$"]),
            ("run-ahead", "Braille Lite: run ahead dropped",
             [r"^FAIL +desktop +ra-default +got ", r"^FAIL +desktop +ra-default differs from default",
              r"^ok +desktop +default ", r"^FAILED: 5 case\(s\) differ$"])) + ((
            ("numbers", "Braille Lite: number words dropped",
             [r"^FAIL +desktop +num-en +got ", r"^FAIL +desktop +num-es +got ", r"^ok +desktop +num-en-off ",
              r"^FAIL +desktop +num-en differs from num-en-off", r"^FAILED: 4 case\(s\) differ$"]),) if ANDROID_NUM
            else ()) + ((
            ("speakout-pitch", "Speak-Out: the request's pitch dropped",
             [r"^FAIL +desktop +s-pitch-150 +got ", r"^ok +desktop +s-pitch-100 ", r"^FAILED: 7 case\(s\) differ$"]),
            ("speakout-settings", "Speak-Out: tone, join and short pauses dropped",
             [r"^FAIL +desktop +s-settings +got ", r"^ok +desktop +s-sliders ", r"^FAILED: 9 case\(s\) differ$"]),
            ("import-hash", "Speak-Out and Mockingboard import: the sha256 dropped",
             [r"^FAIL +import +another HEX ", r"^ok +import +a HEX with one digit changed"] +
             ([r"^FAIL +import +the file with one byte changed: Mockingboard", r"^ok +import +the file cut short",
               r"^FAILED: 2 case\(s\) differ$"] if ANDROID_MB else [r"^FAILED: 1 case\(s\) differ$"])),)
            if ANDROID_SO else ()) + ((
            ("mockingboard-pitch", "Mockingboard: the request's pitch dropped",
             [r"^FAIL +desktop +mb-pitch-150 +got ", r"^ok +desktop +mb-pitch-100 ", r"^FAILED: 9 case\(s\) differ$"]),
            ("mockingboard-numbers", "Mockingboard: number words dropped",
             [r"^FAIL +desktop +mb-num +got ", r"^FAIL +desktop +mb-num-off +got ", r"^ok +desktop +mb-pitch-120 ",
              r"^FAILED: 5 case\(s\) differ$"])) if ANDROID_MB else ()) + ((
            ("import-dsk", "Mockingboard import: the disk image never tried",
             [r"^FAIL +import +the toolkit's \.dsk: ", r"^FAIL +import +a zip's toolkit \.dsk: "] +
             ([r"^ok +import +mockingboard-tts-1\.1\.bin: "] if ANDROID_MB else []) +
             [r"^FAILED: %d case\(s\) differ$" % (4 + ANDROID_DSK2)]),) if ANDROID_DSK else ()):
        # each control runs the blocks its bug touches (SSI263_ANDROID_TEST_ONLY), not all six voices: the gate's time
        only = {"1": "bl,accent", "accent-pitch": "accent,mini", "run-ahead": "bl,ra", "numbers": "num",
                "speakout-pitch": "so", "speakout-settings": "so", "import-hash": "import",
                "mockingboard-pitch": "mb", "mockingboard-numbers": "mb", "import-dsk": "import"}.get(brk, "accent")
        CHECKS.append(check("Android engine CONTROL (%s, must fail)" % what, [PY, ANDROID_TEST],
                            env={"SSI263_ANDROID_TEST_BREAK": brk, "SSI263_ANDROID_TEST_ONLY": only}, expect_fail=True,
                            fail_marks=marks))
    VOLUME_TEST = os.path.join(os.path.dirname(ANDROID_TEST), "test_volume_headroom.py")
    CHECKS.append(check("Android engine: default volume, headroom kept (every voice)", [PY, VOLUME_TEST]))
    CHECKS.append(check("Android engine: volume CONTROL (250 percent clips, must fail)", [PY, VOLUME_TEST],
                        env={"SSI263_VOLUME_TEST_BREAK": "1"}, expect_fail=True,
                        fail_marks=[r"^braille lite volume 250: .* clipped samples -- FAILED",
                                    r"^accent sa volume 250: .* clipped samples -- FAILED", r"^volume headroom: FAILED$"]))
    CHECKS.append(check("Android engine: volume CONTROL (the Accent SA at 250 percent, must fail)", [PY, VOLUME_TEST],
                        env={"SSI263_VOLUME_TEST_BREAK": "accent"}, expect_fail=True,
                        fail_marks=[r"^braille lite volume 150: .* -- ok$",
                                    r"^accent sa volume 250: .* clipped samples -- FAILED", r"^volume headroom: FAILED$"]))
    APK_CHECK = os.path.join(os.path.dirname(ANDROID_TEST), "check_apk_no_firmware.py")
    # the Accent-mini's driver (Aicom's) is let through by its sha256 beside the ROMs once the voice is built in
    APK_AICOM = r"4 \(SPKEMS\.DVC, u2\.BIN, u3\.BIN, u4\.BIN\)" if ANDROID_MINI else r"3 \(u2\.BIN, u3\.BIN, u4\.BIN\)"
    CHECKS.append(check("Android APK check: only Aicom's ROMs and driver pass", [PY, APK_CHECK, "--synthetic"],
                        ok=lambda out: bool(re.search(r"^synthetic\.apk: \d+ entries, no firmware; Aicom ROMs allowed: "
                                                      + APK_AICOM + "$", out, re.M))))
    CHECKS.append(check("Android APK check CONTROL (an Aicom ROM changed, must fail)",
                        [PY, APK_CHECK, "--control-aicom-flipped", "--synthetic"], expect_fail=True,
                        fail_marks=[r"assets/aicom/u2\.BIN: in assets/aicom/ but not one of the Aicom ROMs",
                                    r"Aicom ROMs allowed: %d \(%su3\.BIN, u4\.BIN\)$" % (
                                        3 if ANDROID_MINI else 2, r"SPKEMS\.DVC, " if ANDROID_MINI else "")]))
    # the licences (all-MAME 0.7): MIT (ours, Casso's), MAME's BSD-3-Clause notices, Aicom's; no z180emu or Unicorn
    # engine and no GPL notice (tools/check_no_gpl.py: real evidence only -- comments and MAME's compatibility names
    # are not, Astra's Reply 127)
    CHECKS.append(check("Android APK check: MIT and MAME's BSD notices, no GPL", [PY, APK_CHECK, "--synthetic"],
                        ok=lambda out: bool(re.search(r"^synthetic\.apk: licences MIT \(ours, Casso's\), MAME's "
                                                      r"BSD-3-Clause \(Z180, 8085, V40, 8086\), Aicom's"
                                                      r"(, Fake6502's and EchoTalk's)?; no GPL or Unicorn$",
                                                      out, re.M))))
    CHECKS.append(check("Android APK check CONTROL (z180emu's GPL among the licences, must fail)",
                        [PY, APK_CHECK, "--control-gpl", "--synthetic"], expect_fail=True,
                        fail_marks=[r"^synthetic\.apk: \d+ entries, no firmware; Aicom ROMs allowed: [34] ",
                                    r"^synthetic\.apk: licences WRONG: .*z180emu-GPL-2\.0\.txt: GPL notice"]))
    NO_GPL = os.path.join(os.path.dirname(os.path.dirname(HERE)), "tools", "check_no_gpl.py")
    CHECKS.append(check("no-GPL audit: comments and MAME compatibility names are not evidence",
                        [PY, NO_GPL, "--clean-sample"]))
    CHECKS.append(check("no-GPL audit CONTROL (genuine legacy payloads, must fail)", [PY, NO_GPL, "--control"],
                        expect_fail=True,
                        fail_marks=[r"^FAIL control\.apk: .*libssi263speech\.so: z180emu engine",
                                    r"^FAIL control\.apk: .*libssi263speech\.so: Unicorn engine",
                                    r"^FAIL control\.apk: .*unicorn\.dll: a legacy payload by name",
                                    r"^FAIL control\.apk: .*ucmini\.py: Unicorn import",
                                    r"^FAIL control\.apk: .*i8085\.py: a legacy payload by name",
                                    r"^no-GPL audit: 1 of 1 FAILED$"]))
    ROOT_FW = os.path.join(os.path.dirname(os.path.dirname(HERE)), "firmware")
    for fw, what, mark in ((os.path.join(ROOT_FW, "blazie", "BL2ENG.BNS"), "a Blazie image beside Aicom's ROMs",
                            r"unit\.dat: a Braille Lite ROM image; Aicom ROMs allowed: [34] "),
                           (os.path.join(ROOT_FW, "gw-micro-speakout", "SPEAKOUT.HEX"), "the Speak-Out's firmware",
                            r"unit\.dat: the Speak-Out's SPEAKOUT\.HEX; .*unit\.dat: an Intel HEX image"),
                           (os.path.join(ROOT_FW, "sweet-micro-mockingboard", "mockingboard-tts-1.1.bin"),
                            "the Mockingboard's firmware",
                            r"unit\.dat: the Mockingboard's mockingboard-tts-1\.1\.bin")):
        if os.path.isfile(fw):
            CHECKS.append(check("Android APK check CONTROL (%s, must fail)" % what,
                                [PY, APK_CHECK, "--control", fw, "--synthetic"], expect_fail=True,
                                fail_marks=[r"^synthetic\.apk: \d+ entries, FIRMWARE: ", mark]))
# ... its firmware import (src/csrc/blazie/bl_firmware.c, bl_state.c): files found and refused by content, only the
# releases on the list taken, and the states made from the firmware alone = the listed ones (MAME-made, 0.7), byte
# for byte, the z180emu-made shipped states refused; one control holds the wrong chord at the English warm reset and
# its state must not be the list's, the other drops the list and must take the unknown releases
IMPORT_TEST = os.path.join(os.path.dirname(ANDROID_TEST), "test_import_native.py")
if os.path.isfile(IMPORT_TEST):
    CHECKS.append(check("Android import: known firmware only, states made on the device", [PY, IMPORT_TEST]))
    CHECKS.append(check("Android import CONTROL (wrong warm-reset chord, must fail)", [PY, IMPORT_TEST],
                        env={"SSI263_IMPORT_TEST_BREAK": "1"}, expect_fail=True,
                        fail_marks=[r"^FAIL generated English state = the list's", r"^import: 1 FAILED$"]))
    CHECKS.append(check("Android import CONTROL (the list dropped, must fail)", [PY, IMPORT_TEST],
                        env={"SSI263_IMPORT_TEST_BREAK": "hash"}, expect_fail=True,
                        fail_marks=[r"^FAIL a release not on the list", r"^import: 2 FAILED$"]))
# MAME's 8085 core (the Accent SA's, src/csrc/cpu/i8085_mame.cpp; not yet in a board): CONTRACT.md's clauses
# (src/csrc/cpu/test_i8085_contract.c; its must-fail controls: cpu/i8085_controls.py)
if os.path.isfile(os.path.join(LIB, "test_i8085_contract.exe")):
    CHECKS.append(check("MAME 8085 core: CPU contract tests", [os.path.join(LIB, "test_i8085_contract.exe")]))
# MAME's V40 core (the Speak-Out's, src/csrc/cpu/v40_mame.cpp) and the Speak-Out board on it (src/csrc/speakout): the
# add-on's default since 0.7 (mame-steps).  The contract's clauses, the board's own rules, and, with the firmware,
# the board against the old Unicorn host, selected explicitly as a development reference (speakout_core_compare.py:
# steps coupled as Unicorn's instructions, every write identical -- the shipped mame-steps; clocks at 8 MHz,
# experimental: a speed grade, not a measured clock -- the speech frames identical, the times classified) with its
# must-fail control, and the driver on the MAME core: mame-steps gated, mame a smoke test (Astra, Reply 105).  Built
# by src/csrc/speakout/build_board.py.
SO_LIB = os.path.join(os.path.dirname(HERE), "dist", "speakout-lib")
if os.path.isfile(os.path.join(SO_LIB, "test_v40_contract.exe")):
    CHECKS.append(check("MAME V40 core: CPU contract tests", [os.path.join(SO_LIB, "test_v40_contract.exe")]))
    CHECKS.append(check("Speak-Out board (MAME V40): its rules", [os.path.join(SO_LIB, "test_so_board.exe")]))
    SO_HEX = os.path.join(os.path.dirname(os.path.dirname(HERE)), "firmware", "gw-micro-speakout", "SPEAKOUT.HEX")
    if os.path.isfile(SO_HEX) and os.path.isfile(os.path.join(SO_LIB, "x64", "speakout_v40.dll")):
        CHECKS.append(check("Speak-Out: MAME V40 core against Unicorn", [PY, "speakout_core_compare.py"]))
        CHECKS.append(check("Speak-Out: MAME V40 core CONTROL (a value flipped, must fail)",
                            [PY, "speakout_core_compare.py"], env={"SPEAKOUT_COMPARE_FLIP": "1"}, expect_fail=True,
                            fail_marks=[r"^mame-steps: write values DIFFER from Unicorn's at write \d+ of",
                                        r"^  utterance 1: speech frames DIFFER at frame \d+",
                                        r"^speakout cores: 2 FAILED$"]))
        # mame-steps is the default since 0.7: the plain driver_sim speakout checks (64- and 32-bit) and complete_fuzz
        # speakout above gate it (their explicit mame-steps copies here are gone).  mame (8 MHz clocks) stays a
        # labelled experimental smoke test only
        CHECKS.append(check("driver_sim speakout on the MAME V40 core (mame at 8 MHz: experimental smoke test)",
                            [PY, "-S", "driver_sim.py", "speakout", NVDA, "rt"], env={"SSI263_SPEAKOUT_CORE": "mame"}))
    # so_voice (src/csrc/speakout/so_voice.c, so_host.c: the driver's front end and speakout.py's host in C, for
    # speech-dispatcher and Android): the real driver's PCM, byte for byte, on texts, settings, every sample rate, a
    # capital and cancels; two controls (the driver's currencies() skipped: from the currency case on; the C host's
    # cpu_ips 1 % fast: from the first case); its text path on random texts, with a control (strip() to spaces only);
    # and the C API alone (so_render: the firmware and a text into a WAV, no Python)
    if os.path.isfile(SO_HEX) and os.path.isfile(os.path.join(SO_LIB, "x64", "so_voice.dll")):
        CHECKS.append(check("so_voice = the NVDA Speak-Out driver, byte for byte", [PY, "so_voice_equiv.py"]))
        CHECKS.append(check("so_voice CONTROL (driver's currencies skipped, must fail)", [PY, "so_voice_equiv.py"],
                            env={"SO_VOICE_EQUIV_BREAK": "currencies"}, expect_fail=True,
                            fail_marks=[r"^CONTROL: the driver's currencies\(\) is skipped",
                                        r"^same  plain ", r"^DIFF  numbers\+currency ",
                                        r"^(?!21 )\d+ of 21 utterances byte-identical to the NVDA driver"]))
        CHECKS.append(check("so_voice CONTROL (C host's cpu_ips 1 % fast, must fail)", [PY, "so_voice_equiv.py"],
                            env={"SO_VOICE_EQUIV_BREAK": "timing"}, expect_fail=True,
                            fail_marks=[r"^CONTROL: the C host's cpu_ips is 1 % fast", r"^DIFF  plain ",
                                        r"^(?!21 )\d+ of 21 utterances byte-identical to the NVDA driver"]))
        CHECKS.append(check("so_voice text = the driver's, 5000 random texts", [PY, "so_voice_text_equiv.py", "5000", "1"]))
        CHECKS.append(check("so_voice text CONTROL (strip() to spaces only, must fail)",
                            [PY, "so_voice_text_equiv.py", "2000", "1"], env={"SO_VOICE_TEXT_BREAK": "1"},
                            expect_fail=True,
                            fail_marks=[r"^DIFF ", r"^(?!2000 )\d+ of 2000 texts give the box the same bytes"]))
    if os.path.isfile(SO_HEX) and os.path.isfile(os.path.join(SO_LIB, "so_render.exe")):
        CHECKS.append(check("Speak-Out: the C API alone (so_render)",
                            [os.path.join(SO_LIB, "so_render.exe"), SO_HEX, "Testing one two three.",
                             os.path.join(HERE, "out", "so_render_test.wav")],
                            ok=lambda out: bool(re.search(r"^[1-9]\d*\.\d\d s of audio in ", out, re.M))))

# The Accent SA in C (src/csrc/accentsa: its board on MAME's 8085 and accent_sa.py's host; the add-on's default since
# 0.7, SSI263_ACCENT_SA_CORE=c; the Python 8085 is the development reference): the board's rules (test_as_board.c; their must-fail
# controls, seventeen builds, are accentsa/as_controls.py's, run by hand as so_controls.py); the C host against the
# Python host (compare_accent_sa.py --quick: every write's value and chip time, the audio and the counting events
# identical) with two controls, one value flipped and the core's own counting (writes identical, the counting
# predicate must catch it); the C API alone (as_render: the ROMs from their folder, a text into a WAV, no Python); and
# the built add-on's Accent SA voice on it, 64- and 32-bit.  Built by src/csrc/accentsa/build_board.py.
ASA_LIB = os.path.join(os.path.dirname(HERE), "dist", "accentsa-lib")
ASA_DIR = os.path.join(os.path.dirname(os.path.dirname(HERE)), "src", "csrc", "accentsa")
ASA_ROMS = os.path.join(os.path.dirname(os.path.dirname(HERE)), "firmware", "aicom-accent-sa")
if os.path.isfile(os.path.join(ASA_LIB, "test_as_board.exe")):
    CHECKS.append(check("Accent SA board (MAME 8085): its rules", [os.path.join(ASA_LIB, "test_as_board.exe")]))
    CHECKS.append(check("Accent SA: the C API alone (as_render)",
                        [os.path.join(ASA_LIB, "as_render.exe"), ASA_ROMS, "Testing one two three.",
                         os.path.join(HERE, "out", "as_render_test.wav")],
                        ok=lambda out: bool(re.search(r"^[1-9]\d*\.\d\d s of audio in ", out, re.M))))
    if os.path.isfile(os.path.join(ASA_LIB, "x64", "accent_sa.dll")):
        CMP_ASA = os.path.join(ASA_DIR, "compare_accent_sa.py")
        CHECKS.append(check("Accent SA: C host = Python host (writes, times, audio, counting)", [PY, CMP_ASA, "--quick"]))
        CHECKS.append(check("Accent SA: C host CONTROL (a value flipped, must fail)",
                            [PY, CMP_ASA, "--quick", "--only", "commands"],
                            env={"ACCENTSA_COMPARE_FLIP": "1"}, expect_fail=True,
                            fail_marks=[r"^FAIL values +write values DIFFER at write 100 of \d+/\d+",
                                        r"^ok +times +every write's chip time identical", r"^accent_sa cores: FAILED$"]))
        CHECKS.append(check("Accent SA: C host CONTROL (the core's own counting, must fail)",
                            [PY, CMP_ASA, "--quick", "--only", "commands"],
                            env={"ACCENTSA_COMPARE_SLICES": "chip"}, expect_fail=True,
                            fail_marks=[r"^FAIL counting +the Python host: [1-9]\d* acceptance\(s\) ended a slice, "
                                        r"0 TRAP\(s\) in EI's shadow; the C board: 0 carried, 0 held",
                                        r"^ok +values", r"^ok +times", r"^accent_sa cores: FAILED$"]))
        CHECKS.append(check("driver_sim accentsa on the C host (NVDA 64-bit)",
                            [PY, "-S", "driver_sim.py", "accentsa", NVDA, "rt-c"], env={"SSI263_ACCENT_SA_CORE": "c"}))
        if os.path.isfile(PY37) and os.path.isdir(os.path.join(WIN7, "nvda2023app")):      # the x86 DLL
            CHECKS.append(check("driver_sim accentsa on the C host (nvda2023app, 32-bit)",
                                [PY37, "run37.py", "../driver_sim.py", "accentsa", "nvda2023app", "rt-c"],
                                env={"NVDA_APP": "nvda2023app", "SSI263_ACCENT_SA_CORE": "c"}, cwd=WIN7))

# MAME's 8086 core (the Accent-mini's PC, src/csrc/cpu/i86_mame.cpp; the default since 0.7, Unicorn the development
# reference, selected explicitly by compare_i86_accent.py): CONTRACT.md's
# clauses (test_i86_contract.c; its must-fail controls: cpu/i86_controls.py); the Accent-mini's scripted scenarios on
# both CPUs, every promised invariant identical -- write values and times, audio, registers, FLAGS by its policy
# (Reply 106), host log, memory after INIT but for its allow-list, INIT snapshots (cpu/compare_i86_accent.py) -- with
# a must-fail control per invariant, each perturbing exactly one of them and showing its own FAIL line beside the
# others' ok (Astra, Reply 104); and the built add-on on it (SSI263_ACCENT_CORE=mame)
if os.path.isfile(os.path.join(LIB, "test_i86_contract.exe")):
    CHECKS.append(check("MAME 8086 core: CPU contract tests", [os.path.join(LIB, "test_i86_contract.exe")]))
PC86 = os.path.join(LIB, "x64", "pc86.dll")
if os.path.isfile(PC86):
    CMP86 = os.path.join(os.path.dirname(os.path.dirname(HERE)), "src", "csrc", "cpu", "compare_i86_accent.py")
    CHECKS.append(check("MAME 8086 core: Accent-mini writes = Unicorn's", [PY, CMP86, "--quick"],
                        env={"SSI263_PC86_DLL": PC86}))
    CHECKS.append(check("MAME 8086 core: Accent-mini writes CONTROL (one value flipped, must fail)",
                        [PY, CMP86, "--quick", "--control"], env={"SSI263_PC86_DLL": PC86}, expect_fail=True,
                        fail_marks=[r"^FAIL init +write values DIFFER at write 8 of 17/17",
                                    r"^FAIL state +write values DIFFER at write \d+ of", r"^compare_i86_accent: FAILED$"]))
    OK86 = {"values": r"^ok +%s +\d+ writes, values identical", "times": r"^ok +%s +times identical",
            "audio": r"^ok +%s +audio identical", "regs": r"^ok +%s +CPU registers identical",
            "flags": r"^ok +%s +FLAGS identical in bits 0-11 at \d+ samples .* 0000 / F000 \(unicorn / mame\) at every",
            "log": r"^ok +%s +host log identical", "mem": r"^ok +init +memory after INIT identical but for 4 allowed",
            "snapshot": r"^ok +state +INIT snapshots: .* identical$"}
    for what, scen, fails, mark in (
            ("time", "init", "times", r"^FAIL init +write times DIFFER: 1 of 17, the largest \+0\.000001 s at write 8"),
            ("pcm", "state", "audio", r"^FAIL state +audio DIFFERS \(\d+ / \d+ blocks\)"),
            ("reg", "init", "regs", r"^FAIL init +CPU registers DIFFER \(ax=0000 .* \| ax=10000 "),
            ("log", "init", "log", r"^FAIL init +host log DIFFERS at line \d+ of (\d+)/\d+: unicorn None, mame "
                                   r"'\(a line the control added\)'"),
            ("mem", "init", "mem", r"^FAIL init +memory after INIT DIFFERS in 1 byte\(s\) outside the allowed FLAGS "
                                   r"images \(00500: "),
            ("allowed", "init", "mem", r"^FAIL init +memory after INIT DIFFERS in 1 byte\(s\) outside the allowed "
                                       r"FLAGS images \(9FFB5: 02/F3\)"),
            ("snapshot", "state", "snapshot", r"^FAIL state +INIT snapshots: .* DIFFER in regs$"),
            # FLAGS (Astra, Reply 106): a required flag flipped (IF; CF, an arithmetic one), the masked bits no
            # longer the recorded values, the snapshot's FLAGS
            ("flags", "init", "flags", r"^FAIL init +FLAGS DIFFER at 1 of \d+ samples: first \d+ \(INT 67h\): "
                                       r"unicorn 0002, mame F202, bits 0200 \(IF\)$"),
            ("carry", "init", "flags", r"^FAIL init +FLAGS DIFFER at 1 of \d+ samples: first \d+ \(the end\): "
                                       r"unicorn 0002, mame F003, bits 0001 \(CF\)$"),
            ("flagsmask", "init", "flags", r"^FAIL init +FLAGS bits 12-15 not the recorded 0000 / F000 at 1 of \d+ "
                                           r"samples: first \d+ \(INT 67h\): unicorn 0002, mame E002$"),
            ("snapflags", "state", "snapshot", r"^FAIL state +INIT snapshots: .*\(FLAGS_POLICY: 0002 / F202\).* "
                                               r"DIFFER in eflags$")):
        others = [OK86[k] % scen if "%s" in OK86[k] else OK86[k] for k in OK86
                  if k != fails and (k != "mem" or scen == "init") and (k != "snapshot" or scen == "state")]
        CHECKS.append(check("MAME 8086 core: Accent-mini CONTROL (%s perturbed, must fail)" % what,
                            [PY, CMP86, "--quick", "--only", scen],
                            env={"SSI263_PC86_DLL": PC86, "I86_COMPARE_PERTURB": what}, expect_fail=True,
                            fail_marks=[mark] + others + [r"^compare_i86_accent: FAILED$"]))
    CHECKS.append(check("driver_sim accent on the MAME 8086 core (NVDA 64-bit)", [PY, "-S", "driver_sim.py", "accent",
                        NVDA, "rt"], env={"SSI263_ACCENT_CORE": "mame", "SSI263_PC86_DLL": PC86}))
# the Python wheel (python/): built from this tree's libraries, installed into a fresh venv, the chip audible and
# deterministic and the Braille Lite byte for byte as bl.dll; its control (rate 70 asked for) must differ
WHEEL_TEST = os.path.join(os.path.dirname(os.path.dirname(HERE)), "python", "test_wheel.py")
if os.path.isfile(os.path.join(LIB, "x64", "bl.dll")):
    CHECKS.append(check("Python wheel (64-bit)", [PY, WHEEL_TEST, "--build", ENG]))
    CHECKS.append(check("Python wheel CONTROL (rate 70, must fail)", [PY, WHEEL_TEST, "--build", ENG],
                        env={"WHEEL_TEST_BREAK": "1"}, expect_fail=True,
                        fail_marks=[r"^ok +the chip alone:", r"^FAIL +the Braille Lite:", r"^wheel: 1 FAILED$"]))
# Windows 7 (Tomi: Windows 7 support; the README promises 7 and later): every shipped Windows binary -- the newest
# built add-ons, the SAPI stage's native files (not its python/ folder) and the Blazie emulator -- imports nothing
# Windows 7 SP1 lacks (no newer DLL, API set or function; tools/win7_missing_apis.txt, from the SDK headers) and asks
# for no subsystem above 6.1.  The controls are built by w64devkit's gcc and must fail naming what 7 lacks.
WIN7_CHECK = os.path.join(os.path.dirname(os.path.dirname(HERE)), "tools", "check_win7_imports.py")
DIST = os.path.join(os.path.dirname(HERE), "dist")
WIN7_PATHS, WIN7_EXCLUDE = [], ["python", "obj"]
for addon in ("speakout", "blazie", "accent"):
    built = [os.path.join(DIST, f) for f in os.listdir(DIST) if re.match(r"%s-ssi263-[\d.]+\.nvda-addon$" % addon, f)] \
        if os.path.isdir(DIST) else []
    if built:
        WIN7_PATHS.append(max(built, key=os.path.getmtime))
for stage in ("sapi-0.7.0-final", "sapi-0.7.0"):
    if os.path.isdir(os.path.join(DIST, stage)):
        WIN7_PATHS.append(os.path.join(DIST, stage))
        break
if os.path.isdir(os.path.join(DIST, "blazie-emu")):
    WIN7_PATHS.append(os.path.join(DIST, "blazie-emu"))
if WIN7_PATHS:
    CHECKS.append(check("Windows 7: shipped binaries import nothing 7 lacks",
                        [PY, WIN7_CHECK] + WIN7_PATHS + sum((["--exclude", x] for x in WIN7_EXCLUDE), []),
                        ok=lambda out: bool(re.search(r"^win7 imports: \d+ binaries, all load on Windows 7$", out, re.M))))
for what, marks in (
        ("win8", [r"(?i)^ +import kernel32\.dll!SetProcessInformation: not in Windows 7"]),
        ("ucrt", [r"(?i)^ +import of ucrtbase\.dll: not in Windows 7: the Universal CRT"]),
        ("subsystem", [r"^ +PE header subsystem version 6\.2: above Windows 7's 6\.1"])):
    CHECKS.append(check("Windows 7 imports CONTROL (%s, must fail)" % what, [PY, WIN7_CHECK, "--control", what],
                        expect_fail=True, fail_marks=marks + [r"^control %s: built " % what,
                                                              r"^win7 imports: 1 binaries, 1 FAILED$"]))
CHECKS.append(check("stacked_q_symbols", [PY, "-S", "stacked_q_symbols.py", NVDA]))
# the Braille Lite driver keeps the unit's channel open after speech (hiss/whine until the firmware clicks off),
# at no cost to response time; the control runs it with keep open off and must fail
CHECKS.append(check("keep the channel open", [PY, "keep_open_test.py"]))
CHECKS.append(check("keep the channel open CONTROL (off, must fail)", [PY, "keep_open_test.py"],
                    env={"KEEP_OPEN_BREAK": "1"}, expect_fail=True,
                    fail_marks=[r"^FAIL A hiss, keep open:", r"^FAIL A2 the click", r"^ok +F speech interrupting",
                                r"^keep open: 2 FAILED$"]))
# ... the click at the click-off (Tomi, 0.7: with keep open only): left out, only A2 must fail
CHECKS.append(check("keep the channel open CONTROL (no click, must fail)", [PY, "keep_open_test.py"],
                    env={"KEEP_OPEN_BREAK": "noclick"}, expect_fail=True,
                    fail_marks=[r"^ok +A hiss, keep open:", r"^FAIL A2 the click", r"^keep open: 1 FAILED$"]))
# ... and with every fed sample silent, each audio check must reject it (Astra, Reply 95: missing sound passed)
CHECKS.append(check("keep the channel open CONTROL (mute, every audio check fails)", [PY, "keep_open_test.py"],
                    env={"KEEP_OPEN_BREAK": "mute"}))
# the chip's defaults for front ends without Python (ssi263_defaults.h): still params.py's and the ROM's, as compiled
# bl_voice (the driver's front end in C, for speech-dispatcher and Android): the real driver's PCM, byte for byte;
# and its control (the C side with packing flipped) must fail
# bl_voice on a host fault (Astra, Reply 112 item 2): the utterance ends and says so, the next one recovers
CHECKS.append(check("bl_voice: a host fault ends the utterance, the next recovers", [PY, "blv_fault_test.py"]))
CHECKS.append(check("bl_voice CONTROL (fault taken as busy, must fail)", [PY, "blv_fault_test.py"],
                    env={"BLV_FAULT_TEST_BREAK": "1"}, expect_fail=True,
                    fail_marks=[r"^FAIL the faulted utterance ends and says so: .*NOT DONE",
                                r"^blv fault: 2 FAILED$"]))
CHECKS.append(check("bl_voice = the NVDA driver, byte for byte", [PY, "voice_equiv.py"]))
CHECKS.append(check("bl_voice CONTROL (packing flipped, must fail)", [PY, "voice_equiv.py"],
                    env={"VOICE_EQUIV_BREAK": "1"}, expect_fail=True,
                    fail_marks=[r"^DIFF ", r"^[0-9] of 10 utterances byte-identical to the NVDA driver$"]))
# ... and with "run ahead" on (blv_set_run_ahead, the speech-dispatcher module's SSI263RunAhead): the driver's runAhead
# path, byte for byte, the host running ahead on both sides wherever the driver does (short pauses on); its control
# never turns it on in C, so the C side speaks the lockstep
CHECKS.append(check("bl_voice = the NVDA driver with run ahead, byte for byte", [PY, "voice_equiv.py", "--run-ahead"]))
CHECKS.append(check("bl_voice run ahead CONTROL (never turned on in C, must fail)", [PY, "voice_equiv.py", "--run-ahead"],
                    env={"VOICE_EQUIV_BREAK": "ahead"}, expect_fail=True,
                    fail_marks=[r"^DIFF  en .*run ahead: driver 1, C 0, want 1  'Hello there\.'",
                                r"^DIFF  es .*run ahead: driver 1, C 0, want 1",
                                r"^[0-9] of 10 utterances byte-identical to the NVDA driver \(run ahead on\)$"]))
# bl_voice's text path (currencies, clean-up, lines, encoding) against the driver's, on random texts; and its control
CHECKS.append(check("bl_voice text = the driver's, 5000 random texts", [PY, "voice_text_equiv.py", "5000", "1"]))
CHECKS.append(check("bl_voice text CONTROL (no currencies, must fail)", [PY, "voice_text_equiv.py", "2000", "1"],
                    env={"VOICE_TEXT_BREAK": "1"}, expect_fail=True,
                    fail_marks=[r"^DIFF ", r"^(?!2000 )\d+ of 2000 texts give the unit the same bytes"]))
# Accented letters (Tomi: typing á é ő ú ű ó ü ö said nothing, "tükör" was "t k r"): src/csrc/translit.h runs first
# on every voice's text -- a letter the firmware lacks becomes its base letters ("tukor"), alone its words ("a
# acute"), decomposed input as composed ("a" U+0301 as á); the Spanish unit keeps cp850's own.  translit_test.py:
# the module (test_translit.c), every voice's bytes, the five voices speaking on fresh units (each accented text
# sounds as its ASCII spelling, each utterance complete, ASCII text the same PCM with the pass on and off), and the
# Braille Lite's NVDA driver.  Three layers (Astra, Reply 162): handwritten fixtures own the conversion; no-change
# inputs, pass on against off, hold what it must leave alone; the equivalence checks above against 0.7.0's drivers
# are current-preprocessing / frozen-downstream equivalence (translit_ref.py).  Each control turns the pass off
# (TRANSLIT_BREAK=1, 0.7's path) or makes no unit ever done, and must fail as that.
CHECKS.append(check("accented letters: translit.h (C), the table, alone and in a word, case",
                    [PY, "translit_test.py", "module"]))
CHECKS.append(check("accented letters: translit.h CONTROL (the pass off, must fail)", [PY, "translit_test.py", "module"],
                    env={"TRANSLIT_BREAK": "1"}, expect_fail=True,
                    fail_marks=[r'^FAIL lone: "\\xc3\\xa1" -> "\\xc3\\xa1", not "a acute"$',
                                r'^FAIL in a word: "t\\xc3\\xbck\\xc3\\xb6r" -> "t\\xc3\\xbck\\xc3\\xb6r", not "tukor"$',
                                r'^ok   cp850 known: "\\xc3\\xa1" -> "\\xc3\\xa1"$',
                                r"^ok   ascii: all 128 characters pass unchanged",
                                r'^FAIL decomposed, lone: "a\\xcc\\x81" -> "a\\xcc\\x81", not "a acute"$',
                                r'^FAIL several marks: "au\\xcc\\x88\\xcc\\x81to" -> ',
                                r'^ok   unattached, after a space: ',
                                r"^translit: \d+ of 83 FAILED \(TRANSLIT_BREAK=1: the pass is off\)$"]))
CHECKS.append(check("accented letters: the bytes every voice sends", [PY, "translit_test.py", "bytes"]))
CHECKS.append(check("accented letters: bytes CONTROL (the pass off, must fail)", [PY, "translit_test.py", "bytes"],
                    env={"TRANSLIT_BREAK": "1"}, expect_fail=True,
                    fail_marks=[r"^ +'\\xe1' -> b'', not b'a acute\\r\\x06\\r\\x06'$",
                                r"^ +'t\\xfck\\xf6r' -> b't k r\\r\\x06\\r\\x06', not b'tukor\\r\\x06\\r\\x06'$",
                                r"^ +'\\u0151' -> b'', not b'o double acute\\r\\x06\\r\\x06'$",
                                r"^ +'\\xe1' -> b'\\xe1\\r', not b'a acute\\r'$",
                                r"^ +'man\\u0303ana' -> b'man ana\\r\\x06\\r\\x06', not b'ma\\xa4ana\\r\\x06\\r\\x06'$",
                                r"^ +'au\\u0308\\u0301to' -> b'au to\\r\\x06\\r\\x06', not b'auto\\r\\x06\\r\\x06'$",
                                r"^FAIL blazie_es bytes ", r"^ok   sa +same +14 of 14 inputs",
                                r"^translit bytes: 5 of 10 FAILED"]))
_FW = os.path.join(os.path.dirname(os.path.dirname(HERE)), "firmware")
if all(os.path.isfile(os.path.join(_FW, *p.split("/"))) for p in (
        "blazie/BL2ENG.BNS", "blazie/spanish/BL2SPA.BNS", "gw-micro-speakout/SPEAKOUT.HEX",
        "aicom-accent-mini/SPKEMS.DVC", "aicom-accent-sa/u2.BIN")):
    CHECKS.append(check("accented letters: the five voices speak them", [PY, "translit_test.py", "voices"]))
    CHECKS.append(check("accented letters: voices CONTROL (the pass off, must fail)", [PY, "translit_test.py", "voices"],
                        env={"TRANSLIT_BREAK": "1"}, expect_fail=True,
                        fail_marks=[r"^FAIL blazie +audio +'\\xe1' alone: 0\.00 s, 0 loud samples$",
                                    r"^FAIL speakout +audio +'\\xe1' alone: 0\.00 s, 0 loud samples$",
                                    r"^FAIL blazie +audio +'t\\xfck\\xf6r' is not 0\.7's \"t k r\"",
                                    r"^FAIL sa +audio +'\\u0151' sounds as 'o double acute'",
                                    r"^ok   sa +ascii +8 of 8 ASCII texts give the same PCM",
                                    r"^translit voices: \d+ of 38 FAILED \(TRANSLIT_BREAK=1"]))
    # completion owned (Astra, Reply 162): every unit's render made never to say done, its audio still coming --
    # each utterance must be rejected as never done, not taken as audio
    CHECKS.append(check("accented letters: voices CONTROL (never done, must fail)", [PY, "translit_test.py", "voices"],
                        env={"TRANSLIT_TEST_NEVER_DONE": "1"}, expect_fail=True,
                        fail_marks=[r"^FAIL %s +audio +'\\xe1': never done after 1000 blocks \(0\.[1-9]\d s of audio "
                                    r"so far\)$" % v for v in ("blazie", "blazie_es", "speakout", "mini", "sa")]
                        + [r"^translit voices: 5 of 5 FAILED \(TRANSLIT_TEST_NEVER_DONE=1: no unit ever done\)$"]))
CHECKS.append(check("accented letters: the Braille Lite's NVDA driver", [PY, "translit_test.py", "driver"]))
CHECKS.append(check("accented letters: driver CONTROL (the pass off, must fail)", [PY, "translit_test.py", "driver"],
                    env={"TRANSLIT_BREAK": "1"}, expect_fail=True,
                    fail_marks=[r"^FAIL blazie +driver +'\\xe1': the unit was sent \[\], 0 samples of audio",
                                r"^FAIL blazie +driver +'t\\xfck\\xf6r': the unit was sent \['t k r'\]",
                                r"^ok   blazie +driver +'Hello there\.'",
                                r"^ok   blazie_es driver +'\\xf1': the unit was sent \['\\xf1'\]",
                                r"^FAIL blazie_es driver +'\\u0151': the unit was sent \[\]",
                                r"^translit driver: \d+ of 10 FAILED \(TRANSLIT_BREAK=1"]))
# other currencies than the dollar reach every unit as words (a listener: "£2.63" was read "2.63")
CHECKS.append(check("currency rule", [PY, "currency_test.py", "rules"]))
for w in ("blazie", "speakout", "accent"):
    CHECKS.append(check("currency %s: the unit is sent pounds and pence" % w, [PY, "currency_test.py", w]))
# (on the Braille Lite: its driver's front end is still Python; since 0.7.5 the Speak-Out's and the Accents' are C, their
# rule held to 0.7.0's Python one by so_voice_text_equiv.py, am_voice_equiv.py and native_driver_equiv.py)
CHECKS.append(check("currency CONTROL (rule off, must fail)", [PY, "currency_test.py", "blazie"],
                    env={"CURRENCY_OFF": "1"}, expect_fail=True,
                    fail_marks=[r"^blazie: the unit was sent .*: FAILED$"]))
CHECKS.append(check("idle channel table (bl_idle_table.h = blazie_idle.py)",
                    [PY, os.path.join(os.path.dirname(os.path.dirname(HERE)), "src", "csrc", "blazie", "gen_idle.py"),
                     "--check"]))
CHECKS.append(check("cp850 table", [PY, os.path.join(os.path.dirname(os.path.dirname(HERE)), "src", "csrc", "blazie",
                                                     "gen_cp850.py"), "--check"]))
# the hard G (0.7.6, params hold_release; Astra, Replies 146-150): watched in the chip's own state at every output sample,
# Python and C, on scripted firmware-like sessions and the live Braille Lite -- an HVC after K/KV opens half a frame before
# its end into an open phoneme, on its own stored voice with no noise, and stays shut before D, PA, a pause or a late
# answer.  Each control must fail as named: off = the 0.7.5 suppression restored (the switch); noise = the gate opening
# on the K's noise with the voice off (Reply 146's sustained-noise adversary); click = open 1 ms, then shut; early =
# opening a whole frame ahead
HARD_G = os.path.join(os.path.dirname(os.path.dirname(HERE)), "tools", "check_hard_g.py")
CHECKS.append(check("hard G: HVC after K opens into an open phoneme (chip state; Python, C, live unit)", [PY, HARD_G]))
for brk, marks in (
        ("off", [r"^FAIL  py   K HVC EH S \(guess\) +0 of 1 HVCs opened, want 1$",
                 r"^FAIL  C    K HVC EH S \(guess\) +0 of 1 HVCs opened, want 1$",
                 r"^FAIL  live guess +0 of 1 HVCs opened, want 1$", r"^ok    py   K HVC D \(big dog\) +stayed shut$",
                 r"^hard G: 19 FAILED$"]),
        ("noise", [r"^FAIL  py   K HVC EH S \(guess\) .*the voice is off while the gate is open.*noise while the gate is open",
                   r"^FAIL  live guess .*noise while the gate is open", r"^ok    py   K HVC D \(big dog\) +stayed shut$",
                   r"^hard G: 13 FAILED$"]),
        ("click", [r"^FAIL  py   K HVC EH S \(guess\) .*the gate shut again before HVC's end",
                   r"^FAIL  live guess .*the gate shut again before HVC's end", r"^hard G: 13 FAILED$"]),
        ("early", [r"^FAIL  C    K HVC EH S \(guess\) +HVC at [\d.]+ s opened 24\.\d\d ms before its end, want 12\.29 ms$",
                   r"^FAIL  live guess +HVC at [\d.]+ s opened 20\.\d\d ms before its end, want 10\.24 ms$",
                   r"^hard G: 19 FAILED$"])):
    CHECKS.append(check("hard G CONTROL (%s, must fail)" % brk, [PY, HARD_G], env={"HARD_G_BREAK": brk},
                        expect_fail=True, fail_marks=marks))
# The Mockingboard (src/csrc/mockingboard, 0.8): Sweet Micro's text-to-speech on Fake6502 and the board -- the 6502
# core's contract (test_m6502_contract.c) with its must-fail controls (cpu/m6502_controls.py), and the host's, the
# voice's and the disk reader's tests with theirs (mockingboard/mb_controls.py: each suite as it is, then each rule
# undone must fail exactly its tests).  The firmware file is a local copy (firmware/sweet-micro-mockingboard, never
# committed); the disk images come from MOCKINGBOARD_DISKS (paths.local).
REPO = os.path.dirname(os.path.dirname(HERE))
MB_FW = os.path.join(REPO, "firmware", "sweet-micro-mockingboard")
if os.path.isfile(os.path.join(MB_FW, "mockingboard-tts-1.1.bin")):
    sys.path.insert(0, REPO)
    from tools import repo_paths      # noqa: E402
    MB_DISKS = repo_paths.lookup("MOCKINGBOARD_DISKS") or ""
    # the 1.1 toolkit, another Mockingboard disk the importer refuses (disk 2: the early program with other rules),
    # and disk 1 (the early text-to-speech)
    MB_DSK = [os.path.join(MB_DISKS, n) for n in ("Sweet Micro Systems Mockingboard Developers toolkit 1984.dsk",
                                                   "Mockingboard_Disk2_Snd-Speech_Dev.dsk", "mockingboard1.dsk")]
    have_dsk = all(os.path.isfile(p) for p in MB_DSK)
    CHECKS.append(check("6502 core (Fake6502): contract and its controls",
                        [PY, os.path.join(REPO, "src", "csrc", "cpu", "m6502_controls.py")],
                        ok=lambda out: bool(re.search(r"^controls: all as expected$", out, re.M))))
    CHECKS.append(check("Mockingboard: host, voice%s tests and their controls" % (", disk" if have_dsk else ""),
                        [PY, os.path.join(REPO, "src", "csrc", "mockingboard", "mb_controls.py"), MB_FW]
                        + (MB_DSK if have_dsk else []),
                        ok=lambda out, d=have_dsk: bool(re.search(r"^controls: all as expected$", out, re.M))
                        and (not d or bool(re.search(r"^ok +dsk as it is -> 6 tests", out, re.M)))))
GEN_DEFAULTS = os.path.join(os.path.dirname(os.path.dirname(HERE)), "src", "csrc", "gen_chip_defaults.py")
CHECKS.append(check("chip defaults header", [PY, GEN_DEFAULTS, "--check"]))
# and on Python 3.7 (NVDA 2021-2023): the defaults must not depend on the Python version (3.12 changed float sum())
if os.path.isfile(PY37):
    CHECKS.append(check("chip defaults header (Python 3.7, 32-bit)", [PY37, GEN_DEFAULTS, "--check"]))


def judge_control(c, code, out, timed_out):
    """(passed, why) for a must-fail control: it passes only by failing the way it names (check's docstring)."""
    if timed_out:
        return False, "control TIMED OUT"
    if "Traceback (most recent call last)" in out:
        return False, "control CRASHED instead of failing"
    if code not in c["fail_codes"]:
        return False, "control exit %d, not its expected %s" % (code, "/".join(map(str, c["fail_codes"])))
    missing = [m for m in c["fail_marks"] if not re.search(m, out, re.M)]
    if missing:
        return False, "control's own failure not shown (missing %s)" % missing[0]
    return True, ""


def run(c):
    env = dict(os.environ, PYTHON_COLORS="0", **c["env"])
    t0 = time.perf_counter()
    timed_out, code = False, None
    try:
        p = subprocess.run(c["argv"], cwd=c["cwd"], env=env, capture_output=True, text=True, timeout=LIMIT,
                           encoding="utf-8", errors="replace")
        out, code = p.stdout + p.stderr, p.returncode
        passed = code == 0 and (c["ok"](out) if c["ok"] else True)
        why = "" if passed else "exit %d" % code
    except subprocess.TimeoutExpired:
        out, passed, why, timed_out = "", False, "TIMEOUT after %d s" % LIMIT, True
    if c["expect_fail"]:
        passed, why = judge_control(c, code, out, timed_out)
    last = [ln for ln in out.splitlines() if ln.strip() and not ln.startswith("LOG")][-1:] or [""]
    first_bad = [ln.strip() for ln in out.splitlines() if re.search(r"ended with|died|Error|FAILED|began with", ln)][:1]
    if not passed and first_bad:
        why = (why + ": " if why else "") + first_bad[0]
    if not passed:                      # the whole output kept: a failure seen only under the suite's load can be named
        try:
            keep = os.path.join(HERE, "out", "failed")
            os.makedirs(keep, exist_ok=True)
            with open(os.path.join(keep, re.sub(r"[^A-Za-z0-9._-]+", "_", c["name"])[:120] + ".log"), "w",
                      encoding="utf-8") as f:
                f.write("%s\nexit %s\n\n%s" % (" ".join(c["argv"]), code, out))
        except OSError:
            pass
    return c["name"], passed, time.perf_counter() - t0, why, last[0][:90]


def main():
    t0 = time.perf_counter()
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        results = list(pool.map(run, CHECKS))
    bad = 0
    for name, passed, secs, why, last in results:
        bad += not passed
        print("%-4s %-52s %5.0f s  %s" % ("ok" if passed else "FAIL", name, secs, (why or last)[:200]))
    print("%d of %d checks passed in %.0f s on %d cores" % (len(results) - bad, len(results), time.perf_counter() - t0,
                                                            os.cpu_count() or 0))
    return 1 if bad else 0


if __name__ == "__main__":          # importable: run_tests_guard.py tests run() and judge_control()
    sys.exit(main())
