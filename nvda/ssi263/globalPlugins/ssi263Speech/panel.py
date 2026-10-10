# -*- coding: utf-8 -*-
"""The Voice settings panel after the firmware type changes (design-0.8-nvda-addon.md 2; Reply 161, 1).

NVDA's panel refreshes its controls only when the voice changes, and even then a string setting's choices stay the
ones it was made with: _makeStringSettingControl keeps them (container._<id>s) and _updateValueForControl looks the
new value up there.  So after a firmware change every string control the panel has -- shown or hidden -- gets the new
unit's choices, both the kept options and the wx.Choice's items, and then NVDA's own updateDriverSettings hides what
the new unit lacks, makes what it adds, and sets every value.  The firmware control itself keeps its place and focus.

The hook wraps gui.settingsDialogs.StringDriverSettingChanger.__call__ for this add-on's driver and its firmware
setting only; restore() puts NVDA's back only while ours is still the one installed (another add-on may have wrapped
ours since: then ours just passes through).
"""

DRIVER = "ssi263"
SETTING = "firmwareType"


def refresh(container, driver):
    """the panel's controls for the driver's current firmware type"""
    supported = {s.id: s for s in driver.supportedSettings}
    for sid in list(container.sizerDict):
        if sid == SETTING or sid not in supported:
            continue
        combo = getattr(container, "%sList" % sid, None)
        if combo is None:            # a slider or a checkbox: updateDriverSettings sets its value
            continue
        options = list(getattr(driver, "available%ss" % sid.capitalize()).values())
        setattr(container, "_%ss" % sid, options)
        combo.Set([o.displayName for o in options])
    container.updateDriverSettings(changedSetting=SETTING)


class Hook:
    def __init__(self, changer_cls, log):
        self.cls, self.log = changer_cls, log
        self.active = False
        self.orig = None
        self.mine = None

    def install(self):
        orig = self.cls.__call__
        hook = self

        def call(changer, evt):
            orig(changer, evt)
            if not hook.active:
                return
            try:
                if getattr(changer.setting, "id", None) == SETTING and getattr(changer.driver, "name", None) == DRIVER:
                    refresh(changer.container, changer.driver)
            except Exception:
                hook.log.error("SSI-263: the voice settings were not refreshed after the firmware change",
                               exc_info=True)
        self.orig, self.mine = orig, call
        self.cls.__call__ = call
        self.active = True

    def restore(self):
        self.active = False
        if self.mine is not None and self.cls.__call__ is self.mine:
            self.cls.__call__ = self.orig
        elif self.mine is not None:
            self.log.debugWarning("SSI-263: another add-on wrapped the settings changer after us; ours passes through")
        self.mine = None
