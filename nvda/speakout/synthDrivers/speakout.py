# -*- coding: utf-8 -*-
"""NVDA synthesizer driver: the Speak-Out (1995) talking through an emulated SSI-263.

The box's own firmware runs on MAME's V40 inside NVDA's process, in ssi263speech.dll (src/csrc/speakout: the board,
the host's lockstep with the chip, and this driver's front end in C since 0.7.5 -- no Python host): its
letter-to-sound rules, number reading and settings are the originals, byte for byte.  Its phoneme frames go to a
register-level model of the SSI-263 chip, whose A/R request drives the firmware exactly as the real chip's did.
Nothing is recorded or concatenated.  This file keeps NVDA's side: the worker thread, the player, index and done
callbacks, cancel; nvda/tools/native_driver_equiv.py holds it to 0.7.0's Python driver byte for byte.

This add-on carries the Speak-Out's firmware (GW Micro), which is not ours.
"""

import os
import queue
import threading

import nvwave
from synthDriverHandler import SynthDriver, VoiceInfo, synthIndexReached, synthDoneSpeaking
from autoSettingsUtils.utils import StringParameterInfo
from autoSettingsUtils.driverSetting import BooleanDriverSetting, DriverSetting
from logHandler import log
import speech.commands

_HERE = os.path.dirname(__file__)
_ENGINE_DIR = os.path.join(_HERE, "_ssi263_speakout")

# The engine is this add-on's own package, imported relatively and never through sys.path:
# the other add-ons ship modules of the same names, and one process has one module per name.
from ._ssi263_speakout import ssi263_rates as rates
from ._ssi263_speakout.ssi263speech import SpeakOutC, dll_path

FIRMWARE = os.path.join(_ENGINE_DIR, "SPEAKOUT.HEX")
DLL = dll_path(_ENGINE_DIR)
TONES = "abcdefghijklmnopqrstuvwxyz"


def _nothing():
    """onDone for the end-of-utterance flush: the call is what matters, not the callback."""


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
        DriverSetting(rates.SETTING_ID, rates.SETTING_LABEL, defaultVal=str(rates.DEFAULT)),
    )
    supportedCommands = {speech.commands.IndexCommand, speech.commands.PitchCommand}
    supportedNotifications = {synthIndexReached, synthDoneSpeaking}
    # The synth NVDA's speech manager knows: this driver, or the 0.8 add-on's driver (synthDrivers/ssi263.py)
    # running it inside -- NVDA drops a notification whose synth is not getSynth().
    notifySynth = None

    @classmethod
    def check(cls):
        return os.path.isfile(FIRMWARE) and os.path.isfile(DLL)

    def __init__(self, startVoice=None):
        super().__init__()
        self._rate = 50       # box rate 5, its default
        self._pitch = 50      # box pitch 3, its default
        self._volume = 100
        self._join = True
        self._short = True
        self._tone = "i"      # box tone i, its default
        # Defaults only: NEVER read config.conf["speech"][<driver>] here.  NVDA registers this driver's settings
        # after __init__, and its config caches a failed lookup as missing, so an early read of a new key made
        # NVDA's own loadSettings fail ("setSynth failed ... KeyError: 'voiceInflection'", Tomi, 0.6.0 draft).
        # NVDA applies the saved values through the setters right after; the worker restarts once if needed.
        self._out_rate = self._want_rate = rates.DEFAULT   # the worker switches to _want_rate
        self._player = self._makePlayer()
        self._queue = queue.Queue()
        self._cancelFlag = threading.Event()
        self._stopped = False
        self._box = None
        # the worker's first boot, for a driver running this one inside (synthDrivers/ssi263.py): set once it
        # is up, with bootError when it failed -- the constructor returning says nothing about the firmware
        self.booted = threading.Event()
        self.bootError = None
        self._worker = threading.Thread(target=self._run, name="speakout-ssi263", daemon=True)
        self._worker.start()

    def _makePlayer(self):
        import config
        base = dict(channels=1, samplesPerSec=self._out_rate, bitsPerSample=16)
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

    def _get_availableSamplerates(self):
        return {str(r): StringParameterInfo(str(r), rates.LABELS[r]) for r in rates.RATES}

    def _get_sampleRate(self):
        return str(self._want_rate)

    def _set_sampleRate(self, v):
        # the worker applies it before the next utterance: new player, box rebooted at the new rate
        self._want_rate = rates.parse(v) or self._want_rate

    def _get_availableVoices(self):
        return {"speakout": VoiceInfo("speakout", "Speak-Out", "en")}

    def _get_voice(self):
        return "speakout"

    def _set_voice(self, v):
        pass

    # -- worker: the only thread that touches the emulated box -----------------
    def _boot(self):
        # power-on, the greeting flushed, punctuation none (NVDA speaks symbols itself): so_voice.c's boot
        return SpeakOutC(DLL, FIRMWARE, self._out_rate)

    def _apply(self, box):
        """the settings as they are now; the box sends them at begin() when they changed"""
        box.set(self._rate, self._pitch, TONES.index(self._tone), self._volume, self._join, self._short)

    def _run(self):
        try:
            self._box = self._boot()
        except Exception as e:
            log.error("Speak-Out: could not start the emulated box", exc_info=True)
            self.bootError = e
            self.booted.set()
            return
        self.booted.set()
        while not self._stopped:
            job = self._queue.get()
            if job is None:
                break
            self._cancelFlag.clear()
            if self._want_rate != self._out_rate:
                try:
                    self._switch_rate()
                except Exception:
                    log.error("Speak-Out: could not switch the sample rate", exc_info=True)
            try:
                self._speakJob(job)
            except Exception:
                log.error("Speak-Out speech failed; rebooting the emulated box", exc_info=True)
                try:
                    self._reboot()
                except Exception:
                    log.error("Speak-Out reboot failed", exc_info=True)
            if self._cancelFlag.is_set():
                try:
                    self._box.cancel()           # the box's flush, and the pitch said again if it was dropped
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
                synthDoneSpeaking.notify(synth=self.notifySynth or self)

    def _reboot(self):
        old, self._box = self._box, self._boot()
        old.close()

    def _switch_rate(self):
        """A new sample rate: the chip renders at the host rate, so a new player and a rebooted box."""
        self._out_rate = self._want_rate
        old, self._player = self._player, self._makePlayer()
        try:
            old.close()
        except Exception:
            pass
        self._reboot()

    def _speakJob(self, items):
        box = self._box
        self._apply(box)
        box.begin()
        try:
            self._speakItems(items, box)
        finally:
            # restore the user's pitch only after the capital's audio exists
            box.end()

    def _speakItems(self, items, box):
        for kind, value in items:
            if self._cancelFlag.is_set():
                return
            if kind == "index":
                self._notifyIndex(value)
                continue
            if kind == "pitch":
                self._apply(box)                 # the user's pitch now, the offset on it
                box.pitch(value or 0)
                continue
            text = box.prepare(value)            # currencies ("£2.63": the firmware reads only "$"), clean, strip
            if not text:
                continue
            box.say(text + "\r")
            while not self._cancelFlag.is_set():
                pcm, done = box.render()         # 30 ms of the box; its reading time trimmed at the head
                if pcm and not self._cancelFlag.is_set():
                    self._player.feed(pcm)
                if done:
                    if box.fault:
                        raise RuntimeError("the emulated V40 faulted")
                    break
        if self._cancelFlag.is_set():
            return
        try:
            if self._queue.empty():
                self._player.idle()
            else:
                # NVDA 2021-2023's buffered player holds blocks until it has 300 ms of them,
                # unless a feed carries onDone: without this the last one waited there
                # and came out at the head of the next utterance (a tester, 0.5.0)
                self._player.feed(b"", onDone=_nothing)
        except Exception:
            pass

    def _notifyIndex(self, index):
        cb = lambda: synthIndexReached.notify(synth=self.notifySynth or self, index=index)   # noqa: E731
        try:
            self._player.feed(b"", onDone=cb)
        except Exception:
            cb()
