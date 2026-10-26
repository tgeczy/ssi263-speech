"""The old-driver/current-chip integration reference: 0.7.0's Python Speak-Out, Accent and Braille Lite drivers and
hosts (as nvda/build_*.py made them at REV, the last commit with the Python hosts in the add-ons) over THIS tree's chip
and native libraries.  The native drivers (0.7.5 on) and the C voices are held to it, so it tests the driver ports'
integration -- not the historical 0.7.0 sound, and not the chip's accuracy (the Python/C drift test, the hard-G state
test and the listening renders are that evidence).  The SAPI tests' reference server runs all three
(sapi/reference_drivers.py).

    python legacy_drivers.py [speakout|accent|blazie ...]   # print each one's synthDrivers folder (making it once)
    python legacy_drivers.py --manifest                     # what the reference is made of, and its key
    legacy_drivers.synth_drivers("speakout")                # the same, for a test

What it is made of (plan(), written beside it as MANIFEST.json):
  rev   from REV (git show): the drivers, src/hosts, nvda/shared
  tree  from this tree: the chip package (src/ssi263) and its ROM data (src/data), since 0.7.6's hard G (params
        hold_release) -- REV's package mirrored ssi263_params as it was, and the C struct has grown; and, not in git,
        the chip (src/ssi263/_bin), nvda/dist's native host and CPU libraries (bl.dll, bl_live_mame.exe, pc86.dll,
        accent_sa.dll, speakout_v40.dll) and firmware/
  gen   written here: the engine package's __init__, and the Accent-mini's INIT snapshot (SPKEMS.state), made as
        build_accent.py made it, on MAME's 8086, from the files above
  fix   from REV, with FIXES applied: the later driver fixes the native drivers carry on purpose, each one exact
        replacement (it fails when REV's text is not found once); their text is in the key.  0.8: the Braille Lite's
        end tail (a last sound still decaying when the unit is done is rendered on, up to TAIL_MAX_S: the German
        unit's blip, Tomi) -- without it the Spanish voice, whose last sounds decay, ends a block earlier than the
        native voices and every utterance after it starts from another chip state; and the Speak-Out's cancel
        ending with a CR (the firmware's rules kept a cut word's context: "Hello" after "Custom ..." cut after K HF
        read with an extra UH)
The folder's name is the hash of that list with every file's own bytes (REV's by their git blob ids, this tree's by
SHA-256): any changed binary, firmware or package file -- rebuilt from unchanged sources or not -- makes a new
reference rather than reusing one made over other bytes.  A test points fake_nvda_driver_test.py at a folder by
SSI263_SYNTH_DRIVERS.  Made once, into a temporary folder renamed into place, so parallel checks can share it.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
REV = "9bde0a76eba81be958d216c7b75edba2eb22c29f"      # main before 0.7.5's native drivers (the Python hosts' last)
NAME = "old-driver/current-chip integration reference"
CHIP = ("__init__.py", "chip.py", "dsp.py", "native.py", "params.py", "rom.py")     # src/ssi263, from this tree
DIST = os.path.join(REPO, "nvda", "dist")
FW = os.path.join(REPO, "firmware")
ARCHES = ("x64", "x86")
ADDONS = ("speakout", "accent", "blazie")
TAIL_OLD = """                if not unit.busy():
                    self._pitch_dirty = False    # the unit has read everything sent so far
                    break
"""
TAIL_NEW = """                if not unit.busy():
                    if tail < 0.3 and any(abs(v) > LEAD_THRESHOLD for v in y):
                        tail += BLOCK_S              # its last sound still decaying (0.8's end tail)
                        continue
                    self._pitch_dirty = False    # the unit has read everything sent so far
                    break
"""
TAIL_START_OLD = """            unit.say(lines)
            while not self._cancelFlag.is_set():
                y = unit.run(BLOCK_S)
"""
TAIL_START_NEW = """            unit.say(lines)
            tail = 0.0
            while not self._cancelFlag.is_set():
                y = unit.run(BLOCK_S)
"""
SO_CANCEL_OLD = """        while t < limit and self.busy():
            self.skip(0.02)
            t += 0.02

    def skip(self, seconds, step=0.002):
"""
SO_CANCEL_NEW = """        while t < limit and self.busy():
            self.skip(0.02)
            t += 0.02
        self.say("\\r")                     # 0.8's cancel: a line's end (the rules' word context)

    def skip(self, seconds, step=0.002):
"""
# 0.9.1: a text goes on while the chip still sounds its last phoneme (at rate 0 one outlasts `quiet`, and its rest
# played at the head of the next text: tools/check_so_tail.py), and the write that ends it counts as speech
SO_HELD_OLD = """    def busy(self, quiet=0.06, patience=1.5):
        \"\"\"Still speaking: input pending, a line not yet started (up to `patience` s),
        or a speech phoneme loaded within `quiet` s.\"\"\"
        if self.input_pending() or (self.chip.time - self.last_speech) < quiet:
            return True
"""
SO_HELD_NEW = """    def held(self):
        return bool(self.chip.regs[0] & 0x3F) and not self.chip.regs[3] & 0x80

    def busy(self, quiet=0.06, patience=1.5):
        since = self.chip.time - self.last_speech
        if self.input_pending() or since < quiet or (self.held() and since < patience):
            return True
"""
SO_ENDS_OLD = """    def _chip_write(self, reg, v):
        self.chip.write(reg, v)
        if self.keep_writes:
            self.chip_writes.append((round(self.chip.time, 6), reg, v))
"""
SO_ENDS_NEW = """    def _chip_write(self, reg, v):
        sounding = reg == 0 and self.held()
        self.chip.write(reg, v)
        if self.keep_writes:
            self.chip_writes.append((round(self.chip.time, 6), reg, v))
        if sounding:
            self.last_speech = self.chip.time
"""
# {REV's file: [(old, new)]}: each old text must be found exactly once in REV's file
FIXES = {"nvda/blazie/synthDrivers/blazie.py": [(TAIL_START_OLD, TAIL_START_NEW), (TAIL_OLD, TAIL_NEW)],
         "src/hosts/speakout.py": [(SO_CANCEL_OLD, SO_CANCEL_NEW), (SO_HELD_OLD, SO_HELD_NEW),
                                   (SO_ENDS_OLD, SO_ENDS_NEW)]}
ENGINE_INIT = ('"""The %s\'s engine package: this tree\'s chip under 0.7.0\'s drivers '
               '(nvda/tools/legacy_drivers.py)."""\n' % NAME)
INIT_CODE = ("import hashlib, pickle, sys; sys.path.insert(0, %r)\n"
             "from hosts.accent import Accent\nfrom ssi263.native import SSI263C\n"
             "dvc = %r\nstate = Accent(dvc, chip=SSI263C(), core='mame').init_state()\n"
             "state['dvc_sha256'] = hashlib.sha256(open(dvc, 'rb').read()).hexdigest()\n"
             "pickle.dump(state, open(%r, 'wb'), protocol=4)\n")


def _git(*args):
    return subprocess.run(["git", "-C", REPO] + list(args), capture_output=True, check=True).stdout


# ---- the plan: [(dest relative to the reference folder, kind, source)] ----------------------------------------------
def _rev_files(rev_dir, skip=("drivers.py",)):
    """{path: blob id} of a folder at REV (its research-only drivers.py left out, as the build did)"""
    out = {}
    for line in _git("ls-tree", "-r", REV, rev_dir).decode().splitlines():
        meta, path = line.split("\t", 1)
        if os.path.basename(path) not in skip:
            out[path] = meta.split()[2]
    return out


def _engine(eng):
    """build_common.copy_engine: the chip package (this tree's) with its C core, the data, the package's __init__"""
    items = [(eng + "/ssi263/" + n, "tree", "src/ssi263/" + n) for n in CHIP]
    items += [(eng + "/ssi263/_bin/%s/ssi263.dll" % a, "tree", "src/ssi263/_bin/%s/ssi263.dll" % a) for a in ARCHES]
    for path in sorted(_git("ls-files", "src/data").decode().split()):
        items.append((eng + "/data/" + path[len("src/data/"):], "tree", path))
    items.append((eng + "/__init__.py", "gen", "engine __init__"))
    items += [(eng + "/" + n, "rev", "nvda/shared/" + n) for n in ("ssi263_rates.py", "ssi263_numwords.py")]
    return items


def _addon(addon):
    sd = "%s-build/synthDrivers" % addon
    if addon == "speakout":
        eng = sd + "/_ssi263_speakout"
        items = [(sd + "/speakout.py", "rev", "nvda/speakout/synthDrivers/speakout.py")] + _engine(eng)
        items += [(eng + "/" + d, "rev", "src/hosts/" + s) for s, d in (
            ("speakout.py", "speakout_host.py"), ("speakout_v40.py", "speakout_v40.py"), ("x86_api.py", "x86_api.py"))]
        items = [(d, "fix" if s == "src/hosts/speakout.py" else k, s) for d, k, s in items]
        items += [(eng + "/bin/%s/speakout_v40.dll" % a, "tree", "nvda/dist/speakout-lib/%s/speakout_v40.dll" % a)
                  for a in ARCHES]
        items.append((eng + "/SPEAKOUT.HEX", "tree", "firmware/gw-micro-speakout/SPEAKOUT.HEX"))
    elif addon == "accent":
        eng = sd + "/_ssi263_accent"
        items = [(sd + "/accentmini.py", "rev", "nvda/accent/synthDrivers/accentmini.py")] + _engine(eng)
        items += [(eng + "/" + d, "rev", "src/hosts/" + s) for s, d in (
            ("accent.py", "accent_host.py"), ("accent_sa.py", "accent_sa_host.py"), ("accent_sa_c.py", "accent_sa_c.py"),
            ("pc86.py", "pc86.py"), ("x86_api.py", "x86_api.py"))]
        for a in ARCHES:
            items.append((eng + "/bin/%s/pc86.dll" % a, "tree", "nvda/dist/blazie-lib/%s/pc86.dll" % a))
            items.append((eng + "/bin/%s/accent_sa.dll" % a, "tree", "nvda/dist/accentsa-lib/%s/accent_sa.dll" % a))
        items += [(eng + "/accent-sa/" + n, "tree", "firmware/aicom-accent-sa/" + n) for n in ("u2.BIN", "u3.BIN", "u4.BIN")]
        items.append((eng + "/SPKEMS.DVC", "tree", "firmware/aicom-accent-mini/SPKEMS.DVC"))
        # the INIT snapshot's own tree: REV's hosts and the same engine (made in a temporary folder, not kept)
        items += [("accent-init-src/hosts/" + p[len("src/hosts/"):], "rev", p) for p in sorted(_rev_files("src/hosts"))]
        items += [("accent-init-src/" + d.split("/_ssi263_accent/", 1)[1], k, s) for d, k, s in _engine(eng)]
        items.append((eng + "/SPKEMS.state", "gen", "the driver's INIT on MAME's 8086 (build_accent.write_state)"))
    else:
        eng = sd + "/_ssi263_blazie"
        items = [(sd + "/blazie.py", "fix", "nvda/blazie/synthDrivers/blazie.py")] + _engine(eng)
        items += [(eng + "/" + d, "rev", "src/hosts/" + s) for s, d in (
            ("blazie.py", "blazie_host.py"), ("native_blazie.py", "native_blazie.py"), ("blazie_idle.py", "blazie_idle.py"))]
        items += [(eng + "/bin/%s/bl.dll" % a, "tree", "nvda/dist/blazie-lib/%s/bl.dll" % a) for a in ARCHES]
        items.append((eng + "/bns_live.exe", "tree", "nvda/dist/blazie-lib/bl_live_mame.exe"))
        for n in ("BL2ENG.BNS", "bl2_2003_warm.state"):
            items.append((eng + "/" + n, "tree", "firmware/blazie/" + n))
        for n in ("BL2SPA.BNS", "bl2spa_fresh.state"):             # the Spanish voice, when its firmware is here
            if os.path.isfile(os.path.join(FW, "blazie", "spanish", n)):
                items.append((eng + "/" + n, "tree", "firmware/blazie/spanish/" + n))
    return items


_SHA = {}


def _sha(path):
    if path in _SHA:
        return _SHA[path]
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    _SHA[path] = h.hexdigest()
    return _SHA[path]


def plan():
    """[(dest, kind, source, identity)] for all three add-ons; identity = REV's blob id, the tree file's SHA-256
    ("missing" when it is not built), or the generated content's SHA-256"""
    blobs = {}
    for d in ("nvda/shared", "nvda/speakout", "nvda/accent", "nvda/blazie", "src/hosts"):
        blobs.update(_rev_files(d, skip=()))
    gen = {"engine __init__": hashlib.sha256(ENGINE_INIT.encode()).hexdigest()}
    out = []
    for addon in ADDONS:
        for dest, kind, src in _addon(addon):
            if kind == "rev":
                ident = "git:" + blobs[src]
            elif kind == "fix":
                fixes = "".join(o + "\0" + n + "\0" for o, n in FIXES[src])
                ident = "git:%s+fix:%s" % (blobs[src], hashlib.sha256(fixes.encode()).hexdigest()[:16])
            elif kind == "tree":
                p = os.path.join(REPO, src)
                ident = "sha256:" + _sha(p) if os.path.isfile(p) else "missing"
            else:
                ident = "sha256:" + gen.get(src, hashlib.sha256((INIT_CODE + src).encode()).hexdigest())
            out.append((dest, kind, src, ident))
    return out


def key(items=None):
    h = hashlib.sha256(("%s\n%s\n" % (NAME, REV)).encode())
    for item in items or plan():
        h.update(("\t".join(item) + "\n").encode())
    return h.hexdigest()[:12]


ITEMS = plan()
KEY = key(ITEMS)
OUT = os.path.join(HERE, "out", "reference-%s-%s" % (REV[:7], KEY))


def manifest():
    return {"name": NAME, "rev": REV, "key": KEY,
            "note": "0.7.0's drivers and hosts (rev) over this tree's chip package, ROM data and native libraries "
                    "(tree); not the historical 0.7.0 sound and not a check of the chip's accuracy",
            "rom_data": "tree (src/data of this tree, beside the chip package that reads it)",
            "files": [{"dest": d, "kind": k, "source": s, "id": i} for d, k, s, i in ITEMS]}


# ---- making it ------------------------------------------------------------------------------------------------------
def _make(addon, root):
    for dest, kind, src, ident in ITEMS:
        if not (dest.startswith(addon + "-build/") or (addon == "accent" and dest.startswith("accent-init-src/"))):
            continue
        path = os.path.join(root, *dest.split("/"))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if kind == "rev":
            with open(path, "wb") as f:
                f.write(_git("show", "%s:%s" % (REV, src)))
        elif kind == "fix":
            text = _git("show", "%s:%s" % (REV, src)).decode("utf-8")
            eol = "\r\n" if "\r\n" in text else "\n"
            text = text.replace("\r\n", "\n")
            for old, new in FIXES[src]:
                if text.count(old) != 1:
                    sys.exit("legacy_drivers: a fix's text is not in %s at %s exactly once" % (src, REV[:7]))
                text = text.replace(old, new)
            with open(path, "wb") as f:
                f.write(text.replace("\n", eol).encode("utf-8"))
        elif kind == "tree":
            if ident == "missing":
                sys.exit("legacy_drivers: %s is missing (build it first)" % os.path.join(REPO, src))
            shutil.copy2(os.path.join(REPO, src), path)
        elif src == "engine __init__":
            with open(path, "w") as f:
                f.write(ENGINE_INIT)
    if addon == "accent":
        eng = os.path.join(root, "accent-build", "synthDrivers", "_ssi263_accent")
        code = INIT_CODE % (os.path.join(root, "accent-init-src"), os.path.join(eng, "SPKEMS.DVC"),
                            os.path.join(eng, "SPKEMS.state"))
        env = dict(os.environ, SSI263_PC86_DLL=os.path.join(eng, "bin", "x64" if sys.maxsize > 2 ** 32 else "x86",
                                                            "pc86.dll"))
        subprocess.run([sys.executable, "-c", code], check=True, env=env, capture_output=True)


def synth_drivers(addon):
    """the reference's synthDrivers folder for "speakout", "accent" or "blazie", made on first use"""
    final = os.path.join(OUT, "%s-build" % addon)
    if not os.path.isdir(final):
        os.makedirs(OUT, exist_ok=True)
        mf = os.path.join(OUT, "MANIFEST.json")
        if not os.path.isfile(mf):
            tmp_mf = mf + ".%d.tmp" % os.getpid()
            with open(tmp_mf, "w", encoding="utf-8") as f:
                json.dump(manifest(), f, indent=1)
            os.replace(tmp_mf, mf)
        tmp = tempfile.mkdtemp(prefix="tmp-%s-" % addon, dir=OUT)
        try:
            _make(addon, tmp)
            try:
                os.rename(os.path.join(tmp, "%s-build" % addon), final)
            except OSError:
                if not os.path.isdir(final):        # another check made it meanwhile: use theirs
                    raise
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    return os.path.join(final, "synthDrivers")


if __name__ == "__main__":
    if "--manifest" in sys.argv:
        m = manifest()
        print("%s, key %s (REV %s): %d files" % (NAME, KEY, REV[:7], len(m["files"])))
        for kind in ("rev", "tree", "gen"):
            print("  %-4s %d" % (kind, sum(1 for f in m["files"] if f["kind"] == kind)))
        sys.exit(0)
    for a in [x for x in sys.argv[1:] if not x.startswith("--")] or ADDONS:
        print(synth_drivers(a))
