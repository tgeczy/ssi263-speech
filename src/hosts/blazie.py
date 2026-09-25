"""Blazie firmware (Braille Lite 2000, June 2003 BL2ENG) driving the engine's SSI-263.

The firmware runs in z180emu (`bns_live.exe --live`, GPLv2) as a child process; this
host runs the SSI-263 model and closes the loop: the unit's register writes arrive
as "W reg val" lines and are applied to the chip at the current chip time, and the
chip's A/R request goes back as "A 1" / "A 0", so the firmware's inflection tokens
and phoneme timing happen live instead of being replayed from a batch.

Neither the firmware nor the RAM snapshot (which contains it) is part of any
repository: pass your own paths.  Boot = the warm snapshot plus the speech-box key
chords (345, 123456, L, e), exactly as the MASTER harness did.
"""
import os
import subprocess
import sys

# Packaged (the NVDA add-ons: synthDrivers._ssi263_<name>), the engine is imported relatively,
# never from sys.path: both add-ons ship an `ssi263`, and NVDA keeps one module per name for
# the whole process (0.3.0: Braille Lite imported the older Speak-Out's copy and failed).
try:
    from .ssi263 import SSI263
except ImportError:                       # the research tree: src/ on sys.path
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from ssi263 import SSI263  # noqa: E402

CLOCK_HZ = 6144000.0
CHORDS = ["8000000=5C", "18000000=7F", "28000000=07", "38000000=51"]   # 345, 123456, L, e
BOOT_INSTR = 48000000
KEY_GAP = 10000000                 # instructions between boot keys (1.6 s of unit time)
# Speech-menu letters (BL2000 help, 345-chord menu), as braille key codes: bit n-1 = dot n.
# Punctuation t/m/s/z = total/most/some/none; n toggles digits / full numbers.
MENU = {"punct_total": 0x1E, "punct_most": 0x0D, "punct_some": 0x0E, "punct_none": 0x35,
        "numbers_toggle": 0x1D}
CREATE_NO_WINDOW = 0x08000000


def boot_keys(menu=(), start=None, gap=None):
    """The boot chords, with `menu` letters typed inside the 345-chord speech menu before
    the 123456-chord enters speech-box mode.  Returns (--key args, boot instructions).
    `start` (instructions before the first chord) and `gap` (between keys, and after the
    last) default to the MASTER harness's 8M and 10M."""
    if not menu and start is None and gap is None:
        return list(CHORDS), BOOT_INSTR
    start = 8000000 if start is None else int(start)
    gap = KEY_GAP if gap is None else int(gap)
    codes = [0x5C] + [MENU[m] for m in menu] + [0x7F, 0x07, 0x51]
    keys = ["%d=%02X" % (start + i * gap, c) for i, c in enumerate(codes)]
    return keys, start + len(codes) * gap


class Blazie:
    def __init__(self, exe, firmware, state, chip=None, out_rate=44100, menu=(), key_start=None, key_gap=None):
        self.chip = chip or SSI263(out_rate=out_rate)
        args = [exe, firmware, "--live", "--state-in", state, "--phon-ms", "5"]
        keys, boot_instr = boot_keys(menu, key_start, key_gap)
        for c in keys:
            args += ["--key", c]
        self.proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, text=True, bufsize=1,
                                     creationflags=CREATE_NO_WINDOW if os.name == "nt" else 0,
                                     cwd=os.path.dirname(os.path.abspath(exe)))
        self.ar = None
        self.tx = []               # bytes the unit sent back (XON/XOFF, ^F markers)
        # ^F bookkeeping.  The unit echoes each ^F in speaking order, but it holds the
        # last line it received (an empty flush line, CR ^F) until the next arrives, so
        # when everything sent has been spoken, exactly one ^F is still owed.
        self.sent_f = 0
        self.echo_f = 0
        self.say_time = 0.0
        self.preparing = False     # a line sent and no phoneme of it spoken yet
        self.turbo = 4.0           # CPU speed-up while preparing (the unit reads a whole line first)
        # also re-arm the turbo while the unit reads each NEXT line of a multi-line send.
        # Off = the unit's own pace: CPU speed is audible in its timing (a line at x4 is 10%
        # shorter, measured), so this is a "shorter pauses" option, not a transparent one.
        self.turbo_between_lines = False
        self.prep_step = None      # a coarser lockstep while preparing (None: the run() step)
        self._stale_f = 0          # ^F echoes owed by earlier say()s (the held flush line)
        self.last_speech = -1.0
        self.feed_chip = False     # during boot, writes set chip state but make no audio
        self._cmd("B %d" % boot_instr)
        self._cmd("LIVE")
        self.feed_chip = True

    # ---- protocol ------------------------------------------------------------------
    def _cmd(self, line):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()
        while True:
            r = self.proc.stdout.readline()
            if not r:
                raise RuntimeError("z180emu exited")
            if r.startswith("OK"):
                return
            if r.startswith("W "):
                _, reg, val = r.split()
                reg, val = int(reg), int(val, 16)
                self.chip.write(reg, val)
                if reg == 0 and not (self.chip.regs[3] & 0x80) and (val & 0x3F):
                    self.last_speech = self.chip.time        # PA (code 00) is not speech
                    self.preparing = False
            elif r.startswith("T "):
                b = int(r.split()[1], 16)
                self.tx.append(b)
                if b == 0x06:
                    self.echo_f += 1
                    self.say_time = self.chip.time           # progress: patience counts from here
                    if self._stale_f > 0:
                        # the held flush line of an EARLIER say(), answered now that new text
                        # came -- often after this line's first phoneme, so it must not re-arm
                        self._stale_f -= 1
                    elif self.turbo_between_lines and self.owed() > 0:
                        self.preparing = True                # a line is done; the next is being read
            elif r.startswith("DROPPED "):
                self.sent_f -= int(r.split()[1])
            # anything else is the emulator core's own chatter

    def close(self):
        try:
            self._cmd("Q")
        except Exception:
            pass
        try:
            self.proc.kill()
        except Exception:
            pass

    # ---- host ----------------------------------------------------------------------
    def send(self, data):
        if isinstance(data, str):
            data = data.encode("latin-1", "replace")
        if data:
            self.sent_f += data.count(0x06)
            self._cmd("S " + data.hex())

    def say(self, text):
        """A line (or a list of lines, sent together so the unit moves from one to the next
        by itself), each ended CR ^F, then the flush the unit needs: it speaks a line when
        the next arrives.  Each ^F echo comes once its line's last phoneme is sent; the
        flush line's only when a later transmission arrives, so that one is not waited for."""
        lines = [text] if isinstance(text, str) else list(text)
        self.say_time = self.chip.time
        self.preparing = True
        self._stale_f = max(0, self.sent_f - self.echo_f)     # echoes still due from earlier sends
        self.send(b"".join(ln.encode("latin-1", "replace") + b"\r\x06" for ln in lines) + b"\r\x06")

    def owed(self):
        """^F echoes still to come beyond the one the unit holds (after a cancel it
        holds none until the next line, so this can read -1: nothing owed)."""
        return self.sent_f - 1 - self.echo_f

    def busy(self, quiet=0.1, patience=3.0):
        """Still speaking: a phoneme within `quiet` s, or a line's ^F echo still owed.
        `patience` bounds only SILENT waiting for that echo, counted from the last speech
        or echo.  Counted from the last echo alone (0.3.4 and before), one line longer
        than it -- 8.4 s, a spelled-out Mastodon handle -- ended the utterance just before
        its echo, and the unit's remaining lines came out only with the next utterance."""
        if (self.chip.time - self.last_speech) < quiet:
            return True
        return self.owed() > 0 and (self.chip.time - max(self.say_time, self.last_speech)) < patience

    def cancel(self, limit=3.0, quiet=0.15):
        """Silence and flush.  ^X cuts the word being spoken and flushes the unit's
        input, held flush line and its ^F included; the unit may still move on to a
        word it had already prepared, so keep cutting every 20 ms, audio discarded,
        until nothing has loaded for `quiet` s.  Afterwards nothing is held, so every
        ^F sent counts as answered.  Returns the emulated seconds it took."""
        self._cmd("D")                          # drop what the unit has not taken yet
        self.preparing = False
        t = 0.0
        while t < limit:
            self._cmd("U 18")
            t += self.skip(0.02)
            # a line still being prepared ignores ^X and speaks afterwards: until its ^F is
            # back (or ^X flushed it), keep cutting for up to 1 s, the longest measured wait
            if (t >= 0.04 and self.chip.time - self.last_speech > quiet
                    and (self.owed() <= 0 or t >= 1.0)):
                break
        self.echo_f = self.sent_f
        return t

    def skip(self, seconds):
        """Fast-forward with no sound: the chip only keeps time and A/R."""
        t = 0.0
        while t < seconds - 1e-9:
            if self.chip.request != self.ar:
                self.ar = self.chip.request
                self._cmd("A %d" % (1 if self.ar else 0))
            before = self.chip.time
            self.chip.skip(seconds - t)
            dt = max(self.chip.time - before, 1e-5)
            self._cmd("R %d" % max(1, int(CLOCK_HZ * dt)))
            t += dt
        return t

    def run(self, seconds, step=0.0005):
        """`step` is the lockstep grain: chip and CPU trade A/R and writes every step.  While
        the unit silently reads a line (`preparing`), `prep_step` may be coarser -- each step
        is one pipe round trip, and at a high turbo those dominate (tools/latency_check.py)."""
        out, t = [], 0.0
        while t < seconds:
            if self.chip.request != self.ar:
                self.ar = self.chip.request
                self._cmd("A %d" % (1 if self.ar else 0))
            before = self.chip.time
            st = self.prep_step if (self.preparing and self.prep_step) else step
            y = self.chip.run_until_request(st) if not self.chip.request else self.chip.run(st / 4)
            out.append(y)
            dt = max(self.chip.time - before, 1e-5)
            speed = self.turbo if self.preparing else 1.0
            self._cmd("R %d" % max(1, int(CLOCK_HZ * dt * speed)))
            t += dt
        return self.chip.dsp.concat(out)
