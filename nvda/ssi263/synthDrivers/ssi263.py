# -*- coding: utf-8 -*-
"""NVDA synthesizer driver: Votrax SC-02 / SSI-263 (emulated) -- every unit of this project in one driver (0.8).

The units are the 0.7 add-ons' own drivers, unchanged, in this add-on's private package (synthDrivers/_ssi263_unified:
blazie.py, speakout.py, accentmini.py, and mockingboard.py, each with its engine; NVDA lists no module whose name
starts with "_").  This driver runs ONE of them at a time, chosen by the firmware type, and passes NVDA's speech,
settings and notifications through: what you hear is that driver's, byte for byte (nvda/tools/unified_driver_equiv.py).

Settings (investigation/design-0.8-nvda-addon.md, its revisions after Astra's Reply 161):
  firmware type  the units whose firmware is here (Braille Lite 2000, Speak-Out, Accent SA, Accent-mini, Mockingboard)
  voice          that firmware's languages, with their language codes, so NVDA's language switching works
  the rest       the unit's own settings, as its 0.7 add-on had them
Each firmware type keeps its own values (Tomi): switching to the Accent restores the Accent's rate, pitch and voice.
They live in NVDA's own config, in this driver's section, as fw_<type>_<setting> keys, so configuration profiles
keep them too; the active type's are also under their plain names, as NVDA expects.  They are written only by
saveSettings (NVDA's OK or save), so a Cancel -- loadSettings -- forgets what a switch remembered.

Loading is our own order (Reply 161, 2): the firmware type first, then its language, then its settings.  A firmware
that fails to start leaves the previous unit speaking.  Never read the config in __init__ (blazie.py: 0.6.0's
setSynth KeyError).
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
        self._loading = False        # inside loadSettings: the firmware type is handled before NVDA's own pass
        types = present_types()
        if not types:
            raise RuntimeError("SSI-263: no firmware is here")
        self._fw, self._inner = types[0], self._make(types[0])

    # -- the unit -----------------------------------------------------------------------------------------------
    def _make(self, fw):
        """a started unit of that firmware type, speaking its first language"""
        _name, mod, voices = FIRMWARE_TYPES[fw]
        inner = mod.SynthDriver()
        inner.notifySynth = self
        try:
            inner._set_voice(voices[present_languages(fw)[0]])
        except Exception:
            inner.terminate()
            raise
        return inner

    def _values(self):
        """the active unit's values, by setting id (the voice as its language)"""
        out = OrderedDict()
        for s in self._inner.supportedSettings:
            out[s.id] = self._get_voice() if s.id == "voice" else self._get(s.id)
        return out

    def _apply(self, values):
        """values onto the active unit: the language first (blazie.py starts its unit with it)"""
        if "voice" in values:
            try:
                self._set_voice(values["voice"])
            except Exception:
                pass
        for k, v in values.items():
            if k != "voice" and v is not None:
                try:
                    self._set(k, v)
                except Exception:
                    log.debugWarning("SSI-263: %s=%r not applied" % (k, v), exc_info=True)

    def _saved(self, fw):
        """a firmware type's own values from NVDA's config (the profile's, or the base's); {} when none"""
        try:
            import config
            c = config.conf["speech"][self.name]
        except Exception:
            return {}
        out = OrderedDict()
        for s in _settings_of(fw):
            key = mem_key(fw, s.id)
            try:
                isset = c.isSet(key) if hasattr(c, "isSet") else key in c
                if isset:
                    out[s.id] = c[key]
            except Exception:
                pass
        return out

    def _switch(self, fw, remember=True):
        """to another firmware type: the new unit started first (if it fails, the old one keeps speaking), then its
        own values -- the ones it had when switched away from, else the saved ones, else its defaults"""
        if fw == self._fw:
            return
        if fw not in FIRMWARE_TYPES or not present_languages(fw):
            raise ValueError("SSI-263: no %s firmware here" % fw)
        try:
            import speech
            getattr(speech, "cancelSpeech", lambda: None)()   # NVDA would wait for the old unit's notifications
        except Exception:
            pass
        if remember:
            self._memory[self._fw] = self._values()
        new = self._make(fw)
        old, self._fw, self._inner = self._inner, fw, new
        try:
            old.terminate()
        except Exception:
            log.debugWarning("SSI-263: the old unit did not stop cleanly", exc_info=True)
        self._apply(self._memory.get(fw) or self._saved(fw))
        self._refresh_ring()

    def _refresh_ring(self):
        """NVDA's settings ring holds the old unit's settings (Reply 161, 1): rebuilt after every switch"""
        try:
            import globalVars
            from synthDriverHandler import getSynth
            if globalVars.settingsRing and getSynth() is self:
                globalVars.settingsRing.updateSupportedSettings(self)
        except Exception:
            log.debugWarning("SSI-263: the settings ring was not refreshed", exc_info=True)

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
        super().saveSettings()
        import config
        c = config.conf["speech"][self.name]
        memory = dict(self._memory)
        memory[self._fw] = self._values()
        for fw, values in memory.items():
            for k, v in values.items():
                if v is not None:
                    c[mem_key(fw, k)] = v
        self._memory.clear()

    def loadSettings(self, onlyChanged=False):
        # what a switch remembered is forgotten (a Cancel); the saved firmware type first, then NVDA's own order
        self._memory.clear()
        try:
            import config
            fw = config.conf["speech"][self.name].get("firmwareType")
        except Exception:
            fw = None
        if fw and fw != self._fw:
            if fw in FIRMWARE_TYPES and present_languages(fw):
                try:
                    self._switch(fw, remember=False)
                except Exception:
                    log.error("SSI-263: could not start the %s; keeping the %s" % (fw, self._fw), exc_info=True)
            else:
                log.warning("SSI-263: the saved firmware type %r is not here; keeping the %s" % (fw, self._fw))
        self._loading = True
        try:
            super().loadSettings(onlyChanged)
        finally:
            self._loading = False


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
