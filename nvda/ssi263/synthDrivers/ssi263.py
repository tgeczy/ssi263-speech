# -*- coding: utf-8 -*-
"""NVDA synthesizer driver: Votrax SC-02 / SSI-263 (emulated) -- every unit of this project in one driver (0.8).

The units are the 0.7 add-ons' own drivers, unchanged, in this add-on's private package (synthDrivers/_ssi263_unified:
blazie.py, speakout.py, accentmini.py, and mockingboard.py, each with its engine; NVDA lists no module whose name
starts with "_").  This driver runs ONE of them at a time, chosen by the firmware type, and passes NVDA's speech,
settings and notifications through: what you hear is that driver's, byte for byte (nvda/tools/unified_driver_equiv.py).

Settings (investigation/design-0.8-nvda-addon.md, its revisions after Astra's Reply 161, and Reply 163):
  firmware type  the units whose firmware is here (Braille Lite 2000, Speak-Out, Accent SA, Accent-mini, Mockingboard)
  voice          that firmware's languages, with their language codes, so NVDA's language switching works
  the rest       the unit's own settings, as its 0.7 add-on had them
Each firmware type keeps its own values (Tomi).  Where they are kept is ONE place: NVDA's config, this driver's
section, the keys fw_<type>_<setting> (profile-aware key by key, so a profile that sets only the Braille Lite's
rate inherits everything else).  The type itself is the plain key firmwareType.  The plain keys of the units'
settings (rate, voice ...) are never read: NVDA's settings ring writes the one it changed there, and the global
plugin's ring hook writes it to the firmware's own key as well (globalPlugins/ssi263Speech/ring.py); saveSettings,
which NVDA also runs before every config save, writes them all.  So nothing NVDA writes elsewhere can overrule a
firmware's own value (Reply 163, 1-2).  A switch remembers the old type's values in memory until saveSettings; a
Cancel (loadSettings) forgets them.

Loading is our own order: the firmware type first, then its language, then its settings.  A unit is the new one only
once its worker has booted the requested firmware and language (its booted event; Reply 163, 4): until then, and if
it fails, the previous unit keeps speaking, and a unit switched away from reports to nobody NVDA knows.  Never read
the config in __init__ (blazie.py: 0.6.0's setSynth KeyError).
"""

from collections import OrderedDict

from synthDriverHandler import SynthDriver, VoiceInfo, synthIndexReached, synthDoneSpeaking
from autoSettingsUtils.utils import StringParameterInfo
from autoSettingsUtils.driverSetting import DriverSetting
from logHandler import log

from ._ssi263_unified import accentmini, blazie, speakout
try:
    from ._ssi263_unified import mockingboard
except ImportError:              # staged only with its firmware file (build_ssi263.py)
    mockingboard = None

# firmware type: (its name, the unit's driver module, {language: that driver's voice})
FIRMWARE_TYPES = OrderedDict([
    ("braillelite2000", ("Braille Lite 2000", blazie, OrderedDict([("en", "blazie"), ("es", "blazie_es")]))),
    ("speakout", ("Speak-Out", speakout, OrderedDict([("en", "speakout")]))),
    ("accentsa", ("Accent SA", accentmini, OrderedDict([("en", "sa")]))),
    ("accentmini", ("Accent-mini", accentmini, OrderedDict([("en", "mini")]))),
])
if mockingboard is not None:
    FIRMWARE_TYPES["mockingboard"] = ("Mockingboard", mockingboard, OrderedDict([("en", "mockingboard")]))
LANGUAGES = {"en": "English", "es": "Español"}
# the units' own string settings with choices (NVDA's available<Id>s, its id capitalized)
CHOICES = ("variant", "sampleRate", "whine")
BOOT_TIMEOUT = 30.0              # seconds a unit may take to boot its firmware before it counts as failed


class _Stale:
    """the synth a unit switched away from reports as: not NVDA's, so its late index and done are dropped"""
    name = "ssi263 (switched away)"


def _settings_of(fw):
    """the unit's own settings (its driver's supportedSettings)"""
    return FIRMWARE_TYPES[fw][1].SynthDriver.supportedSettings


def _setting_ids():
    ids = []
    for fw in FIRMWARE_TYPES:
        for s in _settings_of(fw):
            if s.id not in ids:
                ids.append(s.id)
    return ids


def present_languages(fw):
    """the languages of a firmware type whose files are here, in order"""
    _name, mod, voices = FIRMWARE_TYPES[fw]
    drv = mod.SynthDriver
    try:
        if not drv.check():
            return []
        have = drv._present() if hasattr(drv, "_present") else list(voices.values())
    except Exception:
        log.error("SSI-263: checking %s failed" % fw, exc_info=True)
        return []
    return [lang for lang, v in voices.items() if v in have]


def present_types():
    return [fw for fw in FIRMWARE_TYPES if present_languages(fw)]


def mem_key(fw, setting_id):
    """the config key of a firmware type's own value"""
    return "fw_%s_%s" % (fw, setting_id)


class _Unified(SynthDriver):
    name = "ssi263"
    description = "Votrax SC-02 / SSI-263 (emulated)"
    supportedNotifications = {synthIndexReached, synthDoneSpeaking}
    FirmwareTypeSetting = DriverSetting("firmwareType", "&Firmware", availableInSettingsRing=True,
                                        defaultVal="braillelite2000", displayName="Firmware")

    @classmethod
    def check(cls):
        return bool(present_types())

    def __init__(self):
        super().__init__()
        self._fw = None
        self._inner = None
        self._memory = {}            # firmware type -> its values when switched away from; saveSettings writes them
        self._defaults = {}          # firmware type -> its unit's own values when it started (a key never saved)
        self._loading = False        # inside loadSettings: the firmware type is handled before anything else
        types = present_types()
        if not types:
            raise RuntimeError("SSI-263: no firmware is here")
        self._fw, self._inner = types[0], self._make(types[0])

    # -- the unit -----------------------------------------------------------------------------------------------
    def _make(self, fw, lang=None):
        """a unit of that firmware type whose worker has BOOTED that language's firmware; raises otherwise"""
        _name, mod, voices = FIRMWARE_TYPES[fw]
        langs = present_languages(fw)
        lang = lang if lang in langs else langs[0]
        inner = mod.SynthDriver(startVoice=voices[lang])
        try:
            booted = getattr(inner, "booted", None)
            if booted is not None:
                if not booted.wait(BOOT_TIMEOUT):
                    raise RuntimeError("SSI-263: the %s did not boot in %.0f s" % (FIRMWARE_TYPES[fw][0], BOOT_TIMEOUT))
                if inner.bootError is not None:
                    raise RuntimeError("SSI-263: the %s did not boot: %s" % (FIRMWARE_TYPES[fw][0], inner.bootError))
            inner._set_voice(voices[lang])
        except Exception:
            inner.notifySynth = _Stale
            inner.terminate()
            raise
        inner.notifySynth = self
        if fw not in self._defaults:
            self._defaults[fw] = self._values_of(inner, fw)
        return inner

    def _values_of(self, inner, fw):
        out = OrderedDict()
        voices = FIRMWARE_TYPES[fw][2]
        for s in inner.supportedSettings:
            if s.id == "voice":
                cur = inner._get_voice()
                out["voice"] = next((lang for lang, v in voices.items() if v == cur), next(iter(voices)))
            else:
                fn = getattr(inner, "_get_" + s.id, None)
                out[s.id] = fn() if fn else None
        return out

    def _values(self):
        """the active unit's values, by setting id (the voice as its language)"""
        return self._values_of(self._inner, self._fw)

    def _apply(self, values, onlyChanged=False):
        """values onto the active unit: the language first (blazie.py starts its unit with it)"""
        cur = self._values() if onlyChanged else {}
        if "voice" in values and not (onlyChanged and cur.get("voice") == values["voice"]):
            try:
                self._set_voice(values["voice"])
            except Exception:
                log.debugWarning("SSI-263: voice %r not applied" % (values["voice"],), exc_info=True)
        for k, v in values.items():
            if k == "voice" or v is None or (onlyChanged and cur.get(k) == v):
                continue
            try:
                self._set(k, v)
            except Exception:
                log.debugWarning("SSI-263: %s=%r not applied" % (k, v), exc_info=True)

    def _section(self):
        try:
            import config
            return config.conf["speech"][self.name]
        except Exception:
            return None

    def _saved(self, fw):
        """a firmware type's own values from NVDA's config (each from the topmost profile that sets it)"""
        c = self._section()
        out = OrderedDict()
        if c is None:
            return out
        for s in _settings_of(fw):
            key = mem_key(fw, s.id)
            try:
                if c.isSet(key) if hasattr(c, "isSet") else key in c:
                    v = c[key]
                    if v is not None:
                        out[s.id] = v
            except Exception:
                pass
        return out

    def _wanted(self, fw):
        """what a firmware type should have now: remembered from a switch, else saved, else its unit's defaults"""
        if fw in self._memory:
            return self._memory[fw]
        values = OrderedDict(self._defaults.get(fw, {}))
        values.update(self._saved(fw))
        return values

    def _switch(self, fw, remember=True):
        """to another firmware type: the new unit booted first (if it fails, the old one keeps speaking), then its
        own values"""
        if fw == self._fw:
            return
        if fw not in FIRMWARE_TYPES or not present_languages(fw):
            raise ValueError("SSI-263: no %s firmware here" % fw)
        if remember:
            self._memory[self._fw] = self._values()
        want = self._memory.get(fw) or self._saved(fw)
        try:
            import speech
            getattr(speech, "cancelSpeech", lambda: None)()   # NVDA would wait for the old unit's notifications
        except Exception:
            pass
        new = self._make(fw, want.get("voice"))
        old, self._fw, self._inner = self._inner, fw, new
        old.notifySynth = _Stale
        try:
            old.terminate()
        except Exception:
            log.debugWarning("SSI-263: the old unit did not stop cleanly", exc_info=True)
        self._apply(self._wanted(fw))
        self._announce()

    def _announce(self):
        """NVDA's own step after a synth's voice is loaded, which its loadSettings takes and ours must too:
        changeVoice(self, None) makes the settings ring this synth's (creating it at start-up, when getSynth() is not
        yet this one) and loads its voice's dictionary -- without it the ring kept the previous synth's settings,
        Eloquence's voices and variants cycling on the SC-02 (Tomi; Astra, Reply 165).  None: the voice is not set
        again (the unit is not restarted)."""
        try:
            from synthDriverHandler import changeVoice
        except ImportError:          # a stand-in NVDA without it (the byte-for-byte tests)
            return
        try:
            changeVoice(self, None)
        except Exception:
            log.error("SSI-263: the settings ring and voice dictionary were not set for this synth", exc_info=True)

    @property
    def inner(self):
        """the active unit's driver (the tests)"""
        return self._inner

    # -- forwarded to the unit --------------------------------------------------------------------------------
    def _get(self, setting_id):
        fn = getattr(self._inner, "_get_" + setting_id, None)
        return fn() if fn else None

    def _set(self, setting_id, value):
        fn = getattr(self._inner, "_set_" + setting_id, None)
        if fn:
            fn(value)

    def speak(self, speechSequence):
        self._inner.speak(speechSequence)

    def cancel(self):
        self._inner.cancel()

    def pause(self, switch):
        self._inner.pause(switch)

    def terminate(self):
        if self._inner is not None:
            self._inner.notifySynth = _Stale
            self._inner.terminate()

    def _get_supportedCommands(self):
        return self._inner.supportedCommands

    def _get_supportedSettings(self):
        return (self.FirmwareTypeSetting,) + tuple(self._inner.supportedSettings)

    # -- the firmware type and the voice (its language) --------------------------------------------------------
    def _get_availableFirmwaretypes(self):
        return OrderedDict((fw, StringParameterInfo(fw, FIRMWARE_TYPES[fw][0])) for fw in present_types())

    def _get_firmwareType(self):
        return self._fw

    def _set_firmwareType(self, fw):
        if self._loading:            # loadSettings switched first (or kept the unit when that firmware is missing)
            return
        self._switch(fw)

    def _get_availableVoices(self):
        return OrderedDict((lang, VoiceInfo(lang, LANGUAGES.get(lang, lang), lang))
                           for lang in present_languages(self._fw))

    def _get_voice(self):
        voices = FIRMWARE_TYPES[self._fw][2]
        cur = self._get("voice")
        return next((lang for lang, v in voices.items() if v == cur), next(iter(voices)))

    def _set_voice(self, lang):
        voices = FIRMWARE_TYPES[self._fw][2]
        if lang not in voices or lang not in present_languages(self._fw):
            raise ValueError("SSI-263: %s has no voice %r" % (FIRMWARE_TYPES[self._fw][0], lang))
        self._set("voice", voices[lang])

    # -- NVDA's config: every type's settings, and each type's own values --------------------------------------
    def getConfigSpec(self):
        spec = super().getConfigSpec()
        for fw in FIRMWARE_TYPES:
            for s in _settings_of(fw):
                if not getattr(s, "useConfig", True):
                    continue
                spec.setdefault(s.id, s.configSpec)
                spec[mem_key(fw, s.id)] = s.configSpec
        return spec

    def saveSettings(self):
        """the firmware type, and every remembered type's own values; NVDA runs this before every config save"""
        c = self._section()
        if c is None:
            return
        c["firmwareType"] = self._fw
        memory = dict(self._memory)
        memory[self._fw] = self._values()
        for fw, values in memory.items():
            for k, v in values.items():
                if v is not None:
                    c[mem_key(fw, k)] = v
        self._memory.clear()

    def loadSettings(self, onlyChanged=False):
        """the saved firmware type, then its own values (all of them, the same type too: a profile may set only
        some); what a switch remembered is forgotten (a Cancel)"""
        self._memory.clear()
        c = self._section()
        fw = c.get("firmwareType") if c is not None else None
        if fw and fw != self._fw:
            if fw in FIRMWARE_TYPES and present_languages(fw):
                try:
                    self._switch(fw, remember=False)
                    return
                except Exception:
                    log.error("SSI-263: could not start the %s; keeping the %s" % (fw, self._fw), exc_info=True)
            else:
                log.warning("SSI-263: the saved firmware type %r is not here; keeping the %s" % (fw, self._fw))
        self._loading = True
        try:
            self._apply(self._wanted(self._fw), onlyChanged)
        finally:
            self._loading = False
        self._announce()


def _forwarders():
    """_get_/_set_ for every unit setting, and available<Id>s for their choices: NVDA's AutoPropertyObject makes
    properties of the ones in a class's own namespace when the class is made, so they go into SynthDriver's"""
    ns = {}
    for sid in _setting_ids():
        if sid == "voice":
            continue
        ns["_get_" + sid] = (lambda i: lambda self: self._get(i))(sid)
        ns["_set_" + sid] = (lambda i: lambda self, v: self._set(i, v))(sid)
    for sid in CHOICES:
        name = "_get_available%ss" % sid.capitalize()
        ns[name] = (lambda n: lambda self: getattr(self._inner, n)() if hasattr(self._inner, n)
                    else OrderedDict())(name)
    return ns


SynthDriver = type(_Unified)("SynthDriver", (_Unified,), _forwarders())
