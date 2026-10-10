"""Must-fail controls for test_m6502_contract.c: each variant undoes one rule of the 6502 core -- our step driver
(m6502.c), a named change of the extraction, or an upstream rule the contract relies on (m6502_fake_machine.c) -- in a
temporary copy, and exactly that rule's tests must then fail.  The runner guard (exit code, summary line, the full
test inventory) is contract_controls.py's.  nvda/tools/run_tests.py runs it (ten C builds, a few seconds).

    python src/csrc/cpu/m6502_controls.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
from tools import repo_paths  # noqa: E402
from contract_controls import RunError, run_tests  # noqa: E402

DRV, GEN = "m6502.c", "m6502_fake_machine.c"
POLL = "c->i_polled = (op == 0x58 || op == 0x78 || op == 0x28) ? i_before : (c->status & I_FLAG) != 0;"
USES_PCS = ["boundary", "irq_entry", "irq_masked", "cli_delay", "sei_delay", "plp_delay", "rti_at_once", "irq_level",
            "nmi_edge", "brk"]

# a rule -> (its file, its code, the same code with the rule undone, how many times the code occurs, the tests that
# must then fail -- and no others)
VARIANTS = {
    # the step driver
    "CLI, SEI, PLP change I after the poll": (
        DRV, POLL, "c->i_polled = (c->status & I_FLAG) != 0;", 1, ["cli_delay", "plp_delay", "sei_delay"]),
    "RTI's I counts at once": (
        DRV, POLL, POLL.replace("op == 0x28)", "op == 0x28 || op == 0x40)"), 1, ["rti_at_once"]),
    "IRQ masked by I": (
        DRV, "} else if (c->irq && !c->i_polled) {", "} else if (c->irq) {", 1,
        ["cli_delay", "irq_masked", "plp_delay", "rti_at_once", "sei_delay"]),
    "NMI an edge": (
        DRV, "if (asserted && !c->nmi_line)", "if (asserted)", 1, ["nmi_edge"]),
    "B clear on an interrupt's push": (
        DRV, "push8((uint8_t)((c->status | 0x20) & ~0x10));", "push8((uint8_t)(c->status | 0x30));", 1,
        ["irq_entry"]),
    "the boundary": (
        DRV, "        c->bus.boundary(c->bus.ctx, c->pc);\n", "        (void)0;\n", 1, USES_PCS),
    "undocumented counted": (
        DRV, "    if (!DOCUMENTED[op]) {", "    if (0) {", 1, ["undocumented"]),
    # the extraction's named changes
    "decimal mode counted": (
        GEN, "        M6502_CUR->decimal++;   /* CHANGED: counted */\n", "", 2, ["decimal"]),
    # upstream rules the contract states
    "JMP ($xxFF) wraps in its page": (
        GEN, "eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); //replicate 6502 page-boundary wraparound bug",
        "eahelp2 = eahelp + 1;", 1, ["jmp_ind_wrap"]),
    "a branch across a page: 2 more": (
        GEN, "clockticks6502 += 2; //check if jump crossed a page boundary",
        "clockticks6502 += 1; //check if jump crossed a page boundary", 8, ["cycles"]),
}


def build(src_dir, out_exe, env, bindir):
    subprocess.run([os.path.join(bindir, "gcc"), "-O2", "-std=gnu89", "-I" + src_dir, "-o", out_exe,
                    os.path.join(src_dir, "test_m6502_contract.c"), os.path.join(src_dir, DRV)], env=env, check=True)


def main():
    bindir = repo_paths.bin_dir("W64DEVKIT") if os.name == "nt" else "/usr/bin"
    env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        base = os.path.join(tmp, "base")
        shutil.copytree(HERE, base)
        exe = os.path.join(base, "tc")
        build(base, exe, env, bindir)
        try:
            results, f = run_tests(exe, suffixes=("",))
        except RunError as e:
            print("BAD  the core as it is: %s" % e)
            sys.exit(1)
        inventory = sorted(results)
        print("%-4s the core as it is -> %d tests, failing: %s" % ("ok" if not f else "BAD", len(inventory),
                                                                   ", ".join(f) or "none"))
        bad += bool(f)
        for k, (rule, (fname, old, new, count, tests)) in enumerate(VARIANTS.items()):
            d = os.path.join(tmp, "v%d" % k)
            shutil.copytree(HERE, d)
            p = os.path.join(d, fname)
            t = open(p, encoding="utf-8").read()
            assert t.count(old) == count, "the rule's code moved: %s (%d)" % (rule, t.count(old))
            open(p, "w", encoding="utf-8").write(t.replace(old, new))
            exe = os.path.join(d, "tc")
            build(d, exe, env, bindir)
            try:
                _results, f = run_tests(exe, inventory, suffixes=("",))
            except RunError as e:
                bad += 1
                print("BAD  undone: %-40s -> the run itself failed: %s" % (rule, e))
                continue
            ok = f == sorted(tests)
            bad += not ok
            print("%-4s undone: %-40s -> failing: %s" % ("ok" if ok else "BAD", rule, ", ".join(f) or "none"))
    print("controls: %s" % ("all as expected" if not bad else "%d NOT as expected" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
