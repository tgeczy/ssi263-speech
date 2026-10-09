"""The SAPI engine's native voices against the Python server they replace, byte for byte, over the same wire.

The reference is sapi/ssi_serve.py --serve, the server the SAPI engine ran up to 0.7.0: the three NVDA add-ons' own
drivers under NVDA stand-ins -- 0.7.0's Python drivers (nvda/tools/legacy_drivers.py), not nvda/dist's, which since
0.7.5 are the native ones; sapi/reference_drivers.py checks that first and fails the run if not.  The native side is ssi263_serve.exe (sapi/ssi_serve.c), which loads
ssi263speech.dll the way the SAPI DLL does and maps every setting with the DLL's own code (sapi/ssi_native.c) -- as
outspoken-nvda holds its osp_host to osp_serve.py.  Both get the same requests -- texts with numbers, money, currencies,
accents; SAPI's rates and pitches; the voices one after another, and back; the settings dialog's values on the command
line (--bl-numbers among them: with no value, 1 and 0, also held against each other); a cancel mid-utterance and the
utterance after it -- and every PCM byte must be equal.  Both widths of the native
library are held to the one reference (the x86 DLL computes as the x64 one: -msse2 -mfpmath=sse).  0.7.0's drivers
predate the accented-letter pass (src/csrc/translit.h): the reference is sent each text after it
(nvda/tools/translit_ref.py) -- current-preprocessing / frozen-downstream equivalence, not an oracle for the pass
(nvda/tools/translit_test.py's handwritten fixtures are).

A cancel lands where the client's pipe lets it (both hosts stop at the same block, as measured); if they ever stop at
different points, the cut audio must still agree as far as both go and the next utterance is checked as test_serve.py
checks it (voiced, as long as alone within 10 %), with the difference said.

Voices the native library does not have yet (the Speak-Out and the Accent-mini until their sources land) are named and
skipped; every voice it does have must match.

    python sapi/test_native.py [--arch x64|x86|both] [--only <session>]      default both, every session
    SSI263_SERVE_BREAK=setting     control: the native host drops the dialog's settings -- the "dialog" session FAILS
    SSI263_SERVE_BREAK=voice       control: the native host swaps English/Spanish and mini/SA -- FAILS
    SSI263_SERVE_BREAK=numbers     control: the native host turns the drivers' number words off -- the texts with
                                   digits FAIL, in English and in Spanish
    SSI263_SERVE_BREAK=bl-numbers  control: the native host ignores --bl-numbers (the dialog's BrailleLiteNumbers) --
                                   the "blnum0" session FAILS on both Braille Lite voices
    SSI263_SAPI_REF_BREAK=dist     control: the reference on nvda/dist's (native) drivers -- the reference check FAILS

Build first: python src/csrc/build_ssi263speech.py (and the add-ons' libraries, which legacy_drivers.py puts under
0.7.0's drivers for the reference: nvda/build_*.py).
"""
import concurrent.futures
import hashlib
import os
import struct
import subprocess
import sys
import threading

import reference_drivers

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "nvda", "tools"))
import translit_ref  # noqa: E402
FIRMWARE = os.path.join(REPO, "firmware")
REQ, RSP, CANCEL = 0x4F535034, 0x4F535052, 0x4F535043
ARGS = sys.argv[1:]
ARCHES = ["x64", "x86"]
if "--arch" in ARGS and ARGS[ARGS.index("--arch") + 1] != "both":
    ARCHES = [ARGS[ARGS.index("--arch") + 1]]
ONLY = ARGS[ARGS.index("--only") + 1] if "--only" in ARGS else None

LONG_EN = ("This sentence is long enough to be cancelled somewhere in the middle of it, surely. And then a few more "
           "words follow it.")
LONG_ES = "Esta frase es bastante larga para cortarla en algún lugar de la mitad, seguro. Y siguen más palabras."
TEXTS = {
    "en": ["Hello, how are you??",
           "Room 12, $3.50 and 1,234,567 items; the 21st of 3 at 9:45, -2.5 degrees.",
           "Pay $1,234,567,890,123.45 now, or £2.63, 5 € and 50¢.",
           "It’s “quoted” – café… OK button",
           "1,234,567"],
    "es": ["Mañana, ¿qué tal? Él está aquí.",
           "Son 1.234.567 personas, 3,5 metros y el 21 de 1999; -4,25 grados y 2.000.000.000.000 de euros.",
           "1.234.567"],
}
NUMBERS_REQS = [("blazie:blazie", 4, 50, 50), ("blazie:blazie_es", 2, 50, 50), ("accentmini:sa", 4, 50, 50)]

# A session: the server's command-line options (the settings dialog's values) and its requests, in order.  A request
# is (voice, text, rate, pitch) on the drivers' 0-100 (SAPI's rate -10..10 and pitch -10..10 land on multiples of
# five), or ("cancel", voice, long text, chunks): the long text cancelled after that many chunks, then "OK button".
SESSIONS = {
    "default": ([], [
        ("blazie:blazie", 0, 50, 50), ("blazie:blazie", 1, 50, 50), ("blazie:blazie", 2, 50, 50),
        ("blazie:blazie", 3, 75, 60), ("blazie:blazie_es", 0, 50, 50), ("blazie:blazie_es", 1, 35, 40),
        ("blazie:blazie", 0, 0, 100),
        ("accentmini:sa", 0, 50, 50), ("accentmini:sa", 1, 25, 70), ("accentmini:sa", 2, 100, 0),
        ("speakout:speakout", 0, 50, 50), ("speakout:speakout", 1, 70, 35), ("speakout:speakout", 3, 50, 50),
        ("accentmini:mini", 0, 50, 50), ("accentmini:mini", 1, 60, 45),
        ("accentmini:sa", 3, 50, 50), ("accentmini:mini", 3, 50, 50),       # one card: mini -> SA -> mini
        ("cancel", "blazie:blazie", LONG_EN, 12), ("cancel", "blazie:blazie_es", LONG_ES, 20),
        ("cancel", "accentmini:sa", LONG_EN, 15), ("cancel", "speakout:speakout", LONG_EN, 15),
        ("cancel", "accentmini:mini", LONG_EN, 15)]),
    "dialog": (["--inflection", "0", "--whine", "whine", "--accent-inflection", "50", "--run-ahead", "1"], [
        ("blazie:blazie", 0, 50, 50), ("blazie:blazie", 1, 60, 50), ("blazie:blazie_es", 0, 50, 50),
        ("accentmini:sa", 0, 50, 50), ("accentmini:mini", 0, 50, 50), ("speakout:speakout", 0, 50, 50),
        ("cancel", "blazie:blazie", LONG_EN, 12)]),
    "rate11": (["--rate", "11025", "--whine", "hiss"], [
        ("blazie:blazie", 0, 50, 50), ("blazie:blazie_es", 0, 50, 50), ("accentmini:sa", 0, 50, 50),
        ("speakout:speakout", 0, 50, 50), ("accentmini:mini", 0, 50, 50)]),
    "rate44": (["--rate", "44100", "--accent-inflection", "0"], [
        ("blazie:blazie", 1, 50, 50), ("accentmini:sa", 1, 50, 50), ("speakout:speakout", 0, 50, 50),
        ("accentmini:mini", 1, 50, 50)]),
    # the dialog's "Read numbers as words" (BrailleLiteNumbers): no value, 1 and 0, the same requests on fresh units --
    # each against the reference with the same setting, and across them (numbers_checks): no value is on, 0 changes
    # both Braille Lite voices' audio, and never the Accent's (its own driver's number words stay on)
    "blnum": ([], NUMBERS_REQS),
    "blnum1": (["--bl-numbers", "1"], NUMBERS_REQS),
    "blnum0": (["--bl-numbers", "0"], NUMBERS_REQS),
}


def lang_of(voice):
    return "es" if voice.endswith("_es") else "en"


class Client(object):
    def __init__(self, cmd, env=None):
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env)
        self.seq = 0

    def exact(self, n):
        buf = b""
        while len(buf) < n:
            c = self.p.stdout.read(n - len(buf))
            if not c:
                raise EOFError("the server closed the pipe")
            buf += c
        return buf

    def send(self, voice, text, rate=50, pitch=50, volume=100):
        self.seq += 1
        v, t = voice.encode("utf-8"), text.encode("utf-8")
        self.p.stdin.write(struct.pack("<IIiiiII", REQ, self.seq, rate, pitch, volume, len(v), len(t)) + v + t)
        self.p.stdin.flush()
        return self.seq

    def response(self, cut=None, seq=None):
        magic, status = struct.unpack("<Ii", self.exact(8))
        if magic != RSP:
            raise EOFError("desynced stream: %08X" % magic)
        if status:
            return status, b""
        pcm, chunks = bytearray(), 0
        while True:
            (frames,) = struct.unpack("<I", self.exact(4))
            if not frames:
                return status, bytes(pcm)
            if frames > 441000:
                raise EOFError("desynced stream: %d frames" % frames)
            pcm += self.exact(frames * 2)
            chunks += 1
            if cut and chunks == cut:
                self.p.stdin.write(struct.pack("<II", CANCEL, seq))
                self.p.stdin.flush()

    def close(self):
        try:
            self.p.stdin.close()
            self.p.wait(timeout=60)
        except Exception:
            self.p.kill()


def listing(cmd, env=None):
    out = subprocess.run(cmd + ["--list"], capture_output=True, timeout=180, env=env).stdout.decode("utf-8")
    return [ln.split("\t")[0] for ln in out.splitlines() if "\t" in ln]


def reference_text(voice, text):
    """0.7.0's drivers predate the accented-letter pass (src/csrc/translit.h): the reference is sent the text after
    it (nvda/tools/translit_ref.py, the same C), so everything after the pass is held byte for byte ("café" above)"""
    return translit_ref.translit(text, translit_ref.CP850 if voice.endswith("_es") else translit_ref.ASCII)


def play(cmd, requests, env=None, reference=False):
    """Every request's (status, pcm), in order; a cancel case gives two: the cut one and the one after."""
    c = Client(cmd, env)
    tx = reference_text if reference else (lambda voice, text: text)
    out = []
    try:
        for r in requests:
            if r[0] == "cancel":
                _k, voice, text, chunks = r
                seq = c.send(voice, tx(voice, text))
                out.append(c.response(chunks, seq))
                c.send(voice, "OK button")
                out.append(c.response())
            else:
                voice, ti, rate, pitch = r
                c.send(voice, tx(voice, TEXTS[lang_of(voice)][ti]), rate, pitch)
                out.append(c.response())
    finally:
        c.close()
    return out


def voiced(pcm):
    import array
    a = array.array("h", pcm)
    return len(a) and (sum(x * x for x in a[::4]) / max(1, len(a[::4]))) ** 0.5 > 300


def compare(session, arch, requests, ref, nat, rate):
    bad, lines, k = 0, [], 0
    for r in requests:
        parts = [("cut", r[1]), ("after", r[1])] if r[0] == "cancel" else [("", r[0])]
        label = ("cancel %s" % r[1]) if r[0] == "cancel" else "%s text %d rate %d pitch %d" % r
        cut_same = True
        for part, voice in parts:
            (sa, a), (sb, b) = ref[k], nat[k]
            k += 1
            what = "%s %s" % (label, part) if part else label
            if part == "cut" and len(a) != len(b):
                n = min(len(a), len(b))
                cut_same = False
                ok = sa == sb == 0 and a[:n] == b[:n]
                note = "the cancel landed %d samples apart; %s as far as both go" % (
                    (len(a) - len(b)) // 2, "the same audio" if ok else "OTHER audio")
            elif part == "after" and (not cut_same or a != b):
                # every voice is its device's firmware running, and it runs on until the cancel reaches it, so the
                # same cut audio does not mean the same unit after it: under a loaded machine the reference server
                # (Python) takes its cancel a little later in the unit's time (seen in the suite: the Braille Lite
                # 20202 vs 19862 samples, the Accent SA 20608 vs 20542).  The rule test_serve uses for a cancel: the
                # next utterance whole and voiced, within 10 %.
                ok = sb == 0 and voiced(b) and abs(len(a) - len(b)) <= 0.1 * len(a)
                note = "after a cancel at another point: %.2f s against %.2f s, voiced" % (len(b) / 2 / rate,
                                                                                         len(a) / 2 / rate)
            else:
                ok = sa == sb and a == b and (sa or voiced(a))
                note = "%d samples, sha1 %s" % (len(a) // 2, hashlib.sha1(a).hexdigest()[:10]) if a == b else \
                    "reference %d samples (status %d), native %d (status %d), first difference at sample %d" % (
                        len(a) // 2, sa, len(b) // 2, sb,
                        next((i for i in range(0, min(len(a), len(b)), 2) if a[i:i + 2] != b[i:i + 2]),
                             min(len(a), len(b))) // 2)
            bad += not ok
            lines.append("%-4s %-7s %-3s %s: %s" % ("same" if ok else "DIFF", session, arch, what, note))
    return bad, lines


def numbers_checks(side, unset, on, off, requests):
    """The blnum sessions against each other, on one side (the reference or a native width): no value = 1 for every
    voice (the default is on); 0 differs from 1 for the Braille Lite's voices and is the same for the Accent's."""
    bad, lines = 0, []
    for r, (su, u), (s1, a), (s0, b) in zip(requests, unset, on, off):
        voice = r[0]
        ok = su == s1 == 0 and u == a and voiced(a)
        bad += not ok
        lines.append("%-4s numbers %-3s %s: no value %s 1" % ("ok" if ok else "FAIL", side, voice,
                                                               "=" if u == a else "DIFFERS from"))
        blazie = voice.startswith("blazie:")
        ok = s0 == 0 and voiced(b) and (a != b if blazie else a == b)
        bad += not ok
        lines.append("%-4s numbers %-3s %s: 0 %s 1 (%.2f s against %.2f s)%s" % (
            "ok" if ok else "FAIL", side, voice, "differs from" if a != b else "the same as", len(b) / 2 / 22050,
            len(a) / 2 / 22050, "" if blazie else ", the Accent's own number words untouched"))
    return bad, lines


def main():
    ref_cmd = [sys.executable, os.path.join(HERE, "ssi_serve.py"), "--serve"]
    nat_cmd = {a: [os.path.join(REPO, "build", "win", a, "ssi263_serve.exe"), "--serve", "--firmware", FIRMWARE]
               for a in ARCHES}
    for a, cmd in nat_cmd.items():
        if not os.path.isfile(cmd[0]):
            sys.exit("FAILED: %s is missing: python src/csrc/build_ssi263speech.py" % os.path.relpath(cmd[0], REPO))
    # the reference is 0.7.0's Python drivers, never nvda/dist's native ones: one environment for every reference
    # server, checked first (reference_drivers.py; SSI263_SAPI_REF_BREAK=dist must fail here)
    ref_env = reference_drivers.env()
    reference_drivers.guard(sys.executable, os.path.join(HERE, "ssi_serve.py"), ref_env, "native")
    ref_voices = listing([sys.executable, os.path.join(HERE, "ssi_serve.py")], ref_env)
    nat_voices = {a: listing(nat_cmd[a][:1] + ["--firmware", FIRMWARE]) for a in ARCHES}
    common = [v for v in ref_voices if all(v in nat_voices[a] for a in ARCHES)]
    missing = [v for v in ref_voices if v not in common]
    print("voices: %s%s" % (", ".join(common), "; not in the native library yet: " + ", ".join(missing) if missing else ""))
    if not common:
        sys.exit("FAILED: no voice in common")
    jobs, results = [], {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
        for name, (opts, reqs) in SESSIONS.items():
            if ONLY and name != ONLY:
                continue
            reqs = [r for r in reqs if (r[1] if r[0] == "cancel" else r[0]) in common]
            jobs.append((name, opts, reqs))
            results[name, "ref"] = pool.submit(play, ref_cmd + opts, reqs, ref_env, True)
            for a in ARCHES:
                results[name, a] = pool.submit(play, nat_cmd[a] + opts, reqs)
        bad, total, nbad = 0, 0, 0
        for name, opts, reqs in jobs:
            rate = int(opts[opts.index("--rate") + 1]) if "--rate" in opts else 22050
            ref = results[name, "ref"].result()
            for a in ARCHES:
                b, lines = compare(name, a, reqs, ref, results[name, a].result(), rate)
                bad += b
                total += len(lines)
                for ln in lines:
                    if ln.startswith("DIFF") or "--verbose" in ARGS:
                        print(ln)
        names = [j[0] for j in jobs]
        if all(n in names for n in ("blnum", "blnum1", "blnum0")):
            reqs = next(j[2] for j in jobs if j[0] == "blnum")
            for side in ["ref"] + ARCHES:
                b, lines = numbers_checks(side, *[results[n, side].result() for n in ("blnum", "blnum1", "blnum0")],
                                          requests=reqs)
                nbad += b
                for ln in lines:
                    if ln.startswith("FAIL") or "--verbose" in ARGS:
                        print(ln)
                print("numbers %s: %d of %d right (no value = 1; 0 changes the Braille Lite, not the Accent)" % (
                    side, len(lines) - b, len(lines)))
    print("native: %d of %d utterances byte-identical to the Python server (%s)" % (total - bad, total,
                                                                                  ", ".join(ARCHES)))
    print("native: %s" % ("ok" if not bad + nbad else "%d FAILED" % (bad + nbad)))
    sys.exit(1 if bad + nbad else 0)


if __name__ == "__main__":
    main()
