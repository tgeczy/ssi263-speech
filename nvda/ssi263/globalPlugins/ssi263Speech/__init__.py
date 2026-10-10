# -*- coding: utf-8 -*-
"""The 0.8 add-on's global plugin: what the driver (synthDrivers/ssi263.py) cannot do itself.

  the Voice panel   refreshed after a firmware change (panel.py)
  a settings page   "SSI-263 speech chip synthesizer" in NVDA's Settings, and a Tools menu item that opens it: the
                    units this add-on found, the version, Check for updates (by hand only), and removing the 0.7
                    add-ons
  the migration     once per profile, the 0.7 add-ons' settings into this driver's (migrate.py), then NVDA switched
                    to this driver live when it was on one of them -- NVDA picks its synth before global plugins
                    start, so editing the config alone would only take effect next time
  the 0.7 add-ons   offered for removal once, after the migration is saved and this driver speaks; a "No" is kept

Nothing here runs on secure screens or touches the network unless asked (Tomi: updates are always manual).  The
add-on's own small settings -- the migration ledger and the removal answer -- are in
<NVDA's config folder>/ssi263-speech/settings.json; the voice settings are NVDA's own.
"""
import json
import os
import tempfile
import threading

import globalPluginHandler
import globalVars
from logHandler import log

from . import migrate, panel, ring, updates

OLD_ADDONS = ("blazie_ssi263", "accent_ssi263", "speakout_ssi263")
TITLE = "SSI-263 speech chip synthesizer"


def _settings_path():
    return os.path.join(globalVars.appArgs.configPath, "ssi263-speech", "settings.json")


def _load_settings():
    try:
        with open(_settings_path(), encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


def _save_settings(data):
    path = _settings_path()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1, sort_keys=True)
    os.replace(tmp, path)


def _version():
    try:
        import addonHandler
        return addonHandler.getCodeAddon().version
    except Exception:
        return "0"


def _old_addons():
    """the 0.7 add-ons still installed and not already being removed"""
    try:
        import addonHandler
        return [a for a in addonHandler.getRunningAddons() if a.name in OLD_ADDONS and not a.isPendingRemove]
    except Exception:
        log.debugWarning("SSI-263: could not list the add-ons", exc_info=True)
        return []


def _units():
    """what the driver can run, in words ("Braille Lite 2000 (English, Español), Speak-Out, ...")"""
    try:
        from synthDrivers import ssi263
        out = []
        for fw in ssi263.present_types():
            langs = [ssi263.LANGUAGES.get(lang, lang) for lang in ssi263.present_languages(fw)]
            out.append(ssi263.FIRMWARE_TYPES[fw][0] + (" (%s)" % ", ".join(langs) if len(langs) > 1 else ""))
        return ", ".join(out) or "none"
    except Exception:
        log.debugWarning("SSI-263: could not list the units", exc_info=True)
        return "unknown"


# ---- updates, by hand -------------------------------------------------------------------------------------------
def check_for_updates(parent=None):
    import gui
    import wx
    current = _version()

    def work():
        try:
            found = updates.check(current)
        except updates.UpdateError as e:
            wx.CallAfter(gui.messageBox, str(e), "Check for updates", wx.OK | wx.ICON_ERROR, parent)
            return
        wx.CallAfter(offer, found)

    def offer(found):
        if found is None:
            gui.messageBox("You have the newest version, %s." % current, "Check for updates", wx.OK, parent)
            return
        version, name = found[0], found[1]
        if gui.messageBox("Version %s is available (you have %s). Download it now? Its checksum is checked first, "
                          "then NVDA asks you to confirm the installation." % (version, current),
                          "Check for updates", wx.YES_NO | wx.ICON_QUESTION, parent) != wx.YES:
            return
        threading.Thread(target=fetch, args=(found,), name="ssi263-update", daemon=True).start()

    def fetch(found):
        try:
            data = updates.download(found)
            path = os.path.join(tempfile.gettempdir(), found[1])
            with open(path, "wb") as f:
                f.write(data)
        except (updates.UpdateError, OSError) as e:
            wx.CallAfter(gui.messageBox, "Nothing was installed. %s" % e, "Check for updates",
                         wx.OK | wx.ICON_ERROR, parent)
            return
        wx.CallAfter(os.startfile, path)      # NVDA opens .nvda-addon files and asks before installing

    threading.Thread(target=work, name="ssi263-update-check", daemon=True).start()


# ---- removing the 0.7 add-ons ---------------------------------------------------------------------------------
def _profiles(conf):
    """the base configuration ("") and every stored profile, raw"""
    out = [("", conf.profiles[0])]
    for name in conf.listProfiles():
        try:
            out.append((name, conf._getProfile(name)))
        except Exception:
            log.warning("SSI-263: profile %r could not be read; not migrated" % name, exc_info=True)
    return out


def removal_ready():
    """(True, "") when the 0.7 add-ons may go: NVDA speaks with this driver, its unit booted, and every profile's
    migration saved; else (False, why).  Both the start-up offer and the settings page's button ask this first: a
    Yes cannot make those true (Astra, Reply 164)."""
    import config
    import synthDriverHandler
    synth = synthDriverHandler.getSynth()
    if synth is None or synth.name != migrate.NEW:
        return False, "NVDA is not using Votrax SC-02 / SSI-263 (emulated) yet."
    inner = getattr(synth, "inner", None)
    if inner is None or not getattr(getattr(inner, "booted", None), "is_set", lambda: True)() \
            or getattr(inner, "bootError", None) is not None:
        return False, "Votrax SC-02 / SSI-263 (emulated) has not started its unit."
    ledger = set(_load_settings().get("migrated") or [])
    missing = [n for n, _raw in _profiles(config.conf) if n not in ledger]
    if missing:
        return False, "The older add-ons' settings have not all been moved yet (%s)." % ", ".join(
            n or "the base configuration" for n in missing)
    return True, ""


def offer_removal(parent=None, asked_at_start=False):
    import gui
    import wx
    old = _old_addons()
    if not old:
        if not asked_at_start:
            gui.messageBox("The 0.7 SSI-263 add-ons are not installed.", TITLE, wx.OK, parent)
        return
    ready, why = removal_ready()
    if not ready:
        if not asked_at_start:
            gui.messageBox("The older SSI-263 add-ons are kept for now: %s Switch NVDA to Votrax SC-02 / SSI-263 "
                           "(emulated) in Select Synthesizer, restart NVDA, and try again." % why, TITLE,
                           wx.OK | wx.ICON_WARNING, parent)
        return
    names = ", ".join(a.manifest.get("summary", a.name) if hasattr(a, "manifest") else a.name for a in old)
    answer = gui.messageBox("The older SSI-263 add-ons are now part of this one, and NVDA is using it: %s. "
                            "Remove them? (You can also do it later from this add-on's settings page.)" % names,
                            TITLE, wx.YES_NO | wx.ICON_QUESTION, parent)
    data = _load_settings()
    if answer != wx.YES:
        data["removalDeclined"] = True
        _save_settings(data)
        return
    failed = []
    for a in old:
        try:
            a.requestRemove()
        except Exception:
            log.error("SSI-263: could not remove %s" % a.name, exc_info=True)
            failed.append(a.name)
    if failed:
        gui.messageBox("NVDA did not remove %s. Remove them in NVDA's Add-on Store (Installed add-ons)."
                       % ", ".join(failed), TITLE, wx.OK | wx.ICON_WARNING, parent)
        return
    if gui.messageBox("They are removed when NVDA restarts. Restart NVDA now?", TITLE,
                      wx.YES_NO | wx.ICON_QUESTION, parent) == wx.YES:
        import core
        core.restart()


# ---- the migration ---------------------------------------------------------------------------------------------
def _clear_cache(conf):
    try:
        conf["speech"]._cache.clear()   # the aggregated view of the raw profiles just edited
    except Exception:
        pass


def run_migration():
    """the 0.7 settings moved (migrate.py) and saved, then this driver selected live; True when NVDA now speaks with
    it after a switch this made.  The ledger is written only when it all held (Reply 163, 5): a save that fails is
    rolled back in memory and raises (the next start tries it whole again); a switch that fails puts every profile's
    old synth back, saves, and leaves the ledger alone, so the next start tries again."""
    import config
    conf = config.conf
    data = _load_settings()
    ledger = set(data.get("migrated") or [])
    todo = [(n, r) for n, r in _profiles(conf) if n not in ledger]
    if not todo:
        return False
    before = {n: (r.get("speech") or {}).get("synth") if isinstance(r.get("speech"), dict) else None
              for n, r in todo}
    undo = []
    changed = migrate.migrate_all(todo, ledger, undo)
    if changed:
        for name, keys in changed:
            log.info("SSI-263: moving the 0.7 settings of %s: %s" % (name or "the base configuration",
                                                                     ", ".join(keys)))
            if name:
                conf._dirtyProfiles.add(name)
        try:
            conf.save()
        except Exception:
            migrate.rollback(undo)
            _clear_cache(conf)
            log.error("SSI-263: the moved settings could not be saved; undone in memory, tried again at the next "
                      "start", exc_info=True)
            raise
        _clear_cache(conf)
    import synthDriverHandler
    synth = synthDriverHandler.getSynth()
    switched = False
    if synth is not None and synth.name in migrate.OLD_DRIVERS and conf["speech"]["synth"] == migrate.NEW:
        try:
            switched = bool(synthDriverHandler.setSynth(migrate.NEW)) and synthDriverHandler.getSynth().name == \
                migrate.NEW
        except Exception:
            log.error("SSI-263: starting this driver failed", exc_info=True)
            switched = False
        if not switched:
            # the old synth keeps working, now and at the next start: every profile this switched gets its own back
            log.error("SSI-263: the settings were moved, but NVDA could not start this driver; it keeps %s"
                      % synth.name)
            for name, raw in todo:
                speech = raw.get("speech")
                if (isinstance(speech, dict) and speech.get("synth") == migrate.NEW
                        and before.get(name) in migrate.OLD_DRIVERS):
                    speech["synth"] = before[name]
                    if name:
                        conf._dirtyProfiles.add(name)
            conf.save()
            _clear_cache(conf)
            current = synthDriverHandler.getSynth()
            if current is None or current.name != synth.name:
                synthDriverHandler.setSynth(synth.name)
            return False
    migrate.mark_done(ledger, todo)
    data["migrated"] = sorted(ledger)
    _save_settings(data)
    return switched


# ---- the settings page --------------------------------------------------------------------------------------------
def _panel_class():
    import gui
    import wx
    from gui import guiHelper
    from gui.settingsDialogs import SettingsPanel

    class SSI263Panel(SettingsPanel):
        title = TITLE

        def makeSettings(self, sizer):
            h = guiHelper.BoxSizerHelper(self, sizer=sizer)
            h.addItem(wx.StaticText(self, label="Version %s. Units found: %s." % (_version(), _units())))
            check = h.addItem(wx.Button(self, label="&Check for updates..."))
            check.Bind(wx.EVT_BUTTON, lambda evt: check_for_updates(self))
            if _old_addons():
                remove = h.addItem(wx.Button(self, label="&Remove the 0.7 SSI-263 add-ons..."))
                remove.Bind(wx.EVT_BUTTON, lambda evt: offer_removal(self))

        def onSave(self):
            pass

    return SSI263Panel


class GlobalPlugin(globalPluginHandler.GlobalPlugin):
    def __init__(self):
        super().__init__()
        self._hook = self._ringHook = self._panel = self._menuItem = None
        if globalVars.appArgs.secure:
            return
        try:
            import gui
            self._hook = panel.Hook(gui.settingsDialogs.StringDriverSettingChanger, log)
            self._hook.install()
        except Exception:
            log.error("SSI-263: the Voice panel refresh is not installed", exc_info=True)
        try:
            import config
            import synthSettingsRing
            self._ringHook = ring.Hook([getattr(synthSettingsRing, n) for n in
                                        ("SynthSetting", "StringSynthSetting", "BooleanSynthSetting")
                                        if hasattr(synthSettingsRing, n)], lambda: config.conf, log)
            self._ringHook.install()
        except Exception:
            log.error("SSI-263: the settings ring hook is not installed", exc_info=True)
        try:
            self._register()
        except Exception:
            log.error("SSI-263: the settings page is not registered", exc_info=True)
        if not getattr(globalVars.appArgs, "launcher", False):
            import wx
            wx.CallAfter(self._start)

    def _register(self):
        import gui
        import wx
        self._panel = _panel_class()
        gui.settingsDialogs.NVDASettingsDialog.categoryClasses.append(self._panel)
        tools = gui.mainFrame.sysTrayIcon.toolsMenu
        self._menuItem = tools.Append(wx.ID_ANY, "%s..." % TITLE)
        gui.mainFrame.sysTrayIcon.Bind(wx.EVT_MENU, self._openPanel, self._menuItem)

    def _openPanel(self, evt):
        import gui
        gui.mainFrame.popupSettingsDialog(gui.settingsDialogs.NVDASettingsDialog, self._panel)

    def _start(self):
        try:
            switched = run_migration()
        except Exception:
            log.error("SSI-263: moving the 0.7 add-ons' settings failed; nothing was changed for good",
                      exc_info=True)
            return
        if switched:
            try:
                import ui
                ui.message("Your SSI-263 settings were moved into the new add-on.")
            except Exception:
                pass
        if _old_addons() and not _load_settings().get("removalDeclined") and removal_ready()[0]:
            offer_removal(asked_at_start=True)

    def terminate(self):
        if self._hook is not None:
            self._hook.restore()
        if self._ringHook is not None:
            self._ringHook.restore()
        try:
            import gui
            if self._panel in gui.settingsDialogs.NVDASettingsDialog.categoryClasses:
                gui.settingsDialogs.NVDASettingsDialog.categoryClasses.remove(self._panel)
            if self._menuItem is not None:
                gui.mainFrame.sysTrayIcon.toolsMenu.Remove(self._menuItem)
                self._menuItem.Destroy()
        except Exception:
            log.debugWarning("SSI-263: the settings page was not removed cleanly", exc_info=True)
        super().terminate()
