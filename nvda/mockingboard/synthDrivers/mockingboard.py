# -*- coding: utf-8 -*-
"""NVDA synthesizer driver: Sweet Micro Systems' Mockingboard talking through an emulated SSI-263 (0.8).

The card's own text-to-speech (version 1.1, 11 March 1985, from the Mockingboard Developers Toolkit) runs on
Fake6502 and the board inside NVDA's process, in ssi263speech.dll (src/csrc/mockingboard: the board, the host's
lockstep with the chip, and the voice's text path, mb_voice.h): its letter-to-sound rules and its inflection are the
originals, byte for byte, driving a register-level model of the SSI-263.  Nothing is recorded or concatenated.  This
file keeps NVDA's side, as speakout.py does: the worker thread, the player, index and done callbacks, cancel.

It runs inside the 0.8 add-on's driver (synthDrivers/ssi263.py), not as an add-on of its own.  The firmware is
Sweet Micro Systems', not ours.
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
_ENGINE_DIR = os.path.join(_HERE, "_ssi263_mockingboard")

# the engine is this add-on's own package, imported relatively and never through sys.path (speakout.py)
from ._ssi263_mockingboard import ssi263_rates as rates
from ._ssi263_mockingboard.ssi263speech import MockingboardC, dll_path

FIRMWARE = os.path.join(_ENGINE_DIR, "mockingboard-tts-1.1.bin")
DLL = dll_path(_ENGINE_DIR)


def _nothing():
    """onDone for the end-of-utterance flush: the call is what matters, not the callback."""


def _joined(items):
    """NVDA often sends one item in pieces ("select synthesizer", "dialog"); joined, they are one text (speakout.py)."""
    out = []
    for kind, value in items:
        if kind == "text" and out and out[-1][0] == "text":
            out[-1] = ("text", out[-1][1].rstrip() + " " + value.lstrip())
        else:
            out.append((kind, value))
    return out


class SynthDriver(SynthDriver):
    name = "mockingboard"
    description = "Mockingboard (SSI-263 emulation)"

    supportedSettings = (
        SynthDriver.VoiceSetting(),
        SynthDriver.RateSetting(),
        SynthDriver.PitchSetting(),
        SynthDriver.VolumeSetting(),
        BooleanDriverSetting("joinPhrases", "&Join phrases (fewer pauses between words)", defaultVal=True),
        BooleanDriverSetting("numberWords", "Custom n&umber processing (fix digit-by-digit numbers)", defaultVal=True),
        DriverSetting(rates.SETTING_ID, rates.SETTING_LABEL, defaultVal=str(rates.DEFAULT)),
    )
    supportedCommands = {speech.commands.IndexCommand, speech.commands.PitchCommand}
    supportedNotifications = {synthIndexReached, synthDoneSpeaking}
    # the synth NVDA's speech manager knows (blazie.py): this driver, or the 0.8 add-on's driver running it inside
    notifySynth = None

    @classmethod
    def check(cls):
        return os.path.isfile(FIRMWARE) and os.path.isfile(DLL)

    def __init__(self):
        super().__init__()
        self._rate = 50       # the firmware's rate 8, the toolkit demo's
        self._pitch = 50      # its inflection 8, the demo's
        self._volume = 100
        self._join = True
        self._numbers = True
        # Defaults only: never read config.conf here (blazie.py: the 0.6.0 setSynth KeyError).
        self._out_rate = self._want_rate = rates.DEFAULT
        self._player = self._makePlayer()
        self._queue = queue.Queue()
        self._cancelFlag = threading.Event()
        self._stopped = False
        self._box = None
        self._worker = threading.Thread(target=self._run, name="mockingboard-ssi263", daemon=True)
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
            # NVDA 2021-2024 (speakout.py): WinMM stutters on small blocks unless buffered
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
                # a capital: an offset on the user's own pitch, for the texts after it until the next PitchCommand
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

    # -- settings ----------------------------------------------------------
    def _get_rate(self):
        return self._rate

    def _set_rate(self, v):
        self._rate = max(0, min(100, int(v)))

    def _get_pitch(self):
        return self._pitch

    def _set_pitch(self, v):
        self._pitch = max(0, min(100, int(v)))

    def _get_volume(self):
        return self._volume

    def _set_volume(self, v):
        self._volume = max(0, min(100, int(v)))

    def _get_joinPhrases(self):
        return self._join

    def _set_joinPhrases(self, v):
        self._join = bool(v)

    def _get_numberWords(self):
        return self._numbers

    def _set_numberWords(self, v):
        self._numbers = bool(v)

    def _get_availableSamplerates(self):
        return {str(r): StringParameterInfo(str(r), rates.LABELS[r]) for r in rates.RATES}

    def _get_sampleRate(self):
        return str(self._want_rate)

    def _set_sampleRate(self, v):
        # the worker applies it before the next utterance: a new player, the card rebooted at the new rate
        self._want_rate = rates.parse(v) or self._want_rate

    def _get_availableVoices(self):
        return {"mockingboard": VoiceInfo("mockingboard", "Mockingboard", "en")}

    def _get_voice(self):
        return "mockingboard"

    def _set_voice(self, v):
        pass

    # -- worker: the only thread that touches the emulated card ----------------
    def _boot(self):
        return MockingboardC(DLL, FIRMWARE, self._out_rate)

    def _run(self):
        try:
            self._box = self._boot()
        except Exception:
            log.error("Mockingboard: could not start the emulated card", exc_info=True)
            return
        while not self._stopped:
            job = self._queue.get()
            if job is None:
                break
            self._cancelFlag.clear()
            if self._want_rate != self._out_rate:
                try:
                    self._switch_rate()
                except Exception:
                    log.error("Mockingboard: could not switch the sample rate", exc_info=True)
            try:
                self._speakJob(job)
            except Exception:
                log.error("Mockingboard speech failed; rebooting the emulated card", exc_info=True)
                try:
                    self._reboot()
                except Exception:
                    log.error("Mockingboard reboot failed", exc_info=True)
            if self._cancelFlag.is_set():
                try:
                    self._box.cancel()
                except Exception:
                    pass
                # a block computed before NVDA's stop may have been fed after it (speakout.py): stop again
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
        """A new sample rate: the chip renders at the host rate, so a new player and a rebooted card."""
        self._out_rate = self._want_rate
        old, self._player = self._player, self._makePlayer()
        try:
            old.close()
        except Exception:
            pass
        self._reboot()

    def _speakJob(self, items):
        box = self._box
        box.set(self._rate, self._pitch, self._volume, self._numbers)
        offset = 0
        for kind, value in items:
            if self._cancelFlag.is_set():
                return
            if kind == "index":
                self._notifyIndex(value)
                continue
            if kind == "pitch":
                offset = value or 0
                continue
            if not box.say(value, offset):
                continue
            while not self._cancelFlag.is_set():
                pcm, done = box.render()
                if pcm and not self._cancelFlag.is_set():
                    self._player.feed(pcm)
                if done:
                    if box.fault:
                        raise RuntimeError("the emulated 6502 did not come back")
                    break
        if self._cancelFlag.is_set():
            return
        try:
            if self._queue.empty():
                self._player.idle()
            else:
                # NVDA 2021-2023's buffered player (speakout.py): the last block must not wait for the next utterance
                self._player.feed(b"", onDone=_nothing)
        except Exception:
            pass

    def _notifyIndex(self, index):
        cb = lambda: synthIndexReached.notify(synth=self.notifySynth or self, index=index)   # noqa: E731
        try:
            self._player.feed(b"", onDone=cb)
        except Exception:
            cb()
