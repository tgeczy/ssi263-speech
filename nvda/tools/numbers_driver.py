"""Braille Lite driver, number processing on vs off, to WAV for whisper."""
import os
import sys
import time
import wave

sys.argv = [sys.argv[0], "blazie"]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake_nvda_driver_test.py"),
          encoding="utf-8").read().split("time.sleep(2.0)")[0])
time.sleep(2.0)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
TEXT = "The file is 1,234,567 bytes, and the debt is 100000000000000 dollars."
for on in (True, False):
    d._numbers = on
    mark = len(notified)
    n0 = len(d._player.chunks)
    d.speak([TEXT])
    wait_idle()
    y = audio_since(n0)
    with wave.open(os.path.join(OUT, "drv_numbers_%s.wav" % ("on" if on else "off")), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes((np.clip(y, -1, 1) * 32767).astype("<i2").tobytes())
    print("numbers %s: %.2f s" % (on, len(y) / 44100))
d.terminate()
