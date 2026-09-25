"""A phrase from the Accent demo, real recording against the emulated Accent-mini, as a
spectrogram PNG with the emulated phoneme loads marked (for looking, not listening).

accent-demo.wav's opening was declared DEV (2026-09-25); on 2026-09-26 the line "I still
don't like your robotic intonation" joined it, after a listener pointed at a click in the
T of "still".  accent.wav (H9) is never read here.

    python tools/accent_spec.py "Well, that was better, but I still don't like your robotic intonation." out.png [zoom_word]
"""
import os
import sys

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt                  # noqa: E402
import librosa                                   # noqa: E402
import librosa.display                           # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.join(os.path.dirname(HERE), "src")
sys.path.insert(0, ENGINE)
sys.path.insert(0, HERE)
from hosts.accent import Accent                  # noqa: E402
from ssi263 import SSI263                        # noqa: E402
import wave                                      # noqa: E402

REAL = r"Y:\content from streamers\DecTalk archive\Synthesizers\accent-demo.wav"
DVC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "firmware", "aicom-accent-mini", "SPKEMS.DVC")


def load(path):
    with wave.open(path, "rb") as f:
        sr, n, w, ch = f.getframerate(), f.getnframes(), f.getsampwidth(), f.getnchannels()
        raw = f.readframes(n)
    y = (np.frombuffer(raw, np.uint8).astype(float) - 128) / 128 if w == 1 else np.frombuffer(raw, "<i2").astype(float) / 32768
    return y.reshape(-1, ch).mean(axis=1), sr


def render(text, over=None):
    chip = SSI263(over or {}, dsp="c")
    a = Accent(DVC, chip=chip)
    a.keep_writes = False
    a.boot()
    t0 = chip.time
    a.say(text + "\r")
    out = [a.run(0.1)]
    while a.busy(quiet=0.3, patience=3.0):
        out.append(a.run(0.05))
    y = np.asarray(chip.dsp.concat(out), float)
    names = chip.rom.names
    loads = [(t - t0, names.get(int(e[3:], 16) & 0x3F), int(e[3:], 16) >> 6) for t, e in chip.log
             if e.startswith("w0=") and t >= t0 and int(e[3:], 16) & 0x3F]
    on = np.where(np.abs(y) > 0.003)[0]
    s0 = on[0] / 44100.0
    return y[on[0]:on[-1]], [(t - s0, n, d) for t, n, d in loads]


def locate(real, rsr, ours):
    """Where the phrase sits in the real recording: sliding match of log-spectra."""
    import reclaim_words as RW
    RW.SR = rsr
    Fo = RW.features(librosa.resample(ours, orig_sr=44100, target_sr=rsr))
    Fr = RW.features(real)
    i, s = sorted(RW.match(Fr, Fo, 1), key=lambda p: -p[1])[0]
    return i * RW.HOP, s


def main():
    text, png = sys.argv[1], sys.argv[2]
    real, rsr = load(REAL)
    ours, loads = render(text)
    t, score = locate(real, rsr, ours)
    seg = real[int(max(0, t - 0.05) * rsr):int((t + len(ours) / 44100 * 1.2) * rsr)]
    print("phrase in the real demo at %.2f s (match %.2f), real rate %d Hz" % (t, score, rsr))
    fig, axes = plt.subplots(2, 1, figsize=(15, 8), sharex=True)
    for ax, sig, sr, title, marks in ((axes[0], seg, rsr, "real Accent demo", None),
                                     (axes[1], ours, 44100, "emulated Accent-mini", loads)):
        S = librosa.amplitude_to_db(np.abs(librosa.stft(sig, n_fft=512 if sr < 20000 else 1024,
                                                       hop_length=64 if sr < 20000 else 128)), ref=np.max)
        librosa.display.specshow(S, sr=sr, hop_length=64 if sr < 20000 else 128, x_axis="time", y_axis="hz",
                                 ax=ax, vmin=-70)
        ax.set_ylim(0, min(sr / 2, 8000))
        ax.set_title(title)
        for tt, n, d in marks or []:
            if tt >= 0:
                ax.axvline(tt, color="w", lw=0.5)
                ax.text(tt + 0.003, 0.93 * min(sr / 2, 8000), "%s/%d" % (n, d), color="w", fontsize=7, rotation=90)
    fig.tight_layout()
    fig.savefig(png, dpi=80)
    print(png)


if __name__ == "__main__":
    main()
