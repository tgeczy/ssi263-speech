"""The speech-dispatcher module's import of the Super Voice's program (sd_ssi263 --import): from SUPERTOK.BIN and from
a disk image laid out around it (tools/check_sv_voice.py make_disk) into this user's own data folder
($XDG_DATA_HOME/ssi263-speech), a changed program refused; then the module, its data folder empty, offers the voice
from there (INIT, LIST VOICES).  A stand-in home: nothing of the real user's is touched.

    python3 src/platforms/speechd/test_sd_import.py build/linux/sd_ssi263      (needs the local SUPERTOK.BIN)

Control: SD_SSI263_TEST_NO_USER_DIR=1, the user's folder never looked in: "offered" fails.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(REPO, "tools"))
from check_sv_voice import make_disk  # noqa: E402

BIN = os.path.join(REPO, "firmware", "speech-systems-super-voice", "SUPERTOK.BIN")


def main():
    module = os.path.abspath(sys.argv[1])
    if not os.path.isfile(BIN):
        print("skip  no SUPERTOK.BIN in firmware/speech-systems-super-voice")
        return 0
    prog = open(BIN, "rb").read()
    bad = 0

    def ok(cond, name, detail=""):
        nonlocal bad
        print("%-5s %-9s %s" % ("ok" if cond else "FAIL", name, detail))
        bad += not cond

    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, XDG_DATA_HOME=os.path.join(tmp, "data"), HOME=tmp,
                   SSI263_DATADIR=os.path.join(tmp, "empty"))
        os.makedirs(env["SSI263_DATADIR"])
        dest = os.path.join(tmp, "data", "ssi263-speech", "speech-systems-super-voice", "SUPERTOK.BIN")
        disk = os.path.join(tmp, "SUPERV.DSK")
        with open(disk, "wb") as f:
            f.write(make_disk(prog, [33, 34, 5, 60, 41]))
        changed = os.path.join(tmp, "changed.bin")
        with open(changed, "wb") as f:
            f.write(prog[:100] + bytes([prog[100] ^ 1]) + prog[101:])
        r = subprocess.run([module, "--import", changed], env=env, capture_output=True, text=True)
        ok(r.returncode != 0 and not os.path.exists(dest), "refused", r.stderr.strip()[:100])
        r = subprocess.run([module, "--import", disk], env=env, capture_output=True, text=True)
        got = open(dest, "rb").read() if os.path.isfile(dest) else b""
        ok(r.returncode == 0 and got == prog, "imported", (r.stdout or r.stderr).strip()[:100])
        r = subprocess.run([module], input="INIT\nLIST VOICES\nQUIT\n", env=env, capture_output=True, text=True,
                           timeout=60)
        ok("Speech Systems Super Voice" in r.stdout, "offered",
           "listed" if "Speech Systems Super Voice" in r.stdout else (r.stdout.strip().splitlines() or ["-"])[0])
    print("sd import: %s" % ("all passed" if not bad else "%d FAILED" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
