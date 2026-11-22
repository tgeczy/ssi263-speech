"""Must-fail controls for test_az_board.c: each variant undoes one rule of the Accent SA's board (az_board.c) in a
temporary copy, and exactly that rule's tests must then fail.  The runner guard (exit code, summary line, the full
test inventory) is ../cpu/contract_controls.py's.

    python src/csrc/accentz80/az_controls.py

The core (../cpu/z80_mame.cpp) and the 8251 (../accentpc/as_usart.c, whose own rules ../accentpc/as_controls.py
undoes) are compiled once: no variant touches them.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CSRC = os.path.dirname(HERE)
CPU = os.path.join(CSRC, "cpu")
PC = os.path.join(CSRC, "accentpc")
REPO = os.path.dirname(os.path.dirname(CSRC))
sys.path.insert(0, REPO)
sys.path.insert(0, CPU)
from tools import repo_paths  # noqa: E402
from contract_controls import RunError, run_tests  # noqa: E402

B = "az_board.c"
SERIAL = ["rxrdy", "two_boards"]
VARIANTS = {
    "exact ROM sizes": ("n5 != AZ_U5_SIZE", "n5 > AZ_U5_SIZE", ["rom_sizes"]),
    "the switches' fixed bits at power-up": ("    b->switches = 0x10;", "    b->switches = 0x00;", ["power_up"]),
    "MODE 1 is U4's upper half": ("        return b->u4[a];", "        return b->u4[a & 0x7FFF];", ["memory_map"]),
    "7000h is the RAM's upper half": ("&b->ram[0x1000 | (a & 0x0FFF)]", "&b->ram[a & 0x0FFF]", ["memory_map"]),
    "MODE 3 mirrors the RAM's lower half": ("        return &b->ram[a & 0x0FFF];\n    return NULL;",
                                            "        return &b->ram[a & 0x1FFF];\n    return NULL;", ["memory_map"]),
    "ROM ignores writes": ("        *cell = v;\n", "        *cell = v;\n    else\n        b->u5[a & 0xFFFF] = v;\n",
                           ["memory_map"]),
    "the device by the port's high byte": ("    return port >> 12;", "    return (port >> 4) & 0x0F;", ["io_decode"]),
    "A/R in bit 7 of a chip read": ("        return b->chip.request(b->chip.ctx) ? 0x80 : 0x00;",
                                    "        return b->chip.request(b->chip.ctx) ? 0x01 : 0x00;", ["io_decode"]),
    "the chip's registers straight on A0-A2": ("b->chip.write(b->chip.ctx, port & 7, v);",
                                               "b->chip.write(b->chip.ctx, 4 - (port & 7), v);",
                                               ["io_decode", "nmi_gate", "nmi_release", "rxrdy"]),
    "the switches at 4xxx": ("        return b->switches;", "        return 0x00;",
                             ["io_decode", "nmi_gate", "rxrdy", "two_boards"]),
    "the foot switches at 1xxx": ("        return b->foot;", "        return 0x00;", ["io_decode"]),
    "an undecoded port reads FFh": ("#define UNDRIVEN 0xFF", "#define UNDRIVEN 0x00", ["io_decode"]),
    "undecoded reads are counted": ("    b->unmapped_in++;\n", "", ["io_decode"]),
    "undecoded writes are counted": ("    b->unmapped_out++; ", "    ; ", ["io_decode"]),
    "/NMI gated by the latch's bit 4": ("b->chip.request(b->chip.ctx) && (b->latch & NMI_ENABLE);",
                                        "b->chip.request(b->chip.ctx);", ["nmi_gate"]),
    "the gate opened with A/R up is an edge at once": (
        "        b->latch = v;\n        nmi_update(b); ", "        b->latch = v;\n        ; ", ["nmi_gate"]),
    "the line given to the core only when it changes": ("    if (line == b->nmi_line)\n        return;\n", "",
                                                        ["nmi_gate"]),
    "a chip write releases the line at once": ("port & 7, v);\n        nmi_update(b); ", "port & 7, v);\n        ; ",
                                               ["nmi_release"]),
    "a loaded byte raises /INT": ("        z80_set_irq(b->cpu, Z80_INT, 1);\n", "        ;\n", SERIAL),
    "reading the data port drops /INT": ("        if (drop)\n            z80_set_irq(b->cpu, Z80_INT, 0);\n", "",
                                         SERIAL),
    "the 8251's control port at A0": ("as_usart_out(&b->usart, 0x20 | (port & 1), v);",
                                      "as_usart_out(&b->usart, 0x20, v);", SERIAL),
    "the addresses watched": ("b->seen[pc >> 3] |= (uint8_t)(1 << (pc & 7));",
                              "b->seen[(pc + 1) >> 3] |= (uint8_t)(1 << ((pc + 1) & 7));", ["take_pcs"]),
}


def build(d, shared, exe, env, bindir):
    objs = list(shared)
    for f in (B, "test_az_board.c"):
        src = os.path.join(d, f)
        o = src + ".o"
        subprocess.run([os.path.join(bindir, "gcc"), "-O2", "-std=gnu89", "-I" + CPU, "-I" + PC, "-I" + d, "-c", src,
                        "-o", o], env=env, check=True)
        objs.append(o)
    subprocess.run([os.path.join(bindir, "g++"), "-o", exe] + objs, env=env, check=True)


def main():
    bindir = repo_paths.bin_dir("W64DEVKIT") if os.name == "nt" else "/usr/bin"
    env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        core_o = os.path.join(tmp, "z80_mame.o")
        subprocess.run([os.path.join(bindir, "g++"), "-O2", "-std=c++17", "-fno-exceptions", "-fno-rtti", "-I" + CPU,
                        "-c", os.path.join(CPU, "z80_mame.cpp"), "-o", core_o], env=env, check=True)
        usart_o = os.path.join(tmp, "as_usart.o")
        subprocess.run([os.path.join(bindir, "gcc"), "-O2", "-std=gnu89", "-I" + PC, "-c",
                        os.path.join(PC, "as_usart.c"), "-o", usart_o], env=env, check=True)
        shared = [core_o, usart_o]
        d = os.path.join(tmp, "base")
        shutil.copytree(HERE, d)
        exe = os.path.join(d, "tb")
        build(d, shared, exe, env, bindir)
        try:
            results, f = run_tests(exe, suffixes=("",))
        except RunError as e:
            print("BAD  the board as it is: %s" % e)
            sys.exit(1)
        inventory = sorted(results)
        print("%-4s the board as it is -> %d tests, failing: %s" % ("ok" if not f else "BAD", len(inventory),
                                                                    ", ".join(f) or "none"))
        bad += bool(f)
        for k, (rule, (old, new, tests)) in enumerate(VARIANTS.items()):
            d = os.path.join(tmp, "v%d" % k)
            shutil.copytree(HERE, d)
            p = os.path.join(d, B)
            t = open(p, encoding="utf-8").read()
            assert t.count(old) == 1, "the rule's code moved: %s" % rule
            open(p, "w", encoding="utf-8", newline="\n").write(t.replace(old, new))
            exe = os.path.join(d, "tb")
            build(d, shared, exe, env, bindir)
            try:
                _results, f = run_tests(exe, inventory, suffixes=("",))
            except RunError as e:
                bad += 1
                print("BAD  undone: %-48s -> the run itself failed: %s" % (rule, e))
                continue
            ok = f == sorted(tests)
            bad += not ok
            print("%-4s undone: %-48s -> failing: %s" % ("ok" if ok else "BAD", rule, ", ".join(f) or "none"))
    print("controls: %s" % ("all as expected" if not bad else "%d NOT as expected" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
