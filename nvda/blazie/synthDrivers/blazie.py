# -*- coding: utf-8 -*-
"""NVDA synthesizer driver: a Blazie Braille Lite 2000 (June 2003 firmware) in speech-box
mode, talking through an emulated SSI-263.

The unit's own firmware runs in z180emu (bns_live.exe, GPLv2) as a child process; its
register writes drive a register-level SSI-263 model here, and the model's A/R request
drives the firmware back, so its rules, number reading and inflection are the
originals, live.  Nothing is recorded or concatenated.

This add-on carries the Braille Lite's firmware, shared with permission; it is not ours.
"""

import os
import queue
import re
import sys
import threading

import nvwave
from synthDriverHandler import SynthDriver, VoiceInfo, synthIndexReached, synthDoneSpeaking
from autoSettingsUtils.utils import StringParameterInfo
from autoSettingsUtils.driverSetting import BooleanDriverSetting
from logHandler import log
import speech.commands

_HERE = os.path.dirname(__file__)
_ENGINE_DIR = os.path.join(_HERE, "_ssi263_blazie")

# The engine is this add-on's own package, imported relatively and never through sys.path:
# the Speak-Out add-on ships an `ssi263` too, and one process has one module per name.
from ._ssi263_blazie.blazie_host import Blazie
from ._ssi263_blazie import ssi263_numwords as numwords
from ._ssi263_blazie.ssi263.native import SSI263C      # the chip in C

OUT_RATE = 44100
BLOCK_S = 0.03
EXE = os.path.join(_ENGINE_DIR, "bns_live.exe")
FIRMWARE = os.path.join(_ENGINE_DIR, "BL2ENG.BNS")
STATE = os.path.join(_ENGINE_DIR, "bl2_2003_warm.state")
UNIT_VOLUME = 6         # the unit's factory volume; NVDA's slider is applied digitally
MAKEUP = 2.0            # +6 dB so volume 6 sits at a normal level
# Factory settings after a warm reset (MASTER, confirmed on tape): rate 11, tone 7, and
# r1 = 45h (81.4 Hz, the pitch of Tomi's Braille 'n Speak 2000 recording).  NVDA's slider
# midpoints map onto them, and nothing is sent until a slider moves.
DEFAULT_RATE, DEFAULT_PITCH, DEFAULT_TONE = 11, 16, 7
# The firmware takes ^E n E modulo 16 (measured on the unit, and the same emulated): rate 16
# speaks at rate 10's speed, so the fastest rate is 15.
MAX_RATE = 15


def _clean(text):
    out = []
    for ch in text:
        o = ord(ch)
        if o < 32 or o == 127:
            out.append(" ")
        elif o < 128:
            out.append(ch)
        else:
            out.append({"‘": "'", "’": "'", "“": '"', "”": '"',
                        "–": "-", "—": "-", "…": "..."}.get(ch, " "))
    return "".join(out)


_SENTENCE = re.compile(r"(?<=[.!?])\s+")


def _lines(text, max_words=14, pack=True):
    """The unit reads a whole line before it speaks (0.7 s for 14 words, measured) and
    ends every line with its own pause, so: whole sentences, packed together up to
    max_words when `pack`, from the second line on (its in-line sentence pause is ~200 ms
    against ~420 ms for a line break, measured), and long sentences split at commas.  Never at a colon or
    semicolon: "Synthesizer:" + "combo box" as two lines was the second-long gap.
    Sentence ends keep their mark: the firmware's inflection hangs on it."""
    pieces = []
    for sent in _SENTENCE.split(text):
        words = sent.split()
        if len(words) <= max_words:
            if words:
                pieces.append(" ".join(words))
            continue
        cur = []
        for w in words:
            cur.append(w)
            if (w.endswith(",") and len(cur) >= 4) or len(cur) >= max_words:
                pieces.append(" ".join(cur))
                cur = []
        if cur:
            pieces.append(" ".join(cur))
    if not pack:
        return pieces
    out = []
    for piece in pieces:
        # the first line stays as it is: packing onto it delays the first word (+65 ms)
        if len(out) > 1 and len(out[-1].split()) + len(piece.split()) <= max_words:
            out[-1] += " " + piece
        else:
            out.append(piece)
    return out


LEAD_THRESHOLD = 0.003      # chip output; the idle carrier is ~1e-4, speech ~0.1-0.7
LEAD_PREROLL = 220          # samples (5 ms) kept before the first sound


def _trim_lead(y):
    """Drop the silence at the head of an utterance: the unit reading its line and a stop's
    closure are silent, and a listener hears them only as delay.  Returns (audio, found)."""
    for k, v in enumerate(y):
        if v > LEAD_THRESHOLD or v < -LEAD_THRESHOLD:
            return y[max(0, k - LEAD_PREROLL):], True
    return y[:0], False


def _joined(items):
    """NVDA often sends one item in pieces ("select synthesizer", "dialog").  Each piece
    sent as its own line gets the unit's end-of-line pause; joined, they flow."""
    out = []
    for kind, value in items:
        if kind == "text" and out and out[-1][0] == "text":
            out[-1] = ("text", out[-1][1].rstrip() + " " + value.lstrip())
        else:
            out.append((kind, value))
    return out


class SynthDriver(SynthDriver):
    name = "blazie"
    description = "Braille Lite / Braille 'n Speak (SSI-263 emulation)"

    supportedSettings = (
        SynthDriver.VoiceSetting(),
        SynthDriver.VariantSetting(),
        SynthDriver.RateSetting(),
        SynthDriver.PitchSetting(),
        SynthDriver.VolumeSetting(),
        BooleanDriverSetting("joinPhrases", "&Join phrases (fewer pauses between words)", defaultVal=True),
        BooleanDriverSetting("shortPauses", "S&horten pauses between sentences", defaultVal=True),
        BooleanDriverSetting("numberWords", "Custom n&umber processing (fix digits above a trillion)", defaultVal=True),
    )
    supportedCommands = {speech.commands.IndexCommand, speech.commands.PitchCommand}
    supportedNotifications = {synthIndexReached, synthDoneSpeaking}

    @classmethod
    def check(cls):
        return all(os.path.isfile(p) for p in (EXE, FIRMWARE, STATE))

    def __init__(self):
        super().__init__()
        self._rate = 50        # unit rate 11, its factory setting
        self._pitch = 50       # unit pitch ~16 (81 Hz), its factory setting
        self._volume = 100
        self._join = True
        self._short = True
        self._numbers = True
        self._pitch_dirty = False        # a pitch command the unit may not have read yet
        self._snap_until_speech = False
        self._tone = str(DEFAULT_TONE)
        self._sent = (DEFAULT_RATE, DEFAULT_PITCH, DEFAULT_TONE)   # the unit boots with these
        self._player = self._makePlayer()
        self._queue = queue.Queue()
        self._cancelFlag = threading.Event()
        self._stopped = False
        self._unit = None
        self._worker = threading.Thread(target=self._run, name="blazie-ssi263", daemon=True)
        self._worker.start()

    def _makePlayer(self):
        import config
        base = dict(channels=1, samplesPerSec=OUT_RATE, bitsPerSample=16)
        try:
            from nvwave import AudioPurpose
            purpose = {"purpose": AudioPurpose.SPEECH}
        except Exception:
            purpose = {}

        def modern():
            return nvwave.WavePlayer(outputDevice=config.conf["audio"]["outputDevice"], **base, **purpose)

        def legacy():
            # NVDA 2021-2024: WinMM (or opt-in WASAPI).  WinMM stutters on small blocks
            # unless buffered, which is what NVDA's own eSpeak asked for there.
            return nvwave.WavePlayer(outputDevice=config.conf["speech"]["outputDevice"], buffered=True, **base)

        def default():
            return nvwave.WavePlayer(**base, **purpose)

        last = None
        for attempt in (modern, legacy, default):
            try:
                return attempt()
            except Exception as e:
                last = e
        raise last

    # -- NVDA interface ----------------------------------------------------
    def speak(self, speechSequence):
        items = []
        for item in speechSequence:
            if isinstance(item, str):
                items.append(("text", item))
            elif isinstance(item, speech.commands.IndexCommand):
                items.append(("index", item.index))
            elif isinstance(item, speech.commands.PitchCommand):
                # How NVDA marks a capital: an offset on the user's own 0-100 pitch.
                items.append(("pitch", item.offset))
        self._queue.put(_joined(items) if self._join else items)

    def cancel(self):
        self._cancelFlag.set()
        try:
            self._player.stop()
        except Exception:
            pass
        while True:
            try:
                self._queue.get_nowait()
            except queue.Empty:
                break

    def pause(self, switch):
        try:
            self._player.pause(switch)
        except Exception:
            pass

    def terminate(self):
        self._stopped = True
        self.cancel()
        self._queue.put(None)
        try:
            self._player.close()
        except Exception:
            pass

    # -- settings: the unit's own values ------------------------------------------
    def _get_rate(self):
        return self._rate

    def _set_rate(self, v):
        self._rate = max(0, min(100, int(v)))

    def _get_pitch(self):
        return self._pitch

    def _set_pitch(self, v):
        self._pitch = max(0, min(100, int(v)))

    def _get_shortPauses(self):
        return self._short

    def _set_shortPauses(self, v):
        self._short = bool(v)

    def _get_numberWords(self):
        return self._numbers

    def _set_numberWords(self, v):
        self._numbers = bool(v)

    def _get_joinPhrases(self):
        return self._join

    def _set_joinPhrases(self, v):
        self._join = bool(v)

    def _get_volume(self):
        return self._volume

    def _set_volume(self, v):
        self._volume = max(0, min(100, int(v)))

    def _get_availableVariants(self):
        return {str(t): StringParameterInfo(str(t), "Tone %d%s" % (t, " (default)" if t == DEFAULT_TONE else ""))
                for t in range(1, 26)}

    def _get_variant(self):
        return self._tone

    def _set_variant(self, v):
        if v in {str(t) for t in range(1, 26)}:
            self._tone = v

    def _get_availableVoices(self):
        return {"blazie": VoiceInfo("blazie", "Braille Lite 2000 (June 2003)", "en")}

    def _get_voice(self):
        return "blazie"

    def _set_voice(self, v):
        pass

    @staticmethod
    def _unit_pitch(p):
        p = max(0, min(100, p))
        return 1 + int(p * (DEFAULT_PITCH - 1) / 50 + 0.5) if p <= 50 else \
            DEFAULT_PITCH + int((p - 50) * (63 - DEFAULT_PITCH) / 50 + 0.5)

    @staticmethod
    def _unit_rate(r):
        r = max(0, min(100, r))
        return 1 + int(r * (DEFAULT_RATE - 1) / 50 + 0.5) if r <= 50 else \
            DEFAULT_RATE + int((r - 50) * (MAX_RATE - DEFAULT_RATE) / 50 + 0.5)

    def _unit_settings(self):
        return self._unit_rate(self._rate), self._unit_pitch(self._pitch), int(self._tone)

    # -- worker: the only thread that talks to the emulated unit --------------------
    def _boot(self):
        if self._unit is not None:
            self._unit.close()
        # In the unit's speech menu before speech-box mode: punctuation NONE, because NVDA
        # names symbols itself and also passes some through ("left paren (") -- the unit
        # read those too: "eti dash dash eloquence", "left paren left paren" (Tomi, 0.2.0).
        # And full numbers: the snapshot reads every number digit by digit.
        # Keys 3M instructions after power-on, then 1.5M apart: the launch drops from 1.03 s
        # to 0.25 s.  The unit then writes exactly what it did with the MASTER harness's
        # 8M/10M (tools/boot_gaps.py); below a 2.5M start it never reaches speech-box mode.
        # The emulator is deterministic, so this holds on every machine.
        unit = Blazie(EXE, FIRMWARE, STATE, chip=SSI263C(out_rate=OUT_RATE), out_rate=OUT_RATE,
                      menu=("punct_none", "numbers_toggle"), key_start=3000000, key_gap=1500000)
        unit.send(b"\x18")
        unit.send(b"\r\x06")
        unit.run(0.3)
        unit.send(b"\x05%dV" % UNIT_VOLUME)
        unit.run(0.05)
        return unit

    def _run(self):
        try:
            self._unit = self._boot()
        except Exception:
            log.error("Blazie: could not start the emulated unit", exc_info=True)
            return
        while not self._stopped:
            job = self._queue.get()
            if job is None:
                break
            self._cancelFlag.clear()
            try:
                self._speakJob(job)
            except Exception:
                log.error("Blazie speech failed; restarting the emulated unit", exc_info=True)
                try:
                    self._unit = self._boot()
                    self._sent = (DEFAULT_RATE, DEFAULT_PITCH, DEFAULT_TONE)
                except Exception:
                    log.error("Blazie restart failed", exc_info=True)
            if self._cancelFlag.is_set():
                try:
                    self._unit.cancel()
                    if self._pitch_dirty:
                        self._resend_pitch()
                except Exception:
                    pass
                # NVDA stopped the player on its own thread; a block this thread had
                # already computed may have been fed after that and would play at the
                # head of the next utterance.  Stop again from here, after the last feed.
                try:
                    self._player.stop()
                except Exception:
                    pass
            else:
                synthDoneSpeaking.notify(synth=self)
        if self._unit is not None:
            self._unit.close()

    def _speakJob(self, items):
        unit = self._unit
        settings = self._unit_settings()
        if settings != self._sent:
            unit.send(b"\x05%dE\x05%dP\x05%dT" % settings)
            unit.run(0.02)
            self._sent = settings
        gain = MAKEUP * self._volume / 100.0
        self._cur_pitch = settings[1]
        self._lead = True                # nothing audible fed yet in this utterance
        try:
            self._speakItems(items, unit, gain)
        finally:
            # restore the user's pitch only after the capital's audio exists
            if self._cur_pitch != settings[1]:
                unit.chip.snap_pitch = True
                unit.send(b"\x05%dP" % settings[1])
                self._cur_pitch = settings[1]
                self._pitch_dirty = True

    def _speakItems(self, items, unit, gain):
        for kind, value in items:
            if self._cancelFlag.is_set():
                return
            if kind == "index":
                self._notifyIndex(value)
                continue
            if kind == "pitch":
                want = self._unit_pitch(self._pitch + (value or 0))
                if want != self._cur_pitch:
                    unit.chip.snap_pitch = True
                    unit.send(b"\x05%dP" % want)
                    self._cur_pitch = want
                    self._pitch_dirty = True
                continue
            text = _clean(value)
            if self._numbers:
                # the firmware says 5 digits and up one digit at a time: 12345 is "one two
                # three four five", 1,234,567 "one two three four five six seven"
                text = numwords.normalise(text)
            lines = _lines(text, pack=self._short)
            if not lines:
                continue
            # all lines at once: the unit goes from one to the next itself, without a host
            # round trip per line (line breaks 280-360 ms -> 150-260 ms, measured)
            unit.turbo_between_lines = self._short
            t_start = unit.chip.time
            unit.say(lines)
            while not self._cancelFlag.is_set():
                y = unit.run(BLOCK_S)
                if self._lead:
                    # the unit reads the whole line before it speaks (x4 CPU meanwhile); that
                    # silence is only delay: 360 -> 60 ms for a 24-word line, ~100 -> 20 ms
                    # for a short one (tools/latency_check.py)
                    y, found = _trim_lead(y)
                    self._lead = not found
                if len(y) and not self._cancelFlag.is_set():
                    pcm = unit.chip.dsp.pcm16(y, gain)
                    self._player.feed(pcm)
                if self._snap_until_speech and unit.last_speech >= t_start:
                    # the first phoneme is set up; a leftover snap would flatten a glide later
                    unit.chip.snap_pitch = False
                    self._snap_until_speech = False
                if not unit.busy():
                    self._pitch_dirty = False    # the unit has read everything sent so far
                    break
        if not self._cancelFlag.is_set() and self._queue.empty():
            try:
                self._player.idle()
            except Exception:
                pass

    def _resend_pitch(self):
        """A cancel drops whatever the unit has not read yet, pitch commands with it: a
        capital cut off mid-word left every later word high, because the driver thought
        the pitch was back (Tomi, 0.2.0).  Say the user's pitch again, and have it land
        at once on the next utterance's first phoneme instead of gliding down into it."""
        base = self._sent[1]
        self._unit.chip.snap_pitch = True
        self._snap_until_speech = True
        self._unit.send(b"\x05%dP" % base)
        self._cur_pitch = base

    def _notifyIndex(self, index):
        cb = lambda: synthIndexReached.notify(synth=self, index=index)   # noqa: E731
        try:
            self._player.feed(b"", onDone=cb)
        except Exception:
            cb()
