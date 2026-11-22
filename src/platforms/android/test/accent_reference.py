"""The Accent-PC's reference for test_android_native.py: the NVDA Accent add-on's driver as 0.7.0 shipped it, in Python
(nvda/tools/legacy_drivers.py; its "sa" voice on the desktop's C host: accent_sa.dll, SSI263_ACCENT_SA_CORE=c), under the
stand-in NVDA of nvda/tools/fake_nvda_driver_test.py, speaks each case on a freshly booted unit -- as the Android
engine gives each utterance a unit of its own -- and every PCM byte it feeds its player is hashed.

    python accent_reference.py <cases.json>

cases.json: {"speech": [{"name", "text", "rate", "offset", "inflection", "volume", "sample_rate", "blocks"}...],
             "texts": [...]}
  rate, inflection, volume: the driver's settings (NVDA's scales; volume may pass 100: the gain volume / 100)
  offset: a PitchCommand(offset) before the text, as NVDA sends for a capital (0: none); the driver's pitch stays 50
  blocks: only the first that many non-empty blocks (the Android engine's stopped case), 0 = all

Prints "<name> <samples> <fnv-1a 64>" per case and "text <i> <hex>" per text: the driver's currencies, _clean, strip
and _numbers.  Exit 1 when the driver does not finish.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
HARNESS = os.path.join(REPO, "nvda", "tools", "fake_nvda_driver_test.py")


def fnv(data):
    h = 1469598103934665603
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def main():
    cases = json.load(open(sys.argv[1], encoding="utf-8"))
    os.environ.setdefault("SSI263_ACCENT_SA_CORE", "c")      # the old driver's own name for it (not 0.9.5's _PC_)
    # 0.7.0's Python driver: since 0.7.5 the add-on's driver speaks through as_voice itself, the code under test here
    # (nvda/tools/native_driver_equiv.py holds the two byte for byte), so the reference stays the Python one
    sys.path.insert(0, os.path.dirname(HARNESS))
    import legacy_drivers
    os.environ["SSI263_SYNTH_DRIVERS"] = legacy_drivers.synth_drivers("accent")
    sys.argv = [HARNESS, "accent"]
    src = open(HARNESS, encoding="utf-8").read().split("time.sleep(2.0)")[0]
    g = {"__file__": HARNESS, "__name__": "accent_reference_harness"}
    exec(compile(src, HARNESS, "exec"), g)
    d, drv, PitchCommand, wait_idle, player = g["d"], g["drv_mod"], g["PitchCommand"], g["wait_idle"], g["FakePlayer"]

    raw = []
    feed = player.feed

    def hooked(self, data, onDone=None):           # every player the driver makes (a new rate makes a new one)
        if data:
            raw.append(bytes(data))
        return feed(self, data, onDone)
    player.feed = hooked
    if "sa" not in d._present():
        sys.exit("the built Accent add-on has no Accent-PC ROMs")
    d._set_voice("sa")
    for c in cases["speech"]:
        d._want_rate = c["sample_rate"]
        d._booted = None                           # a fresh unit: the worker boots one before this job
        d._rate, d._pitch, d._inflection, d._volume, d._numbers = c["rate"], 50, c["inflection"], c["volume"], True
        seq = ([PitchCommand(c["offset"])] if c["offset"] else []) + [c["text"]]
        del raw[:]
        d.speak(seq)
        if not wait_idle(60):
            print("the driver did not finish %s: %s" % (c["name"], g["last_wait_error"][0]))
            d.terminate()
            return 1
        pcm = b"".join(raw[:c["blocks"]] if c["blocks"] else raw)
        print("%s %d %s" % (c["name"], len(pcm) // 2, fnv(pcm)))
    for i, text in enumerate(cases["texts"]):
        t = drv._clean(drv.numwords.currencies(text)).strip()
        t = drv._numbers(t)
        print("text %d %s" % (i, t.encode("latin-1").hex()))
    d.terminate()
    return 0


if __name__ == "__main__":
    sys.exit(main())
