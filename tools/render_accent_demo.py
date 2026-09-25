"""The Accent demo dialogue (as Tomi transcribed accent-demo.wav) through the emulated
Accent-mini: Aicom's SPKEMS.DVC driving the engine's SSI-263.  Default voice throughout --
the recording's pitch / rate / voice changes are not reproduced.  For listening next to
the recording only (an external clip: never tuned on).

    python tools/render_accent_demo.py [path\\to\\SPKEMS.DVC]
"""
import os
import sys
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "src"))
from hosts.accent import Accent          # noqa: E402
from ssi263.native import SSI263C        # noqa: E402

DVC = sys.argv[1] if len(sys.argv) > 1 else \
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "firmware", "aicom-accent-mini", "SPKEMS.DVC")
LINES = [
    "Hi, I am Accent. Have you ever heard a computer talking?",
    "Yes, of course I have!",
    "No, I mean, have you ever heard a real computer talking? Your pitch is too high and your "
    "speech rate is too slow.",
    "By your command, is this acceptable to you?",
    "Well, that was better, but I still don't like your robotic intonation. Can you change that?",
    "By your command, certainly, I can use any pitch, speech rate, or any type of voice "
    "personality as you command.",
    "Fantastic! Now, speak something as slow as you can, then as fast as you can.",
    "This is the slowest story. And now, this is the fastest story.",
    "Not too bad, so far. Now, let's see if you can speak for yourself with your grammatical ability.",
    "By your command, let me say this to demonstrate my capability. I'd like to present the "
    "present to you, and after that, we can record a pillow talk on a record.",
    "Hey, stop that. What kind of elegance is that?",
    "I am sorry, it just slipped out of my mouth. But don't think a computer has no feeling. "
    "And your language is not very polite either, you turkey!",
    "Gosh, you really are something, aren't you? Alright, let's both behave ourselves.",
]


def main():
    chip = SSI263C()
    a = Accent(DVC, chip=chip)
    a.keep_writes = True
    a.init()
    t0 = time.perf_counter()
    out = [a.run(2.0)]                              # "Accent ready."
    names = chip.rom.names
    for line in LINES:
        a.say(line + "\r")
        out.append(a.run(0.1))
        # the driver queues sentences and speaks them in turn: wait for real silence
        while a.busy(quiet=1.5, patience=5.0):
            out.append(a.run(0.05))
    spoken = [names.get(v & 0x3F, "?") for _, r, v in a.writes if r == 0 and v & 0x3F]
    print("phonemes spoken: %d; last: %s" % (len(spoken), " ".join(spoken[-30:])))
    y = chip.dsp.concat(out)
    os.makedirs(os.path.join(os.path.dirname(HERE), "investigation", "out"), exist_ok=True)
    path = os.path.join(os.path.dirname(HERE), "investigation", "out", "accent-demo-emulated.wav")
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(44100)
        f.writeframes(chip.dsp.pcm16(y, 1.0))
    print("wrote %s: %.1f s of audio in %.1f s" % (path, len(y) / 44100.0, time.perf_counter() - t0))


if __name__ == "__main__":
    main()
