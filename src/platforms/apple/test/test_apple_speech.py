"""The Apple front end's own C, proven on the Mac: test_apple_speech.c linked against SSI263Core's macos-arm64 slice.

- VoiceOver's SSML (ssp_ssml): the segments and their pauses at each Pause mode, as TGSpeechBox's speech extension
  makes them (iOS 27's <s> parts, an 800.0ms break folding into the sentence end before it, strengths, "0.8s",
  the 30 ms floor, the 2 s cap, the end-of-request pause, a break-only spacer), the entities, the prosody: VoiceOver's
  rate="249.99995%", the volume rotor's decibels (volume="-6.0206003dB" is 50 %), pitch changes.
- A request spoken (ssp_speech), for every voice whose files are there (the Braille Lite, English and Spanish; the
  Speak-Out; the Accent SA; the Accent-mini): byte for byte the segments through ssa_start/ssa_pull directly with the
  pauses' zeros between them -- the voice's own PCM, which test_android_native.py and test_apple_core.py hold to the
  desktop -- and a stop: nothing after it (-2, and again -2), and the next request clean.
- Aicom's files (ssp_import): each known by its sha256 under any name, one changed byte not, and the Braille Lite's
  and the Speak-Out's still judged as Android judges them.

    sh src/platforms/apple/build_apple.sh macos            first
    python src/platforms/apple/test/test_apple_speech.py
    SSI263_APPLE_SPEECH_BREAK=ssml-1|ssml-2|speech-1|speech-2|import python ...     the controls: each must FAIL

Options: --firmware <folder> (default $SSI263_FIRMWARE, else firmware/blazie), --aicom <folder> (default
firmware/aicom-accent-sa).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "src", "platforms", "android", "test"))
import test_android_native as tan  # noqa: E402

LIB = os.path.join(REPO, "build", "apple", "slices", "macos-arm64", "libssi263core.a")
CORE = os.path.join(REPO, "src", "platforms", "apple", "core")
OUT = os.path.join(REPO, "build", "apple", "test")
OFF, SHORT, LONG = 0, 1, 2

IOS27 = ('<speak><prosody rate="249.99995%" volume="+0.0dB"><s><lang xml:lang="en-US">Messages</lang></s>'
         '<s><lang xml:lang="en-US">Active</lang></s><s><break time="800.0ms"/></s>'
         '<s><lang xml:lang="en-US">Swipe up with three fingers to close the app</lang></s>'
         '<s><lang xml:lang="en-US">Actions available</lang></s></prosody></speak>')
# (name, pause mode, SSML, (rate, pitch, volume), [(text, pause ms), ...])
PARSE = [
    ("ios27-short", SHORT, IOS27, (250, 100, 1.0),
     [("Messages", 150), ("Active", 400), ("Swipe up with three fingers to close the app", 150),
      ("Actions available", 150)]),
    ("ios27-long", LONG, IOS27, (250, 100, 1.0),
     [("Messages", 300), ("Active", 800), ("Swipe up with three fingers to close the app", 300),
      ("Actions available", 300)]),
    ("ios27-off", OFF, IOS27, (250, 100, 1.0),
     [("Messages", 0), ("Active", 0), ("Swipe up with three fingers to close the app", 0), ("Actions available", 0)]),
    ("strengths-short", SHORT, 'Hello<break strength="weak"/>world<break strength="x-strong"/>again', (100, 100, 1.0),
     [("Hello", 100), ("world", 400), ("again", 150)]),
    ("strengths-long", LONG, 'Hello<break strength="weak"/>world<break strength="x-strong"/>again', (100, 100, 1.0),
     [("Hello", 200), ("world", 800), ("again", 300)]),
    ("medium-default", LONG, "One<break/>two", (100, 100, 1.0), [("One", 350), ("two", 300)]),
    ("seconds", SHORT, "A<break time='0.8s'/>B", (100, 100, 1.0), [("A", 400), ("B", 150)]),
    ("floor", SHORT, 'A<break time="40ms"/>B<break time="20ms"/>C', (100, 100, 1.0), [("A", 30), ("B", 20), ("C", 150)]),
    ("cap", LONG, 'A<break time="5000ms"/>B', (100, 100, 1.0), [("A", 2000), ("B", 300)]),
    ("none", LONG, 'A<break strength="none"/>B', (100, 100, 1.0), [("A", 0), ("B", 300)]),
    ("spacer", LONG, '<speak><break time="500ms"/></speak>', (100, 100, 1.0), [("", 500)]),
    ("plain", SHORT, "Hello there", (100, 100, 1.0), [("Hello there", 150)]),
    ("empty", SHORT, "", (100, 100, 1.0), [("", 0)]),
    ("entities", OFF, "Tom &amp; Jerry &lt;3 &#233;t&#xE9; &quot;q&quot; &apos;a&apos; &bogus; & x",
     (100, 100, 1.0), [("Tom & Jerry <3 été \"q\" 'a' &bogus; & x", 0)]),
    ("tags-space", OFF, "Hello<emphasis>world</emphasis>  and\n<sub alias='x'>more</sub>", (100, 100, 1.0),
     [("Hello world and more", 0)]),
    ("rotor-50", OFF, '<prosody volume="-6.0206003dB">x</prosody>', (100, 100, 0.5), [("x", 0)]),
    ("rotor-loud", OFF, '<prosody volume="+10dB">x</prosody>', (100, 100, 2.0), [("x", 0)]),
    ("volume-words", OFF, '<prosody volume="soft">x</prosody>', (100, 100, 0.5), [("x", 0)]),
    ("volume-pct", OFF, '<prosody volume="30%">x</prosody>', (100, 100, 0.3), [("x", 0)]),
    ("rate-word", OFF, '<prosody rate="fast">x</prosody>', (150, 100, 1.0), [("x", 0)]),
    ("rate-mult", OFF, '<prosody rate="1.5">x</prosody>', (150, 100, 1.0), [("x", 0)]),
    ("rate-slow", OFF, '<prosody rate="x-slow">x</prosody>', (50, 100, 1.0), [("x", 0)]),
    ("pitch-up", OFF, '<prosody pitch="+20%">x</prosody>', (100, 120, 1.0), [("x", 0)]),
    ("pitch-down", OFF, '<prosody pitch="-25%">x</prosody>', (100, 75, 1.0), [("x", 0)]),
    ("pitch-word", OFF, '<prosody pitch="high">x</prosody>', (100, 115, 1.0), [("x", 0)]),
    ("pitch-hz", OFF, '<prosody pitch="200Hz">x</prosody>', (100, 100, 1.0), [("x", 0)]),
    ("first-prosody", OFF, '<prosody rate="slow"><prosody rate="fast">x</prosody></prosody>', (75, 100, 1.0),
     [("x", 0)]),
    ("unclosed", OFF, "a < b and c", (100, 100, 1.0), [("a < b and c", 0)]),
    ("p-ends", LONG, "<p>One.</p><p>Two.</p>", (100, 100, 1.0), [("One.", 300), ("Two.", 300)]),
]


def build():
    if not os.path.isfile(LIB):
        sys.exit("no %s: run sh src/platforms/apple/build_apple.sh macos first" % os.path.relpath(LIB, REPO))
    os.makedirs(OUT, exist_ok=True)
    exe = os.path.join(OUT, "test_apple_speech")
    subprocess.run(["xcrun", "clang", "-O2", "-std=c99", "-ffp-contract=off", "-Wall", "-Wextra",
                    "-Wno-unused-parameter", "-I" + tan.SRC, "-I" + tan.CPP, "-I" + CORE, "-o", exe + ".o", "-c",
                    os.path.join(HERE, "test_apple_speech.c")], check=True)
    subprocess.run(["xcrun", "clang++", "-o", exe, exe + ".o", LIB, "-lm"], check=True)
    os.remove(exe + ".o")
    return exe


def check_parse(exe, env):
    stdin = "".join("%d %s\n" % (mode, ssml.encode("utf-8").hex()) for _, mode, ssml, _, _ in PARSE)
    out = subprocess.run([exe, "--parse"], input=stdin, capture_output=True, text=True, env=env, check=True).stdout
    got, cur = [], None
    for line in out.splitlines():
        p = line.split()
        if p[0] == "request":
            cur = ((int(p[1]), int(p[2]), round(float(p[3]), 3)), [])
        elif p[0] == "seg":
            cur[1].append(("" if p[1] == "-" else bytes.fromhex(p[1]).decode("utf-8"), int(p[2])))
        elif p[0] == "end":
            got.append(cur)
    bad = 0
    for (name, _, _, prosody, segs), g in zip(PARSE, got):
        want = ((prosody[0], prosody[1], round(prosody[2], 3)), segs)
        ok = g == want
        if not ok:
            print("FAIL  parse  %s: %r, want %r" % (name, g, want))
        bad += not ok
    if len(got) != len(PARSE):
        print("FAIL  parse  %d of %d requests parsed" % (len(got), len(PARSE)))
        bad += 1
    print("%-5s parse  %d of %d requests as TGSpeechBox's rules make them" % ("ok" if not bad else "FAIL",
                                                                          len(PARSE) - bad, len(PARSE)))
    return bad


def check_speak(exe, data, aicom, env):
    args = [exe, "--speak", data, aicom] + ([tan.MINI_DVC] if os.path.isfile(tan.MINI_DVC) else [])
    out = subprocess.run(args, capture_output=True, text=True, env=env)
    if out.returncode:
        print("FAIL  speak  the program failed (%d): %s" % (out.returncode, out.stderr.strip()[-500:]))
        return 1
    bad = cases = 0
    rows = {}
    for line in out.stdout.splitlines():
        p = line.split()
        if p[0] == "skip":
            print("skip  speak  %s: its files are not there" % p[1])
        elif p[0] == "case":
            name, n, h, wn, wh, after, errors = p[1], int(p[2]), p[3], int(p[4]), p[5], int(p[6]), int(p[7])
            rows[name] = (n, h)
            cases += 1
            ok = (n, h) == (wn, wh) and n > 0 and errors == 0
            if name.endswith("-stopped"):
                ok = ok and after == -2
            if not ok:
                print("FAIL  speak  %s: %d samples %s, reference %d %s, after the stop %d, errors %d" % (
                    name, n, h, wn, wh, after, errors))
            bad += not ok
    # the pauses are heard: the same request at off, short (phrases' 100 %) and long differ in length
    for v in sorted({k.split("-")[0] for k in rows}):
        off, short, lng = (rows.get(v + "-" + c, (0,))[0] for c in ("off", "phrases", "long"))
        ok = off < short < lng
        if not ok:
            print("FAIL  speak  %s: off %d, short %d, long %d samples: the pauses not heard" % (v, off, short, lng))
        bad += not ok
    print("%-5s speak  %d requests on %d voices: the segments' own PCM with the pauses between, a stop that holds" % (
        "ok" if not bad else "FAIL", cases, len({k.split("-")[0] for k in rows})))
    return bad


def check_import(exe, data, tmp, env):
    out = os.path.join(tmp, "imported.bin")
    cases = []
    for name in ("u2.BIN", "u3.BIN", "u4.BIN"):
        cases.append(("Aicom %s" % name, os.path.join(tan.AICOM, name), {"u2.BIN": 10, "u3.BIN": 11, "u4.BIN": 12}[name]))
    if os.path.isfile(tan.MINI_DVC):
        cases.append(("Aicom SPKEMS.DVC", tan.MINI_DVC, 13))
    renamed = os.path.join(tmp, "renamed.dat")
    shutil.copyfile(os.path.join(tan.AICOM, "u3.BIN"), renamed)
    cases.append(("u3.BIN renamed", renamed, 11))
    flipped = os.path.join(tmp, "flipped.BIN")
    with open(os.path.join(tan.AICOM, "u2.BIN"), "rb") as f:
        b = bytearray(f.read())
    b[1000] ^= 1
    with open(flipped, "wb") as f:
        f.write(b)
    cases.append(("u2.BIN with one byte changed", flipped, -1))
    if os.path.isfile(os.path.join(data, "BL2ENG.BNS")):
        cases.append(("BL2ENG.BNS (the Braille Lite's)", os.path.join(data, "BL2ENG.BNS"), 0))
    if os.path.isfile(tan.SPEAKOUT_HEX):
        cases.append(("SPEAKOUT.HEX (the Speak-Out's)", tan.SPEAKOUT_HEX, 3))
    bad = 0
    for label, path, want in cases:
        r = subprocess.run([exe, "--import", path, out], capture_output=True, text=True, env=env).stdout.strip()
        got = int(r.split()[0]) if r and r.split()[0].lstrip("-").isdigit() else None
        same = want < 0 or (os.path.isfile(out) and open(out, "rb").read() == open(path, "rb").read()
                            if want >= 10 else True)
        ok = got == want and same
        print("%-5s import %s: %s" % ("ok" if ok else "FAIL", label, r))
        bad += not ok
        if os.path.exists(out):
            os.remove(out)
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--firmware", default=os.environ.get("SSI263_FIRMWARE") or os.path.join(REPO, "firmware", "blazie"))
    ap.add_argument("--aicom", default=tan.AICOM)
    a = ap.parse_args()
    env = dict(os.environ)
    exe = build()
    tmp = tempfile.mkdtemp(prefix="ssi263-apple-speech-")
    try:
        data = tan.data_folder(a.firmware, tmp)
        bad = check_parse(exe, env) + check_speak(exe, data, a.aicom, env) + check_import(exe, data, tmp, env)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("FAILED: %d" % bad if bad else "the Apple front end's C speaks as the voices do, with VoiceOver's pauses")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
