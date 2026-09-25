"""One Speak-Out session, three ways: python verify_nonumpy.py old|numpy|c  -> out_<mode>.bin/.json;
python verify_nonumpy.py compare.  Runs under any Python 3.7+ (no numpy needed for old/c... old needs it)."""
import json
import os
import sys
import time

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
OLD = r"C:\git\ssi263-speech\nvda\dist\speakout-build\synthDrivers\_ssi263_speakout"
NEW = r"C:\git\ssi263-speech\src"
HEX = r"C:\git\ssi263-speech\nvda\dist\speakout-build\synthDrivers\_ssi263_speakout\SPEAKOUT.HEX"
TEXTS = ["\x05Mn\x05R5\x05P3\x05Ti", "Hello there. Select, press, two sixty three.\r",
         "Is this a question?\r", "\x05R9\x05Tq", "ETI-Eloquence at rate nine.\r", "\x05R2\x05P7\x05Tc",
         "Slow and high, tone c.\r"]


def session(mode):
    from array import array
    if mode == "old":
        sys.path[:0] = [OLD, os.path.join(OLD, "lib")]
        from speakout_host import SpeakOut
        from ssi263 import SSI263
        chip = SSI263(out_rate=44100)
    else:
        sys.path[:0] = [NEW]
        from hosts.speakout import SpeakOut
        from ssi263 import SSI263
        if mode.startswith("cc"):
            from ssi263.native import SSI263C
            chip = SSI263C(out_rate=44100)
        else:
            chip = SSI263(out_rate=44100, dsp=mode.replace("37", ""))
    box = SpeakOut(HEX, chip=chip, out_rate=44100)
    t0 = time.perf_counter()
    box.boot()
    out = []
    for t in TEXTS:
        box.say(t)
        out.append(box.run(0.05))
        while box.busy():
            out.append(box.run(0.03))
    # a cancel mid-sentence, then more speech
    box.say("This sentence gets cut off right about here, and then it goes on.\r")
    for _ in range(20):
        out.append(box.run(0.03))
    box.cancel()
    box.say("After.\r")
    out.append(box.run(0.05))
    while box.busy():
        out.append(box.run(0.03))
    el = time.perf_counter() - t0
    y = array("d")
    for b in out:
        y.extend(array("d", bytes(memoryview(b).cast("B"))) if not isinstance(b, array) else b)
    if hasattr(chip, "dsp"):
        pcm = chip.dsp.pcm16(y, 1.0)
    else:
        import numpy as np
        pcm = (np.clip(np.asarray(y) * 1.0, -1.0, 1.0) * 32767.0).astype("<i2").tobytes()
    with open(os.path.join(HERE, "out_%s.bin" % mode), "wb") as f:
        f.write(y.tobytes())
    with open(os.path.join(HERE, "out_%s.pcm" % mode), "wb") as f:
        f.write(pcm)
    with open(os.path.join(HERE, "out_%s.json" % mode), "w") as f:
        json.dump({"writes": box.chip_writes, "seconds": len(y) / 44100, "cpu": el,
                   "python": sys.version, "bits": 8 * __import__("struct").calcsize("P")}, f)
    print("%s: %.2f s audio in %.2f s cpu, %d chip writes, python %s %d-bit"
          % (mode, len(y) / 44100, el, len(box.chip_writes), sys.version.split()[0],
             8 * __import__("struct").calcsize("P")))


def compare(a, b):
    from array import array
    ja, jb = (json.load(open(os.path.join(HERE, "out_%s.json" % m))) for m in (a, b))
    ya, yb = (array("d", open(os.path.join(HERE, "out_%s.bin" % m), "rb").read()) for m in (a, b))
    pa, pb = (array("h", open(os.path.join(HERE, "out_%s.pcm" % m), "rb").read()) for m in (a, b))
    same_w = ja["writes"] == jb["writes"]
    n = min(len(ya), len(yb))
    dmax = max((abs(ya[i] - yb[i]) for i in range(n)), default=0.0)
    lsb = max((abs(pa[i] - pb[i]) for i in range(min(len(pa), len(pb)))), default=0)
    nl = sum(1 for i in range(min(len(pa), len(pb))) if pa[i] != pb[i])
    print("%s vs %s: writes identical %s (%d/%d); samples %d/%d; max |diff| %.3g; pcm max %d LSB on %d samples"
          % (a, b, same_w, len(ja["writes"]), len(jb["writes"]), len(ya), len(yb), dmax, lsb, nl))


if __name__ == "__main__":
    if sys.argv[1] == "compare":
        for a, b in (("old", "numpy"), ("old", "c"), ("numpy", "c")) + tuple(
                tuple(x.split(":")) for x in sys.argv[2:]):
            if all(os.path.exists(os.path.join(HERE, "out_%s.json" % m)) for m in (a, b)):
                compare(a, b)
    else:
        session(sys.argv[1])
