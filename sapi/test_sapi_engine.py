"""The SAPI engine DLL itself, 64- and 32-bit, against the Python server it replaces: every byte SAPI would get.

sapi_harness.exe (sapi\\build.ps1 -Dev) loads the development ssi263_sapi.dll -- its own COM class, its own settings
key, nothing registered anywhere -- takes the engine from DllGetClassObject and drives ISpTTSEngine as SAPI does:
SetObjectToken, GetOutputFormat, Speak with SAPI's fragments (text, a bookmark, the pitch's MiddleAdj) and an engine
site that answers GetRate, collects the writes and the events, and raises SPVES_ABORT after a given number of writes.
The settings dialog's values are written to the harness's own key first (SSI263_SAPI_SETTINGS_KEY: this run's, deleted
afterwards), so this user's settings and the installed engine are never touched.

(Why not SAPI itself: SAPI refuses voice tokens kept under HKCU -- E_ACCESSDENIED at Speak, even for a copy of a
Microsoft voice's token -- so a development build reaches SAPI only through machine-wide tokens.  test_sapi.ps1 and
test_sapi_settings.ps1 go through SAPI and System.Speech for an installed build.)

The checks, each case's audio against sapi/ssi_serve.py (the 0.7.0 engine) with the same settings, on 0.7.0's Python
drivers (nvda/tools/legacy_drivers.py; nvda/dist's are the native ones since 0.7.5), which sapi/reference_drivers.py
checks first -- SSI263_SAPI_REF_BREAK=dist, the reference on nvda/dist's drivers, must fail that:
  - each voice at SAPI's rates and pitches, texts with numbers and money: byte for byte, then the engine's 150 ms of
    silence, at the rate GetOutputFormat declared;
  - the dialog's settings (inflection off, the whine, the Accent's intonation, run ahead, the Braille Lite's number
    words off) and each sample rate: the same;
  - BrailleLiteNumbers with no value, 1 and 0 on both Braille Lite voices ("1,234,567", Spain's "1.234.567"): each the
    same as the Python server with the driver's numberWords so; no value = 1, and 0 differs from 1;
  - a bookmark between two halves: the event comes, and the audio is the joined text's;
  - SAPI's abort after a few writes: Speak returns S_OK at once, nothing more is written (no events, no silence),
    and the next text comes out whole (voiced, as long as alone within 10 %);
  - the controls, in the same run (each must make its cases differ): SSI263_SAPI_TEST_BREAK=voice (English and
    Spanish swapped; the Mockingboard spoken by the Braille Lite, as ssi_voice's fallback would), =setting (the
    dialog's settings dropped) and =bl-numbers (BrailleLiteNumbers 0 ignored).

The voices 0.7.0's drivers never had (NATIVE_ONLY: the Mockingboard, new in 0.8) have no Python server to be held to:
their reference is the native serve host (ssi263_serve.exe, build/win/x64) on the stage's firmware with the same
settings on its command line -- the voice as the engine's own mapping makes it (sapi/ssi_native.c), so what is held is
the DLL's side: SAPI's fragments, rates and pitches, the declared rate, the 150 ms of silence, the bookmark and the
abort, byte for byte.  The voice itself is test_native.py's (both widths, voiced, its settings changing it) and
src/csrc/mockingboard's.

    python sapi/test_sapi_engine.py [--arch x64|x86|both] [--verbose]
"""
import concurrent.futures
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import winreg

import reference_drivers

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
STAGE = os.path.join(REPO, "nvda", "dist", "sapi-dev")
SERVE = os.path.join(REPO, "build", "win", "x64", "ssi263_serve.exe")
ARGS = sys.argv[1:]
ARCHES = ["x64", "x86"]
if "--arch" in ARGS and ARGS[ARGS.index("--arch") + 1] != "both":
    ARCHES = [ARGS[ARGS.index("--arch") + 1]]
REQ = 0x4F535034
KEYS = ("Inflection", "Whine", "AccentInflection", "RunAhead", "SampleRate", "BrailleLiteNumbers")
# None: no value in the key at all (the harness deletes it), the engine's own default
DEFAULTS = {"Inflection": 1, "Whine": 0, "AccentInflection": 100, "RunAhead": 0, "SampleRate": 22050,
            "BrailleLiteNumbers": None}
DIALOG = {"Inflection": 0, "Whine": 2, "AccentInflection": 50, "RunAhead": 1, "SampleRate": 22050,
          "BrailleLiteNumbers": 0}
# the Braille Lite's "Read numbers as words": no value, 1 and 0, each on fresh units (a harness and a Python server of
# its own: the setting is not a boot setting, so in the main run the units would carry the last case's state)
NUMBERS = (("numbers unset", DEFAULTS), ("numbers on", dict(DEFAULTS, BrailleLiteNumbers=1)),
           ("numbers off", dict(DEFAULTS, BrailleLiteNumbers=0)))
NUMBERS_TEXT = {"en": "1,234,567", "es": "1.234.567"}
TEXT_EN = ["Hello, how are you??", "Room 12, $3.50 and 1,234,567 items; the 21st of 3, -2.5 degrees."]
TEXT_ES = ["Mañana, ¿qué tal? Son 1.234.567 y 3,5."]
NATIVE_ONLY = ("mockingboard:mockingboard",)
LONG = "This sentence is long enough to be cut somewhere in the middle of it, surely. And a few more words follow it."
ABORT_AFTER = 6


def cases(voices):
    """[(label, voice, sapi rate, middle adj, settings, break, abort after, text, second text or None)], in the order
    the harness speaks them.  The units keep their state from one utterance to the next, in the engine as in the
    Python server, so the order is the reference's too: each settings group together (a boot setting changed boots
    every unit again in the engine, and is a new Python server), an abort last in its group (where it lands decides
    what the unit says next), and the controls after everything."""
    out = []
    rates_voice = [v for v in voices if v[0] not in NATIVE_ONLY][-1][0]
    for group, s in (("", DEFAULTS), ("dialog settings", DIALOG), ("11025 Hz", dict(DEFAULTS, SampleRate=11025)),
                     ("44100 Hz", dict(DEFAULTS, SampleRate=44100))):
        for vid, _name, lang in voices:
            texts = TEXT_ES if lang == "es" else TEXT_EN
            if group.endswith("Hz") and vid != rates_voice:
                continue                     # the rates: one voice shows the DLL declares and renders them (every
                                             # voice at every rate is test_native.py's, through the same mapping)
            if group:
                out.append(("%s %s" % (vid, group), vid, 0, 0, s, "-", 0, texts[-1], None))
                continue
            out += [("%s default" % vid, vid, 0, 0, s, "-", 0, texts[0], None),
                    ("%s rate 5 pitch -4" % vid, vid, 5, -4, s, "-", 0, texts[-1], None),
                    ("%s rate -6 pitch 10" % vid, vid, -6, 10, s, "-", 0, texts[0], None),
                    ("%s bookmark" % vid, vid, 0, 2, s, "-", 0, texts[0], "and the rest.")]
        if not group:
            for vid, _name, _lang in voices:
                out += [("%s abort" % vid, vid, 0, 0, s, "-", ABORT_AFTER, LONG, None),
                        ("%s after the abort" % vid, vid, 0, 0, s, "-", 0, "OK button", None)]
    for tag, s in NUMBERS:
        for vid, _name, lang in voices:
            if vid.startswith("blazie:"):
                out.append(("%s %s" % (vid, tag), vid, 0, 0, s, "-", 0, NUMBERS_TEXT[lang], None))
    for vid, _name, lang in voices:                                 # the controls
        texts = TEXT_ES if lang == "es" else TEXT_EN
        if vid.startswith("blazie:") or vid in NATIVE_ONLY:
            out.append(("%s CONTROL voice" % vid, vid, 0, 0, DEFAULTS, "voice", 0, texts[0], None))
        if vid.startswith("blazie:"):
            out.append(("%s CONTROL numbers ignored" % vid, vid, 0, 0, NUMBERS[2][1], "bl-numbers", 0,
                        NUMBERS_TEXT[lang], None))
        # the dialog has nothing for the Speak-Out or the Mockingboard: dropping it changes nothing
        if not vid.startswith("speakout:") and vid not in NATIVE_ONLY:
            out.append(("%s CONTROL setting" % vid, vid, 0, 0, DIALOG, "setting", 0, texts[-1], None))
    return out


# ---- the reference: the Python server with the same settings (NATIVE_ONLY: the native serve host) ------------------
def reference(opts, requests, env, native=False):
    cmd = [SERVE, "--serve", "--firmware", os.path.join(STAGE, "firmware")] if native else \
        [sys.executable, os.path.join(HERE, "ssi_serve.py"), "--serve"]
    p = subprocess.Popen(cmd + opts, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                         env=None if native else env)

    def exact(n):
        b = b""
        while len(b) < n:
            c = p.stdout.read(n - len(b))
            if not c:
                raise EOFError("the reference server closed the pipe")
            b += c
        return b
    out = []
    try:
        for seq, (voice, text, rate, pitch) in enumerate(requests, 1):
            v, t = voice.encode("utf-8"), text.encode("utf-8")
            p.stdin.write(struct.pack("<IIiiiII", REQ, seq, rate, pitch, 100, len(v), len(t)) + v + t)
            p.stdin.flush()
            _magic, status = struct.unpack("<Ii", exact(8))
            pcm = bytearray()
            while not status:
                (frames,) = struct.unpack("<I", exact(4))
                if not frames:
                    break
                pcm += exact(frames * 2)
            out.append(bytes(pcm))
    finally:
        p.stdin.close()
        p.wait(timeout=60)
    return out


def opts_for(s):
    return ["--inflection", str(s["Inflection"]), "--whine", ("off", "hiss", "whine")[s["Whine"]],
            "--accent-inflection", str(s["AccentInflection"]), "--run-ahead", str(s["RunAhead"]),
            "--rate", str(s["SampleRate"])] + \
        ([] if s["BrailleLiteNumbers"] is None else ["--bl-numbers", str(s["BrailleLiteNumbers"])])


def numbers_run(label):
    """The NUMBERS group a case belongs to ("numbers on" ...), or None."""
    return next((tag for tag, _s in NUMBERS if label.endswith(" " + tag)), None)


def voiced(pcm):
    import array
    a = array.array("h", pcm)
    return len(a) and (sum(x * x for x in a[::4]) / max(1, len(a[::4]))) ** 0.5 > 300


def delete_tree(path):
    try:
        k = winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_ALL_ACCESS)
    except OSError:
        return
    while True:
        try:
            sub = winreg.EnumKey(k, 0)
        except OSError:
            break
        delete_tree(path + "\\" + sub)
    winreg.CloseKey(k)
    winreg.DeleteKey(winreg.HKEY_CURRENT_USER, path)


def harness(arch, todo, tmp, name):
    """One harness process (a fresh engine) speaking todo in order: {index in todo: its result}."""
    key = r"Software\SSI-263 SAPI (development)\test-%d-%s" % (os.getpid(), name)
    job = os.path.join(tmp, "job_%s.txt" % name)
    out = os.path.join(tmp, name)
    os.makedirs(out, exist_ok=True)
    with open(job, "w", encoding="utf-8", newline="\n") as f:
        for _label, vid, rate, pitch, s, brk, abort, t1, t2 in todo:
            f.write("\t".join([vid, str(rate), str(pitch), ",".join("-" if s[k] is None else str(s[k]) for k in KEYS),
                               brk, str(abort), t1]
                              + ([t2] if t2 else [])) + "\n")
    try:
        r = subprocess.run([os.path.join(STAGE, arch, "sapi_harness.exe"), os.path.join(STAGE, arch, "ssi263_sapi.dll"),
                            job, out], capture_output=True, text=True, timeout=600,
                           env=dict(os.environ, SSI263_SAPI_SETTINGS_KEY=key))
    finally:
        delete_tree(key)
    got = {}
    for ln in r.stdout.splitlines():
        f = dict(x.split("=", 1) for x in ln.split()[2:] if "=" in x)
        if ln.startswith("case "):
            k = int(ln.split()[1])
            got[k] = (int(f["hr"], 16), int(f["rate"]), int(f["writes"]), f["marks"], int(f["ms"]),
                      open(os.path.join(out, "case_%d.pcm" % k), "rb").read())
    if len(got) != len(todo):
        sys.exit("FAILED: the %s harness ran %d of %d cases: %s" % (arch, len(got), len(todo), (r.stdout + r.stderr)[-1500:]))
    return got


def main():
    if not os.path.isfile(SERVE):
        sys.exit("FAILED: %s is missing: python src/csrc/build_ssi263speech.py" % os.path.relpath(SERVE, REPO))
    for arch in ARCHES:
        for f in ("ssi263_sapi.dll", "sapi_harness.exe", "ssi263speech.dll"):
            if not os.path.isfile(os.path.join(STAGE, arch, f)):
                sys.exit("FAILED: %s is missing: powershell -File sapi\\build.ps1 -Dev" % os.path.join(STAGE, arch, f))
    # the reference is 0.7.0's Python drivers, never nvda/dist's native ones: one environment for every reference
    # server (not the harness), checked first (reference_drivers.py; SSI263_SAPI_REF_BREAK=dist must fail here)
    ref_env = reference_drivers.env()
    reference_drivers.guard(sys.executable, os.path.join(HERE, "ssi_serve.py"), ref_env, "sapi engine")
    voices = [ln.split("\t") for ln in open(os.path.join(STAGE, "voices.txt"), encoding="utf-8").read().splitlines()
              if "\t" in ln]
    todo = cases(voices)
    # the harness runs: every arch speaks the cases in order in one engine; each control alone in a fresh one (x64),
    # against a fresh Python server, so what differs is the control's doing and nothing else
    main_idx = [k for k, c in enumerate(todo) if "CONTROL" not in c[0] and not numbers_run(c[0])]
    runs = [(a, a, main_idx) for a in ARCHES] + [("x64", "control%d" % k, [k]) for k, c in enumerate(todo)
                                                 if "CONTROL" in c[0]]
    runs += [(a, "%s-%s" % (a, tag.replace(" ", "-")), [k for k, c in enumerate(todo) if numbers_run(c[0]) == tag])
             for a in ARCHES for tag, _s in NUMBERS]
    groups = {}                         # one Python server per settings, or per control; NATIVE_ONLY: a native host
    for k, (label, vid, rate, pitch, s, brk, abort, t1, t2) in enumerate(todo):
        text = t1 + " " + t2 if t2 else t1                          # the engine joins the fragments with a space
        g = json.dumps(s, sort_keys=True) + (label if "CONTROL" in label else "") + (numbers_run(label) or "") + \
            (" native" if vid in NATIVE_ONLY else "")
        groups.setdefault(g, []).append((k, (vid, text, (rate + 10) * 5, 50 + pitch * 5)))
    tmp = tempfile.mkdtemp(prefix="ssi263_sapi_engine_")
    bad = 0
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            refs_f = {g: pool.submit(reference, opts_for(todo[items[0][0]][4]), [q for _k, q in items], ref_env,
                                     g.endswith(" native"))
                      for g, items in groups.items()}
            got_f = {name: pool.submit(harness, a, [todo[k] for k in idx], tmp, name) for a, name, idx in runs}
            refs = {}
            for g, items in groups.items():
                for (k, _q), pcm in zip(items, refs_f[g].result()):
                    refs[k] = pcm
            results = {}
            for a, name, idx in runs:
                for j, k in enumerate(idx):
                    results[a, k] = got_f[name].result()[j]
            for a in ARCHES:
                lines, n = [], 0
                for k, (label, vid, rate, pitch, s, brk, abort, t1, t2) in enumerate(todo):
                    if (a, k) not in results:
                        continue
                    hr, declared, writes, marks, ms, data = results[a, k]
                    sr = s["SampleRate"]
                    pad = b"\0" * ((sr * 3 // 20) * 2)
                    want = refs[k] + pad
                    server = "the native host" if vid in NATIVE_ONLY else "the Python server"
                    if abort:
                        ok = hr == 0 and writes == abort and marks == "-" and not data.endswith(pad) and ms < 3000
                        what = "aborted after %d writes: S_OK, %d writes, events %s, %d ms" % (abort, writes, marks, ms)
                    elif label.endswith("after the abort"):
                        alone = refs[k]
                        ok = hr == 0 and voiced(data) and abs(len(data) - len(want)) <= 0.1 * len(want)
                        what = "%.2f s (alone %.2f s), voiced" % (len(data) / 2 / sr, len(alone) / 2 / sr)
                    elif "CONTROL" in label:
                        ok = data != want
                        what = "differs from %s, as it must" % server if ok else "the same: NOT caught"
                    else:
                        ok = hr == 0 and declared == sr and data == want and (marks == "7" if t2 else marks == "-")
                        what = ("byte-identical to " + server if data == want else
                                "DIFFERS at sample %d (%d against %d)" % (
                                    next((i for i in range(0, min(len(data), len(want)), 2)
                                          if data[i:i + 2] != want[i:i + 2]), min(len(data), len(want))) // 2,
                                    len(data) // 2, len(want) // 2)) + \
                            ", %d Hz declared" % declared + (", bookmark %s" % marks if t2 else "") + ", %d ms" % ms
                    n += 1
                    bad += not ok
                    lines.append("%-4s %s %-38s %s" % ("ok" if ok else "FAIL", a, label, what))
                # across the numbers runs: no value = 1, and 0 changes the audio (each is the Python server's above)
                for vid, _name, _lang in voices:
                    if not vid.startswith("blazie:"):
                        continue
                    pcm = {tag: results[a, next(k for k, c in enumerate(todo) if c[0] == "%s %s" % (vid, tag))][5]
                           for tag, _s in NUMBERS}
                    for what, ok in (("no value = 1", pcm["numbers unset"] == pcm["numbers on"]),
                                     ("0 differs from 1", pcm["numbers off"] != pcm["numbers on"])):
                        n += 1
                        bad += not ok
                        lines.append("%-4s %s %-38s %s" % ("ok" if ok else "FAIL", a, "%s numbers" % vid,
                                                           "BrailleLiteNumbers: " + what))
                for ln in lines:
                    if ln.startswith("FAIL") or "--verbose" in ARGS:
                        print(ln)
                print("sapi engine %s: %d of %d cases right (%d through SAPI's interface byte-identical to the Python "
                      "server, %d to the native host, controls caught)" % (
                          a, sum(not ln.startswith("FAIL") for ln in lines), n,
                          sum("byte-identical to the Python server" in ln for ln in lines),
                          sum("byte-identical to the native host" in ln for ln in lines)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
        try:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, r"Software\SSI-263 SAPI (development)")
        except OSError:
            pass                                                     # another run's key still there, or none
    print("sapi engine: %s" % ("ok" if not bad else "%d FAILED" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
