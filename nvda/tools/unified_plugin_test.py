"""The 0.8 add-on's global plugin (globalPlugins/ssi263Speech) under stand-ins: the update check, the migration of the
0.7 add-ons' settings, and the Voice panel's refresh after a firmware change.

  updates   a newer release found by its asset's name (not its tag); the same and older versions, a draft and a
            prerelease not offered; the download refused without SHA256SUMS.txt, with no entry or two entries for the
            file, or with a wrong hash, and taken with the right one
  migrate   each 0.7 driver's section into its firmware types' own keys (the Braille Lite's Spanish voice as es,
            the Accent SA's as accentsa); the synth switched with that type's plain keys; a profile's own overrides
            only (nothing written into one with no 0.7 section); a key already set kept; the old sections kept; a
            second run writing nothing (the ledger); a failed save leaving the ledger unwritten; NVDA switched live
  panel     every string control given the new unit's choices (kept options and items) before NVDA's update; the
            hook restored only while ours is installed, passing through otherwise

    UNIFIED_PLUGIN_BREAK=hash      control: any download accepted -- wrong_hash must FAIL
    UNIFIED_PLUGIN_BREAK=fill      control: the migration overwrites a key already set -- kept_existing must FAIL
    UNIFIED_PLUGIN_BREAK=ledger    control: the ledger ignored -- second_run must FAIL
    UNIFIED_PLUGIN_BREAK=options   control: the choices not replaced -- stale_choices must FAIL
    UNIFIED_PLUGIN_BREAK=revert    control: no profile's synth put back when this driver fails to start --
                                   switch_failed must FAIL

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
    def __init__(self, name):
        self.name = name


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
def test_migrate(tmp):
    base = {"speech": {"synth": "blazie", "blazie": {"voice": "blazie_es", "rate": "70", "whine": "low"},
                       "speakout": {"rate": "20"}}}
    profiles = {
        "reading": {"speech": {"synth": "accentmini", "accentmini": {"voice": "sa", "rate": "30"}}},
        "typing": {"speech": {"speakout": {"pitch": "60"}, "ssi263": {"fw_speakout_pitch": "10"}}},
        "plain": {"speech": {"rate": "40"}, "keyboard": {}},
    }
    conf = Conf(copy.deepcopy(base), copy.deepcopy(profiles), active=("typing",))
    plugin, state = install(tmp, conf, Synth("blazie"))
    mig = plugin.migrate
    if BREAK == "fill":
        orig = mig.migrate_profile

        def overwrite(raw):
            dest = raw.get("speech", {}).get("ssi263")
            if dest:
                for k in list(dest):
                    if k.startswith("fw_"):
                        del dest[k]
            return orig(raw)
        mig.migrate_profile = overwrite
    if BREAK == "ledger":
        plugin.migrate.migrate_all = (lambda f: lambda profiles, ledger: f(profiles, set()))(mig.migrate_all)
        mig_mark = mig.mark_done
        mig.mark_done = lambda ledger, profiles: None
    switched = plugin.run_migration()
    b = conf.profiles[0]["speech"]
    new = b.get("ssi263", {})
    check("base_braillelite", b.get("synth") == "ssi263" and new.get("firmwareType") == "braillelite2000"
          and new.get("voice") == "es" and new.get("rate") == "70" and new.get("fw_braillelite2000_voice") == "es"
          and new.get("fw_braillelite2000_whine") == "low" and new.get("fw_speakout_rate") == "20",
          "synth %s, %s" % (b.get("synth"), {k: v for k, v in new.items() if not k.startswith("fw_speakout")}))
    check("old_kept", b.get("blazie") == base["speech"]["blazie"])
    r = conf.stored["reading"]["speech"]
    check("inactive_accent", r.get("synth") == "ssi263" and r["ssi263"].get("firmwareType") == "accentsa"
          and r["ssi263"].get("fw_accentsa_rate") == "30" and r["ssi263"].get("fw_accentmini_rate") == "30"
          and r["ssi263"].get("voice") == "en", repr(r.get("ssi263")))
    t = conf.stored["typing"]["speech"]
    check("kept_existing", t["ssi263"].get("fw_speakout_pitch") == "10" and "synth" not in t,
          "fw_speakout_pitch %s (it was 10; the 0.7 section says 60); synth %s" % (t["ssi263"].get("fw_speakout_pitch"),
                                                                                    t.get("synth")))
    check("own_overrides", "ssi263" not in conf.stored["plain"]["speech"], repr(conf.stored["plain"]))
    check("live_switch", switched and state["set"] == ["ssi263"] and conf.saved == 1,
          "switched %s, setSynth %s, saves %d" % (switched, state["set"], conf.saved))
    ledger = json.load(open(os.path.join(tmp, "ssi263-speech", "settings.json"), encoding="utf-8"))["migrated"]
    # a second start: the user went back to the 0.7 driver by hand; the ledger keeps it that way
    conf.profiles[0]["speech"]["synth"] = "blazie"
    state["synth"] = Synth("blazie")
    plugin.run_migration()
    check("second_run", conf.profiles[0]["speech"]["synth"] == "blazie" and state["set"] == ["ssi263"],
          "ledger %s; synth now %s" % (ledger, conf.profiles[0]["speech"]["synth"]))

    # a failed save: the ledger is not written
    tmp2 = tempfile.mkdtemp(prefix="ssi263-plugin-")
    conf2 = Conf(copy.deepcopy(base), {})
    conf2.fail_save = True
    plugin2, _state = install(tmp2, conf2, Synth("blazie"))
    try:
        plugin2.run_migration()
        raised = False
    except OSError:
        raised = True
    check("failed_save", raised and not os.path.isfile(os.path.join(tmp2, "ssi263-speech", "settings.json")),
          "raised %s, ledger written: %s" % (raised, os.path.isfile(os.path.join(tmp2, "ssi263-speech",
                                                                                 "settings.json"))))
    shutil.rmtree(tmp2, ignore_errors=True)

    # this driver fails to start after the save: every switched profile gets its own synth back, saved
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
    switched = plugin3.run_migration()
    got = (conf3.profiles[0]["speech"]["synth"], conf3.stored["reading"]["speech"]["synth"])
    check("switch_failed", not switched and got == ("blazie", "accentmini") and conf3.saved == 2
          and state3["synth"].name == "blazie" and "reading" in conf3._dirtyProfiles,
          "synths now %s (were blazie, accentmini), saves %d, NVDA on %s" % (got, conf3.saved, state3["synth"].name))
    shutil.rmtree(tmp3, ignore_errors=True)


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
        pnl.refresh = (lambda f: lambda container, driver: container.updateDriverSettings(changedSetting="firmwareType"))(
            pnl.refresh)
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
        test_migrate(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("unified plugin: %s" % ("all passed" if all(results) else "%d FAILED" % results.count(False)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
