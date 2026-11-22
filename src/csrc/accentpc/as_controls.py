"""Must-fail controls for test_as_board.c: each variant undoes one rule of the Accent-PC board (as_board.c,
as_usart.c) in a temporary copy, and exactly that rule's tests must then fail.  The runner guard (exit code, summary
line, the full test inventory) is ../cpu/contract_controls.py's.  Not in run_tests: seventeen builds.

    python src/csrc/accentpc/as_controls.py

The core (../cpu/i8085_mame.cpp) is compiled once: no variant touches it.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CPU = os.path.join(os.path.dirname(HERE), "cpu")
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, REPO)
sys.path.insert(0, CPU)
from tools import repo_paths  # noqa: E402
from contract_controls import RunError, run_tests  # noqa: E402

B, U = "as_board.c", "as_usart.c"
VARIANTS = {
    "the window's bank from port 40h": (B, "b->bank = b->banks[v & 3];", "b->bank = b->banks[0];", ["memory_map"]),
    "bank 3 is nothing (FFh)": (B, "memset(b->banks[3], 0xFF, 0x8000);", "memset(b->banks[3], 0x00, 0x8000);",
                                ["memory_map"]),
    "ROM ignores writes": (
        B, "    if (a >= RAM_BASE && a < WINDOW)       /* RAM only: ROM and the window ignore writes */\n",
        "    if (a < RAM_BASE)\n        b->u2[a] = v;\n    else if (a < WINDOW)\n", ["memory_map"]),
    "the chip's registers reversed": (B, "b->chip.write(b->chip.ctx, 7 - p, v);",
                                      "b->chip.write(b->chip.ctx, p - 3, v);",
                                      ["chip_ports", "split_effects"]),
    "A/R in bit 7 of port 07h": (B, "b->chip.request(b->chip.ctx) ? 0x80 : 0x00;",
                                 "b->chip.request(b->chip.ctx) ? 0x01 : 0x00;", ["chip_ports"]),
    "the switches on port 40h": (B, "        return b->switches;", "        return 0x00;",
                                 ["chip_ports", "trap_gate", "rxrdy", "drop_input", "two_boards"]),
    "TRAP gated by port 40h bit 4": (B, "b->chip.request(b->chip.ctx) && (b->latch40 & 0x10);",
                                     "b->chip.request(b->chip.ctx);", ["trap_gate"]),
    "RTS gates the input": (U, "if (u->rx_ready || !u->n || !(u->cmd & AS_USART_RTS))", "if (u->rx_ready || !u->n)",
                            ["rxrdy"]),
    "RxRDY in the status": (U, "(0x85 | (u->rx_ready ? 0x02 : 0))", "(0x85 | 0)", ["rxrdy"]),
    "reading port 20h drops RST 6.5": (B, "        if (drop)\n            i8085_set_irq(b->cpu, I8085_RST65, 0);\n", "",
                                       ["rxrdy", "drop_input", "two_boards"]),
    "a command's bit 6 re-arms the mode word": (U, "            if (v & AS_USART_IR)\n", "            if (0)\n",
                                                ["usart_mode"]),
    "the input dropped": (U, "    u->head = 0;\n    u->n = 0;\n}", "}", ["drop_input"]),
    "RST 7.5 an edge": (B, "    i8085_set_irq(b->cpu, I8085_RST75, 1);  /* an edge: the core latches it */\n", "",
                        ["rst75"]),
    "an acceptance at the budget ends the slice": (
        B, "i8085_step_slice(b->cpu, budget - done, &accepted_only)", "i8085_step(b->cpu)",
        ["ei_trap", "python_split", "split_effects"]),
    "the vector's instruction is not executed early": (
        B, "        if (accepted_only) {\n", "        if (accepted_only) {\n            i8085_step(b->cpu);\n",
        ["ei_trap", "python_split", "split_effects"]),
    "TRAP held in EI's shadow": (B, "if (line && b->python_slices && b->last_op == OP_EI) {", "if (0) {", ["ei_trap"]),
    "the held TRAP given after the step": (B, "        if (b->trap_held) {                /* EI's shadow is over",
                                           "        if (0) {                /* EI's shadow is over", ["ei_trap"]),
}


def build(d, core_o, exe, env, bindir):
    objs = [core_o]
    for f in ("as_board.c", "as_usart.c", "test_as_board.c"):
        src = os.path.join(d, f)
        o = src + ".o"
        subprocess.run([os.path.join(bindir, "gcc"), "-O2", "-std=gnu89", "-I" + CPU, "-I" + d, "-c", src, "-o", o],
                       env=env, check=True)
        objs.append(o)
    subprocess.run([os.path.join(bindir, "g++"), "-o", exe] + objs, env=env, check=True)


def main():
    bindir = repo_paths.bin_dir("W64DEVKIT") if os.name == "nt" else "/usr/bin"
    env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        core_o = os.path.join(tmp, "i8085_mame.o")
        subprocess.run([os.path.join(bindir, "g++"), "-O2", "-std=c++17", "-fno-exceptions", "-fno-rtti",
                        "-Wno-sign-compare", "-I" + CPU, "-c", os.path.join(CPU, "i8085_mame.cpp"), "-o", core_o],
                       env=env, check=True)
        d = os.path.join(tmp, "base")
        shutil.copytree(HERE, d)
        exe = os.path.join(d, "tb")
        build(d, core_o, exe, env, bindir)
        try:
            results, f = run_tests(exe, suffixes=("",))
        except RunError as e:
            print("BAD  the board as it is: %s" % e)
            sys.exit(1)
        inventory = sorted(results)
        print("%-4s the board as it is -> %d tests, failing: %s" % ("ok" if not f else "BAD", len(inventory),
                                                                    ", ".join(f) or "none"))
        bad += bool(f)
        for k, (rule, (fname, old, new, tests)) in enumerate(VARIANTS.items()):
            d = os.path.join(tmp, "v%d" % k)
            shutil.copytree(HERE, d)
            p = os.path.join(d, fname)
            t = open(p, encoding="utf-8").read()
            assert t.count(old) == 1, "the rule's code moved: %s" % rule
            open(p, "w", encoding="utf-8", newline="\n").write(t.replace(old, new))
            exe = os.path.join(d, "tb")
            build(d, core_o, exe, env, bindir)
            try:
                _results, f = run_tests(exe, inventory, suffixes=("",))
            except RunError as e:
                bad += 1
                print("BAD  undone: %-42s -> the run itself failed: %s" % (rule, e))
                continue
            ok = f == sorted(tests)
            bad += not ok
            print("%-4s undone: %-42s -> failing: %s" % ("ok" if ok else "BAD", rule, ", ".join(f) or "none"))
    print("controls: %s" % ("all as expected" if not bad else "%d NOT as expected" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
