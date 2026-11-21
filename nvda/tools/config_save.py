"""A configuration save runs only the unified driver's own save, not its unit drivers' (issue #72).

Each unit driver inside the unified one is a real NVDA SynthDriver: NVDA's AutoSettings registers its stock
saveSettings on config.pre_configSave when it is made, and nothing took it off.  So every configuration save (NVDA+
Ctrl+C, or saving at exit) also wrote [speech][blazie] (or speakout, accentmini, ...): a KeyError logged with a
traceback where that section is absent, the 0.7 add-on's section overwritten where it is.  The stand-in NVDA's
SynthDriver registers its save as AutoSettings does.

  inner     after the unified driver starts, and after a switch to another firmware type: a save runs no unit
            driver's stock save, only the unified driver's

    python config_save.py                                   exit 1 on any failure
    CONFIG_SAVE_BREAK=1   the unified driver's QUIET_INNER_SAVE off: the unit drivers' saves left on -- must fail
"""
import os
import sys
import time

sys.argv = sys.argv[:1] + ["ssi263"]
if os.environ.get("CONFIG_SAVE_BREAK"):
    os.environ["SSI263_QUIET_INNER_SAVE_BREAK"] = "1"
HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, "fake_nvda_driver_test.py"), encoding="utf-8").read()
exec(src.split("time.sleep(2.0)")[0])
time.sleep(1.0)
bad = 0


def save(label):
    global bad
    del config_saves[:]
    config.pre_configSave.notify()
    inner = [n for n in config_saves if n != d.name]
    ok = not inner
    bad += not ok
    print("%-4s inner     %s: a save ran %s" % ("ok" if ok else "FAIL", label,
                                               ", ".join(config_saves) or "nothing of the stand-in's"))


save("the unified driver started (%s)" % d._fw)
others = [fw for fw in drv_mod.present_types() if fw != d._fw]
if others:
    d._set_firmwareType(others[0])
    time.sleep(0.5)
    save("switched to %s" % others[0])
print("config save: %s" % ("%d FAILED" % bad if bad else "all ok"))
sys.stdout.flush()
d.terminate()
os._exit(1 if bad else 0)
