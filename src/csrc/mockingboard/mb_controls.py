"""Must-fail controls for test_mockingboard.c and test_mb_voice.c: each variant undoes one rule of the Mockingboard's board, host or voice in a
temporary copy, and exactly that rule's tests must then fail.  The runner guard (exit code, summary line, the full
test inventory) is ../cpu/contract_controls.py's.  Needs the firmware file; not in run_tests (seven C builds).

    python src/csrc/mockingboard/mb_controls.py <folder holding mockingboard-tts-1.1.bin>
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CSRC = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(CSRC))
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(CSRC, "cpu"))
from tools import repo_paths  # noqa: E402
from contract_controls import RunError, run_tests  # noqa: E402

SPEECH = ["golden_r0", "streams", "clean_core", "settings", "busy_refused", "overflow_guard", "cancel"]

# a rule -> (its file under src/csrc, its code, the code with the rule undone, the tests that must then fail); a rule
# in two places: a list of (code, undone) pairs, and None
VARIANTS = {
    "A/R's edge flags CA1": (
        "mockingboard/mb_board.c", "        b->via_ifr |= 0x02;                        /* CA1 */\n", "", SPEECH),
    "the text between spaces (MB$ GETTEXT)": (
        "mockingboard/mb_host.c", "    mb_board_poke(h->b, A_TEXT, ' ');", "    mb_board_poke(h->b, A_TEXT, 'X');",
        ["golden_r0", "overflow_guard", "cancel"]),
    "the overflow guard (both checks)": (
        "mockingboard/mb_host.c", [("    if (raw + 4 * marks > MBH_MAX_FRAMES) {", "    if (0) {"),
                                   ("    mb_board_guard(h->b, A_SPARE, A_SPARE + 0xFF);\n", "")],
        None, ["overflow_guard"]),
    "cancel moves the end": (
        "mockingboard/mb_host.c", "    mb_board_poke(h->b, A_END, mb_board_peek(h->b, A_PLAYING));\n", "",
        ["cancel"]),
    "the firmware's sha256": (
        "mockingboard/mb_host.c", "    if (!hex_is(sum, SHA256_HEX)) {", "    if (0) {", ["wrong_image"]),
    "the settings bytes": (
        "mockingboard/mb_host.c", "    mb_board_poke(h->b, A_SETTINGS + 1, (uint8_t)clamp(rate, 0, 15));\n", "",
        ["settings", "streams"]),
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

HOST_SOURCES = ["mockingboard/test_mockingboard.c", "mockingboard/mb_host.c", "mockingboard/mb_board.c", "cpu/m6502.c",
                "ssi263.c", "ssi263dsp.c"]
VOICE_SOURCES = ["mockingboard/test_mb_voice.c", VOICE, "mockingboard/mb_host.c", "mockingboard/mb_board.c",
                 "cpu/m6502.c", "accent_text.c", "numwords.c", "ssi263.c", "ssi263dsp.c"]
# (name, its test program's sources, its rules)
SUITES = [("host", HOST_SOURCES, VARIANTS), ("voice", VOICE_SOURCES, VOICE_VARIANTS)]


def build(src, sources, out_exe, env, bindir):
    subprocess.run([os.path.join(bindir, "gcc"), "-O2", "-std=gnu99", "-ffp-contract=off", "-I" + src, "-o", out_exe]
                   + [os.path.join(src, s) for s in sources] + ["-lm"], env=env, check=True)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    fw = os.path.abspath(sys.argv[1])
    if not os.path.isfile(os.path.join(fw, "mockingboard-tts-1.1.bin")):
        print("skipped: no mockingboard-tts-1.1.bin in %s" % fw)
        sys.exit(77)
    bindir = repo_paths.bin_dir("W64DEVKIT") if os.name == "nt" else "/usr/bin"
    env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    bad = 0

    def tests(exe):
        wrapper = exe + "_run" + (".cmd" if os.name == "nt" else "")
        with open(wrapper, "w") as f:
            if os.name == "nt":
                f.write('@"%s" "%s"\r\n@exit /b %%errorlevel%%\r\n' % (exe, fw))
            else:
                f.write('#!/bin/sh\nexec "%s" "%s"\n' % (exe, fw))
        if os.name != "nt":
            os.chmod(wrapper, 0o755)
        return wrapper

    with tempfile.TemporaryDirectory() as tmp:
        for suite, sources, variants in SUITES:
            base = os.path.join(tmp, suite + "_base")
            shutil.copytree(CSRC, base, ignore=shutil.ignore_patterns("*.tar.gz", "__pycache__"))
            exe = os.path.join(base, "tm.exe" if os.name == "nt" else "tm")
            build(base, sources, exe, env, bindir)
            try:
                results, f = run_tests(tests(exe), suffixes=("",))
            except RunError as e:
                print("BAD  %s as it is: %s" % (suite, e))
                sys.exit(1)
            inventory = sorted(results)
            print("%-4s %s as it is -> %d tests, failing: %s" % ("ok" if not f else "BAD", suite, len(inventory),
                                                               ", ".join(f) or "none"))
            bad += bool(f)
            for k, (rule, (fname, old, new, want)) in enumerate(variants.items()):
                d = os.path.join(tmp, "%s_v%d" % (suite, k))
                shutil.copytree(CSRC, d, ignore=shutil.ignore_patterns("*.tar.gz", "__pycache__"))
                p = os.path.join(d, fname)
                t = open(p, encoding="utf-8").read()
                for o, n in (old if new is None else [(old, new)]):
                    assert t.count(o) == 1, "the rule's code moved: %s" % rule
                    t = t.replace(o, n)
                open(p, "w", encoding="utf-8").write(t)
                exe = os.path.join(d, "tm.exe" if os.name == "nt" else "tm")
                build(d, sources, exe, env, bindir)
                try:
                    _r, f = run_tests(tests(exe), inventory, suffixes=("",))
                except RunError as e:
                    bad += 1
                    print("BAD  undone: %-40s -> the run itself failed: %s" % (rule, e))
                    continue
                ok = f == sorted(want)
                bad += not ok
                print("%-4s undone: %-40s -> failing: %s" % ("ok" if ok else "BAD", rule, ", ".join(f) or "none"))
    print("controls: %s" % ("all as expected" if not bad else "%d NOT as expected" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
