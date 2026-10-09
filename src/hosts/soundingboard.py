"""GW Micro Sounding Board (desktop ISA card) driving the engine's SSI-263.

The Sounding Board carried an SSI-263 and little else: its text-to-speech ran on the PC, in two
DOS TSRs by Douglas Geoffray (version 2.7, 1988-96), both packed .COM files that unpack
themselves as they start (a PKLITE-style stub):

  * SBLOAD.COM, "Text-To-Speech in DOS", turns text into five-byte frames (R0..R4) in a ring
    buffer.  Between them the two programs carry the Speak-Out's rule and allophone tables
    byte for byte (addresses as loaded, the file starting at 100h): SB.COM 1000-16EB is
    Speak-Out 5318-5A03, SBLOAD 1A9D-3C5F is 5A03-7BC5 and SBLOAD 3C60-3F52 is 7BD0-7EC2.
    Fed the same text, both hosts send the chip the same frames, register for register;
  * SB.COM, the card driver: it finds SBLOAD through INT 2Fh, takes text the way a printer
    would (INT 17h on LPT3 by default, or INT 14h on a COM port), and its IRQ handler sends
    one frame per A/R: R0 = 00, then R1, R2, R3, R4, then the phoneme byte into R0 -- the
    Speak-Out's handler exactly.  Its start-up writes R2 = F8, R4 = E9, R3 = 82, R0 = EB,
    R3 = 52, also the Speak-Out's.

This host loads both programs as DOS would (SBLOAD, then SB, each running until it goes
resident), with the card at its factory settings: the chip's R0..R4 at I/O 300h..304h, A/R
on IRQ2 (vector 0Ah), text on LPT3.  The driver reads 300h before each frame and 301h when it
has nothing left to send (presumably acknowledging the card's interrupt); it never uses the values.

The drivers are NOT part of this repository: pass the paths of your own copies.
"""
import os
import struct
import sys

try:
    from .x86_api import (UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN, UC_HOOK_INTR, UC_X86_INS_IN,
                         UC_X86_INS_OUT, UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
                         UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_CS, UC_X86_REG_IP,
                         UC_X86_REG_SS, UC_X86_REG_SP, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_EFLAGS)
    from .ssi263 import SSI263
except ImportError:                       # the research tree: src/ on sys.path
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from hosts.x86_api import (UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN, UC_HOOK_INTR,  # noqa: E402
                              UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_AX, UC_X86_REG_BX,
                              UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI,
                              UC_X86_REG_BP, UC_X86_REG_CS, UC_X86_REG_IP, UC_X86_REG_SS,
                              UC_X86_REG_SP, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_EFLAGS)
    from ssi263 import SSI263  # noqa: E402

IDLE_SEG = 0x0050            # 0050:0000 STI; JMP $ -- "DOS" between interrupts
CLIENT_SEG = 0x0058          # 0058:0000 a tiny application printing DS:SI, CX chars via INT 17h
ENV_SEG = 0x0068             # an environment block: no variables, the program's path
TEXT_SEG = 0x0070            # the client's text buffer (16 KB)
FIRST_PSP = 0x0800           # SBLOAD's PSP; SB's follows SBLOAD's resident part
STACK_SEG = 0x9000           # the host's own stack, for far calls and interrupts from "DOS"
BIOS_SEG = 0xF000
IRET_OFF = 0xFF53            # F000:FF53, where every unclaimed vector points
RETURN_OFF = 0xFF00          # F000:FF00: far calls into the driver return here (HLT)
IRQ_EOI_OFF = 0xFF60         # F000:FF60, the BIOS's default for hardware IRQs: EOI, IRET
TIMER_OFF = 0xFF70           # F000:FF70, the BIOS's INT 08h: count the tick at 0040:006C, INT 1Ch, EOI
DOS_OFF = 0xFF90             # F000:FF90, DOS's INT 21h for a driver that chains to it: STI; INT E1h; RETF 2
INDOS_OFF = 0xFE00           # F000:FE00, the InDOS flag (always 0) and the critical-error flag before it
CARD_BASE = 0x300            # factory setting of the SPEECH BASE ADDRESS switches (/!B48)
CARD_IRQ = 2                 # factory jumper (/!I2): vector 0Ah
LPT = 2                      # LPT3, the driver's default port (/!L3), as INT 17h numbers it
TIMER_HZ = 1193182.0 / 65536
REGS = {"ax": UC_X86_REG_AX, "bx": UC_X86_REG_BX, "cx": UC_X86_REG_CX, "dx": UC_X86_REG_DX,
        "si": UC_X86_REG_SI, "di": UC_X86_REG_DI, "bp": UC_X86_REG_BP, "cs": UC_X86_REG_CS,
        "ip": UC_X86_REG_IP, "ds": UC_X86_REG_DS, "es": UC_X86_REG_ES, "ss": UC_X86_REG_SS,
        "sp": UC_X86_REG_SP}


class SoundingBoard:
    def __init__(self, sbload_path, sb_path, chip=None, out_rate=44100, cpu_ips=5_000_000, log=None,
                 core=None, sb_args=""):
        self.chip = chip or SSI263(out_rate=out_rate)
        self.cpu_ips = cpu_ips           # instructions per chip second: a scheduling policy, not a clock
        self.log = log if log is not None else []
        # MAME's 8086 (pc86.py), as the Accent-mini's host; Unicorn remains a development reference
        self.core = core or os.environ.get("SSI263_SB_CORE", "mame")
        if self.core == "mame":
            try:
                from .pc86 import Pc86
            except ImportError:
                from hosts.pc86 import Pc86
            self.uc = uc = Pc86()
        elif self.core == "unicorn":
            try:
                from .ucmini import Uc
            except ImportError:
                from hosts.ucmini import Uc
            self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_16)
        else:
            raise ValueError("SSI263_SB_CORE: unicorn or mame, not %r" % self.core)
        uc.mem_map(0, 0x100000)
        # BIOS: every vector an IRET, hardware IRQs EOI, 640 KB, no printers, the far-call trap
        uc.mem_write(BIOS_SEG * 16 + IRET_OFF, b"\xcf")
        uc.mem_write(BIOS_SEG * 16 + RETURN_OFF, b"\xf4")
        uc.mem_write(BIOS_SEG * 16 + IRQ_EOI_OFF, bytes([0x50, 0xB0, 0x20, 0xE6, 0x20, 0x58, 0xCF]))
        for n in range(256):
            uc.mem_write(n * 4, struct.pack("<HH", IRQ_EOI_OFF if 0x08 <= n <= 0x0F else IRET_OFF, BIOS_SEG))
        # push ax; push ds; xor ax,ax; mov ds,ax; inc word [46Ch]; jnz $+6; inc word [46Eh]; int 1Ch;
        # mov al,20h; out 20h,al; pop ds; pop ax; iret
        uc.mem_write(BIOS_SEG * 16 + TIMER_OFF, bytes([0x50, 0x1E, 0x31, 0xC0, 0x8E, 0xD8, 0xFF, 0x06, 0x6C, 0x04,
                                                       0x75, 0x04, 0xFF, 0x06, 0x6E, 0x04, 0xCD, 0x1C, 0xB0, 0x20,
                                                       0xE6, 0x20, 0x1F, 0x58, 0xCF]))
        uc.mem_write(0x08 * 4, struct.pack("<HH", TIMER_OFF, BIOS_SEG))
        # INT 21h: SB.COM hooks it and jumps on to the old vector, so DOS has to be code to jump to
        uc.mem_write(BIOS_SEG * 16 + DOS_OFF, bytes([0xFB, 0xCD, 0xE1, 0xCA, 0x02, 0x00]))
        uc.mem_write(0x21 * 4, struct.pack("<HH", DOS_OFF, BIOS_SEG))
        uc.mem_write(0xFFFFE, b"\xfc")                       # model byte: AT
        uc.mem_write(0x413, struct.pack("<H", 640))
        uc.mem_write(IDLE_SEG * 16, b"\xfb\xeb\xfe")
        # mov al,[si]; mov ah,0; mov dx,LPT; push cx; push si; push ds; int 17h;
        # pop ds; pop si; pop cx; inc si; loop; retf
        uc.mem_write(CLIENT_SEG * 16, bytes([0x8A, 0x04, 0xB4, 0x00, 0xBA, LPT, 0x00, 0x51, 0x56,
                                             0x1E, 0xCD, 0x17, 0x1F, 0x5E, 0x59, 0x46, 0xE2, 0xEE, 0xCB]))
        # the card and the PIC
        self.ports = {}
        self.pic_mask = 0xB8               # as the BIOS leaves it: the timer, keyboard, IRQ2 and floppy on
        self.in_service = set()
        self.irq_latched = False
        self.last_request = False
        self.timer_due = 0.0
        self.writes = []                 # (chip time, reg, value)
        self.keep_writes = True
        self.last_speech = -1.0
        self.preparing = False
        self.say_time = 0.0
        self.pending = []
        self.insns = 0
        self.stopped = None
        self.resident = None             # (paragraphs) once a program has gone resident
        self.dta = 0
        uc.hook_add(UC_HOOK_INTR, self._int)
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        # load SBLOAD, then SB right after what SBLOAD keeps resident
        psp = FIRST_PSP
        for path, args in ((sbload_path, ""), (sb_path, sb_args)):
            paras = self._exec(path, psp, args)
            self.log.append("%s resident: %04X paragraphs at %04X" % (os.path.basename(path), paras, psp))
            self.sb_seg = psp                    # the last one loaded: SB.COM, whose data SBLOAD's code also works in
            psp += paras
        self.w("cs", IDLE_SEG)
        self.w("ip", 0)
        self.w("ss", STACK_SEG)
        self.w("sp", 0xFFFE)

    # ---- helpers -------------------------------------------------------------------
    def r(self, name):
        return self.uc.reg_read(REGS[name]) & 0xFFFF

    def w(self, name, value):
        self.uc.reg_write(REGS[name], value & 0xFFFF)

    def regs(self):
        return " ".join("%s=%04X" % (k, self.r(k)) for k in REGS)

    def _push(self, v):
        sp = (self.r("sp") - 2) & 0xFFFF
        self.w("sp", sp)
        self.uc.mem_write(self.r("ss") * 16 + sp, struct.pack("<H", v & 0xFFFF))

    def _carry(self, on):
        fl = self.uc.reg_read(UC_X86_REG_EFLAGS)
        self.uc.reg_write(UC_X86_REG_EFLAGS, (fl | 1) if on else (fl & ~1))

    def _stop(self, why):
        self.log.append("STOP " + why + " | " + self.regs())
        self.stopped = why
        self.uc.emu_stop()

    def _vector(self, n):
        off, seg = struct.unpack("<HH", self.uc.mem_read(n * 4, 4))
        return seg, off

    def _dispatch(self, n):
        """What INT n does on a real CPU: push flags, CS, IP; clear IF, TF; jump."""
        seg, off = self._vector(n)
        fl = self.uc.reg_read(UC_X86_REG_EFLAGS)
        self._push(fl)
        self._push(self.r("cs"))
        self._push(self.r("ip"))
        self.uc.reg_write(UC_X86_REG_EFLAGS, fl & ~0x300)
        self.w("cs", seg)
        self.w("ip", off)

    def _string(self, seg, off, end=b"\0", limit=200):
        return bytes(self.uc.mem_read(seg * 16 + off, limit)).split(end, 1)[0]

    # ---- loading a .COM program as COMMAND.COM would ------------------------------------
    def _exec(self, path, psp, args):
        data = open(path, "rb").read()
        uc = self.uc
        tail = (" " + args if args else "").encode("latin-1")[:126]
        p = bytearray(256)
        p[0:2] = b"\xcd\x20"
        struct.pack_into("<H", p, 2, 0xA000)                 # first paragraph beyond the program
        struct.pack_into("<H", p, 0x2C, ENV_SEG)
        p[0x80] = len(tail)
        p[0x81:0x82 + len(tail)] = tail + b"\r"
        uc.mem_write(psp * 16, bytes(p))
        uc.mem_write(psp * 16 + 0x100, data)
        name = os.path.basename(path).upper().encode("latin-1")
        uc.mem_write(ENV_SEG * 16, b"COMSPEC=C:\\COMMAND.COM\0\0\x01\0C:\\SB\\" + name + b"\0")
        for seg in ("cs", "ds", "es", "ss"):
            self.w(seg, psp)
        self.w("sp", 0xFFFE)
        uc.mem_write(psp * 16 + 0xFFFE, b"\0\0")              # RET to PSP:0000, INT 20h
        self.w("ip", 0x100)
        self.dta = psp * 16 + 0x80
        self.current_psp = psp
        self.resident = None
        self.stopped = None
        # the driver talks to the card while it starts (it checks the chip answers): run the
        # chip and the IRQ beside it, as everywhere else
        uc.emu_start(psp * 16 + 0x100, 0, count=1_000_000)
        waited = 0.0
        while self.resident is None and not self.stopped and waited < 10.0:
            self._try_irq()
            before = self.chip.time
            step = 0.0005
            self.pending.append(self.chip.run_until_request(step) if not self.chip.request else self.chip.run(step / 4))
            dt = max(self.chip.time - before, 1e-5)
            uc.emu_start(self.r("cs") * 16 + self.r("ip"), 0, count=max(100, int(self.cpu_ips * dt)))
            waited += dt
        if self.resident is None:
            raise RuntimeError("%s did not go resident: %s | %s"
                               % (path, self.stopped or "ran out of time", self.regs()))
        return self.resident

    # ---- interrupts: the drivers' own handlers run; BIOS and DOS are emulated -----------
    def _int(self, uc, n, _):
        seg, off = self._vector(n)
        if seg != BIOS_SEG:
            self._dispatch(n)                   # a vector a driver installed
            return
        ah, al = self.r("ax") >> 8, self.r("ax") & 0xFF
        if n in (0x21, 0xE1):
            self._dos(ah, al)
        elif n == 0x27:                          # terminate and stay resident, DX bytes
            self._go_resident((self.r("dx") + 15) // 16)
        elif n == 0x20:
            self._stop("program ended without staying resident")
        elif n == 0x2F:
            pass                                 # multiplex: nobody else installed (AL stays 0)
        elif n == 0x16:                          # keyboard: nothing typed
            if ah in (0x01, 0x11):
                fl = self.uc.reg_read(UC_X86_REG_EFLAGS)
                self.uc.reg_write(UC_X86_REG_EFLAGS, fl | 0x40)
            elif ah in (0x02, 0x12):
                self.w("ax", self.r("ax") & 0xFF00)
            else:
                self._stop("unimplemented INT 16 AH=%02X" % ah)
        elif n in (0x10, 0x14, 0x15, 0x17, 0x1A, 0x1C, 0x28, 0x2A):
            pass                                 # video, serial, AT services, printer, clock, tick, DOS idle:
                                                 # nothing answers (SB.COM asks INT 17h AX=FFFF if it is loaded)
        else:
            self._stop("unimplemented INT %02X AX=%04X" % (n, self.r("ax")))

    def _go_resident(self, paras):
        self.resident = paras
        self.uc.emu_stop()

    def _dos(self, ah, al):
        uc = self.uc
        self._carry(False)
        if ah == 0x09:
            s = self._string(self.r("ds"), self.r("dx"), b"$", 400)
            self.log.append("DOS print: " + s.decode("cp437", "replace").strip())
        elif ah == 0x02:
            pass
        elif ah == 0x30:
            self.w("ax", 0x0005)                # DOS 5.0
            self.w("bx", 0)
            self.w("cx", 0)
        elif ah == 0x35:
            seg, off = self._vector(al)
            self.w("es", seg)
            self.w("bx", off)
        elif ah == 0x25:
            uc.mem_write(al * 4, struct.pack("<HH", self.r("dx"), self.r("ds")))
            self.log.append("DOS set vector %02X -> %04X:%04X" % (al, self.r("ds"), self.r("dx")))
        elif ah == 0x31:                         # keep DX paragraphs resident
            self._go_resident(self.r("dx"))
        elif ah == 0x34:                         # InDOS flag address
            self.w("es", BIOS_SEG)
            self.w("bx", INDOS_OFF)
        elif ah in (0x51, 0x62):
            self.w("bx", self.current_psp)
        elif ah == 0x50:
            self.current_psp = self.r("bx")
        elif ah == 0x1A:
            self.dta = self.r("ds") * 16 + self.r("dx")
        elif ah == 0x2F:
            self.w("es", self.dta >> 4)
            self.w("bx", self.dta & 15)
        elif ah == 0x49 or ah == 0x4A:          # free / resize memory: always fine
            pass
        elif ah == 0x3D:                         # open: no SPEECH.DIC here
            self.log.append("DOS open %r: not found" % self._string(self.r("ds"), self.r("dx")))
            self.w("ax", 2)
            self._carry(True)
        elif ah == 0x2A:                         # date: 1996-01-22, a Monday
            self.w("cx", 1996)
            self.w("dx", 0x0116)
            self.w("ax", (self.r("ax") & 0xFF00) | 1)
        elif ah == 0x2C:
            self.w("cx", 0x0C00)
            self.w("dx", 0)
        elif ah == 0x5D and al == 0x06:          # swappable data area
            self.w("ds", BIOS_SEG)
            self.w("si", INDOS_OFF - 1)
            self.w("cx", 0x80)
            self.w("dx", 0x40)
        elif ah == 0x4C:
            self._stop("program exited (code %d) without staying resident" % al)
        else:
            self._stop("unimplemented DOS AH=%02X AL=%02X" % (ah, al))

    # ---- the card and the PIC -------------------------------------------------------------
    def _in(self, uc, port, size, _):
        key = ("in", port)
        self.ports[key] = self.ports.get(key, 0) + 1
        if port == 0x21:
            return self.pic_mask
        if port == 0x61:
            return 0x00
        if port == 0x60:
            return 0x00
        if CARD_BASE <= port < CARD_BASE + 16:
            # the SSI-263 drives D7 with A/R* (low = requesting) when it is read
            return 0x7F if self.chip.request else 0xFF
        self._stop("unimplemented IN %04X" % port)
        return 0xFF

    def _out(self, uc, port, size, value, _):
        key = ("out", port)
        self.ports[key] = self.ports.get(key, 0) + 1
        value &= 0xFF
        if port == 0x21:
            self.pic_mask = value
        elif port == 0x20:
            if value == 0x20 and self.in_service:
                self.in_service.discard(min(self.in_service))
        elif port in (0x61, 0x42, 0x43, 0xE2, 0xE3):
            pass                                 # speaker and timer 2 (the bell), Toshiba config
        elif CARD_BASE <= port < CARD_BASE + 5:
            reg = port - CARD_BASE
            self.chip.write(reg, value)
            if self.keep_writes:
                self.writes.append((round(self.chip.time, 6), reg, value))
            if reg == 0 and (value & 0x3F) and not (self.chip.regs[3] & 0x80):
                self.last_speech = self.chip.time
                self.preparing = False
        else:
            self._stop("unimplemented OUT %04X <- %02X" % (port, value))

    def _try_irq(self):
        """IRQ2 from the chip's A/R: the PIC latches the rising edge of the request."""
        line = self.chip.request
        if line and not self.last_request:
            self.irq_latched = True
        self.last_request = line
        if not line:
            self.irq_latched = False
        fl = self.uc.reg_read(UC_X86_REG_EFLAGS)
        if not fl & 0x200:
            return False
        if self.chip.time >= self.timer_due:
            self.timer_due = self.chip.time + 1.0 / TIMER_HZ
            if not (self.pic_mask & 1) and not self.in_service:
                self._dispatch(0x08)
                self.in_service.add(0)
                return True
        if self.irq_latched and not (self.pic_mask & (1 << CARD_IRQ)) and \
                not any(i <= CARD_IRQ for i in self.in_service):
            self._dispatch(0x08 + CARD_IRQ)
            self.in_service.add(CARD_IRQ)
            self.irq_latched = False
            return True
        return False

    # ---- driving the drivers ----------------------------------------------------------------
    def _cpu(self, count):
        self.uc.emu_start(self.r("cs") * 16 + self.r("ip"), 0, count=count)
        self.insns += count
        if self.stopped:
            raise RuntimeError(self.stopped)

    def _far_call(self, seg, off, budget=2_000_000, wait_limit=30.0, step=0.0005):
        """Far-call seg:off from the host; run until it returns, with the card running if it waits."""
        self.stopped = None
        self.w("ss", STACK_SEG)
        self.w("sp", 0xFFFE)
        self._push(BIOS_SEG)
        self._push(RETURN_OFF)
        self.w("cs", seg)
        self.w("ip", off)
        ret = BIOS_SEG * 16 + RETURN_OFF
        self.uc.emu_start(seg * 16 + off, ret, count=budget)
        waited = 0.0
        while not self.stopped and self.r("cs") * 16 + self.r("ip") != ret:
            if waited > wait_limit:
                self._stop("call %04X:%04X still waiting after %.0f s" % (seg, off, waited))
                break
            self._try_irq()
            before = self.chip.time
            y = self.chip.run_until_request(step) if not self.chip.request else self.chip.run(step / 4)
            self.pending.append(y)
            dt = max(self.chip.time - before, 1e-5)
            self.uc.emu_start(self.r("cs") * 16 + self.r("ip"), ret, count=max(100, int(self.cpu_ips * dt)))
            waited += dt
        if self.stopped:
            raise RuntimeError(self.stopped)
        self.w("cs", IDLE_SEG)
        self.w("ip", 0)

    def _settle(self, budget=400_000):
        """Let an interrupt handler a CPU slice stopped inside finish before calling the driver."""
        n = 0
        while self.r("cs") != IDLE_SEG and n < budget:
            self._cpu(1000)
            n += 1000

    def say(self, text, speech=None):
        """Print text to LPT3 (INT 17h, AH=0 per character), as a screen reader did."""
        data = text.encode("latin-1", "replace")
        if not data:
            return
        if speech is None:
            speech = any(ch.isalnum() for ch in text)
        self._settle()
        if speech:
            self.preparing = True
            self.say_time = self.chip.time
        for k in range(0, len(data), 0x4000):
            chunk = data[k:k + 0x4000]
            self.uc.mem_write(TEXT_SEG * 16, chunk)
            self.w("ds", TEXT_SEG)
            self.w("si", 0)
            self.w("cx", len(chunk))
            self._far_call(CLIENT_SEG, 0)

    def run(self, seconds, step=0.0005):
        out, t = self.pending, 0.0
        self.pending = []
        while t < seconds:
            self._try_irq()
            before = self.chip.time
            y = self.chip.run_until_request(step) if not self.chip.request else self.chip.run(step / 4)
            out.append(y)
            dt = max(self.chip.time - before, 1e-5)
            self._cpu(max(100, int(self.cpu_ips * dt)))
            t += dt
        return self.chip.dsp.concat(out)

    def skip(self, seconds, step=0.002):
        """Run the drivers with the chip keeping time but making no sound."""
        self.pending = []
        t = 0.0
        while t < seconds - 1e-9:
            self._try_irq()
            before = self.chip.time
            self.chip.skip(min(step, seconds - t))
            dt = max(self.chip.time - before, 1e-5)
            self._cpu(max(100, int(self.cpu_ips * dt)))
            t += dt
        return t

    def _word(self, off):
        return struct.unpack("<H", self.uc.mem_read(self.sb_seg * 16 + off, 2))[0]

    def input_pending(self):
        """Frames the rules have made and the IRQ handler has not yet sent: SB.COM's ring of
        five-byte frames, [14Ch] where the rules write and [14Eh] where the handler reads."""
        return self._word(0x14C) != self._word(0x14E)

    def boot(self, limit=6.0):
        """Let the card's own greeting ("Sounding board version 2.7") play out silently."""
        t = self.skip(0.2)
        while t < limit and self.busy():
            t += self.skip(0.05)

    def cancel(self, limit=0.6):
        """Ctrl-X, the driver's momentary silence: it stops speech and clears its output buffer."""
        self.preparing = False
        self.say("\x18", speech=False)
        t = self.skip(0.01)
        while t < limit and self.busy():
            t += self.skip(0.01)
        return t

    def busy(self, quiet=0.05, patience=1.5):
        if self.input_pending() or (self.chip.time - self.last_speech) < quiet:
            return True
        return self.preparing and (self.chip.time - self.say_time) < patience


if __name__ == "__main__":
    import wave
    here = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    folder = os.path.join(here, "firmware", "gw-micro-sounding-board")
    sbload = sys.argv[1] if len(sys.argv) > 1 else os.path.join(folder, "SBLOAD.COM")
    sb = sys.argv[2] if len(sys.argv) > 2 else os.path.join(folder, "SB.COM")
    text = sys.argv[3] if len(sys.argv) > 3 else "Hello, this is the Sounding Board."
    out_path = sys.argv[4] if len(sys.argv) > 4 else "soundingboard_test.wav"
    b = SoundingBoard(sbload, sb)
    b.boot()
    b.say(text + "\r")
    out = [b.run(0.05)]
    while b.busy():
        out.append(b.run(0.05))
    y = b.chip.dsp.concat(out)
    print("\n".join(b.log[-20:]))
    print("ports:", {("%s %X" % k): v for k, v in sorted(b.ports.items())})
    names = b.chip.rom.names
    print("phonemes:", " ".join(names.get(v & 0x3F, "?") for t, r, v in b.writes if r == 0 and v & 0x3F))
    print("%.2f s of audio, %d chip writes" % (len(y) / 44100.0, len(b.writes)))
    with wave.open(out_path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(44100)
        f.writeframes(b.chip.dsp.pcm16(y, 1.0))
