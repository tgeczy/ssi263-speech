"""Make a Mockingboard voice's firmware file from one of Sweet Micro Systems' disks, as src/csrc/mockingboard/mb_dsk.c
does: the DOS 3.3 binary files a version of its text-to-speech is made of, back to back, each exactly as DOS stores it
(load address, length, bytes).  Nothing else of the disk is taken -- not Apple's DOS, nor any other program on it.

    python tools/mockingboard_firmware.py <a disk's .dsk (DOS order)> <output folder>

  the Mockingboard Developers Toolkit (1984; also "Mockingboard - Developer's Toolkit" and "MNBTOOLKIT for IIc"):
    text-to-speech 1.1 (11 March 1985), six files -> mockingboard-tts-1.1.bin
  Mockingboard disk 1: the earlier text-to-speech, four files -> mockingboard-tts-early.bin

The output is checked against its known sha256, which src/csrc/mockingboard/mb_host.h also requires (MB_SHA256,
MB_SHA256_EARLY).
"""
import hashlib
import os
import sys

# (output, its sha256, the files in order, their load addresses) -- mb_host.h
SETS = [
    ("mockingboard-tts-1.1.bin", "88e1e90f1e76b7afa2f370db3c3bf34892c9621b5360304359242570b41bdfae",
     ["TEXT TO SPEECH", "INFLECTION", "IIE TTS DRIVER", "MKB:RULE.INDEX", "MKB:RULE.LENGTH", "MKB:RULE.TABLE"],
     [0x8C00, 0x9000, 0x9300, 0xD000, 0xD100, 0xD200]),
    ("mockingboard-tts-early.bin", "c7c049b1b61792719e21e461a2a8c25fc32c12882c81305814a3dc67af6e5835",
     ["TEXT TO SPEECH", "MKB:RULE.INDEX", "MKB:RULE.LENGTH", "MKB:RULE.TABLE"],
     [0x6600, 0x6E00, 0x6F00, 0x7000]),
]


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


def from_disk(disk):
    """(output name, its bytes) for the first known set on the disk; None when there is none"""
    cat = catalog(disk)
    for out_name, out_sha, files, addrs in SETS:
        out = b""
        for name, addr in zip(files, addrs):
            if name not in cat or cat[name][0] != 4:
                break
            f = binary_file(disk, cat[name][1], cat[name][2])
            if (f[0] | f[1] << 8) != addr:
                break
            out += f
        else:
            if hashlib.sha256(out).hexdigest() == out_sha:
                return out_name, out
    return None


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    disk = open(sys.argv[1], "rb").read()
    if len(disk) != 143360:
        sys.exit("%s is not a 140 KB Apple II disk image" % sys.argv[1])
    found = from_disk(disk)
    if not found:
        sys.exit("%s holds none of the text-to-speech versions these voices run" % sys.argv[1])
    name, out = found
    with open(os.path.join(sys.argv[2], name), "wb") as fh:
        fh.write(out)
    print("%s: %d bytes, sha256 %s" % (name, len(out), hashlib.sha256(out).hexdigest()))


if __name__ == "__main__":
    main()
