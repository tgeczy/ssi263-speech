"""The Super Voice host's tests and their must-fail controls: test_sv_host.c as it is (all must pass), then each
variant undoing one rule of the host (sv_host.c) in a temporary copy -- exactly that rule's tests must then fail.
The runner guard (exit code, summary line, the full test inventory) is ../cpu/contract_controls.py's.  The 6809 core
and the chip are built once; each variant is sv_host.c alone, built side by side (nvda/tools/run_tests.py runs this).

    python src/csrc/supervoice/sv_controls.py <folder holding SUPERTOK.BIN>
"""
import os
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

# every test that speaks a text to its end and looks at when or what
SPOKEN = ["cancel", "codes", "each_text_fresh", "golden_r0", "in_step", "refusals", "starts_at_once", "stuck_guard"]

# a rule -> (its code in sv_host.c, the code with the rule undone, the tests that must then fail -- and no others)
VARIANTS = {
    # the program keeps its last phoneme and its control codes: a text after another is not a first text, the text
    # after ^V3 keeps that volume, after a cut in a long pause the chip is left without A/R (the call is given up by
    # the guard instead of returning), and the text after a call the guard gave up is not the golden one either
    "every text finds the program as loaded": (
        "    memcpy(h->mem, h->loaded, sizeof h->mem);                /* every text finds the program as loaded (sv_host.h) */\n",
        "", ["cancel", "codes", "each_text_fresh", "stuck_guard"]),
    # (cancel too: the text after a cut then starts later than the program's opening pause)
    "the chip's time stands still at a text's start": (
        "    h->skip_start = 1;\n", "    h->skip_start = 0;\n", ["cancel", "starts_at_once"]),
    # the status read at the chip's time of the last write or block end: a request is seen up to a block late, so the
    # times depend on the blocks (in_step), the first phoneme waits for one (starts_at_once), and a text's times are
    # no longer within a tick of a new host's (each_text_fresh)
    "the chip follows the 6809 at every access": (
        "        int request;\n        chip_sync(h);\n", "        int request;\n",
        ["each_text_fresh", "in_step", "starts_at_once"]),
    # read the other way round, the program writes without waiting: a text is over in milliseconds
    "D7 high is the request": (
        "        return request ? 0x80 : 0x00;", "        return request ? 0x00 : 0x80;", SPOKEN),
    "cancel writes the program's own end": (
        "    chip_write(h, 3, ssi263_reg(h->chip, 3) & 0x70);\n    chip_write(h, 4, 0xFF);\n", "", ["cancel"]),
    # the cut phoneme is still in the chip: the next text's opening gives it its amplitude back
    "the chip rested after a cut": (
        "    if (h->cut)\n        rest(h);\n", "", ["cancel"]),
    # the chip glides to the text's pitch from where its counter was: the first word climbs
    "a text's pitch lands at once": (
        "    else if (h->phonemes < 2 && (reg == 1 || reg == 2))", "    else if (0)", ["start_pitch"]),
    "a call that waits for ever is given up": (
        "        if (h->active && h->clock - h->last_write > WAIT_LIMIT) {", "        if (0) {", ["stuck_guard"]),
    "the manual's 240 characters": (
        "    if (n <= 0 || n > SVH_MAX_TEXT || h->active)", "    if (n <= 0 || h->active)", ["refusals"]),
    "a text while one is speaking is refused": (
        "    if (n <= 0 || n > SVH_MAX_TEXT || h->active)", "    if (n <= 0 || n > SVH_MAX_TEXT)", ["refusals"]),
    # a file of the right shape taken whatever its bytes: a changed byte gets through
    "the firmware's sha256": (
        "    if (!svh_is_known(image, n)) {", "    if (0) {", ["wrong_image"]),
}

C = ["-O2", "-std=gnu99", "-ffp-contract=off"]
CXX = ["-O3", "-std=c++17", "-fno-exceptions", "-fno-rtti", "-ffp-contract=off", "-w"]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    fw = os.path.abspath(sys.argv[1])
    if not os.path.isfile(os.path.join(fw, "SUPERTOK.BIN")):
        print("skipped: no SUPERTOK.BIN in %s" % fw)
        sys.exit(77)
    bindir = repo_paths.bin_dir("W64DEVKIT") if os.name == "nt" else "/usr/bin"
    env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    gcc, gxx = os.path.join(bindir, "gcc"), os.path.join(bindir, "g++")
    inc = ["-I" + CSRC, "-I" + os.path.join(CSRC, "cpu"), "-I" + HERE]
    source = open(os.path.join(HERE, "sv_host.c"), encoding="utf-8").read()

    with tempfile.TemporaryDirectory() as tmp:
        def obj(cc, flags, src, name):
            out = os.path.join(tmp, name + ".o")
            subprocess.run([cc] + flags + inc + ["-c", "-o", out, src], env=env, check=True)
            return out

        with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            shared = list(pool.map(lambda a: obj(*a), [
                (gxx, CXX, os.path.join(CSRC, "cpu", "m6809_mame.cpp"), "m6809_mame"),
                (gcc, C, os.path.join(CSRC, "ssi263.c"), "ssi263"),
                (gcc, C, os.path.join(CSRC, "ssi263dsp.c"), "ssi263dsp"),
                (gcc, C, os.path.join(HERE, "test_sv_host.c"), "test_sv_host")]))

            def build(job):
                """the test program with this sv_host.c; a wrapper that runs it on the firmware folder"""
                k, (_rule, (old, new, _want)) = job
                text = source
                if old is not None:
                    assert text.count(old) == 1, "the rule's code moved: %s" % old[:60]
                    text = text.replace(old, new)
                d = os.path.join(tmp, "v%d" % k)
                os.mkdir(d)
                with open(os.path.join(d, "sv_host.c"), "w", encoding="utf-8", newline="\n") as f:
                    f.write(text)
                host = obj(gcc, C, os.path.join(d, "sv_host.c"), "sv_host_%d" % k)
                exe = os.path.join(d, "tm.exe" if os.name == "nt" else "tm")
                subprocess.run([gxx, "-static" if os.name == "nt" else "-pthread", "-o", exe, host] + shared + ["-lm"],
                               env=env, check=True)
                wrapper = exe + "_run" + (".cmd" if os.name == "nt" else "")
                with open(wrapper, "w") as f:
                    if os.name == "nt":
                        f.write('@"%s" "%s"\r\n@exit /b %%errorlevel%%\r\n' % (exe, fw))
                    else:
                        f.write('#!/bin/sh\nexec "%s" "%s"\n' % (exe, fw))
                if os.name != "nt":
                    os.chmod(wrapper, 0o755)
                return wrapper

            jobs = [(0, (None, (None, None, [])))] + [(k + 1, v) for k, v in enumerate(VARIANTS.items())]
            runners = list(pool.map(build, jobs))
            try:
                results, f = run_tests(runners[0], suffixes=("",))
            except RunError as e:
                print("BAD  the host as it is: %s" % e)
                sys.exit(1)
            inventory = sorted(results)
            bad = bool(f)
            print("%-4s the host as it is -> %d tests, failing: %s" % ("ok" if not f else "BAD", len(results),
                                                                     ", ".join(f) or "none"))

            def run_variant(pair):
                (_k, (rule, (_old, _new, want))), runner = pair
                try:
                    _r, f = run_tests(runner, inventory, suffixes=("",))
                except RunError as e:
                    return 1, "BAD  undone: %-48s -> the run itself failed: %s" % (rule, e)
                ok = f == sorted(want)
                return (not ok), "%-4s undone: %-48s -> failing: %s" % ("ok" if ok else "BAD", rule,
                                                                       ", ".join(f) or "none")

            for b, line in pool.map(run_variant, list(zip(jobs, runners))[1:]):
                bad += b
                print(line)
    print("controls: %s" % ("all as expected" if not bad else "%d NOT as expected" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
