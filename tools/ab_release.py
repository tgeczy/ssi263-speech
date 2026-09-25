"""A/B renders of the release_lookahead change, for listening: each word through the Braille
Lite firmware (z180emu) and the Speak-Out firmware (Unicorn), A = the v0.10 release (every
stop releases early), B = release only into an open phoneme, held otherwise.

    python tools/ab_release.py
"""
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.join(os.path.dirname(HERE), "src")
sys.path.insert(0, ENGINE)
from hosts.blazie import Blazie          # noqa: E402
from hosts.speakout import SpeakOut      # noqa: E402
from ssi263 import SSI263                # noqa: E402
from ssi263.native import SSI263C        # noqa: E402

B = r"C:\git\ssi263-speech\nvda\dist\blazie-build\synthDrivers\_ssi263_blazie"
HEX = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "firmware", "gw-micro-speakout", "SPEAKOUT.HEX")
WORDS = ["manager.", "storage.", "image.", "message.", "judge.", "pages.", "large.", "changing.",
         "management.", "knowledge.", "program."]
OUT = os.path.join(os.path.dirname(ENGINE), "investigation", "out")
GAP = np.zeros(int(0.35 * 44100))


def blazie(over):
    chip = SSI263(over, dsp="c")
    u = Blazie(B + r"\bns_live.exe", B + r"\BL2ENG.BNS", B + r"\bl2_2003_warm.state", chip=chip,
               menu=("punct_none", "numbers_toggle"))
    u.run(0.3)
    out = []
    for w in WORDS:
        u.say([w])
        got = []
        while True:
            got.append(np.asarray(u.run(0.02), float))
            if not u.busy():
                break
        y = np.concatenate(got)
        on = np.where(np.abs(y) > 0.003)[0]
        out += [y[max(0, on[0] - 220):on[-1] + 2200] if len(on) else y, GAP]
    u.close()
    return np.concatenate(out)


def speakout(over):
    s = SpeakOut(HEX, chip=SSI263C(over, out_rate=44100))
    s.boot()
    out = []
    for w in WORDS:
        s.say(w + "\r")
        got = [np.asarray(s.run(0.1), float)]
        while s.busy():
            got.append(np.asarray(s.run(0.05), float))
        y = np.concatenate(got)
        on = np.where(np.abs(y) > 0.003)[0]
        out += [y[max(0, on[0] - 220):on[-1] + 2200] if len(on) else y, GAP]
    return np.concatenate(out)


def save(name, y):
    y = y / (np.abs(y).max() + 1e-9) * 0.7
    path = os.path.join(OUT, name)
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(44100)
        f.writeframes((np.clip(y, -1, 1) * 32767).astype("<i2").tobytes())
    print("wrote", path, "%.1f s" % (len(y) / 44100))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for tag, over in (("A-old", {"release_lookahead": False}), ("B-held", {"fricative_precharge": False}),
                      ("C-precharged", {})):
        save("dge-blazie-%s.wav" % tag, blazie(over))
        save("dge-speakout-%s.wav" % tag, speakout(over))
