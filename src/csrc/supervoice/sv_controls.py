"""The Super Voice's tests and their must-fail controls: test_sv_host.c and test_sv_voice.c as they are (all must
pass), then each variant undoing one rule of the host (sv_host.c) or of the voice (sv_voice.c) in a temporary copy --
exactly that rule's tests must then fail.  The runner guard (exit code, summary line, the full test inventory) is
../cpu/contract_controls.py's.  The 6809 core, the chip and the shared text path are built once; each variant is its
one file alone, built side by side (nvda/tools/run_tests.py runs this).

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
    # (cancel too: the text after a cut is then not written at once)
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

# the same for sv_voice.c
VOICE_VARIANTS = {
    "the control character and the quote taken out": (
        "        if (c == '^' || c == '\"')", "        if (0)", ["parts_rules"]),
    "a hyphen made a space": (
        "        else if (c == '-' && !isdigit((unsigned char)t[i + 1]))", "        else if (0)", ["parts_rules"]),
    # the program says "point" after "Page 1"
    "a space before a digit's period": (
        "        else if (c == '.' && i > 0 &&", "        else if (0 && c == '.' && i > 0 &&", ["parts_rules", "point"]),
    "the numbers setting": (
        "    char *t = at_say_text(utf8 ? utf8 : \"\", numbers), *o;",
        "    char *t = at_say_text(utf8 ? utf8 : \"\", 0), *o;", ["parts_text"]),
    "sentence ends first": (
        "        k = cut_after(s, n, max, \".!?\");", "        k = -1;", ["parts_split"]),
    "nothing added at the program's defaults": (
        "        if (rate != RATE_DEFAULT)", "        if (1)", ["default_plain"]),
    "the rate reaches the program": (
        "        if (rate != RATE_DEFAULT)", "        if (0)", ["settings"]),
    "the pitch reaches the program": (
        "        if (pitch != PITCH_DEFAULT)", "        if (0)", ["settings"]),
    "a capital raises the pitch": (
        "                      + (v->offset ? ", "                      + (0 ? ", ["settings"]),
    "the volume as gain": (
        "        ssi_pcm16(y, n, v->gain, v->pcm);", "        ssi_pcm16(y, n, 1.0, v->pcm);", ["volume"]),
    # the new parts would wait for the old text to end
    "a text being said is cut by the next": (
        "    svh_cancel(v->h);                       /* a text still being said is cut: the host rests the chip before the next */\n",
        "", ["speak_over"]),
}

# (its name, the file its rules are in, its test, its rules)
SUITES = [("host", "sv_host.c", "test_sv_host.c", VARIANTS), ("voice", "sv_voice.c", "test_sv_voice.c", VOICE_VARIANTS)]

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

    with tempfile.TemporaryDirectory() as tmp:
        def obj(cc, flags, src, name):
            out = os.path.join(tmp, name + ".o")
            subprocess.run([cc] + flags + inc + ["-c", "-o", out, src], env=env, check=True)
            return out

        with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            built = dict(zip(
                ("m6809_mame", "ssi263", "ssi263dsp", "accent_text", "numwords", "sv_host", "test_sv_host.c",
                 "test_sv_voice.c"),
                pool.map(lambda a: obj(*a), [
                    (gxx, CXX, os.path.join(CSRC, "cpu", "m6809_mame.cpp"), "m6809_mame"),
                    (gcc, C, os.path.join(CSRC, "ssi263.c"), "ssi263"),
                    (gcc, C, os.path.join(CSRC, "ssi263dsp.c"), "ssi263dsp"),
                    (gcc, C + ["-w"], os.path.join(CSRC, "accent_text.c"), "accent_text"),
                    (gcc, C + ["-w"], os.path.join(CSRC, "numwords.c"), "numwords"),
                    (gcc, C, os.path.join(HERE, "sv_host.c"), "sv_host"),
                    (gcc, C, os.path.join(HERE, "test_sv_host.c"), "test_sv_host"),
                    (gcc, C, os.path.join(HERE, "test_sv_voice.c"), "test_sv_voice")])))
            chip = [built["m6809_mame"], built["ssi263"], built["ssi263dsp"]]
            # what each suite's variant file is linked with
            rest = {"host": [built["test_sv_host.c"]] + chip,
                    "voice": [built["test_sv_voice.c"], built["sv_host"], built["accent_text"], built["numwords"]] + chip}
            sources = {name: open(os.path.join(HERE, name), encoding="utf-8").read() for _s, name, _t, _v in SUITES}

            def build(job):
                """the suite's test program with this copy of its file; a wrapper that runs it on the firmware folder"""
                suite, fname, k, (_rule, (old, new, _want)) = job
                text = sources[fname]
                if old is not None:
                    assert text.count(old) == 1, "the rule's code moved: %s" % old[:60]
                    text = text.replace(old, new)
                d = os.path.join(tmp, "%s_v%d" % (suite, k))
                os.mkdir(d)
                with open(os.path.join(d, fname), "w", encoding="utf-8", newline="\n") as f:
                    f.write(text)
                mine = obj(gcc, C, os.path.join(d, fname), "%s_%d" % (suite, k))
                exe = os.path.join(d, "tm.exe" if os.name == "nt" else "tm")
                subprocess.run([gxx, "-static" if os.name == "nt" else "-pthread", "-o", exe, mine] + rest[suite]
                               + ["-lm"], env=env, check=True)
                wrapper = exe + "_run" + (".cmd" if os.name == "nt" else "")
                with open(wrapper, "w") as f:
                    if os.name == "nt":
                        f.write('@"%s" "%s"\r\n@exit /b %%errorlevel%%\r\n' % (exe, fw))
                    else:
                        f.write('#!/bin/sh\nexec "%s" "%s"\n' % (exe, fw))
                if os.name != "nt":
                    os.chmod(wrapper, 0o755)
                return wrapper

            jobs = []
            for suite, fname, _test, variants in SUITES:
                jobs.append((suite, fname, 0, (None, (None, None, []))))
                jobs += [(suite, fname, k + 1, v) for k, v in enumerate(variants.items())]
            runners = list(pool.map(build, jobs))
            inventories, bad = {}, 0
            for job, runner in zip(jobs, runners):
                if job[3][0] is None:                  # each suite as it is first: its inventory
                    try:
                        results, f = run_tests(runner, suffixes=("",))
                    except RunError as e:
                        print("BAD  the %s as it is: %s" % (job[0], e))
                        sys.exit(1)
                    inventories[job[0]] = sorted(results)
                    bad += bool(f)
                    print("%-4s the %s as it is -> %d tests, failing: %s" % ("ok" if not f else "BAD", job[0],
                                                                           len(results), ", ".join(f) or "none"))

            def run_variant(pair):
                (suite, _fname, _k, (rule, (_old, _new, want))), runner = pair
                try:
                    _r, f = run_tests(runner, inventories[suite], suffixes=("",))
                except RunError as e:
                    return 1, "BAD  undone: %-48s -> the run itself failed: %s" % (rule, e)
                ok = f == sorted(want)
                return (not ok), "%-4s undone: %-48s -> failing: %s" % ("ok" if ok else "BAD", rule,
                                                                       ", ".join(f) or "none")

            for b, line in pool.map(run_variant, [(j, r) for j, r in zip(jobs, runners) if j[3][0] is not None]):
                bad += b
                print(line)
    print("controls: %s" % ("all as expected" if not bad else "%d NOT as expected" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
