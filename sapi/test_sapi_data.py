"""The SAPI settings program's data folders (sapi/dataroot.ps1, Panthera SAPI's system): which data root it takes, what
"Move voices for all users" would do, and the Super Voice's program read from a file or a download -- through
settings.ps1's own switches (-ShowDataPlan, -ExtractFrom, -DownloadTo), on a stage made here from the scripts and the
-Dev stage's library, with the registry and every folder isolated (APPDATA, ProgramData, SSI263_SAPI_DATA_KEY and
SSI263_SAPI_DATA_MACHINE_KEY: HKCU test keys).  No network: the download's sources are file: URLs.

    python sapi/test_sapi_data.py          (after sapi\\build.ps1 -Dev); the extraction cases skip without SUPERTOK.BIN

Cases:
  order       NVDA's folder holding the program beats %ProgramData%'s; the chosen folder beats both
  twopass     NVDA's folder holding only its settings loses to the per-user folder that holds the program
  fresh       nothing anywhere: this person's own %APPDATA%\\ssi263-speech\\firmware, never %ProgramData%
  plans       per-user -> migrate (to %ProgramData%); NVDA's -> nvda; %ProgramData%'s -> done; a chosen one -> chosen;
              an empty one -> none
  extract     the program from itself, from a disk image, and from a zip holding the disk; a changed one refused
  zipbad      a zip whose first member cannot be inflated, the disk after it: the disk taken
  register    the Register button hands register.ps1 this person's data root while it holds the program (elevated,
              register.ps1 may be another account, whose folders are not this person's), the bare -Register otherwise
  download    the first source not its pinned file, the second taken
Controls (SSI263_SAPI_DATA_BREAK, each must fail exactly these): onepass -> twopass; movenvda -> plans; nozip -> extract,
zipbad; zipall -> zipbad.
SSI263_SAPI_SETTINGS_BREAK=register-root (settings.ps1: the bare -Register it was) -> register.
"""
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DEV = os.path.join(REPO, "nvda", "dist", "sapi-dev")
BIN = os.path.join(REPO, "firmware", "speech-systems-super-voice", "SUPERTOK.BIN")
sys.path.insert(0, os.path.join(REPO, "tools"))
from check_sv_voice import make_disk  # noqa: E402

REL = os.path.join("speech-systems-super-voice", "SUPERTOK.BIN")
results = []


def check(name, ok, detail=""):
    results.append(ok)
    print("%-5s %-9s %s" % ("ok" if ok else "FAIL", name, detail))


def main():
    if not os.path.isfile(os.path.join(DEV, "voices.txt")):
        print("no %s: run sapi\\build.ps1 -Dev first" % DEV)
        return 1
    tmp = tempfile.mkdtemp(prefix="ssi263-sapidata-")
    pid = os.getpid()
    ukey, mkey = "Software\\SSI-263 SAPI test data %d" % pid, "Software\\SSI-263 SAPI test machine %d" % pid
    try:
        stage = os.path.join(tmp, "stage")
        os.makedirs(stage)
        for n in ("settings.ps1", "dataroot.ps1", "updates.ps1", "register.ps1"):
            shutil.copy2(os.path.join(HERE, n), stage)
        shutil.copy2(os.path.join(DEV, "voices.txt"), stage)
        for arch in ("x64", "x86"):
            os.makedirs(os.path.join(stage, arch))
            shutil.copy2(os.path.join(DEV, arch, "ssi263speech.dll"), os.path.join(stage, arch))
        appdata, common = os.path.join(tmp, "Roaming"), os.path.join(tmp, "ProgramData")
        nvda = os.path.join(appdata, "nvda", "ssi263-speech", "firmware")
        user = os.path.join(appdata, "ssi263-speech", "firmware")
        shared = os.path.join(common, "ssi263-speech", "firmware")

        def ps(*args):
            env = dict(os.environ, APPDATA=appdata, ProgramData=common, SSI263_SAPI_DATA_KEY=ukey,
                       SSI263_SAPI_DATA_MACHINE_KEY=mkey)
            r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                os.path.join(stage, "settings.ps1")] + list(args), capture_output=True, text=True,
                               env=env, timeout=120)
            return r.returncode, r.stdout

        def plan():
            _c, out = ps("-ShowDataPlan")
            got = dict(ln.split(": ", 1) for ln in out.splitlines() if ": " in ln)
            return got.get("root", ""), got.get("plan", "").split(" | ")[0]

        def put(root):
            os.makedirs(os.path.join(root, "speech-systems-super-voice"), exist_ok=True)
            with open(os.path.join(root, REL), "wb") as f:
                f.write(b"stand-in")

        def clean():
            for d in (appdata, common):
                shutil.rmtree(d, ignore_errors=True)
            for k in (ukey, mkey):
                subprocess.run(["reg", "delete", "HKCU\\" + k, "/f"], capture_output=True)

        clean()
        put(nvda)
        put(shared)
        root, _p = plan()
        nvda_first = root == nvda
        chosen = os.path.join(tmp, "chosen")
        put(chosen)
        subprocess.run(["reg", "add", "HKCU\\" + ukey, "/v", "DataPath", "/d", chosen, "/f"], capture_output=True)
        root2, _p = plan()
        check("order", nvda_first and root2 == chosen, "NVDA's over %%ProgramData%%: %s; the chosen one first: %s"
              % (nvda_first, root2 == chosen))

        clean()
        os.makedirs(nvda)
        with open(os.path.join(os.path.dirname(nvda), "settings.json"), "w") as f:
            f.write("{}")
        put(user)
        root, _p = plan()
        check("twopass", root == user, root)

        clean()
        root, _p = plan()
        check("fresh", root == user, root)

        got = []
        for where, want in ((user, "migrate"), (nvda, "nvda"), (shared, "done"), (chosen, "chosen")):
            clean()
            put(where)
            if where == chosen:
                subprocess.run(["reg", "add", "HKCU\\" + ukey, "/v", "DataPath", "/d", chosen, "/f"], capture_output=True)
            got.append((want, plan()[1]))
        clean()
        os.makedirs(user)
        got.append(("none", plan()[1]))
        check("plans", all(a == b for a, b in got), ", ".join("%s=%s" % (a, b) for a, b in got))

        # the Register button hands register.ps1 (elevated: maybe another account) this person's data root while it
        # holds the program, and the bare -Register when nothing does
        def register_args():
            _c, out = ps("-ShowRegisterArguments")
            return next((ln.split(": ", 1)[1] for ln in out.splitlines() if ln.startswith("register: ")), "?")
        clean()
        put(nvda)
        with_data = register_args()
        clean()
        without = register_args()
        check("register", with_data == '-Register -DataRoot "%s"' % nvda and without == "-Register",
              "the program in NVDA's folder: %s; nowhere: %s" % (with_data, without))

        if not os.path.isfile(BIN):
            print("skip  extract, download: no SUPERTOK.BIN")
        else:
            prog = open(BIN, "rb").read()
            disk = make_disk(prog, [33, 34, 5, 60, 41])
            z = io.BytesIO()
            with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
                zf.writestr("Super Voice (Speech Systems)/SUPERV.DSK", disk)
                zf.writestr("README.TXT", b"the disk")
            files = {"prog.bin": prog, "disk.dsk": disk, "disk.zip": z.getvalue(),
                     "changed.bin": prog[:7] + bytes([prog[7] ^ 1]) + prog[8:]}
            for n, b in files.items():
                with open(os.path.join(tmp, n), "wb") as f:
                    f.write(b)
            outs = []
            for n in ("prog.bin", "disk.dsk", "disk.zip", "changed.bin"):
                out = os.path.join(tmp, "out_" + n)
                code, text = ps("-ExtractFrom", os.path.join(tmp, n), "-ExtractTo", out)
                outs.append((n, code == 0 and open(out, "rb").read() == prog if os.path.isfile(out) else False,
                             text.strip()))
            ok = outs[0][1] and outs[1][1] and outs[2][1] and not outs[3][1] and "sha256" in outs[3][2]
            check("extract", ok, "; ".join("%s %s" % (n, "taken" if t else "refused") for n, t, _x in outs))

            # a zip whose first member cannot be inflated (its stream's first block of a reserved type) and the disk
            # after it: the disk is still taken -- one bad member threw the import away (the 0.9.5 audit)
            zb = io.BytesIO()
            with zipfile.ZipFile(zb, "w", zipfile.ZIP_DEFLATED) as zf:
                zf.writestr("A README.TXT", b"the disk is the next member " * 20)
                zf.writestr("Super Voice (Speech Systems)/SUPERV.DSK", disk)
            bad = bytearray(zb.getvalue())
            bad[30 + len("A README.TXT")] = 0xFF               # BFINAL 1, BTYPE 11: no such block
            with open(os.path.join(tmp, "bad.zip"), "wb") as f:
                f.write(bytes(bad))
            out = os.path.join(tmp, "out_bad.zip")
            code, text = ps("-ExtractFrom", os.path.join(tmp, "bad.zip"), "-ExtractTo", out)
            took = code == 0 and os.path.isfile(out) and open(out, "rb").read() == prog
            check("zipbad", took, "a member that cannot be inflated first: %s" % (
                "taken" if took else "refused: " + text.strip()[:90]))

            sha = lambda b: hashlib.sha256(b).hexdigest()  # noqa: E731
            bad = os.path.join(tmp, "bad.zip")
            with open(bad, "wb") as f:
                f.write(z.getvalue() + b"tail")
            url = lambda p: "file:///" + p.replace("\\", "/")  # noqa: E731
            os.environ["SSI263_SAPI_DATA_TEST_SOURCES"] = "%s|%d|%s|zip;%s|%d|%s|dsk" % (
                url(bad), len(z.getvalue()), sha(z.getvalue()), url(os.path.join(tmp, "disk.dsk")), len(disk),
                sha(disk))
            out = os.path.join(tmp, "out_download")
            code, text = ps("-DownloadTo", out)
            del os.environ["SSI263_SAPI_DATA_TEST_SOURCES"]
            check("download", code == 0 and os.path.isfile(out) and open(out, "rb").read() == prog, text.strip()[:110])
    finally:
        for k in (ukey, mkey):
            subprocess.run(["reg", "delete", "HKCU\\" + k, "/f"], capture_output=True)
        shutil.rmtree(tmp, ignore_errors=True)
    print("sapi data: %s" % ("all passed" if all(results) else "%d FAILED" % results.count(False)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
