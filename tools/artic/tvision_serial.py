"""Business Vision for the TransPort (TVISION.COM 3.08) on the SynPhonix research host's DOS stand-in, with a UART on
COM1, to record what it sends a TransPort.  Research only: no test rests on it, and it is not a TransPort.

    python tools/artic/tvision_serial.py [seconds]

The replies are written by hand (REPLIES): an identity starting "TP" for ^E~?, ACK "P" and two hex digits for ^E%,
64 characters for the serial challenge ^E~*, a carriage return after ^EQ, and XON after any other command.  TVISION
compares the serial it is sent with its own and refuses to load on a mismatch; the stand-in skips that comparison
in memory (the JZ at A7CC made a JMP: SKIP_SERIAL), so it is never answered right and never needs a serial.  What
it sends is in docs/front-ends.md, "The TransPort, as TVISION drives it".

TVISION.COM from firmware/artic-synphonix/ (or SYNPHONIX_TVISION); libpc86 from SSI263_PC86_DLL, else build/linux/.
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "src"))
os.environ.setdefault("SSI263_PC86_DLL", os.path.join(ROOT, "build", "linux", "libpc86.so"))
from hosts import synphonix as S                  # noqa: E402
from hosts.synphonix import *                     # noqa: E402,F401,F403

TV = os.environ.get("SYNPHONIX_TVISION", os.path.join(ROOT, "firmware", "artic-synphonix", "TVISION.COM"))
COM1 = 0x3F8
SKIP_SERIAL = 0xA7CC                              # TVISION's JZ past "does NOT MATCH Speech Device"
REPLIES = [(b"\x05~? ", b"TP615 V1.00\r"), (b"\x05%\r", b"\x06P00\r"), (b"\x05~*d`@ ", b"0" * 64),
           (b"\x05Q ", b"\r")]


class TVPC(SynPhonix):
    def __init__(self, path, args=" /M3 /C1", reply=b""):
        self.chip = S.SSI263(out_rate=11025); self.cpu_ips = 5_000_000; self.log = []
        self.key = b"\xff" * 6
        self.sent = bytearray(); self.rx = bytearray(); self.replies = reply; self.sent_len = -1; self.uart = {}; self.other = {}
        self.ier = 0; self.lcr = 0
        from hosts.pc86 import Pc86
        self.uc = uc = Pc86(); uc.mem_map(0, 0x100000)
        uc.mem_write(BIOS_SEG * 16 + IRET_OFF, b"\xcf"); uc.mem_write(BIOS_SEG * 16 + RETURN_OFF, b"\xf4")
        uc.mem_write(BIOS_SEG * 16 + IRQ_EOI_OFF, bytes([0x50, 0xB0, 0x20, 0xE6, 0x20, 0x58, 0xCF]))
        for n in range(256):
            uc.mem_write(n * 4, struct.pack("<HH", IRQ_EOI_OFF if 0x08 <= n <= 0x0F else IRET_OFF, BIOS_SEG))
        uc.mem_write(BIOS_SEG * 16 + TIMER_OFF, bytes([0x50, 0x1E, 0x31, 0xC0, 0x8E, 0xD8, 0xFF, 0x06, 0x6C, 0x04,
                     0x75, 0x04, 0xFF, 0x06, 0x6E, 0x04, 0xCD, 0x1C, 0xB0, 0x20, 0xE6, 0x20, 0x1F, 0x58, 0xCF]))
        uc.mem_write(0x08 * 4, struct.pack("<HH", TIMER_OFF, BIOS_SEG))
        uc.mem_write(0xFFFFE, b"\xfc"); uc.mem_write(0x413, struct.pack("<H", 640)); uc.mem_write(0x449, b"\x03")
        uc.mem_write(0x400, struct.pack("<HHHH", 0x3F8, 0x2F8, 0, 0)); uc.mem_write(0x410, struct.pack("<H", 0x0242))
        uc.mem_write(0x44A, b"\x50\x00"); uc.mem_write(0x484, b"\x18")
        uc.mem_write(IDLE_SEG * 16, b"\xfb\xeb\xfe")
        self.ports = {}; self.pic_mask = 0xB8; self.in_service = set(); self.irq_latched = False; self.last_req = False
        self.timer_due = 0.0; self.writes = []; self.last_speech = -1; self.screen = []; self.insns = 0
        self.stopped = None; self.resident = None
        uc.hook_add(UC_HOOK_INTR, self._int); uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        image = open(path, "rb").read()
        p = bytearray(256); p[0:2] = b"\xcd\x20"; struct.pack_into("<H", p, 2, 0xA000); struct.pack_into("<H", p, 0x2C, ENV_SEG)
        tail = args.encode(); p[0x80] = len(tail); p[0x81:0x82 + len(tail)] = tail + b"\r"
        uc.mem_write(PSP_SEG * 16, bytes(p)); uc.mem_write(PSP_SEG * 16 + 0x100, image)
        drive = b"C" + b":"
        uc.mem_write(ENV_SEG * 16, b"COMSPEC=" + drive + b"\\COMMAND.COM\0\0\x01\0" + drive + b"\\ARTIC\\VISION\\TVISION.COM\0")
        for n in ("cs", "ds", "es", "ss"): self.w(n, PSP_SEG)
        self.w("sp", 0xFFFE); uc.mem_write(PSP_SEG * 16 + 0xFFFE, b"\0\0"); self.w("ip", 0x100)
        self.uc.reg_write(UC_X86_REG_EFLAGS, 0x0202)
    def _try_irq(self):
        fl = self.uc.reg_read(UC_X86_REG_EFLAGS)
        if fl & 0x200 and self.rx and (self.uart.get(1, 0) & 1) and not (self.pic_mask & 0x10) and 4 not in self.in_service \
                and not any(i < 4 for i in self.in_service) and self.chip.time >= getattr(self, "rx_due", 0):
            self.rx_due = self.chip.time + 0.00104       # 9600 baud
            self._dispatch(0x0C); self.in_service.add(4); return True
        return SynPhonix._try_irq(self)
    def answer(self):
        for q, a in self.replies:
            if self.sent.endswith(q) and not self.rx and self.sent_len != len(self.sent):
                self.rx += a; self.sent_len = len(self.sent)
    def go(self, seconds=8.0):
        waited = 0.0
        while self.resident is None and not self.stopped and waited < seconds:
            self.answer(); self._try_irq(); before = self.chip.time; self.chip.skip(0.0005)
            dt = max(self.chip.time - before, 1e-5)
            self.uc.emu_start(self.r("cs") * 16 + self.r("ip"), 0, count=max(100, int(self.cpu_ips * dt)))
            waited += dt
        return waited
    def _in(self, uc, port, size, _):
        self.ports[("in", port)] = self.ports.get(("in", port), 0) + 1
        if port == 0x21: return self.pic_mask
        if COM1 <= port <= COM1 + 7:
            o = port - COM1
            if o == 0 and not (self.lcr & 0x80): return self.rx.pop(0) if self.rx else 0
            if o == 5: return 0x60 | (1 if self.rx else 0)
            if o == 6: return 0xB0
            if o == 2: return 0x04 if self.rx else 0x01
            if o == 3: return self.lcr
            return self.uart.get(o, 0)
        if port in (0x60, 0x61, 0x64): return 0
        self.other[port] = self.other.get(port, 0) + 1
        return 0xFF
    def _out(self, uc, port, size, value, _):
        self.ports[("out", port)] = self.ports.get(("out", port), 0) + 1
        if port == 0x21: self.pic_mask = value
        elif port == 0x20:
            if self.in_service: self.in_service.discard(min(self.in_service))
        elif COM1 <= port <= COM1 + 7:
            o = port - COM1
            if o == 0 and not (self.lcr & 0x80): self.sent.append(value)
            elif o == 3: self.lcr = value
            else: self.uart[o + (8 if (self.lcr & 0x80 and o < 2) else 0)] = value
        else: self.other[("o", port)] = value
    def _int(self, uc, n, _):
        seg, off = self._vector(n)
        if seg != BIOS_SEG: self._dispatch(n); return
        ah, al = self.r("ax") >> 8, self.r("ax") & 0xFF
        if n == 0x21: self._dos(ah, al)
        elif n == 0x27: self.resident = self.r("dx"); uc.emu_stop()
        elif n == 0x20: self._stop("ended: " + "".join(self.screen)[-200:])
        elif n == 0x10:
            if ah == 0x0E: self.screen.append(chr(al) if al < 0x80 else " ")
            elif ah == 0x0F: self.w("ax", 0x5003); self.w("bx", self.r("bx") & 0xFF)
            elif ah == 0x03: self.w("dx", 0); self.w("cx", 0x0607)
            elif ah == 0x08: self.w("ax", 0x0720)
        elif n == 0x14:
            if ah == 0x00: self.w("ax", 0x6030)
            elif ah == 0x01: self.sent.append(al); self.w("ax", 0x6000 | al)
            elif ah == 0x02: self.w("ax", (0x61 << 8) | (self.rx.pop(0) if self.rx else 0) if self.rx else 0x8000)
            elif ah == 0x03: self.w("ax", 0x60B0 | (0x100 if self.rx else 0))
        elif n == 0x15:
            if ah == 0xC4: self.w("ax", 0x8600 | al); self._carry(True)
        elif n == 0x16:
            if ah in (0x01, 0x11):
                fl = self.uc.reg_read(UC_X86_REG_EFLAGS); self.uc.reg_write(UC_X86_REG_EFLAGS, fl | 0x40)
            elif ah in (0x02, 0x12): self.w("ax", self.r("ax") & 0xFF00)
        elif n in (0x1A, 0x1C, 0x28, 0x2F, 0x17, 0x33, 0x13, 0x11, 0x12, 0x67): pass
        else: self.log.append("INT %02X AX=%04X" % (n, self.r("ax")))
    def _dos(self, ah, al):
        if ah in (0x3D, 0x3C, 0x4E, 0x43): self._carry(True); self.w("ax", 2); self.log.append("DOS %02X %r" % (ah, self._string(self.r("ds"), self.r("dx"))))
        elif ah == 0x19: self.w("ax", (self.r("ax") & 0xFF00) | 2)
        elif ah == 0x47: self.uc.mem_write(self.r("ds") * 16 + self.r("si"), b"ARTIC\\VISION\0")
        elif ah in (0x0E, 0x3E, 0x3B, 0x1A, 0x2C, 0x2A): pass
        else:
            try: super()._dos(ah, al)
            except Exception as e: self.log.append(str(e))
            if self.stopped and "unimplemented DOS" in self.stopped:
                self.log.append(self.stopped); self.stopped = None; self._carry(True)

def show(b):
    return "".join(("^%s" % chr(c + 64)) if c < 32 else chr(c) if c < 127 else "\\x%02x" % c for c in b)


def start(seconds=20.0):
    pc = TVPC(TV, reply=REPLIES)
    assert pc.uc.mem_read(PSP_SEG * 16 + SKIP_SERIAL, 1)[0] == 0x74, "not TVISION 3.08"
    pc.uc.mem_write(PSP_SEG * 16 + SKIP_SERIAL, b"\xeb")

    def answer():
        if pc.sent and pc.sent_len != len(pc.sent) and not pc.rx and pc.sent[-1:] in (b" ", b"\r"):
            for q, a in pc.replies:
                if pc.sent.endswith(q):
                    pc.rx += a
                    pc.sent_len = len(pc.sent)
                    return
            pc.rx += b"\x11"
            pc.sent_len = len(pc.sent)
    pc.answer = answer
    pc.t = pc.go(seconds)
    return pc


if __name__ == "__main__":
    pc = start(float(sys.argv[1]) if len(sys.argv) > 1 else 20.0)
    print("resident:", pc.resident, "stopped:", pc.stopped, "after %.1f s" % pc.t)
    print("sent (%d bytes): %s" % (len(pc.sent), show(pc.sent)))
