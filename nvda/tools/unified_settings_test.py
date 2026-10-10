"""The 0.8 driver's settings (synthDrivers/ssi263.py) under a stand-in NVDA whose config, profiles and SynthDriver
settings methods follow NVDA's own (2026.2: autoSettingsUtils/autoSettings.py, synthDriverHandler.py's loadSettings
with the voice first, config profiles that override the base key by key): the real units start and stop behind it.

    first_load      no config yet: the defaults saved, the firmware type first, each type's own keys declared
    memory          each firmware type keeps its own values across switches (Tomi: per-firmware memory)
    saved           saveSettings writes the active type's under the plain names and every type's fw_<type>_<id>
    cancel          a Cancel (loadSettings) forgets what a switch remembered: the saved values come back
    order           a saved Spanish Braille Lite loads as Spanish (the firmware type before NVDA's voice-first load)
    profile         a profile with another type and rate switches to it and back (loadSettings(onlyChanged=True))
    missing         a saved type whose firmware is not here: the current unit kept, a warning
    boot_failure    a unit that fails to start: the old one kept, still current
    settings_list   the settings follow the type (the Braille Lite's whine, not the Speak-Out's)

    UNIFIED_SETTINGS_BREAK=order    control: NVDA's load first, the firmware type after -- order must FAIL
    UNIFIED_SETTINGS_BREAK=cancel   control: loadSettings keeps what a switch remembered -- cancel must FAIL
    UNIFIED_SETTINGS_BREAK=loading  control: NVDA's own pass sets the firmware type again (no _loading guard) --
                                    missing must FAIL (a saved type that is not here: setSynth would fail)

Exit 0 when all pass; 1 otherwise.  Build first: nvda/build_ssi263.py.
"""
import copy
import importlib
import os
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
BUILD = os.path.join(REPO, "nvda", "dist", "ssi263-build", "synthDrivers")
BREAK = os.environ.get("UNIFIED_SETTINGS_BREAK", "")
LOG = []


# ---- the stand-in NVDA --------------------------------------------------------------------------------------
class Section:
    """config.conf["speech"]["ssi263"]: the active profiles over the base, key by key (NVDA's AggregatedSection)"""

    def __init__(self, store):
        self.store = store
        self.spec = {}

    def _layers(self):
        return [self.store["profiles"][n] for n in reversed(self.store["active"])] + [self.store["base"]]

    def isSet(self, key):
        return any(key in layer for layer in self._layers())

    def __contains__(self, key):
        return self.isSet(key)

    def __getitem__(self, key):
        for layer in self._layers():
            if key in layer:
                return layer[key]
        if key in self.spec:
            return None
        raise KeyError(key)

    def get(self, key, default=None):
        try:
            v = self[key]
        except KeyError:
            return default
        return default if v is None else v

    def __setitem__(self, key, value):
        top = self.store["profiles"][self.store["active"][-1]] if self.store["active"] else self.store["base"]
        top[key] = value


STORE = {"base": {}, "profiles": {}, "active": []}
SECTION = Section(STORE)


class Speech(dict):
    def isSet(self, key):
        return key == "ssi263" and bool(STORE["base"] or STORE["profiles"])

    def __getitem__(self, key):
        return SECTION if key == "ssi263" else {"outputDevice": "default"}

    def __setitem__(self, key, value):
        pass


class _Setting:
    def __init__(self, id, displayNameWithAccelerator="", *a, **k):
        self.id, self.defaultVal = id, k.get("defaultVal")
        self.configSpec, self.useConfig = "string(default=None)", True
        self.availableInSettingsRing = k.get("availableInSettingsRing", False)


class AutoPropertyType(type):
    """NVDA's baseObject.AutoPropertyType: properties from the _get_ / _set_ in a class's own namespace"""

    def __init__(cls, name, bases, ns):
        super().__init__(name, bases, ns)
        for attr in list(ns):
            if attr.startswith("_get_") or attr.startswith("_set_"):
                prop = attr[5:]
                getter = getattr(cls, "_get_" + prop, None)
                setter = getattr(cls, "_set_" + prop, None)
                setattr(cls, prop, property(getter, setter))


def changeVoice(synth, voice):
    if voice:
        synth.voice = voice


class Base(metaclass=AutoPropertyType):
    """NVDA's SynthDriver, its settings methods as 2026.2 has them"""
    VoiceSetting = staticmethod(lambda: _Setting("voice"))
    RateSetting = staticmethod(lambda: _Setting("rate"))
    PitchSetting = staticmethod(lambda: _Setting("pitch"))
    VolumeSetting = staticmethod(lambda: _Setting("volume"))
    VariantSetting = staticmethod(lambda: _Setting("variant"))
    InflectionSetting = staticmethod(lambda: _Setting("inflection"))

    def __init__(self):
        pass

    def isSupported(self, sid):
        return any(s.id == sid for s in self.supportedSettings)

    def getConfigSpec(self):
        return {s.id: s.configSpec for s in self.supportedSettings if s.useConfig}

    def initSettings(self):
        firstLoad = not config.conf["speech"].isSet(self.name)
        config.conf["speech"][self.name].spec.update(self.getConfigSpec())
        if firstLoad:
            changeVoice(self, self.voice if self.isSupported("voice") else None)
            self.saveSettings()
        else:
            self.loadSettings()

    def saveSettings(self):
        c = config.conf["speech"][self.name]
        for s in self.supportedSettings:
            if s.useConfig:
                c[s.id] = getattr(self, s.id)

    def loadSettings(self, onlyChanged=False):
        c = config.conf["speech"][self.name]
        if self.isSupported("voice"):
            voice = c.get("voice", None)
            if not onlyChanged or self.voice != voice:
                try:
                    changeVoice(self, voice)
                except Exception:
                    LOG.append("warning: Invalid voice: %s" % voice)
                    c["voice"] = self.voice
                    changeVoice(self, self.voice)
        for s in self.supportedSettings:
            if not s.useConfig or s.id == "voice" or c[s.id] is None:
                continue
            val = c[s.id]
            if onlyChanged and getattr(self, s.id) == val:
                continue
            setattr(self, s.id, val)


class _Player:
    def __init__(self, *a, **k):
        pass

    def feed(self, data, onDone=None):
        if onDone:
            onDone()

    def stop(self):
        pass

    idle = close = stop

    def pause(self, s):
        pass


class _Log:
    def __getattr__(self, name):
        return lambda msg, *a, **k: LOG.append("%s: %s" % (name, msg))

    def isEnabledFor(self, level):
        return False


class _Notifier:
    def notify(self, **kw):
        pass


def install():
    global config
    config = types.ModuleType("config")
    config.conf = {"speech": Speech(), "audio": {"outputDevice": "default"}}
    sys.modules["config"] = config
    nvwave = types.ModuleType("nvwave")
    nvwave.WavePlayer = _Player
    sys.modules["nvwave"] = nvwave
    sdh = types.ModuleType("synthDriverHandler")
    sdh.SynthDriver, sdh.VoiceInfo = Base, (lambda *a: types.SimpleNamespace(id=a[0], displayName=a[1],
                                                                              language=a[2]))
    sdh.synthIndexReached, sdh.synthDoneSpeaking = _Notifier(), _Notifier()
    sdh.getSynth = lambda: None
    sys.modules["synthDriverHandler"] = sdh
    for name in ("autoSettingsUtils", "autoSettingsUtils.utils", "autoSettingsUtils.driverSetting"):
        sys.modules[name] = types.ModuleType(name)
    sys.modules["autoSettingsUtils.utils"].StringParameterInfo = lambda *a: types.SimpleNamespace(id=a[0],
                                                                                                 displayName=a[1])
    sys.modules["autoSettingsUtils.driverSetting"].BooleanDriverSetting = _Setting
    sys.modules["autoSettingsUtils.driverSetting"].DriverSetting = _Setting
    lh = types.ModuleType("logHandler")
    lh.log = _Log()
    sys.modules["logHandler"] = lh
    speech = types.ModuleType("speech")
    cmds = types.ModuleType("speech.commands")
    for n in ("IndexCommand", "PitchCommand", "LangChangeCommand"):
        setattr(cmds, n, type(n, (), {"__init__": lambda self, *a: None}))
    speech.commands = cmds
    sys.modules["speech"], sys.modules["speech.commands"] = speech, cmds
    sd = types.ModuleType("synthDrivers")
    sd.__path__ = [BUILD]
    sys.modules["synthDrivers"] = sd
    return importlib.import_module("synthDrivers.ssi263")


# ---- the tests --------------------------------------------------------------------------------------------------
results = []


def check(name, ok, detail):
    results.append(ok)
    print("%s %-14s %s" % ("ok  " if ok else "FAIL", name, detail))


def new_driver():
    d = mod.SynthDriver()
    d.initSettings()
    return d


def main():
    global mod
    if not os.path.isdir(BUILD):
        print("no nvda/dist/ssi263-build: run nvda/build_ssi263.py first")
        return 1
    mod = install()
    W = mod.SynthDriver
    if BREAK == "order":
        load = W.loadSettings

        def nvda_first(self, onlyChanged=False):
            Base.loadSettings(self, onlyChanged)
            load(self, True)
        W.loadSettings = nvda_first
    if BREAK == "loading":
        def switches(self, fw):
            self._switch(fw)
        W._set_firmwareType = switches
        W.firmwareType = property(W._get_firmwareType, switches)
    if BREAK == "cancel":
        load = W.loadSettings

        def keeps(self, onlyChanged=False):
            kept = dict(self._memory)
            load(self, onlyChanged)
            self._memory.update(kept)
        W.loadSettings = keeps

    d = new_driver()
    keys = SECTION.spec
    check("first_load", d.firmwareType == "braillelite2000" and STORE["base"].get("firmwareType") == "braillelite2000"
          and "fw_speakout_rate" in keys and "fw_mockingboard_rate" in keys and "whine" in keys,
          "type %s saved %s; %d keys declared" % (d.firmwareType, STORE["base"].get("firmwareType"), len(keys)))

    d.rate = 70
    d.firmwareType = "speakout"
    so_default = d.rate
    d.rate = 30
    d.firmwareType = "braillelite2000"
    bl = d.rate
    d.firmwareType = "speakout"
    so = d.rate
    check("memory", bl == 70 and so == 30 and so_default == 50,
          "Braille Lite 70 -> %s, Speak-Out default %s, 30 -> %s" % (bl, so_default, so))

    d.firmwareType = "braillelite2000"
    d.saveSettings()
    b = STORE["base"]
    check("saved", b.get("firmwareType") == "braillelite2000" and b.get("rate") == 70
          and b.get("fw_speakout_rate") == 30 and b.get("fw_braillelite2000_rate") == 70,
          "firmwareType %s rate %s fw_speakout_rate %s" % (b.get("firmwareType"), b.get("rate"),
                                                           b.get("fw_speakout_rate")))

    d.firmwareType = "speakout"
    d.rate = 10                      # unsaved, and remembered by the switch back
    d.firmwareType = "braillelite2000"
    d.firmwareType = "speakout"
    d.loadSettings()                 # the dialog's Cancel
    after_type, after_rate = d.firmwareType, d.rate
    d.firmwareType = "speakout"
    so = d.rate
    check("cancel", after_type == "braillelite2000" and after_rate == 70 and so == 30,
          "after Cancel: %s rate %s; Speak-Out again: rate %s (saved 30, unsaved 10)" % (after_type, after_rate, so))
    d.firmwareType = "braillelite2000"

    d.voice = "es"
    d.saveSettings()
    d.firmwareType = "speakout"      # a Speak-Out current, a Braille Lite in Spanish saved (Reply 161, 2)
    d.loadSettings()
    on_load = (d.firmwareType, d.voice, d.inner.voice)
    d.terminate()
    d = new_driver()
    fresh = (d.firmwareType, d.voice, d.inner.voice)
    want = ("braillelite2000", "es", "blazie_es")
    check("order", on_load == want and fresh == want and STORE["base"].get("voice") == "es",
          "from the Speak-Out %s; a new driver %s; saved voice %s" % (on_load, fresh, STORE["base"].get("voice")))
    d.voice = "en"
    d.saveSettings()

    STORE["profiles"]["reading"] = {"firmwareType": "speakout", "rate": 40, "voice": "en"}
    STORE["active"].append("reading")
    d.loadSettings(onlyChanged=True)
    in_profile = (d.firmwareType, d.rate)
    STORE["active"].remove("reading")
    d.loadSettings(onlyChanged=True)
    back = (d.firmwareType, d.rate)
    check("profile", in_profile == ("speakout", 40) and back == ("braillelite2000", 70),
          "in the profile %s, back %s" % (in_profile, back))

    present = mod.present_languages
    mod.present_languages = lambda fw: [] if fw == "accentsa" else present(fw)
    STORE["base"]["firmwareType"] = "accentsa"
    n = len(LOG)
    try:
        d.loadSettings()
        crashed = None
    except Exception as e:
        crashed = e
    mod.present_languages = present
    STORE["base"]["firmwareType"] = "braillelite2000"
    warned = any("not here" in m for m in LOG[n:])
    check("missing", d.firmwareType == "braillelite2000" and warned and crashed is None,
          "kept %s; warned: %s%s" % (d.firmwareType, warned, "; loadSettings RAISED %r" % crashed if crashed else ""))

    unit = mod.FIRMWARE_TYPES["speakout"][1].SynthDriver
    real_init = unit.__init__

    def broken(self):
        raise RuntimeError("the box did not start")
    unit.__init__ = broken
    before = d.inner
    try:
        d.firmwareType = "speakout"
        raised = False
    except RuntimeError:
        raised = True
    unit.__init__ = real_init
    check("boot_failure", raised and d.inner is before and d.firmwareType == "braillelite2000",
          "raised %s; the Braille Lite still current: %s" % (raised, d.inner is before))

    bl_ids = [s.id for s in d.supportedSettings]
    d.firmwareType = "speakout"
    so_ids = [s.id for s in d.supportedSettings]
    check("settings_list", bl_ids[0] == "firmwareType" and "whine" in bl_ids and "whine" not in so_ids
          and "variant" in so_ids, "Braille Lite %d settings, Speak-Out %d" % (len(bl_ids), len(so_ids)))
    d.terminate()

    print("unified settings: %s" % ("all passed" if all(results) else "%d FAILED" % results.count(False)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
