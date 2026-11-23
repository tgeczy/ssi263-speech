# -*- coding: utf-8 -*-
"""NVDA synthesizer driver: Artic's SynPhonix 215 talking through an emulated SSI-263.

The SynPhonix 215 was an ISA card with an Artic-263 (an SSI-263) and little else; its text-to-speech ran on the PC, in
Artic's own DOS program VEST.COM (version 1.60, the "Special Release For Artic Visions" that Business Vision 3.08
loads first).  Here that program runs on MAME's 8086 inside NVDA's process, in ssi263speech.dll (src/csrc/synphonix:
the card and the host's lockstep with the chip, sp_host.h; the voice's text path, sp_voice.h): its letter-to-sound
rules, its lexicon and its inflection are the originals, driving a register-level model of the SSI-263.  Nothing is
recorded or concatenated.  This file keeps NVDA's side, as smartalk.py does: the worker thread, the player, index and
done callbacks, cancel.

The settings are VEST's own offsets (sp_voice.h): rate and pitch on NVDA's scales, its inflection on or off; the volume
is the audio's.  VEST has no tone and no index marks of its own: each index is reported at its share of the line, as
on the Mockingboard.  It runs inside the 0.8 add-on's driver (synthDrivers/ssi263.py), not as an add-on of its own.
The software is Artic Technologies', not ours.
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
_ENGINE_DIR = os.path.join(_HERE, "_ssi263_synphonix")

# the engine is this add-on's own package, imported relatively and never through sys.path (speakout.py)
from ._ssi263_synphonix import ssi263_rates as rates
from ._ssi263_synphonix.ssi263speech import SynPhonixC, dll_path
try:
    from ._ssi263_synphonix import ssi263_flow as _flow      # say all's lines as one line to the unit
except ImportError:                       # an engine from before it: each text a line of its own
    _flow = None
try:
    from ._ssi263_synphonix import ssi263_feed as _first     # an utterance's first feed is never a sliver
except ImportError:                       # an engine from before it
    _first = None
FLOW = True                               # (the tests' control takes it away)
CANCEL_FIRST = True                       # cancel() empties the queue first (#34; a test's control)
PART = 240                                # the characters the voice takes as one part (sp_voice.h's SPV_PART)

FIRMWARE = os.path.join(_ENGINE_DIR, "VEST.COM")
DLL = dll_path(_ENGINE_DIR)
VOICES = (("vest", "Artic SynPhonix", FIRMWARE),)


def _nothing():
    """onDone for the end-of-utterance flush: the call is what matters, not the callback."""


def _joined(items):
    """NVDA often sends one item in pieces ("select synthesizer", "dialog"); each said as its own text ends in
    VEST's sentence pause.  Joined, they flow (speakout.py)."""
    out = []
    for kind, value in items:
        if kind == "text" and out and out[-1][0] == "text":
            out[-1] = ("text", out[-1][1].rstrip() + " " + value.lstrip())
        else:
            out.append((kind, value))
    return out


# NVDA's BreakCommand, a pause: honoured where NVDA has it (#71)
_BREAK = getattr(speech.commands, "BreakCommand", None)
_BREAKS = {_BREAK} if _BREAK else set()


class SynthDriver(SynthDriver):
    name = "synphonix"
    description = "Artic SynPhonix (SSI-263 emulation)"

    supportedSettings = (
        SynthDriver.VoiceSetting(),
        SynthDriver.RateSetting(),
        SynthDriver.PitchSetting(),
        SynthDriver.VolumeSetting(),
        BooleanDriverSetting("voiceInflection", "Voice &inflection (the unit's own on/off)", defaultVal=True),
        BooleanDriverSetting("joinPhrases", "&Join phrases (fewer pauses between words)", defaultVal=True),
        BooleanDriverSetting("numberWords", "Custom n&umber processing (fix digit-by-digit numbers)", defaultVal=True),
        DriverSetting(rates.SETTING_ID, rates.SETTING_LABEL, defaultVal=str(rates.DEFAULT)),
    )
    supportedCommands = {speech.commands.IndexCommand, speech.commands.PitchCommand} | _BREAKS                    # NVDA's pause (#71)
    supportedNotifications = {synthIndexReached, synthDoneSpeaking}
    # the synth NVDA's speech manager knows (blazie.py): this driver, or the 0.8 add-on's driver running it inside
    notifySynth = None

    @staticmethod
    def _present():
        if not os.path.isfile(DLL):
            return []
        return [v for v, _name, fw in VOICES if os.path.isfile(fw)]

    @classmethod
    def check(cls):
        return bool(cls._present())

    def __init__(self, startVoice=None):
        super().__init__()
        self._rate = 50       # VEST's own rate offset, 2 (sp_voice.c DEF_RATE)
        self._pitch = 50      # its own pitch (offset 0)
        self._volume = 100
        self._inflection = True
        self._join = True
        self._numbers = True
        # Defaults only: never read config.conf here (blazie.py: the 0.6.0 setSynth KeyError).
        self._out_rate = self._want_rate = rates.DEFAULT
        self._player = self._whole(self._makePlayer())
        self._queue = queue.Queue()
        self._cancelFlag = threading.Event()
        self._gen = 0                    # cancels so far: each job carries the count it was queued after
        self._stopped = False
        self._box = None
        present = self._present()
        self._model = startVoice if startVoice in present else (present[0] if present else "vest")
        # the worker's first boot, for a driver running this one inside (synthDrivers/ssi263.py): set once it
        # is up, with bootError when it failed -- the constructor returning says nothing about the firmware
        self.booted = threading.Event()
        self.bootError = None
        self._worker = threading.Thread(target=self._run, name="synphonix-ssi263", daemon=True)
        self._worker.start()

    def _whole(self, player):
        """the player behind ssi263_feed.FirstFeed: after a stop or an idle its first feed is never a
        sliver of sound, which NVDA's own silence trim would leave to pop (a listener, 2026-11-20)"""
        return _first.FirstFeed(player, self._out_rate) if _first else player

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
            elif _BREAK is not None and isinstance(item, _BREAK):
                items.append(("break", item.time))          # a pause (#71)
        self._queue.put((self._gen, _joined(items) if self._join else items))

    def cancel(self):
        if CANCEL_FIRST:
            self._gen += 1               # (#34) before the flag: see the worker's check
        self._cancelFlag.set()
        if CANCEL_FIRST:                 # (#34) emptied before the player's stop, a call into NVDA's
            while True:                  # C library that lets the worker run and take a job
                try:
                    self._queue.get_nowait()
                except queue.Empty:
                    break
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

    def _get_voiceInflection(self):
        return self._inflection

    def _set_voiceInflection(self, v):
        self._inflection = bool(v)

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
        # the worker applies it before the next utterance: a new player, the card booted again at the new rate
        self._want_rate = rates.parse(v) or self._want_rate

    def _get_availableVoices(self):
        names = dict((v, name) for v, name, _fw in VOICES)
        return {v: VoiceInfo(v, names[v], "en") for v in self._present()}

    def _get_voice(self):
        return self._model

    def _set_voice(self, v):
        if v in self._present():
            self._model = v

    # -- worker: the only thread that touches the emulated card ---------------
    def _boot(self):
        return SynPhonixC(DLL, dict((v, fw) for v, _n, fw in VOICES)[self._model], self._out_rate)

    def _run(self):
        try:
            self._box = self._boot()
        except Exception as e:
            log.error("SynPhonix: could not start the emulated card", exc_info=True)
            self.bootError = e
            self.booted.set()
            return
        self.booted.set()
        while not self._stopped:
            job = self._queue.get()
            if job is None:
                break
            self._cancelFlag.clear()
            gen, job = job
            if CANCEL_FIRST and gen != self._gen:
                continue                     # queued before a cancel that came since (#34)
            if self._want_rate != self._out_rate:
                try:
                    self._switch_rate()
                except Exception:
                    log.error("SynPhonix: could not switch the sample rate", exc_info=True)
            self._handed = []                # the job's indexes handed on (_report_owed)
            try:
                self._speakJob(job)
            except Exception:
                log.error("SynPhonix speech failed; booting the emulated card again", exc_info=True)
                self._report_owed(job)
                try:
                    self._reboot()
                except Exception:
                    log.error("SynPhonix reboot failed", exc_info=True)
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
        """A new sample rate: the chip renders at the host rate, so a new player and a card booted again."""
        self._out_rate = self._want_rate
        old, self._player = self._player, self._whole(self._makePlayer())
        try:
            old.close()
        except Exception:
            pass
        self._reboot()

    def _speakJob(self, items):
        box = self._box
        box.set(self._rate, self._pitch, self._volume, self._inflection, self._numbers)
        offset = 0
        for kind, value in (_flow.lines(items) if _flow and FLOW and self._join else items):
            if self._cancelFlag.is_set():
                return
            if kind == "break":                  # NVDA's pause: that much silence, here (#71)
                if _flow and hasattr(_flow, "pause"):
                    _flow.pause(value, self._out_rate, self._player.feed, self._cancelFlag.is_set)
                continue
            if kind == "index":
                self._notifyIndex(value)
                continue
            if kind == "line":                   # say all's lines, as one line (ssi263_flow.py); the voice
                # prepares the text itself (in groups of at most PART characters, cut between lines: the voice's own
                # parts); VEST has no index marks of its own, so each index is reported at its share of the line
                for parts, marks, chars in _flow.line_groups(value, lambda t: " ".join(t.split()), PART):
                    if parts and box.say(" ".join(parts), offset):
                        _flow.play_line(box.render, marks, chars, self._player.feed, self._notifyIndex,
                                        self._cancelFlag.is_set)
                    if box.fault:
                        raise RuntimeError("the emulated SynPhonix failed")
                    for _chars, index in marks:  # (nothing was said: the indexes alone)
                        if not self._cancelFlag.is_set():
                            self._notifyIndex(index)
                continue
            if kind == "pitch":
                offset = value or 0
                continue
            if not box.say(value, offset):
                if box.fault:
                    raise RuntimeError("the emulated SynPhonix failed")
                continue
            while not self._cancelFlag.is_set():
                pcm, done = box.render()
                if pcm and not self._cancelFlag.is_set():
                    self._player.feed(pcm)
                if done:
                    if box.fault:
                        raise RuntimeError("the emulated SynPhonix failed")
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

    def _report_owed(self, job):
        """A fault ended the job (#33): what it had fed is let play (idle() calls the indexes already given and
        lets NVDA's ducking go), then the indexes it never handed on are reported, so NVDA, which waits for an
        utterance's last index before it says more, is not left waiting.  Not after a cancel: NVDA wants none."""
        if self._cancelFlag.is_set() or not _first or not getattr(_first, "REPORT_OWED", False):
            return
        try:
            self._player.idle()
        except Exception:
            pass
        if self._cancelFlag.is_set() and getattr(_first, "CANCEL_AFTER_IDLE", True):
            return                       # a cancel came while idle() waited: only this thread clears the flag
        for index in _first.owed(job, getattr(self, "_handed", ())):
            synthIndexReached.notify(synth=self.notifySynth or self, index=index)

    def _notifyIndex(self, index):
        if getattr(self, "_handed", None) is not None:
            self._handed.append(index)
        cb = lambda: synthIndexReached.notify(synth=self.notifySynth or self, index=index)   # noqa: E731
        try:
            self._player.feed(b"", onDone=cb)
        except Exception:
            cb()
