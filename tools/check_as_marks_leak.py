"""The Accent-PC voice gives back its index marks' memory when it is destroyed (issue #79).

asv_speak_marked keeps the caller's marks (mk) and, while it renders, the marks waiting and reached (wait, reached);
asv_destroy freed the host, the ROM copies and the audio, not those three.  The Android app (with "Tell Android which
word is being spoken") speaks each utterance on a unit of its own and destroys it after: a few dozen bytes per word,
never given back while the speech service lives.

A small C program, built here with LeakSanitizer against the voices library (-fsanitize=leak intercepts every
allocation in the process, the library's too): three units, each says a marked sentence to its end and is destroyed.
LeakSanitizer must find nothing that asv_speak_marked or the rendering allocated.  Linux only (w64devkit has no
sanitizers).

    python3 tools/check_as_marks_leak.py <libssi263_voices.so>        exit 1 on a leak
    AS_LEAK_BREAK=1   as_voice.c: the three arrays left unfreed, as they were -- must fail
"""
import os
import platform
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROMS = os.environ.get("SSI263_ACCENT_PC_DIR") or os.path.join(ROOT, "firmware", "aicom-accent-sa")

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "accentpc/as_voice.h"

static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *p;
    long k;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); k = ftell(f); fseek(f, 0, SEEK_SET);
    p = (unsigned char *)malloc((size_t)k);
    *n = fread(p, 1, (size_t)k, f);
    fclose(f);
    return p;
}

int main(int argc, char **argv)
{
    static const char *text = "One two three four five six seven.";
    char path[1024], err[256];
    unsigned char *rom[3];
    size_t n[3];
    const char *names[3] = {"u2.BIN", "u3.BIN", "u4.BIN"};
    ssv_mark marks[4] = {{0, 1}, {8, 2}, {18, 3}, {29, 4}};
    int k, i;
    for (i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", argv[1], names[i]);
        if (!(rom[i] = slurp(path, &n[i]))) { printf("FAIL no %s\n", path); return 2; }
    }
    for (k = 0; k < 3; k++) {
        const short *pcm;
        int done = 0, blocks = 0, reached = 0;
        as_voice *v = asv_create(rom[0], n[0], rom[1], n[1], rom[2], n[2], 22050.0, err, (int)sizeof err);
        if (!v) { printf("FAIL voice: %s\n", err); return 2; }
        asv_speak_marked(v, text, 0, marks, 4);
        while (!done && blocks++ < 3000) {
            const ssv_reach *r;
            asv_render(v, &pcm, &done);
            reached += asv_reached(v, &r);
        }
        printf("unit %d: %d marks reached\n", k + 1, reached);
        fflush(stdout);
        asv_destroy(v);
    }
    for (i = 0; i < 3; i++) free(rom[i]);
    return 0;
}
'''


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    lib = os.path.abspath(sys.argv[1])
    if not all(os.path.isfile(os.path.join(ROMS, n)) for n in ("u2.BIN", "u3.BIN", "u4.BIN")):
        print("skip  the Accent-PC's ROMs are not in %s" % ROMS)
        return 0
    with tempfile.TemporaryDirectory(prefix="as_leak_") as tmp:
        src, exe = os.path.join(tmp, "as_leak.c"), os.path.join(tmp, "as_leak")
        open(src, "w").write(DRIVER)
        cc = subprocess.run(["gcc", "-g", "-O1", "-fsanitize=leak", "-fno-omit-frame-pointer",
                             "-I" + os.path.join(ROOT, "src", "csrc"), src, lib,
                             "-Wl,-rpath," + os.path.dirname(lib), "-o", exe],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
        if cc.returncode:
            print("FAIL  building the leak check:\n" + cc.stdout[-2000:])
            return 1
        env = dict(os.environ, LSAN_OPTIONS="exitcode=23:print_suppressions=0")
        if os.environ.get("AS_LEAK_BREAK"):
            env["AS_LEAK_BREAK"] = "1"
        # with address-space randomisation off for this one program: GCC 11's sanitizer runtime dies at start-up now
        # and then (SIGSEGV, no output, about one run in three on Coconut) under newer kernels' wider randomisation.
        # Where that is not allowed (a container: GitHub's runners say "Operation not permitted") the program runs as
        # it is, and a start-up death -- a signal before it printed a line -- is tried again, three times at most
        def run_once(argv):
            return subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True,
                                  env=env)
        r = None
        if shutil.which("setarch"):
            r = run_once(["setarch", platform.machine(), "-R", exe, ROMS])
            if "failed to set personality" in r.stdout:
                r = None
        tries = 0
        while r is None or (r.returncode < 0 and "unit " not in r.stdout and tries < 3):
            r = run_once([exe, ROMS])
            tries += 1
    out = r.stdout
    print("\n".join(ln for ln in out.splitlines() if ln.startswith("unit ")))
    ours = [ln.strip() for ln in out.splitlines() if "#" in ln and ("take_marks" in ln or "report_marks" in ln or "room" in ln)]
    leaked = r.returncode == 23 or "LeakSanitizer" in out
    if leaked:
        summary = [ln for ln in out.splitlines() if ln.startswith("SUMMARY")]
        print("FAIL  leaks after three units destroyed: %s" % (summary[0] if summary else "exit %d" % r.returncode))
        for ln in ours[:4]:
            print("        " + ln)
    elif r.returncode:
        print("FAIL  the check program exited %d:\n%s" % (r.returncode, out[-1500:]))
        return 1
    else:
        print("ok    three units with marks, destroyed: nothing leaked")
    print("as marks leak: %s" % ("FAILED" if leaked else "all ok"))
    return 1 if leaked else 0


if __name__ == "__main__":
    sys.exit(main())
