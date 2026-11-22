"""The MAME 8085 core (cpu.h, i8085_mame.cpp) against the Python core (src/hosts/i8085.py) on the Accent-PC firmware:
per-instruction registers and T-states at normalised phases, and the FIRST differences, by class, each with what
Intel's documentation says about it.  It decides nothing and changes nothing: it lists.  (CONTRACT.md 4 and 10:
"per-step traces against the Python core at normalised phases, each difference decided from Intel's documentation".)

    python src/csrc/cpu/trace_i8085.py <Accent-PC ROM folder: u2.BIN u3.BIN u4.BIN> [--ticks N] [--dll i8085.dll]

Both cores run on the same stub machine, driven tick by tick in lockstep, so a difference is the cores', never the
host's timing: one tick is one i8085_step() on the C core (an instruction with any acceptance before it, or one HALT
slot) and one pass of I8085.run()'s loop body on the Python core (EI's delay, acceptance, then an instruction; nothing
while halted).  External events come between ticks, by tick number, identically on both sides.
  - memory and ports: hosts/accent_pc.py's map (u2 low, RAM 7800-7FFF, the 32 KB bank by port 40h);
  - the SSI-263 is a stub: A/R (port 07 bit 7) rises AR_TICKS ticks after each R0 write, once powered up (an R3
    write with bit 7 clear); TRAP = A/R AND port 40h bit 4 (the host's gate), a level on the C core and its rising
    edge on the Python core (its trap() is edge-only);
  - the 8251: TxRDY/TxEMPTY/DSR; from FEED_TICK, TEXT arrives a byte at a time while the firmware holds RTS, each
    raising RST 6.5 (RxRDY) until port 20h is read;
  - no RST 7.5 clock (the firmware's mode 3, as accent_pc.py with tick_hz = 0).
Normalised phases: registers are compared at each instruction's start, after any acceptance (the C core's boundary
callback; the Python core just before step()); T-states per instruction and per acceptance, separately.  The C core
is built from the working tree into a temporary DLL (w64devkit, paths.local W64DEVKIT) unless --dll is given.
"""
import argparse
import ctypes as C
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(REPO, "src"))
from tools import repo_paths  # noqa: E402
from hosts.i8085 import I8085, Unimplemented  # noqa: E402

AR_TICKS = 600
FEED_TICK = 60000
TEXT = b"\x1b=F\x1b=M\x18Hello.\r"
INTR, RST55, RST65, RST75, TRAP = range(5)

# Intel's documentation, per class (MCS-80/85 Family User's Manual, Jan 1983; the 8085AH data sheet)
INTEL = {
    "F bits 1/3/5": "undefined: Intel's PUSH PSW listing marks bits 1, 3 and 5 'X: Undefined'.  MAME keeps its "
                    "undocumented V (bit 1) and K (bit 5) there; the Python core forces bit 1 = 1, bit 5 = 0.",
    "F documented bits": "S Z AC P CY: each instruction's 'Flags:' line in chapter 5 says which it sets.",
    "other registers": "a data difference; see what preceded it.",
    "instruction T": "each instruction's 'States:' line in chapter 5 (8085 column).",
    "acceptance T": "12: the RST listing ('12 (8085)') and section 2.3.5, Figure 2-19.",
    "interrupt timing": "section 2.2.7: TRAP 'is not subject to any mask or interrupt enable/disable instruction'; "
                        "the EI listing: 'enabled following the execution of the next instruction'.",
    "PC": "the instruction stream diverged: the trace stops here.",
}

READ = C.CFUNCTYPE(C.c_uint8, C.c_void_p, C.c_uint32)
WRITE = C.CFUNCTYPE(None, C.c_void_p, C.c_uint32, C.c_uint8)
IN = C.CFUNCTYPE(C.c_uint8, C.c_void_p, C.c_uint16)
OUT = C.CFUNCTYPE(None, C.c_void_p, C.c_uint16, C.c_uint8)
ACK = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int)
RX = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int)
TX = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_uint8)
PIN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int)
OPIN = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_int)
BND = C.CFUNCTYPE(None, C.c_void_p, C.c_uint32)


class Bus(C.Structure):
    _fields_ = [("ctx", C.c_void_p), ("read", READ), ("fetch", READ), ("write", WRITE), ("in_", IN), ("out", OUT),
                ("irq_ack", ACK), ("serial_rx", RX), ("serial_tx", TX), ("serial_pin", PIN),
                ("serial_out_pin", OPIN), ("boundary", BND)]


class Regs(C.Structure):
    _fields_ = [("af", C.c_uint16), ("bc", C.c_uint16), ("de", C.c_uint16), ("hl", C.c_uint16), ("sp", C.c_uint16),
                ("pc", C.c_uint16), ("im", C.c_uint8), ("halted", C.c_uint8)]


def build_dll(out_dir, control=False):
    """The core as a DLL.  control: from a copy whose acceptance is the 8080's 11 T-states -- the comparison must
    then report 'acceptance T' (a must-fail control: the trace sees a known difference)."""
    src = HERE
    if control:
        src = os.path.join(out_dir, "src")
        shutil.copytree(HERE, src)
        p = os.path.join(src, "i8085_mame_machine.cpp")
        t = open(p, encoding="utf-8").read()
        assert t.count("constexpr int I8085_ACCEPT_T = 12;") == 1
        open(p, "w", encoding="utf-8").write(t.replace("I8085_ACCEPT_T = 12;", "I8085_ACCEPT_T = 11;"))
    bindir = repo_paths.bin_dir("W64DEVKIT")
    dll = os.path.join(out_dir, "i8085.dll")
    env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    subprocess.run([os.path.join(bindir, "g++"), "-O2", "-std=c++17", "-fno-exceptions", "-fno-rtti",
                    "-Wno-sign-compare", "-shared", "-static-libstdc++", "-static-libgcc", "-I" + src, "-o", dll,
                    os.path.join(src, "i8085_mame.cpp")], env=env, check=True)
    return dll


class Board:
    """The stub Accent-PC both cores run on; `line(n, level)` sets a CPU input for the core it serves."""

    def __init__(self, rom_dir):
        u2 = open(os.path.join(rom_dir, "u2.BIN"), "rb").read()
        self.low = u2[:0x7800]
        self.banks = [u2[0x8000:], open(os.path.join(rom_dir, "u3.BIN"), "rb").read(),
                      open(os.path.join(rom_dir, "u4.BIN"), "rb").read(), b"\xff" * 0x8000]
        self.ram = bytearray(0x800)
        self.bank = self.banks[0]
        self.latch40 = 0
        self.powered = False
        self.last_r0 = None
        self.ar = False
        self.rx = bytearray(TEXT)
        self.rx_ready = False
        self.rx_byte = 0
        self.mode_next = True
        self.cmd = 0
        self.tick_no = 0
        self.writes = []                          # (tick, register, value): the SSI-263 writes
        self.line = None
        self.trap_level = False
        self.force = 0                            # ticks left of a forced TRAP pulse (--trap-after-ei)

    def read(self, a):
        a &= 0xFFFF
        if a < 0x7800:
            return self.low[a]
        if a < 0x8000:
            return self.ram[a - 0x7800]
        return self.bank[a - 0x8000]

    def write(self, a, v):
        a &= 0xFFFF
        if 0x7800 <= a < 0x8000:
            self.ram[a - 0x7800] = v

    def inp(self, p):
        p &= 0xFF
        if p == 0x07:
            return 0x80 if self.ar else 0x00
        if p == 0x20:
            self.rx_ready = False
            self.line(RST65, 0)
            return self.rx_byte
        if p == 0x21:
            return 0x85 | (0x02 if self.rx_ready else 0)
        return 0x00

    def outp(self, p, v):
        p &= 0xFF
        if 0x03 <= p <= 0x07:
            reg = 7 - p
            self.writes.append((self.tick_no, reg, v))
            if reg == 0:
                self.last_r0 = self.tick_no
                self.ar = False
            elif reg == 3 and not (v & 0x80):
                self.powered = True
        elif p == 0x40:
            self.latch40 = v
            self.bank = self.banks[v & 3]
        elif p == 0x21:
            if self.mode_next:
                self.mode_next = False
            else:
                self.cmd = v
                if v & 0x40:
                    self.mode_next = True

    def between_ticks(self):
        """Events after tick `tick_no`: A/R, the TRAP gate, one serial byte."""
        if self.powered and not self.ar and (self.last_r0 is None or self.tick_no - self.last_r0 >= AR_TICKS):
            self.ar = True
        trap = bool(self.ar and self.latch40 & 0x10) or self.force > 0
        self.force = max(0, self.force - 1)
        if trap != self.trap_level:
            self.trap_level = trap
            self.line(TRAP, int(trap))
        if self.tick_no >= FEED_TICK and self.rx and not self.rx_ready and self.cmd & 0x20:
            self.rx_byte = self.rx.pop(0)
            self.rx_ready = True
            self.line(RST65, 1)


class CCore:
    def __init__(self, lib, board):
        self.lib, self.board = lib, board
        self.rec = None
        b = board
        self._cb = [READ(lambda c, a: b.read(a)), WRITE(lambda c, a, v: b.write(a, v)),
                    IN(lambda c, p: b.inp(p)), OUT(lambda c, p, v: b.outp(p, v)), BND(self._boundary)]
        bus = Bus()
        bus.read, bus.write, bus.in_, bus.out, bus.boundary = self._cb
        self.bus = bus
        lib.i8085_create.restype = C.c_void_p
        lib.i8085_create.argtypes = [C.POINTER(Bus), C.c_double]
        for f in ("i8085_step",):
            getattr(lib, f).argtypes = [C.c_void_p]
        lib.i8085_cycles.restype = C.c_uint64
        lib.i8085_cycles.argtypes = [C.c_void_p]
        lib.i8085_regs_get.argtypes = [C.c_void_p, C.POINTER(Regs)]
        lib.i8085_set_irq.argtypes = [C.c_void_p, C.c_int, C.c_int]
        lib.i8085_destroy.argtypes = [C.c_void_p]
        self.cpu = lib.i8085_create(C.byref(bus), 3072000.0)
        self.regs = Regs()
        board.line = lambda n, level: lib.i8085_set_irq(self.cpu, n, level)

    def _boundary(self, ctx, pc):
        self.lib.i8085_regs_get(self.cpu, C.byref(self.regs))
        r = self.regs
        if r.halted:
            self.rec = None
        else:
            self.rec = (r.pc, r.af, r.bc, r.de, r.hl, r.sp, self.lib.i8085_cycles(self.cpu))

    def tick(self):
        """One step: (the instruction's start record or None for a HALT slot, acceptance T, instruction T)."""
        c0 = self.lib.i8085_cycles(self.cpu)
        self.rec = None
        self.lib.i8085_step(self.cpu)
        c1 = self.lib.i8085_cycles(self.cpu)
        if self.rec is None:
            return None, 0, 0
        return self.rec[:6], self.rec[6] - c0, c1 - self.rec[6]


class PyCore:
    def __init__(self, board):
        b = board
        self.cpu = I8085(b.read, b.write, b.inp, b.outp)
        self.trap_level = 0

        def line(n, level):
            if n == TRAP:
                if level and not self.trap_level:
                    self.cpu.trap()
                self.trap_level = level
            elif n == RST65:
                self.cpu.set_rst65(level)
            elif n == RST55:
                self.cpu.set_rst55(level)
        board.line = line

    def tick(self):
        """One pass of I8085.run()'s loop body, cut at the instruction's start."""
        cpu = self.cpu
        c0 = cpu.cycles
        while True:                               # as run(): `continue` after an acceptance
            if cpu.ei_pending:
                cpu.ei_pending = False
                cpu.ie = True
            elif cpu.trap_edge or cpu.ie:
                if cpu._interrupt():
                    continue
            break
        if cpu.halted:
            return None, cpu.cycles - c0, 0
        rec = (cpu.pc, cpu.a << 8 | cpu.f, cpu.b << 8 | cpu.c, cpu.d << 8 | cpu.e, cpu.h << 8 | cpu.l, cpu.sp)
        c1 = cpu.cycles
        cpu.step()
        return rec, c1 - c0, cpu.cycles - c1


NAMES = ["PC", "AF", "BC", "DE", "HL", "SP"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom_dir")
    ap.add_argument("--ticks", type=int, default=300000)
    ap.add_argument("--dll")
    ap.add_argument("--trap-after-ei", type=int, default=0, metavar="N",
                    help="from FEED_TICK, raise TRAP right after each of the first N EIs (CONTRACT.md 5)")
    ap.add_argument("--control", action="store_true",
                    help="build the core with an 11-T acceptance: exit 1 unless 'acceptance T' is reported")
    ap.add_argument("--first", type=int, default=20, help="how many differences to list in order")
    a = ap.parse_args()
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:   # a loaded DLL can't be deleted
        dll = a.dll or build_dll(tmp, a.control)
        lib = C.CDLL(dll)
        bc, bp = Board(a.rom_dir), Board(a.rom_dir)
        cc, pc = CCore(lib, bc), PyCore(bp)
        first = {}
        order = []
        n_ins = 0
        prev_op = None
        prev_x = None                              # the previous instruction's differences, to see where new ones start
        taken = {}                                 # (core, vector) -> acceptances
        causes = set()
        forced = 0
        stop = None
        for t in range(a.ticks):
            bc.tick_no = bp.tick_no = t
            try:
                rc, acc_c, ins_c = cc.tick()
                rp, acc_p, ins_p = pc.tick()
            except Unimplemented as e:
                stop = "tick %d: the Python core stops: %s" % (t, e)
                break
            if (rc is None) != (rp is None):
                stop = "tick %d: one core is halted, the other is not (C %s, Python %s)" % (
                    t, "halted" if rc is None else "at %04X" % rc[0], "halted" if rp is None else "at %04X" % rp[0])
                break
            if (rc is not None and a.trap_after_ei and forced < a.trap_after_ei and t >= FEED_TICK
                    and bc.read(rc[0]) == 0xFB):
                forced += 1                       # EI just ran on both: a TRAP pulse now, held for two ticks
                bc.force = bp.force = 2
            bc.between_ticks()
            bp.between_ticks()
            if rc is None:
                continue
            n_ins += 1
            op = bc.read(rc[0])
            for core, acc, r in (("C", acc_c, rc), ("Python", acc_p, rp)):
                if acc:
                    taken[core, r[0]] = taken.get((core, r[0]), 0) + 1
            diffs = []
            if rc[0] != rp[0]:
                diffs.append(("PC", "%04X" % rc[0], "%04X" % rp[0]))
            fc, fp = rc[1] & 0xFF, rp[1] & 0xFF
            if (fc ^ fp) & 0x2A:
                diffs.append(("F bits 1/3/5", "%02X" % fc, "%02X" % fp))
            if (fc ^ fp) & 0xD5:
                diffs.append(("F documented bits", "%02X" % fc, "%02X" % fp))
            for k in range(1, 6):
                v1, v2 = (rc[k] >> 8, rp[k] >> 8) if k == 1 else (rc[k], rp[k])
                if v1 != v2:
                    diffs.append(("other registers", "%s %04X" % (NAMES[k], rc[k]), "%04X" % rp[k]))
            if acc_c != acc_p:
                diffs.append(("acceptance T" if acc_c and acc_p else "interrupt timing", str(acc_c), str(acc_p)))
            if ins_c != ins_p:
                diffs.append(("instruction T", str(ins_c), str(ins_p)))
            x = {cls: (vc, vp) for cls, vc, vp in diffs}
            for cls, vc, vp in diffs:
                what = (t, n_ins, rc[0], op, prev_op, cls, vc, vp)
                if cls not in first:
                    first[cls] = [what, 0]
                first[cls][1] += 1
                # listed in order where it starts or changes (the instruction just run made it), once per class and
                # making opcode
                cause = (cls, prev_op[1] if prev_op else None)
                if len(order) < a.first and (prev_x is None or prev_x.get(cls) != (vc, vp)) and cause not in causes:
                    causes.add(cause)
                    order.append(what)
            prev_x = x
            prev_op = (rc[0], op)
            if rc[0] != rp[0]:
                stop = "tick %d: the PC diverged" % t
                break
        lib.i8085_destroy(cc.cpu)
    same_writes = bc.writes == bp.writes
    print("%d ticks, %d instructions compared; SSI-263 writes: C %d, Python %d, %s" % (
        t + 1, n_ins, len(bc.writes), len(bp.writes), "identical (values and ticks)" if same_writes else "DIFFERENT"))
    print("stopped: %s" % (stop or "no, ran to the end"))
    print("acceptances by vector: " + ", ".join("%s %04X x%d" % (k[0], k[1], v) for k, v in sorted(taken.items())))
    print("\nThe first difference in each class (tick, instruction no., PC, opcode; C core vs Python core):")
    for cls in INTEL:
        if cls in first:
            (t0, n0, pc0, op0, prev, _c, vc, vp), cnt = first[cls]
            print("  %-18s tick %d, #%d, at %04X (op %02X; the previous instruction %s): C %s, Python %s  [%d in all]"
                  % (cls, t0, n0, pc0, op0, "%02X at %04X" % (prev[1], prev[0]) if prev else "none", vc, vp, cnt))
            print("  %-18s Intel: %s" % ("", INTEL[cls]))
        else:
            print("  %-18s none" % cls)
    print("\nThe first %d differences in order where each starts or changes, once per class and making instruction "
          "(the one before):" % len(order))
    for t0, n0, pc0, op0, prev, cls, vc, vp in order:
        print("  tick %-7d #%-7d %04X op %02X (after %s)  %-18s C %s  Python %s" % (
            t0, n0, pc0, op0, "%02X" % prev[1] if prev else "--", cls, vc, vp))
    if a.control:
        seen = "acceptance T" in first
        print("\nCONTROL (acceptance 11 T): %s" % ("seen, as it must be" if seen else "NOT SEEN: the trace is blind"))
        sys.exit(0 if seen else 1)


if __name__ == "__main__":
    main()
