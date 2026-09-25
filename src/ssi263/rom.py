"""The phoneme ROM: raw bits as read from the die, and a swappable reading of them.

data/rom_bits.csv holds the 29 physical bits per code exactly as read
(tools/make_rom_bits.py).  A reading turns bits into fields.  The only reading
so far is the candidate decode (Casso 2026-08-26, significance-interleaved
nibbles, MSB..LSB); its field names are hypotheses (NAS -> F2Q is a tracing
hypothesis, PAR -> F1 bit 0 is disputed, no F4 source is assigned).
"""
import os

DATA = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data",
                    "rom_bits.csv")
ROWS = ["b%02d" % i for i in range(28)] + ["PAR"]

# field -> physical rows, most significant first
CANDIDATE = {
    "FA": ("b05", "b11", "b17", "b23"),
    "VA": ("b06", "b12", "b18", "b24"),
    "F3": ("b07", "b13", "b19", "b25"),
    "NAS": ("b08", "b14", "b20", "b26"),
    "F2": ("b09", "b15", "b21", "b27"),
    "F1": ("b10", "b16", "b22", "PAR"),
}
FLAGS = {"closure_clear": "b00", "class1": "b01", "class2": "b02",
         "not_fricative": "b03", "not_voiced": "b04"}


class Rom:
    def __init__(self, bits, names, reading=None):
        self.bits = bits            # code -> {row: 0/1}
        self.names = names          # code -> name
        self.reading = reading or CANDIDATE

    @classmethod
    def load(cls, path=DATA, reading=None):
        bits, names = {}, {}
        # plain comma-separated, no quoting; read by hand because csv is not in the
        # stdlib that NVDA 2021-2023 ship (the add-ons run there)
        with open(path, newline="") as f:
            rows = [ln.rstrip("\r\n").split(",") for ln in f if ln.strip() and not ln.startswith("#")]
        for vals in rows[1:]:
            r = dict(zip(rows[0], vals))
            code = int(r["code"], 16)
            bits[code] = {k: int(r[k]) for k in ROWS}
            names[code] = r["name"]
        return cls(bits, names, reading)

    def field_bits(self, code, field):
        """The field's bits, LSB first (b0, b1, b2, b3)."""
        return tuple(self.bits[code][row] for row in reversed(self.reading[field]))

    def field(self, code, field):
        return sum(b << i for i, b in enumerate(self.field_bits(code, field)))

    def entry(self, code):
        e = {f: self.field(code, f) for f in self.reading}
        e["bits"] = {f: self.field_bits(code, f) for f in self.reading}
        e.update({k: self.bits[code][row] for k, row in FLAGS.items()})
        e["name"] = self.names[code]
        return e

    def code(self, name):
        for c, n in self.names.items():
            if n == name:
                return c
        raise KeyError(name)
