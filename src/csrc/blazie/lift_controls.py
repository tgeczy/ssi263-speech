"""The line-start lift's test (test_line_lift.c) and its must-fail controls: the test as it is (all must pass), then
each variant undoing one rule of the lift (bl_host.c, bl_voice.c) in a temporary copy -- exactly that rule's checks
must then fail.  The runner guard (exit code, summary line, the full test inventory) is ../cpu/contract_controls.py's.
The unit's core (MAME's Z180 and the board) is compiled once; each variant rebuilds the host and the voice.

    python src/csrc/blazie/lift_controls.py <BL2ENG.BNS> <bl2_2003_warm.state>
"""
import os
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
CSRC = os.path.dirname(HERE)
CPU = os.path.join(CSRC, "cpu")
REPO = os.path.dirname(os.path.dirname(CSRC))
sys.path.insert(0, REPO)
sys.path.insert(0, CPU)
from tools import repo_paths  # noqa: E402
from contract_controls import RunError, run_tests  # noqa: E402

INC = ["-I" + HERE, "-I" + CPU, "-I" + CSRC]
BOARD = ["-O3", "-std=gnu89", "-ffp-contract=off", "-DBL_Z180_MAME", "-w"] + INC
MAME_CXX = ["-O3", "-std=c++17", "-fno-exceptions", "-fno-rtti", "-ffp-contract=off", "-w"] + INC
# (object, source) of the unit's core, the same in every build
CORE = [("ssi263", os.path.join(CSRC, "ssi263.c"), BOARD), ("ssi263dsp", os.path.join(CSRC, "ssi263dsp.c"), BOARD)]
CORE += [(n, os.path.join(HERE, n + ".c"), BOARD)
         for n in ("bl_board", "flash29", "bl_serial", "bl_idle", "bl_clock", "tns_board", "bl_firmware", "bl_state")]
CORE += [(n, os.path.join(CPU, n + ".cpp"), MAME_CXX) for n in ("z180_mame", "z180_asci")]

# a rule -> (its file under src/csrc/blazie, its code, the code with the rule undone, the checks that must then fail)
VARIANTS = {
    "the raise is + 1Bh": ("bl_host.c", "h->lift_val = (val + 0x1B) & 0xFF;", "h->lift_val = val;", ["lifted", "pause"]),
    "a say arms the lift": ("bl_host.c", "        h->lift = v ? 1 : 0;\n", "        h->lift = 0;\n", ["lifted", "pause"]),
    "the second marker ends it": ("bl_host.c", "                h->lift = 0;                           /* the second",
                                  "                h->lift = 2;                           /* the second", ["lifted", "pause"]),
    # every utterance lifted, queued ones too: say-all would lift each line
    "only after a cancel": ("bl_voice.c", "        v->lift_next = 0;\n", "", ["say_all"]),
    "or after a pause": ("bl_voice.c", "blv_clock() - v->quiet_since >= LIFT_PAUSE_S", "0", ["pause"]),
}


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    fw, state = (os.path.abspath(a) for a in sys.argv[1:])
    if not (os.path.isfile(fw) and os.path.isfile(state)):
        print("skipped: the Braille Lite's firmware or state is not there")
        sys.exit(77)
    bindir = repo_paths.bin_dir("W64DEVKIT") if os.name == "nt" else "/usr/bin"
    env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    gcc, gxx = (os.path.join(bindir, c) for c in ("gcc", "g++"))

    with tempfile.TemporaryDirectory() as tmp:
        core = os.path.join(tmp, "core")
        os.makedirs(core)

        def compile_core(item):
            name, src, flags = item
            o = os.path.join(core, name + ".o")
            subprocess.run([gxx if src.endswith(".cpp") else gcc] + flags + ["-c", src, "-o", o], env=env, check=True)
            return o

        jobs = [(None, os.path.join(tmp, "base"), [], [])]
        for k, (rule, (fname, old, new, want)) in enumerate(VARIANTS.items()):
            jobs.append((rule, os.path.join(tmp, "v%d" % k), [(fname, old, new)], sorted(want)))

        def build_one(job, objs):
            rule, d, edits, _want = job
            os.makedirs(d)
            for f in ("bl_host.c", "bl_voice.c", "run_ahead.c", "test_line_lift.c"):
                shutil.copy2(os.path.join(HERE, f), d)
            for fname, o, n in edits:
                p = os.path.join(d, fname)
                t = open(p, encoding="utf-8").read()
                assert t.count(o) == 1, "the rule's code moved: %s" % o[:60]
                open(p, "w", encoding="utf-8").write(t.replace(o, n))
            mine = []
            for f in ("bl_host.c", "bl_voice.c", "test_line_lift.c"):
                o = os.path.join(d, f[:-2] + ".o")
                subprocess.run([gcc] + BOARD + ["-I" + d, "-c", os.path.join(d, f), "-o", o], env=env, check=True)
                mine.append(o)
            exe = os.path.join(d, "tl.exe" if os.name == "nt" else "tl")
            subprocess.run([gxx, "-static", "-o", exe] + mine + objs + ["-lm"], env=env, check=True)
            wrapper = exe + "_run" + (".cmd" if os.name == "nt" else "")
            with open(wrapper, "w") as f:
                if os.name == "nt":
                    f.write('@"%s" "%s" "%s"\r\n@exit /b %%errorlevel%%\r\n' % (exe, fw, state))
                else:
                    f.write('#!/bin/sh\nexec "%s" "%s" "%s"\n' % (exe, fw, state))
            if os.name != "nt":
                os.chmod(wrapper, 0o755)
            return wrapper

        with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            objs = list(pool.map(compile_core, CORE))
            runners = list(pool.map(lambda j: build_one(j, objs), jobs))
            try:
                results, failing = run_tests(runners[0], suffixes=("",))
            except RunError as e:
                print("BAD  the test as it is: %s" % e)
                sys.exit(1)
            inventory = sorted(results)
            bad = bool(failing)
            print("%-4s the test as it is -> %d checks, failing: %s" % ("ok" if not failing else "BAD", len(results),
                                                                        ", ".join(failing) or "none"))

            def run_variant(pair):
                (rule, _d, _e, want), runner = pair
                try:
                    _r, f = run_tests(runner, inventory, suffixes=("",))
                except RunError as e:
                    return 1, "BAD  undone: %-28s -> the run itself failed: %s" % (rule, e)
                ok = f == want
                return (not ok), "%-4s undone: %-28s -> failing: %s" % ("ok" if ok else "BAD", rule, ", ".join(f) or "none")

            for b, line in pool.map(run_variant, list(zip(jobs[1:], runners[1:]))):
                bad += b
                print(line)
    print("controls: %s" % ("all as expected" if not bad else "%d NOT as expected" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
