"""The 0.8 driver's settings (synthDrivers/ssi263.py) under a stand-in NVDA whose config, profiles and SynthDriver
settings methods follow NVDA's own (2026.2: autoSettingsUtils/autoSettings.py, synthDriverHandler.py, config
profiles that override the base key by key): the real units start and stop behind it.  Each firmware type's values
live only in fw_<type>_<setting>; the plain keys are never read (Astra, Reply 163, 1-2).

    first_load        no config yet: the defaults saved, the firmware type first, each type's own keys declared
    memory            each firmware type keeps its own values across switches (Tomi: per-firmware memory)
    saved             saveSettings writes the firmware type and every type's fw_<type>_<id>, no plain unit keys
    cancel            a Cancel (loadSettings) forgets what a switch remembered: the saved values come back
    order             a saved Spanish Braille Lite loads as Spanish, from a Speak-Out current and in a new driver
    profile           a profile with another type and its rate switches to it and back (onlyChanged=True)
    profile_one_key   a profile setting only fw_braillelite2000_rate (what the migration writes for an old profile
                      with only [[blazie]] rate): the same type, that rate, the base's back after (Reply 163, 1)
    ring_type_only    the ring writes only the plain firmwareType: a new driver from that config is the Speak-Out
                      with ITS rate, not a plain rate left over (Reply 163, 2)
    missing           a saved type whose firmware is not here: the current unit kept, a warning, no exception
    boot_failure      a unit whose constructor fails: the old one kept, still current
    worker_boot       a unit whose WORKER fails to boot its firmware: the switch refused, the old unit still current
                      and speaking, the new one stopped (Reply 163, 4)
    settings_list     the settings follow the type (the Braille Lite's whine, not the Speak-Out's)

    UNIFIED_SETTINGS_BREAK=plain     control: the plain keys read after the firmware's own -- ring_type_only must FAIL
    UNIFIED_SETTINGS_BREAK=sametype  control: the same type's saved values not loaded -- profile_one_key must FAIL
    UNIFIED_SETTINGS_BREAK=cancel    control: loadSettings keeps what a switch remembered -- cancel must FAIL
    UNIFIED_SETTINGS_BREAK=ready     control: a unit taken without waiting for its boot -- worker_boot must FAIL

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
    if BREAK == "plain":
        load = W.loadSettings

        def plain_too(self, onlyChanged=False):
            load(self, onlyChanged)
            for s in self.supportedSettings:
                if s.id not in ("firmwareType", "voice") and SECTION.isSet(s.id):
                    self._set(s.id, SECTION[s.id])
        W.loadSettings = plain_too
    if BREAK == "sametype":
        load = W.loadSettings

        def other_types_only(self, onlyChanged=False):
            fw = SECTION.get("firmwareType")
            if fw == self._fw:
                self._memory.clear()
                return
            load(self, onlyChanged)
        W.loadSettings = other_types_only
    if BREAK == "cancel":
        load = W.loadSettings

        def keeps(self, onlyChanged=False):
            kept = dict(self._memory)
            load(self, onlyChanged)
            self._memory.update(kept)
        W.loadSettings = keeps
    if BREAK == "ready":
        for _name, m, _v in mod.FIRMWARE_TYPES.values():
            m.SynthDriver.booted = None          # what _make waits on, hidden: it takes the unit at once
        real_make = W._make

        def no_wait(self, fw, lang=None):
            _n, m, voices = mod.FIRMWARE_TYPES[fw]
            inner = m.SynthDriver(startVoice=voices[lang or mod.present_languages(fw)[0]])
            inner.notifySynth = self
            self._defaults.setdefault(fw, self._values_of(inner, fw))
            return inner
        W._make = no_wait

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
    check("saved", b.get("firmwareType") == "braillelite2000" and b.get("fw_braillelite2000_rate") == 70
          and b.get("fw_speakout_rate") == 30 and "rate" not in b and "voice" not in b,
          "firmwareType %s fw_braillelite2000_rate %s fw_speakout_rate %s; plain rate written: %s"
          % (b.get("firmwareType"), b.get("fw_braillelite2000_rate"), b.get("fw_speakout_rate"), "rate" in b))

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
    check("order", on_load == want and fresh == want and STORE["base"].get("fw_braillelite2000_voice") == "es",
          "from the Speak-Out %s; a new driver %s; saved voice %s" % (on_load, fresh,
                                                                     STORE["base"].get("fw_braillelite2000_voice")))
    d.voice = "en"
    d.saveSettings()

    STORE["profiles"]["reading"] = {"firmwareType": "speakout", "fw_speakout_rate": 40}
    STORE["active"].append("reading")
    d.loadSettings(onlyChanged=True)
    in_profile = (d.firmwareType, d.rate)
    STORE["active"].remove("reading")
    d.loadSettings(onlyChanged=True)
    back = (d.firmwareType, d.rate)
    check("profile", in_profile == ("speakout", 40) and back == ("braillelite2000", 70),
          "in the profile %s, back %s" % (in_profile, back))

    STORE["profiles"]["typing"] = {"fw_braillelite2000_rate": 25}
    STORE["active"].append("typing")
    d.loadSettings(onlyChanged=True)
    in_typing = (d.firmwareType, d.rate)
    STORE["active"].remove("typing")
    d.loadSettings(onlyChanged=True)
    after_typing = d.rate
    check("profile_one_key", in_typing == ("braillelite2000", 25) and after_typing == 70,
          "in the profile %s (want the Braille Lite at 25), back %s" % (in_typing, after_typing))

    SECTION["rate"] = 70             # a plain rate as the ring (or a 0.8 preview) left it
    d.firmwareType = "speakout"
    SECTION["firmwareType"] = "speakout"     # the ring's own write: the type only
    d.terminate()
    d = new_driver()
    got = (d.firmwareType, d.rate)
    check("ring_type_only", got == ("speakout", 30), "a new driver from that config: %s (want speakout 30)" % (got,))
    d.firmwareType = "braillelite2000"
    d.saveSettings()
    STORE["base"].pop("rate", None)

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

    def broken(self, startVoice=None):
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

    real_boot = unit._boot

    def fails(self):
        raise RuntimeError("review injected worker firmware boot failure")
    unit._boot = fails
    before = d.inner
    made = []

    def track(self, *a, **k):
        made.append(self)
        real_init(self, *a, **k)
    unit.__init__ = track
    try:
        d.firmwareType = "speakout"
        raised = False
    except RuntimeError:
        raised = True
    unit._boot = real_boot
    unit.__init__ = real_init
    new_stopped = bool(made) and made[-1]._stopped
    old_alive = before._worker.is_alive() and not before._stopped
    check("worker_boot", raised and d.firmwareType == "braillelite2000" and d.inner is before and old_alive
          and new_stopped, "raised %s; current %s; the Braille Lite still running: %s; the failed unit stopped: %s"
          % (raised, d.firmwareType, old_alive, new_stopped))
    if d.firmwareType != "braillelite2000":
        d.terminate()
        d = new_driver()

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
