"""Where things are, for every tool and build script: the repo's own folders, and the
few things that live outside it.

Nothing here names anyone's machine.  Paths inside the repo come from this file's own
location.  Paths outside it (the real-unit recordings, z180emu, the compilers) come from,
in order:

  1. an environment variable, SSI263_<KEY>;
  2. paths.local at the repo root (gitignored; copy paths.local.example and fill it in);
  3. for a program, the PATH, where that makes sense;

and otherwise the script stops and says which key to set.  tools/check_no_machine_paths.py
keeps it that way.

Plain os.path and no newer syntax: nvda/tools/driver_sim.py imports this on the old-NVDA
rig's 32-bit Python 3.7, under python -S.

Use from any script in the repo:

    REPO = <the repo root, from __file__>
    sys.path.insert(0, REPO)
    from tools import repo_paths
"""
import os
import shutil

# ---------------------------------------------------------------- inside the repo

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "src")                         # the engine and the hosts
TOOLS = os.path.join(REPO, "tools")
FIRMWARE = os.path.join(REPO, "firmware")
NVDA = os.path.join(REPO, "nvda")
DIST = os.path.join(NVDA, "dist")                       # built add-ons (gitignored)
INVESTIGATION_OUT = os.path.join(REPO, "investigation", "out")   # renders (gitignored)
LOCAL_FILE = os.path.join(REPO, "paths.local")


def synth_drivers(addon):
    """A built add-on's synthDrivers folder: addon is speakout, blazie or accent."""
    return os.path.join(DIST, "%s-build" % addon, "synthDrivers")


def engine_dir(addon):
    """The engine package a built add-on carries (synthDrivers/_ssi263_<addon>)."""
    return os.path.join(synth_drivers(addon), "_ssi263_%s" % addon)


# ---------------------------------------------------------------- outside the repo

# Every key a script may ask for, and what it is.  paths.local.example lists the same.
KEYS = {
    "RECORDINGS": "the Braille Lite capture folder (holds scripts/ and sessions/MASTER/)",
    "ARCHIVE_AUDIO": "the folder of other real-unit recordings (accent-demo.wav, accent.wav, speakout.wav)",
    "RECLAIM_WAV": "the Braille Lite reading reclaim.txt, recorded from its line out",
    "RECLAIM_TXT": "the text the Braille Lite read for RECLAIM_WAV",
    "Z180EMU": "a z180emu checkout with this project's bns.c",
    "W64DEVKIT": "w64devkit's bin folder, x86_64 (gcc, objdump)",
    "W64DEVKIT_X86": "w64devkit's bin folder, i686",
    "PYTHON64": "64-bit Python 3.13 (python.exe), for the C-vs-Python gate",
    "PYTHON32": "32-bit Python 3.13 (python.exe), for the C-vs-Python gate",
}

_local = None


def _read_local():
    """paths.local: KEY = value lines; # starts a comment."""
    global _local
    if _local is None:
        _local = {}
        if os.path.isfile(LOCAL_FILE):
            with open(LOCAL_FILE, encoding="utf-8") as f:
                for line in f:
                    line = line.split("#", 1)[0].strip()
                    if "=" in line:
                        k, v = line.split("=", 1)
                        _local[k.strip().upper()] = v.strip().strip('"')
    return _local


def lookup(key):
    """The configured path for key, or None."""
    if key not in KEYS:
        raise KeyError("unknown path key %r (add it to KEYS in tools/repo_paths.py)" % key)
    return os.environ.get("SSI263_" + key) or _read_local().get(key) or None


def external(key):
    """The configured path for key; stops with a clear message if it isn't set or doesn't exist."""
    p = lookup(key)
    if not p:
        raise SystemExit("%s is not set: %s.\nSet SSI263_%s, or add '%s = <path>' to %s "
                         "(see paths.local.example)." % (key, KEYS[key], key, key, LOCAL_FILE))
    if not os.path.exists(p):
        raise SystemExit("%s = %s does not exist (%s)." % (key, p, KEYS[key]))
    return p


def program(key, name, path_fallback=True):
    """A program: key's folder (a bin folder) + name, else name on the PATH, else stop.

    path_fallback=False where the PATH could hold the wrong one (the i686 compiler: the
    PATH's gcc is usually the x86_64 one)."""
    folder = lookup(key)
    if folder:
        for p in (os.path.join(folder, name), os.path.join(folder, name + ".exe")):
            if os.path.isfile(p):
                return p
        raise SystemExit("%s = %s has no %s." % (key, folder, name))
    found = shutil.which(name) if path_fallback else None
    if found:
        return found
    raise SystemExit("can't find %s: set SSI263_%s, or %s in %s (%s)."
                     % (name, key, key, LOCAL_FILE, KEYS[key]))


def bin_dir(key, probe="gcc", path_fallback=True):
    """A toolchain's bin folder (to put first on PATH), found as program() finds probe."""
    return os.path.dirname(program(key, probe, path_fallback))


def python(key):
    """PYTHON64 or PYTHON32: a python.exe that must exist.  No PATH fallback: the gate
    has to run on both word sizes, never quietly on one."""
    return external(key)


# ---------------------------------------------------------------- the recordings

def master_predictions():
    """The MASTER run's emulated register streams, one JSON line per utterance."""
    return os.path.join(external("RECORDINGS"), "scripts", "MASTER_predictions.jsonl")


def master_session():
    """The MASTER run's folder: master.wav and utterances.jsonl."""
    return os.path.join(external("RECORDINGS"), "sessions", "MASTER")


def archive_audio(name):
    """One file from the folder of other real-unit recordings."""
    return os.path.join(external("ARCHIVE_AUDIO"), name)
