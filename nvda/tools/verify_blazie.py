"""One Braille Lite session, several ways: python verify_blazie.py MODE  (old | x64 | x86 | x86_37)
-> blz_<mode>.bin ; python verify_blazie.py compare a:b ..."""
import os
import struct
import sys
import time
from array import array

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, REPO)
from tools import repo_paths             # noqa: E402

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
OLD = repo_paths.engine_dir("blazie")
NEW = repo_paths.SRC
EXE64 = os.path.join(repo_paths.external("Z180EMU"), "bns_live.exe")
EXE32 = os.path.join(repo_paths.engine_dir("blazie"), "bns_live.exe")
FW = os.path.join(OLD, "BL2ENG.BNS")
STATE = os.path.join(OLD, "bl2_2003_warm.state")
TEXTS = [["Hello there."], ["Select, press, two sixty three.", "Is this a question?"],
         "\x055P", ["Pitch five now."], "\x0516P\x0514E", ["Faster at fourteen."]]


def session(mode):
    if mode == "old":
        sys.path[:0] = [OLD, os.path.join(OLD, "lib")]
        from blazie_host import Blazie
        from ssi263 import SSI263
        chip, exe = SSI263(out_rate=44100), EXE64
    else:
        sys.path[:0] = [NEW]
        from hosts.blazie import Blazie
        from ssi263 import SSI263
        if mode.startswith("cc"):
            from ssi263.native import SSI263C
            chip = SSI263C(out_rate=44100)
        else:
            chip = SSI263(out_rate=44100, dsp="c")
        exe = EXE64 if mode == "x64" else EXE32
    u = Blazie(exe, FW, STATE, chip=chip, out_rate=44100)
    t0 = time.perf_counter()
    out = [u.run(0.3)]
    for t in TEXTS:
        if isinstance(t, str):
            u.send(t.encode("latin-1"))
            out.append(u.run(0.05))
            continue
        u.turbo_between_lines = True
        u.say(t)
        while True:
            out.append(u.run(0.02))
            if not u.busy():
                break
    u.say(["This line gets cut off right about here, and then it goes on and on."])
    for _ in range(40):
        out.append(u.run(0.02))
    u.cancel()
    u.say(["After."])
    while True:
        out.append(u.run(0.02))
        if not u.busy():
            break
    u.close()
    el = time.perf_counter() - t0
    y = array("d")
    for b in out:
        y.extend(b if isinstance(b, array) else array("d", memoryview(b).cast("B").tobytes()))
    open(os.path.join(HERE, "blz_%s.bin" % mode), "wb").write(y.tobytes())
    print("%s: %.2f s audio in %.2f s cpu, python %s %d-bit"
          % (mode, len(y) / 44100, el, sys.version.split()[0], 8 * struct.calcsize("P")))


def compare(a, b):
    ya, yb = (array("d", open(os.path.join(HERE, "blz_%s.bin" % m), "rb").read()) for m in (a, b))
    n = min(len(ya), len(yb))
    dmax = max((abs(ya[i] - yb[i]) for i in range(n)), default=0.0)
    print("%s vs %s: samples %d/%d; max |diff| %.3g" % (a, b, len(ya), len(yb), dmax))


if __name__ == "__main__":
    if sys.argv[1] == "compare":
        for pair in sys.argv[2:]:
            compare(*pair.split(":"))
    else:
        session(sys.argv[1])
