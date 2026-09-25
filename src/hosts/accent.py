"""Aicom Accent-mini (and 1600 / XE / SX / L40) driving the engine's SSI-263.

The Accent-mini card carried Aicom's AI901 speech chip -- SSI-263 register-compatible
(manual 3.2.2.20; the driver's writer at file 0x99F3 sends R4..R0 per event) -- and
little else: the text-to-speech ran on the PC, in Aicom's DOS device driver SPKEMS.DVC
("Accent-EMS Device Driver", V4.5, 1986-1993), with its rules in expanded memory.  The
Messenger-IC's SPKMIC.TSR carries 61,669 of the same bytes, i.e. the same pronunciation
engine, re-targeted at a DSP.

This host runs that driver under Unicorn the way DOS would load it from CONFIG.SYS
(DEVICE=EMM.SYS, DEVICE=SPKEMS.DVC): an INIT request to its strategy and interrupt
routines, an expanded-memory manager (INT 67h), and the card as the manual sets it up by
default -- data port 3EEh, control port 3EFh, the chip's A/R on IRQ2/IRQ9 (vector 0Ah).
Text goes in as DOS would pass it to the character device "SPK" (write requests).

The driver is NOT part of this repository: pass the path of your own copy.
"""
import copy
import os
import struct
import sys
import zlib

try:
    from .ucmini import (Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN, UC_HOOK_INTR, UC_X86_INS_IN,
                         UC_X86_INS_OUT, UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
                         UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_CS, UC_X86_REG_IP,
                         UC_X86_REG_SS, UC_X86_REG_SP, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_EFLAGS)
    from .ssi263 import SSI263
except ImportError:                       # the research tree: src/ on sys.path
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from hosts.ucmini import (Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN, UC_HOOK_INTR,  # noqa: E402
                              UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_AX, UC_X86_REG_BX,
                              UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI,
                              UC_X86_REG_BP, UC_X86_REG_CS, UC_X86_REG_IP, UC_X86_REG_SS,
                              UC_X86_REG_SP, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_EFLAGS)
    from ssi263 import SSI263  # noqa: E402

LOAD_SEG = 0x0800            # the driver image (130 KB) sits at 0800:0000
PACKET_SEG = 0x0060          # DOS request packets
TEXT_SEG = 0x0070            # a write request's transfer buffer
STACK_SEG = 0x9000
EMM_SEG = 0xC800             # a fake EMM.SYS: device header with "EMMXXXX0"
FRAME_SEG = 0xD000           # EMS page frame: four 16 KB windows
BIOS_SEG = 0xF000
IRET_OFF = 0xFF53            # F000:FF53, where every unclaimed vector points
RETURN_OFF = 0xFF00          # F000:FF00: far calls into the driver return here (HLT)
IRQ_EOI_OFF = 0xFF60         # F000:FF60, the BIOS's default for hardware IRQs: EOI, IRET
DATA_PORT, CONTROL_PORT = 0x3EF, 0x3EE
_ESC = __import__("re").compile("\x1b(?:[=+\\-O][A-Za-z]|[A-Z][0-9A-Z]|\\|~[^~]*~)")
IDLE_SEG = 0x0050            # 0050:0000 STI; JMP $ -- the CPU between interrupts
CLIENT_SEG = 0x0058          # 0058:0000 a tiny application printing DS:SI, CX chars via INT 17h
PAGE = 0x4000
EMM_CALL_RET = 0x30          # EMM_SEG:0030, INT 67h: "the routine EMS 56h called has returned"
REGS = {"ax": UC_X86_REG_AX, "bx": UC_X86_REG_BX, "cx": UC_X86_REG_CX, "dx": UC_X86_REG_DX,
        "si": UC_X86_REG_SI, "di": UC_X86_REG_DI, "bp": UC_X86_REG_BP, "cs": UC_X86_REG_CS,
        "ip": UC_X86_REG_IP, "ds": UC_X86_REG_DS, "es": UC_X86_REG_ES, "ss": UC_X86_REG_SS,
        "sp": UC_X86_REG_SP}


class Stop(Exception):
    pass


class Accent:
    def __init__(self, dvc_path, chip=None, out_rate=44100, cpu_ips=5_000_000, log=None):
        self.chip = chip or SSI263(out_rate=out_rate)
        self.cpu_ips = cpu_ips
        self.log = log if log is not None else []
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x100000)
        data = open(dvc_path, "rb").read()
        h = struct.unpack_from("<14H", data)
        if h[0] != 0x5A4D:
            raise ValueError("expected an EXE-format device driver")
        image = bytearray(data[h[4] * 16:])
        for i in range(h[3]):
            off, seg = struct.unpack_from("<HH", data, h[12] + i * 4)
            at = seg * 16 + off
            struct.pack_into("<H", image, at, (struct.unpack_from("<H", image, at)[0] + LOAD_SEG) & 0xFFFF)
        uc.mem_write(LOAD_SEG * 16, bytes(image))
        self.strategy, self.interrupt = struct.unpack_from("<HH", image, 6)
        # BIOS: every vector an IRET, AT machine id, 640 KB, the far-call return trap
        uc.mem_write(BIOS_SEG * 16 + IRET_OFF, b"\xcf")
        uc.mem_write(BIOS_SEG * 16 + RETURN_OFF, b"\xf4")
        # push ax; mov al,20h; out 0A0h,al; out 20h,al; pop ax; iret
        uc.mem_write(BIOS_SEG * 16 + IRQ_EOI_OFF, bytes([0x50, 0xB0, 0x20, 0xE6, 0xA0, 0xE6, 0x20, 0x58, 0xCF]))
        for n in range(256):
            hw = 0x08 <= n <= 0x0F or 0x70 <= n <= 0x77
            uc.mem_write(n * 4, struct.pack("<HH", IRQ_EOI_OFF if hw else IRET_OFF, BIOS_SEG))
        uc.mem_write(0xFFFFE, b"\xfc")
        uc.mem_write(0x413, struct.pack("<H", 640))
        # EMM.SYS: detected by the name in the device header its INT 67h vector points into
        uc.mem_write(EMM_SEG * 16 + 0x0A, b"EMMXXXX0")
        uc.mem_write(EMM_SEG * 16 + 0x20, b"\xcf")
        uc.mem_write(EMM_SEG * 16 + EMM_CALL_RET, b"\xcd\x67")    # 56h's called routine returns here
        self.ems_calls = []                      # (return CS, return IP, handle, old map entries, mode)
        uc.mem_write(0x67 * 4, struct.pack("<HH", 0x20, EMM_SEG))
        self.ems_pages = {}                      # (handle, logical) -> bytes
        self.ems_handles = {}                    # handle -> number of pages
        self.ems_map = [None] * 4                # physical window -> (handle, logical)
        self.ems_total = 128                     # 2 MB of expanded memory
        # the card
        self.data_latch = 0
        self.ports = {}
        self.pic_mask = [0xFF, 0xFF]
        self.in_service = False
        self.irq_latched = False                  # IRQ2: the PIC latches the A/R rising edge
        self.last_request = False
        self.control = 0
        self.last_speech = -1.0
        self.preparing = False
        self.say_time = 0.0
        self.stopped = None
        uc.mem_write(IDLE_SEG * 16, b"\xfb\xeb\xfe")
        self.lpt = image[0x53]                    # the driver answers INT 17h for this printer
        # mov al,[si]; mov ah,0; mov dx,lpt; push cx; push si; push ds; int 17h;
        # pop ds; pop si; pop cx; inc si; loop; retf
        uc.mem_write(CLIENT_SEG * 16, bytes([0x8A, 0x04, 0xB4, 0x00, 0xBA, self.lpt, 0x00, 0x51, 0x56,
                                             0x1E, 0xCD, 0x17, 0x1F, 0x5E, 0x59, 0x46, 0xE2, 0xEE, 0xCB]))
        self.writes = []                          # (chip time, reg, value)
        self.keep_writes = True
        self.pending = []           # audio the chip made while a driver call waited on it
        self.insns = 0
        self.call = None            # a driver call left running in the background (say(background=True))
        self.settles = 0            # host calls that had to let an interrupted handler finish first
        self.trace = None           # optional callable(str): host events, for a debug log
        uc.hook_add(UC_HOOK_INTR, self._int)
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        uc.reg_write(UC_X86_REG_SS, STACK_SEG)
        uc.reg_write(UC_X86_REG_SP, 0xFFFE)

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

    # ---- interrupts: the driver's own handlers run; BIOS, DOS and EMS are emulated -----
    def _int(self, uc, n, _):
        if n == 0x67 and self.r("cs") == EMM_SEG and self.r("ip") == EMM_CALL_RET + 2:
            self._ems_call_return()
            return
        seg, off = self._vector(n)
        if seg not in (BIOS_SEG, EMM_SEG):
            self._dispatch(n)                   # a vector the driver (or client) installed
            return
        ah, al = self.r("ax") >> 8, self.r("ax") & 0xFF
        if n == 0x21:
            self._dos(ah, al)
        elif n == 0x67:
            self._ems(ah, al)
        else:
            self._stop("unimplemented INT %02X AX=%04X" % (n, self.r("ax")))

    def _dos(self, ah, al):
        uc = self.uc
        self._carry(False)
        if ah == 0x09:
            s = bytes(uc.mem_read(self.r("ds") * 16 + self.r("dx"), 400)).split(b"$", 1)[0]
            self.log.append("DOS print: " + s.decode("cp437", "replace").strip())
        elif ah == 0x02:
            self.log.append("DOS char: %r" % chr(self.r("dx") & 0xFF))
        elif ah == 0x30:
            self.w("ax", 0x0005)            # DOS 5.0
            self.w("bx", 0)
            self.w("cx", 0)
        elif ah == 0x35:
            seg, off = self._vector(al)
            self.w("es", seg)
            self.w("bx", off)
        elif ah == 0x25:
            uc.mem_write(al * 4, struct.pack("<HH", self.r("dx"), self.r("ds")))
            self.log.append("DOS set vector %02X -> %04X:%04X" % (al, self.r("ds"), self.r("dx")))
        else:
            self._stop("unimplemented DOS AH=%02X AL=%02X" % (ah, al))

    def _ems_save(self, window):
        cur = self.ems_map[window]
        if cur is not None:
            self.ems_pages[cur] = bytes(self.uc.mem_read(FRAME_SEG * 16 + window * PAGE, PAGE))

    def _ems_put(self, window, key):
        """Map `key` (handle, logical) or None into a window, copying only on a change: the
        driver maps some 25,000 times during INIT alone, mostly the page already there."""
        if self.ems_map[window] == key:
            return
        self._ems_save(window)
        if key is not None:
            self.uc.mem_write(FRAME_SEG * 16 + window * PAGE, self.ems_pages.get(key, bytes(PAGE)))
        self.ems_map[window] = key

    def _ems_state(self):
        out = b""
        for m in self.ems_map:
            out += struct.pack("<HH", *(m if m is not None else (0xFFFF, 0xFFFF)))
        return out

    def _ems_restore(self, blob):
        for window in range(4):
            handle, logical = struct.unpack_from("<HH", blob, window * 4)
            self._ems_put(window, None if handle == 0xFFFF else (handle, logical))

    def _ems_map_list(self, handle, ptr, count, by_segment):
        for i in range(count):
            logical, where = struct.unpack("<HH", self.uc.mem_read(ptr + i * 4, 4))
            window = (where - FRAME_SEG) // 0x400 if by_segment else where
            self._ems_put(window, None if logical == 0xFFFF else (handle, logical))

    def _ems_alter_and_call(self, al):
        # DS:SI -> target far pointer, new map (count, far ptr), old map (count, far ptr)
        blob = bytes(self.uc.mem_read(self.r("ds") * 16 + self.r("si"), 18))
        t_off, t_seg, n_new, new_off, new_seg, n_old, old_off, old_seg = struct.unpack("<HHBHHBHH", blob[:14])
        handle = self.r("dx")
        self._ems_map_list(handle, new_seg * 16 + new_off, n_new, al == 1)
        self.ems_calls.append((self.r("cs"), self.r("ip"), handle, (old_seg * 16 + old_off, n_old), al == 1))
        self._push(EMM_SEG)
        self._push(EMM_CALL_RET)
        self.w("cs", t_seg)
        self.w("ip", t_off)

    def _ems_call_return(self):
        cs, ip, handle, (ptr, count), by_segment = self.ems_calls.pop()
        self._ems_map_list(handle, ptr, count, by_segment)
        self.w("ax", self.r("ax") & 0xFF)       # AH = 0: success
        self.w("cs", cs)
        self.w("ip", ip)

    def _ems(self, ah, al):
        """LIM EMS 3.2/4.0, as much as the driver asks for; the page frame is real memory
        and mapping copies pages in and out of it."""
        ok = 0
        if ah == 0x40:                              # status
            pass
        elif ah == 0x41:                            # page frame segment
            self.w("bx", FRAME_SEG)
        elif ah == 0x42:                            # unallocated / total pages
            used = sum(self.ems_handles.values())
            self.w("bx", self.ems_total - used)
            self.w("dx", self.ems_total)
        elif ah == 0x43:                            # allocate BX pages -> DX handle
            n = self.r("bx")
            if n > self.ems_total - sum(self.ems_handles.values()):
                ok = 0x88
            else:
                handle = 1 + max(self.ems_handles, default=0)
                self.ems_handles[handle] = n
                self.w("dx", handle)
                self.log.append("EMS allocate %d pages -> handle %d" % (n, handle))
        elif ah == 0x44:                            # map logical BX of handle DX to window AL
            window, logical, handle = al, self.r("bx"), self.r("dx")
            if handle not in self.ems_handles or window > 3:
                ok = 0x83 if handle not in self.ems_handles else 0x8B
            else:
                if logical != 0xFFFF and logical >= self.ems_handles[handle]:
                    ok = 0x8A
                else:
                    self._ems_put(window, None if logical == 0xFFFF else (handle, logical))
        elif ah == 0x45:                            # deallocate
            self.ems_handles.pop(self.r("dx"), None)
        elif ah == 0x46:                            # version
            self.w("ax", (self.r("ax") & 0xFF00) | 0x40)
        elif ah == 0x4B:                            # handle count
            self.w("bx", len(self.ems_handles))
        elif ah == 0x56 and al in (0, 1):          # alter page map and call
            self._ems_alter_and_call(al)
            return
        elif ah == 0x56 and al == 2:                # stack space 56h needs
            self.w("bx", 10)
        elif ah == 0x4E:                            # get / set page map
            if al in (0, 2):
                self.uc.mem_write(self.r("es") * 16 + self.r("di"), self._ems_state())
            if al in (1, 2):
                self._ems_restore(bytes(self.uc.mem_read(self.r("ds") * 16 + self.r("si"), 16)))
            if al == 3:
                self.w("ax", 16)                  # AH = 0 (ok), AL = size of a map
                return
        elif ah == 0x47:                            # save the map with handle DX
            self.ems_saved = getattr(self, "ems_saved", {})
            self.ems_saved[self.r("dx")] = self._ems_state()
        elif ah == 0x48:                            # restore it
            blob = getattr(self, "ems_saved", {}).get(self.r("dx"))
            if blob is None:
                ok = 0x8E
            else:
                self._ems_restore(blob)
        else:
            self._stop("unimplemented EMS AH=%02X AL=%02X" % (ah, al))
            return
        self.w("ax", (ok << 8) | (self.r("ax") & 0xFF))

    # ---- the card and the PIC -------------------------------------------------------------
    # 3EFh: data.  3EEh on write: control -- per register the driver sends (68h|reg), then
    # (70h|reg), and the second (bit 4 up, bit 3 down) latches the data into register reg;
    # bit 7 enables the card's interrupt (C0h while speaking, 40h when done).  3EEh on read:
    # status, bits 0-1 = the chip is requesting (A/R); A/R with bit 7 raises IRQ2.
    def _in(self, uc, port, size, _):
        self.ports[("in", port)] = self.ports.get(("in", port), 0) + 1
        if port == 0x21:
            return self.pic_mask[0]
        if port == 0xA1:
            return self.pic_mask[1]
        if port == CONTROL_PORT:
            return 0x03 if self.chip.request else 0x00
        if port == DATA_PORT:
            return 0xFF
        self._stop("unimplemented IN %04X" % port)
        return 0xFF

    def _out(self, uc, port, size, value, _):
        self.ports[("out", port)] = self.ports.get(("out", port), 0) + 1
        value &= 0xFF
        if port == 0x21:
            self.pic_mask[0] = value
        elif port == 0xA1:
            self.pic_mask[1] = value
        elif port in (0x20, 0xA0):
            if value == 0x20:
                self.in_service = False
        elif port == DATA_PORT:
            self.data_latch = value
        elif port == CONTROL_PORT:
            self.control = value
            self._try_irq()                       # enabling with A/R already low raises the line
            if (value & 0x18) == 0x10:
                reg, v = value & 7, self.data_latch
                self.chip.write(reg, v)
                if self.keep_writes:
                    self.writes.append((round(self.chip.time, 6), reg, v))
                if reg == 0 and (v & 0x3F) and not (self.chip.regs[3] & 0x80):
                    self.last_speech = self.chip.time        # PA (00) is not speech
                    self.preparing = False
        else:
            self._stop("unimplemented OUT %04X <- %02X" % (port, value))

    def _try_irq(self):
        # the card's IRQ line = A/R AND its interrupt enable (control bit 7: C0h while the
        # driver speaks, 40h when it is done); the PIC latches the line's rising edge
        line = self.chip.request and bool(self.control & 0x80)
        if line and not self.last_request:
            self.irq_latched = True
        self.last_request = line
        if not line:
            # an 8259 needs the request held until it is acknowledged; one that has dropped is
            # gone (a spurious IRQ 7 that sets no in-service bit).  The driver's INT 17h serves
            # the card by polling while it waits, and a stale edge delivered after that found
            # no request: the handler returned without an EOI, and IRQ2 stayed in service for
            # good -- Tomi's freeze scrolling long Mastodon posts (2026-09-25)
            self.irq_latched = False
            return False
        if not self.irq_latched:
            return False
        fl = self.uc.reg_read(UC_X86_REG_EFLAGS)
        if not fl & 0x200 or self.in_service:
            return False
        if (self.pic_mask[0] & 0x04) and (self.pic_mask[1] & 0x02):   # IRQ2 and IRQ9 both masked
            return False
        self._dispatch(0x0A)
        self.in_service = True
        self.irq_latched = False
        return True

    # ---- driving the driver -----------------------------------------------------------------
    def _far_call(self, seg, off, budget=1_000_000, wait_limit=30.0, step=0.0005, background=False):
        """Far-call seg:off from the host; run until it returns to the RETURN trap.

        Most calls return within `budget` instructions.  One that does not is waiting on the
        card -- under ESC =K (the default) a speech option command blocks inside INT 17h
        until the speech buffer has drained -- so from then on the chip runs and the IRQ is
        delivered while the driver spins, as on the real machine.  What the chip says
        meanwhile is kept in self.pending for the next run().

        background=True: a call still waiting after `budget` is left running, and run()
        carries on with it step for step -- the same steps, so the same audio -- handing the
        host what the card says while the driver is still taking the text."""
        self.stopped = None
        sp0 = self.r("sp")
        self._push(BIOS_SEG)
        self._push(RETURN_OFF)
        self.w("cs", seg)
        self.w("ip", off)
        ret = BIOS_SEG * 16 + RETURN_OFF
        self.uc.emu_start(seg * 16 + off, ret, count=budget)
        waited = 0.0
        if self.trace and not self.stopped and self.r("cs") * 16 + self.r("ip") != ret:
            self.trace("far call %04X:%04X blocked: running the card while the driver waits" % (seg, off))
        if background and not self.stopped and self.r("cs") * 16 + self.r("ip") != ret:
            # the client pushed CX (characters left) just below the return address
            self.call = {"name": "%04X:%04X" % (seg, off), "waited": 0.0, "limit": wait_limit,
                         "cx_at": self.r("ss") * 16 + ((sp0 - 6) & 0xFFFF)}
            return False
        while not self.stopped and self.r("cs") * 16 + self.r("ip") != ret:
            if waited > wait_limit:
                self._stop("driver call %04X:%04X still waiting after %.0f s" % (seg, off, waited))
                break
            self._try_irq()
            before = self.chip.time
            y = self.chip.run_until_request(step) if not self.chip.request else self.chip.run(step / 4)
            self.pending.append(y)
            dt = max(self.chip.time - before, 1e-5)
            count = max(100, int(self.cpu_ips * dt))
            self.uc.emu_start(self.r("cs") * 16 + self.r("ip"), ret, count=count)
            self.insns += count
            waited += dt
        if self.trace and waited:
            self.trace("far call %04X:%04X returned after %.3f s of card time" % (seg, off, waited))
        if self.stopped:
            raise RuntimeError(self.stopped)
        self.w("cs", IDLE_SEG)                   # back to "DOS", idle, interrupts on
        self.w("ip", 0)
        return True

    def _call_step(self, dt):
        """The CPU's share of one step while a background call runs (as _far_call's loop)."""
        ret = BIOS_SEG * 16 + RETURN_OFF
        count = max(100, int(self.cpu_ips * dt))
        self.uc.emu_start(self.r("cs") * 16 + self.r("ip"), ret, count=count)
        self.insns += count
        if self.stopped:
            raise RuntimeError(self.stopped)
        self.call["waited"] += dt
        if self.r("cs") * 16 + self.r("ip") == ret:
            if self.trace:
                self.trace("far call %s returned after %.3f s of card time" % (self.call["name"], self.call["waited"]))
            self.call = None
            self.w("cs", IDLE_SEG)
            self.w("ip", 0)
        elif self.call["waited"] > self.call["limit"]:
            raise RuntimeError("driver call %s still waiting after %.0f s" % (self.call["name"], self.call["waited"]))

    def _finish_call(self, step=0.0005):
        """Run a background call to its end, keeping what the card says (as the old blocking call)."""
        while self.call:
            self._try_irq()
            before = self.chip.time
            y = self.chip.run_until_request(step) if not self.chip.request else self.chip.run(step / 4)
            self.pending.append(y)
            self._call_step(max(self.chip.time - before, 1e-5))

    def _drop_call(self, step=0.002):
        """End a background call fast and silently, for a flush: the client stops after the
        character the driver is taking now, and the card runs without sound until it has."""
        if not self.call:
            return
        self.uc.mem_write(self.call["cx_at"], struct.pack("<H", 1))
        while self.call:
            self._try_irq()
            before = self.chip.time
            self.chip.skip(step)
            self._call_step(max(self.chip.time - before, 1e-5))
        self.pending = []

    def _settle(self, budget=400_000, limit=0.5, step=0.0005):
        """Let a handler that a CPU slice stopped inside finish before the host calls into the
        driver.  A far call from mid-handler abandons it: its EMS page map stays swapped in,
        the chip's registers half written, its frame left on the stack, and the driver then
        runs on the wrong pages and the card stays silent.  That was the freeze Tomi met
        scrolling Mastodon (2026-09-25): about 1 run() block in 400 ends inside the handler."""
        if self.r("cs") == IDLE_SEG:
            return
        where = "%04X:%04X" % (self.r("cs"), self.r("ip"))
        n = 0
        while self.r("cs") != IDLE_SEG and n < budget:
            self._cpu(1000)
            n += 1000
        waited = 0.0
        while self.r("cs") != IDLE_SEG and waited < limit:
            # the handler waits on the card: let the card run (what it says is kept)
            self._try_irq()
            before = self.chip.time
            y = self.chip.run_until_request(step) if not self.chip.request else self.chip.run(step / 4)
            self.pending.append(y)
            dt = max(self.chip.time - before, 1e-5)
            self._cpu(max(100, int(self.cpu_ips * dt)))
            waited += dt
        self.settles += 1
        if self.trace:
            self.trace("settle: the CPU was inside the driver at %s; %s after %d instructions and %.3f s"
                       % (where, "idle" if self.r("cs") == IDLE_SEG else "STILL NOT IDLE", n, waited))

    def state(self):
        """One line of the card's state, for a log."""
        six_b = self.uc.mem_read(LOAD_SEG * 16 + 0x6B, 1)[0]
        return ("cs:ip %04X:%04X control %02X in_service %s latched %s masks %02X/%02X request %s "
                "preparing %s t %.3f last_speech %.3f say_time %.3f reentry %d settles %d"
                % (self.r("cs"), self.r("ip"), self.control, self.in_service, self.irq_latched,
                   self.pic_mask[0], self.pic_mask[1], self.chip.request, self.preparing,
                   self.chip.time, self.last_speech, self.say_time, six_b, self.settles))

    def _request(self, command, extra=b"", length=None):
        """A DOS request packet to the strategy routine, then the interrupt routine."""
        pkt = bytearray(struct.pack("<BBBH8s", length or 13 + len(extra), 0, command, 0, bytes(8)) + extra)
        self.uc.mem_write(PACKET_SEG * 16, bytes(pkt))
        self.w("es", PACKET_SEG)
        self.w("bx", 0)
        self._far_call(LOAD_SEG, self.strategy)
        self.w("es", PACKET_SEG)
        self.w("bx", 0)
        self._far_call(LOAD_SEG, self.interrupt, budget=50_000_000)
        return struct.unpack_from("<H", self.uc.mem_read(PACKET_SEG * 16 + 3, 2))[0]

    def init(self):
        self.uc.mem_write(TEXT_SEG * 16, b"SPKEMS.DVC\r\n")
        status = self._request(0, struct.pack("<BIIB", 0, 0, TEXT_SEG << 16, 0), length=23)
        brk = struct.unpack_from("<HH", self.uc.mem_read(PACKET_SEG * 16 + 14, 4))
        self.log.append("INIT status %04X, resident up to %04X:%04X" % (status, brk[1], brk[0]))
        return status

    def say(self, text, speech=None, background=False):
        """Print text to the Accent's LPT (INT 17h, AH=0 per character), as a screen reader did.
        It speaks at sentence punctuation, after its input time-out, or -- after ESC =F, which
        boot() sends -- at a carriage return (manual 3.2.1.13-14).  `speech`: whether this
        is text to be spoken (default: any letter or digit outside ESC commands)."""
        data = text.encode("latin-1", "replace")
        if not data:
            return
        if speech is None:
            speech = any(ch.isalnum() for ch in _ESC.sub("", text))
        if self.call:
            self._finish_call()
        # before touching a register: the CPU may be inside the driver's IRQ handler
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
            if not self._far_call(CLIENT_SEG, 0, background=background and k + 0x4000 >= len(data)):
                break

    # ---- time ---------------------------------------------------------------------------------
    def _cpu(self, count):
        uc = self.uc
        uc.emu_start(self.r("cs") * 16 + self.r("ip"), 0, count=count)
        self.insns += count
        if self.stopped:
            raise RuntimeError(self.stopped)

    def run(self, seconds, step=0.0005):
        out, t = self.pending, 0.0
        self.pending = []
        while t < seconds:
            self._try_irq()
            before = self.chip.time
            y = self.chip.run_until_request(step) if not self.chip.request else self.chip.run(step / 4)
            out.append(y)
            dt = max(self.chip.time - before, 1e-5)
            if self.call:
                self._call_step(dt)
            else:
                self._cpu(max(100, int(self.cpu_ips * dt)))
            t += dt
        return self.chip.dsp.concat(out)

    @property
    def speaking(self):
        """The driver's own flag, on the card: interrupt enabled (C0h) while it speaks, 40h
        the moment it is done -- including after a flush."""
        return bool(self.control & 0x80)

    def busy(self, quiet=0.03, patience=1.5):
        if self.call or self.speaking or (self.chip.time - self.last_speech) < quiet:
            return True
        return self.preparing and (self.chip.time - self.say_time) < patience

    def skip(self, seconds, step=0.002):
        """Run the driver with the chip keeping time but making no sound."""
        if self.call:
            self._finish_call()
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

    # ---- the machine after INIT, saved once: INIT is 0.58 s of launch, a restore a few ms ------
    _STATE = ("ems_pages", "ems_handles", "ems_map", "ems_calls", "ems_total", "data_latch", "ports",
              "pic_mask", "in_service", "irq_latched", "last_request", "control", "last_speech",
              "preparing", "say_time", "insns")

    def init_state(self):
        """INIT, then everything needed to recreate this machine without running it again:
        memory, CPU registers, the host's card and EMS state, and the chip writes INIT made
        (all at chip time 0, so replaying them recreates the chip exactly)."""
        n0, keep = len(self.writes), self.keep_writes
        self.keep_writes = True
        if self.chip.time != 0.0:
            raise RuntimeError("init_state wants a fresh chip")
        self.init()
        state = {"version": 1, "mem": zlib.compress(bytes(self.uc.mem_read(0, 0x100000)), 9),
                 "regs": dict((k, self.r(k)) for k in REGS), "eflags": self.uc.reg_read(UC_X86_REG_EFLAGS),
                 "chip_writes": [(r, v) for _, r, v in self.writes[n0:]],
                 "host": dict((k, getattr(self, k)) for k in self._STATE), "log": list(self.log)}
        if self.chip.time != 0.0:
            raise RuntimeError("INIT ran the chip's clock: a snapshot would not be exact")
        self.keep_writes = keep
        return state

    def load_state(self, state):
        if state.get("version") != 1:
            raise ValueError("unknown state version")
        self.uc.mem_write(0, zlib.decompress(state["mem"]))
        for k, v in state["regs"].items():
            self.w(k, v)
        self.uc.reg_write(UC_X86_REG_EFLAGS, state["eflags"])
        for k, v in state["host"].items():
            setattr(self, k, copy.deepcopy(v))
        self.log.extend(state["log"])
        for r, v in state["chip_writes"]:
            self.chip.write(r, v)

    def boot(self, limit=4.0, state=None):
        """Load the driver, let its "Accent ready" play out silently, then have a carriage
        return start speech (ESC =F) and Ctrl-X flush at once (ESC =M), as a screen reader
        would set it up.  `state` (from init_state) replaces running INIT."""
        if state is not None:
            self.load_state(state)
        else:
            self.init()
        # set up at once and flush the greeting rather than sit through it (0.6 s of launch)
        self.say("\x1b=F\x1b=M\x18", speech=False)
        t = self.skip(0.02)
        while t < limit and self.busy():
            t += self.skip(0.02)

    def cancel(self, limit=0.6):
        """Ctrl-X: the Accent's instant flush; then let what it had already sent play out
        silently, so none of the cut speech reaches the next utterance."""
        self.preparing = False
        self._drop_call()
        self.say("\x18", speech=False)
        t = self.skip(0.01)
        while t < limit and self.busy():
            t += self.skip(0.01)
        return t


if __name__ == "__main__":
    import wave
    dvc = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "firmware", "aicom-accent-mini", "SPKEMS.DVC")
    text = sys.argv[2] if len(sys.argv) > 2 else "Hello, this is the Accent."
    a = Accent(dvc)
    a.init()
    a.run(0.2)
    a.say(text + "\r")
    out = [a.run(0.05)]
    while a.busy():
        out.append(a.run(0.05))
    y = a.chip.dsp.concat(out)
    print("\n".join(a.log[-12:]))
    print("ports:", {("%s %X" % k): v for k, v in a.ports.items()})
    names = a.chip.rom.names
    print("phonemes:", " ".join(names.get(v & 0x3F, "?") for t, r, v in a.writes if r == 0 and v & 0x3F))
    print("%.2f s of audio, %d chip writes" % (len(y) / 44100.0, len(a.writes)))
    with wave.open("accent_test.wav", "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(44100)
        f.writeframes(a.chip.dsp.pcm16(y, 1.0))
