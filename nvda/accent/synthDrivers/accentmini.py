# -*- coding: utf-8 -*-
"""NVDA synthesizer driver: the Aicom Accent SA and Accent-mini, talking through an emulated SSI-263.

Two voices, two of Aicom's own programs:
- Accent-mini: Aicom's DOS device driver (SPKEMS.DVC, "Accent-EMS", V4.5, 1986-1993) runs
  under Unicorn inside NVDA's process, loaded as DOS would load it, with its rules in
  emulated expanded memory; the model's A/R request is the card's IRQ.
- Accent SA: the stand-alone box's own 8085 firmware (1986-1989) and dictionary ROMs run
  on an emulated 8085 (accent_sa_host.py), with text arriving on its serial port and A/R
  on its TRAP.
Both drive a register-level model of the SSI-263 (Aicom's AI901), so the rules, intonation
and timing are Aicom's own.  Nothing is recorded or concatenated.

This add-on carries Aicom's driver and firmware; see AICOM.txt beside them.
"""

import os
import queue
import re
import sys
import threading
import time
import traceback

import nvwave
from synthDriverHandler import SynthDriver, VoiceInfo, synthIndexReached, synthDoneSpeaking
from autoSettingsUtils.utils import StringParameterInfo
from autoSettingsUtils.driverSetting import BooleanDriverSetting
from logHandler import log
import speech.commands

_HERE = os.path.dirname(__file__)
_ENGINE_DIR = os.path.join(_HERE, "_ssi263_accent")

# The engine is this add-on's own package, imported relatively and never through sys.path:
# the Speak-Out and Braille Lite add-ons ship an `ssi263` too.
from ._ssi263_accent.accent_host import Accent
from ._ssi263_accent.accent_sa_host import AccentSA
from ._ssi263_accent.ssi263.native import SSI263C      # the chip in C
from ._ssi263_accent import ssi263_numwords as numwords

# Developer switch: True writes a step-by-step "Accent:" trace to NVDA's log (at debug level)
# and starts a thread that reports a stuck worker.  Off for everyone else; not a setting.
DEBUG_LOG = False

OUT_RATE = 44100
BLOCK_S = 0.03
DRIVER = os.path.join(_ENGINE_DIR, "SPKEMS.DVC")
STATE = os.path.join(_ENGINE_DIR, "SPKEMS.state")
SA_ROMS = os.path.join(_ENGINE_DIR, "accent-sa")      # u2.BIN u3.BIN u4.BIN
VOICES = (("mini", "Accent-mini"), ("sa", "Accent SA"))
RATES = "0123456789ABCDEFGH"            # ESC Rn: 0-9, A-H; the Accent's default is 5
# NVDA's inflection 0/25/50/75/100 -> ESC M1 (monotone), M2, M3, M4, M0 (full, the default),
# as the Accent Messenger add-on maps them
INFLECTION = ((0, 1), (25, 2), (50, 3), (75, 4), (100, 0))
DEFAULTS = ("5", 5, 5, 0)               # rate, pitch, voice, intonation after power-up


def _clean(text):
    """7-bit text with no control characters: ESC and Ctrl-X are the Accent's commands.  No
    tilde either: "~/" opens the Accent's phoneme input (manual 4.2), and it then swallows
    everything until the next "~" -- a path like ~/code silenced it."""
    out = []
    for ch in text:
        o = ord(ch)
        if o < 32 or o == 127 or ch == "~":
            out.append(" ")
        elif o < 128:
            out.append(ch)
        else:
            out.append({"‘": "'", "’": "'", "“": '"', "”": '"', "–": "-", "—": "-",
                        "…": "...", "é": "e", "è": "e", "á": "a", "à": "a", "ö": "o", "ü": "u",
                        "ñ": "n", "ç": "c"}.get(ch, " "))
    return "".join(out)


_MONEY = re.compile(r"(\$\d[\d,]*(?:\.\d+)?|\$\.\d+)")


def _numbers(text):
    """Numbers as words.  The Accent reads 100 as "one zero zero" and anything of five
    digits or more digit by digit; only comma-grouped numbers get hundreds and thousands
    (manual 4.1.4).  Dollar amounts stay with the Accent, which reads them properly
    ("$30.50": "thirty dollars and fifty cents", 4.1.3) once the dollars carry the commas
    it counts by: "$6723" alone is "six seven two three dollars"."""
    parts = _MONEY.split(text)
    return "".join(_grouped(p) if k % 2 else numwords.normalise(p) for k, p in enumerate(parts))


def _grouped(money):
    whole, dot, cents = money[1:].partition(".")
    digits = whole.replace(",", "")
    if digits and len(digits) <= 15:                          # the Accent counts to trillions
        whole = format(int(digits), ",")
    return "$" + whole + dot + cents


LEAD_THRESHOLD = 0.003      # chip output; the idle carrier is ~1e-4, speech ~0.1-0.7
LEAD_PREROLL = 220          # samples (5 ms) kept before the first sound


def _trim_lead(y):
    """Drop the silence at the head of an utterance: the driver reading its text and a
    stop's closure are silent, and a listener hears them only as delay."""
    for k, v in enumerate(y):
        if v > LEAD_THRESHOLD or v < -LEAD_THRESHOLD:
            return y[max(0, k - LEAD_PREROLL):], True
    return y[:0], False


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
    )
    supportedCommands = {speech.commands.IndexCommand, speech.commands.PitchCommand}
    supportedNotifications = {synthIndexReached, synthDoneSpeaking}

    @staticmethod
    def _present():
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
        self._pitch_dirty = False
        self._snap_until_speech = False
        self._sent = DEFAULTS   # the driver boots with these; nothing is sent until one changes
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

    def _get_availableVoices(self):
        names = dict(VOICES)
        return {v: VoiceInfo(v, names[v], "en") for v in self._present()}

    def _get_voice(self):
        return self._model

    def _set_voice(self, v):
        # the worker boots the other machine before its next job (0.4's one voice was "accent")
        if v in self._present():
            self._model = v

    @staticmethod
    def _accent_pitch(p):
        p = max(0, min(100, p))
        return int(p * 5 / 50 + 0.5) if p <= 50 else 5 + int((p - 50) * 4 / 50 + 0.5)      # 50 -> 5

    def _accent_settings(self):
        r = self._rate
        rate = int(r * 5 / 50 + 0.5) if r <= 50 else 5 + int((r - 50) * 12 / 50 + 0.5)    # 50 -> 5
        infl = min(INFLECTION, key=lambda x: abs(x[0] - self._inflection))[1]
        return (RATES[rate], self._accent_pitch(self._pitch), int(self._voice_char), infl)

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
        t = time.monotonic()
        model = self._model
        if model == "sa":
            box = AccentSA(SA_ROMS, chip=SSI263C(out_rate=OUT_RATE), out_rate=OUT_RATE)
            box.keep_writes = False
            box.trace = lambda msg: _dbg("SA: " + msg)
            box.boot()
        else:
            box = Accent(DRIVER, chip=SSI263C(out_rate=OUT_RATE), out_rate=OUT_RATE)
            box.keep_writes = False
            box.trace = lambda msg: _dbg("card: " + msg)
            box.boot(state=self._saved_state())
        self._booted = model
        _dbg("%s booted in %.0f ms" % (model, (time.monotonic() - t) * 1e3))
        return box

    @staticmethod
    def _saved_state():
        """The machine after the driver's INIT, saved at build time: launch in a few ms
        instead of 0.6 s.  None (so INIT runs) if it is missing or made from another driver."""
        try:
            import hashlib
            import pickle
            with open(STATE, "rb") as f:
                state = pickle.load(f)
            with open(DRIVER, "rb") as f:
                if state.get("dvc_sha256") != hashlib.sha256(f.read()).hexdigest():
                    return None
            return state
        except Exception:
            return None

    def _run(self):
        try:
            self._box = self._boot()
        except Exception:
            log.error("Accent: could not start the emulated card", exc_info=True)
            return
        while not self._stopped:
            self._set_phase("waiting for speech")
            job = self._queue.get()
            if job is None:
                break
            self._cancelFlag.clear()
            if self._model != self._booted:
                self._set_phase("switching voice")
                try:
                    self._box = self._boot()
                    self._sent = DEFAULTS
                except Exception:
                    log.error("Accent: could not start %s" % self._model, exc_info=True)
            t_job = time.monotonic()
            self._set_phase("speaking")
            try:
                self._speakJob(job)
            except Exception:
                log.error("Accent speech failed; restarting the emulated card", exc_info=True)
                try:
                    self._box = self._boot()
                    self._sent = DEFAULTS
                except Exception:
                    log.error("Accent restart failed", exc_info=True)
            if self._cancelFlag.is_set():
                self._set_phase("flushing the card")
                t = time.monotonic()
                try:
                    took = self._box.cancel()
                    if self._pitch_dirty:
                        self._resend_pitch()
                    _dbg("job cancelled after %.0f ms; flush %.3f s card time, %.0f ms wall"
                         % ((t - t_job) * 1e3, took or 0.0, (time.monotonic() - t) * 1e3))
                except Exception:
                    # 0.3.6 swallowed this and kept a broken card: restart it instead
                    log.error("Accent flush failed; restarting the emulated card", exc_info=True)
                    try:
                        self._box = self._boot()
                        self._sent = DEFAULTS
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
        settings = self._accent_settings()
        if settings != self._sent:
            # only what changed: repeated option commands emit preparation records of their own
            cmd = "".join("\x1b%s%s" % (letter, value) for letter, value, old in
                          zip("RPVM", settings, self._sent) if value != old)
            _dbg("settings %s" % ascii(cmd))
            self._set_phase("sending settings")
            box.say(cmd, speech=False)
            self._sent = settings
        self._cur_pitch = settings[1]
        gain = self._volume / 100.0
        self._lead = True
        try:
            self._speakItems(items, box, gain)
        finally:
            # restore the user's pitch only after the capital's audio exists
            if self._cur_pitch != settings[1]:
                box.chip.snap_pitch = True
                box.say("\x1bP%d" % settings[1], speech=False)
                self._cur_pitch = settings[1]
                self._pitch_dirty = True

    def _speakItems(self, items, box, gain):
        for kind, value in items:
            if self._cancelFlag.is_set():
                return
            if kind == "index":
                self._notifyIndex(value)
                continue
            if kind == "pitch":
                want = self._accent_pitch(self._pitch + (value or 0))
                if want != self._cur_pitch:
                    box.chip.snap_pitch = True
                    box.say("\x1bP%d" % want, speech=False)
                    self._cur_pitch = want
                    self._pitch_dirty = True
                continue
            text = _clean(value).strip()
            if self._numbers:
                text = _numbers(text)
            if not text:
                continue
            t_start = box.chip.time
            t = time.monotonic()
            self._set_phase("sending text")
            # ESC =F: the carriage return starts speech.  In the background: audio starts while
            # the driver is still taking a long text (same steps, same audio as waiting for it)
            box.say(text + "\r", background=True)
            _dbg("text %d chars handed over in %.0f ms: %s" % (len(text), (time.monotonic() - t) * 1e3, _short(text)))
            blocks, audio, why = 0, 0.0, "cancelled"
            while not self._cancelFlag.is_set():
                self._set_phase("running the card")
                y = box.run(BLOCK_S)
                blocks += 1
                audio += len(y) / float(OUT_RATE)
                if self._lead:
                    y, found = _trim_lead(y)
                    self._lead = not found
                if len(y) and not self._cancelFlag.is_set():
                    self._set_phase("feeding audio")
                    t = time.monotonic()
                    self._player.feed(box.chip.dsp.pcm16(y, gain))
                    if time.monotonic() - t > 0.5:
                        _dbg("feed blocked %.0f ms" % ((time.monotonic() - t) * 1e3))
                if self._snap_until_speech and box.last_speech >= t_start:
                    box.chip.snap_pitch = False
                    self._snap_until_speech = False
                if not box.busy():
                    self._pitch_dirty = False     # the driver has taken everything sent so far
                    why = "done"
                    break
                # watchdog: the card says it is speaking but nothing has come out for 4 s
                quiet = box.chip.time - max(box.last_speech, t_start)
                if box.speaking and quiet > 4.0:
                    log.warning("Accent: card silent %.1f s while busy; restarting it. card: %s"
                                % (quiet, box.state()))
                    raise RuntimeError("card stalled")
            _dbg("item %s: %d blocks, %.2f s audio" % (why, blocks, audio))
        if not self._cancelFlag.is_set() and self._queue.empty():
            try:
                self._player.idle()
            except Exception:
                pass

    def _resend_pitch(self):
        """A cancel drops whatever the driver has not taken yet, pitch commands with it;
        say the user's pitch again (see the Speak-Out driver)."""
        base = self._sent[1]
        self._box.chip.snap_pitch = True
        self._snap_until_speech = True
        self._box.say("\x1bP%d" % base, speech=False)
        self._cur_pitch = base

    def _notifyIndex(self, index):
        _dbg("index %s queued" % index)
        cb = lambda: synthIndexReached.notify(synth=self, index=index)   # noqa: E731
        try:
            self._player.feed(b"", onDone=cb)
        except Exception:
            cb()
