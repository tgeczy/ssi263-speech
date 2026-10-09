"""Must-fail controls for test_mockingboard.c: each variant undoes one rule of the Mockingboard's board or host in a
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

SOURCES = ["mockingboard/test_mockingboard.c", "mockingboard/mb_host.c", "mockingboard/mb_board.c", "cpu/m6502.c",
           "ssi263.c", "ssi263dsp.c"]


def build(src, out_exe, env, bindir):
    subprocess.run([os.path.join(bindir, "gcc"), "-O2", "-std=gnu89", "-ffp-contract=off", "-I" + src, "-o", out_exe]
                   + [os.path.join(src, s) for s in SOURCES] + ["-lm"], env=env, check=True)


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
        base = os.path.join(tmp, "base")
        shutil.copytree(CSRC, base, ignore=shutil.ignore_patterns("*.tar.gz", "__pycache__"))
        exe = os.path.join(base, "tm.exe" if os.name == "nt" else "tm")
        build(base, exe, env, bindir)
        try:
            results, f = run_tests(tests(exe), suffixes=("",))
        except RunError as e:
            print("BAD  as it is: %s" % e)
            sys.exit(1)
        inventory = sorted(results)
        print("%-4s as it is -> %d tests, failing: %s" % ("ok" if not f else "BAD", len(inventory), ", ".join(f) or "none"))
        bad += bool(f)
        for k, (rule, (fname, old, new, want)) in enumerate(VARIANTS.items()):
            d = os.path.join(tmp, "v%d" % k)
            shutil.copytree(CSRC, d, ignore=shutil.ignore_patterns("*.tar.gz", "__pycache__"))
            p = os.path.join(d, fname)
            t = open(p, encoding="utf-8").read()
            for o, n in (old if new is None else [(old, new)]):
                assert t.count(o) == 1, "the rule's code moved: %s" % rule
                t = t.replace(o, n)
            open(p, "w", encoding="utf-8").write(t)
            exe = os.path.join(d, "tm.exe" if os.name == "nt" else "tm")
            build(d, exe, env, bindir)
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
