"""No machine paths in release binaries; a clean MAME source build reproduces the native goldens.

    python tools/check_binary_paths.py

1. Built release DLLs, executables and shared libraries are searched for embedded machine paths.
2. bl.dll (x64) is rebuilt from a clean temporary copy of the current MAME board/core sources, using the
   release builder's object list. It must reproduce both protected native-host goldens bit for bit.
   This checks source/build reproducibility, not binary identity or a legacy source-distribution requirement.
Exit 1 on either failure.
"""
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path[:0] = [REPO, os.path.join(REPO, "src", "csrc", "blazie")]
from tools import repo_paths            # noqa: E402
import build_board as B                 # noqa: E402

MACHINE = re.compile(rb"(?i)[a-z]:[\\/](?:[ -~]{0,80})|\\users\\|/home/|/Users/|\\git\\")
DIST = os.path.join(REPO, "nvda", "dist")


def machine_paths(filename):
    data = Path(filename).read_bytes()
    hits = sorted(set(m.group().decode("latin-1")[:90] for m in MACHINE.finditer(data)))
    # System DLL names and format strings are not paths into this machine.
    return [h for h in hits if re.search(r"(?i)git|users|home|z180", h)]


def main():
    bad = 0
    files = [f for pat in ("blazie-lib/**/*.dll", "blazie-lib/*.exe", "*-build/**/*.dll", "*-build/**/*.exe",
                           "linux/**/*.so", "blazie-emu/blazie_*.exe", "sapi/x*/*.dll", "sapi/*.exe")
             for f in glob.glob(os.path.join(DIST, pat), recursive=True)]
    for f in sorted(set(files)):
        hits = machine_paths(f)
        if hits:
            bad += 1
            print("MACHINE PATH in %s: %s" % (os.path.relpath(f, REPO), hits[:3]))
    print("%d native binaries searched, %d with a machine path" % (len(set(files)), bad))
    if not files:
        print("FAIL: no native binaries found; build the release first")
        bad += 1

    old_here, old_out = B.HERE, B.OUT
    try:
        with tempfile.TemporaryDirectory(prefix="ssi263-mame-rebuild-") as folder:
            d = Path(folder).resolve()
            assert d.is_relative_to(Path(tempfile.gettempdir()).resolve())
            source = Path(REPO) / "src" / "csrc"
            for name in ("blazie", "cpu"):
                shutil.copytree(source / name, d / name,
                                ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
            for header in source.glob("*.h"):
                shutil.copy2(header, d / header.name)
            B.HERE, B.OUT = str(d / "blazie"), str(d / "out")
            target = d / "out" / "x64"
            target.mkdir(parents=True)
            chip = target / "ssi263.dll"
            shutil.copy2(Path(REPO) / "src/ssi263/_bin/x64/ssi263.dll", chip)
            bindir = repo_paths.bin_dir("W64DEVKIT", path_fallback=True)
            env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
            B.build_mame_dll("x64", bindir, env, [], str(chip))
            rebuilt = target / "bl.dll"
            hits = machine_paths(rebuilt)
            if hits:
                print("MACHINE PATH in clean MAME rebuild: %s" % hits[:3])
                bad += 1
            tools = os.path.join(REPO, "nvda", "tools")
            for lang in ("en", "es"):
                g = subprocess.run([sys.executable, "bns_equiv.py", str(rebuilt), "--native",
                                    "--against=" + os.path.join(tools, "golden", "blazie_%s.txt" % lang)]
                                   + (["--es"] if lang == "es" else []), cwd=tools, capture_output=True, text=True,
                                   timeout=120, env=dict(os.environ, PYTHON_COLORS="0"))
                print("clean MAME rebuild, %s native golden: %s" % (lang, "PASS" if g.returncode == 0 else "FAIL"))
                if g.returncode:
                    print((g.stdout + g.stderr)[-2000:])
                    bad += 1
    finally:
        B.HERE, B.OUT = old_here, old_out
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
