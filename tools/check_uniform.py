"""Every voice on every platform: the voice x platform matrix, read from what each platform really offers.

0.7.0 passed 243 of 243 checks while Linux and Android lacked three voices: no check looked at the whole matrix
(Tomi: a green suite that hides a broken integration is worse than a red one that tells the truth).  This one does.

The canonical voices are defined once: src/csrc/voices.c's VOICES[] table (voices.h), the table the SAPI engine, the
speech-dispatcher module and Android all speak through.  Every platform must offer every one of them; a voice is
matched by its stable id, voices.h's "<NVDA driver module>:<voice>" ("blazie:blazie", "blazie:blazie_es",
"speakout:speakout", "accentmini:mini", "accentmini:sa"), never by its name, which differs per platform:

  NVDA     the 0.8 add-on (nvda/dist/ssi263-speech-<version>.nvda-addon, the version from nvda/ssi263/manifest.ini),
           unpacked, its units' drivers (synthDrivers/_ssi263_unified) and its own driver imported against stand-in
           NVDA modules (never instantiated): a voice is offered when a firmware type of that driver reaches it
           (FIRMWARE_TYPES, present_languages) and the unit's check() and availableVoices have it; its id is the
           unit's driver name and the voice's key.  Without nvda/ssi263, the three 0.7 add-ons
           (nvda/dist/<addon>-ssi263-<version>.nvda-addon), each driver's own voices.
  SAPI     the stage (nvda/dist/sapi-<version>-final, else sapi-dev): voices.txt (what register.ps1 registers, ids in
           its first column) and, through ctypes, the stage's own ssi263speech.dll (ssv_count, ssv_voice_info,
           ssv_engine_built, ssv_available on the stage's firmware); the two must agree.
  Linux    `sd_ssi263 --voices` when the module is built (--sd-binary, default build/linux/sd_ssi263), its names
           mapped to ids by sd_voices.c's own table; else that table with the engines build_linux.sh compiles into
           voices.c (its SSV_HAVE_* flags, voices.c's #ifdef'd engines).
  Android  SsiEngine.kt's voice list (the TTS service's onGetVoices), its SsiNative indexes (which must equal
           ssa_engine.h's), each index's id in ssa_engine.c's table, and the engines build_android.sh compiles in.

The Braille Lite voices (voices.c's SSV_BLAZIE ones) must also carry "run ahead" and "number words" on each:
  NVDA     the runAhead and numberWords driver settings with their getters and setters (run ahead: the voice in
           RUN_AHEAD_TESTED when the driver keeps one);
  SAPI     the registry values RunAhead and BrailleLiteNumbers, in both stage DLLs (ssi263_sapi.dll, x86 and x64)
           and in the stage's settings dialog (settings.ps1);
  Linux    SSI263RunAhead and SSI263BrailleLiteNumbers in the packaged ssi263.conf (the built package's, else the one
           tools/package_linux.sh writes), read by sd_ssi263.c into a field that sd_voices.c's settings_for puts on
           the voice's engine (and in the binary's strings when it is given);
  Android  the RUN_AHEAD and NUMBERS settings: a checkbox in SettingsActivity, passed to nativeStart (SsiEngine.kt,
           ssa_jni.c), and put on the voice's case in ssa_engine.c's settings_for.

A platform whose artifacts are not there is "skip <platform>: not built" -- a failure when --require names it.

    python tools/check_uniform.py [--require nvda,sapi,linux,android] [--sd-binary PATH] [--linux-conf PATH]
                                  [--sapi-stage DIR]

Must-fail control: SSI263_UNIFORM_DROP=<platform>:<voice id>[:run-ahead|:numbers][,...] drops that voice (or that
feature) from that platform's discovered list, after discovery -- e.g. android:speakout:speakout.  The run must then
fail naming exactly that cell.  Exit 0: no gap; 1: a gap or a required platform skipped.  Stdlib only.
"""
import argparse
import ctypes
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSRC = os.path.join(REPO, "src", "csrc")
DIST = os.path.join(REPO, "nvda", "dist")
ANDROID = os.path.join(REPO, "src", "platforms", "android", "app", "src", "main")
SPEECHD = os.path.join(REPO, "src", "platforms", "speechd")
PLATFORMS = ("nvda", "sapi", "linux", "android")
LABEL = {"nvda": "NVDA", "sapi": "SAPI", "linux": "Linux", "android": "Android"}
FEATURES = ("run-ahead", "numbers")
FEATURE_LABEL = {"run-ahead": "run ahead", "numbers": "number words"}
ADDONS = ("blazie", "speakout", "accent")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def rel(path):
    return os.path.relpath(path, REPO).replace(os.sep, "/")


def c_string(s):
    """A C string literal's body ("espa\\xc3\\xb1ol") as text: its escapes are UTF-8 bytes."""
    return s.encode("latin-1").decode("unicode_escape").encode("latin-1").decode("utf-8")


class Platform:
    """What one platform offers: voices {id: its own name}, features {(id, feature): evidence}, or skipped."""

    def __init__(self, key):
        self.key, self.voices, self.features, self.source, self.skip, self.errors = key, {}, {}, "", None, []
        self.why_not = {}            # (id, feature) -> what was missing, for the report


# ---- the canonical list: voices.c's table --------------------------------------------------------------------------
def canonical():
    text = read(os.path.join(CSRC, "voices.c"))
    table = re.search(r"static const ssv_info VOICES\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not table:
        raise SystemExit("uniform: no VOICES[] table in src/csrc/voices.c")
    voices = []
    for m in re.finditer(r'\{\s*"([^"]+)",\s*"((?:[^"\\]|\\.)*)",\s*"([^"]+)",\s*(SSV_\w+),\s*(\d+),', table.group(1)):
        voices.append(dict(id=m.group(1), name=c_string(m.group(2)), lang=m.group(3), engine=m.group(4)))
    ids = [v["id"] for v in voices]
    if not voices or len(set(ids)) != len(ids):
        raise SystemExit("uniform: src/csrc/voices.c's table is empty or repeats an id: %s" % ids)
    return voices


def engine_gates():
    """voices.c's engine(): each engine kind -> the SSV_HAVE_* flag it needs (None: always compiled in)."""
    text = read(os.path.join(CSRC, "voices.c"))
    body = re.search(r"static const ssv_engine \*engine\(int kind\)\s*\{(.*?)\n\}", text, re.S).group(1)
    sw = body[body.index("switch"):]
    gates, flag = {}, None
    for line in sw.splitlines():
        line = line.strip()
        m = re.match(r"#ifdef (SSV_HAVE_\w+)", line)
        if m:
            flag = m.group(1)
        elif line.startswith("#endif"):
            flag = None
        else:
            m = re.match(r"case (SSV_\w+):", line)
            if m:
                gates[m.group(1)] = flag
    return gates


def script_flags(path, var):
    """The SSV_HAVE_* flags a build script gives voices.c through $var: each -D on a line assigning var, kept when
    every `if [ -f "$SRC/<file>" ]` around it holds in this tree."""
    defined, stack = set(), []
    for line in read(path).splitlines():
        s = line.strip()
        m = re.match(r'if \[ -f "\$SRC/([^"]+)" \]', s)
        if m:
            stack.append(os.path.isfile(os.path.join(CSRC, m.group(1))))
        elif re.match(r"if\b", s):
            stack.append(True)
        elif s == "fi" and stack:
            stack.pop()
        elif re.match(r"else\b", s) and stack:
            stack[-1] = not stack[-1]
        if re.match(r"%s=" % var, s):
            for flag in re.findall(r"-D(SSV_HAVE_\w+)", s):
                if all(stack):
                    defined.add(flag)
    return defined


def built_from_sources(voices, script, var):
    gates, flags = engine_gates(), script_flags(script, var)
    return {v["id"] for v in voices if v["engine"] in gates and (gates[v["engine"]] is None
                                                                 or gates[v["engine"]] in flags)}


def switch_cases(text, func):
    """A C function's switch: [(case labels, body)] up to each break."""
    m = re.search(r"static void %s\(.*?\n\}\n" % func, text, re.S)
    if not m:
        return []
    return [(re.findall(r"case\s+(\w+):", labels), body)
            for labels, body in re.findall(r"((?:\s*case\s+\w+:)+)(.*?)\bbreak;", m.group(0), re.S)]


# ---- NVDA ------------------------------------------------------------------------------------------------------------
PROBE = r'''
import importlib, json, os, sys, types
dirs = sys.argv[1:]
class Setting:
    def __init__(self, id, *a, **k): self.id = id
def mod(name, **kw):
    m = types.ModuleType(name); m.__dict__.update(kw); sys.modules[name] = m; return m
class Base:
    VoiceSetting = staticmethod(lambda: Setting("voice"))
    VariantSetting = staticmethod(lambda: Setting("variant"))
    RateSetting = staticmethod(lambda: Setting("rate"))
    PitchSetting = staticmethod(lambda: Setting("pitch"))
    VolumeSetting = staticmethod(lambda: Setting("volume"))
    InflectionSetting = staticmethod(lambda: Setting("inflection"))
class Cmd:
    def __init__(self, *a, **k): pass
class Log:
    def __getattr__(self, n): return lambda *a, **k: None
mod("nvwave", WavePlayer=object)
mod("config", conf={"audio": {}, "speech": {}})
mod("synthDriverHandler", SynthDriver=Base, VoiceInfo=lambda *a: a, synthIndexReached=object(), synthDoneSpeaking=object())
mod("autoSettingsUtils"); mod("autoSettingsUtils.utils", StringParameterInfo=lambda *a: a)
mod("autoSettingsUtils.driverSetting", BooleanDriverSetting=Setting, DriverSetting=Setting)
mod("logHandler", log=Log())
cmds = mod("speech.commands", IndexCommand=type("IndexCommand", (Cmd,), {}), PitchCommand=type("PitchCommand", (Cmd,), {}),
           LangChangeCommand=type("LangChangeCommand", (Cmd,), {}))
mod("speech", commands=cmds)
unified = dirs[0] == "--unified"
if unified:
    dirs = dirs[1:]
pkg = mod("synthDrivers"); pkg.__path__ = dirs
out = []
reach = None
if unified:      # the 0.8 driver: the unit voices its firmware types reach
    w = importlib.import_module("synthDrivers.ssi263")
    reach = {(t[1].SynthDriver.name, t[2][lang]) for fw, t in w.FIRMWARE_TYPES.items()
             for lang in w.present_languages(fw)}
for d in dirs:
    sub = os.path.join(d, "_ssi263_unified") if unified else d
    for f in sorted(os.listdir(sub)):
        if not f.endswith(".py") or f.startswith("_"):
            continue
        if unified and f == "ssi263.py":
            continue
        m = importlib.import_module(("synthDrivers._ssi263_unified." if unified else "synthDrivers.") + f[:-3])
        cls = m.SynthDriver
        ok = bool(cls.check())
        voices = {k: list(v)[1] for k, v in cls._get_availableVoices(cls).items()} if ok else {}
        if reach is not None:
            voices = {k: v for k, v in voices.items() if (cls.name, k) in reach}
        ids = [s.id for s in cls.supportedSettings]
        acc = sorted(n for n in dir(cls) if n.startswith(("_get_", "_set_")))
        tested = getattr(m, "RUN_AHEAD_TESTED", None)
        out.append(dict(file=f, name=cls.name, check=ok, voices=voices, settings=ids, accessors=acc,
                        run_ahead_tested=list(tested) if tested is not None else None))
print("PROBE " + json.dumps(out))
'''


def nvda(voices, p):
    built, missing = [], []
    unified = os.path.isfile(os.path.join(REPO, "nvda", "ssi263", "manifest.ini"))
    for addon in ADDONS if not unified else ():
        ver = re.search(r"^version\s*=\s*(\S+)", read(os.path.join(REPO, "nvda", addon, "manifest.ini")), re.M).group(1)
        path = os.path.join(DIST, "%s-ssi263-%s.nvda-addon" % (addon, ver))
        (built if os.path.isfile(path) else missing).append(path)
    if unified:
        ver = re.search(r"^version\s*=\s*(\S+)", read(os.path.join(REPO, "nvda", "ssi263", "manifest.ini")),
                        re.M).group(1)
        path = os.path.join(DIST, "ssi263-speech-%s.nvda-addon" % ver)
        (built if os.path.isfile(path) else missing).append(path)
    if missing:
        p.skip = "not built (%s)" % ", ".join(rel(m) for m in missing)
        return
    p.source = ", ".join(rel(b) for b in built)
    tmp = tempfile.mkdtemp(prefix="uniform-nvda-")
    try:
        dirs = []
        for b in built:
            d = os.path.join(tmp, os.path.basename(b))
            with zipfile.ZipFile(b) as z:
                z.extractall(d)
            dirs.append(os.path.join(d, "synthDrivers"))
        r = subprocess.run([sys.executable, "-S", "-c", PROBE] + (["--unified"] if unified else []) + dirs,
                           capture_output=True, text=True,
                           encoding="utf-8", errors="replace", env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"))
        line = [ln for ln in r.stdout.splitlines() if ln.startswith("PROBE ")]
        if r.returncode or not line:
            p.errors.append("the add-ons' drivers could not be read: %s" % (r.stderr.strip().splitlines() or ["?"])[-1])
            return
        drivers = json.loads(line[0][6:])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    blazie = {v["id"] for v in voices if v["engine"] == "SSV_BLAZIE"}
    for drv in drivers:
        if not drv["check"]:
            p.errors.append("the %s driver's check() fails in its built add-on" % drv["name"])
        for key, name in drv["voices"].items():
            vid = "%s:%s" % (drv["name"], key)
            p.voices[vid] = name
            if vid not in blazie:
                continue
            for feat, setting in (("run-ahead", "runAhead"), ("numbers", "numberWords")):
                have = [setting in drv["settings"], "_get_" + setting in drv["accessors"],
                        "_set_" + setting in drv["accessors"]]
                if feat == "run-ahead" and drv["run_ahead_tested"] is not None:
                    have.append(key in drv["run_ahead_tested"])
                if all(have):
                    p.features[(vid, feat)] = "the %s setting" % setting
                else:
                    p.why_not[(vid, feat)] = "the %s driver: %s setting %s" % (
                        drv["name"], setting, "present" if have[0] else "absent") + (
                        "" if all(have[:3]) else ", accessors %s" % have[1:3]) + (
                        ", the voice not in RUN_AHEAD_TESTED" if len(have) > 3 and not have[3] else "")


# ---- SAPI ------------------------------------------------------------------------------------------------------------
class SsvInfo(ctypes.Structure):
    _fields_ = [("id", ctypes.c_char_p), ("name", ctypes.c_char_p), ("lang", ctypes.c_char_p),
                ("engine", ctypes.c_int), ("slot", ctypes.c_int), ("files", ctypes.c_char_p * 4)]


def sapi(voices, p, stage=None):
    manifest = os.path.join(REPO, "nvda", "ssi263", "manifest.ini")      # 0.8's one add-on, else 0.7's Blazie
    if not os.path.isfile(manifest):
        manifest = os.path.join(REPO, "nvda", "blazie", "manifest.ini")
    ver = re.search(r"^version\s*=\s*(\S+)", read(manifest), re.M).group(1)
    stages = [stage] if stage else [os.path.join(DIST, "sapi-%s-final" % ver), os.path.join(DIST, "sapi-dev")]
    stage = next((s for s in stages if os.path.isfile(os.path.join(s, "voices.txt"))), None)
    if not stage:
        p.skip = "not built (no voices.txt in %s)" % " or ".join(rel(s) for s in stages)
        return
    arch = "x64" if sys.maxsize > 2 ** 32 else "x86"
    lib_path = os.path.join(stage, arch, "ssi263speech.dll")
    if os.name != "nt" or not os.path.isfile(lib_path):
        p.skip = "not built (%s)" % rel(lib_path) if os.name == "nt" else "its DLLs cannot be loaded off Windows"
        return
    p.source = rel(stage)
    with open(os.path.join(stage, "voices.txt"), encoding="utf-8-sig") as f:
        listed = {ln.split("\t")[0]: ln.split("\t")[1] for ln in f.read().splitlines() if "\t" in ln}
    lib = ctypes.CDLL(lib_path)
    lib.ssv_voice_info.restype = ctypes.POINTER(SsvInfo)
    lib.ssv_available.argtypes = [ctypes.c_int, ctypes.c_char_p]
    fw = os.path.join(stage, "firmware").encode("mbcs" if os.name == "nt" else "utf-8")
    native = {}
    for i in range(lib.ssv_count()):
        info = lib.ssv_voice_info(i).contents
        if lib.ssv_engine_built(i) and lib.ssv_available(i, fw):
            native[info.id.decode("utf-8")] = info.name.decode("utf-8")
    if set(listed) != set(native):
        p.errors.append("voices.txt lists %s, the stage's ssi263speech.dll can make %s" % (sorted(listed), sorted(native)))
    p.voices = {k: v for k, v in listed.items() if k in native}
    dlls = [os.path.join(stage, a, "ssi263_sapi.dll") for a in ("x86", "x64")]
    blobs = [open(d, "rb").read() if os.path.isfile(d) else b"" for d in dlls]
    dialog = os.path.join(stage, "settings.ps1")
    ps1 = read(dialog) if os.path.isfile(dialog) else ""
    for feat, value in (("run-ahead", "RunAhead"), ("numbers", "BrailleLiteNumbers")):
        wide = value.encode("utf-16-le")
        in_dlls = [rel(d) for d, b in zip(dlls, blobs) if wide in b]
        in_dialog = bool(re.search(r"^[^#\n]*Save-Setting\s+'%s'" % value, ps1, re.M))    # not in a comment
        for v in voices:
            if v["engine"] != "SSV_BLAZIE":
                continue
            if len(in_dlls) == 2 and in_dialog:
                p.features[(v["id"], feat)] = "the %s value" % value
            else:
                p.why_not[(v["id"], feat)] = "the registry value %s: read by %s; in settings.ps1: %s" % (
                    value, " and ".join(in_dlls) or "neither ssi263_sapi.dll", "yes" if in_dialog else "no")


# ---- Linux -----------------------------------------------------------------------------------------------------------
def linux(voices, p, binary=None, conf=None):
    sdv = read(os.path.join(SPEECHD, "sd_voices.c"))
    table = re.search(r"static const voice_def ALL\[\]\s*=\s*\{(.*?)\n\};", sdv, re.S).group(1)
    sd_ids = {c_string(m.group(2)): m.group(1)
              for m in re.finditer(r'\{\s*"([^"]+)",\s*"((?:[^"\\]|\\.)*)",\s*"[^"]+",\s*"[^"]+"', table)}
    if binary is None:
        default = os.path.join(REPO, "build", "linux", "sd_ssi263")
        binary = default if os.path.isfile(default) and os.name != "nt" else None
    blob = b""
    if binary:
        r = subprocess.run([binary, "--voices"], capture_output=True)
        lines = [ln.split("\t") for ln in r.stdout.decode("utf-8").splitlines() if ln.count("\t") == 2]
        if r.returncode or not lines:
            p.errors.append("%s --voices listed nothing (exit %d)" % (binary, r.returncode))
        for _engine, _lang, name in lines:
            if name in sd_ids:
                p.voices[sd_ids[name]] = name
            else:
                p.errors.append("sd_ssi263 offers %r, which sd_voices.c's table does not name" % name)
        with open(binary, "rb") as f:
            blob = f.read()
        p.source = "%s --voices" % rel(os.path.abspath(binary))
    else:
        built = built_from_sources(voices, os.path.join(REPO, "build_linux.sh"), "SD_DEFS")
        p.voices = {i: n for n, i in sd_ids.items() if i in built}
        p.source = "src/platforms/speechd/sd_voices.c + build_linux.sh's engines (no sd_ssi263 built here)"
    # the packaged conf: the built package's, else the one package_linux.sh writes
    if conf is None:
        found = glob.glob(os.path.join(REPO, "build", "package", "ssi263-speech-*-linux-*", "share", "ssi263-speech",
                                       "speech-dispatcher", "ssi263.conf"))
        conf = max(found, key=os.path.getmtime) if found else None
    if conf:
        conf_text, conf_from = read(conf), rel(os.path.abspath(conf))
    else:
        pkg = read(os.path.join(REPO, "tools", "package_linux.sh"))
        m = re.search(r'cat > "\$STAGE/share/ssi263-speech/speech-dispatcher/ssi263\.conf" <<\'EOF\'\n(.*?)\nEOF\n', pkg, re.S)
        conf_text, conf_from = (m.group(1) if m else ""), "tools/package_linux.sh's ssi263.conf"
    p.source += "; conf: " + conf_from
    reader = read(os.path.join(SPEECHD, "sd_ssi263.c"))
    cases = switch_cases(sdv, "settings_for")
    for feat, key, field in (("run-ahead", "SSI263RunAhead", "run_ahead"), ("numbers", "SSI263BrailleLiteNumbers",
                                                                             "numbers")):
        in_conf = bool(re.search(r"^#?\s*%s\s+\S" % key, conf_text, re.M))
        m = re.search(r'"%s"\)\)\s*conf\.(\w+)\s*=' % key, reader)
        conf_field = m.group(1) if m else None
        in_binary = not binary or key.encode() in blob
        for v in voices:
            if v["engine"] != "SSV_BLAZIE":
                continue
            applied = conf_field and any(v["engine"] in labels and re.search(r"o->%s\s*=\s*s->%s\b" % (field, conf_field), body)
                                         for labels, body in cases)
            if in_conf and applied and in_binary:
                p.features[(v["id"], feat)] = key
            else:
                p.why_not[(v["id"], feat)] = "%s: in %s %s; read by sd_ssi263.c into %s; put on %s by settings_for %s%s" % (
                    key, conf_from, "yes" if in_conf else "no", conf_field, v["engine"], "yes" if applied else "no",
                    "" if in_binary else "; not in the binary's strings")


# ---- Android ---------------------------------------------------------------------------------------------------------
def android(voices, p):
    kt = os.path.join(ANDROID, "kotlin", "com", "ssi263speech", "tts")
    engine_kt, native_kt = read(os.path.join(kt, "SsiEngine.kt")), read(os.path.join(kt, "SsiNative.kt"))
    header, engine_c = read(os.path.join(ANDROID, "cpp", "ssa_engine.h")), read(os.path.join(ANDROID, "cpp", "ssa_engine.c"))
    consts = {k: int(v) for k, v in re.findall(r"const val (\w+) = (\d+)", native_kt)}
    defines = {k: int(v) for k, v in re.findall(r"#define SSA_(\w+) (\d+)", header)}
    table = re.search(r"static const voice_def VOICES\[SSA_VOICES\]\s*=\s*\{(.*?)\n\};", engine_c, re.S).group(1)
    ids = re.findall(r'\{\s*"([^"]+)",\s*\{', table)
    if len(ids) != defines.get("VOICES"):
        p.errors.append("ssa_engine.c's table has %d voices, SSA_VOICES is %s" % (len(ids), defines.get("VOICES")))
    built = built_from_sources(voices, os.path.join(REPO, "build_android.sh"), "HAVE")
    index_name = {}
    for const, name in re.findall(r'VoiceInfo\(SsiNative\.(\w+),\s*"([^"]+)"', engine_kt):
        if const not in consts or consts[const] != defines.get(const):
            p.errors.append("SsiNative.%s is %s, ssa_engine.h's SSA_%s is %s" % (const, consts.get(const), const,
                                                                                    defines.get(const)))
            continue
        index_name[consts[const]] = "SSA_" + const
        vid = ids[consts[const]] if consts[const] < len(ids) else None
        if vid in built:
            p.voices[vid] = name
    p.source = "SsiEngine.kt + SsiNative.kt + ssa_engine.c + build_android.sh's engines"
    settings = read(os.path.join(kt, "SettingsActivity.kt"))
    jni = read(os.path.join(ANDROID, "cpp", "ssa_jni.c"))
    cases = switch_cases(engine_c, "settings_for")
    for feat, const, kt_field, c_field in (("run-ahead", "RUN_AHEAD", "runAhead", "run_ahead"),
                                           ("numbers", "NUMBERS", "numbers", "numbers")):
        chain = [bool(re.search(r"ui\.checkBox\([^\n]*\n?\s*put\(SsiSettings\.%s\b" % const, settings)),
                 "bit(s.%s)" % kt_field in engine_kt,
                 bool(re.search(r"s\.%s\s*=\s*%s;" % (c_field, c_field), jni))]
        for v in voices:
            if v["engine"] != "SSV_BLAZIE" or v["id"] not in ids:
                continue
            label = index_name.get(ids.index(v["id"]))
            applied = any(label in labels and re.search(r"o->%s\s*=\s*s->%s\b" % (c_field, c_field), body)
                          for labels, body in cases)
            if all(chain) and applied:
                p.features[(v["id"], feat)] = "the %s setting" % const
            else:
                p.why_not[(v["id"], feat)] = ("SsiSettings.%s: a checkbox %s, sent by SsiEngine.kt %s, by ssa_jni.c %s, "
                                              "put on %s by settings_for %s" % (const, *("yes" if c else "no" for c in chain),
                                                                               label, "yes" if applied else "no"))


# ---- the matrix ------------------------------------------------------------------------------------------------------
def drop(found):
    """SSI263_UNIFORM_DROP: the must-fail control's voices and features taken out after discovery."""
    for item in filter(None, os.environ.get("SSI263_UNIFORM_DROP", "").split(",")):
        plat, _, rest = item.strip().partition(":")
        feat = next((f for f in FEATURES if rest.endswith(":" + f)), None)
        vid = rest[:-len(feat) - 1] if feat else rest
        if plat not in found:
            raise SystemExit("uniform: SSI263_UNIFORM_DROP names no platform %r" % plat)
        p = found[plat]
        if feat:
            p.features.pop((vid, feat), None)
            p.why_not[(vid, feat)] = "dropped by SSI263_UNIFORM_DROP"
        else:
            p.voices.pop(vid, None)
        print("control: %s dropped from %s's discovered list" % (FEATURE_LABEL.get(feat, "") + " of " + vid if feat
                                                                   else vid, LABEL[plat]))


def main():
    try:                                # the voices' names are UTF-8 ("español") whatever the console is
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--require", default="", help="platforms that may not be skipped, comma-separated")
    ap.add_argument("--sd-binary", help="the built speech-dispatcher module (default build/linux/sd_ssi263, on Linux)")
    ap.add_argument("--linux-conf", help="the packaged ssi263.conf (default: the built package's, else package_linux.sh's)")
    ap.add_argument("--sapi-stage", help="the SAPI stage (default nvda/dist/sapi-<version>-final, else sapi-dev)")
    a = ap.parse_args()
    require = {r.strip() for r in a.require.split(",") if r.strip()}
    if require - set(PLATFORMS):
        raise SystemExit("uniform: --require names unknown platforms: %s" % sorted(require - set(PLATFORMS)))
    voices = canonical()
    found = {k: Platform(k) for k in PLATFORMS}
    nvda(voices, found["nvda"])
    sapi(voices, found["sapi"], a.sapi_stage)
    linux(voices, found["linux"], a.sd_binary, a.linux_conf)
    android(voices, found["android"])
    drop(found)

    canon = {v["id"] for v in voices}
    gaps, lines = [], []
    rows = [(v["name"], v["id"], None) for v in voices]
    rows += [("  %s: %s" % (FEATURE_LABEL[f], v["name"]), v["id"], f) for f in FEATURES for v in voices
             if v["engine"] == "SSV_BLAZIE"]
    width = max(len(r[0]) for r in rows) + 2
    print("voice x platform (canonical: src/csrc/voices.c, %d voices, by id)" % len(voices))
    print("%-*s%s" % (width, "", "".join("%-9s" % LABEL[k] for k in PLATFORMS)))
    for label, vid, feat in rows:
        cells = []
        for k in PLATFORMS:
            p = found[k]
            if p.skip:
                cells.append("skip")
            elif feat is None:
                cells.append("yes" if vid in p.voices else "NO")
                if vid not in p.voices:
                    gaps.append((k, "MISSING  %s [%s] on %s" % (label, vid, LABEL[k])))
            elif vid not in p.voices:
                cells.append("-")                    # the voice itself is the gap
            else:
                have = (vid, feat) in p.features
                cells.append("yes" if have else "NO")
                if not have:
                    gaps.append((k, "MISSING  %s for %s [%s] on %s: %s" % (
                        FEATURE_LABEL[feat], label.split(": ", 1)[1], vid, LABEL[k], p.why_not.get((vid, feat), "?"))))
        print("%-*s%s" % (width, label, "".join("%-9s" % c for c in cells)))
    for k in PLATFORMS:
        p = found[k]
        if p.skip:
            if k in require:
                gaps.append((k, "FAIL  %s is required: %s" % (LABEL[k], p.skip)))
            lines.append("skip %s: %s" % (LABEL[k], p.skip))
            continue
        for vid in sorted(set(p.voices) - canon):
            gaps.append((k, "EXTRA  %s offers %s [%s], which src/csrc/voices.c does not list" % (LABEL[k], p.voices[vid],
                                                                                                  vid)))
        for e in p.errors:
            gaps.append((k, "FAIL  %s: %s" % (LABEL[k], e)))
        lines.append("%s %s: %d of %d voices, from %s" % ("gap " if any(g[0] == k for g in gaps) else "ok  ",
                                                          LABEL[k], len(set(p.voices) & canon), len(canon), p.source))
    for ln in lines + [g[1] for g in gaps]:
        print(ln)
    if gaps:
        print("uniform: %d gap%s" % (len(gaps), "" if len(gaps) == 1 else "s"))
        return 1
    checked = [k for k in PLATFORMS if not found[k].skip]
    print("uniform: every voice on %s, with the Braille Lite's run ahead and number words" %
          ", ".join(LABEL[k] for k in checked))
    return 0


if __name__ == "__main__":
    sys.exit(main())
