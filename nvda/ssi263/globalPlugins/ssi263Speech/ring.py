# -*- coding: utf-8 -*-
"""NVDA's settings ring, for this add-on's driver (Astra, Reply 163, 2).

The ring writes a change to the config at once, under the setting's plain name (synthSettingsRing.SynthSetting's
value setter: config.conf["speech"][synth.name][setting.id] = value).  synthDrivers/ssi263.py keeps each firmware
type's values under fw_<type>_<setting> and never reads the plain names, so the ring's write would only last until
the next config save (saveSettings writes the live values then).  This hook writes the same value to the active
firmware's own key as well, the moment the ring writes the plain one: a profile switch or a reload in between keeps
it.  The firmware type itself and the voice are left alone (the type is the plain key firmwareType; the ring saves
the whole driver after a voice change).

The ring's classes are AutoPropertyObjects: `value` is a property made when the class was, so the hook replaces each
class's `value` property and restore() puts each one back only while ours is still the one there.
"""

DRIVER = "ssi263"
SKIP = ("firmwareType", "voice")


def mem_key(fw, setting_id):
    return "fw_%s_%s" % (fw, setting_id)


class Hook:
    def __init__(self, classes, conf_getter, log):
        self.classes, self.conf, self.log = classes, conf_getter, log
        self.installed = []          # (class, NVDA's property, ours)
        self.active = False

    def _wrap(self, prop):
        hook = self

        def setter(ring_setting, value):
            prop.fset(ring_setting, value)
            if not hook.active:
                return
            try:
                synth, sid = ring_setting.synth, ring_setting.setting.id
                if getattr(synth, "name", None) != DRIVER or sid in SKIP:
                    return
                hook.conf()["speech"][DRIVER][mem_key(synth.firmwareType, sid)] = getattr(synth, sid)
            except Exception:
                hook.log.error("SSI-263: a settings ring change was not kept for its firmware", exc_info=True)
        return property(prop.fget, setter, prop.fdel, prop.__doc__)

    def install(self):
        for cls in self.classes:
            prop = cls.__dict__.get("value")
            if not isinstance(prop, property) or prop.fset is None:
                continue
            mine = self._wrap(prop)
            setattr(cls, "value", mine)
            self.installed.append((cls, prop, mine))
        self.active = bool(self.installed)

    def restore(self):
        self.active = False
        for cls, prop, mine in self.installed:
            if cls.__dict__.get("value") is mine:
                setattr(cls, "value", prop)
            else:
                self.log.debugWarning("SSI-263: another add-on replaced the ring's %s.value; ours passes through"
                                      % cls.__name__)
        self.installed = []
