"""Hosts that drive the chip: they write registers and wait on A/R.

A driver is where a front end (the SSI data-book examples, a Blazie register
stream, later an Accent or Speakout adapter) meets the chip.  The chip model
never sees text.
"""
import csv
import json

ORDER_ATTR_FIRST = ("IS", "RE", "TA", "FF", "DP")   # phoneme load last
REG = {"DP": 0, "IS": 1, "RE": 2, "TA": 3, "FF": 4}


def init(chip, first, mode=3, log=None):
    """Power-up sequence: attributes with CTL set, mode bits in R0, then CTL 1 -> 0.

    The data book gives the register values, not this ordering; it is the
    minimum the mode chart requires (DR1 DR0 latched on CTL 1 -> 0).
    """
    chip.write(4, first["FF"])
    chip.write(2, first["RE"])
    chip.write(1, first["IS"])
    chip.write(3, 0x80 | (first["TA"] & 0x7F))
    chip.write(0, (mode << 6) | 0x00)
    chip.write(3, first["TA"] & 0x7F)


def play_rows(chip, rows, order=ORDER_ATTR_FIRST, tail_s=0.3, max_wait_s=5.0):
    """rows: dicts of register values.  Each row is written when A/R requests."""
    out = [chip.run(0.02)]
    init(chip, rows[0])
    for i, row in enumerate(rows):
        if i:
            out.append(chip.run_until_request(max_wait_s))
        for name in order:
            chip.write(REG[name], row[name])
    out.append(chip.run_until_request(max_wait_s))
    out.append(chip.run(tail_s))
    return chip.dsp.concat(out)


def hello_rows(path, stage):
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            if int(r["stage"]) == stage:
                rows.append({k: int(r[k], 16) for k in ("DP", "IS", "RE", "TA", "FF")}
                            | {"phoneme": r["phoneme"]})
    return rows


def master_rows(predictions_path, script_line, r1=None):
    """One MASTER line as rows, from the EMULATED firmware stream (z180emu).

    r1 defaults to the line's LAST r1 value, i.e. the punctuation tokens are
    left out: their placement is the open emulator-vs-unit disagreement.
    """
    with open(predictions_path) as f:
        for line in f:
            x = json.loads(line)
            if x["script_line"] == script_line:
                break
        else:
            raise KeyError(script_line)
    is_ = int(r1 if r1 is not None else x["r1_at_sends"][-1], 16)
    re_, ta, ff = int(x["r2"][-1], 16), int(x["r3"][-1], 16), int(x["r4"][-1], 16)
    silent = int(x["r3"][0], 16) if len(x["r3"]) > 1 else ta
    rows = []
    for s in x["sends"]:
        dp = int(s, 16)
        # the firmware's silent E'3 (C1) before an H is sent with r3 = 00, then r3 restored
        rows.append({"DP": dp, "IS": is_, "RE": re_, "TA": silent if dp == 0xC1 else ta, "FF": ff})
    return rows, x
