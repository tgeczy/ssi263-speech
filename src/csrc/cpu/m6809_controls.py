"""Must-fail controls for test_m6809_contract.c: each variant undoes one rule of the MAME 6809 core -- our step driver
(m6809_mame.cpp), the shim (m6809_mame.hpp), a named change of the extraction, or an upstream rule the contract relies
on (m6809_mame_machine.cpp) -- in a temporary copy, and exactly that rule's tests must then fail.  Proves the tests see
what they claim to (the repo's rule for any fix claim).  The runner guard (exit code, summary line, the full test
inventory) is contract_controls.py's.  nvda/tools/run_tests.py runs it: some fifty small C++ builds, a few at a time
(SSI263_CONTROL_JOBS to change how many), a few seconds.

    python src/csrc/cpu/m6809_controls.py

No control: the instruction cycle counts one by one (three stand for them: a long branch taken, a 16-bit indexed
offset, MUL), and upstream's own choices for the unused opcodes (01h as NEG, the byte after a prefix dropped), which
the `undefined` test pins as they are.
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

DRV, HPP, GEN, TEST = "m6809_mame.cpp", "m6809_mame.hpp", "m6809_mame_machine.cpp", "test_m6809_contract.c"
FILES = (DRV, HPP, GEN, TEST, "cpu.h")
NOT_S = " && !m_nmi_line && m_lds_encountered);"
PENDING_F = "\telse if (!(m_cc & CC_F) && m_firq_line)\n\t\treturn VECTOR_FIRQ;\n"
PENDING_I = "\telse if (!(m_cc & CC_I) && m_irq_line)\n\t\treturn VECTOR_IRQ;\n"
SYNC_TEST = "!m_nmi_asserted && !m_firq_line && !m_irq_line"
# every test that takes an interrupt at a step's A
ACCEPTS = ["boundary_irq", "firq_entry", "irq_entry", "masks", "nmi_armed", "nmi_edge", "prefix_run", "priority",
           "reset", "rti", "sync", "wait_boundary", "wait_nmi"]
# every test that reads the boundaries' PCs or raises a line from one
USES_PCS = ["boundary", "boundary_irq", "call_return", "cwai", "firq_entry", "irq_entry", "irq_level", "masks",
            "nmi_armed", "nmi_edge", "page_prefix", "prefix_run", "priority", "regs_set_wait", "reset", "sync",
            "wait_boundary", "wait_nmi"]
# With reset's vector fetch left to the first step (upstream's way), that step runs no instruction and every program
# is one step late: every test that starts at the vector fails -- but cwai_firq, regs_set_wait and two_cores, which do
# not depend on which step does what (and call_return, which starts where the host says; zero_budget runs nothing).
LATE_BY_A_STEP = ["boundary", "boundary_irq", "bus_cycles", "cwai", "cycles", "cycles_indexed", "cycles_stack",
                  "firq_entry", "free_run", "irq_entry", "irq_level", "masks", "nmi_armed", "nmi_edge", "page_prefix",
                  "prefix_run", "priority", "reset", "rti", "sync", "undefined", "wait_boundary", "wait_nmi"]

# a rule -> (its file, its code, the same code with the rule undone, how many times the code occurs, the tests that
# must then fail -- and no others)
VARIANTS = {
    # the step driver
    "acceptance at a step's A": (
        DRV, "    if (!inside && !d.m_drv_fault && d.get_pending_interrupt() != 0) {   // A", "    if (0) {", 1,
        ACCEPTS),
    "no acceptance inside an instruction": (
        DRV, "    if (!inside && !d.m_drv_fault && d.get_pending_interrupt() != 0) {   // A",
        "    if (!d.m_drv_fault && d.get_pending_interrupt() != 0) {", 1,
        ["cwai", "cwai_firq", "prefix_run", "sync", "wait_nmi"]),
    "the step limit": (
        DRV, "static const int M6809_STEP_LIMIT = 64;", "static const int M6809_STEP_LIMIT = 1 << 20;", 1,
        ["prefix_run"]),
    "m6809_pc() in A: the interrupted address": (
        DRV, "    c->pc = inside ? d.m_ppc.w : d.m_pc.w;\n", "", 1, ["irq_entry"]),
    "a continued instruction keeps its start": (
        DRV, "    if (!inside)\n        c->pc = d.m_pc.w; ", "    c->pc = d.m_pc.w; ", 1,
        ["cwai", "prefix_run", "sync", "wait_boundary"]),
    # the boundary comes before E -- before the instruction's writes, and before a wait's own test for a line
    "the boundary before E": (
        DRV, "    if (c->bus.boundary)\n        c->bus.boundary(c->bus.ctx, c->pc);\n\n"
        "    int ins = d.m_drv_fault ? 1 : d.drv_pass();   // E\n",
        "    int ins = d.m_drv_fault ? 1 : d.drv_pass();   // E\n"
        "    if (c->bus.boundary)\n        c->bus.boundary(c->bus.ctx, c->pc);\n", 1, ["boundary", "wait_boundary"]),
    "regs_set leaves a wait": (
        DRV, "    d.m_state = 0;                            // out of any wait or cut instruction", "    //", 1,
        ["regs_set_wait"]),
    "regs_set arms NMI as told": (
        DRV, "    d.m_lds_encountered = in->nmi_armed != 0;\n", "", 1, ["nmi_armed"]),
    "reset fetches the vector at once": (
        DRV, "    d.drv_pass();                             // INTERRUPT_VECTOR, with EA = FFFEh\n", "", 1, LATE_BY_A_STEP),
    "unused opcodes counted": (
        DRV, "    if (!m6809_defined(page, m_opcode)) {", "    if (0) {", 1, ["free_run", "prefix_run", "undefined"]),
    "the free run's bytes are not opcodes": (
        DRV, "    if (m_free_run)\n        return; ", "    if (0)\n        return; ", 1, ["free_run"]),
    "Table 9's page 2 (LDY #)": (
        DRV, "    0x83, 0x8C, 0x8E, 0x93,", "    0x83, 0x8C, 0x93,", 1, ["page_prefix", "prefix_run", "undefined"]),
    "a zero budget runs nothing": (
        DRV, "    while (done < budget)\n        done += (uint64_t)m6809_step(c);\n",
        "    do\n        done += (uint64_t)m6809_step(c);\n    while (done < budget);\n", 1, ["zero_budget"]),
    "the boundary callback": (
        DRV, "        c->bus.boundary(c->bus.ctx, c->pc);\n", "        (void)0;\n", 1, USES_PCS),
    "steps counted": (
        DRV, "    c->steps++;                               // D\n", "", 1, ["boundary"]),
    "next_pc is the PC now": (
        DRV, "uint32_t m6809_next_pc(const m6809 *c) { return c->dev->m_pc.w; }",
        "uint32_t m6809_next_pc(const m6809 *c) { return c->pc; }", 1, ["call_return"]),
    "the waiting state reported": (
        DRV, "(d.m_opcode == 0x13 ? M6809_IN_SYNC : M6809_IN_CWAI)", "M6809_RUNNING", 1,
        ["cwai", "regs_set_wait", "sync"]),
    # the shim
    "opcode bytes through bus.fetch": (
        HPP, "return bus->fetch ? bus->fetch(bus->ctx, a) : bus->read(bus->ctx, a);", "return bus->read(bus->ctx, a);",
        1, ["bus_cycles", "free_run", "page_prefix"]),
    # the extraction's named changes
    "MAIN samples at A only": (
        GEN, "\tswitch(m_drv_sample ? get_pending_interrupt() : 0)", "\tswitch(get_pending_interrupt())", 1,
        ["boundary_irq", "prefix_run"]),
    "SYNC: the acknowledge cycle before the test": (
        GEN, "\tdo   // CHANGED: the first sync-acknowledge cycle",
        "\tif (%s) do   // CHANGED: the first sync-acknowledge cycle" % SYNC_TEST, 1, ["sync"]),
    "a wait is a one-cycle slot": (
        GEN, "\tm_drv_waiting = true;\n\tm_drv_left = m_icount - 1;\n\tm_icount = 0;\n", "\teat(m_icount);\n", 1,
        ["cwai", "regs_set_wait", "sync"]),
    "a slot costs a cycle": (
        GEN, "\tm_drv_left = m_icount - 1;\n", "\tm_drv_left = m_icount;\n", 1, ["cwai", "every_opcode", "sync"]),
    "page 1's fetch counted": (
        GEN, "\tdrv_fetched(0);", "\t;", 1, ["free_run", "undefined"]),
    "page 2's fetch counted": (
        GEN, "\tdrv_fetched(1);", "\t;", 1, ["prefix_run", "undefined"]),
    "page 3's fetch counted": (
        GEN, "\tdrv_fetched(2);", "\t;", 1, ["undefined"]),
    # upstream rules the contract states
    "NMI only once S is loaded": (
        GEN, NOT_S, " && !m_nmi_line);", 1, ["nmi_armed"]),
    "NMI an edge": (
        GEN, "((state != CLEAR_LINE)" + NOT_S, "((state != CLEAR_LINE) && m_lds_encountered);", 1, ["nmi_edge"]),
    "LDS arms NMI": (
        GEN, "\tset_flags(CC_NZV, regop16().w);\n\tif (&regop16() == &m_s)\n\t\tm_lds_encountered = true;\n",
        "\tset_flags(CC_NZV, regop16().w);\n", 1, ["nmi_armed", "nmi_edge", "priority", "wait_nmi"]),
    "LEAS arms NMI": (
        GEN, "\tif (&regop16() == &m_s)\n\t\tm_lds_encountered = true;\n\tregop16().w = m_ea.w;\n",
        "\tregop16().w = m_ea.w;\n", 1, ["nmi_armed"]),
    "TFR r,S arms NMI": (
        GEN, "\t\tif ((param & 0x0F) == 4)\n\t\t{\n\t\t\tm_lds_encountered = true;\n\t\t}\n", "", 1, ["nmi_armed"]),
    "IRQ a level": (
        GEN, "\t\t\tm_irq_line = (state != CLEAR_LINE);\n", "\t\t\tm_irq_line = m_irq_line || (state != CLEAR_LINE);\n",
        1, ["irq_level"]),
    "FIRQ a level": (     # rti: the FIRQ it dropped comes back in place of its IRQ
        GEN, "\t\t\tm_firq_line = (state != CLEAR_LINE);\n",
        "\t\t\tm_firq_line = m_firq_line || (state != CLEAR_LINE);\n", 1, ["irq_level", "rti"]),
    # the registers are each core's own (upstream's members, kept): one made shared, two cores disturb each other
    "each core its own registers": (
        GEN, "\tPAIR16                      m_x, m_y;           // index registers\n",
        "\tstatic inline PAIR16 m_x; PAIR16 m_y;\n", 1, ["two_cores"]),
    "reset drops a latched NMI": (
        GEN, "\tm_nmi_asserted = false;\n\tm_lds_encountered = false;\n\tm_free_run = false;\n",
        "\tm_lds_encountered = false;\n\tm_free_run = false;\n", 1, ["reset"]),
    "reset disarms NMI": (
        GEN, "\tm_nmi_asserted = false;\n\tm_lds_encountered = false;\n\tm_free_run = false;\n",
        "\tm_nmi_asserted = false;\n\tm_free_run = false;\n", 1, ["reset"]),
    "reset ends the free run": (
        GEN, "\tm_nmi_asserted = false;\n\tm_lds_encountered = false;\n\tm_free_run = false;\n",
        "\tm_nmi_asserted = false;\n\tm_lds_encountered = false;\n", 1, ["free_run"]),
    "reset clears DP": (
        GEN, "\tm_dp = 0x00;        // reset direct page register\n", "", 1, ["reset"]),
    "NMI sets F and I": (
        GEN, "\tm_cc |= CC_I | CC_F;\n\tset_ea(VECTOR_NMI);\n", "\tset_ea(VECTOR_NMI);\n", 1, ["priority"]),
    "FIRQ stacks PC and CC only": (
        GEN, "\t\tm_temp.w = partial_state_registers();\n", "\t\tm_temp.w = entire_state_registers();\n", 1,
        ["firq_entry", "rti"]),
    "FIRQ clears E": (
        GEN, "\t\tm_cc &= ~CC_E;\n\t\tm_temp.w = partial_state_registers();\n",
        "\t\tm_temp.w = partial_state_registers();\n", 1, ["firq_entry"]),
    "IRQ leaves F": (
        GEN, "\tm_cc |= CC_I;\n\tset_ea(VECTOR_IRQ);\n", "\tm_cc |= CC_I | CC_F;\n\tset_ea(VECTOR_IRQ);\n", 1,
        ["irq_entry"]),
    "RTI pulls what E says": (
        GEN, "\tm_temp.w = ((m_cc & CC_E) ? entire_state_registers() : partial_state_registers()) & ~0x01;\n",
        "\tm_temp.w = entire_state_registers() & ~0x01;\n", 1, ["rti"]),
    "FIRQ before IRQ": (
        GEN, PENDING_F + PENDING_I, PENDING_I + PENDING_F.replace("else if", "else  if"), 1, ["priority"]),
    "I masks IRQ": (
        GEN, PENDING_I, PENDING_I.replace("!(m_cc & CC_I) && ", ""), 1, ["cwai", "masks", "reset", "sync"]),
    "F masks FIRQ": (
        GEN, PENDING_F, PENDING_F.replace("!(m_cc & CC_F) && ", ""), 1, ["cwai", "masks", "sync"]),
    "CWAI stacks the whole state with E set": (
        GEN, "state_146:\n\n\tm_cc |= CC_E;\n", "state_146:\n\n", 1, ["cwai", "cwai_firq"]),
    "CWAI: a FIRQ or NMI sets F too": (
        GEN, "\tm_cc |= CC_I | (m_ea.w != VECTOR_IRQ ? CC_F : 0);\n", "\tm_cc |= CC_I;\n", 1,
        ["cwai_firq", "wait_nmi"]),
    "CWAI spends the NMI's edge": (
        GEN, "\tif (m_nmi_asserted)\n\t\tm_nmi_asserted = false;\n", "", 1, ["wait_nmi"]),
    # no opcode reaches upstream's fatalerror: with an operand mode taken away (register B's), INCB and its like do
    "every operand mode is there (no fault)": (
        GEN, "\t\tcase ADDRESSING_MODE_REGISTER_B:    return m_q.r.b;\n", "", 1, ["every_opcode"]),
    "CLR reads its byte first": (
        GEN, "\nCLR8:\n\tread_operand();\n", "\nCLR8:\n\teat(1);\n", 1, ["bus_cycles", "cycles", "cycles_indexed"]),
    "a long branch taken: one more cycle": (
        GEN, "\t\tm_pc.w += m_temp.w;\n\t\tif(!hd6309_native_mode())\n", "\t\tm_pc.w += m_temp.w;\n\t\tif(0)\n", 1,
        ["cycles"]),
    "a 16-bit indexed offset: 4 more cycles": (
        GEN, "\t\t\t\tm_temp.w = ireg() + m_temp.w;\n\t\t\t\tdummy_vma(3);\n",
        "\t\t\t\tm_temp.w = ireg() + m_temp.w;\n\t\t\t\tdummy_vma(2);\n", 1, ["cycles_indexed"]),
    "MUL: 11 cycles": (
        GEN, "\tdummy_vma(hd6309_native_mode() ? 8 : 9);\n", "\tdummy_vma(8);\n", 1, ["cycles_stack"]),
}

CXX = ["-O1", "-std=c++17", "-fno-exceptions", "-fno-rtti"]


def build(src_dir, out_exe, env, bindir):
    inc = ["-I" + src_dir]
    t_o, c_o = os.path.join(src_dir, "t.o"), os.path.join(src_dir, "c.o")
    subprocess.run([os.path.join(bindir, "gcc"), "-O1", "-std=gnu89"] + inc
                   + ["-c", os.path.join(src_dir, TEST), "-o", t_o], env=env, check=True)
    subprocess.run([os.path.join(bindir, "g++")] + CXX + inc + ["-c", os.path.join(src_dir, DRV), "-o", c_o],
                   env=env, check=True)
    subprocess.run([os.path.join(bindir, "g++"), "-o", out_exe, t_o, c_o], env=env, check=True)


def copy(to):
    os.makedirs(to)
    for name in FILES:
        shutil.copy(os.path.join(HERE, name), to)


def main():
    bindir = repo_paths.bin_dir("W64DEVKIT") if os.name == "nt" else "/usr/bin"
    env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    jobs = int(os.environ.get("SSI263_CONTROL_JOBS") or min(8, max(2, (os.cpu_count() or 4) // 2)))
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
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
            p = os.path.join(d, fname)
            t = open(p, encoding="utf-8", newline="").read()
            if t.count(old) != count:
                return rule, None, "the rule's code moved (found %d times, not %d)" % (t.count(old), count)
            open(p, "w", encoding="utf-8", newline="").write(t.replace(old, new))
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
                    print("BAD  undone: %-40s -> %s" % (rule, want))
                    continue
                ok = f == want
                bad += not ok
                print("%-4s undone: %-40s -> failing: %s" % ("ok" if ok else "BAD", rule, ", ".join(f) or "none"))
    print("controls: %s" % ("all as expected" if not bad else "%d NOT as expected" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
