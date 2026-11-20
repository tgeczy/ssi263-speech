"""The Braille Lite's text preparation (src/csrc/blazie/bl_voice.c) on long runs of digits, under AddressSanitizer.

The English-style currency pass (amounts(), run for every voice whose text is 7-bit) keeps the ends of a number's
whole part in a 64-entry array.  A run of 64 or more digits filled it, and the "whole part absent" entry after them
went one past its end: one int written past a stack array, then read back.  Its copy in numwords.c had the bound
right; this one did not.  The SAPI engine, the speech-dispatcher module, the phones and the Python wheel all reach
it with any text (a 64-bit binary number is exactly 64 digits).  The NVDA add-on prepares its text in Python.

An overrun of one int is not seen in the output, so this check builds bl_voice.c alone with -fsanitize=address
(the rest of the library as the Linux build left it, in build/linux/obj), says the texts below through
blv_say_bytes, and fails on any report.  Linux only: w64devkit has no AddressSanitizer.

    python3 tools/check_bl_digit_run.py            exit 1 on any failure (run after build_linux.sh)
    BL_DIGIT_RUN_BREAK=1   the bound put back as it was (nw < 64)   -- must fail: an AddressSanitizer report
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src", "csrc")
OBJ = os.path.join(ROOT, "build", "linux", "obj")
FIXED = "    for (g = j; g > i && nw < 63; g--) wend[nw++] = g;"
BROKEN = "    for (g = j; g > i && nw < 64; g--) wend[nw++] = g;"
TEXTS = [
    "1" * 64,                                  # a 64-bit binary number: exactly the array
    "0" * 64 + " and " + "1" * 100,            # at the start, and after a space
    "$" + "9" * 80,                            # after a currency sign
    "9" * 70 + ".50",                          # with a fraction
    "pay 0" + "1" * 63 + " now",
    "1,000," + "1" * 70,                       # comma groups, then a long run
    "x" * 10 + " " + "7" * 4000,               # a very long run
]
DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "bl_voice.h"
static const char *T[] = {%s};
int main(void)
{
    static unsigned char out[65536];
    int k, n;
    for (k = 0; k < (int)(sizeof T / sizeof T[0]); k++) {
        n = blv_say_bytes(T[k], BLV_LATIN1, 0, out, (int)sizeof out);
        printf("said %%d: %%d bytes\n", k, n);
    }
    return 0;
}
'''


def main():
    if os.name != "posix":
        print("skip  Braille Lite digit runs: AddressSanitizer is Linux's here")
        return 0
    if not os.path.isfile(os.path.join(OBJ, "bl_host.o")):
        print("FAIL  Braille Lite digit runs: build/linux/obj is not built (./build_linux.sh)")
        return 1
    source = open(os.path.join(SRC, "blazie", "bl_voice.c"), encoding="utf-8").read()
    if os.environ.get("BL_DIGIT_RUN_BREAK"):
        if source.count(FIXED) != 1:
            print("FAIL  the control could not find the bound to undo (the code moved)")
            return 1
        source = source.replace(FIXED, BROKEN)
    flags = ["-O1", "-g", "-fsanitize=address", "-fno-omit-frame-pointer", "-std=gnu89", "-ffp-contract=off", "-w",
             "-DBL_Z180_MAME", "-I" + os.path.join(SRC, "blazie"), "-I" + os.path.join(SRC, "cpu"), "-I" + SRC]
    tmp = tempfile.mkdtemp(prefix="bl_digit_run_")
    try:
        with open(os.path.join(tmp, "bl_voice.c"), "w", encoding="utf-8") as f:
            f.write(source)
        with open(os.path.join(tmp, "main.c"), "w", encoding="utf-8") as f:
            f.write(DRIVER % ", ".join('"%s"' % t for t in TEXTS))
        objs = []
        for name in ("bl_voice.c", "main.c"):
            o = os.path.join(tmp, name + ".o")
            subprocess.run(["gcc"] + flags + ["-c", "-o", o, os.path.join(tmp, name)], check=True)
            objs.append(o)
        others = sorted(os.path.join(OBJ, f) for f in os.listdir(OBJ) if f.endswith(".o") and f != "bl_voice.o")
        exe = os.path.join(tmp, "said")
        subprocess.run(["g++", "-fsanitize=address", "-o", exe] + objs + others + ["-lm"], check=True)
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:abort_on_error=0:exitcode=23")
        r = subprocess.run([exe], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        out, err = r.stdout.decode(errors="replace"), r.stderr.decode(errors="replace")
        said = out.count("said ")
        report = next((l for l in err.splitlines() if "ERROR: AddressSanitizer" in l), "")
        if r.returncode == 0 and not report and said == len(TEXTS):
            print("ok    Braille Lite digit runs: %d texts (64 to 4,000 digits in a row) prepared, no "
                  "AddressSanitizer report" % said)
            return 0
        print("FAIL  Braille Lite digit runs: %s (after %d of %d texts; exit %d)" % (
            report.split("ERROR: ")[-1].strip() or "no report", said, len(TEXTS), r.returncode))
        frame = next((l.strip() for l in err.splitlines() if " in amounts " in l or "bl_voice.c:" in l), "")
        if frame:
            print("      at %s" % frame)
        return 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
