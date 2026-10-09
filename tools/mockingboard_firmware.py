"""Make the Mockingboard voice's firmware file from Sweet Micro Systems' Mockingboard Developers Toolkit disk (1984;
its text-to-speech is version 1.1, 11 March 1985): the six DOS 3.3 binary files the voice runs, back to back, each
exactly as DOS stores it (load address, length, bytes).  Nothing else of the disk is taken -- not Apple's DOS, nor
any other program on it.

    python tools/mockingboard_firmware.py <the toolkit's .dsk> <output folder>

The disk must be the known image (DISK_SHA256, a 140 KB DOS-order .dsk); the output is checked against OUT_SHA256,
which src/csrc/mockingboard/mb_host.c also requires.
"""
import hashlib
import os
import sys

DISK_SHA256 = "7b2930489cfe8952d0338d2d8751cef3bdca004075161050481da8301d0136a2"
OUT_SHA256 = "88e1e90f1e76b7afa2f370db3c3bf34892c9621b5360304359242570b41bdfae"
OUT_NAME = "mockingboard-tts-1.1.bin"       # mb_host.h's MB_FILE
FILES = ["TEXT TO SPEECH", "INFLECTION", "IIE TTS DRIVER", "MKB:RULE.INDEX", "MKB:RULE.LENGTH", "MKB:RULE.TABLE"]
ADDRS = [0x8C00, 0x9000, 0x9300, 0xD000, 0xD100, 0xD200]


def sector(disk, t, s):
    o = (t * 16 + s) * 256
    return disk[o:o + 256]


def catalog(disk):
    """DOS 3.3's catalog: {name: (type, first track/sector list)}"""
    v = sector(disk, 17, 0)
    if not (v[3] == 3 and v[0x27] == 122):
        sys.exit("not a DOS 3.3 disk in DOS order")
    out = {}
    t, s = v[1], v[2]
    seen = set()
    while t and (t, s) not in seen:
        seen.add((t, s))
        c = sector(disk, t, s)
        for i in range(7):
            e = c[0x0B + i * 35:0x0B + (i + 1) * 35]
            if e[0] in (0, 0xFF):
                continue
            name = bytes(x & 0x7F for x in e[3:33]).decode("ascii").rstrip()
            out[name] = (e[2] & 0x7F, e[0], e[1])
        t, s = c[1], c[2]
    return out


def binary_file(disk, t, s):
    """A B file as DOS stores it: its address, its length, its bytes (the rest of the last sector dropped)."""
    raw = b""
    while t:
        ts = sector(disk, t, s)
        for k in range(122):
            dt, ds = ts[0x0C + 2 * k], ts[0x0D + 2 * k]
            if dt or ds:
                raw += sector(disk, dt, ds)
        t, s = ts[1], ts[2]
    n = raw[2] | raw[3] << 8
    return raw[:4 + n]


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    disk = open(sys.argv[1], "rb").read()
    if len(disk) != 143360 or hashlib.sha256(disk).hexdigest() != DISK_SHA256:
        sys.exit("%s is not the Mockingboard Developers Toolkit disk this voice knows" % sys.argv[1])
    cat = catalog(disk)
    out = b""
    for name, addr in zip(FILES, ADDRS):
        if name not in cat or cat[name][0] != 4:
            sys.exit("%s: no binary file %r" % (sys.argv[1], name))
        f = binary_file(disk, cat[name][1], cat[name][2])
        if (f[0] | f[1] << 8) != addr:
            sys.exit("%r loads at %04X, not %04X" % (name, f[0] | f[1] << 8, addr))
        out += f
    if hashlib.sha256(out).hexdigest() != OUT_SHA256:
        sys.exit("the six files are not the known set")
    path = os.path.join(sys.argv[2], OUT_NAME)
    with open(path, "wb") as fh:
        fh.write(out)
    print("%s: %d bytes, sha256 %s" % (OUT_NAME, len(out), hashlib.sha256(out).hexdigest()))


if __name__ == "__main__":
    main()
