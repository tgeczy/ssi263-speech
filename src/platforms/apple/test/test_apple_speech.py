"""The Apple front end's own C, proven on the Mac: test_apple_speech.c linked against SSI263Core's macos-arm64 slice.

- VoiceOver's SSML (ssp_ssml): the segments and their pauses at each Pause mode, as TGSpeechBox's speech extension
  makes them (iOS 27's <s> parts, an 800.0ms break folding into the sentence end before it, strengths, "0.8s",
  the 30 ms floor, the 2 s cap, the end-of-request pause, a break-only spacer), the entities, the prosody: VoiceOver's
  rate="249.99995%", the volume rotor's decibels (volume="-6.0206003dB" is 50 %), pitch changes.
- A request spoken (ssp_speech), for every voice whose files are there (the Braille Lite, English and Spanish; the
  Speak-Out; the Accent-PC; the Accent-mini): byte for byte the segments through ssa_start/ssa_pull directly with the
  pauses' zeros between them -- the voice's own PCM, which test_android_native.py and test_apple_core.py hold to the
  desktop -- and a stop: nothing after it (-2, and again -2), and the next request clean.
- Aicom's files (ssp_import): each known by its sha256 under any name, one changed byte not, and the Braille Lite's,
  the Speak-Out's and the Mockingboard's still judged as Android judges them -- the Mockingboard's file, and the
  toolkit's disk image (MOCKINGBOARD_DISKS in paths.local) in DOS and ProDOS order, a blank disk and another disk;
  a Braille Lite 2000 release with no shipped voice (a release voice's, and one no voice speaks) as SSP_FW_RELEASE
  plus its row (import-2 drops them).
- The Artic SynPhonix (0.9.5): the repository's VEST.COM staged into the TEST data folder only (data_folder; the
  Apple apps never carry it), speaking after the SmarTalk; VEST.COM known by its content (SSA_SYNPHONIX, 29), a copy
  with another card's serial as well, and written as it came (synphonix drops the imported copy, synphonix-import
  never knows it).
- Speech Systems' Super Voice (0.9.5), when firmware/speech-systems-super-voice/SUPERTOK.BIN is there (never in the
  repository): staged into the TEST data folder only, speaking after the SynPhonix (supervoice stages a copy with one
  byte changed, which the host refuses, so it must FAIL); its program known from itself, from a disk laid out around it
  (tools/check_sv_voice.py make_disk) and from the real SUPERV.DSK when it is there, the program alone written; a
  changed program not (supervoice-import never knows it).
- The release voices (release_voices.py: Hungarian, Turkish, ... whose firmware is in the firmware folder, their
  states made once and checked against the table): each one's requests as every voice's, and each one speaks.

    sh src/platforms/apple/build_apple.sh macos            first
    python src/platforms/apple/test/test_apple_speech.py
    SSI263_APPLE_SPEECH_BREAK=ssml-1|ssml-2|ssml-3|speech-1|speech-2|import|import-2|synphonix|synphonix-import|
        supervoice|supervoice-import python ...                                                 the controls:
                                                                                                each must FAIL

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
import release_voices  # noqa: E402

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
# Try it's and Export to WAV's typed text (ssp_parse_text, #59): taken as it is, never as SSML -- no tag dropped, no
# entity decoded -- with its white space collapsed and the request's end pause, as plain text through ssp_parse.
PARSE_TEXT = [
    ("text-angles", SHORT, "Press <Enter> to continue", (100, 100, 1.0), [("Press <Enter> to continue", 150)]),
    ("text-compare", OFF, "if a < b and c > d", (100, 100, 1.0), [("if a < b and c > d", 0)]),
    ("text-tag-only", SHORT, "<hello>", (100, 100, 1.0), [("<hello>", 150)]),
    ("text-entities", OFF, "&lt; &amp; &#65; &nbsp;", (100, 100, 1.0), [("&lt; &amp; &#65; &nbsp;", 0)]),
    ("text-ssml", SHORT, 'A<break time="500ms"/>B <prosody rate="fast">C</prosody>', (100, 100, 1.0),
     [('A<break time="500ms"/>B <prosody rate="fast">C</prosody>', 150)]),
    ("text-space", OFF, "Hello\n\n  there", (100, 100, 1.0), [("Hello there", 0)]),
]


SSP_FW_RELEASE = 100                    # ssp_import.h's


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
    stdin = "".join("%d %s\n" % (mode, ssml.encode("utf-8").hex()) for _, mode, ssml, _, _ in PARSE) + \
        "".join("text %d %s\n" % (mode, text.encode("utf-8").hex()) for _, mode, text, _, _ in PARSE_TEXT)
    cases = PARSE + PARSE_TEXT
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
    for (name, _, _, prosody, segs), g in zip(cases, got):
        want = ((prosody[0], prosody[1], round(prosody[2], 3)), segs)
        ok = g == want
        if not ok:
            print("FAIL  parse  %s: %r, want %r" % (name, g, want))
        bad += not ok
    if len(got) != len(cases):
        print("FAIL  parse  %d of %d requests parsed" % (len(got), len(cases)))
        bad += 1
    print("%-5s parse  %d of %d requests as TGSpeechBox's rules make them, typed text as it is" % (
        "ok" if not bad else "FAIL", len(cases) - bad, len(cases)))
    return bad


def check_speak(exe, data, aicom, env, releases=()):
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
            rows[name] = (n, h, errors)
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
    # every release voice whose files are there is offered, and speaks
    mute = [key for key, _ in releases if rows.get(key + "-phrases", (0,))[0] == 0]
    if releases:
        print("%-5s speak  the %d release voices each spoke%s" % ("ok" if not mute else "FAIL", len(releases),
                                                                 ": not " + ", ".join(mute) if mute else ""))
    bad += bool(mute)
    # the Sounding Board (voice 27, after the release voices) speaks when its two drivers are there
    if all(os.path.isfile(os.path.join(data, f)) for f in ("SBLOAD.COM", "SB.COM")):
        ok = rows.get("sb-phrases", (0,))[0] > 0
        print("%-5s speak  the Sounding Board spoke, after the release voices" % ("ok" if ok else "FAIL"))
        bad += not ok
    # the SmarTalk (voice 28, after the Sounding Board): its S_TALK.EXE is in the repository, staged by data_folder
    if os.path.isfile(os.path.join(data, "S_TALK.EXE")):
        ok = rows.get("st-phrases", (0,))[0] > 0
        print("%-5s speak  the SmarTalk spoke, after the Sounding Board" % ("ok" if ok else "FAIL"))
        bad += not ok
    def spoke(v):
        """The voice's own sound: its request with no pauses (off) made samples, and no request had an error."""
        return rows.get(v + "-off", (0,))[0] > 0 and all(r[2] == 0 for k, r in rows.items() if k.startswith(v + "-"))

    # the SynPhonix (voice 29, after the SmarTalk): the repository's VEST.COM, staged by data_folder into this test's
    # data folder alone -- never the app's
    if os.path.isfile(os.path.join(data, "VEST.COM")):
        ok = spoke("sp")
        print("%-5s speak  the SynPhonix spoke, after the SmarTalk" % ("ok" if ok else "FAIL"))
        bad += not ok
    # the Super Voice (voice 30, after the SynPhonix): the program staged by data_folder into this test's data folder
    # alone, when the person's copy is in the firmware folder
    if os.path.isfile(os.path.join(data, "SUPERTOK.BIN")):
        ok = spoke("sv")
        print("%-5s speak  the Super Voice spoke, after the SynPhonix" % ("ok" if ok else "FAIL"))
        bad += not ok
    else:
        print("skip  speak  the Super Voice: no firmware/speech-systems-super-voice/SUPERTOK.BIN")
    print("%-5s speak  %d requests on %d voices: the segments' own PCM with the pauses between, a stop that holds" % (
        "ok" if not bad else "FAIL", cases, len({k.split("-")[0] for k in rows})))
    return bad


def check_import(exe, data, tmp, env, firmware=None):
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
    # the SynPhonix's VEST.COM, known by its content alone: every copy holds its own card's serial, so another card's
    # copy is known the same, and both are written as they came
    if os.path.isfile(tan.SP_COM):
        com = open(tan.SP_COM, "rb").read()
        other = os.path.join(tmp, "VEST-other.COM")
        with open(other, "wb") as f:
            f.write(com.replace(b"S/N 999999", b"S/N 123456"))
        cases += [("VEST.COM (the SynPhonix's)", tan.SP_COM, 29), ("VEST.COM with another card's serial", other, 29)]
    # the Mockingboard's file, and the toolkit's disk image (MOCKINGBOARD_DISKS, paths.local; never in the repository)
    # in DOS and ProDOS order, through the Apple build's ssp_import_firmware (ssa_import_mockingboard behind it)
    if os.path.isfile(tan.MB_BIN):
        cases.append(("mockingboard-tts-1.1.bin (the Mockingboard's)", tan.MB_BIN, 5))
        renamed_mb = os.path.join(tmp, "speech.dat")
        shutil.copyfile(tan.MB_BIN, renamed_mb)
        cases.append(("the Mockingboard's file renamed", renamed_mb, 5))
    toolkit, disk1, other = tan.mb_disks()        # the toolkit (1.1), Mockingboard disk 1 (the early one), disk 2
    if toolkit:
        dsk = open(toolkit, "rb").read()
        po = os.path.join(tmp, "toolkit.po")
        with open(po, "wb") as f:
            f.write(tan.to_prodos(dsk))
        blank = os.path.join(tmp, "blank.dsk")
        with open(blank, "wb") as f:
            f.write(bytes(len(dsk)))
        cases += [("the toolkit's .dsk", toolkit, 5), ("the toolkit's .dsk in ProDOS order (.po)", po, 5),
                  ("a blank 140 KB disk", blank, -7)]
        if disk1:
            cases.append(("Mockingboard disk 1 (the early text-to-speech)", disk1, 6))
        if other:
            cases.append(("another Mockingboard disk (disk 2)", other, -7))
    else:
        print("skip  import the toolkit's disk image: MOCKINGBOARD_DISKS (paths.local) does not have it")
    # a release with no shipped voice: a release voice (its own row past SSP_FW_RELEASE), and one no voice speaks,
    # kept for the unit tab the same way
    if firmware:
        for k, r, bns in release_voices.voices(firmware)[:1] + [u for u in [release_voices.unit_only(firmware)] if u]:
            cases.append(("%s (%s)" % (os.path.basename(bns), r["id"]), bns, SSP_FW_RELEASE + k))
    bad = check_supervoice_import(exe, tmp, env)
    for label, path, want in cases:
        r = subprocess.run([exe, "--import", path, out], capture_output=True, text=True, env=env).stdout.strip()
        got = int(r.split()[0]) if r and r.split()[0].lstrip("-").isdigit() else None
        same = want < 0 or (os.path.isfile(out) and open(out, "rb").read() == open(path, "rb").read()
                            if 10 <= want < SSP_FW_RELEASE else True)
        ok = got == want and same
        print("%-5s import %s: %s" % ("ok" if ok else "FAIL", label, r))
        bad += not ok
        if os.path.exists(out):
            os.remove(out)
    return bad


def check_supervoice_import(exe, tmp, env):
    """The Super Voice's program known (SSA_SUPERVOICE, 30) from itself, from a disk laid out around it and from the
    real SUPERV.DSK, the program alone written each time; one byte changed, not (as test_android_native's)."""
    if not os.path.isfile(tan.SV_BIN):
        print("skip  import the Super Voice: no firmware/speech-systems-super-voice/SUPERTOK.BIN")
        return 0
    sys.path.insert(0, os.path.join(REPO, "tools"))
    from check_sv_voice import make_disk
    prog = open(tan.SV_BIN, "rb").read()
    cases = [("SUPERTOK.BIN", prog, 30), ("a disk laid out around it", make_disk(prog, [33, 34, 5, 60, 41]), 30),
             ("SUPERTOK.BIN with a byte changed", prog[:9] + bytes([prog[9] ^ 1]) + prog[10:], -1)]
    dsk = os.path.join(os.path.dirname(tan.SV_BIN), "SUPERV.DSK")
    if os.path.isfile(dsk):
        cases.append(("SUPERV.DSK, the real disk", open(dsk, "rb").read(), 30))
    bad = 0
    src, out = os.path.join(tmp, "sv.in"), os.path.join(tmp, "sv.out")
    for label, b, want in cases:
        with open(src, "wb") as f:
            f.write(b)
        if os.path.exists(out):
            os.remove(out)
        r = subprocess.run([exe, "--import", src, out], capture_output=True, text=True, env=env).stdout.strip()
        got = int(r.split()[0]) if r and r.split()[0].lstrip("-").isdigit() else None
        ok = got == want and (want < 0 or (os.path.isfile(out) and open(out, "rb").read() == prog))
        print("%-5s import the Super Voice, %s: %s" % ("ok" if ok else "FAIL", label, r))
        bad += not ok
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
        # the Sounding Board's two drivers (0.8.5), when they are in the firmware folder: voice 27 speaks too
        if all(os.path.isfile(os.path.join(tan.SB_DIR, f)) for f in ("SBLOAD.COM", "SB.COM")):
            for f in ("SBLOAD.COM", "SB.COM"):
                shutil.copy2(os.path.join(tan.SB_DIR, f), data)
        if os.environ.get("SSI263_APPLE_SPEECH_BREAK") == "supervoice" and \
                os.path.isfile(os.path.join(data, "SUPERTOK.BIN")):
            # the control: a changed program in the voice's place, which the host refuses, so it must not speak
            with open(os.path.join(data, "SUPERTOK.BIN"), "r+b") as f:
                f.seek(9)
                b = f.read(1)
                f.seek(9)
                f.write(bytes([b[0] ^ 1]))
        releases, bad = release_voices.add(data, a.firmware)
        bad += check_parse(exe, env) + check_speak(exe, data, a.aicom, env, releases) + \
            check_import(exe, data, tmp, env, a.firmware)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("FAILED: %d" % bad if bad else "the Apple front end's C speaks as the voices do, with VoiceOver's pauses")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
