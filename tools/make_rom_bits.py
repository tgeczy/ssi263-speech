"""Build src/data/rom_bits.csv from Astra's reconciled physical ROM reading.

Source: investigation/die-review/rom-reconciled-physical.csv (Visual6502
SSI-263P die, 64 columns x 29 data rows; oval = 1).  Image column c holds
phoneme code 63 - c.  The 29 data rows, top to bottom, are b00..b27 then the
lone final row PAR.  Names come from die-review/bns-rom-index.csv.

The raw bits are kept as read.  What the bits MEAN is a separate, swappable
layer (ssi263/rom.py).  Differences from Casso's older rom-bits.csv are
printed, not resolved here.
"""
import csv
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.join(os.path.dirname(HERE), "src")
REPO = os.path.dirname(ENGINE)
REVIEW = os.path.join(REPO, "investigation", "die-review")
SRC = os.path.join(REVIEW, "rom-reconciled-physical.csv")
NAMES = os.path.join(REVIEW, "bns-rom-index.csv")
CASSO = os.path.join(REPO, "third_party", "casso", "specs", "024-mockingboard-speech",
                     "rom-extraction", "rom-bits.csv")
OUT = os.path.join(ENGINE, "data", "rom_bits.csv")

ROWS = ["b%02d" % i for i in range(28)] + ["PAR"]


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    cols = {}
    with open(SRC, newline="") as f:
        for r in csv.DictReader(f):
            if r["role"] != "data":
                continue
            c = int(r["column_left_to_right"])
            cols.setdefault(c, []).append((int(r["physical_row_top_to_bottom"]),
                                           1 if r["final_label"] == "oval" else 0))
    names = {}
    with open(NAMES, newline="") as f:
        for r in csv.DictReader(f):
            names[int(r["code"], 16)] = r["name"]
    if sorted(cols) != list(range(64)) or any(len(v) != 29 for v in cols.values()):
        sys.exit("unexpected lattice: %d columns" % len(cols))
    table = {}
    for c, cells in cols.items():
        bits = [b for _, b in sorted(cells)]
        table[63 - c] = bits
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="") as f:
        f.write("# raw ROM bits, oval = 1; source %s sha256 %s\n"
                % (os.path.relpath(SRC, REPO).replace("\\", "/"), sha256(SRC)))
        w = csv.writer(f)
        w.writerow(["code", "name"] + ROWS)
        for code in range(64):
            w.writerow(["0x%02X" % code, names.get(code, "")] + table[code])
    print("wrote", OUT)

    if os.path.exists(CASSO):
        diffs = []
        with open(CASSO, newline="") as f:
            for r in csv.DictReader(f):
                code = int(r["code"], 16)
                for i, k in enumerate(ROWS):
                    if int(r[k]) != table[code][i]:
                        diffs.append("0x%02X %s %s: casso %s, reconciled %s"
                                     % (code, r["name"], k, r[k], table[code][i]))
        print("differences from Casso rom-bits.csv: %d" % len(diffs))
        for d in diffs:
            print("  " + d)


if __name__ == "__main__":
    main()
