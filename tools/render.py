"""Render the engine's milestone material, each wav with a JSON sidecar.

  hello-stage1 / hello-stage8   the data book's Hello register streams (A, 1985
                                p. 1-54), XCK 1 MHz, FF E9 -- manufacturer test
                                vectors, independent of Blazie and of any fit
  master-6306                   MASTER script line 6306 ("Hello Tomi. This is the
                                Braille Lite ...") from the EMULATED firmware stream,
                                punctuation tokens left out; HOLD-OUT material
  master-6306-real              the same line cut from the real recording

Nothing here was tuned to the hold-out.  See ../src/HOLDOUT.md.
"""
import argparse
import hashlib
import json
import os
import sys

import numpy as np
import soundfile as sf

ENGINE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src")
sys.path.insert(0, ENGINE)
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools import repo_paths             # noqa: E402
from ssi263 import SSI263, ENGINE_VERSION, drivers  # noqa: E402

REPO = os.path.dirname(ENGINE)
HELLO = os.path.join(REPO, "investigation", "die-review", "manufacturer-docs",
                     "hello-stages-1-and-8.csv")
PRED = repo_paths.master_predictions()
MASTER = repo_paths.master_session()
OUT = os.path.join(os.path.dirname(ENGINE), "investigation", "out")
SR = 44100


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def write(name, y, meta):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".wav")
    sf.write(path, np.clip(y, -1, 1).astype(np.float32), SR, subtype="PCM_16")
    with open(os.path.join(OUT, name + ".json"), "w") as f:
        json.dump(meta, f, indent=1, default=str)
    rms = np.sqrt(np.mean(y ** 2)) if len(y) else 0
    print("%-28s %6.2f s  rms %6.1f dBFS  peak %6.1f dBFS"
          % (name + ".wav", len(y) / SR, 20 * np.log10(rms + 1e-12),
             20 * np.log10(np.abs(y).max() + 1e-12)))


def render_rows(name, rows, source, extra=None):
    chip = SSI263(out_rate=SR)
    y = drivers.play_rows(chip, rows)
    meta = {"engine": ENGINE_VERSION, "label": "ENGINE v0 -- not a verified recreation",
            "source": source, "rows": rows, "driver_order": drivers.ORDER_ATTR_FIRST,
            "rom_bits_sha256": sha(os.path.join(ENGINE, "data", "rom_bits.csv")),
            "params": chip.p.manifest(), "events": chip.log}
    meta.update(extra or {})
    write(name, y, meta)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-real", action="store_true")
    a = ap.parse_args()
    for stage in (1, 8):
        rows = drivers.hello_rows(HELLO, stage)
        render_rows("hello-stage%d" % stage, rows,
                    {"file": HELLO, "sha256": sha(HELLO), "stage": stage,
                     "note": "SSI 263A User's Guide Hello example; XCK 1 MHz assumed"})
    rows, x = drivers.master_rows(PRED, 6306)
    render_rows("master-6306", rows,
                {"file": PRED, "sha256": sha(PRED), "script_line": 6306, "text": x["text"],
                 "note": "EMULATED 2003 firmware stream; r1 held at the base value, so the "
                         "real unit's '.' falls are absent here"},
                {"holdout": True})
    if not a.no_real:
        with open(os.path.join(MASTER, "utterances.jsonl")) as f:
            for line in f:
                u = json.loads(line)
                if u["script_line"] == 6306:
                    break
        ev = u["events"]
        with sf.SoundFile(os.path.join(MASTER, "master.wav")) as f:
            f.seek(ev["text_sent"])
            y = f.read(ev["end_with_tail"] - ev["text_sent"])
        y = y if y.ndim == 1 else y[:, 0]
        write("master-6306-real", y,
              {"label": "REAL Braille Lite recording (MASTER, 2026-09-24)",
               "utterance": u, "master_wav_sha256_frozen":
               "e7cc0c4d7bbaa3e6452ad4408e268275bc3988e6747022841c3bc9e3bb3250e5"})


if __name__ == "__main__":
    main()
