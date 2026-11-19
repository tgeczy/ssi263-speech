"""Shared by all three NVDA add-on builders.

Since 0.2 the add-ons ship no numpy and no VC runtime: every native file is a static
w64devkit build that imports only KERNEL32 and msvcrt, in both 32- and 64-bit, so one
add-on loads in NVDA 2021.1 on Windows 7 (32-bit Python 3.7) up to NVDA 2026 (64-bit
Python 3.13).  check_native() refuses to package anything else.
"""
import os
import re
import shutil
import stat
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO not in sys.path:
    sys.path.insert(0, REPO)
from tools import repo_paths  # noqa: E402  (paths outside the repo come from paths.local)

ALLOWED_IMPORTS = {"KERNEL32.dll", "msvcrt.dll"}
FORMATS = {"x64": "pei-x86-64", "x86": "pei-i386"}
# research-only modules left out of the add-ons (drivers.py needs csv, absent from NVDA 2021-2023)
IGN = shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo", "drivers.py")



# The Braille Lite driver's optional units beside the English one: (the name in the add-on's engine folder, its
# file under firmware/blazie/).  Each voice is offered when both its files are there (blazie.py VOICES); the
# release requires them all (build_ssi263.py).  Firmware: release assets only, never in git.
BLAZIE_UNITS = [
    ("BL2SPA.BNS", "spanish/BL2SPA.BNS"), ("bl2spa_fresh.state", "spanish/bl2spa_fresh.state"),
    ("Lite2000_de.BNS", "german/Lite2000.BNS"), ("lite2000_de_fresh.state", "german/lite2000_de_fresh.state"),
    ("Lite2000_it.BNS", "italian/Lite2000.BNS"), ("lite2000_it_fresh.state", "italian/lite2000_it_fresh.state"),
    ("BS2SLL.BNS", "bns2000/BS2SLL.BNS"), ("bs2sll_fresh.state", "bns2000/bs2sll_fresh.state"),
]


def _release_units():
    """0.8: the release table's voices (src/csrc/blazie/bl_languages.txt rows with a voice key, Blazie's updates in 17
    more languages, an anonymous donation): each its image and its state under bl2000/, in the engine folder as in
    firmware/blazie/ (tools/make_release_states.py makes them there)."""
    sys.path.insert(0, os.path.join(REPO, "src", "csrc", "blazie"))
    import gen_languages
    table, problems = gen_languages.rows()
    if problems:
        sys.exit("bl_languages.txt: " + "; ".join(problems))
    out = []
    for r in table:
        if r["voice"].startswith("blazie_"):
            for name in (r["file"], r["id"] + ".state"):
                out.append(("bl2000/" + name, "bl2000/" + name))
    return out


RELEASE_UNITS = _release_units()
BLAZIE_UNITS += RELEASE_UNITS


def read_manifest(addon_dir):
    """An add-on's manifest.ini, kept in its own folder (nvda/<name>/manifest.ini) so add-on crawlers
    find it in the repo; the build copies it into the package as is.  Returns (text, version)."""
    with open(os.path.join(addon_dir, "manifest.ini"), encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"^version\s*=\s*(\S+)\s*$", text, re.M)
    if not m:
        raise SystemExit("%s: no version line in manifest.ini" % addon_dir)
    return text, m.group(1)


def rm(path):
    """rmtree that also clears read-only flags (the firmware copies keep the floppy's)."""
    def onexc(func, p, exc):
        os.chmod(p, stat.S_IWRITE)
        func(p)
    shutil.rmtree(path, onexc=onexc)


def check_native(path, arch, imports=()):
    objdump = repo_paths.program("W64DEVKIT", "objdump")
    dump = subprocess.run([objdump, "-p", path], capture_output=True, text=True, check=True).stdout
    if FORMATS[arch] not in dump:
        sys.exit("%s is not %s" % (path, FORMATS[arch]))
    dlls = {ln.split(":", 1)[1].strip() for ln in dump.splitlines() if "DLL Name:" in ln}
    allowed = ALLOWED_IMPORTS | set(imports)
    if not dlls <= allowed:
        sys.exit("%s imports %s (VC runtime / UCRT not allowed)" % (path, sorted(dlls - allowed)))


def copy_mame_notices(eng, *cores):
    """Own code is MIT; each extracted MAME component retains its BSD notice and provenance."""
    dest = os.path.join(eng, "licenses")
    os.makedirs(dest, exist_ok=True)
    shutil.copy2(os.path.join(REPO, "LICENSE"), os.path.join(dest, "LICENSE-MIT.txt"))
    shutil.copy2(os.path.join(REPO, "third_party", "casso", "LICENSE"), os.path.join(dest, "LICENSE-Casso-MIT.txt"))
    for core in cores:
        source = os.path.join(REPO, "src", "csrc", "cpu", "mame_" + core)
        for src, name in (("LICENSE-BSD-3-Clause.txt", "LICENSE-" + core + "-BSD-3-Clause.txt"),
                          ("PINNED.txt", "MAME-" + core + "-provenance.txt")):
            shutil.copy2(os.path.join(source, src), os.path.join(dest, name))


_built_boards = set()


def build_native_voices():
    """Build the complete shared Windows voice library once per release invocation."""
    if "native-voices" not in _built_boards:
        subprocess.run([sys.executable, os.path.join(REPO, "src", "csrc", "build_ssi263speech.py")], check=True)
        _built_boards.add("native-voices")


def copy_native_voices(eng):
    """The unified library and all linked CPU notices, with both architectures checked."""
    build_native_voices()
    check_core(os.path.join(REPO, "src"), unified=True)
    for arch in ("x64", "x86"):
        dest = os.path.join(eng, "bin", arch)
        os.makedirs(dest, exist_ok=True)
        dll = os.path.join(REPO, "build", "win", arch, "ssi263speech.dll")
        check_native(dll, arch)
        shutil.copy2(dll, dest)
    copy_mame_notices(eng, "z180", "nec", "i86", "i8085")


def copy_native_binding(eng):
    """The Speak-Out/Accent ctypes layer; no research engine or Python firmware host."""
    copy_native_voices(eng)
    with open(os.path.join(eng, "__init__.py"), "w", encoding="utf-8") as f:
        f.write('"""Private native voice bindings for this add-on."""\n')
    for name in ("ssi263speech.py", "ssi263_rates.py", "ssi263_flow.py"):
        shutil.copy2(os.path.join(REPO, "nvda", "shared", name), eng)


def build_board(script):
    """Separate processes avoid the three board builders' identical Python module names."""
    if script not in _built_boards:
        subprocess.run([sys.executable, os.path.join(REPO, "src", "csrc", script, "build_board.py")], check=True)
        _built_boards.add(script)


# The C chip must match chip.py before anything ships: tools/check_native_core.py on
# each architecture's Python (A/R timing and chip time exact, PCM identical).
CHECK_PYTHONS = ("PYTHON64", "PYTHON32")          # keys in paths.local; both are required
_checked = set()


def check_core(engine, unified=False):
    if unified in _checked:
        return
    # nothing ships from a tree that names someone's own folders
    r = subprocess.run([sys.executable, os.path.join(REPO, "tools", "check_no_machine_paths.py")],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("machine paths in tracked files:\n" + r.stdout[-2000:])
    for key, arch in zip(CHECK_PYTHONS, ("x64", "x86")):
        py = repo_paths.python(key)
        dll = (os.path.join(REPO, "build", "win", arch, "ssi263speech.dll") if unified else
               os.path.join(engine, "ssi263", "_bin", arch, "ssi263.dll"))
        r = subprocess.run([py, os.path.join(os.path.dirname(engine), "tools", "check_native_core.py")],
                           capture_output=True, text=True, env=dict(os.environ, SSI263_LIB=dll))
        print(r.stdout.strip())
        if r.returncode:
            sys.exit("the C chip no longer matches chip.py (%s): %s" % (py, r.stderr[-2000:]))
    _checked.add(unified)


def copy_engine(engine, eng):
    """The chip package with its C core (ssi263.dll) for both architectures."""
    check_core(engine)
    def windows_engine_only(path, names):
        ignored = set(IGN(path, names))
        if os.path.basename(path) == "_bin":
            ignored.update(n for n in names if n not in ("x86", "x64"))
        return ignored
    shutil.copytree(os.path.join(engine, "ssi263"), os.path.join(eng, "ssi263"), ignore=windows_engine_only)
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
        for root, dirs, files in os.walk(build):
            dirs[:] = [d for d in dirs if d != "__pycache__"]
            for fn in files:
                if fn.endswith((".pyc", ".pyo")):
                    continue
                p = os.path.join(root, fn)
                z.write(p, os.path.relpath(p, build))
    print("wrote %s (%.1f MB)" % (out, os.path.getsize(out) / 1e6))


def copy_unicorn_license(eng):
    """Unicorn's GPLv2 text, as COPYING.unicorn, taken from the pinned source tarball itself."""
    import tarfile
    with tarfile.open(os.path.join(REPO, "src", "csrc", "unicorn-2.1.4.tar.gz")) as t:
        text = t.extractfile("unicorn-2.1.4/src/qemu/COPYING").read()
    with open(os.path.join(eng, "COPYING.unicorn"), "wb") as f:
        f.write(text)
