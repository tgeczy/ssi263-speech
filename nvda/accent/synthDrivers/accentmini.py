# -*- coding: utf-8 -*-
"""NVDA synthesizer driver: the Aicom Accent SA and Accent-mini, talking through an emulated SSI-263.

Two voices, two of Aicom's own programs, both in ssi263speech.dll (since 0.7.5 with this driver's front end in C
too -- no Python host):
- Accent-mini: Aicom's DOS device driver (SPKEMS.DVC, "Accent-EMS", V4.5, 1986-1993) runs on MAME's 8086 inside
  NVDA's process, loaded as DOS would load it, with its rules in emulated expanded memory; the model's A/R request
  is the card's IRQ (src/csrc/accentmini).
- Accent SA: the stand-alone box's own 8085 firmware (1986-1989) and dictionary ROMs run on MAME's 8085, with text
  arriving on its serial port and A/R on its TRAP (src/csrc/accentsa).
Both drive a register-level model of the SSI-263 (Aicom's AI901), so the rules, intonation and timing are Aicom's
own.  Nothing is recorded or concatenated.  This file keeps NVDA's side: the worker thread, the player, index and
done callbacks, cancel; nvda/tools/native_driver_equiv.py holds it to 0.7.0's Python driver byte for byte.

This add-on carries Aicom's driver and firmware; see AICOM.txt beside them.
"""

import os
import queue
import sys
import threading
import time
import traceback

import nvwave
from synthDriverHandler import SynthDriver, VoiceInfo, synthIndexReached, synthDoneSpeaking
from autoSettingsUtils.utils import StringParameterInfo
from autoSettingsUtils.driverSetting import BooleanDriverSetting, DriverSetting
from logHandler import log
import speech.commands

_HERE = os.path.dirname(__file__)
_ENGINE_DIR = os.path.join(_HERE, "_ssi263_accent")

# The engine is this add-on's own package, imported relatively and never through sys.path:
# the other add-ons ship modules of the same names.
from ._ssi263_accent import ssi263_rates as rates
from ._ssi263_accent.ssi263speech import AccentMiniC, AccentSAC, dll_path

# Developer switch: True writes a step-by-step "Accent:" trace to NVDA's log (at debug level)
# and starts a thread that reports a stuck worker.  Off for everyone else; not a setting.
DEBUG_LOG = False

DRIVER = os.path.join(_ENGINE_DIR, "SPKEMS.DVC")
SA_ROMS = os.path.join(_ENGINE_DIR, "accent-sa")      # u2.BIN u3.BIN u4.BIN
DLL = dll_path(_ENGINE_DIR)
VOICES = (("mini", "Accent-mini"), ("sa", "Accent SA"))


def _nothing():
    """onDone for the end-of-utterance flush: the call is what matters, not the callback."""


def _dbg(msg):
    if not DEBUG_LOG:
        return
    try:
        if log.isEnabledFor(10):             # logging.DEBUG: only when NVDA logs at debug level
            log.debug("Accent: " + msg)
    except Exception:
        pass


def _short(text, n=80):
    return ascii(text[:n] + ("..." if len(text) > n else ""))


def _summary(job):
    return " ".join("%s(%s)" % (k, len(v) if k == "text" else v) for k, v in job)


def _joined(items):
    """NVDA often sends one item in pieces ("select synthesizer", "dialog"); joined, they flow."""
    out = []
    for kind, value in items:
        if kind == "text" and out and out[-1][0] == "text":
            out[-1] = ("text", out[-1][1].rstrip() + " " + value.lstrip())
        else:
            out.append((kind, value))
    return out


class SynthDriver(SynthDriver):
    name = "accentmini"
    description = "Accent SA and Mini (SSI-263 emulation)"

    supportedSettings = (
        SynthDriver.VoiceSetting(),
        SynthDriver.VariantSetting(),
        SynthDriver.RateSetting(),
        SynthDriver.PitchSetting(),
        SynthDriver.InflectionSetting(),
        SynthDriver.VolumeSetting(),
        BooleanDriverSetting("joinPhrases", "&Join phrases (fewer pauses between words)", defaultVal=True),
        BooleanDriverSetting("numberWords", "Custom n&umber processing (fix digit-by-digit numbers)", defaultVal=True),
        DriverSetting(rates.SETTING_ID, rates.SETTING_LABEL, defaultVal=str(rates.DEFAULT)),
    )
    supportedCommands = {speech.commands.IndexCommand, speech.commands.PitchCommand}
    supportedNotifications = {synthIndexReached, synthDoneSpeaking}

    @staticmethod
    def _present():
        if not os.path.isfile(DLL):
            return []
        have = {"mini": os.path.isfile(DRIVER),
                "sa": all(os.path.isfile(os.path.join(SA_ROMS, n)) for n in ("u2.BIN", "u3.BIN", "u4.BIN"))}
        return [v for v, _ in VOICES if have[v]]

    @classmethod
    def check(cls):
        return bool(cls._present())

    def __init__(self):
        super().__init__()
        self._rate = 50        # Accent rate 5, its default
        self._pitch = 50       # Accent pitch 5, its default
        self._inflection = 100  # full intonation (M0), its default
        self._volume = 100
        self._voice_char = "5"  # voice characteristic V5, its default
        self._model = self._present()[0]   # which Accent, "mini" or "sa"; the worker boots it
        self._booted = None
        self._join = True
        self._numbers = True
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
        self._phase = ("start", time.monotonic())
        self._worker = threading.Thread(target=self._run, name="accent-ssi263", daemon=True)
        self._worker.start()
        if DEBUG_LOG:
            self._monitor = threading.Thread(target=self._watch, name="accent-ssi263-watch", daemon=True)
            self._monitor.start()

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
                items.append(("pitch", item.offset))
        job = _joined(items) if self._join else items
        _dbg("speak: %d items %s" % (len(job), _summary(job)))
        self._queue.put(job)

    def cancel(self):
        _dbg("cancel (worker in %s)" % self._phase[0])
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

    # -- settings: the Accent's own values -----------------------------------
    def _get_rate(self):
        return self._rate

    def _set_rate(self, v):
        self._rate = max(0, min(100, int(v)))

    def _get_pitch(self):
        return self._pitch

    def _set_pitch(self, v):
        self._pitch = max(0, min(100, int(v)))

    def _get_inflection(self):
        return self._inflection

    def _set_inflection(self, v):
        self._inflection = max(0, min(100, int(v)))

    def _get_joinPhrases(self):
        return self._join

    def _set_joinPhrases(self, v):
        self._join = bool(v)

    def _get_numberWords(self):
        return self._numbers

    def _set_numberWords(self, v):
        self._numbers = bool(v)

    def _get_volume(self):
        return self._volume

    def _set_volume(self, v):
        self._volume = max(0, min(100, int(v)))

    def _get_availableVariants(self):
        return {str(n): StringParameterInfo(str(n), "Voice %d%s" % (n, " (default)" if n == 5 else ""))
                for n in range(10)}

    def _get_variant(self):
        return self._voice_char

    def _set_variant(self, v):
        if v in {str(n) for n in range(10)}:
            self._voice_char = v

    def _get_availableSamplerates(self):
        return {str(r): StringParameterInfo(str(r), rates.LABELS[r]) for r in rates.RATES}

    def _get_sampleRate(self):
        return str(self._want_rate)

    def _set_sampleRate(self, v):
        # the worker applies it before the next utterance: new player, card rebooted at the new rate
        self._want_rate = rates.parse(v) or self._want_rate

    def _get_availableVoices(self):
        names = dict(VOICES)
        return {v: VoiceInfo(v, names[v], "en") for v in self._present()}

    def _get_voice(self):
        return self._model

    def _set_voice(self, v):
        # the worker boots the other machine before its next job (0.4's one voice was "accent")
        if v in self._present():
            self._model = v

    # -- diagnostics (0.3.7): where the worker is, and a thread that reports it stuck --
    def _set_phase(self, name):
        self._phase = (name, time.monotonic())

    def _watch(self):
        reported = None
        while not self._stopped:
            time.sleep(1.0)
            name, since = self._phase
            if name == "waiting for speech" or (name, since) == reported:
                continue
            if time.monotonic() - since > 5.0:
                reported = (name, since)
                frame = sys._current_frames().get(self._worker.ident)
                stack = "".join(traceback.format_stack(frame)) if frame else "(no frame)"
                card = ""
                try:
                    card = self._box.state()
                except Exception:
                    pass
                log.warning("Accent: worker stuck in '%s' for %.1f s; card: %s\n%s"
                            % (name, time.monotonic() - since, card, stack))

    # -- worker: the only thread that touches the emulated card ----------------
    def _boot(self):
        """The card for the current voice at the current rate; its settings are then the Accent's power-up ones
        (rate 5, pitch 5, voice 5, full intonation), and only a setting that differs is sent."""
        t = time.monotonic()
        model = self._model
        if model == "sa":
            box = AccentSAC(DLL, SA_ROMS, self._out_rate)
        else:
            box = AccentMiniC(DLL, DRIVER, self._out_rate)     # its INIT runs as DOS would run it
        old, self._box = self._box, box
        if old is not None:
            old.close()
        self._booted = model
        _dbg("%s booted in %.0f ms" % (model, (time.monotonic() - t) * 1e3))
        return box

    def _switch_rate(self):
        """A new sample rate: the chip renders at the host rate, so a new player and a rebooted card."""
        self._out_rate = self._want_rate
        old, self._player = self._player, self._makePlayer()
        try:
            old.close()
        except Exception:
            pass
        self._boot()

    def _apply(self, box):
        """the settings as they are now; the card is sent those that changed at begin()"""
        box.set(self._rate, self._pitch, self._inflection, self._volume, self._numbers, int(self._voice_char))

    def _run(self):
        try:
            self._boot()
        except Exception:
            log.error("Accent: could not start the emulated card", exc_info=True)
            return
        while not self._stopped:
            self._set_phase("waiting for speech")
            job = self._queue.get()
            if job is None:
                break
            self._cancelFlag.clear()
            if self._want_rate != self._out_rate:
                self._set_phase("switching sample rate")
                try:
                    self._switch_rate()
                except Exception:
                    log.error("Accent: could not switch the sample rate", exc_info=True)
            if self._model != self._booted:
                self._set_phase("switching voice")
                try:
                    self._boot()
                except Exception:
                    log.error("Accent: could not start %s" % self._model, exc_info=True)
            t_job = time.monotonic()
            self._set_phase("speaking")
            try:
                self._speakJob(job)
            except Exception:
                log.error("Accent speech failed; restarting the emulated card", exc_info=True)
                try:
                    self._boot()
                except Exception:
                    log.error("Accent restart failed", exc_info=True)
            if self._cancelFlag.is_set():
                self._set_phase("flushing the card")
                t = time.monotonic()
                try:
                    self._box.cancel()           # the Accent's flush, and the pitch said again if it was dropped
                    _dbg("job cancelled after %.0f ms; flush %.0f ms wall"
                         % ((t - t_job) * 1e3, (time.monotonic() - t) * 1e3))
                except Exception:
                    # 0.3.6 swallowed this and kept a broken card: restart it instead
                    log.error("Accent flush failed; restarting the emulated card", exc_info=True)
                    try:
                        self._boot()
                    except Exception:
                        log.error("Accent restart failed", exc_info=True)
                # a block computed before NVDA's stop may have been fed after it
                try:
                    self._player.stop()
                except Exception:
                    pass
            else:
                _dbg("job done in %.0f ms" % ((time.monotonic() - t_job) * 1e3))
                synthDoneSpeaking.notify(synth=self)

    def _speakJob(self, items):
        box = self._box
        self._apply(box)
        self._set_phase("sending settings")
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
            # currencies ("£2.63": the firmware reads only "$"), clean, strip, and the number words when on
            text = box.prepare(value, self._numbers)
            if not text:
                continue
            t = time.monotonic()
            self._set_phase("sending text")
            # ESC =F: the carriage return starts speech.  In the background: audio starts while
            # the driver is still taking a long text (same steps, same audio as waiting for it)
            box.say(text + "\r")
            _dbg("text %d chars handed over in %.0f ms: %s" % (len(text), (time.monotonic() - t) * 1e3, _short(text)))
            blocks, why = 0, "cancelled"
            while not self._cancelFlag.is_set():
                self._set_phase("running the card")
                pcm, done = box.render()         # 30 ms of the card; its reading time trimmed at the head
                blocks += 1
                if pcm and not self._cancelFlag.is_set():
                    self._set_phase("feeding audio")
                    t = time.monotonic()
                    self._player.feed(pcm)
                    if time.monotonic() - t > 0.5:
                        _dbg("feed blocked %.0f ms" % ((time.monotonic() - t) * 1e3))
                if done:
                    if box.fault:
                        # the watchdog: the card said it was speaking but nothing came out for 4 s
                        log.warning("Accent: card stalled; restarting it. card: %s" % box.state())
                        raise RuntimeError("card stalled")
                    if getattr(box, "limit", False):
                        # issue #8: the Accent SA's firmware was still reading when its wait ran out
                        log.warning("Accent: text stopped at the safety limit, the card still working (the rest flushed). card: %s"
                                    % box.state())
                    why = "done"
                    break
            _dbg("item %s: %d blocks" % (why, blocks))
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
        _dbg("index %s queued" % index)
        cb = lambda: synthIndexReached.notify(synth=self, index=index)   # noqa: E731
        try:
            self._player.feed(b"", onDone=cb)
        except Exception:
            cb()
