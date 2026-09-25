"""Shared by build_speakout.py and build_blazie.py.

Since 0.2 the add-ons ship no numpy and no VC runtime: every native file is a static
w64devkit build that imports only KERNEL32 and msvcrt, in both 32- and 64-bit, so one
add-on loads in NVDA 2021.1 on Windows 7 (32-bit Python 3.7) up to NVDA 2026 (64-bit
Python 3.13).  check_native() refuses to package anything else.
"""
import os
import shutil
import stat
import subprocess
import sys

OBJDUMP = r"C:\w64devkit\bin\objdump.exe"
ALLOWED_IMPORTS = {"KERNEL32.dll", "msvcrt.dll"}
FORMATS = {"x64": "pei-x86-64", "x86": "pei-i386"}
# research-only modules left out of the add-ons (drivers.py needs csv, absent from NVDA 2021-2023)
IGN = shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo", "drivers.py")

NVDA_RANGE = "minimumNVDAVersion = 2021.1\nlastTestedNVDAVersion = 2026.1\n"


def rm(path):
    """rmtree that also clears read-only flags (the firmware copies keep the floppy's)."""
    def onexc(func, p, exc):
        os.chmod(p, stat.S_IWRITE)
        func(p)
    shutil.rmtree(path, onexc=onexc)


def check_native(path, arch):
    dump = subprocess.run([OBJDUMP, "-p", path], capture_output=True, text=True, check=True).stdout
    if FORMATS[arch] not in dump:
        sys.exit("%s is not %s" % (path, FORMATS[arch]))
    dlls = {ln.split(":", 1)[1].strip() for ln in dump.splitlines() if "DLL Name:" in ln}
    if not dlls <= ALLOWED_IMPORTS:
        sys.exit("%s imports %s (VC runtime / UCRT not allowed)" % (path, sorted(dlls - ALLOWED_IMPORTS)))


# The C chip must match chip.py before anything ships: tools/check_native_core.py on
# each architecture's Python (A/R timing and chip time exact, PCM identical).
CHECK_PYTHONS = (r"C:\Python313\python.exe", r"C:\Python313-32\python.exe")
_checked = []


def check_core(engine):
    if _checked:
        return
    for py in CHECK_PYTHONS:
        if not os.path.isfile(py):
            sys.exit("missing %s for the native-core check" % py)
        r = subprocess.run([py, os.path.join(os.path.dirname(engine), "tools", "check_native_core.py")],
                           capture_output=True, text=True)
        print(r.stdout.strip())
        if r.returncode:
            sys.exit("the C chip no longer matches chip.py (%s): %s" % (py, r.stderr[-2000:]))
    _checked.append(True)


def copy_engine(engine, eng):
    """The chip package with its C core (ssi263.dll) for both architectures."""
    check_core(engine)
    shutil.copytree(os.path.join(engine, "ssi263"), os.path.join(eng, "ssi263"), ignore=IGN)
    # a package of its own (synthDrivers._ssi263_<name>): the driver imports the engine
    # relatively, so two add-ons, or two versions, never share one `ssi263` module
    with open(os.path.join(eng, "__init__.py"), "w", encoding="utf-8") as f:
        f.write('"""This add-on\'s own copy of the SSI-263 engine; its driver imports it relatively."""\n')
    shutil.copytree(os.path.join(engine, "data"), os.path.join(eng, "data"), ignore=IGN)
    for arch in ("x64", "x86"):
        check_native(os.path.join(eng, "ssi263", "_bin", arch, "ssi263.dll"), arch)


def zip_build(build, out):
    import zipfile
    if os.path.exists(out):
        os.remove(out)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for root, _, files in os.walk(build):
            for fn in files:
                p = os.path.join(root, fn)
                z.write(p, os.path.relpath(p, build))
    print("wrote %s (%.1f MB)" % (out, os.path.getsize(out) / 1e6))
