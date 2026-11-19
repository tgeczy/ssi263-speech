"""Automated Functions SmarTalk (1995) driving the engine's SSI-263.

The SmarTalk is a five-ounce box on the PC's parallel port with an SSI-263 inside and little
else: its text-to-speech runs on the PC, in Automated Functions' DOS TSR (version 1.21,
1995-11-15).  S_TALK.EXE is the whole of it in conventional memory, rules and exception
dictionary included (XS_TALK.EXE keeps the tables in XMS instead).  A screen reader prints
text to it on INT 17h, with the Accent's commands or its own (TECHMAN.TXT).

This host loads S_TALK.EXE as DOS would, with the command line the installer writes into
SMARTALK.BAT (L1 I7 T0 F3 S1: LPT1, IRQ7, the shortest strobe time, text also taken on LPT3,
a Ctrl-K before each carriage return), and runs it until it goes resident.  The box, as read
from the TSR's code and confirmed by it speaking (the addresses are the EXE image's):

  378h data     the byte for the chip; reads back what was written
  37Ah control  bit 0, /STROBE: the data latches into the selected register when it is
                released (the writer at 0C37 sends the select with bit 0 set, then again with
                it clear); bits 1-3 select it, from the table at 0C2D: R0..R4 = 03, 01, 07, 05,
                0D.  Read as the chip's RS0 = NOT bit 1, RS1 = bit 2, RS2 = bit 3 they are 0, 1,
                2, 3 and 7, and the SSI-263 takes any address with RS2 set as R4.  Bit 4 enables
                the port's interrupt (the writer sets it after each R0).
  379h status   bit 6, ACK = the chip's A/R, high while it requests; IRQ7 on its rising edge
                while control bit 4 is set.  Bit 3, ERROR = control bit 2 (INIT): every 6 timer
                ticks while idle (54 while speaking) the timer handler flips INIT (1453) and
                expects ERROR to follow -- an unplugged port, its ERROR floating high, fails
                every other check.  Bit 4,
                SELECT, high once the box is ready (the power-up at 037C waits for it).  Bits 7
                and 5 read 0 and 1 (BUSY and PAPER-OUT high), the levels the timer handler takes
                for a powered box; what drives them (the battery?) is a GUESS -- the TSR only
                ever compares them.
  power-up      data 00 and control 0B for 20 timer ticks, then data 3F for 20 more (the data
                lines likely power the box; not modelled), then R4 = E3, R2 = F8 and the mode
                set (R0 = C0, R3 = DA then 5A).

Speech waits for a "{": the rules ignore every character until one arrives (15F5).  The
batch file's last program, ST_RUN.EXE ("allows SmarTalk to speak"), sends it: with the tables
in conventional memory it prints "{ smartalk ready" and "Using conventional memory." to the
TSR on INT 17h and waits for them to be spoken.  ST_RUN is 286 code (POPA), which this 8086
does not run, so boot() sends those two lines itself.

src/csrc/smartalk/st_host.c is this host in C, line for line, for the voice (st_voice.h) and the NVDA add-on;
tools/check_st_host.py holds the two to each other, write for write.

The software is NOT part of this repository: pass the path of your own S_TALK.EXE.
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
ENV_SEG = 0x0068             # an environment block: COMSPEC, then the program's path
TEXT_SEG = 0x0070            # the client's text buffer (16 KB)
PSP_SEG = 0x0800             # S_TALK's PSP; its image loads 10h paragraphs above
STACK_SEG = 0x9000           # the host's own stack, for far calls and interrupts from "DOS"
BIOS_SEG = 0xF000
IRET_OFF = 0xFF53            # F000:FF53, where every unclaimed vector points
RETURN_OFF = 0xFF00          # F000:FF00: far calls into the TSR return here (HLT)
IRQ_EOI_OFF = 0xFF60         # F000:FF60, the BIOS's default for hardware IRQs: EOI, IRET
TIMER_OFF = 0xFF70           # F000:FF70, the BIOS's INT 08h: count the tick at 0040:006C, INT 1Ch, EOI
DOS_OFF = 0xFF90             # F000:FF90, DOS's INT 21h for a program that chains to it: STI; INT E1h; RETF 2
INDOS_OFF = 0xFE00           # F000:FE00, the InDOS flag (always 0)
LPT_BASE = 0x378             # LPT1, as the BIOS data area lists it (0040:0008)
LPT_IRQ = 7                  # the TSR's default (I7): vector 0Fh
ARGS = "L1 I7 T0 F3 S1"      # SMARTALK.BAT's line for S_TALK, as MAKEIT.BAT writes it
DATA_SEG_AT = 0x538          # CS:0538 holds the TSR's data segment
QUEUED, SPEAKING = 0x1E2, 0x1EC   # in it: bytes of phonemes waiting (4 each), and 1 while it speaks
MARK_COUNT = 0x2DA                # ... and the index marks passed since its last status call or Ctrl-X: a word
START_LINES = "{ smartalk ready\nUsing conventional memory.\n"   # what ST_RUN prints to the TSR
TIMER_HZ = 1193182.0 / 65536
REGS = {"ax": UC_X86_REG_AX, "bx": UC_X86_REG_BX, "cx": UC_X86_REG_CX, "dx": UC_X86_REG_DX,
        "si": UC_X86_REG_SI, "di": UC_X86_REG_DI, "bp": UC_X86_REG_BP, "cs": UC_X86_REG_CS,
        "ip": UC_X86_REG_IP, "ds": UC_X86_REG_DS, "es": UC_X86_REG_ES, "ss": UC_X86_REG_SS,
        "sp": UC_X86_REG_SP}


def register(control):
    """The SSI-263 register that control bits 1-3 select (RS0 = NOT bit 1, RS1 = bit 2, RS2 = bit 3)."""
    if control & 0x08:
        return 4
    return (2 if control & 0x04 else 0) | (0 if control & 0x02 else 1)


class SmarTalk:
    def __init__(self, exe_path, chip=None, out_rate=44100, cpu_ips=5_000_000, log=None, core=None, args=ARGS):
        self.chip = chip or SSI263(out_rate=out_rate)
        self.cpu_ips = cpu_ips           # instructions per chip second: a scheduling policy, not a clock
        self.log = log if log is not None else []
        # MAME's 8086 (pc86.py), as the Accent-mini's and the Sounding Board's hosts
        self.core = core or os.environ.get("SSI263_SMARTALK_CORE", "mame")
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
            raise ValueError("SSI263_SMARTALK_CORE: unicorn or mame, not %r" % self.core)
        uc.mem_map(0, 0x100000)
        # BIOS: every vector an IRET, hardware IRQs EOI, LPT1 at 378h, 640 KB, the far-call trap
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
        uc.mem_write(BIOS_SEG * 16 + DOS_OFF, bytes([0xFB, 0xCD, 0xE1, 0xCA, 0x02, 0x00]))
        uc.mem_write(0x21 * 4, struct.pack("<HH", DOS_OFF, BIOS_SEG))
        uc.mem_write(0xFFFFE, b"\xfc")                       # model byte: AT
        uc.mem_write(0x408, struct.pack("<HHH", LPT_BASE, 0, 0))
        uc.mem_write(0x410, struct.pack("<H", 0x4000))       # equipment word: one printer port
        uc.mem_write(0x413, struct.pack("<H", 640))
        uc.mem_write(IDLE_SEG * 16, b"\xfb\xeb\xfe")
        # mov al,[si]; mov ah,0; mov dx,0 (LPT1); push cx; push si; push ds; int 17h;
        # pop ds; pop si; pop cx; inc si; loop; retf
        uc.mem_write(CLIENT_SEG * 16, bytes([0x8A, 0x04, 0xB4, 0x00, 0xBA, 0x00, 0x00, 0x51, 0x56,
                                             0x1E, 0xCD, 0x17, 0x1F, 0x5E, 0x59, 0x46, 0xE2, 0xEE, 0xCB]))
        # the port, the box and the PIC
        self.ports = {}
        self.data = 0
        self.control = 0
        self.last_ack = False
        self.pic_mask = 0xB8               # as the BIOS leaves it: the timer, keyboard, IRQ2 and floppy on
        self.in_service = set()
        self.irq_latched = False
        self.timer_due = 0.0
        self.writes = []                 # (chip time, reg, value)
        self.keep_writes = True
        self.last_speech = -1.0
        self.preparing = False
        self.say_time = 0.0
        self.pending = []
        self.replies = []                # the index marks passed in the audio the last run() returned: the samples
        self._replies_read = False       # of that audio made when each was passed (st_host.h sth_replies)
        self._mark_seen = 0
        self.marks = 0                   # every mark ever passed
        self.insns = 0
        self.stopped = None
        self.resident = None
        uc.hook_add(UC_HOOK_INTR, self._int)
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        paras = self._exec(exe_path, PSP_SEG, args)
        self.log.append("%s resident: %04X paragraphs at %04X" % (os.path.basename(exe_path), paras, PSP_SEG))
        self.data_seg = struct.unpack("<H", uc.mem_read(self.load * 16 + DATA_SEG_AT, 2))[0]
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

    def _word(self, off):
        return struct.unpack("<H", self.uc.mem_read(self.data_seg * 16 + off, 2))[0]

    # ---- loading an .EXE as COMMAND.COM would --------------------------------------------
    def _exec(self, path, psp, args):
        data = open(path, "rb").read()
        h = struct.unpack_from("<14H", data)
        if h[0] != 0x5A4D:
            raise ValueError("expected an MZ executable")
        load = psp + 0x10
        image = bytearray(data[h[4] * 16:])
        for i in range(h[3]):
            off, seg = struct.unpack_from("<HH", data, h[12] + i * 4)
            at = seg * 16 + off
            struct.pack_into("<H", image, at, (struct.unpack_from("<H", image, at)[0] + load) & 0xFFFF)
        uc = self.uc
        tail = (" " + args if args else "").encode("latin-1")[:126]
        p = bytearray(256)
        p[0:2] = b"\xcd\x20"
        struct.pack_into("<H", p, 2, 0xA000)                 # first paragraph beyond the program
        struct.pack_into("<H", p, 0x2C, ENV_SEG)
        p[0x80] = len(tail)
        p[0x81:0x82 + len(tail)] = tail + b"\r"
        uc.mem_write(psp * 16, bytes(p))
        uc.mem_write(load * 16, bytes(image))
        name = os.path.basename(path).upper().encode("latin-1")
        drive = b"C" + b":"                      # the emulated DOS's drive (in pieces: no host path in the source)
        uc.mem_write(ENV_SEG * 16, b"COMSPEC=" + drive + b"\\COMMAND.COM\0\0\x01\0" + drive + b"\\SMARTALK\\" + name + b"\0")
        self.w("ds", psp)
        self.w("es", psp)
        self.w("ss", load + h[7])
        self.w("sp", h[8])
        self.w("cs", load + h[11])
        self.w("ip", h[10])
        self.load = load
        self.dta = psp * 16 + 0x80
        self.current_psp = psp
        self.resident = None
        self.stopped = None
        # the power-up talks to the box for over two seconds, timed by the timer: let the chip
        # and the PC's interrupts run beside it, silently
        uc.emu_start((load + h[11]) * 16 + h[10], 0, count=1_000_000)
        waited = 0.0
        while self.resident is None and not self.stopped and waited < 10.0:
            self._try_irq()
            before = self.chip.time
            self.chip.skip(0.0005)
            dt = max(self.chip.time - before, 1e-5)
            uc.emu_start(self.r("cs") * 16 + self.r("ip"), 0, count=max(100, int(self.cpu_ips * dt)))
            waited += dt
        if self.resident is None:
            raise RuntimeError("%s did not go resident: %s | %s"
                               % (path, self.stopped or "ran out of time", self.regs()))
        return self.resident

    # ---- interrupts: the TSR's own handlers run; BIOS and DOS are emulated -----------------
    def _int(self, uc, n, _):
        seg, off = self._vector(n)
        if seg != BIOS_SEG:
            self._dispatch(n)                   # a vector the TSR installed
            return
        ah, al = self.r("ax") >> 8, self.r("ax") & 0xFF
        if n in (0x21, 0xE1):
            self._dos(ah, al)
        elif n == 0x27:                          # terminate and stay resident, DX bytes
            self._go_resident((self.r("dx") + 15) // 16)
        elif n == 0x20:
            self._stop("program ended without staying resident")
        elif n == 0x16:                          # keyboard: nothing typed; AH=1Ch, "is SmarTalk loaded?": no
            if ah in (0x01, 0x11):
                fl = self.uc.reg_read(UC_X86_REG_EFLAGS)
                self.uc.reg_write(UC_X86_REG_EFLAGS, fl | 0x40)
            elif ah in (0x02, 0x12):
                self.w("ax", self.r("ax") & 0xFF00)
        elif n in (0x10, 0x14, 0x15, 0x17, 0x1A, 0x1C, 0x28, 0x2A, 0x2F):
            pass                                 # video, serial, AT services, printer, clock, tick, DOS idle,
                                                 # multiplex: nothing else answers
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
        elif ah == 0x4C:
            self._stop("program exited (code %d) without staying resident" % al)
        else:
            self._stop("unimplemented DOS AH=%02X AL=%02X" % (ah, al))

    # ---- the parallel port, the box and the PIC ---------------------------------------------
    def status(self):
        """379h: ACK = A/R, ERROR = INIT, SELECT high; BUSY and PAPER-OUT at a powered box's levels."""
        return (0x07 | (0x40 if self.chip.request else 0) | 0x20 | 0x10
                | (0x08 if self.control & 0x04 else 0))

    def _in(self, uc, port, size, _):
        key = ("in", port)
        self.ports[key] = self.ports.get(key, 0) + 1
        if port == 0x21:
            return self.pic_mask
        if port in (0x60, 0x61):
            return 0x00
        if port == LPT_BASE:
            return self.data
        if port == LPT_BASE + 1:
            return self.status()
        if port == LPT_BASE + 2:
            return self.control | 0xE0            # bits 5-7 read high on a plain port
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
        elif port in (0x61, 0x42, 0x43):
            pass                                 # the PC speaker and timer 2 (its beeps)
        elif port == LPT_BASE:
            self.data = value
        elif port == LPT_BASE + 2:
            old, self.control = self.control, value
            if (old & 1) and not (value & 1):     # /STROBE released: latch the data into the selected register
                reg = register(old)
                self.chip.write(reg, self.data)
                if self.keep_writes:
                    self.writes.append((round(self.chip.time, 6), reg, self.data))
                if reg == 0 and (self.data & 0x3F) and not (self.chip.regs[3] & 0x80):
                    self.last_speech = self.chip.time
                    self.preparing = False
        else:
            self._stop("unimplemented OUT %04X <- %02X" % (port, value))

    def _try_irq(self):
        """IRQ7 on ACK's rising edge while the port's interrupt is enabled; the timer at 18.2 Hz.
        The PIC is fully nested: a request waits while one of its own or a higher priority is served."""
        ack = self.chip.request
        if ack and not self.last_ack and (self.control & 0x10):
            self.irq_latched = True
        self.last_ack = ack
        fl = self.uc.reg_read(UC_X86_REG_EFLAGS)
        if not fl & 0x200:
            return False
        if self.chip.time >= self.timer_due:
            self.timer_due = self.chip.time + 1.0 / TIMER_HZ
            if not (self.pic_mask & 1) and 0 not in self.in_service:
                self._dispatch(0x08)
                self.in_service.add(0)
                return True
        if self.irq_latched and not (self.pic_mask & (1 << LPT_IRQ)) and \
                not any(i <= LPT_IRQ for i in self.in_service):
            self._dispatch(0x08 + LPT_IRQ)
            self.in_service.add(LPT_IRQ)
            self.irq_latched = False
            return True
        return False

    # ---- driving the TSR ----------------------------------------------------------------------
    def _marks_poll(self, sample):
        """the TSR's own count of marks passed, read after a slice: those new since the last look"""
        if not getattr(self, "data_seg", 0):
            return
        count = self._word(MARK_COUNT)
        if count < self._mark_seen:      # zeroed (a Ctrl-X): it counts from nothing again
            self._mark_seen = 0
        fresh = count - self._mark_seen
        self._mark_seen = count
        if not fresh:
            return
        if self._replies_read:
            self.replies, self._replies_read = [], False
        self.marks += fresh
        self.replies += [sample] * fresh

    def _cpu(self, count):
        self.uc.emu_start(self.r("cs") * 16 + self.r("ip"), 0, count=count)
        self.insns += count
        if self.stopped:
            raise RuntimeError(self.stopped)

    def _far_call(self, seg, off, budget=2_000_000, wait_limit=30.0, step=0.0005):
        """Far-call seg:off from the host; run until it returns, with the box running if it waits
        (the TSR keeps the CPU while its buffer is full, TECHMAN.TXT)."""
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
        """Let an interrupt handler a CPU slice stopped inside finish before calling the TSR."""
        n = 0
        while self.r("cs") != IDLE_SEG and n < budget:
            self._cpu(1000)
            n += 1000

    def say(self, text, speech=None):
        """Print text to LPT1 (INT 17h, AH=0 per character), as a screen reader did.  It speaks at a
        carriage return.  `speech`: whether this is text to be spoken (default: any letter or digit)."""
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
        if self._replies_read:
            self.replies, self._replies_read = [], False
        made = sum(len(p) for p in out)
        while t < seconds:
            self._try_irq()
            before = self.chip.time
            y = self.chip.run_until_request(step) if not self.chip.request else self.chip.run(step / 4)
            out.append(y)
            made += len(y)
            dt = max(self.chip.time - before, 1e-5)
            self._cpu(max(100, int(self.cpu_ips * dt)))
            self._marks_poll(made)
            t += dt
        self._replies_read = True
        return self.chip.dsp.concat(out)

    def skip(self, seconds, step=0.002):
        """Run the TSR with the chip keeping time but making no sound."""
        self.pending = []
        t = 0.0
        while t < seconds - 1e-9:
            self._try_irq()
            before = self.chip.time
            self.chip.skip(min(step, seconds - t))
            dt = max(self.chip.time - before, 1e-5)
            self._cpu(max(100, int(self.cpu_ips * dt)))
            self._marks_poll(0)
            t += dt
        return t

    @property
    def speaking(self):
        """The TSR's own flag: 1 from the first phoneme it sends until its queue has run dry."""
        return bool(self._word(SPEAKING))

    def input_pending(self):
        """Phonemes the rules have queued and the IRQ handler has not yet sent."""
        return self._word(QUEUED) != 0

    def boot(self, limit=8.0):
        """ST_RUN's part: the "{" that lets the rules speak, in its own two lines, played out silently."""
        self.say(START_LINES)
        t = self.skip(0.05)
        while t < limit and self.busy():
            t += self.skip(0.05)

    def cancel(self, limit=0.6):
        """Ctrl-X, the shut-up command: it silences speech and empties the buffer at once."""
        self.preparing = False
        self.say("\x18", speech=False)
        t = self.skip(0.01)
        while t < limit and self.busy():
            t += self.skip(0.01)
        self.replies, self._replies_read = [], False      # the cancelled text's marks: none is given
        self._mark_seen = self._word(MARK_COUNT)
        return t

    def busy(self, quiet=0.05, patience=1.5):
        if self.input_pending() or self.speaking or (self.chip.time - self.last_speech) < quiet:
            return True
        return self.preparing and (self.chip.time - self.say_time) < patience


if __name__ == "__main__":
    import wave
    here = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "firmware", "automated-functions-smartalk", "S_TALK.EXE")
    text = sys.argv[2] if len(sys.argv) > 2 else "Hello, this is SmarTalk."
    out_path = sys.argv[3] if len(sys.argv) > 3 else "smartalk_test.wav"
    s = SmarTalk(exe)
    s.boot()
    s.say(text + "\r")
    out = [s.run(0.05)]
    while s.busy():
        out.append(s.run(0.05))
    y = s.chip.dsp.concat(out)
    print("\n".join(s.log[-12:]))
    print("ports:", {("%s %X" % k): v for k, v in sorted(s.ports.items())})
    names = s.chip.rom.names
    print("phonemes:", " ".join(names.get(v & 0x3F, "?") for t, r, v in s.writes if r == 0 and v & 0x3F))
    print("%.2f s of audio, %d chip writes" % (len(y) / 44100.0, len(s.writes)))
    with wave.open(out_path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(44100)
        f.writeframes(s.chip.dsp.pcm16(y, 1.0))
