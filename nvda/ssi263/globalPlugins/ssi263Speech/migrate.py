# -*- coding: utf-8 -*-
"""The 0.7 add-ons' settings, moved into this add-on's driver (design-0.8-nvda-addon.md 3; Astra's Replies 161, 4
and 163, 3 and 5).

Each 0.7 driver's section -- [speech][blazie], [speech][accentmini], [speech][speakout] -- becomes that firmware
type's own values in [speech][ssi263] (fw_<type>_<setting>, the only keys synthDrivers/ssi263.py reads for a type's
values): the Braille Lite 2000's from blazie (voice blazie -> en, blazie_es -> es), the Speak-Out's from speakout,
and both Accents' from accentmini (it was one set for both).  Where the profile's synth was one of the three, it
becomes ssi263, with firmwareType the unit the old driver ran: for the Accent, the model the profile EFFECTIVELY had
(its own voice, else the base configuration's, else mini) -- the selector read through inheritance, the values copied
from the profile's own keys only, two different things (Reply 163, 3).

Every profile is migrated as it is stored, its own overrides only: a key the profile does not set is not written
(never the base's or a default into a profile), a destination key already set is left alone, and the old sections
stay as they were, so going back to the 0.7 add-ons still works.  Every write is logged, so a save that fails can be
undone in memory (rollback) and tried again whole at the next start (Reply 163, 5b).

No NVDA here: the functions work on the raw profile (a ConfigObj, or any dict of dicts), so the tests drive them on
stand-ins (nvda/tools/unified_plugin_test.py).
"""

OLD_DRIVERS = ("blazie", "accentmini", "speakout")
# old driver -> the firmware types its values go to
TYPES = {"blazie": ("braillelite2000",), "accentmini": ("accentsa", "accentmini"), "speakout": ("speakout",)}
# old driver -> {its voice: the language the new driver calls it}
VOICES = {"blazie": {"blazie": "en", "blazie_es": "es"}, "accentmini": {"sa": "en", "mini": "en"},
          "speakout": {"speakout": "en"}}
NEW = "ssi263"
_MISSING = object()


def mem_key(fw, setting_id):
    """synthDrivers/ssi263.py's key for a firmware type's own value"""
    return "fw_%s_%s" % (fw, setting_id)


def active_type(old, voice):
    """the firmware type a profile whose synth was `old` becomes, given the voice it effectively had"""
    if old == "accentmini":
        return "accentsa" if voice == "sa" else "accentmini"
    return TYPES[old][0]


def _values(old, section):
    """the old section's own values (no subsections), its voice as the new driver's language"""
    out = []
    for key in list(section.keys()):
        val = section[key]
        if isinstance(val, dict):
            continue
        if key == "voice":
            val = "en" if old == "accentmini" else VOICES[old].get(val)
            if val is None:
                continue
        out.append((key, val))
    return out


def _effective_voice(old, speech, inherited):
    """the old driver's voice as the profile had it: its own, else the base configuration's"""
    own = speech.get(old) if isinstance(speech.get(old), dict) else {}
    if "voice" in own:
        return own["voice"]
    base = inherited.get(old) if isinstance(inherited, dict) and isinstance(inherited.get(old), dict) else {}
    return base.get("voice")


def migrate_profile(raw, inherited=None, undo=None):
    """Move one stored profile's 0.7 settings; the keys written, in order (empty: nothing to do).  inherited: the
    base configuration's [speech] as it was before any migration (for a profile; None for the base itself).  undo:
    a list that gets (section, key, its old value or _MISSING) for every write, oldest first."""
    speech = raw.get("speech") if hasattr(raw, "get") else None
    if not isinstance(speech, dict):
        return []
    written = []

    def put(section, key, val, label):
        if undo is not None:
            undo.append((section, key, section[key] if key in section else _MISSING))
        section[key] = val
        written.append(label)

    dest = [speech.get(NEW)]

    def dest_section():
        if dest[0] is None:
            if undo is not None:
                undo.append((speech, NEW, _MISSING))
            speech[NEW] = {}
            dest[0] = speech[NEW]
        return dest[0]

    def fill(key, val):
        d = dest[0]
        if d is not None and key in d:
            return
        put(dest_section(), key, val, "%s.%s" % (NEW, key))

    for old in OLD_DRIVERS:
        section = speech.get(old)
        if not isinstance(section, dict) or not section:
            continue
        for fw in TYPES[old]:
            for key, val in _values(old, section):
                fill(mem_key(fw, key), val)
    synth = speech.get("synth")
    if synth in OLD_DRIVERS:
        fill("firmwareType", active_type(synth, _effective_voice(synth, speech, inherited or {})))
        put(speech, "synth", NEW, "synth")
    return written


def rollback(undo):
    """every write of a failed migration undone in memory, newest first"""
    for section, key, old in reversed(undo):
        if old is _MISSING:
            if key in section:
                del section[key]
        else:
            section[key] = old
    del undo[:]


def _plain(d):
    """a copy of a [speech] section's old-driver parts, as they were (the base's inherited context)"""
    out = {}
    if isinstance(d, dict):
        for k, v in d.items():
            out[k] = dict(v) if isinstance(v, dict) else v
    return out


def migrate_all(profiles, ledger, undo=None):
    """profiles: [(name, raw)] with "" the base config; ledger: the names already done (read only here).  Returns
    [(name, keys written)] for the profiles changed.  The base's selectors, as they were, are every profile's
    inherited context."""
    base = next((raw for name, raw in profiles if name == ""), None)
    inherited = _plain(base.get("speech")) if base is not None and hasattr(base, "get") else {}
    changed = []
    for name, raw in profiles:
        if name in ledger:
            continue
        written = migrate_profile(raw, None if name == "" else inherited, undo)
        if written:
            changed.append((name, written))
    return changed


def mark_done(ledger, profiles):
    """after the save succeeded and NVDA runs this driver (or nothing needed switching): every profile looked at goes
    into the ledger (also those with nothing to move)"""
    for name, _raw in profiles:
        ledger.add(name)
