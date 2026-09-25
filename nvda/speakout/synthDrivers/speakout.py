# -*- coding: utf-8 -*-
"""NVDA synthesizer driver: the Speak-Out (1995) talking through an emulated SSI-263.

The box's own firmware runs under Unicorn inside NVDA's process: its letter-to-sound
rules, number reading and settings are the originals, byte for byte.  Its phoneme
frames go to a register-level model of the SSI-263 chip, whose A/R request drives
the firmware exactly as the real chip's did.  Nothing is recorded or concatenated.

This add-on carries the Speak-Out's firmware (GW Micro), which is not ours.
"""

import os
import queue
import sys
import threading

import nvwave
from synthDriverHandler import SynthDriver, VoiceInfo, synthIndexReached, synthDoneSpeaking
from autoSettingsUtils.utils import StringParameterInfo
from autoSettingsUtils.driverSetting import BooleanDriverSetting
from logHandler import log
import speech.commands

_HERE = os.path.dirname(__file__)
_ENGINE_DIR = os.path.join(_HERE, "_ssi263_speakout")

# The engine is this add-on's own package, imported relatively and never through sys.path:
# the Braille Lite add-on ships an `ssi263` too, and one process has one module per name.
from ._ssi263_speakout.speakout_host import SpeakOut
from ._ssi263_speakout.ssi263.native import SSI263C    # the chip in C

OUT_RATE = 44100
BLOCK_S = 0.03
FIRMWARE = os.path.join(_ENGINE_DIR, "SPEAKOUT.HEX")
TONES = "abcdefghijklmnopqrstuvwxyz"


def _clean(text):
    """Latin-1 text with no control characters: ^E and ^X are the box's commands."""
    out = []
    for ch in text:
        o = ord(ch)
        if o < 32 or o == 127:
            out.append(" ")
        elif o < 256:
            out.append(ch)
        else:
            out.append({"‘": "'", "’": "'", "“": '"', "”": '"',
                        "–": "-", "—": "-", "…": "..."}.get(ch, " "))
    return "".join(out)


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
    name = "speakout"
    description = "Speak-Out (SSI-263 emulation)"

    supportedSettings = (
        SynthDriver.VoiceSetting(),
        SynthDriver.VariantSetting(),
        SynthDriver.RateSetting(),
        SynthDriver.PitchSetting(),
        SynthDriver.VolumeSetting(),
        BooleanDriverSetting("joinPhrases", "&Join phrases (fewer pauses between words)", defaultVal=True),
        BooleanDriverSetting("shortPauses", "S&horten pauses between sentences", defaultVal=True),
    )
    supportedCommands = {speech.commands.IndexCommand, speech.commands.PitchCommand}
    supportedNotifications = {synthIndexReached, synthDoneSpeaking}

    @classmethod
    def check(cls):
        return os.path.isfile(FIRMWARE)

    def __init__(self):
        super().__init__()
        self._rate = 50       # box rate 5, its default
        self._pitch = 50      # box pitch 3, its default
        self._volume = 100
        self._join = True
        self._short = True
        self._pitch_dirty = False        # a pitch command the box may not have read yet
        self._snap_until_speech = False
        self._tone = "i"      # box tone i, its default
        self._sent = None     # settings last sent to the box
        self._player = self._makePlayer()
        self._queue = queue.Queue()
        self._cancelFlag = threading.Event()
        self._stopped = False
        self._box = None
        self._worker = threading.Thread(target=self._run, name="speakout-ssi263", daemon=True)
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

    # -- settings: the box's own 0-9 / a-z values ----------------------------
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

    def _get_joinPhrases(self):
        return self._join

    def _set_joinPhrases(self, v):
        self._join = bool(v)

    def _get_volume(self):
        return self._volume

    def _set_volume(self, v):
        self._volume = max(0, min(100, int(v)))

    def _get_availableVariants(self):
        return {t: StringParameterInfo(t, "Tone %s%s" % (t.upper(), " (default)" if t == "i" else ""))
                for t in TONES}

    def _get_variant(self):
        return self._tone

    def _set_variant(self, v):
        if v in TONES:
            self._tone = v

    def _get_availableVoices(self):
        return {"speakout": VoiceInfo("speakout", "Speak-Out", "en")}

    def _get_voice(self):
        return "speakout"

    def _set_voice(self, v):
        pass

    @staticmethod
    def _box_pitch(p):
        p = max(0, min(100, p))
        return int(p * 3 / 50 + 0.5) if p <= 50 else 3 + int((p - 50) * 6 / 50 + 0.5)   # 50 -> 3

    def _box_settings(self):
        rate = int(self._rate * 9 / 100 + 0.5)                       # 50 -> 5, the default
        # word delay 0 when joining, sentence delay 0 when shortening; the box's factory is 1 and 1
        return (rate, self._box_pitch(self._pitch), self._tone, 0 if self._join else 1,
                0 if self._short else 1)

    # -- worker: the only thread that touches the emulated box -----------------
    def _boot(self):
        box = SpeakOut(FIRMWARE, chip=SSI263C(out_rate=OUT_RATE), out_rate=OUT_RATE)
        box.keep_writes = False
        box.boot()
        box.say("\x05Mn")        # punctuation: none -- NVDA speaks symbols itself
        box.run(0.05)
        return box

    def _run(self):
        try:
            self._box = self._boot()
        except Exception:
            log.error("Speak-Out: could not start the emulated box", exc_info=True)
            return
        while not self._stopped:
            job = self._queue.get()
            if job is None:
                break
            self._cancelFlag.clear()
            try:
                self._speakJob(job)
            except Exception:
                log.error("Speak-Out speech failed; rebooting the emulated box", exc_info=True)
                try:
                    self._box = self._boot()
                    self._sent = None
                except Exception:
                    log.error("Speak-Out reboot failed", exc_info=True)
            if self._cancelFlag.is_set():
                try:
                    self._box.cancel()
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

    def _speakJob(self, items):
        box = self._box
        settings = self._box_settings()
        if settings != self._sent:
            box.say("\x05R%d\x05P%d\x05T%s\x05W%d\x05I%d" % settings)
            self._sent = settings
        self._cur_pitch = settings[1]
        gain = self._volume / 100.0
        cur_pitch = settings[1]
        self._lead = True                # nothing audible fed yet in this utterance
        try:
            self._speakItems(items, box, gain)
        finally:
            # restore the user's pitch only after the capital's audio exists
            if self._cur_pitch != settings[1]:
                box.chip.snap_pitch = True
                box.say("\x05P%d" % settings[1])
                self._cur_pitch = settings[1]
                self._pitch_dirty = True

    def _speakItems(self, items, box, gain):
        cur_pitch = self._cur_pitch
        for kind, value in items:
            if self._cancelFlag.is_set():
                return
            if kind == "index":
                self._notifyIndex(value)
                continue
            if kind == "pitch":
                want = self._box_pitch(self._pitch + (value or 0))
                if want != cur_pitch:
                    box.chip.snap_pitch = True
                    box.say("\x05P%d" % want)
                    cur_pitch = self._cur_pitch = want
                    self._pitch_dirty = True
                continue
            text = _clean(value).strip()
            if not text:
                continue
            t_start = box.chip.time
            box.say(text + "\r")
            while not self._cancelFlag.is_set():
                y = box.run(BLOCK_S)
                if self._lead:
                    y, found = _trim_lead(y)     # the box's reading time is only delay
                    self._lead = not found
                if len(y) and not self._cancelFlag.is_set():
                    pcm = box.chip.dsp.pcm16(y, gain)
                    self._player.feed(pcm)
                if self._snap_until_speech and box.last_speech >= t_start:
                    # the first phoneme is set up; a leftover snap would flatten a glide later
                    box.chip.snap_pitch = False
                    self._snap_until_speech = False
                if not box.busy():
                    self._pitch_dirty = False    # the box has read everything sent so far
                    break
        if not self._cancelFlag.is_set() and self._queue.empty():
            try:
                self._player.idle()
            except Exception:
                pass

    def _resend_pitch(self):
        """A cancel drops whatever the box has not read yet, pitch commands with it: a
        capital cut off mid-word left every later word high, because the driver thought
        the pitch was back (Tomi, 0.2.0).  Say the user's pitch again, and have it land
        at once on the next utterance's first phoneme instead of gliding down into it."""
        if self._sent is None:
            return                       # rebooted: the next job sends every setting anyway
        base = self._sent[1]
        self._box.chip.snap_pitch = True
        self._snap_until_speech = True
        self._box.say("\x05P%d" % base)
        self._cur_pitch = base

    def _notifyIndex(self, index):
        cb = lambda: synthIndexReached.notify(synth=self, index=index)   # noqa: E731
        try:
            self._player.feed(b"", onDone=cb)
        except Exception:
            cb()
