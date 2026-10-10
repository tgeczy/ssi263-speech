# -*- coding: utf-8 -*-
"""The 0.7 add-ons' settings, moved into this add-on's driver (design-0.8-nvda-addon.md 3, and its revisions after
Astra's Reply 161, 4).

Each 0.7 driver's section -- [speech][blazie], [speech][accentmini], [speech][speakout] -- becomes that firmware
type's own values in [speech][ssi263] (fw_<type>_<setting>, the keys synthDrivers/ssi263.py keeps per type): the
Braille Lite 2000's from blazie (voice blazie -> en, blazie_es -> es), the Speak-Out's from speakout, and both
Accents' from accentmini (it was one set for both).  Where the profile's synth was one of the three, it becomes
ssi263 on that firmware type -- the Accent the old driver had (its voice; mini when none was saved) -- with that
type's values under their plain names too, so NVDA loads them.

Every profile is migrated as it is stored, its own overrides only: a key the profile does not set is not written
(never the base's or a default into a profile), a destination key already set is left alone, and the old sections
stay as they were, so going back to the 0.7 add-ons still works.  The ledger (settings.json) names each profile done
after its save succeeded; a profile already in it is not touched again.

No NVDA here: the functions work on the raw profile (a ConfigObj, or any dict of dicts), so the tests drive them on
stand-ins (nvda/tools/unified_migrate_test.py).
"""

OLD_DRIVERS = ("blazie", "accentmini", "speakout")
# old driver -> the firmware types its values go to
TYPES = {"blazie": ("braillelite2000",), "accentmini": ("accentsa", "accentmini"), "speakout": ("speakout",)}
# old driver -> {its voice: the language the new driver calls it}
VOICES = {"blazie": {"blazie": "en", "blazie_es": "es"}, "accentmini": {"sa": "en", "mini": "en"},
          "speakout": {"speakout": "en"}}
NEW = "ssi263"


def mem_key(fw, setting_id):
    """synthDrivers/ssi263.py's key for a firmware type's own value"""
    return "fw_%s_%s" % (fw, setting_id)


def active_type(old, section):
    """the firmware type a profile whose synth was `old` becomes"""
    if old == "accentmini":
        return "accentsa" if section.get("voice") == "sa" else "accentmini"
    return TYPES[old][0]


def _values(old, section):
    """the old section's own values (no subsections), its voice as the new driver's language"""
    out = []
    for key in list(section.keys()):
        val = section[key]
        if isinstance(val, dict):
            continue
        if key == "voice":
            val = VOICES[old].get(val)
            if val is None:
                continue
        out.append((key, val))
    return out


def migrate_profile(raw):
    """Move one stored profile's 0.7 settings; the keys written, in order (empty: nothing to do)."""
    speech = raw.get("speech") if hasattr(raw, "get") else None
    if not isinstance(speech, dict):
        return []
    written = []
    dest = speech.get(NEW)

    def put(key, val):
        nonlocal dest
        if dest is None:
            speech[NEW] = {}
            dest = speech[NEW]
        if key not in dest:
            dest[key] = val
            written.append("%s.%s" % (NEW, key))

    for old in OLD_DRIVERS:
        section = speech.get(old)
        if not isinstance(section, dict) or not section:
            continue
        for fw in TYPES[old]:
            for key, val in _values(old, section):
                put(mem_key(fw, key), "en" if key == "voice" and old == "accentmini" else val)
    synth = speech.get("synth")
    if synth in OLD_DRIVERS:
        section = speech.get(synth) if isinstance(speech.get(synth), dict) else {}
        fw = active_type(synth, section)
        if dest is None or "firmwareType" not in dest:
            put("firmwareType", fw)
            for key, val in _values(synth, section):
                put(key, "en" if key == "voice" and synth == "accentmini" else val)
        speech["synth"] = NEW
        written.append("synth")
    return written


def migrate_all(profiles, ledger):
    """profiles: [(name, raw)] with "" the base config; ledger: the names already done (a set, changed in place
    only by mark_done).  Returns [(name, keys written)] for the profiles changed, the ledger's untouched."""
    changed = []
    for name, raw in profiles:
        if name in ledger:
            continue
        written = migrate_profile(raw)
        if written:
            changed.append((name, written))
    return changed


def mark_done(ledger, profiles):
    """after their save succeeded: every profile looked at goes into the ledger (also those with nothing to move)"""
    for name, _raw in profiles:
        ledger.add(name)
