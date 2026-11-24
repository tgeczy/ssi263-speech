"""The SAPI stage's firmware list (ssi263_serve.exe --files, which sapi/build.ps1 copies into {app}\\firmware) never names
a file no package may carry (src/csrc/voices.h SSV_USER_DATA: the Super Voice's SUPERTOK.BIN), even when the firmware
folder holds it -- and still names the shipped voices' files.

    python sapi/test_stage_files.py          (after src/csrc/build_ssi263speech.py); skips without SUPERTOK.BIN

Control: SSI263_SERVE_BREAK=userdata lists the Super Voice's file as the others': "never staged" fails.  (build.ps1's own
guard then stops the build on the file, by name and sha256.)
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SERVE = os.path.join(REPO, "build", "win", "x64", "ssi263_serve.exe")
FW = os.path.join(REPO, "firmware")


def main():
    if not os.path.isfile(os.path.join(FW, "speech-systems-super-voice", "SUPERTOK.BIN")):
        print("skip  no SUPERTOK.BIN in firmware/speech-systems-super-voice")
        return 0
    r = subprocess.run([SERVE, "--files", "--firmware", FW], capture_output=True, text=True)
    files = [ln.strip() for ln in r.stdout.splitlines() if ln.strip()]
    bad = 0
    staged = [f for f in files if f.startswith("speech-systems-super-voice/")]
    ok = r.returncode == 0 and not staged
    print("%-5s never staged: the Super Voice's file   %s" % ("ok" if ok else "FAIL", ", ".join(staged) or "not listed"))
    bad += not ok
    shipped = "blazie/BL2ENG.BNS" in files
    print("%-5s the shipped voices' files listed      %d files%s" % ("ok" if shipped else "FAIL", len(files),
                                                                       "" if shipped else ", no BL2ENG.BNS"))
    bad += not shipped
    print("stage files: %s" % ("all passed" if not bad else "%d FAILED" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
