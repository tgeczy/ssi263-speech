"""The Aicom Accent SA: its own 8085 firmware (u2, with the u3/u4 dictionary ROMs) on an
emulated 8085, driving the engine's SSI-263.

The stand-alone Accent SA is a serial box: text arrives on an 8251 USART, its 8085 runs
Aicom's rules and dictionaries, and an AI901 (the SSI-263) speaks.  This host gives the
firmware the machine it was written for, as read from u2 and confirmed by running it (the
one host liberty is `turbo`, see _cpu):

  0000-77FF  u2, lower half (program)          7800-7FFF  RAM (2 KB; SP = 8000h)
  8000-FFFF  a 32 KB window, banked by port 40h bits 0-1:
             0 = u2's upper half (more program and tables), 1 = u3, 2 = u4 (the
             "AICOM COPYRIGHT1" / "2" exception dictionary), 3 = nothing (the firmware
             probes it for a signature and finds none)
  ports 03-07  the SSI-263, reversed: 03 = R4, 04 = R3, 05 = R2, 06 = R1, 07 = R0; a read
               of 07 has A/R in bit 7
  ports 20/21  the 8251: data, and mode / command / status.  RxRDY is RST 6.5.
  port 40      out: bank (bits 0-1) and bit 4, which gates A/R onto TRAP; in: option switches
  port 80      a parallel input (RST 5.5), unused here
  TRAP         A/R AND port 40 bit 4.  The firmware sets bit 4 (4FFDh) once it has queued
               phonemes and clears it (5014h) when its queue runs dry; its TRAP handler
               (4E68h) writes the next five registers and refills the queue.  Ungated, an
               idle A/R walks the queue's read count past its write count and it never
               speaks again.
  RST 7.5      a clock the firmware times at power-up (0F41h) to pick a mode; with none
               (tick_hz = 0) it runs in the mode the boot code calls 3.

The bank map and the TRAP gate are read from the code and confirmed by it speaking ("Accent
ready." at power-up, then text); the 8085's clock is a GUESS (3.072 MHz, a 6.144 MHz crystal).
"""
import collections
import os
import re

from .i8085 import I8085

try:
    from .ssi263 import SSI263           # inside an add-on: the engine is a sibling package
except ImportError:                      # the research tree: src/ on sys.path
    from ssi263 import SSI263  # noqa: E402

CPU_HZ = 3_072_000
REG = {0x03: 4, 0x04: 3, 0x05: 2, 0x06: 1, 0x07: 0}
RTS, DTR = 0x20, 0x02
_ESC = re.compile("\x1b(?:[=+\\-O][A-Za-z]|[A-Z][0-9A-Z]|\\|~[^~]*~)")   # as hosts.accent


class AccentSA:
    def __init__(self, rom_dir, chip=None, out_rate=44100, cpu_hz=CPU_HZ, tick_hz=0.0, switches=0x00,
                 turbo=8.0):
        if chip is None:
            chip = SSI263(out_rate=out_rate)
        self.chip = chip
        self.cpu_hz = cpu_hz
        self.turbo = turbo
        self.tick_hz = tick_hz
        self.switches = switches
        u2 = open(os.path.join(rom_dir, "u2.BIN"), "rb").read()
        self.low = u2[:0x7800]
        self.banks = [u2[0x8000:], open(os.path.join(rom_dir, "u3.BIN"), "rb").read(),
                      open(os.path.join(rom_dir, "u4.BIN"), "rb").read(), b"\xff" * 0x8000]
        self.ram = bytearray(0x800)
        self.bank = self.banks[0]
        self.latch40 = 0
        self.rx = collections.deque()
        self.rx_ready = False
        self.rx_byte = 0
        self.usart_mode_next = True     # the 8251 takes a mode word after reset
        self.usart_cmd = 0
        self.tx = bytearray()
        self.trap_line = False
        self.last_speech = -1.0
        self.preparing = False
        self.say_time = 0.0
        self.keep_writes = True
        self.writes = []
        self.trace = None
        self._tick_acc = 0.0
        self.cpu = I8085(self._read, self._write, self._in, self._out)

    # ---- the machine ----------------------------------------------------------------------
    def _read(self, a):
        if a < 0x7800:
            return self.low[a]
        if a < 0x8000:
            return self.ram[a - 0x7800]
        return self.bank[a - 0x8000]

    def _write(self, a, v):
        if 0x7800 <= a < 0x8000:
            self.ram[a - 0x7800] = v

    def _in(self, p):
        if p == 0x07:
            return 0x80 if self.chip.request else 0x00
        if p == 0x20:
            self.rx_ready = False
            self.cpu.set_rst65(False)
            return self.rx_byte
        if p == 0x21:
            # TxRDY, TxEMPTY, DSR (the host's DTR is up), RxRDY
            return 0x85 | (0x02 if self.rx_ready else 0)
        if p == 0x40:
            return self.switches
        return 0x00

    def _out(self, p, v):
        reg = REG.get(p)
        if reg is not None:
            self.chip.write(reg, v)
            if self.keep_writes:
                self.writes.append((round(self.chip.time, 6), reg, v))
            if reg == 0 and (v & 0x3F) and not (self.chip.regs[3] & 0x80):
                self.last_speech = self.chip.time        # PA (00) is not speech
                self.preparing = False
        elif p == 0x40:
            self.latch40 = v
            self.bank = self.banks[v & 3]
        elif p == 0x21:
            if self.usart_mode_next:
                self.usart_mode_next = False
            else:
                self.usart_cmd = v
                if v & 0x40:                              # internal reset: a mode word follows
                    self.usart_mode_next = True
        elif p == 0x20:
            self.tx.append(v)

    def _serial(self):
        """One byte into the 8251 when it is free and the Accent holds RTS up (its handshake:
        37h raises it, 15h drops it while its buffer is full)."""
        if not self.rx_ready and self.rx and self.usart_cmd & RTS:
            self.rx_byte = self.rx.popleft()
            self.rx_ready = True
            self.cpu.set_rst65(True)

    def _trap(self):
        line = bool(self.chip.request and self.latch40 & 0x10)
        if line and not self.trap_line:
            self.cpu.trap()
        self.trap_line = line

    def _cpu(self, dt):
        # a host feature, not the SA: while it analyses a sentence before its first phoneme
        # the 8085 runs `turbo` times faster (at 3 MHz that is 0.46 s for "Testing one two
        # three.", 0.06 s at 8x).  Speech itself is paced by A/R, so it is unchanged.
        hz = self.cpu_hz * (self.turbo if self.preparing else 1.0)
        self.cpu.run(max(20, int(hz * dt)))
        if self.tick_hz:
            self._tick_acc += dt * self.tick_hz
            while self._tick_acc >= 1.0:
                self._tick_acc -= 1.0
                self.cpu.rst75()

    # ---- the host interface (as hosts.accent.Accent) ---------------------------------------
    def say(self, text, speech=None, background=False):
        """Text down the serial line.  `speech`: whether it is text to be spoken (default: any
        letter or digit outside ESC commands)."""
        data = text.encode("latin-1", "replace")
        if not data:
            return
        if speech is None:
            speech = any(ch.isalnum() for ch in _ESC.sub("", text))
        if speech:
            self.preparing = True
            self.say_time = self.chip.time
        self.rx.extend(data)

    def run(self, seconds, step=0.0005):
        out, t = [], 0.0
        while t < seconds:
            self._serial()
            self._trap()
            before = self.chip.time
            y = self.chip.run_until_request(step) if not self.chip.request else self.chip.run(step / 4)
            out.append(y)
            dt = max(self.chip.time - before, 1e-5)
            self._cpu(dt)
            t += dt
        return self.chip.dsp.concat(out)

    def skip(self, seconds, step=0.002):
        """Run the firmware with the chip keeping time but making no sound."""
        t = 0.0
        while t < seconds - 1e-9:
            self._serial()
            self._trap()
            before = self.chip.time
            self.chip.skip(min(step, seconds - t))
            dt = max(self.chip.time - before, 1e-5)
            self._cpu(dt)
            t += dt
        return t

    @property
    def speaking(self):
        """The firmware's own speech flag: A/R gated onto TRAP while it has phonemes queued."""
        return bool(self.latch40 & 0x10)

    def busy(self, quiet=0.03, patience=1.5):
        if self.rx or self.rx_ready or self.speaking or (self.chip.time - self.last_speech) < quiet:
            return True
        return self.preparing and (self.chip.time - self.say_time) < patience

    def boot(self, limit=4.0, state=None):
        """Power up: the RAM test and set-up (about 0.1 s), then, as for the Accent-mini,
        a carriage return starts speech (ESC =F), Ctrl-X flushes at once (ESC =M), and the
        "Accent ready." greeting is flushed rather than sat through."""
        t = self.skip(0.12)
        self.say("\x1b=F\x1b=M\x18", speech=False)
        t += self.skip(0.02)
        while t < limit and self.busy():
            t += self.skip(0.02)

    def cancel(self, limit=0.6):
        """Ctrl-X: the Accent's flush; then let what was already sent play out silently."""
        self.preparing = False
        self.rx.clear()
        self.say("\x18", speech=False)
        t = self.skip(0.01)
        while t < limit and self.busy():
            t += self.skip(0.01)
        return t

    def state(self):
        c = self.cpu
        return ("pc %04X sp %04X ie %s masks %d latch40 %02X usart %02X rx %d/%s request %s t %.3f "
                "last_speech %.3f preparing %s"
                % (c.pc, c.sp, c.ie, c.masks, self.latch40, self.usart_cmd, len(self.rx), self.rx_ready,
                   self.chip.request, self.chip.time, self.last_speech, self.preparing))


if __name__ == "__main__":
    import sys
    import wave
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from ssi263 import SSI263 as _S
    roms = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "firmware", "aicom-accent-sa")
    text = sys.argv[2] if len(sys.argv) > 2 else "Hello, this is the Accent S A."
    a = AccentSA(roms, chip=_S(dsp="c"))
    a.boot()
    a.say(text + "\r")
    out = [a.run(0.05)]
    while a.busy():
        out.append(a.run(0.05))
    y = a.chip.dsp.concat(out)
    names = a.chip.rom.names
    print("phonemes:", " ".join(names.get(v & 0x3F, "?") for t, r, v in a.writes if r == 0 and v & 0x3F))
    print("%.2f s of audio" % (len(y) / 44100.0))
    with wave.open("accent_sa_test.wav", "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(44100)
        f.writeframes(a.chip.dsp.pcm16(y, 1.0))
