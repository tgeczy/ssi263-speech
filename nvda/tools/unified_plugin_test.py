"""The 0.8 add-on's global plugin (globalPlugins/ssi263Speech) under stand-ins: the update check, the migration of the
0.7 add-ons' settings, removing the 0.7 add-ons, the Voice panel's refresh after a firmware change, and the settings
ring hook.

  updates   a newer release found by its asset's name (not its tag); the same and older versions, a draft and a
            prerelease not offered; the download refused without SHA256SUMS.txt, with no entry or two entries for the
            file, or with a wrong hash, and taken with the right one
  migrate   each 0.7 driver's section into its firmware types' own keys only (the Braille Lite's Spanish voice as es);
            the synth switched with the type the profile EFFECTIVELY had (an Accent SA inherited from the base stays
            the SA: Reply 163, 3); a profile's own overrides only (a rate-only profile gets only that rate; nothing
            into one with no 0.7 section); a key already set kept; the old sections kept; a second run writing nothing
            (the ledger); NVDA switched live.  Failures (Reply 163, 5): a failed save rolled back in memory, the ledger
            unwritten, and the next run saving it whole; a failed switch putting every profile's synth back, the ledger
            unwritten, and the next run trying again
  removal   the 0.7 add-ons removed only when NVDA speaks with this driver, its unit booted, and every profile is
            migrated -- the settings page's button too (Reply 164)
  panel     every string control given the new unit's choices (kept options and items) before NVDA's update; the
            hook restored only while ours is installed, passing through otherwise
  ring      a ring change of this driver's rate also written to the active firmware's own key; the firmware type and
            the voice left to the ring; another driver untouched; the properties restored

    UNIFIED_PLUGIN_BREAK=hash      control: any download accepted -- wrong_hash must FAIL
    UNIFIED_PLUGIN_BREAK=fill      control: the migration overwrites a key already set -- kept_existing must FAIL
    UNIFIED_PLUGIN_BREAK=ledger    control: the ledger ignored -- second_run must FAIL
    UNIFIED_PLUGIN_BREAK=options   control: the choices not replaced -- stale_choices must FAIL
    UNIFIED_PLUGIN_BREAK=revert    control: no profile's synth put back when this driver fails to start --
                                   switch_failed must FAIL
    UNIFIED_PLUGIN_BREAK=inherit   control: the Accent model read from the profile alone -- inherited_accent must FAIL
    UNIFIED_PLUGIN_BREAK=rollback  control: a failed save not undone in memory -- save_retry must FAIL
    UNIFIED_PLUGIN_BREAK=pending   control: the ledger written though the switch failed -- start_retry must FAIL
    UNIFIED_PLUGIN_BREAK=ready     control: removal without the readiness check -- manual_removal must FAIL
    UNIFIED_PLUGIN_BREAK=ringkey   control: the ring hook writes nothing -- ring_key must FAIL
    UNIFIED_PLUGIN_BREAK=version   control: an unversioned (preview) ledger taken as done -- preview_ledger must FAIL
    UNIFIED_PLUGIN_BREAK=context   control: only the pending profiles given to the migration -- late_profile must FAIL
    UNIFIED_PLUGIN_BREAK=active    control: removal only while this driver speaks -- other_synth must FAIL

  upgrade   a preview's unversioned ledger, "done" over a profile still on blazie: looked at once more, switched,
            the ledger now versioned (Reply 165, 1); a profile added after the base was migrated still inherits the
            base's Accent SA (Reply 165, 2)
  removal+  NVDA on another synth (Eloquence), everything migrated: the 0.7 add-ons offered and removed (Tomi: the
            ROG); NVDA on a 0.7 driver: kept, with the reason; the status lines name each profile and the reason

Exit 0 when all pass.  Build first: nvda/build_ssi263.py.
"""
import copy
import hashlib
import importlib
import json
import os
import shutil
import sys
import tempfile
import types

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PLUGINS = os.path.join(REPO, "nvda", "dist", "ssi263-build", "globalPlugins")
BREAK = os.environ.get("UNIFIED_PLUGIN_BREAK", "")
results = []


def check(name, ok, detail=""):
    results.append(ok)
    print("%s %-16s %s" % ("ok  " if ok else "FAIL", name, detail))


# ---- the stand-ins ---------------------------------------------------------------------------------------------
class _Log:
    def __getattr__(self, name):
        return lambda *a, **k: None


class Conf:
    """config.conf: the base and the profiles as stored (dicts), the active ones over the base"""

    def __init__(self, base, profiles, active=()):
        self.profiles = [base] + [profiles[n] for n in active]
        self.stored = profiles
        self._dirtyProfiles = set()
        self.saved = 0
        self.fail_save = False

    def listProfiles(self):
        return iter(sorted(self.stored))

    def _getProfile(self, name):
        return self.stored[name]

    def save(self):
        if self.fail_save:
            raise OSError("the disk is full")
        self.saved += 1

    def __getitem__(self, key):
        view = _View(self, key)
        return view


class _View(dict):
    def __init__(self, conf, key):
        super().__init__()
        self._cache = {}
        for p in conf.profiles:
            self.update(p.get(key, {}))


class Synth:
    def __init__(self, name, booted=True):
        self.name = name
        if name == "ssi263":         # this driver: its unit, booted or not
            ev = __import__("threading").Event()
            if booted:
                ev.set()
            self.inner = types.SimpleNamespace(booted=ev, bootError=None)


def install(tmp, conf, synth):
    for name in ("globalPluginHandler", "globalVars", "logHandler", "config", "synthDriverHandler"):
        sys.modules.pop(name, None)
    gph = types.ModuleType("globalPluginHandler")
    gph.GlobalPlugin = object
    gv = types.ModuleType("globalVars")
    gv.appArgs = types.SimpleNamespace(configPath=tmp, secure=False, launcher=False)
    lh = types.ModuleType("logHandler")
    lh.log = _Log()
    cfg = types.ModuleType("config")
    cfg.conf = conf
    sdh = types.ModuleType("synthDriverHandler")
    state = {"synth": synth, "set": [], "fail": ()}
    sdh.getSynth = lambda: state["synth"]

    def setSynth(name):
        state["set"].append(name)
        if name in state["fail"]:
            return False             # NVDA keeps the synth it had
        state["synth"] = Synth(name)
        return True
    sdh.setSynth = setSynth
    for m in (gph, gv, lh, cfg, sdh):
        sys.modules[m.__name__] = m
    for name in list(sys.modules):
        if name.startswith("ssi263Speech"):
            del sys.modules[name]
    sys.path.insert(0, PLUGINS)
    try:
        return importlib.import_module("ssi263Speech"), state
    finally:
        sys.path.remove(PLUGINS)


# ---- updates -----------------------------------------------------------------------------------------------------
def rel(tag, *names, draft=False, pre=False):
    return {"tag_name": tag, "draft": draft, "prerelease": pre,
            "assets": [{"name": n, "browser_download_url": "https://x/%s/%s" % (tag, n)} for n in names]}


def test_updates(up):
    if BREAK == "hash":
        up.verify = lambda data, sha: None
    addon = "ssi263-speech-0.8.1.nvda-addon"
    rels = [rel("v0.8.1", addon, "SHA256SUMS.txt"), rel("v0.8.0", "ssi263-speech-0.8.0.nvda-addon"),
            rel("v0.7.7", "blazie-ssi263-0.7.7.nvda-addon")]
    found = up.newest(rels, "0.8.0")
    check("newer", found is not None and found[0] == "0.8.1" and found[1] == addon, repr(found and found[:2]))
    check("same", up.newest(rels[1:], "0.8.0") is None)
    check("older", up.newest([rel("v0.7.9", "ssi263-speech-0.7.9.nvda-addon")], "0.8.0") is None)
    check("draft_pre", up.newest([rel("v0.9", "ssi263-speech-0.9.0.nvda-addon", draft=True),
                                  rel("v0.9b", "ssi263-speech-0.9.1.nvda-addon", pre=True)], "0.8.0") is None)
    t = up.newest([rel("v9.9.9", "ssi263-speech-0.8.2.nvda-addon", "SHA256SUMS.txt")], "0.8.0")
    check("name_over_tag", t is not None and t[0] == "0.8.2", repr(t and t[0]))
    data = b"the add-on's bytes"
    good = hashlib.sha256(data).hexdigest()

    def fetcher(sums):
        return lambda url: sums.encode() if url.endswith("SHA256SUMS.txt") else data

    def refused(found, sums):
        try:
            up.download(found, fetcher(sums))
            return None
        except up.UpdateError as e:
            return str(e)
    no_sums = (found[0], found[1], found[2], None)
    check("no_sums", refused(no_sums, "") is not None, refused(no_sums, "") or "ACCEPTED")
    why = refused(found, "%s  other.nvda-addon\n" % good)
    check("no_entry", why is not None, why or "ACCEPTED")
    why = refused(found, "%s  %s\n%s *%s\n" % (good, addon, "0" * 64, addon))
    check("two_entries", why is not None, why or "ACCEPTED")
    why = refused(found, "%s  %s\n" % ("1" * 64, addon))
    check("wrong_hash", why is not None, why or "ACCEPTED a wrong hash")
    try:
        ok = up.download(found, fetcher("%s  %s\n" % (good, addon))) == data
    except up.UpdateError as e:
        ok = False
    check("right_hash", ok)


# ---- migrate -----------------------------------------------------------------------------------------------------
def ledger_of(tmp):
    path = os.path.join(tmp, "ssi263-speech", "settings.json")
    return json.load(open(path, encoding="utf-8")).get("migrated") if os.path.isfile(path) else None


def test_migrate(tmp):
    base = {"speech": {"synth": "blazie", "blazie": {"voice": "blazie_es", "rate": "70", "whine": "low"},
                       "speakout": {"rate": "20"}, "accentmini": {"voice": "sa", "rate": "60"}}}
    profiles = {
        "reading": {"speech": {"synth": "accentmini", "accentmini": {"voice": "mini", "rate": "30"}}},
        "typing": {"speech": {"speakout": {"pitch": "60"}, "ssi263": {"fw_speakout_pitch": "10"}}},
        "plain": {"speech": {"rate": "40"}, "keyboard": {}},
        "fast": {"speech": {"blazie": {"rate": "25"}}},
        "sa": {"speech": {"synth": "accentmini"}},
    }
    conf = Conf(copy.deepcopy(base), copy.deepcopy(profiles), active=("typing",))
    plugin, state = install(tmp, conf, Synth("blazie"))
    mig = plugin.migrate
    if BREAK == "fill":
        orig = mig.migrate_profile

        def overwrite(raw, inherited=None, undo=None):
            dest = raw.get("speech", {}).get("ssi263")
            if dest:
                for k in list(dest):
                    if k.startswith("fw_"):
                        del dest[k]
            return orig(raw, inherited, undo)
        mig.migrate_profile = overwrite
    if BREAK == "ledger":
        mig.migrate_all = (lambda f: lambda profiles, ledger, undo=None: f(profiles, set(), undo))(mig.migrate_all)
        mig.mark_done = lambda ledger, profiles: None
    if BREAK == "inherit":
        mig._effective_voice = lambda old, speech, inherited: (speech.get(old) or {}).get("voice")
    switched = plugin.run_migration()
    b = conf.profiles[0]["speech"]
    new = b.get("ssi263", {})
    check("base_braillelite", b.get("synth") == "ssi263" and new.get("firmwareType") == "braillelite2000"
          and new.get("fw_braillelite2000_voice") == "es" and new.get("fw_braillelite2000_rate") == "70"
          and new.get("fw_braillelite2000_whine") == "low" and new.get("fw_speakout_rate") == "20"
          and "rate" not in new and "voice" not in new,
          "synth %s, %s" % (b.get("synth"), {k: v for k, v in new.items() if k.startswith(("fw_braille", "firm"))}))
    check("old_kept", b.get("blazie") == base["speech"]["blazie"])
    r = conf.stored["reading"]["speech"]
    check("inactive_accent", r.get("synth") == "ssi263" and r["ssi263"].get("firmwareType") == "accentmini"
          and r["ssi263"].get("fw_accentmini_rate") == "30" and r["ssi263"].get("fw_accentsa_rate") == "30",
          repr(r.get("ssi263")))
    sa = conf.stored["sa"]["speech"]
    check("inherited_accent", sa.get("synth") == "ssi263" and sa.get("ssi263") == {"firmwareType": "accentsa"},
          "a profile with only synth=accentmini, the base's voice sa: %r" % (sa.get("ssi263"),))
    f = conf.stored["fast"]["speech"]
    check("rate_only", f.get("ssi263") == {"fw_braillelite2000_rate": "25"} and "synth" not in f,
          "a profile with only [[blazie]] rate=25: %r" % (f.get("ssi263"),))
    t = conf.stored["typing"]["speech"]
    check("kept_existing", t["ssi263"].get("fw_speakout_pitch") == "10" and "synth" not in t,
          "fw_speakout_pitch %s (it was 10; the 0.7 section says 60); synth %s" % (t["ssi263"].get("fw_speakout_pitch"),
                                                                                    t.get("synth")))
    check("own_overrides", "ssi263" not in conf.stored["plain"]["speech"], repr(conf.stored["plain"]))
    check("live_switch", switched and state["set"] == ["ssi263"] and conf.saved == 1,
          "switched %s, setSynth %s, saves %d" % (switched, state["set"], conf.saved))
    ledger = ledger_of(tmp)
    # a second start: the user went back to the 0.7 driver by hand; the ledger keeps it that way
    conf.profiles[0]["speech"]["synth"] = "blazie"
    state["synth"] = Synth("blazie")
    plugin.run_migration()
    check("second_run", conf.profiles[0]["speech"]["synth"] == "blazie" and state["set"] == ["ssi263"],
          "ledger %s; synth now %s" % (ledger, conf.profiles[0]["speech"]["synth"]))

    # a failed save: undone in memory, the ledger unwritten, raised; once the disk works, saved whole
    tmp2 = tempfile.mkdtemp(prefix="ssi263-plugin-")
    conf2 = Conf(copy.deepcopy(base), {})
    conf2.fail_save = True
    plugin2, state2 = install(tmp2, conf2, Synth("blazie"))
    if BREAK == "rollback":
        plugin2.migrate.rollback = lambda undo: None
    try:
        plugin2.run_migration()
        raised = False
    except OSError:
        raised = True
    undone = "ssi263" not in conf2.profiles[0]["speech"] and conf2.profiles[0]["speech"]["synth"] == "blazie"
    check("failed_save", raised and ledger_of(tmp2) is None and undone and state2["set"] == [],
          "raised %s, ledger %s, undone in memory %s, setSynth %s" % (raised, ledger_of(tmp2), undone, state2["set"]))
    conf2.fail_save = False
    retried = plugin2.run_migration()
    check("save_retry", retried and conf2.saved == 1 and ledger_of(tmp2) == [""] and state2["set"] == ["ssi263"]
          and conf2.profiles[0]["speech"].get("ssi263", {}).get("firmwareType") == "braillelite2000",
          "saves %d, ledger %s, setSynth %s" % (conf2.saved, ledger_of(tmp2), state2["set"]))
    shutil.rmtree(tmp2, ignore_errors=True)

    # this driver fails to start after the save: every switched profile gets its own synth back, saved; the ledger
    # unwritten, so once it can start the next run switches again
    tmp3 = tempfile.mkdtemp(prefix="ssi263-plugin-")
    conf3 = Conf(copy.deepcopy(base), copy.deepcopy(profiles))
    plugin3, state3 = install(tmp3, conf3, Synth("blazie"))
    state3["fail"] = ("ssi263",)
    if BREAK == "revert":
        class Gone(tuple):
            """the old drivers, until setSynth was tried: then none (the revert finds nothing to put back)"""

            def __contains__(self, x):
                return not state3["set"] and tuple.__contains__(self, x)
        plugin3.migrate.OLD_DRIVERS = Gone(plugin3.migrate.OLD_DRIVERS)
    if BREAK == "pending":
        real_save = plugin3._save_settings

        def save_anyway(data):
            real_save(data)
        orig_run = plugin3.run_migration

        def marks_anyway():
            r = orig_run()
            data = plugin3._load_settings()
            data["migrated"] = ["", "fast", "plain", "reading", "sa", "typing"]
            data["version"] = 2
            real_save(data)
            return r
        plugin3.run_migration = marks_anyway
    switched = plugin3.run_migration()
    got = (conf3.profiles[0]["speech"]["synth"], conf3.stored["reading"]["speech"]["synth"])
    check("switch_failed", not switched and got == ("blazie", "accentmini") and conf3.saved == 2
          and state3["synth"].name == "blazie" and "reading" in conf3._dirtyProfiles,
          "synths now %s (were blazie, accentmini), saves %d, NVDA on %s" % (got, conf3.saved, state3["synth"].name))
    state3["fail"] = ()
    again = plugin3.run_migration()
    check("start_retry", again and state3["set"] == ["ssi263", "ssi263"] and state3["synth"].name == "ssi263"
          and ledger_of(tmp3) is not None and "" in ledger_of(tmp3),
          "retried %s, setSynth %s, NVDA on %s, ledger %s" % (again, state3["set"], state3["synth"].name,
                                                             ledger_of(tmp3)))
    shutil.rmtree(tmp3, ignore_errors=True)


# ---- removal ---------------------------------------------------------------------------------------------------
def test_removal():
    def session(synth, ledger):
        tmp = tempfile.mkdtemp(prefix="ssi263-plugin-")
        conf = Conf({"speech": {"synth": synth.name}}, {})
        plugin, state = install(tmp, conf, synth)
        if ledger is not None:
            plugin._save_settings({"migrated": ledger, "version": 2})
        removed, prompts = [], []
        plugin._old_addons = lambda: [types.SimpleNamespace(name="blazie_ssi263",
                                                            requestRemove=lambda: removed.append("blazie_ssi263"))]
        wx = types.ModuleType("wx")
        wx.YES, wx.NO, wx.YES_NO, wx.ICON_QUESTION, wx.OK, wx.ICON_WARNING = 1, 0, 2, 4, 8, 16
        gui = types.ModuleType("gui")
        answers = iter([wx.YES, wx.NO])

        def message_box(message, *a):
            prompts.append(message)
            return next(answers)
        gui.messageBox = message_box
        sys.modules["wx"], sys.modules["gui"] = wx, gui
        if BREAK == "ready":
            plugin.removal_ready = lambda: (True, "")
        if BREAK == "active":
            real_ready = plugin.removal_ready

            def only_active():
                r = real_ready()
                return r if state["synth"].name == "ssi263" else (False, "NVDA is not using this driver.")
            plugin.removal_ready = only_active
        plugin._unified_check = lambda: unified_ok
        plugin.offer_removal()
        lines = plugin.status()
        shutil.rmtree(tmp, ignore_errors=True)
        return removed, prompts, lines

    unified_ok = True

    removed, prompts, _l = session(Synth("blazie"), None)
    check("manual_removal", removed == [] and prompts and "kept for now" in prompts[0],
          "on blazie, nothing migrated, Yes pressed: removed %s; said %r" % (removed, (prompts or [""])[0][:70]))
    removed, _p, _l = session(Synth("ssi263", booted=False), [""])
    check("not_booted", removed == [], "this driver's unit not booted: removed %s" % removed)
    removed, _p, _l = session(Synth("ssi263"), [""])
    check("ready_removal", removed == ["blazie_ssi263"], "this driver speaking, all migrated: removed %s" % removed)
    removed, prompts, lines = session(Synth("eloquence"), [""])
    check("other_synth", removed == ["blazie_ssi263"] and prompts and "were moved" in prompts[0],
          "NVDA on Eloquence, all migrated: removed %s; asked %r" % (removed, (prompts or [""])[0][:60]))
    removed, prompts, lines = session(Synth("blazie"), [""])
    reason = [ln for ln in lines if ln.startswith("Removing them")]
    check("old_driver_on", removed == [] and reason and "0.7 blazie driver" in reason[0]
          and any(ln.startswith("The normal configuration: synthesizer blazie") for ln in lines),
          "NVDA on the 0.7 blazie driver: removed %s; status %r" % (removed, reason))
    unified_ok = False
    removed, _p, lines = session(Synth("eloquence"), [""])
    check("no_firmware", removed == [], "this driver finds no firmware: removed %s" % removed)


# ---- upgrade from a preview -------------------------------------------------------------------------------------
def test_upgrade():
    base = {"speech": {"synth": "blazie", "blazie": {"rate": "70"}, "accentmini": {"voice": "sa"}}}
    tmp = tempfile.mkdtemp(prefix="ssi263-plugin-")
    conf = Conf(copy.deepcopy(base), {})
    plugin, state = install(tmp, conf, Synth("blazie"))
    plugin._save_settings({"migrated": [""]})          # preview 3 after a failed switch: "done", still on blazie
    if BREAK == "version":
        plugin._ledger = lambda: set(plugin._load_settings().get("migrated") or [])
    switched = plugin.run_migration()
    data = plugin._load_settings()
    check("preview_ledger", switched and state["set"] == ["ssi263"] and data.get("version") == 2,
          "switched %s, setSynth %s, ledger %s" % (switched, state["set"], data))
    # a profile added after the base was migrated: only synth=accentmini, the base's voice sa
    conf.stored["late"] = {"speech": {"synth": "accentmini"}}
    if BREAK == "context":
        real = plugin.migrate.migrate_all

        def pending_only(profiles, ledger, undo=None):
            return real([p for p in profiles if p[0] not in ledger], ledger, undo)
        plugin.migrate.migrate_all = pending_only
    state["synth"] = Synth("ssi263")
    plugin.run_migration()
    late = conf.stored["late"]["speech"].get("ssi263", {})
    check("late_profile", late.get("firmwareType") == "accentsa",
          "the later profile: %r (want the base's Accent SA)" % late)
    shutil.rmtree(tmp, ignore_errors=True)


# ---- ring --------------------------------------------------------------------------------------------------------
def test_ring(rng):
    class Meta(type):
        """properties from _get_value/_set_value, as NVDA's AutoPropertyType makes them"""

        def __init__(cls, name, bases, ns):
            super().__init__(name, bases, ns)
            if "_set_value" in ns:
                cls.value = property(getattr(cls, "_get_value"), ns["_set_value"])

    store = {"speech": {"ssi263": {}, "other": {}}}

    class SynthSetting(metaclass=Meta):
        def __init__(self, synth, sid):
            self.synth, self.setting = synth, types.SimpleNamespace(id=sid)

        def _get_value(self):
            return getattr(self.synth, self.setting.id)

        def _set_value(self, v):
            setattr(self.synth, self.setting.id, v)
            store["speech"][self.synth.name][self.setting.id] = v

    class StringSynthSetting(SynthSetting):
        def _set_value(self, v):
            SynthSetting._set_value(self, v)

    synth = types.SimpleNamespace(name="ssi263", firmwareType="speakout", rate=50, variant="i")
    other = types.SimpleNamespace(name="other", firmwareType="speakout", rate=50)
    nvda_props = (SynthSetting.__dict__["value"], StringSynthSetting.__dict__["value"])
    hook = rng.Hook([SynthSetting, StringSynthSetting], lambda: store, _Log())
    hook.install()
    if BREAK == "ringkey":
        hook.active = False
    SynthSetting(synth, "rate").value = 35
    StringSynthSetting(synth, "variant").value = "a"
    StringSynthSetting(synth, "firmwareType").value = "speakout"
    SynthSetting(other, "rate").value = 20
    hook.restore()
    restored = (SynthSetting.__dict__["value"], StringSynthSetting.__dict__["value"]) == nvda_props
    got = store["speech"]["ssi263"]
    check("ring_key", got.get("fw_speakout_rate") == 35 and got.get("fw_speakout_variant") == "a"
          and got.get("rate") == 35 and "fw_speakout_firmwareType" not in got
          and store["speech"]["other"] == {"rate": 20}
          and restored, "the config %s; another driver %s; NVDA's properties back %s" % (got, store["speech"]["other"],
                                                                                         restored))


# ---- panel -------------------------------------------------------------------------------------------------------
class Combo:
    def __init__(self, items):
        self.items = list(items)

    def Set(self, items):
        self.items = list(items)


class Container:
    def __init__(self):
        self.sizerDict = {"firmwareType": 1, "voice": 1, "variant": 1, "rate": 1, "whine": 1}
        old = [types.SimpleNamespace(id=str(i), displayName="Tone %d" % i) for i in range(27)]
        self._variants, self.variantList = old, Combo(o.displayName for o in old)
        self._voices = [types.SimpleNamespace(id="en", displayName="English"),
                        types.SimpleNamespace(id="es", displayName="Español")]
        self.voiceList = Combo(o.displayName for o in self._voices)
        self.whineList = Combo(["Off"])
        self._whines = []
        self.updated = []

    def updateDriverSettings(self, changedSetting=None):
        self.updated.append(changedSetting)


def test_panel(pnl):
    if BREAK == "options":
        pnl.refresh = lambda container, driver: container.updateDriverSettings(changedSetting="firmwareType")
    tones = [types.SimpleNamespace(id=t, displayName="Tone %s" % t.upper()) for t in "abcdefghijklmnopqrstuvwxyz"]
    driver = types.SimpleNamespace(
        name="ssi263",
        supportedSettings=[types.SimpleNamespace(id=i) for i in ("firmwareType", "voice", "variant", "rate")],
        availableVariants={t.id: t for t in tones},
        availableVoices={"en": types.SimpleNamespace(id="en", displayName="English")})
    c = Container()
    pnl.refresh(c, driver)
    check("stale_choices", [o.id for o in c._variants] == list("abcdefghijklmnopqrstuvwxyz")
          and c.variantList.items[0] == "Tone A" and [o.id for o in c._voices] == ["en"]
          and c.voiceList.items == ["English"] and c.whineList.items == ["Off"] and c.updated == ["firmwareType"],
          "variants %s..., voices %s, whine untouched %s, updated %s" % ([o.id for o in c._variants][:3],
                                                                         [o.id for o in c._voices],
                                                                         c.whineList.items, c.updated))

    class Changer:
        def __call__(self, evt):
            evt.append("nvda")
    nvda_call = Changer.__call__
    hook = pnl.Hook(Changer, _Log())
    hook.install()
    ours = Changer.__call__
    hook.restore()
    restored = Changer.__call__ is nvda_call
    hook.install()
    ours = Changer.__call__

    def theirs(self, evt):
        evt.append("theirs")
        ours(self, evt)
    Changer.__call__ = theirs
    hook.restore()
    evt = []
    ch = Changer()
    ch.setting = types.SimpleNamespace(id="firmwareType")
    ch.driver = driver
    ch.container = Container()
    Changer.__call__(ch, evt)
    check("hook_restore", restored and Changer.__call__ is theirs and evt == ["theirs", "nvda"]
          and ch.container.updated == [], "restored %s; wrapped: kept theirs, ours passes through %s" % (restored, evt))


def main():
    if not os.path.isdir(os.path.join(PLUGINS, "ssi263Speech")):
        print("no globalPlugins in nvda/dist/ssi263-build: run nvda/build_ssi263.py first")
        return 1
    tmp = tempfile.mkdtemp(prefix="ssi263-plugin-")
    try:
        plugin, _state = install(tmp, Conf({}, {}), None)
        test_updates(plugin.updates)
        test_panel(plugin.panel)
        test_ring(plugin.ring)
        test_migrate(tmp)
        test_removal()
        test_upgrade()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("unified plugin: %s" % ("all passed" if all(results) else "%d FAILED" % results.count(False)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
