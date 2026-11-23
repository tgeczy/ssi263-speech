"""Artic Technologies' SynPhonix 215 driving the engine's SSI-263, through Artic's own VEST.COM.

The SynPhonix 215 is an ISA card with an Artic-263 (an SSI-263) and little else: its text-to-speech runs on the PC,
in Artic's DOS TSR VEST ("Visions Enhanced Speech Translator", version 1.60 English, the "Special Release For Artic
Visions" that Business Vision 3.08 loads first: BV.BAT).  VEST.COM is the whole of it -- letter-to-sound rules,
numbers, lexicon and the card's interrupt handler.  A program calls it far (below); nothing else on the PC talks.

This host loads VEST.COM as DOS would (no switches: IRQ 7, as BV.BAT), with the card on its first ISA address, and
runs it until it goes resident.  The card, as read from VEST's code and confirmed by it speaking (the addresses are
VEST.COM's own, CS:0100 being the file's first byte):

  2E0h-2E5h  the card's key: "AR" (41h 52h: the probe at A02E adds 4365h and wants B784h), then four bytes of the
             card's serial number, each inverted, two decimal digits a byte (low nibble first).  VEST takes the first
             card it finds from 2E0h down to 0E0h in steps of 20h (the MAP jumper); the chip is 8 above the key.  The
             other cards it looks for first (a Micro Channel card through the POS registers, ID FF70h; a card at
             2F8h; a key in an option ROM from C000h to EC00h) are not this one and read as absent.
  2E8h-2ECh  the chip's R0-R4.  Every byte VEST writes is rotated left one bit first (the routine at 0D27,
             NOP; ROL AL,1; RET): the card's data lines go to the chip one place over, so the chip's bit n is the
             PC's bit n+1.  The host rotates each byte back.  VEST rotates only when the key's first two digits
             are 25 (a "2X5 type" card, 9649); this host gives a 215's key.
  2E8h read  the switch interface's pedals on bits 6 and 7 (the IRQ handler's turbo, read only when Business Vision
             turns it on); both read high here, no pedal pressed (a GUESS: open switches pulled up).
  IRQ 7      the chip's A/R.  The handler (0AFA) sends the next frame and ends with a specific EOI (67h); every
             8Fh interrupts it re-reads the key's fifth byte and ends the program if its low nibble is above 0Ah.

The serial check: VEST holds the serial of the card it was sold with (encoded at 09BC, each digit + A7h), and with a
key of any other serial it quietly sends every write to 3F0h instead (9689).  The host makes the key from the
program it is given -- it finds the comparison's code and reads the encoded serial back -- so any copy of this VEST
finds its own card, and no serial is written here.

Programs reach VEST as Business Vision does: it looks for VEST's signature (words 0038h at 01E0, 1D2Dh at 01E2 and
7A83h at 012A, on a paragraph boundary from 0100:0 up) and calls far, with PUSHF, the address at 01E4 (0558).  AL
selects: 7 speaks ES:SI, DI characters long, with CH/CL/DH/DL the rate, amplitude, pitch and filter offsets the
frames get (6A2); 1 cuts speech and empties the queue (6DB); 3 sets the offsets alone; 4 and 5 send or queue one
frame of five register values; 6 queues a pause; 2 and 0Fh read status.  VEST's own software interrupt (the SONIX
vectors at 60h-67h) is not installed by this special release.

Its own start-up speaks "Visions Enhanced Speech Translator. 1 point 6 0" (9EA9) with CX = 0200h, DX = 00E6h; say()
uses the same offsets unless given others.  boot() lets that announcement play out silently.

The software is NOT part of this repository: pass the path of your own VEST.COM (firmware/artic-synphonix/README.txt).

Must-fail controls (SYNPHONIX_HOST_BREAK, tools/check_synphonix_host.py): "key" makes the key with one serial
digit changed; "rotate" writes the PC's bytes to the chip unrotated; "eoi" ignores the specific EOI; "cancel" cuts
with AL=0 (VEST's reset, which leaves the queue) instead of AL=1; "keycache" makes the key once per process, from the
first program loaded.
"""
import os
import struct
import sys

try:
    from .x86_api import (UC_HOOK_INSN, UC_HOOK_INTR, UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_AX, UC_X86_REG_BX,
                         UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_CS,
                         UC_X86_REG_IP, UC_X86_REG_SS, UC_X86_REG_SP, UC_X86_REG_DS, UC_X86_REG_ES,
                         UC_X86_REG_EFLAGS)
    from .pc86 import Pc86
except ImportError:                       # the research tree: src/ on sys.path
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from hosts.x86_api import (UC_HOOK_INSN, UC_HOOK_INTR, UC_X86_INS_IN, UC_X86_INS_OUT,  # noqa: E402
                              UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI,
                              UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_CS, UC_X86_REG_IP, UC_X86_REG_SS,
                              UC_X86_REG_SP, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_EFLAGS)
    from hosts.pc86 import Pc86  # noqa: E402
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from ssi263 import SSI263  # noqa: E402

IDLE_SEG = 0x0050            # 0050:0000 STI; JMP $ -- "DOS" between interrupts
ENV_SEG = 0x0068             # an environment block: COMSPEC, then the program's path
TEXT_SEG = 0x0070            # the caller's text buffer (16 KB)
PSP_SEG = 0x0800             # VEST's PSP; the .COM loads at PSP:0100
STACK_SEG = 0x9000           # the host's own stack, for far calls
BIOS_SEG = 0xF000
IRET_OFF = 0xFF53            # F000:FF53, where every unclaimed vector points
RETURN_OFF = 0xFF00          # F000:FF00: far calls into the TSR return here (HLT)
IRQ_EOI_OFF = 0xFF60         # F000:FF60, the BIOS's default for hardware IRQs: EOI, IRET
TIMER_OFF = 0xFF70           # F000:FF70, the BIOS's INT 08h: count the tick at 0040:006C, INT 1Ch, EOI
KEY_BASE = 0x2E0             # the first ISA address VEST tries (A00C): the key here, the chip 8 above
CHIP_BASE = KEY_BASE + 8
CARD_IRQ = 7                 # VEST without /I= (BV.BAT)
TIMER_HZ = 1193182.0 / 65536
# in VEST's segment (its data is its code segment)
SIG = ((0x1E0, 0x0038), (0x1E2, 0x1D2D), (0x12A, 0x7A83))   # what Business Vision looks for (BVISION 9B67)
ENTRY_AT = 0x1E4             # ... and the entry it calls there
QUEUE_IN, QUEUE_OUT = 0x190, 0x192   # the frame queue's write and read pointers (5 bytes a frame, AC1 and D56)
IDLE = 0x16D                 # 1 once the IRQ handler has found the queue empty (0BAB), 0 when a frame is queued (0AEB)
SAY_CX, SAY_DX = 0x0200, 0x00E6      # VEST's own start-up announcement's offsets (9EB5): rate +2, filter E6h
# the serial check's code, VEST 1.60 at 9657: MOV SI,serial; MOV DI,buffer; CMP BYTE [SI],0; JZ; MOV AX,[SI];
# SUB AL,A7h; SUB AH,A7h
SERIAL_CODE = (b"\xbe", 2, b"\xbf", 2, b"\x80\x3c\x00\x74", 1, b"\x8b\x04\x2c\xa7\x80\xec\xa7")
REGS = {"ax": UC_X86_REG_AX, "bx": UC_X86_REG_BX, "cx": UC_X86_REG_CX, "dx": UC_X86_REG_DX,
        "si": UC_X86_REG_SI, "di": UC_X86_REG_DI, "bp": UC_X86_REG_BP, "cs": UC_X86_REG_CS,
        "ip": UC_X86_REG_IP, "ds": UC_X86_REG_DS, "es": UC_X86_REG_ES, "ss": UC_X86_REG_SS,
        "sp": UC_X86_REG_SP}
BREAK = os.environ.get("SYNPHONIX_HOST_BREAK", "")
_cached_key = None


def ror1(v):
    """The chip's byte from the PC's: VEST rotated it left one bit for the card's wiring."""
    return ((v >> 1) | (v << 7)) & 0xFF


def _find_serial(image):
    """The offset (in the file) of VEST's encoded serial, read from the code that compares it."""
    for at in range(len(image) - 24):
        p, ok = at, True
        for part in SERIAL_CODE:
            if isinstance(part, int):
                p += part
            elif image[p:p + len(part)] == part:
                p += len(part)
            else:
                ok = False
                break
        if ok:
            return struct.unpack_from("<H", image, at + 1)[0] - 0x100
    raise ValueError("not VEST 1.60: the serial check's code is not there")


def card_key(image):
    """The six bytes a 215 matching this VEST gives at 2E0h-2E5h.

    VEST turns the key's four serial bytes into eight digits (each byte inverted, low nibble first: 9623), reverses
    the four digit pairs (96A7) and wants 25 then the last six of its own serial, decoded (each byte - A7h, a pair's
    two swapped, ASCII: 9662) and reversed the same way."""
    global _cached_key
    if BREAK == "keycache" and _cached_key is not None:
        return _cached_key
    at = _find_serial(image)
    raw = []
    while image[at]:
        raw += [(image[at + 1] - 0xA7 - 0x30) & 0xFF, (image[at] - 0xA7 - 0x30) & 0xFF]   # ASCII digits
        at += 2
    if len(raw) != 8 or any(d > 9 for d in raw):
        raise ValueError("VEST's serial is not eight digits")
    pairs = [raw[i:i + 2] for i in range(0, 8, 2)][::-1]
    want = [2, 5] + [d for pair in pairs for d in pair][2:]
    if BREAK == "key":
        want[-1] = (want[-1] + 1) % 10
    key = [0x41, 0x52]
    for i in range(4):                    # byte i gives the digit pair that ends up (3 - i)th
        lo, hi = want[2 * (3 - i)], want[2 * (3 - i) + 1]
        key.append(((hi << 4) | lo) ^ 0xFF)
    _cached_key = bytes(key)
    return _cached_key


class SynPhonix:
    def __init__(self, com_path, chip=None, out_rate=44100, cpu_ips=5_000_000, log=None):
        self.chip = chip or SSI263(out_rate=out_rate)
        self.cpu_ips = cpu_ips           # instructions per chip second: a scheduling policy, not a clock
        self.log = log if log is not None else []
        image = open(com_path, "rb").read()
        self.key = card_key(image)
        self.uc = uc = Pc86()             # MAME's 8086 (pc86.py), as the SmarTalk's and the Accent-mini's hosts
        uc.mem_map(0, 0x100000)
        # BIOS: every vector an IRET, hardware IRQs EOI, the timer, 640 KB, an AT's model byte, the far-call trap
        uc.mem_write(BIOS_SEG * 16 + IRET_OFF, b"\xcf")
        uc.mem_write(BIOS_SEG * 16 + RETURN_OFF, b"\xf4")
        uc.mem_write(BIOS_SEG * 16 + IRQ_EOI_OFF, bytes([0x50, 0xB0, 0x20, 0xE6, 0x20, 0x58, 0xCF]))
        for n in range(256):
            uc.mem_write(n * 4, struct.pack("<HH", IRQ_EOI_OFF if 0x08 <= n <= 0x0F else IRET_OFF, BIOS_SEG))
        for n in range(0x60, 0x68):       # the user vectors free, as on a PC without SONIX (VEST looks at them)
            uc.mem_write(n * 4, b"\0\0\0\0")
        # push ax; push ds; xor ax,ax; mov ds,ax; inc word [46Ch]; jnz $+6; inc word [46Eh]; int 1Ch;
        # mov al,20h; out 20h,al; pop ds; pop ax; iret
        uc.mem_write(BIOS_SEG * 16 + TIMER_OFF, bytes([0x50, 0x1E, 0x31, 0xC0, 0x8E, 0xD8, 0xFF, 0x06, 0x6C, 0x04,
                                                       0x75, 0x04, 0xFF, 0x06, 0x6E, 0x04, 0xCD, 0x1C, 0xB0, 0x20,
                                                       0xE6, 0x20, 0x1F, 0x58, 0xCF]))
        uc.mem_write(0x08 * 4, struct.pack("<HH", TIMER_OFF, BIOS_SEG))
        uc.mem_write(0xFFFFE, b"\xfc")                       # model byte: AT (VEST: A44C)
        uc.mem_write(0x413, struct.pack("<H", 640))
        uc.mem_write(0x449, b"\x03")                         # video mode 3, for its banner
        uc.mem_write(IDLE_SEG * 16, b"\xfb\xeb\xfe")
        self.ports = {}
        self.pic_mask = 0xB8               # as the BIOS leaves it: the timer, keyboard, IRQ2 and floppy on
        self.in_service = set()
        self.irq_latched = False
        self.last_req = False
        self.timer_due = 0.0
        self.writes = []                   # (chip time, reg, value): the chip's values, rotated back
        self.last_speech = -1.0
        self.screen = []
        self.insns = 0
        self.stopped = None
        self.resident = None
        uc.hook_add(UC_HOOK_INTR, self._int)
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        self._exec(com_path, image)
        self.log.append("VEST resident: %d bytes at %04X" % (self.resident, PSP_SEG))
        self.seg, self.entry = self._find_vest()
        self.log.append("VEST found at %04X, entry %04X:%04X" % (self.seg, self.seg, self.entry))
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

    def word(self, off):
        """A word of VEST's data (its own segment)."""
        return struct.unpack("<H", self.uc.mem_read(self.seg * 16 + off, 2))[0]

    def byte(self, off):
        return self.uc.mem_read(self.seg * 16 + off, 1)[0]

    # ---- loading a .COM as COMMAND.COM would -------------------------------------------
    def _exec(self, path, image):
        uc = self.uc
        p = bytearray(256)
        p[0:2] = b"\xcd\x20"
        struct.pack_into("<H", p, 2, 0xA000)                 # first paragraph beyond the program
        struct.pack_into("<H", p, 0x2C, ENV_SEG)
        p[0x81] = 0x0D                                       # no switches (BV.BAT)
        uc.mem_write(PSP_SEG * 16, bytes(p))
        uc.mem_write(PSP_SEG * 16 + 0x100, image)
        drive = b"C" + b":"                      # the emulated DOS's drive (in pieces: no host path in the source)
        uc.mem_write(ENV_SEG * 16, b"COMSPEC=" + drive + b"\\COMMAND.COM\0\0\x01\0"
                     + drive + b"\\ARTIC\\VISION\\VEST.COM\0")
        for name in ("cs", "ds", "es", "ss"):
            self.w(name, PSP_SEG)
        self.w("sp", 0xFFFE)
        uc.mem_write(PSP_SEG * 16 + 0xFFFE, b"\0\0")         # a RET from the program ends it (INT 20h at PSP:0)
        self.w("ip", 0x100)
        # VEST starts the chip and speaks its announcement before it goes resident, timing its wait for the card's
        # interrupts by the timer (9717): the chip and the PC's interrupts run beside it, silently
        waited = 0.0
        while self.resident is None and not self.stopped and waited < 20.0:
            self._try_irq()
            before = self.chip.time
            self.chip.skip(0.0005)
            dt = max(self.chip.time - before, 1e-5)
            uc.emu_start(self.r("cs") * 16 + self.r("ip"), 0, count=max(100, int(self.cpu_ips * dt)))
            waited += dt
        if self.resident is None:
            raise RuntimeError("%s did not go resident: %s | %s"
                               % (path, self.stopped or "ran out of time", self.regs()))
        text = "".join(self.screen)
        if "ERROR" in text:
            raise RuntimeError("VEST refused the card: " + " ".join(text.split()))

    def _find_vest(self):
        """Business Vision's search (BVISION 9B67): VEST's signature on a paragraph boundary, then its entry."""
        mem = self.uc.mem_read(0, 0x100000)
        for seg in range(0x100, 0xFF00):
            if all(struct.unpack_from("<H", mem, seg * 16 + off)[0] == v for off, v in SIG):
                return seg, struct.unpack_from("<H", mem, seg * 16 + ENTRY_AT)[0]
        raise RuntimeError("VEST's signature not found")

    # ---- interrupts: the TSR's own handlers run; BIOS and DOS are emulated -----------------
    def _int(self, uc, n, _):
        seg, off = self._vector(n)
        if seg != BIOS_SEG:
            self._dispatch(n)                   # a vector the TSR installed
            return
        ah, al = self.r("ax") >> 8, self.r("ax") & 0xFF
        if n == 0x21:
            self._dos(ah, al)
        elif n == 0x27:                          # terminate and stay resident, DX bytes
            self.resident = self.r("dx")
            uc.emu_stop()
        elif n == 0x20:
            self._stop("program ended without staying resident: " + " ".join("".join(self.screen).split()))
        elif n == 0x10:                          # video: its banner and messages, by teletype
            if ah == 0x0E:
                self.screen.append(chr(al) if al < 0x80 else " ")
            elif ah == 0x0F:
                self.w("ax", 0x5003)             # 80 columns, mode 3
                self.w("bx", self.r("bx") & 0x00FF)
        elif n == 0x15:                          # AT services: not a PS/2 (no POS registers: C400h, C401h)
            if ah == 0xC4:
                self.w("ax", (0x86 << 8) | al)
                self._carry(True)
        elif n in (0x16, 0x1A, 0x1C, 0x28, 0x2F):
            pass
        else:
            self._stop("unimplemented INT %02X AX=%04X" % (n, self.r("ax")))

    def _dos(self, ah, al):
        uc = self.uc
        self._carry(False)
        if ah == 0x09:
            self.screen.append(self._string(self.r("ds"), self.r("dx"), b"$", 400).decode("cp437", "replace"))
        elif ah == 0x02:
            self.screen.append(chr(self.r("dx") & 0x7F))
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
            self.resident = self.r("dx") * 16
            uc.emu_stop()
        elif ah in (0x51, 0x62):
            self.w("bx", PSP_SEG)
        elif ah in (0x49, 0x4A):                 # free / resize memory: always fine
            pass
        elif ah == 0x4C:
            self._stop("program exited (code %d) without staying resident" % al)
        else:
            self._stop("unimplemented DOS AH=%02X AL=%02X" % (ah, al))

    # ---- the card and the PIC -----------------------------------------------------------------
    def _in(self, uc, port, size, _):
        key = ("in", port)
        self.ports[key] = self.ports.get(key, 0) + 1
        if port == 0x21:
            return self.pic_mask
        if KEY_BASE <= port < KEY_BASE + len(self.key):
            return self.key[port - KEY_BASE]
        if port == CHIP_BASE:
            return 0xFF                          # the pedals: none pressed (a GUESS)
        return 0xFF                              # VEST's probes of other cards' places: nothing there

    def _out(self, uc, port, size, value, _):
        key = ("out", port)
        self.ports[key] = self.ports.get(key, 0) + 1
        value &= 0xFF
        if port == 0x21:
            self.pic_mask = value
        elif port == 0x20:
            if value == 0x20 and self.in_service:            # non-specific EOI: the highest priority in service
                self.in_service.discard(min(self.in_service))
            elif value & 0xF8 == 0x60 and BREAK != "eoi":   # specific EOI (VEST's 0C04: 60h + its IRQ)
                self.in_service.discard(value & 7)
        elif CHIP_BASE <= port <= CHIP_BASE + 4:
            reg = port - CHIP_BASE
            v = value if BREAK == "rotate" else ror1(value)
            self.chip.write(reg, v)
            self.writes.append((round(self.chip.time, 6), reg, v))
            if reg == 0 and (v & 0x3F) and not (self.chip.regs[3] & 0x80):
                self.last_speech = self.chip.time
        else:
            self._stop("unimplemented OUT %04X <- %02X" % (port, value))

    def _try_irq(self):
        """IRQ 7 on A/R's rising edge (an ISA line, edge-triggered); the timer at 18.2 Hz.  The PIC is fully nested:
        a request waits while one of its own or a higher priority is served."""
        req = self.chip.request
        if req and not self.last_req:
            self.irq_latched = True
        self.last_req = req
        fl = self.uc.reg_read(UC_X86_REG_EFLAGS)
        if not fl & 0x200:
            return False
        if self.chip.time >= self.timer_due:
            self.timer_due = self.chip.time + 1.0 / TIMER_HZ
            if not (self.pic_mask & 1) and 0 not in self.in_service:
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

    # ---- driving the TSR ----------------------------------------------------------------------
    def _cpu(self, count):
        self.uc.emu_start(self.r("cs") * 16 + self.r("ip"), 0, count=count)
        self.insns += count
        if self.stopped:
            raise RuntimeError(self.stopped)

    def _settle(self, budget=400_000):
        """Let an interrupt handler a CPU slice stopped inside finish before calling the TSR."""
        n = 0
        while self.r("cs") != IDLE_SEG and n < budget:
            self._cpu(1000)
            n += 1000

    def call(self, ax, budget=5_000_000, wait_limit=30.0, step=0.0005, **regs):
        """PUSHF; CALL FAR VEST's entry with these registers (Business Vision's 5197); run until it returns, with
        the card running if VEST waits for room in its queue.  Returns AX."""
        self._settle()
        self.stopped = None
        self.w("ss", STACK_SEG)
        self.w("sp", 0xFFFE)
        self._push(self.uc.reg_read(UC_X86_REG_EFLAGS) | 0x200)
        self._push(BIOS_SEG)
        self._push(RETURN_OFF)
        self.w("ax", ax)
        for k, v in regs.items():
            self.w(k, v)
        self.w("cs", self.seg)
        self.w("ip", self.entry)
        ret = BIOS_SEG * 16 + RETURN_OFF
        self.uc.emu_start(self.seg * 16 + self.entry, ret, count=budget)
        waited = 0.0
        self.pending = []
        while not self.stopped and self.r("cs") * 16 + self.r("ip") != ret:
            if waited > wait_limit:
                self._stop("call AX=%04X still waiting after %.0f s" % (ax, waited))
                break
            self._try_irq()
            before = self.chip.time
            self.pending.append(self.chip.run_until_request(step) if not self.chip.request else self.chip.run(step / 4))
            dt = max(self.chip.time - before, 1e-5)
            self.uc.emu_start(self.r("cs") * 16 + self.r("ip"), ret, count=max(100, int(self.cpu_ips * dt)))
            waited += dt
        if self.stopped:
            raise RuntimeError(self.stopped)
        ax = self.r("ax")
        self.w("cs", IDLE_SEG)
        self.w("ip", 0)
        self.w("ss", STACK_SEG)
        self.w("sp", 0xFFFE)
        return ax

    def say(self, text, cx=SAY_CX, dx=SAY_DX):
        """Speak text (AL=7): ES:SI and DI its length; CH, CL, DH, DL the rate, amplitude, pitch and filter offsets
        VEST adds to its frames (6A2, applied at 09F1)."""
        data = text.encode("latin-1", "replace")[:0x4000]
        if not data:
            return
        self.uc.mem_write(TEXT_SEG * 16, data)
        self.call(0x0007, es=TEXT_SEG, si=0, di=len(data), cx=cx, dx=dx)

    def cancel(self):
        """Cut speech and empty the queue (AL=1, 6DB: the read pointer set to the write pointer, a silent R0)."""
        self.call(0x0000 if BREAK == "cancel" else 0x0001)

    def run(self, seconds, step=0.0005):
        out, t = getattr(self, "pending", []), 0.0
        self.pending = []
        while t < seconds:
            self._try_irq()
            before = self.chip.time
            out.append(self.chip.run_until_request(step) if not self.chip.request else self.chip.run(step / 4))
            dt = max(self.chip.time - before, 1e-5)
            self._cpu(max(100, int(self.cpu_ips * dt)))
            t += dt
        return self.chip.dsp.concat(out)

    def skip(self, seconds, step=0.002):
        """Run VEST with the chip keeping time but making no sound."""
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

    def queued(self):
        """Frames VEST has queued and its IRQ handler has not yet sent."""
        return self.word(QUEUE_IN) != self.word(QUEUE_OUT)

    def busy(self, quiet=0.05):
        """Speaking: frames queued, the handler not yet idle, or a phoneme within the last `quiet` seconds."""
        return self.queued() or not self.byte(IDLE) or (self.chip.time - self.last_speech) < quiet

    def boot(self, limit=10.0):
        """Let VEST's own announcement play out, silently."""
        t = self.skip(0.05)
        while t < limit and self.busy():
            t += self.skip(0.05)
        return t


if __name__ == "__main__":
    import wave
    here = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    com = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "firmware", "artic-synphonix", "VEST.COM")
    text = sys.argv[2] if len(sys.argv) > 2 else "Hello, this is the Artic SynPhonix."
    out_path = sys.argv[3] if len(sys.argv) > 3 else "synphonix_test.wav"
    s = SynPhonix(com)
    s.boot()
    n0 = len(s.writes)
    s.say(text)
    out = [s.run(0.05)]
    while s.busy():
        out.append(s.run(0.05))
    y = s.chip.dsp.concat(out)
    print("\n".join(s.log[-6:]))
    names = s.chip.rom.names
    print("phonemes:", " ".join(names.get(v & 0x3F, "?") for t, r, v in s.writes[n0:] if r == 0 and v & 0x3F))
    print("%.2f s of audio, %d chip writes" % (len(y) / 44100.0, len(s.writes) - n0))
    with wave.open(out_path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(44100)
        f.writeframes(s.chip.dsp.pcm16(y, 1.0))
