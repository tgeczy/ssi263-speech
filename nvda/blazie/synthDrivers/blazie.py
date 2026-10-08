# -*- coding: utf-8 -*-
"""NVDA synthesizer driver: a Blazie Braille Lite 2000 (June 2003 firmware) in speech-box
mode, talking through an emulated SSI-263.

The unit's own firmware runs on MAME's Z180 in the native board library; its
register writes drive a register-level SSI-263 model here, and the model's A/R request
drives the firmware back, so its rules, number reading and inflection are the
originals, live.  Nothing is recorded or concatenated.

This add-on carries the Braille Lite's firmware, shared with permission; it is not ours.
"""

import math
import os
import queue
import re
import sys
import threading
import time
from array import array

import nvwave
from synthDriverHandler import SynthDriver, VoiceInfo, synthIndexReached, synthDoneSpeaking
from autoSettingsUtils.utils import StringParameterInfo
from autoSettingsUtils.driverSetting import BooleanDriverSetting, DriverSetting
from logHandler import log
import speech.commands

_HERE = os.path.dirname(__file__)
_ENGINE_DIR = os.path.join(_HERE, "_ssi263_blazie")

# The engine is this add-on's own package, imported relatively and never through sys.path:
# the Speak-Out add-on ships an `ssi263` too, and one process has one module per name.
from ._ssi263_blazie.blazie_host import Blazie
from ._ssi263_blazie import ssi263_numwords as numwords
from ._ssi263_blazie import ssi263_rates as rates
from ._ssi263_blazie import blazie_idle
from ._ssi263_blazie.ssi263.native import SSI263C      # the chip in C
from ._ssi263_blazie.native_blazie import NativeBlazie

BLOCK_S = 0.03
# After speech, with "keep the channel open" and the hiss or whine on, the unit keeps running in real time: its
# channel stays open, the hiss or whine goes on, and the firmware clicks it off itself (R3 = 00, ~9.95 s after the
# last phoneme on Tomi's unit, and the emulated firmware's own timer).  The idle audio is fed this far ahead of the
# player, so new speech drops at most this much of it.
IDLE_AHEAD_S = 0.12
TAIL_KEEP_QUEUED = False           # True: new speech waits behind the idle audio already queued (0.7 draft; a test's control)
TAIL_PLAIN_WAIT = False            # True: wait for the last speech's onDone without servicing callbacks (a test's control)
PLAYED_POLL_S = 0.005              # while waiting for it: how often an empty feed lets the player fire what is due
# The click when the firmware clicks the open channel off (Tomi, 0.7: with keep open only): Tomi's unit's, measured
# (blazie_idle.CLICK, the emulator's too).  Its audible part is the first ~0.1 s; the rest is a slow drift (the 0.49 s
# decay), faded out by CLICK_S so that speech coming just then does not wait behind it.
CLICK_S = 0.25
NO_CLICK = False                   # True: no click (a test's control)
CLICK_FADE_S = 0.15


def _click(rate):
    """The click-off, in chip units at `rate`: a step through two first-order high-passes, then faded out."""
    a, t1, t2 = blazie_idle.CLICK
    a *= blazie_idle.REF_RMS
    w1, w2 = 1.0 / t1, 1.0 / t2
    m1, m2 = a * w1 / (w1 - w2), a * w2 / (w1 - w2)
    e1, e2 = math.exp(-1.0 / (t1 * rate)), math.exp(-1.0 / (t2 * rate))
    n, nf = int(CLICK_S * rate), int(CLICK_FADE_S * rate)
    out = array("d", bytes(8 * n))
    for i in range(n):
        v = m1 - m2
        if i >= n - nf:
            v *= 0.5 + 0.5 * math.cos(math.pi * (i - (n - nf)) / nf)
        out[i] = v
        m1 *= e1
        m2 *= e2
    return out
_now = time.perf_counter           # the idle tail's clock (a test may speed it up)
EXE = os.path.join(_ENGINE_DIR, "bns_live.exe")
# 0.7: the unit in-process (bl.dll: the Z180, the board and the host lockstep in C, for this Python's bitness):
# no child process and no pipe. SSI263_BLAZIE_PIPE=1 explicitly selects the MAME
# pipe reference for development comparisons; a native failure never selects it silently.
DLL = os.path.join(_ENGINE_DIR, "bin", "x64" if sys.maxsize > 2 ** 32 else "x86", "ssi263speech.dll")
FIRMWARE = os.path.join(_ENGINE_DIR, "BL2ENG.BNS")
STATE = os.path.join(_ENGINE_DIR, "bl2_2003_warm.state")
# The Spanish Braille Lite 2000 (ONCE's BL2SPA.BNS) from a full-reset snapshot; optional: the voice is offered only
# when both files are present.  It reads DOS code page 850 (its braille table's accents: a-acute A0, n-tilde A4 ...).
FIRMWARE_ES = os.path.join(_ENGINE_DIR, "BL2SPA.BNS")
STATE_ES = os.path.join(_ENGINE_DIR, "bl2spa_fresh.state")
# voice id: (display name, language, firmware, snapshot, text encoding)
VOICES = {"blazie": ("Braille Lite 2000 (June 2003)", "en", FIRMWARE, STATE, "latin-1"),
          "blazie_es": ("Braille Lite 2000 (espa\u00f1ol)", "es", FIRMWARE_ES, STATE_ES, "cp850")}
# the voices whose firmware run ahead's interaction contract has been tested on (Astra, Reply 107): the mechanism is
# board-independent, its use is not -- another release or unit gets the lockstep until its own tests exist
RUN_AHEAD_TESTED = ("blazie", "blazie_es")
UNIT_VOLUME = 6         # the unit's factory volume; NVDA's slider is applied digitally
MAKEUP = 2.0            # +6 dB so volume 6 sits at a normal level
# A roll-off after the chip (hosts/blazie.py) that matches the unit's line out: first order at 5 kHz matched
# its line out on Reclaim and the MASTER sentences, and won Tomi's A/B ("9E really wins").
BOARD_LOWPASS_HZ = 5000.0
# The stops' noise may build behind the shut gate for this long before a release (engine closure_noise_lead_ms;
# Astra Reply 59, the A/B Tomi liked; checked at rates 1-15, the gate always fully shut when it starts).
CLOSURE_NOISE_LEAD_MS = 10.0
# Factory settings after a warm reset (MASTER, confirmed on tape): rate 11, tone 7, and
# r1 = 45h (81.4 Hz, the pitch of Tomi's Braille 'n Speak 2000 recording).  NVDA's slider
# midpoints map onto them, and nothing is sent until a slider moves.
DEFAULT_RATE, DEFAULT_PITCH, DEFAULT_TONE = 11, 16, 7
# Tones: the unit's frequency chords (dots 23 / 56) step 0-16, default 7, and the value goes straight to the chip
# (R4 = E0h + tone).  In speech-box mode ^E n T takes 0-31 unchecked (32 wraps to 0), so a screen reader could
# send more than the chords allow; Tomi hears 17-26 on the unit that way.  0-26 (27-31 put the filter clock
# at 100-500 kHz).
TONES = range(0, 27)
# The unit's idle sound, generated from the chip's clock (hosts/blazie.py whine_wave): off, the slight hiss of even
# volumes (the unit's factory volume 6), or the whine of odd volumes (Tomi's recordings, 2026-09-27).
WHINES = (("off", "Off"), ("hiss", "Hiss (even volumes, as the factory setting)"), ("whine", "Whine (odd volumes)"))
# The firmware takes ^E n E modulo 16 (measured on the unit, and the same emulated): rate 16
# speaks at rate 10's speed, so the fastest rate is 15.
MAX_RATE = 15


_translit_fn = None


def _translit(text, encoding="latin-1"):
    """The letters the unit's alphabet lacks, before everything else (src/csrc/translit.h, the C voices' own pass,
    from the add-on's ssi263speech.dll): "tükör" -> "tukor", a lone "á" -> "a acute"; for the Spanish unit only what
    cp850 lacks ("ő").  Without the library (it is the unit too) the text is left as it was."""
    global _translit_fn
    if _translit_fn is None:
        try:
            import ctypes
            fn = ctypes.CDLL(DLL).ssv_translit
            fn.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
            fn.restype = ctypes.c_int
            _translit_fn = (ctypes, fn)
        except (OSError, AttributeError):
            _translit_fn = False
    if not _translit_fn or not text:
        return text
    ctypes, fn = _translit_fn
    raw = text.encode("utf-8", "surrogatepass")
    charset = 1 if encoding == "cp850" else 0                # voices.h SSV_CP850 / SSV_ASCII
    n = fn(raw, len(raw), charset, None, 0)
    if n < 0:
        return text
    out = ctypes.create_string_buffer(n + 1)
    fn(raw, len(raw), charset, out, n + 1)
    return out.raw[:n].decode("utf-8", "surrogatepass")


def _clean(text, encoding="latin-1"):
    """7-bit text for the English unit; for the Spanish one also every character its code page has (accents,
    n-tilde, the inverted marks)."""
    out = []
    for ch in text:
        o = ord(ch)
        if o < 32 or o == 127:
            out.append(" ")
        elif o < 128:
            out.append(ch)
        elif encoding == "cp850" and _cp850(ch):
            out.append(ch)
        else:
            out.append({"‘": "'", "’": "'", "“": '"', "”": '"',
                        "–": "-", "—": "-", "…": "..."}.get(ch, " "))
    return "".join(out)


def _cp850(ch):
    try:
        ch.encode("cp850")
        return True
    except UnicodeEncodeError:
        return False


def _numbers(text, lang="en"):
    """Numbers as words, except money: the firmware says "$25" as "twenty five dollars" and
    "$1,234.56" with its cents, but drops a "$" in front of words, so "$twenty five" was
    "twenty five" (a tester, 0.5.0).  Dollar amounts go to the firmware as they are, up to
    $999,999,999,999; from a trillion up the firmware says "billion" (measured), so those
    become words here."""
    if lang == "es":
        # Spain's convention (ONCE's firmware): "3,5" = tres coma cinco, "1.234.567" one number
        return numwords.normalise(text, lang="es", decimal_comma=True)
    parts = numwords.MONEY.split(text)
    return "".join(_money(p) if k % 2 else numwords.normalise(p) for k, p in enumerate(parts))


def _money(amount):
    whole, _, cents = amount[1:].partition(".")
    whole = whole.replace(",", "")
    if len(whole.lstrip("0")) <= 12:
        return amount
    out = numwords.normalise(whole) + " dollars"
    if len(cents) == 2:
        out += " " + numwords.cardinal(int(cents)) + " cents"
    elif cents:
        out += " point " + numwords.digits(cents)
    return out


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


def _nothing():
    """onDone for the end-of-utterance flush: the call is what matters, not the callback."""


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
        BooleanDriverSetting("voiceInflection", "Voice &inflection (the unit's own on/off)", defaultVal=True),
        DriverSetting("whine", "Unit &hiss and whine", defaultVal="off"),
        BooleanDriverSetting("keepOpen", "&Keep the channel open after speaking (the hiss or whine until the unit "
                             "clicks off)", defaultVal=True),
        DriverSetting(rates.SETTING_ID, rates.SETTING_LABEL, defaultVal=str(rates.DEFAULT)),
        # EXPERIMENTAL, off by default: the unit run ahead of the chip (src/csrc/blazie/run_ahead.h; Astra, Reply 107)
        # -- a bounded streaming capture (up to 16 segments ahead) replayed at the unit's own answer times, the
        # reading pauses cut; "done" once a pause after the last spoken phoneme has ended.  Its end is inferred from
        # the ^F echo accounting and a quiet interval (a policy).  The same register values on the tested sessions
        # (nvda/tools/run_ahead_equiv.py, run_ahead_lanes.py, run_ahead_state.py).  A cancel mid-utterance once left
        # cancelled text at the head of the next one (English: 4 of 25 cancel times; through this driver, long lines
        # cut 0.3-0.4 s in): fixed in the host's cancel (run_ahead.h ra_settle; run_ahead_cancel.py, 0 of 172, and
        # run_ahead_driver.py).  With "short pauses" on only, the in-process unit only, and only the voices whose
        # firmware those tests cover (RUN_AHEAD_TESTED).
        BooleanDriverSetting("runAhead", "&Run the unit ahead (experimental: with short pauses)", defaultVal=False),
    )
    # LangChangeCommand: NVDA's automatic language switching (and MultiLang passing a language on) sends each
    # stretch of text to the unit for its language
    supportedCommands = {speech.commands.IndexCommand, speech.commands.PitchCommand, speech.commands.LangChangeCommand}
    supportedNotifications = {synthIndexReached, synthDoneSpeaking}

    @classmethod
    def check(cls):
        return all(os.path.isfile(p) for p in (FIRMWARE, STATE, DLL))

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
        # Defaults only: NEVER read config.conf["speech"][<driver>] here.  NVDA registers this driver's settings
        # after __init__, and its config caches a failed lookup as missing, so an early read of a new key made
        # NVDA's own loadSettings fail ("setSynth failed ... KeyError: 'voiceInflection'", Tomi, 0.6.0 draft).
        # NVDA applies the saved values through the setters right after; the worker restarts once if needed.
        self._out_rate = self._want_rate = rates.DEFAULT   # the worker switches to _want_rate
        self._infl = self._want_infl = True                # likewise to _want_infl
        self._whine = self._want_whine = "off"
        self._keep_open = True
        self._play_end = 0.0             # when the listener will have heard everything fed (_feed); 0: nothing queued
        self._run_ahead = False
        self._player = self._makePlayer()
        self._queue = queue.Queue()
        self._cancelFlag = threading.Event()
        self._wake = threading.Event()   # speak() / cancel() wake the idle tail
        self._stopped = False
        self._unit = None
        self._units = {}        # voice id -> its emulated unit, started on first use
        self._voice = "blazie"
        self._worker = threading.Thread(target=self._run, name="blazie-ssi263", daemon=True)
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
            elif isinstance(item, speech.commands.LangChangeCommand):
                items.append(("lang", item.lang))
        self._queue.put(_joined(items) if self._join else items)
        self._wake.set()

    def cancel(self):
        self._cancelFlag.set()
        self._wake.set()
        try:
            self._player.stop()
        except Exception:
            pass
        self._play_end = 0.0
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
                for t in TONES}

    def _get_variant(self):
        return self._tone

    def _set_variant(self, v):
        if v in {str(t) for t in TONES}:
            self._tone = v

    def _get_availableSamplerates(self):
        return {str(r): StringParameterInfo(str(r), rates.LABELS[r]) for r in rates.RATES}

    def _get_voiceInflection(self):
        return self._want_infl

    def _set_voiceInflection(self, v):
        # the unit's status-menu setting ("Voice inflection: i y/n"), which no ^E command reaches: the worker
        # applies it before the next utterance by starting the unit again with the menu keys (silently)
        self._want_infl = str(v).strip().lower() not in ("false", "0", "no", "off") if isinstance(v, str) else bool(v)

    def _get_availableWhines(self):
        return {k: StringParameterInfo(k, label) for k, label in WHINES}

    def _get_whine(self):
        return self._want_whine

    def _set_whine(self, v):
        # the host's measured whine (hosts/blazie.py whine_wave) replaces the chip's two-sine carrier, a chip
        # parameter, so the worker restarts the unit (silently) before the next utterance
        if v in dict(WHINES):
            self._want_whine = v

    def _get_runAhead(self):
        return self._run_ahead

    def _set_runAhead(self, v):
        self._run_ahead = bool(v)        # the worker applies it at the next utterance (_speakSegment)

    def _get_keepOpen(self):
        return self._keep_open

    def _set_keepOpen(self, v):
        self._keep_open = bool(v)

    def _get_sampleRate(self):
        return str(self._want_rate)

    def _set_sampleRate(self, v):
        # the worker applies it before the next utterance: new player, unit rebooted at the new rate
        self._want_rate = rates.parse(v) or self._want_rate

    @staticmethod
    def _present():
        return [v for v, (_, _, fw, st, _) in VOICES.items() if os.path.isfile(fw) and os.path.isfile(st)]

    def _get_availableVoices(self):
        return {v: VoiceInfo(v, VOICES[v][0], VOICES[v][1]) for v in self._present()}

    def _get_voice(self):
        return self._voice

    def _set_voice(self, v):
        # the worker starts that voice's unit before the next utterance (the English one runs from the start)
        if v in self._present():
            self._voice = v

    def _voice_for(self, lang):
        """The unit for a language NVDA marked (None: the chosen voice's)."""
        if not lang:
            return self._voice
        base = lang.replace("-", "_").split("_")[0].lower()
        for v in self._present():
            if VOICES[v][1] == base:
                return v
        return self._voice

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
    def _boot(self, voice="blazie"):
        # In the unit's speech menu before speech-box mode: punctuation NONE, because NVDA
        # names symbols itself and also passes some through ("left paren (") -- the unit
        # read those too: "eti dash dash eloquence", "left paren left paren" (Tomi, 0.2.0).
        # And full numbers: the snapshot reads every number digit by digit.
        # Keys 3M instructions after power-on, then 1.5M apart: the launch drops from 1.03 s
        # to 0.25 s.  The unit then writes exactly what it did with the MASTER harness's
        # 8M/10M (tools/boot_gaps.py); below a 2.5M start it never reaches speech-box mode.
        # The emulator is deterministic, so this holds on every machine.
        # the bounded T/P/K precharge Tomi chose by ear ("more bursty without being thicker", 2026-09-27): noise builds
        # behind the fully shut gate over the last 10 ms before a release
        params = {"closure_noise_lead_ms": CLOSURE_NOISE_LEAD_MS}
        if self._whine != "off":
            params["carrier_rel_db"] = -300.0     # the whine model carries the carrier's lines
        chip = SSI263C(params=params, out_rate=self._out_rate)
        _name, lang, firmware, state, encoding = VOICES[voice]
        kw = dict(chip=chip, out_rate=self._out_rate, menu=("punct_none", "numbers_toggle"), key_start=3000000,
                  key_gap=1500000, board_lowpass_hz=BOARD_LOWPASS_HZ,
                  # the snapshot has inflection on; only turning it off needs the status-menu keys
                  status=() if self._infl else ("inflection_off",))
        if os.environ.get("SSI263_BLAZIE_PIPE") == "1":     # development comparison, never an automatic fallback
            unit = Blazie(EXE, firmware, state, **kw)
        else:
            unit = NativeBlazie(DLL, firmware, state, **kw)
        unit.send(b"\x18")
        unit.send(b"\r\x06")
        unit.run(0.3)
        unit.send(b"\x05%dV" % UNIT_VOLUME)
        unit.run(0.05)
        unit.whine = None if self._whine == "off" else self._whine
        unit.encoding = encoding
        unit.lang = lang
        unit.voice = voice
        unit.sent_settings = (DEFAULT_RATE, DEFAULT_PITCH, DEFAULT_TONE)   # the unit boots with these
        return unit

    def _unit_for(self, voice):
        unit = self._units.get(voice)
        if unit is None:
            unit = self._units[voice] = self._boot(voice)
        return unit

    def _close_units(self):
        for unit in self._units.values():
            try:
                unit.close()
            except Exception:
                pass
        self._units = {}
        self._unit = None

    def _run(self):
        try:
            self._unit = self._unit_for("blazie")
        except Exception:
            log.error("Blazie: could not start the emulated unit", exc_info=True)
            return
        while not self._stopped:
            job = self._queue.get()
            if job is None:
                break
            self._cancelFlag.clear()
            if self._want_rate != self._out_rate or self._want_infl != self._infl or self._want_whine != self._whine:
                try:
                    self._switch_rate()
                except Exception:
                    log.error("Blazie: could not apply the sample rate / inflection", exc_info=True)
            try:
                self._speakJob(job)
            except Exception:
                log.error("Blazie speech failed; restarting the emulated unit", exc_info=True)
                try:
                    self._close_units()
                    self._unit = self._unit_for(self._voice)
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
                self._play_end = 0.0
            elif self._tail_wanted():
                # done once the speech has played (the tail's audio follows it), then the open channel
                played = threading.Event()

                def done(played=played):
                    played.set()
                    synthDoneSpeaking.notify(synth=self)
                try:
                    self._player.feed(b"", onDone=done)
                except Exception:
                    done()
                try:
                    self._idle_tail(played)
                except Exception:
                    log.error("Blazie: the idle tail failed", exc_info=True)
            else:
                synthDoneSpeaking.notify(synth=self)
        self._close_units()

    def _feed(self, pcm, rate):
        """Feed audio, keeping the time the listener will have heard all of it (a device plays from the first feed,
        in real time, each block after the one before): the idle tail paces itself on that."""
        now = _now()
        self._play_end = max(now, self._play_end) + len(pcm) / 2.0 / rate
        self._player.feed(pcm)

    def _tail_ahead(self, fed, start):
        """How far the fed audio runs ahead of the listener.  Counted from what has been heard, the speech's own
        unplayed audio included: counted from the tail's start (0.6.0), the idle audio queued in front of new speech
        that came without a cancel grew by the speech still playing (387 ms against 20 ms without the tail,
        tools/tail_latency.py)."""
        return self._play_end - _now()

    def _tail_wanted(self):
        return self._keep_open and self._whine != "off" and self._queue.empty() and not self._cancelFlag.is_set()

    def _idle_tail(self, played=None):
        """The unit after speech, in real time: its channel stays open with the hiss or whine until the firmware
        clicks it off (R3 = 00), or until new speech or a cancel.  New speech that comes without a cancel waits
        until the last utterance has played -- `played`, set by the player's own onDone at its end -- and then the
        idle audio still queued (up to IDLE_AHEAD_S and a block) is dropped, so the new speech does not wait behind
        it (Tomi, 0.7: a longer pause with keep open; 141 ms against 18 without the tail, tools/tail_latency.py).
        Stopping before `played` would drop the end of the last utterance and its done.  A cancel stops it
        (cancel() did, on NVDA's thread; again here, after the last feed).  The unit's own time runs meanwhile, as
        on the real unit: the next utterance finds it open or clicked off, whichever the firmware decided."""
        unit = self._unit
        if unit is None:
            return
        gain = MAKEUP * self._volume / 100.0
        rate = float(unit.chip.out_rate)
        start, fed, clicked = _now(), 0.0, False
        self._wake.clear()
        while not self._stopped and self._queue.empty() and not self._cancelFlag.is_set():
            if unit.chip.regs[3] == 0:
                clicked = True           # clicked off: the firmware's own end of the open channel
                break
            ahead = self._tail_ahead(fed, start)
            if ahead > IDLE_AHEAD_S:
                # the speech may still be playing: look again soon (the clock may be a test's, sped up)
                self._wake.wait(min(ahead - IDLE_AHEAD_S / 2, 0.02))
                self._wake.clear()
                continue
            y = unit.run(BLOCK_S)
            fed += len(y) / rate
            if len(y) and self._queue.empty() and not self._cancelFlag.is_set():
                self._feed(unit.chip.dsp.pcm16(y, gain), rate)
        try:
            if self._cancelFlag.is_set():
                self._player.stop()
            elif self._queue.empty():
                if clicked and not NO_CLICK:
                    self._feed(unit.chip.dsp.pcm16(_click(rate), gain), rate)
                self._player.idle()      # clicked off (or the driver stopping)
            elif fed and played is not None and not TAIL_KEEP_QUEUED and self._await_played(played):
                self._player.stop()      # new speech: only idle audio is left in the player
                self._play_end = 0.0
        except Exception:
            pass
        if self._cancelFlag.is_set() or self._queue.empty():
            self._play_end = 0.0

    def _await_played(self, played):
        """True once the last utterance's own onDone has come (`played`); False on a cancel, the driver stopping, or a
        timeout -- never an invented "played".  NVDA's WASAPI player calls onDone only from feed() and sync() on the
        feeding thread (nvdaHelper/local/wasapi.cpp: maybeFireCallback), and this thread feeds nothing while it
        waits, so it services them itself: an empty feed fires the ones due, and adds nothing (Astra, Reply 109:
        a plain wait on the event let the done go undelivered for its whole timeout, ~0.57 s, even after a cancel)."""
        deadline = time.perf_counter() + max(0.0, self._play_end - _now()) + 0.5
        while not played.is_set():
            if self._cancelFlag.is_set() or self._stopped or time.perf_counter() > deadline:
                return False
            if TAIL_PLAIN_WAIT:
                played.wait(max(0.0, deadline - time.perf_counter()))    # the Reply 109 bug (a test's control)
                continue
            try:
                self._player.feed(b"")
            except Exception:
                return False
            played.wait(PLAYED_POLL_S)
        return not self._cancelFlag.is_set()

    def _switch_rate(self):
        """A new sample rate or inflection setting: a rebooted unit (and, for a new rate, a new player, since the
        chip renders at the host rate).  The unit's boot is silent; its audio is never fed."""
        if self._want_rate != self._out_rate:
            self._out_rate = self._want_rate
            old, self._player = self._player, self._makePlayer()
            try:
                old.close()
            except Exception:
                pass
        self._infl = self._want_infl
        self._whine = self._want_whine
        self._close_units()
        self._unit = self._unit_for(self._voice)
        self._pitch_dirty = False

    def _speakJob(self, items):
        """One utterance: split where NVDA changes the language, each stretch to its own unit."""
        segs, voice = [], self._voice
        for kind, value in items:
            if kind == "lang":
                voice = self._voice_for(value)
                continue
            if segs and segs[-1][0] == voice:
                segs[-1][1].append((kind, value))
            else:
                segs.append((voice, [(kind, value)]))
        for voice, part in segs:
            if self._cancelFlag.is_set():
                return
            self._unit = self._unit_for(voice)
            self._speakSegment(part)

    def _speakSegment(self, items):
        unit = self._unit
        settings = self._unit_settings()
        if settings != unit.sent_settings:
            unit.send(b"\x05%dE\x05%dP\x05%dT" % settings)
            unit.run(0.02)
            unit.sent_settings = settings
        gain = MAKEUP * self._volume / 100.0
        self._cur_pitch = settings[1]
        self._lead = True                # nothing audible fed yet in this utterance
        if hasattr(type(unit), "run_ahead"):                 # the in-process unit (the pipe host has no such mode)
            unit.run_ahead = 1 if (self._run_ahead and self._short
                                   and getattr(unit, "voice", None) in RUN_AHEAD_TESTED) else 0
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
            # "£2.63": the firmware reads only "$" (and the English unit has no pound sign at all); first the letters
            # its alphabet lacks, as their base letters (or a lone one's words)
            text = _clean(numwords.currencies(_translit(value, unit.encoding), unit.lang), unit.encoding)
            if self._numbers:
                # with the add-on's boot the firmware counts to 999,999,999,999 and says a
                # trillion as "one billion" (measured); this is for those
                text = _numbers(text, unit.lang)
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
                    self._feed(pcm, unit.chip.out_rate)
                if self._snap_until_speech and unit.last_speech >= t_start:
                    # the first phoneme is set up; a leftover snap would flatten a glide later
                    unit.chip.snap_pitch = False
                    self._snap_until_speech = False
                if not unit.busy():
                    self._pitch_dirty = False    # the unit has read everything sent so far
                    break
        if self._cancelFlag.is_set():
            return
        try:
            if self._tail_wanted():
                pass                     # the idle tail follows without a gap (_run)
            elif self._queue.empty():
                self._player.idle()
            else:
                # NVDA 2021-2023's buffered player holds blocks until it has 300 ms of them,
                # unless a feed carries onDone: without this the last one waited there
                # and came out at the head of the next utterance (a tester, 0.5.0)
                self._player.feed(b"", onDone=_nothing)
        except Exception:
            pass

    def _resend_pitch(self):
        """A cancel drops whatever the unit has not read yet, pitch commands with it: a
        capital cut off mid-word left every later word high, because the driver thought
        the pitch was back (Tomi, 0.2.0).  Say the user's pitch again, and have it land
        at once on the next utterance's first phoneme instead of gliding down into it."""
        base = self._unit.sent_settings[1]
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
