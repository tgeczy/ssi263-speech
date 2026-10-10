"""The Mockingboard's tests and their must-fail controls: test_mockingboard.c, test_mb_voice.c and, given the disk
images, test_mb_dsk.c, each as it is (all must pass), then each variant undoing one rule of the board, host, voice or
disk reader in a temporary copy -- exactly that rule's tests must then fail.  The runner guard (exit code, summary
line, the full test inventory) is ../cpu/contract_controls.py's.  The builds run side by side (nvda/tools/run_tests.py
runs this).

    python src/csrc/mockingboard/mb_controls.py <folder holding mockingboard-tts-1.1.bin>
        [<the Developers Toolkit .dsk> <another Mockingboard .dsk> [<Mockingboard disk 1 .dsk>]]

The host's tests run on 1.1 and, when the folder also holds mockingboard-tts-early.bin, on the early version.
"""
import os
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
CSRC = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(CSRC))
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(CSRC, "cpu"))
from tools import repo_paths  # noqa: E402
from contract_controls import RunError, run_tests  # noqa: E402

SPEECH = ["golden_r0", "streams", "clean_core", "settings", "busy_refused", "overflow_guard", "overflow_in_rules",
          "cancel"]
SPEECH += ["early_" + t for t in SPEECH]           # the host's tests run on both versions (test_mockingboard.c)

# a rule -> (its file under src/csrc, its code, the code with the rule undone, the tests that must then fail); a rule
# in two places: a list of (code, undone) pairs, and None
VARIANTS = {
    "A/R's edge flags CA1": (
        "mockingboard/mb_board.c", "        b->via_ifr |= 0x02;                        /* CA1 */\n", "",
        # 1.1's in-rules overflow is refused before anything plays; the early one's waits on the digits before it
        [t for t in SPEECH if t != "overflow_in_rules"]),
    "the text between spaces (MB$ GETTEXT)": (
        "mockingboard/mb_host.c", "    mb_board_poke(h->b, L->text, ' ');", "    mb_board_poke(h->b, L->text, 'X');",
        ["golden_r0", "overflow_guard", "cancel", "early_golden_r0", "early_overflow_guard", "early_cancel"]),
    "the overflow guard (both checks)": (
        "mockingboard/mb_host.c", [("    if (raw + 4 * marks > MBH_MAX_FRAMES) {", "    if (0) {"),
                                   ("    mb_board_guard(h->b, L->guard_lo, L->guard_hi);\n", "")],
        None, ["overflow_guard", "overflow_in_rules", "early_overflow_guard", "early_overflow_in_rules"]),
    "cancel moves the end": (
        "mockingboard/mb_host.c", "    mb_board_poke(h->b, A_END, mb_board_peek(h->b, A_PLAYING));\n", "",
        ["cancel", "early_cancel"]),
    # a file of the right size taken whatever its bytes: a changed byte gets through
    "the firmware's sha256": (
        "mockingboard/mb_host.c", "        if (hex_is(sum, LAYOUTS[k].sha256))",
        "        if (hex_is(sum, LAYOUTS[k].sha256) || n == (LAYOUTS[k].variant == MBH_V11 ? 11948u : 11309u))",
        ["wrong_image", "early_wrong_image"]),
    # the early version's guard (mb_host.c LAYOUTS): it must start after the program's own set-up, which stores the
    # prepared text's last index at 6600h, and it must watch 6600h, where a 257th R0 frame would land
    "early: the guard after its set-up": (
        "mockingboard/mb_host.c", "0x6000, 0x6600, 0x660E, 0x662C, 0x6617, 0x6600, 0x6600,",
        "0x6000, 0x6600, 0x660E, 0x662C, 0, 0x6600, 0x6600,",
        ["early_busy_refused", "early_cancel", "early_clean_core", "early_golden_r0", "early_overflow_guard",
         "early_settings", "early_streams"]),
    "early: the guard on 6600h": (
        "mockingboard/mb_host.c", "0x6000, 0x6600, 0x660E, 0x662C, 0x6617, 0x6600, 0x6600,",
        "0x6000, 0x6600, 0x660E, 0x662C, 0x6617, 0xBFFF, 0xBFFF,", ["early_overflow_in_rules"]),
    "the settings bytes": (
        "mockingboard/mb_host.c",
        "    mb_board_poke(h->b, (uint16_t)(h->L->settings + 1), (uint8_t)clamp(rate, 0, 15));\n", "",
        ["settings", "streams", "early_settings", "early_streams"]),
}

VOICE = "mockingboard/mb_voice.c"
VOICE_VARIANTS = {
    "a refused part split again": (
        VOICE, "        if (mbh_get_int(v->h, \"fault\") != MBH_FAULT_LONG || len < 2) {", "        if (1) {",
        ["refused_split"]),
    "the volume as gain": (
        VOICE, "        ssi_pcm16(y + start, count, v->gain, v->pcm);", "        ssi_pcm16(y + start, count, 1.0, v->pcm);",
        ["volume"]),
    "sentence ends first": (VOICE, "        int k = cut_after(s, n, max, \".!?\");", "        int k = -1;",
                            ["parts_split"]),
    "the numbers setting": (VOICE, "    char *t = at_say_text(utf8 ? utf8 : \"\", numbers);",
                            "    char *t = at_say_text(utf8 ? utf8 : \"\", 0);", ["parts_text"]),
}

DSK = "mockingboard/mb_dsk.c"
DSK_VARIANTS = {
    "ProDOS order read as ProDOS": (DSK, "        k.prodos = order;", "        k.prodos = 0;", ["prodos_order"]),
    # (damaged too: it finds the byte to change through the file dos_order reads)
    "the length from DOS's header": (DSK, "    need = got >= 4 ? 4 + (buf[2] | buf[3] << 8) : -1;",
                                     "    need = got;", ["damaged", "dos_order", "early_disk", "prodos_order"]),
    "only the known sets": (DSK, "    return mbh_variant(res, (size_t)*at) ? R_OK : R_OTHER;", "    return R_OK;",
                            ["damaged"]),
}

HOST_SOURCES = ["mockingboard/test_mockingboard.c", "mockingboard/mb_host.c", "mockingboard/mb_board.c", "cpu/m6502.c",
                "ssi263.c", "ssi263dsp.c"]
VOICE_SOURCES = ["mockingboard/test_mb_voice.c", VOICE, "mockingboard/mb_host.c", "mockingboard/mb_board.c",
                 "cpu/m6502.c", "accent_text.c", "numwords.c", "ssi263.c", "ssi263dsp.c"]
DSK_SOURCES = ["mockingboard/test_mb_dsk.c", DSK, "mockingboard/mb_host.c", "mockingboard/mb_board.c", "cpu/m6502.c",
               "ssi263.c", "ssi263dsp.c"]
# (name, its test program's sources, its rules)
SUITES = [("host", HOST_SOURCES, VARIANTS), ("voice", VOICE_SOURCES, VOICE_VARIANTS)]


def build(src, sources, out_exe, env, bindir):
    subprocess.run([os.path.join(bindir, "gcc"), "-O2", "-std=gnu99", "-ffp-contract=off", "-I" + src, "-o", out_exe]
                   + [os.path.join(src, s) for s in sources] + ["-lm"], env=env, check=True)


def main():
    if len(sys.argv) not in (2, 4, 5):
        sys.exit(__doc__)
    fw = os.path.abspath(sys.argv[1])
    if not os.path.isfile(os.path.join(fw, "mockingboard-tts-1.1.bin")):
        print("skipped: no mockingboard-tts-1.1.bin in %s" % fw)
        sys.exit(77)
    early = os.path.isfile(os.path.join(fw, "mockingboard-tts-early.bin"))
    args = {"host": [fw, fw] if early else [fw], "voice": [fw]}
    suites = list(SUITES)
    if len(sys.argv) >= 4 and all(os.path.isfile(p) for p in sys.argv[2:]):
        args["dsk"] = [os.path.abspath(p) for p in sys.argv[2:]]
        suites.append(("dsk", DSK_SOURCES, DSK_VARIANTS))
    else:
        print("note: the disk images are not given or not there: test_mb_dsk not run")
    bindir = repo_paths.bin_dir("W64DEVKIT") if os.name == "nt" else "/usr/bin"
    env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])

    def tests(exe, suite):
        wrapper = exe + "_run" + (".cmd" if os.name == "nt" else "")
        quoted = " ".join('"%s"' % a for a in args[suite])
        with open(wrapper, "w") as f:
            if os.name == "nt":
                f.write('@"%s" %s\r\n@exit /b %%errorlevel%%\r\n' % (exe, quoted))
            else:
                f.write('#!/bin/sh\nexec "%s" %s\n' % (exe, quoted))
        if os.name != "nt":
            os.chmod(wrapper, 0o755)
        return wrapper

    def tree(d, edits):
        """a copy of src/csrc with each (file, code, undone) applied; its test program built"""
        shutil.copytree(CSRC, d, ignore=shutil.ignore_patterns("*.tar.gz", "__pycache__"))
        for fname, o, n in edits:
            p = os.path.join(d, fname)
            t = open(p, encoding="utf-8").read()
            assert t.count(o) == 1, "the rule's code moved: %s" % o[:60]
            open(p, "w", encoding="utf-8").write(t.replace(o, n))

    with tempfile.TemporaryDirectory() as tmp:
        jobs = []                          # (suite, rule or None, its folder, its sources, its edits, the tests to fail)
        for suite, sources, variants in suites:
            jobs.append((suite, None, os.path.join(tmp, suite + "_base"), sources, [], []))
            for k, (rule, (fname, old, new, want)) in enumerate(variants.items()):
                edits = [(fname, o, n) for o, n in (old if new is None else [(old, new)])]
                jobs.append((suite, rule, os.path.join(tmp, "%s_v%d" % (suite, k)), sources, edits, sorted(want)))

        def build_one(job):
            suite, _rule, d, sources, edits, _want = job
            tree(d, edits)
            exe = os.path.join(d, "tm.exe" if os.name == "nt" else "tm")
            build(d, sources, exe, env, bindir)
            return tests(exe, suite)

        with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            runners = list(pool.map(build_one, jobs))
            # each suite as it is first: its inventory is what every variant must report
            inventories, lines, bad = {}, [], 0
            for job, runner in zip(jobs, runners):
                if job[1] is None:
                    try:
                        results, f = run_tests(runner, suffixes=("",))
                    except RunError as e:
                        print("BAD  %s as it is: %s" % (job[0], e))
                        sys.exit(1)
                    inventories[job[0]] = sorted(results)
                    print("%-4s %s as it is -> %d tests, failing: %s" % ("ok" if not f else "BAD", job[0],
                                                                       len(results), ", ".join(f) or "none"))
                    bad += bool(f)

            def run_variant(pair):
                (suite, rule, _d, _s, _e, want), runner = pair
                try:
                    _r, f = run_tests(runner, inventories[suite], suffixes=("",))
                except RunError as e:
                    return 1, "BAD  undone: %-40s -> the run itself failed: %s" % (rule, e)
                ok = f == want
                return (not ok), "%-4s undone: %-40s -> failing: %s" % ("ok" if ok else "BAD", rule,
                                                                       ", ".join(f) or "none")

            for b, line in pool.map(run_variant, [(j, r) for j, r in zip(jobs, runners) if j[1] is not None]):
                bad += b
                print(line)
    print("controls: %s" % ("all as expected" if not bad else "%d NOT as expected" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
