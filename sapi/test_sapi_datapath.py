"""The SAPI engine and a voice no package carries (src/csrc/voices.h SSV_USER_DATA: the Super Voice): its token carries
the data folder its program was downloaded or imported into (DataPath, written by register.ps1), and the engine speaks
it from there -- the same samples the voice table gives -- and refuses the token (SetObjectToken,
HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) when the program is no longer there, so the screen reader takes another
voice instead of hearing a Braille Lite under the Super Voice's name.  Through sapi_harness.exe (sapi\\build.ps1 -Dev),
x64 and x86, a data folder under a name outside the system's code page.

    python sapi/test_sapi_datapath.py [--control]     skips without SUPERTOK.BIN

--control: SSI263_SAPI_TEST_BREAK=datapath, the token taken without its program: "refused" fails on both.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
STAGE = os.path.join(REPO, "nvda", "dist", "sapi-dev")
BIN = os.path.join(REPO, "firmware", "speech-systems-super-voice", "SUPERTOK.BIN")
sys.path.insert(0, os.path.join(REPO, "nvda", "shared"))
import ssi263speech  # noqa: E402

TEXT = "This is the Super Voice, speaking through SAPI."


def run(arch, data, brk, tmp, tag):
    job = os.path.join(tmp, "job_%s_%s.txt" % (arch, tag))
    with open(job, "w", encoding="utf-8", newline="\n") as f:
        f.write("supervoice:supertok\t0\t0\t-,-,-,-,22050,-\t%s\t0\t%s\n" % (brk or "-", TEXT))
    out = os.path.join(tmp, "out_%s_%s" % (arch, tag))
    os.makedirs(out)
    key = "Software\\SSI-263 SAPI test datapath %d" % os.getpid()
    r = subprocess.run([os.path.join(STAGE, arch, "sapi_harness.exe"), os.path.join(STAGE, arch, "ssi263_sapi.dll"),
                        job, out], capture_output=True, text=True,
                       env=dict(os.environ, SSI263_SAPI_SETTINGS_KEY=key, SSI263_HARNESS_DATAPATH=data))
    subprocess.run(["reg", "delete", "HKCU\\" + key, "/f"], capture_output=True)
    line = next((ln for ln in r.stdout.splitlines() if ln.startswith("case 0 ")), "")
    fields = dict(p.split("=", 1) for p in line.split()[2:] if "=" in p)
    pcm = os.path.join(out, "case_0.pcm")
    return fields.get("hr", "?"), (open(pcm, "rb").read() if os.path.isfile(pcm) else b"")


def main():
    control = "--control" in sys.argv[1:]
    if not os.path.isfile(BIN):
        print("skip  no SUPERTOK.BIN in firmware/speech-systems-super-voice")
        return 0
    if not os.path.isfile(os.path.join(STAGE, "x64", "sapi_harness.exe")):
        print("no %s: run sapi\\build.ps1 -Dev first" % STAGE)
        return 1
    bad = 0

    def ok(cond, name, detail):
        nonlocal bad
        print("%-5s %-34s %s" % ("ok" if cond else "FAIL", name, detail))
        bad += not cond

    tmp = tempfile.mkdtemp(prefix="ssi263-datapath-")
    try:
        root = os.path.join(tmp, "Daten éő中")          # outside the code page (P27's lesson)
        os.makedirs(os.path.join(root, "speech-systems-super-voice"))
        shutil.copy2(BIN, os.path.join(root, "speech-systems-super-voice"))
        empty = os.path.join(tmp, "empty")
        os.makedirs(empty)
        for arch in ("x64", "x86"):
            ref = ssi263speech.SuperVoiceC(os.path.join(STAGE, arch, "ssi263speech.dll"), BIN, 22050) \
                if arch == ("x64" if sys.maxsize > 2 ** 32 else "x86") else None
            want = b""
            if ref is not None:
                ref.set(50, 50, 100, False)
                ref.say(TEXT)
                while True:
                    data, done = ref.render()
                    want += data
                    if done:
                        break
                ref.close()
            hr, pcm = run(arch, root, None, tmp, "here")
            same = "" if not want else (", the voice table's samples" if pcm[:len(want)] == want
                                        else ", NOT the voice table's samples")
            ok(hr == "00000000" and len(pcm) > 22050 and (not want or pcm[:len(want)] == want),
               "%s: speaks from its DataPath" % arch, "hr=%s, %.2f s%s" % (hr, len(pcm) / 44100.0, same))
            hr, pcm = run(arch, empty, "datapath" if control else None, tmp, "gone")
            ok(hr == "80070002" and not pcm, "%s: refused without its program" % arch,
               "hr=%s, %d bytes spoken" % (hr, len(pcm)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("sapi datapath: %s" % ("all passed" if not bad else "%d FAILED" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
