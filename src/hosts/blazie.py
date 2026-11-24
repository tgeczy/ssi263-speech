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
PLAYING_MAX_S = 1.0                # longer than any phoneme the firmware loads (busy())
UNANSWERED_S = 1.0                 # busy()'s host timeout policy: a request given to the firmware and left
                                   # unanswered this long counts as the end (not proof it is, in every state)
KEY_GAP = 10000000                 # instructions between boot keys (1.6 s of unit time)
# Speech-menu letters (BL2000 help, 345-chord menu), as braille key codes: bit n-1 = dot n.
# Punctuation t/m/s/z = total/most/some/none; n toggles digits / full numbers.
MENU = {"punct_total": 0x1E, "punct_most": 0x0D, "punct_some": 0x0E, "punct_none": 0x35,
        "numbers_toggle": 0x1D}
# Status-menu entries (BL2000 help, 34-chord menu: "Voice inflection: i y/n"): the letter that jumps to the
# entry, then y (dots 13456) or n (dots 1345).  The e-chord leaves the menu.
STATUS = {"inflection_on": (0x0A, 0x3D), "inflection_off": (0x0A, 0x1D)}
CREATE_NO_WINDOW = 0x08000000


def boot_keys(menu=(), start=None, gap=None, status=()):
    """The boot chords, with `menu` letters typed inside the 345-chord speech menu before
    the 123456-chord enters speech-box mode.  Returns (--key args, boot instructions).
    `start` (instructions before the first chord) and `gap` (between keys, and after the
    last) default to the MASTER harness's 8M and 10M."""
    if not menu and not status and start is None and gap is None:
        return list(CHORDS), BOOT_INSTR
    start = 8000000 if start is None else int(start)
    gap = KEY_GAP if gap is None else int(gap)
    codes = ([0x4C] + [c for st in status for c in STATUS[st]] + [0x51] if status else [])   # 34-chord .. e-chord
    codes += [0x5C] + [MENU[m] for m in menu] + [0x7F, 0x07, 0x51]
    keys = ["%d=%02X" % (start + i * gap, c) for i, c in enumerate(codes)]
    return keys, start + len(codes) * gap


# ---- the optional idle whine, generated from the chip's own clock (Tomi's unit, blite_sweep W03, 2026-09-27) ----
# The unit's hiss and whine: a comb of lines at n x fc/64 (fc = the chip's filter clock, xck / (2 (256 - R4))), from a
# mod-64 counter that steps 256 - R4 per filter tick (Tomi's unit, sessions W02/W03).  Two classes, as Tomi hears
# them: EVEN volumes (6, the factory volume this host uses) give a slight hiss, ODD volumes the actual whine.
# The frequencies follow the chip's clock and tone register live; each line's LEVEL is measured, per tone: the
# median over the class's volumes of the W03 grid (every volume 1-15 x tone 0-26, 405 cells, idle 0.3-9 s after a
# line), dB re a loud vowel's mean square, kept where the line shows in at least half of them (blite_sweep
# analysis/whine_tone_fit.py).  Blind (fitted without volumes 7 and 13 / 6 and 12, scored on them): audible-line
# error median 2.6 dB for the whine, 5.9 for the hiss, against 20.1 and 14.6 for the 0.6.0 draft's one waveform for
# every tone, which put secondary lines up to ~30 dB too loud and missed the low tones (Tomi: "barely there below
# tone 7").  Phases were not measured: a fixed pseudo-random set (WHINE_SEED).  The chip's own two-sine carrier
# (carrier_rel_db) is switched off while this runs, since the table carries the fc line (n = 64).
# An empirical model of the recorded output, not a traced circuit.
WHINE_TONES = {        # class: per tone 0..26, "n:dB ..." (line n x fc/64, dB re a loud vowel's mean square)
    "hiss": (
        "32:-89.1 64:-71.3",   # tone 0
        "2:-55.4 8:-66.3 9:-84.1 14:-89.3 15:-92.2 16:-76.7 17:-91.4 18:-93.5 22:-92.8 23:-95.6 24:-79.5 25:-93.3 26:-95 30:-95.8 31:-97.8 32:-85 33:-96.7 40:-87.5 48:-92.2 64:-71.7",   # tone 1
        "4:-63.7 12:-87.7 16:-80.5 20:-93.3 30:-97.1 32:-93.9 64:-71.5",   # tone 2
        "6:-69 8:-73.9 16:-83.3 18:-96.8 24:-83.8 26:-88.9 29:-97.1 32:-85.2 40:-91.8 64:-71",   # tone 3
        "8:-72.9 24:-89.6 28:-93.3 32:-86.8 43:-90.6 64:-70.6",   # tone 4
        "8:-69.9 10:-75.7 16:-82.2 24:-83.5 27:-95.6 32:-87.3 38:-92.5 64:-70.5",   # tone 5
        "10:-85.7 12:-77.5 16:-78.2 26:-94.9 32:-93.8 64:-70.5",   # tone 6
        "8:-67.7 14:-81.3 16:-80.3 22:-93.7 24:-81.9 25:-95.5 32:-86.1 48:-91.9 64:-70",   # tone 7
        "8:-80.8 16:-81.8 24:-94.2 64:-70.1",   # tone 8
        "5:-75.6 8:-76.1 10:-86.9 14:-89.3 16:-81.8 18:-85.5 22:-95.7 23:-94.8 24:-85.4 32:-86.8 40:-88.7 53:-90.5 64:-69.8",   # tone 9
        "2:-61.7 16:-79.7 20:-85.9 22:-93.9 32:-92.6 64:-69.8",   # tone 10
        "1:-53.6 8:-79.1 16:-89.4 21:-93.3 22:-87.9 32:-89.6 64:-69.1",   # tone 11
        "4:-68.4 8:-78.1 20:-87.8 24:-85.2 32:-85.3 64:-68.9",   # tone 12
        "2:-66.1 7:-77.7 8:-75.9 14:-92.1 16:-83.6 19:-92.1 24:-88.9 26:-91.4 32:-90.8 64:-68.6",   # tone 13
        "10:-80.4 12:-88.1 16:-83.6 18:-89.1 28:-91.8 64:-68",   # tone 14
        "8:-73.2 13:-86 17:-91.7 24:-85.9 32:-88.8 37:-83.6 64:-67.9",   # tone 15
        "16:-90.8 32:-90.8 64:-67.4",   # tone 16
        "4:-77.4 8:-76.3 11:-87.5 15:-90.1 16:-87.1 19:-92.9 64:-67.5",   # tone 17
        "6:-79.5 8:-85.2 14:-88.2 16:-81 64:-67.1",   # tone 18
        "1:-56 5:-82.5 8:-84.9 13:-89 14:-91.2 16:-89.3 64:-66.6",   # tone 19
        "4:-74.2 8:-83.1 12:-90.5 32:-79.4 64:-66.7",   # tone 20
        "2:-65.9 8:-74 9:-89.8 10:-89.8 11:-87.6 16:-80.7 24:-79.7",   # tone 21
        "4:-76.3 10:-87.9 16:-82.4 20:-88.1 32:-84.7",   # tone 22
        "1:-62.9 6:-87 8:-71.2 9:-86.7 10:-87.8 16:-82 24:-83.2",   # tone 23
        "8:-88 16:-85",   # tone 24
        "6:-84.5 7:-85.9 8:-84.7",   # tone 25
        "2:-74.2 4:-79 6:-84.4",   # tone 26
    ),
    "whine": (
        "32:-91.2 64:-71.8",   # tone 0
        "2:-55.4 6:-71.2 8:-58 9:-76 10:-68.2 11:-69.9 12:-82.9 14:-76.7 15:-79.5 16:-74.2 18:-88.8 22:-92.6 23:-95.7 24:-86.5 26:-93.8 30:-94.7 32:-87.3 33:-96.2 34:-96.2 40:-85.9 48:-92.5 56:-87.8 64:-71.3",   # tone 1
        "4:-63.2 6:-66.7 8:-65.8 10:-72.6 12:-72.4 14:-80.2 16:-77.3 18:-90.7 20:-90.2 30:-93 32:-91.8 48:-88.4 64:-71.2",   # tone 2
        "6:-60.7 7:-77.3 8:-59.2 9:-76.5 10:-69.9 11:-70.7 13:-84.1 14:-81.7 15:-81.3 16:-76.2 17:-93.7 18:-94.4 24:-89.7 26:-89.7 29:-98.4 32:-87.6 40:-91.7 56:-90.8 64:-70.7",   # tone 3
        "8:-55.9 16:-89.5 20:-98.5 24:-93.4 28:-95.5 32:-88.4 43:-90.3 56:-88.8 64:-71",   # tone 4
        "6:-68.3 7:-74.8 8:-53.6 9:-78.4 10:-61.9 11:-70.5 13:-85.5 14:-78.8 15:-77.6 16:-71.5 18:-88.1 19:-91.3 21:-92.7 22:-90.6 24:-80.1 27:-95.5 30:-94.4 32:-85.9 38:-90.8 40:-83.9 48:-87.3 54:-90.7 56:-85.9 64:-70.6",   # tone 5
        "6:-66.7 8:-61.5 10:-72.5 12:-64 14:-81.7 16:-71.1 26:-95.3 32:-87.2 48:-89.4 52:-89.1 64:-70.5",   # tone 6
        "5:-79.6 6:-69.6 8:-51.7 9:-77.2 10:-72.9 11:-71.9 12:-83.7 13:-85.8 14:-66.8 15:-78.7 16:-77.7 18:-91.3 22:-96.2 24:-88.7 25:-94.8 32:-86.3 48:-87.4 50:-88.8 56:-84.9 64:-70.5",   # tone 7
        "8:-59.2 16:-79.8 24:-90.7 56:-89.4 64:-69.8",   # tone 8
        "5:-76.4 6:-67.4 7:-78.3 8:-53.3 9:-76 10:-72.1 11:-69.2 13:-86.1 14:-80.2 15:-78.1 16:-75.5 18:-90.6 23:-92.6 24:-87.2 32:-89.9 53:-89.2 56:-86.5 64:-69.5",   # tone 9
        "2:-60.5 8:-66.9 10:-75.2 12:-78.3 14:-85.9 16:-81.9 18:-97.1 20:-88.3 22:-92.2 32:-84.2 64:-69.5",   # tone 10
        "1:-52.6 6:-70 7:-80.3 8:-59.6 9:-73.3 10:-72 11:-73.5 13:-85.8 14:-89.2 15:-81.9 16:-74.5 17:-91.5 19:-89 21:-93.2 22:-90.6 24:-76.7 32:-87.7 40:-85.1 64:-69.2",   # tone 11
        "4:-67 8:-59.8 12:-77.8 16:-86.2 20:-91.6 24:-90 32:-85.3 64:-68.8",   # tone 12
        "2:-65.4 6:-69.5 7:-76 8:-54.6 9:-76 10:-71 11:-74.6 12:-79.6 13:-88.7 14:-79.8 15:-80.6 16:-75.3 19:-91.6 24:-86.6 26:-91.9 32:-93 56:-87.3 64:-68.5",   # tone 13
        "6:-76 8:-65.5 10:-79.9 12:-77.2 16:-82.8 18:-89.7 28:-90 64:-68.1",   # tone 14
        "5:-77.9 6:-70.5 8:-55.1 9:-77.7 10:-71.4 11:-71.6 13:-79.8 14:-79.6 15:-81.4 16:-75.7 17:-91.5 18:-91.7 24:-79.4 32:-85.4 37:-86.6 40:-86.8 64:-68",   # tone 15
        "16:-81.5 64:-67.6",   # tone 16
        "3:-75.3 6:-68.8 7:-75.1 8:-63.9 9:-75.3 10:-74.6 11:-68.5 12:-78.7 14:-80.8 15:-82.7 16:-73.8 17:-89.7 18:-91.4 19:-91.2 24:-81 32:-85.6 40:-87.7 48:-86.4 64:-67.3",   # tone 17
        "6:-76.1 8:-62.2 10:-74.9 12:-80.5 14:-87 16:-79.5 64:-66.8",   # tone 18
        "1:-56.1 5:-80.3 6:-71.5 7:-79.3 8:-61 9:-75.6 10:-76.2 11:-71.3 12:-73.9 13:-86.3 14:-80.8 15:-81.7 16:-78.3 64:-66.7",   # tone 19
        "4:-76.4 8:-66.7 12:-83 16:-84.6 24:-88.3 64:-66.8",   # tone 20
        "2:-64.2 5:-78.6 6:-69.5 7:-80.4 8:-55 9:-73.7 10:-74.8 11:-69.6 12:-84.1 13:-88.3 14:-78.9 15:-80.8 16:-76.9 22:-89.3 56:-84.7",   # tone 21
        "4:-76.1 8:-67.2 10:-75.1 12:-83.3 14:-83.2 16:-81.8 20:-86.3 32:-84.3",   # tone 22
        "1:-64.8 5:-84.4 6:-78.2 7:-87.2 8:-54.1 9:-74.6 10:-74.2 11:-77.2 12:-81.2 13:-89 14:-83.6 16:-79.4 24:-83",   # tone 23
        "8:-67.7 16:-81.1",   # tone 24
        "4:-85 5:-85.2 6:-70.1 7:-73.9 8:-57.2 9:-78.8 10:-72 11:-80.2 12:-86.6 14:-81.5 15:-83.4 16:-79.5 24:-83.8",   # tone 25
        "2:-73.7 4:-79 6:-78.8 8:-69.4 10:-70.6 12:-80.4 14:-83.4 16:-76.6",   # tone 26
    ),
}
WHINE_VOWEL_RMS = 0.0426                 # this host's loud vowel at unit volume 6, after the board pole
WHINE_TABLE = 1024
WHINE_SEED = 392


def whine_wave(r4, out_rate, mode="whine", xck=1e6):
    """One 64-tick period of the hiss or whine at tone register r4, band-limited under 0.45 x out_rate: (fc, table).
    Tones past 26 (R4 FB-FF, not offered) use tone 26's levels."""
    import math
    n_div = 256 - r4
    if n_div <= 0:
        return None, None
    fc = xck / (2.0 * n_div)
    tone = min(26, max(0, 32 - n_div))
    levels = dict((int(n), float(db)) for n, db in (p.split(":") for p in WHINE_TONES[mode][tone].split()))
    seed, lines = WHINE_SEED, []
    for n in range(1, 65):
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        if n in levels and n * fc / 64.0 < 0.45 * out_rate:
            lines.append((n, math.sqrt(2.0) * 10 ** (levels[n] / 20.0) * WHINE_VOWEL_RMS, 2 * math.pi * seed / 0x7FFFFFFF))
    tab = [sum(a * math.cos(2 * math.pi * n * j / WHINE_TABLE + p) for n, a, p in lines) for j in range(WHINE_TABLE)]
    return fc, tab


class Blazie:
    def __init__(self, exe, firmware, state, chip=None, out_rate=44100, menu=(), key_start=None, key_gap=None,
                 board_lowpass_hz=None, status=()):
        """`board_lowpass_hz`: a roll-off after the chip that matches the unit's line out (None: off).
        The unit's line out has ~3 dB less at 4-8 kHz and ~12 dB less at 8-16 kHz than the chip
        model; a first-order 5 kHz low-pass matches it (octave error 4.6 -> 1.2 dB on Reclaim's
        first sentence; Tomi's ear, 2026-09-27).  Fitted, not traced: it may compensate for the
        board's output stage, the headphone socket, the recording chain or chip-model error (the Artic sample
        schematic's AO network corners near 224 Hz, so it is no support).  Not the chip model."""
        self.chip = chip or SSI263(out_rate=out_rate)
        self.board = self.chip.dsp.onepole(board_lowpass_hz, self.chip.out_rate) if board_lowpass_hz else None
        self.whine = None            # None, "hiss" or "whine" (whine_wave); the driver sets it
        self.encoding = "latin-1"    # how say() sends text: the Spanish firmware reads DOS code page 850
        # cancel(): the emulated time between ^X cuts.  Each cut costs pipe round trips: 20 ms cuts (0.5.0) made
        # the cancel ~32 ms of a ~44 ms tab-to-speech; 100 ms gives ~20 ms, no tail left in 24 cancel points
        # (nvda/tools/cut_test.py; 150 and 200 ms are slower again)
        self.cancel_cut = 0.1
        # cancel(): how long nothing may load before the unit counts as silent.  0.15 s until 0.6.0's draft; 0.08 s
        # needs one cut instead of two (tab ~15 -> ~11 ms) and left no tail in 72 cancel points: NVDA rates 0, 50
        # and 100, a line with four commas, cancels 0.15-3.5 s in (nvda/tools/cut_test.py and its slow-rate run)
        self.cancel_quiet = 0.08
        self._whine_key, self._whine_fc, self._whine_tab, self._whine_ph = None, None, None, 0.0
        args = [exe, firmware, "--live", "--state-in", state, "--phon-ms", "5"]
        keys, boot_instr = boot_keys(menu, key_start, key_gap, status)
        for c in keys:
            args += ["--key", c]
        self.proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, text=True, bufsize=1,
                                     creationflags=CREATE_NO_WINDOW if os.name == "nt" else 0,
                                     cwd=os.path.dirname(os.path.abspath(exe)))
        self.ar = None
        self.ar_time = -1.0
        self.tx = []               # bytes the unit sent back (XON/XOFF, ^F markers)
        # ^F bookkeeping.  The unit echoes each ^F in speaking order, but it holds the
        # last line it received (an empty flush line, CR ^F) until the next arrives, so
        # when everything sent has been spoken, exactly one ^F is still owed.
        self.sent_f = 0
        self.echo_f = 0
        self.echoes = []           # the last run()'s ^F echoes: (its number -- echo_f with it counted --, the samples
        self._made = 0             # of that run's audio made when it came): the unit's own index marks (bl_voice.h)
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
        self.last_load = -1.0
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
                if reg == 0 and not (self.chip.regs[3] & 0x80):
                    self.last_load = self.chip.time          # any phoneme, PA included (busy())
                if reg == 0 and not (self.chip.regs[3] & 0x80) and (val & 0x3F):
                    self.last_speech = self.chip.time        # PA (code 00) is not speech
                    self.preparing = False
            elif r.startswith("T "):
                b = int(r.split()[1], 16)
                self.tx.append(b)
                if b == 0x06:
                    self.echo_f += 1
                    self.echoes.append((self.echo_f, self._made))
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
        self.send(b"".join(ln.encode(self.encoding, "replace") + b"\r\x06" for ln in lines) + b"\r\x06")

    def owed(self):
        """^F echoes still to come beyond the one the unit holds (after a cancel it
        holds none until the next line, so this can read -1: nothing owed)."""
        return self.sent_f - 1 - self.echo_f

    def busy(self, quiet=0.1, patience=3.0):
        """Still speaking: a phoneme within `quiet` s, or a line's ^F echo still owed.
        `patience` bounds only SILENT waiting for that echo, counted from the last phoneme
        loaded (PA included: since 0.9.5) or echo.  Counted from the last echo alone (0.3.4 and before), one line longer
        than it -- 8.4 s, a spelled-out Mastodon handle -- ended the utterance just before
        its echo, and the unit's remaining lines came out only with the next utterance."""
        if (self.chip.time - self.last_speech) < quiet:
            return True
        # The firmware is still inside an utterance while the chip plays a phoneme it loaded: in a pause it loads PA
        # after PA, each within a few ms of the chip's A/R request; at the end it leaves A/R unanswered.  So "done"
        # waits for that, whatever the ^F count says -- a count one echo short after some cancels (complete_fuzz
        # seed 4, investigation of 2026-09-29) ended "One, two, three, four." at its comma.  Bounded by
        # PLAYING_MAX_S: a chip the firmware has switched off is not playing.  Only while speech has loaded since
        # the last ^F echo: at a real end the last echo follows all the speech, and the PA the firmware loads after
        # it must not delay "done" (121 ms an utterance, measured); a miscounted echo comes before speech that follows.
        if self.last_speech > self.say_time:
            if not self.chip.request and (self.chip.time - self.last_load) < PLAYING_MAX_S:
                return True
            # ... and while the chip's request has not reached the firmware, or reached it and is not answered yet:
            # only a request given to the firmware and left unanswered for UNANSWERED_S counts as an end -- a host
            # timeout policy, not proof the firmware is done in every state (Astra, Replies 100-101:
            # a request raised at the end of a run() call, not yet forwarded, passed for one; and a line break's next
            # segment can take a while to translate).  The normal end never gets here (no speech since the last echo).
            if self.chip.request and not (self.ar and self.last_load < self.ar_time
                                          and self.chip.time - self.ar_time >= UNANSWERED_S):
                return True
        # patience counts from the last phoneme LOADED, PA included: a unit loading pause after pause is at work.  From
        # the last speech, a run of lone periods at rate 0 (each a long pause, no speech) ran out of it with lines owed:
        # done was said and the unit kept the rest (0.9.5, tools/check_marks_alone.py).  BL_PATIENCE_BREAK=1: as it was
        last = self.last_speech if os.environ.get("BL_PATIENCE_BREAK") else self.last_load
        return self.owed() > 0 and (self.chip.time - max(self.say_time, last)) < patience

    def cancel(self, limit=3.0, quiet=None, cut=None):
        """Silence and flush.  ^X cuts the word being spoken and flushes the unit's
        input, held flush line and its ^F included; the unit may still move on to a
        word it had already prepared, so keep cutting (every `cancel_cut` s), audio discarded,
        until nothing has loaded for `quiet` s.  Afterwards nothing is held, so every
        ^F sent counts as answered -- unless every line had already echoed: then the unit
        holds its flush line, ^X leaves it held (measured), and its ^F comes with the next
        text.  Counted as answered (0.5.0), that ^F was taken for the next line's own
        echo, and the next utterance ended at its first word gap longer than `quiet`,
        the rest held until the one after (a tester: at NVDA's slower rates, "the last
        syllable is cut off, then joined to the next utterance").  Returns the emulated
        seconds it took."""
        self._cmd("D")                          # drop what the unit has not taken yet
        # every line the unit HAS taken echoed: its flush line is held.  Decided after the drop: before it (0.6.0),
        # text queued behind a finished line still counted, so a cancel then took the held flush line's ^F as
        # answered, the next utterance took that ^F for its own and ended at its first pause, the rest held until
        # the one after -- and the count stayed one ahead from then on (a tester, 0.6.0: "if you use it for a
        # certain amount of time you lose speech midway ... waits for the next utterance"; complete_fuzz.py)
        holding = self.owed() == 0
        self.preparing = False
        t = 0.0
        cut = self.cancel_cut if cut is None else cut
        quiet = self.cancel_quiet if quiet is None else quiet
        while t < limit:
            self._cmd("U 18")
            t += self.skip(cut)
            # a line still being prepared ignores ^X and speaks afterwards: until its ^F is
            # back (or ^X flushed it), keep cutting for up to 1 s, the longest measured wait
            if (t >= 0.04 and self.chip.time - self.last_speech > quiet
                    and (self.owed() <= 0 or t >= 1.0)):
                break
        self.echo_f = self.sent_f - 1 if holding else self.sent_f
        return t

    def skip(self, seconds):
        """Fast-forward with no sound: the chip only keeps time and A/R."""
        t = 0.0
        while t < seconds - 1e-9:
            if self.chip.request != self.ar:
                self.ar = self.chip.request
                if self.ar:
                    self.ar_time = self.chip.time    # when the firmware was given the request (busy())
                self._cmd("A %d" % (1 if self.ar else 0))
            before = self.chip.time
            self.chip.skip(seconds - t)
            dt = max(self.chip.time - before, 1e-5)
            self._cmd("R %d" % max(1, int(CLOCK_HZ * dt)))
            t += dt
        # skipped output is discarded, as chip.skip lets the chip's own ringing die: the board pole forgets the last
        # sample it was given too, or that sample comes out at the head of the next block (after a stop: a blip
        # v0.16's F1 lifted over the lead threshold, so the silence before the next speech went untrimmed, 76 ms)
        if self.board and t > 0.0:              # a skip of no time discards nothing (Astra, Reply 200)
            self.board.reset()
        return t

    def run(self, seconds, step=0.0005):
        """`step` is the lockstep grain: chip and CPU trade A/R and writes every step.  While
        the unit silently reads a line (`preparing`), `prep_step` may be coarser -- each step
        is one pipe round trip, and at a high turbo those dominate (tools/latency_check.py)."""
        out, t = [], 0.0
        self.echoes, self._made = [], 0
        while t < seconds:
            if self.chip.request != self.ar:
                self.ar = self.chip.request
                if self.ar:
                    self.ar_time = self.chip.time    # when the firmware was given the request (busy())
                self._cmd("A %d" % (1 if self.ar else 0))
            before = self.chip.time
            st = self.prep_step if (self.preparing and self.prep_step) else step
            y = self.chip.run_until_request(st) if not self.chip.request else self.chip.run(st / 4)
            out.append(y)
            self._made += len(y)
            dt = max(self.chip.time - before, 1e-5)
            speed = self.turbo if self.preparing else 1.0
            self._cmd("R %d" % max(1, int(CLOCK_HZ * dt * speed)))
            t += dt
        y = self.chip.dsp.concat(out)
        y = self.board.process(y) if self.board else y
        return self._add_whine(y) if self.whine else y

    def _add_whine(self, y):
        """The whine under this block, phase-continuous; silent while the chip is powered down or clicked off
        (R3 = 00: on the unit everything, whine included, stops there)."""
        regs = self.chip.regs
        rate = self.chip.out_rate
        key = (regs[4], rate, self.whine)
        if key != self._whine_key:
            self._whine_key = key
            self._whine_fc, self._whine_tab = whine_wave(regs[4], rate, self.whine)
        if self._whine_tab is None or not len(y):
            return y
        inc = self._whine_fc / 64.0 / rate
        ph = self._whine_ph
        if (regs[3] & 0x80) or not (regs[3] & 0x70):
            self._whine_ph = (ph + inc * len(y)) % 1.0
            return y
        tab, size = self._whine_tab, WHINE_TABLE
        out = self.chip.dsp.concat([y])
        for i in range(len(out)):
            pos = ph * size
            j = int(pos)
            fr = pos - j
            out[i] += tab[j % size] * (1.0 - fr) + tab[(j + 1) % size] * fr
            ph += inc
            if ph >= 1.0:
                ph -= 1.0
        self._whine_ph = ph
        return out
