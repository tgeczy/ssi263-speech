"""Must-fail controls for test_z80_contract.c: each variant undoes one rule of the MAME Z80 core -- our step driver
(z80_mame.cpp), the shim (z80_mame.hpp), a named change of the extraction, or an upstream rule the contract relies on
(z80_mame_machine.cpp) -- in a temporary copy, and exactly that rule's tests must then fail.  Proves the tests see what
they claim to (the repo's rule for any fix claim).  The runner guard (exit code, summary line, the full test inventory)
is contract_controls.py's.  nvda/tools/run_tests.py runs it: some fifty C++ builds of a few seconds each, a few at a
time (SSI263_CONTROL_JOBS to change how many).

    python src/csrc/cpu/z80_controls.py
    python src/csrc/cpu/z80_controls.py --zex <folder with zexdoc.com and zexall.com>

--zex: the controls of test_z80_zex.c instead (the exercisers are Frank Cringle's, GPL, test inputs only and not in
the repository).  One group of each exerciser as the core is, then a flag rule undone: a documented flag, which both
exercisers must report, and bits 3 and 5 of F, which zexall alone must -- the runner exiting 1 each time.

No control: the instruction T-states one by one (the bus cycles' three lengths stand for them), upstream's choices for
the undocumented opcodes, which the `undocumented` test pins as they are, and z80_regs.fault (nothing reaches
upstream's assert).
"""
import os
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
from tools import repo_paths  # noqa: E402
from contract_controls import RunError, run_tests  # noqa: E402

DRV, HPP, GEN, TEST, ZEX = "z80_mame.cpp", "z80_mame.hpp", "z80_mame_machine.cpp", "test_z80_contract.c", "test_z80_zex.c"
FILES = (DRV, HPP, GEN, TEST, ZEX, "cpu.h")
STEP_A = "    if (!inside && d.m_service_attention && !d.m_drv_fault)   // A\n"
# upstream's test for INT, in the fast loop's copy of the fetch state (two tabs; the resumed copy has three)
INT_TEST = "\n\t\t} else if (m_irq_state != CLEAR_LINE && m_iff1 && !get_service_attention<SA_AFTER_EI>()) { // SA_IRQ_ON\n"
NMI_TEST = "\n\t\tif (get_service_attention<SA_NMI_PENDING>()) {\n"
RESET_END = ("\tset_service_attention<SA_NMI_PENDING, 0>();\n\tset_service_attention<SA_AFTER_EI, 0>();\n"
             "\tset_service_attention<SA_AFTER_LDAIR, 0>();\n}")
# every test that takes an INT, and every one that takes an NMI
TAKES_INT = ["block", "boundary_irq", "ei_delay", "halt", "int_im0", "int_im1", "int_im2", "prefix_run", "priority",
             "regs_set", "reset"]
TAKES_NMI = ["ei_nmi", "halt_masked", "nmi_edge", "nmi_first", "priority"]
# every test that reads the boundaries' PCs or raises a line from one
USES_PCS = ["block", "boundary", "boundary_irq", "call_return", "ei_delay", "ei_nmi", "halt", "halt_masked", "int_im0",
            "int_im1", "int_im2", "int_level", "int_masked", "nmi_edge", "nmi_first", "prefix", "prefix_run",
            "priority", "regs_set", "reset", "undocumented"]
# every test that times a step
TIMES = ["block", "call_return", "cycles", "halt", "int_im0", "int_im1", "int_im2", "io", "nmi_edge", "nmi_first",
         "prefix", "prefix_run", "regs_set", "undocumented"]
# With a pass running on past the instruction's boundary (upstream's way) a step is as many instructions as fit in
# its 64 T-states: every test that steps fails -- but two_cores, whose cores are both wrong alike (zero_budget runs
# nothing).
SEVERAL_A_STEP = ["block", "boundary", "boundary_irq", "call_return", "cycles", "ei_delay", "ei_nmi", "every_opcode",
                  "halt", "halt_masked", "int_im0", "int_im1", "int_im2", "int_level", "int_masked", "io", "nmi_edge",
                  "nmi_first", "prefix", "prefix_run", "priority", "r_register", "regs_set", "reset", "undocumented"]
# With the acceptance pass running on into the fetch (upstream's way), a step whose core has anything to look at -- a
# line held, an EI just run, a HALT -- runs two instructions: every test with such a step fails.
TWO_A_STEP = ["block", "boundary_irq", "ei_delay", "ei_nmi", "halt", "halt_masked", "int_im0", "int_im1", "int_im2",
              "int_level", "int_masked", "nmi_edge", "nmi_first", "prefix_run", "priority", "regs_set", "reset"]

# a rule -> (its file, its code, the same code with the rule undone, how many times the code occurs, the tests that
# must then fail -- and no others)
VARIANTS = {
    # the step driver
    "acceptance at a step's A": (
        DRV, STEP_A, "    if (0)\n", 1, sorted(set(TAKES_INT + TAKES_NMI))),
    "no acceptance inside an instruction": (
        DRV, STEP_A, "    if (d.m_service_attention && !d.m_drv_fault)\n", 1, ["prefix_run"]),
    "the step limit": (
        DRV, "static const int Z80_STEP_LIMIT = 64;", "static const int Z80_STEP_LIMIT = 1 << 20;", 1,
        ["prefix_run", "regs_set"]),
    "z80_pc() in A: the interrupted address": (
        DRV, "    c->pc = inside ? d.m_prvpc.w : d.m_pc.w;\n", "", 1, ["halt", "int_im1"]),
    "a HALT slot's PC is its HALT": (
        DRV, "(uint16_t)(d.m_pc.w - (d.m_halt ? 1 : 0));", "d.m_pc.w;", 1, ["boundary_irq", "halt", "halt_masked"]),
    "a continued instruction keeps its start": (
        DRV, "    if (!inside)                              // (a continued", "    if (1)   // (a continued", 1,
        ["prefix_run"]),
    # the boundary comes before E: before the instruction's writes
    "the boundary before E": (
        DRV, "    if (c->bus.boundary)\n        c->bus.boundary(c->bus.ctx, c->pc);\n\n"
        "    int ins = d.m_drv_fault ? 4 : d.drv_pass(false, !inside);   // C, E\n",
        "    int ins = d.m_drv_fault ? 4 : d.drv_pass(false, !inside);   // C, E\n"
        "    if (c->bus.boundary)\n        c->bus.boundary(c->bus.ctx, c->pc);\n", 1, ["boundary"]),
    "regs_set leaves HALT": (
        DRV, "    d.leave_halt();\n", "", 1, ["regs_set"]),
    "regs_set ends the EI delay": (
        DRV, "    d.set_service_attention<z80_device::SA_AFTER_EI, 0>();\n", "", 1, ["regs_set"]),
    "regs_set leaves a cut instruction": (
        DRV, "    d.set_service_attention<z80_device::SA_AFTER_EI, 0>();\n    d.m_ref = 0xffff00;\n",
        "    d.set_service_attention<z80_device::SA_AFTER_EI, 0>();\n", 1, ["regs_set"]),
    "regs_set sets the flags": (
        DRV, "    d.set_f((uint8_t)in->af);\n", "", 1, ["regs_set"]),
    "regs_get makes F from the flags": (
        DRV, "(uint16_t)(d.m_af.b.h << 8 | d.get_f());", "d.m_af.w;", 1, ["reset", "undocumented"]),
    "R's bit 7 is LD R,A's": (
        DRV, "(uint8_t)((d.m_r & 0x7f) | (d.m_r2 & 0x80));", "(uint8_t)(d.m_r & 0x7f);", 1,
        ["r_register", "regs_set"]),
    "reset sets mode 0": (
        DRV, "    d.m_im = 0;\n", "", 1, ["reset"]),
    "reset zeroes the counts": (
        DRV, "    c->cycles = c->steps = 0;\n    c->pc = d.m_pc.w;\n", "    c->pc = d.m_pc.w;\n", 1, ["reset"]),
    "a zero budget runs nothing": (
        DRV, "    while (done < budget)\n        done += (uint64_t)z80_step(c);\n",
        "    do\n        done += (uint64_t)z80_step(c);\n    while (done < budget);\n", 1, ["zero_budget"]),
    "the boundary callback": (
        DRV, "        c->bus.boundary(c->bus.ctx, c->pc);\n", "        (void)0;\n", 1, USES_PCS),
    "steps counted": (
        DRV, "    c->steps++;                               // D\n", "", 1, ["boundary"]),
    "next_pc is the PC now": (
        DRV, "uint32_t z80_next_pc(const z80 *c) { return c->dev->m_pc.w; }",
        "uint32_t z80_next_pc(const z80 *c) { return c->pc; }", 1, ["call_return"]),
    # the shim
    "M1 bytes through bus.fetch": (
        HPP, "return bus->fetch ? bus->fetch(bus->ctx, a) : bus->read(bus->ctx, a);", "return bus->read(bus->ctx, a);",
        1, ["halt", "prefix"]),
    "the acknowledge's byte from irq_ack": (
        HPP, "        return v < 0 ? 0xff : v & 0xff;\n", "        return 0xff;\n", 1, ["int_im0", "int_im2"]),
    "the acknowledge asked for at all": (
        HPP, "m_data.bus->irq_ack ? m_data.bus->irq_ack(m_data.bus->ctx, Z80_INT, n) : -1;", "-1;", 1,
        ["halt", "int_im0", "int_im1", "int_im2", "priority"]),
    "mode 0: CALL's and JP's address from bytes 1 and 2": (
        HPP, "        if (im == 0 && (op == 0xcd || op == 0xc3)) {", "        if (0) {", 1, ["int_im0"]),
    "a port's 16 address bits": (
        HPP, "    u8 read_interruptible(u16 p) const { return bus->in(bus->ctx, p); }",
        "    u8 read_interruptible(u16 p) const { return bus->in(bus->ctx, p & 0xff); }", 1, ["io"]),
    # the extraction's named changes
    "a pass ends at the instruction's boundary": (
        GEN, "\n\t\tif (m_icount <= 0 || !m_drv_enter) {\n", "\n\t\tif (m_icount <= 0) {\n", 1, SEVERAL_A_STEP),
    # (looked at in E's pass too: a line raised at a boundary is taken in that same step, before its instruction)
    "the lines sampled at A only": (
        GEN, "\n\t\tif (m_drv_accept) {   // CHANGED: the lines are looked at", "\n\t\tif (true) {   // the lines", 1,
        ["boundary_irq", "ei_nmi", "prefix_run"]),
    "the acceptance pass ends before the fetch": (
        GEN, "\n\t\tif (m_drv_accept) {   // CHANGED: the acceptance pass ends here", "\n\t\tif (false) {   // ends", 1,
        TWO_A_STEP),
    # (upstream's rule for its own time 0 put back, by R: nothing has been fetched yet)
    "an NMI edge before the first step": (
        GEN, "\t\tif (m_nmi_state == CLEAR_LINE && state != CLEAR_LINE)   // CHANGED",
        "\t\tif (m_nmi_state == CLEAR_LINE && state != CLEAR_LINE && m_r)   //", 1, ["nmi_first"]),
    # upstream rules the contract states
    # (a level: a held line is taken again, after RETN and after reset)
    "NMI an edge": (
        GEN, NMI_TEST, "\n\t\tif (m_nmi_state != CLEAR_LINE) {\n", 1, ["nmi_edge", "priority", "reset"]),
    "NMI before INT": (
        GEN, NMI_TEST, "\n\t\tif (get_service_attention<SA_NMI_PENDING>() && !(m_irq_state != CLEAR_LINE && m_iff1)) {\n",
        1, ["priority"]),
    "NMI clears IFF1 and keeps IFF2": (
        GEN, "\n\t\tm_iff1 = 0;\n\t\tm_r++;\n\t\tm_icount -= 5;\n", "\n\t\tm_iff1 = m_iff2 = 0;\n\t\tm_r++;\n\t\tm_icount -= 5;\n",
        1, ["nmi_edge", "priority"]),
    "NMI: 11 T": (
        GEN, "\n\t\tm_iff1 = 0;\n\t\tm_r++;\n\t\tm_icount -= 5;\n", "\n\t\tm_iff1 = 0;\n\t\tm_r++;\n\t\tm_icount -= 4;\n", 1,
        ["nmi_edge", "nmi_first"]),
    "RETN puts IFF2 into IFF1": (
        GEN, "\n\t\tm_iff1 = m_iff2;\n", "\n", 8, ["nmi_edge", "priority"]),
    "INT clears IFF1 and IFF2": (
        GEN, "\n\t\t// clear both interrupt flip flops\n\t\tm_iff1 = m_iff2 = 0;\n", "\n", 1,
        ["block", "int_im0", "int_im1"]),
    "IFF1 masks INT": (
        GEN, INT_TEST, INT_TEST.replace(" && m_iff1", ""), 1,
        ["block", "ei_delay", "halt_masked", "int_im0", "int_im1", "int_im2", "int_level", "int_masked", "priority",
         "reset"]),
    "the instruction after EI first": (
        GEN, INT_TEST, INT_TEST.replace(" && !get_service_attention<SA_AFTER_EI>()", ""), 1,
        ["ei_delay", "int_im0", "int_im1", "int_im2", "int_level", "priority", "reset"]),
    # (a latch: a line dropped before its acceptance is still taken)
    "INT a level": (
        GEN, "\t\tm_irq_state = state;\n", "\t\tm_irq_state = m_irq_state != CLEAR_LINE ? m_irq_state : state;\n", 1,
        ["boundary_irq", "int_level"]),
    "the acknowledge cycle's two wait states": (
        GEN, "\n\t\t// 'interrupt latency' cycles\n\t\tm_icount -= 2;\n", "\n\t\tm_icount -= 0;\n", 1,
        ["halt", "int_im0", "int_im1", "int_im2"]),
    "an acceptance counts in R": (
        GEN, "\n\t\tm_irqack_cb(1);\n\t\tm_r++;\n", "\n\t\tm_irqack_cb(1);\n", 1, ["int_im1"]),
    "reset drops a latched NMI": (
        GEN, RESET_END, RESET_END.replace("\tset_service_attention<SA_NMI_PENDING, 0>();\n", ""), 1, ["reset"]),
    "reset clears IFF1 and IFF2": (
        GEN, "\tm_iff1 = 0;\n\tm_iff2 = 0;\n\n\tset_service_attention<SA_NMI_PENDING, 0>();",
        "\n\tset_service_attention<SA_NMI_PENDING, 0>();", 1, ["reset"]),
    "reset leaves HALT": (
        GEN, "{\n\tleave_halt();\n\n\tm_ref = 0xffff00;\n", "{\n\tm_ref = 0xffff00;\n", 1, ["reset"]),
    "reset clears I and R": (
        GEN, "\tm_i = 0;\n\tm_r = 0;\n\tm_r2 = 0;\n", "", 1, ["reset"]),
    # the registers are each core's own (upstream's members, kept): one made shared, two cores disturb each other
    "each core its own registers": (
        GEN, "\tPAIR16 m_hl;\n", "\tstatic inline PAIR16 m_hl;\n", 1, ["two_cores"]),
    # the bus cycles' lengths (the class's head, upstream's constructor's values) stand for the T-state counts
    "an M1 cycle: 4 T": (
        GEN, "m_m1_cycles(4), m_mreq_cycles(3),", "m_m1_cycles(5), m_mreq_cycles(3),", 1, TIMES),
    "a memory cycle: 3 T": (
        GEN, "m_m1_cycles(4), m_mreq_cycles(3),", "m_m1_cycles(4), m_mreq_cycles(4),", 1, TIMES),
    "an I/O cycle: 4 T": (
        GEN, "m_iorq_cycles(4)", "m_iorq_cycles(5)", 1, ["cycles", "io", "undocumented"]),
}
CXX = ["-O1", "-std=c++17", "-fno-exceptions", "-fno-rtti"]

# --zex: (the rule, its file, its code, the same undone, the group, what zexdoc and zexall must then say)
DAA = "\t\tif (m_f.h() || ((A & 0xf) > 9)) a += 6;\n"
ZEX_VARIANTS = [
    ("the core as it is", None, None, None, "<daa,cpl,scf,ccf>", "OK", "OK"),
    ("the core as it is", None, None, None, "aluop a,nn", "OK", "OK"),
    ("DAA's low digit (a documented result)", GEN, DAA, "\t\tif (m_f.h()) a += 6;\n", "<daa,cpl,scf,ccf>", "ERROR",
     "ERROR"),
    ("CP: bits 3 and 5 of F from the operand (undocumented)", GEN, "\t\tm_f.yx_val = value;\n\t\tm_f.c = res & 0x100;\n",
     "\t\tm_f.yx_val = res;\n\t\tm_f.c = res & 0x100;\n", "aluop a,nn", "OK", "ERROR"),
]


def build(src_dir, out_exe, env, bindir, test=TEST, opt="-O1"):
    inc = ["-I" + src_dir]
    t_o, c_o = os.path.join(src_dir, "t.o"), os.path.join(src_dir, "c.o")
    subprocess.run([os.path.join(bindir, "gcc"), "-O1", "-std=gnu89"] + inc
                   + ["-c", os.path.join(src_dir, test), "-o", t_o], env=env, check=True)
    subprocess.run([os.path.join(bindir, "g++"), opt] + CXX[1:] + inc + ["-c", os.path.join(src_dir, DRV), "-o", c_o],
                   env=env, check=True)
    subprocess.run([os.path.join(bindir, "g++"), "-o", out_exe, t_o, c_o], env=env, check=True)


def copy(to):
    os.makedirs(to)
    for name in FILES:
        shutil.copy(os.path.join(HERE, name), to)


def undo(d, fname, old, new, count=1):
    """The rule's code replaced in the copy `d`; False when it is not there `count` times."""
    p = os.path.join(d, fname)
    t = open(p, encoding="utf-8", newline="").read()
    if t.count(old) != count:
        return False
    open(p, "w", encoding="utf-8", newline="").write(t.replace(old, new))
    return True


def zex_controls(folder, tmp, env, bindir):
    """test_z80_zex.c's: each line one build and one group on both exercisers."""
    bad = 0
    for k, (rule, fname, old, new, group, doc, every) in enumerate(ZEX_VARIANTS):
        d = os.path.join(tmp, "z%d" % k)
        copy(d)
        if fname and not undo(d, fname, old, new):
            bad += 1
            print("BAD  undone: %-50s -> the rule's code moved" % rule)
            continue
        exe = os.path.join(d, "zex")
        build(d, exe, env, bindir, test=ZEX, opt="-O2")
        said = []
        for com, want in (("zexdoc.com", doc), ("zexall.com", every)):
            p = subprocess.run([exe, os.path.join(folder, com), "--only", group], capture_output=True, text=True,
                               timeout=120)
            lines = p.stdout.splitlines()
            got = "ERROR" if any("ERROR ****" in ln for ln in lines) else "OK" if any(
                ln.startswith(group) and ln.endswith("OK") for ln in lines) else "nothing"
            summary = lines[-1] if lines else ""
            # the runner's own judgement must agree: exit 0 and "all passed" with OK, exit 1 and "FAILED" with ERROR
            agrees = (p.returncode, summary.endswith(": all passed"), summary.endswith(": FAILED")) == (
                (0, True, False) if got == "OK" else (1, False, True))
            said.append("%s %s%s" % (com[:-4], got, "" if agrees else " (the runner's exit %d disagrees)" % p.returncode))
            bad += got != want or not agrees
        ok = said == ["zexdoc " + doc, "zexall " + every]
        print("%-4s %s: %-52s -> %s: %s" % ("ok" if ok else "BAD", "undone" if fname else "as it is", rule if fname
                                           else "the core", group, ", ".join(said)))
    return bad


def main():
    bindir = repo_paths.bin_dir("W64DEVKIT") if os.name == "nt" else "/usr/bin"
    env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    jobs = int(os.environ.get("SSI263_CONTROL_JOBS") or min(8, max(2, (os.cpu_count() or 4) // 2)))
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        if sys.argv[1:2] == ["--zex"] and len(sys.argv) == 3:
            for com in ("zexdoc.com", "zexall.com"):
                if not os.path.isfile(os.path.join(sys.argv[2], com)):
                    sys.exit("no %s in the folder given" % com)
            bad = zex_controls(sys.argv[2], tmp, env, bindir)
            print("zex controls: %s" % ("all as expected" if not bad else "%d NOT as expected" % bad))
            sys.exit(1 if bad else 0)
        if sys.argv[1:]:
            sys.exit(__doc__)
        base = os.path.join(tmp, "base")
        copy(base)
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

        def variant(item):
            k, (rule, (fname, old, new, count, tests)) = item
            d = os.path.join(tmp, "v%d" % k)
            copy(d)
            if not undo(d, fname, old, new, count):
                return rule, None, "the rule's code moved (not found %d time(s))" % count
            exe = os.path.join(d, "tc")
            try:
                build(d, exe, env, bindir)
                _results, f = run_tests(exe, inventory, suffixes=("",))
            except subprocess.CalledProcessError:
                return rule, None, "the variant did not build"
            except RunError as e:
                return rule, None, "the run itself failed: %s" % e
            return rule, f, sorted(tests)

        with ThreadPoolExecutor(max_workers=jobs) as pool:      # each variant in its own folder; printed in order
            for rule, f, want in pool.map(variant, enumerate(VARIANTS.items())):
                if f is None:
                    bad += 1
                    print("BAD  undone: %-50s -> %s" % (rule, want))
                    continue
                ok = f == want
                bad += not ok
                print("%-4s undone: %-50s -> failing: %s" % ("ok" if ok else "BAD", rule, ", ".join(f) or "none"))
    print("controls: %s" % ("all as expected" if not bad else "%d NOT as expected" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
