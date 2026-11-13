"""What SAPI can ask of the engine beyond plain text, on the development DLL through sapi_harness.exe (its "v2" job
lines): how the site takes writes, the program's volume, a fragment's rate, volume and pitch, a <spell>, a skip
request, and when each bookmark's event arrives.  sapi/test_sapi_engine.py holds plain requests to the reference byte
for byte; here the engine is held to itself -- a request spoken one way must come out as the same request spoken the
other way -- so no reference server is needed.

The checks:
  - writes: a site that takes at most 1000 bytes of a Write, and one that refuses each block twice (a Write that
    takes nothing: Windows 7's full buffer), get the same audio as a site that takes everything;
  - the deadline: a site that takes 25 ms over each Write, with a deadline of one second, still gets the whole text
    (the deadline is for a unit that says nothing, not for a text that plays for a long time);
  - rate: a fragment's RateAdj (+4), the program's rate (+4), and +2 with +2, are the same audio, and not rate 0's;
  - volume: the program's volume 50 halves the samples; a fragment's Volume 50 is the same audio; 50 of 50 is 25;
  - pitch: a pitch change inside a request is three unit utterances -- the same audio as three requests;
  - JAWS's request (a word a fragment, a bookmark between every pair) is the joined text's audio, bookmarks in order;
  - spell: a <spell> fragment is each character as a text of its own (the same audio as one request a character), on
    the English and the Spanish unit, and not the word's audio;
  - skip: +1, 0, +2 and -1 are answered with that count and speech goes on from that sentence (as long as that text
    spoken alone, within 15 %); more sentences than there are, a move back from the first, and a kind that is not
    sentences are answered with what could be done and the request ends; an abort with a skip is an abort;
  - bookmarks: one before any text arrives before any audio; one between two unit utterances arrives between them
    with the bytes written as its offset; one at the end arrives after the audio and before the 150 ms of silence;
    JAWS's (inside one unit utterance) all arrive, in order -- after the audio, which is printed, not judged.

SSI263_SAPI_REQUESTS_BREAK=partial-write | deadline-wall | markup | spell | skip | marks-end puts one bug back in
every request (the DLL's SSI263_SAPI_TEST_BREAK): the must-fail controls, registered in nvda/tools/run_tests.py.

    python sapi/test_sapi_requests.py [--arch x64|x86|both] [--verbose]
"""
import array
import concurrent.futures
import os
import shutil
import subprocess
import sys
import tempfile
import winreg

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
STAGE = os.path.join(REPO, "nvda", "dist", "sapi-dev")
ARGS = sys.argv[1:]
ARCHES = ["x64", "x86"]
if "--arch" in ARGS and ARGS[ARGS.index("--arch") + 1] != "both":
    ARCHES = [ARGS[ARGS.index("--arch") + 1]]
BREAK = os.environ.get("SSI263_SAPI_REQUESTS_BREAK", "-") or "-"
RATE = 22050
PAD = b"\0" * ((RATE * 3 // 20) * 2)
SETTINGS = "1,0,100,0,%d,-" % RATE
EN, ES = "blazie:blazie", "blazie:blazie_es"
TEXT = "Hello, how are you today? I am fine."
THREE = ["One is first.", "Two is second.", "Three is third."]
DEADLINE_MS = 1000        # the slow site's request takes over two seconds: twice the deadline and more


def S(text, rate=0, volume=100, middle=0):
    return "S:%d:%d:%d:%s" % (rate, volume, middle, text)


def L(text):
    return "L:0:100:0:" + text


def B(name):
    return "B:%s" % name


def req(frags, voice=EN, rate=0, volume=100, writes="all", actions="-"):
    return "\t".join(["v2", voice, str(rate), str(volume), SETTINGS, BREAK, writes, actions] + list(frags))


# every process is a fresh engine (the units keep their state from one request to the next), so a request and the
# requests it is compared with each start from the same state
PROCS = {
    "plain": [req([S(TEXT)])],
    "max": [req([S(TEXT)], writes="max=1000")],
    "zero": [req([S(TEXT)], writes="zero=2")],
    "slow": [req([S(TEXT)], writes="slow=25")],
    "rate-site": [req([S(TEXT)], rate=4)],
    "rate-frag": [req([S(TEXT, rate=4)])],
    "rate-both": [req([S(TEXT, rate=2)], rate=2)],
    "vol-site": [req([S(TEXT)], volume=50)],
    "vol-frag": [req([S(TEXT, volume=50)])],
    "vol-both": [req([S(TEXT, volume=50)], volume=50)],
    "vol-25": [req([S(TEXT)], volume=25)],
    "pitch-mid": [req([S("Hello there,"), S("how are you", middle=8), S("today.")])],
    "pitch-sep": [req([S("Hello there,")]), req([S("how are you", middle=8)]), req([S("today.")])],
    "jaws": [req([S("Hello,"), B(1), S("how"), B(2), S("are"), B(3), S("you"), B(4), S("today?"), B(5)])],
    "jaws-joined": [req([S("Hello, how are you today?")])],
    "spell": [req([L("Hi 2")])],
    "spell-sep": [req([S("H")]), req([S("i")]), req([S("2")])],
    "spell-word": [req([S("Hi 2")])],
    "spell-es": [req([L("año")], voice=ES)],
    "spell-es-sep": [req([S("a")], voice=ES), req([S("ñ")], voice=ES), req([S("o")], voice=ES)],
    "three": [req([S(" ".join(THREE))])],
    "from-two": [req([S(" ".join(THREE[1:]))])],
    "from-three": [req([S(THREE[2])])],
    "skip+1": [req([S(" ".join(THREE))], actions="skip@3:1")],
    # 45 writes of 30 ms: in the second sentence (the first is over by 1.0 s, the second by 2.0 s)
    "skip-1": [req([S(" ".join(THREE))], actions="skip@45:-1")],
    "skip0": [req([S(" ".join(THREE))], actions="skip@3:0")],
    "skip+2": [req([S(" ".join(THREE))], actions="skip@3:2")],
    "skip+5": [req([S(" ".join(THREE))], actions="skip@3:5")],
    "skip-1-first": [req([S(" ".join(THREE))], actions="skip@3:-1")],
    "skip-kind": [req([S(" ".join(THREE))], actions="skip@3:1:x")],
    "skip-abort": [req([S(" ".join(THREE))], actions="abort@3,skip@3:1")],
    "mark-start": [req([B(1), S(TEXT)])],
    "mark-seam": [req([S("Hello there,"), B(2), S("how are you", middle=8)])],
    "mark-end": [req([S(TEXT), B(3)])],
}


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


def harness(arch, name, lines, tmp):
    """One harness process speaking lines in order: a list of results, each a dict."""
    key = r"Software\SSI-263 SAPI (development)\req-%d-%s-%s" % (os.getpid(), arch, name)
    job = os.path.join(tmp, "job_%s_%s.txt" % (arch, name))
    out = os.path.join(tmp, "%s_%s" % (arch, name))
    os.makedirs(out, exist_ok=True)
    with open(job, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    env = dict(os.environ, SSI263_SAPI_SETTINGS_KEY=key)
    if name.startswith("slow"):
        env["SSI263_SAPI_TEST_DEADLINE_MS"] = str(DEADLINE_MS)
    try:
        r = subprocess.run([os.path.join(STAGE, arch, "sapi_harness.exe"), os.path.join(STAGE, arch, "ssi263_sapi.dll"),
                            job, out], capture_output=True, text=True, timeout=300, env=env)
    finally:
        delete_tree(key)
    got = []
    for ln in r.stdout.splitlines():
        if not ln.startswith("case "):
            continue
        f = dict(x.split("=", 1) for x in ln.split()[2:] if "=" in x)
        k = int(ln.split()[1])
        events = [] if f["events"] == "-" else [
            (e.split("@")[0], int(e.split("@")[1].split("/")[0]), int(e.split("/")[1])) for e in f["events"].split(",")]
        got.append(dict(hr=int(f["hr"], 16), writes=int(f["writes"]), calls=int(f["calls"]), events=events,
                        skips=[] if f["skips"] == "-" else [int(x) for x in f["skips"].split(",")],
                        ms=int(f["ms"]), pcm=open(os.path.join(out, "case_%d.pcm" % k), "rb").read()))
    if len(got) != len(lines):
        sys.exit("FAILED: the %s harness ran %d of %d requests of %s: %s" % (
            arch, len(got), len(lines), name, (r.stdout + r.stderr)[-800:]))
    return got


def rms(pcm):
    a = array.array("h", pcm)
    return (sum(x * x for x in a[::3]) / max(1, len(a[::3]))) ** 0.5


def body(r):
    """A request's audio without the engine's 150 ms of silence at its end."""
    return r["pcm"][:-len(PAD)] if r["pcm"].endswith(PAD) else r["pcm"]


def secs(n):
    return "%.2f s" % (n / 2 / RATE)


def checks(g):
    """[(label, ok, what)] for one arch's results g: {process: [result, ...]}."""
    out = []

    def add(label, ok, what=""):
        out.append((label, bool(ok), what))

    whole = lambda name: g[name][0]["pcm"]                  # noqa: E731
    a = whole("plain")
    add("a plain request is spoken", g["plain"][0]["hr"] == 0 and rms(a) > 300 and a.endswith(PAD), secs(len(a)))
    # the writes
    for name, how in (("max", "at most 1000 bytes of each Write taken"), ("zero", "each block refused twice first")):
        r = g[name][0]
        add("writes: %s" % how, r["hr"] == 0 and r["pcm"] == a,
            "the same audio, %d Write calls" % r["calls"] if r["pcm"] == a else
            "%s of %s arrived" % (secs(len(r["pcm"])), secs(len(a))))
    r = g["slow"][0]
    add("the deadline does not count time SAPI takes", r["hr"] == 0 and r["pcm"] == a,
        "%s arrived of %s, in %d ms with a deadline of %d ms" % (secs(len(r["pcm"])), secs(len(a)), r["ms"], DEADLINE_MS))
    # rate, volume, pitch
    fast = whole("rate-site")
    add("rate: the program's +4 is faster than 0", len(fast) < 0.9 * len(a), "%s against %s" % (secs(len(fast)), secs(len(a))))
    add("rate: a fragment's +4 is the program's +4", whole("rate-frag") == fast, secs(len(whole("rate-frag"))))
    add("rate: +2 on the program's +2 is +4", whole("rate-both") == fast, secs(len(whole("rate-both"))))
    half, ratio = whole("vol-site"), rms(whole("vol-site")) / max(1.0, rms(a))
    add("volume: the program's 50 halves the samples", len(half) == len(a) and 0.42 < ratio < 0.58, "%.2f of full" % ratio)
    add("volume: a fragment's 50 is the program's 50", whole("vol-frag") == half)
    add("volume: 50 of the program's 50 is 25", whole("vol-both") == whole("vol-25"),
        "%.2f of full" % (rms(whole("vol-both")) / max(1.0, rms(a))))
    sep = b"".join(body(x) for x in g["pitch-sep"]) + PAD
    add("pitch: a change inside a request is three unit utterances", whole("pitch-mid") == sep,
        "%s against %s as three requests" % (secs(len(whole("pitch-mid"))), secs(len(sep))))
    # JAWS
    r = g["jaws"][0]
    add("JAWS: a word a fragment is the joined text", r["pcm"] == whole("jaws-joined"))
    add("JAWS: its five bookmarks, in order", [e[0] for e in r["events"]] == ["1", "2", "3", "4", "5"],
        ",".join(e[0] for e in r["events"]) or "none")
    # spell
    for name, tag in (("spell", "English"), ("spell-es", "Spanish")):
        sep = b"".join(body(x) for x in g[name + "-sep"]) + PAD
        add("spell (%s): each character a text of its own" % tag, whole(name) == sep,
            "%s against %s character by character" % (secs(len(whole(name))), secs(len(sep))))
    add("spell: not the word", whole("spell") != whole("spell-word"),
        "%s spelled, %s as a word" % (secs(len(whole("spell"))), secs(len(whole("spell-word")))))
    # skip
    three = whole("three")
    for name, count, rest in (("skip+1", 1, "from-two"), ("skip0", 0, "three"), ("skip+2", 2, "from-three"),
                              ("skip-1", -1, "three")):
        r = g[name][0]
        after = len(body(r)) - _taken(r, three)
        want = len(body(g[rest][0]))
        add("skip %+d: answered %d, and speech goes on from there" % (count, count),
            r["hr"] == 0 and r["skips"] == [count] and r["pcm"].endswith(PAD) and abs(after - want) <= 0.15 * want
            and rms(r["pcm"][_taken(r, three):]) > 300,
            "answered %s; %s after the skip, %s alone" % (r["skips"], secs(after), secs(want)))
    for name, answer, what in (("skip+5", 2, "5 asked with 2 sentences left"), ("skip-1-first", 0, "back from the first"),
                               ("skip-kind", 0, "a kind that is not sentences")):
        r = g[name][0]
        add("skip (%s): answered %d and the request ends" % (what, answer),
            r["hr"] == 0 and r["skips"] == [answer] and r["writes"] == 3 and not r["pcm"].endswith(PAD),
            "answered %s, %d writes" % (r["skips"], r["writes"]))
    r = g["skip-abort"][0]
    add("skip: an abort with it is an abort", r["hr"] == 0 and r["skips"] == [] and r["writes"] == 3,
        "answered %s, %d writes" % (r["skips"], r["writes"]))
    # bookmarks
    r = g["mark-start"][0]
    add("bookmark before the text: before any audio", [e[:3] for e in r["events"]] == [("1", 0, 0)] and r["pcm"] == a,
        str(r["events"]))
    r = g["mark-seam"][0]
    ev = r["events"]
    add("bookmark between two unit utterances: between them, its offset the bytes written",
        len(ev) == 1 and ev[0][0] == "2" and 0 < ev[0][1] == ev[0][2] < len(body(r)), str(ev))
    r = g["mark-end"][0]
    add("bookmark at the end: after the audio, before the silence",
        [e[:3] for e in r["events"]] == [("3", len(a) - len(PAD), len(a) - len(PAD))] and r["pcm"] == a, str(r["events"]))
    ev = g["jaws"][0]["events"]
    late = [w - o for _n, w, o in ev]
    add("JAWS: offsets in order, none past the audio",
        all(x[2] <= y[2] for x, y in zip(ev, ev[1:])) and all(o <= len(body(g["jaws"][0])) for _n, _w, o in ev),
        "inside one unit utterance, so each came after its audio: %s late" % ", ".join(secs(x) for x in late))
    return out


def _taken(r, whole):
    """The bytes of r that are the request's own start (what SAPI took before the skip): r's common head with the
    request spoken through."""
    n = 0
    m = min(len(r["pcm"]), len(whole))
    while n < m and r["pcm"][n] == whole[n]:
        n += 1
    return n - n % 2


def main():
    for arch in ARCHES:
        for f in ("ssi263_sapi.dll", "sapi_harness.exe", "ssi263speech.dll"):
            if not os.path.isfile(os.path.join(STAGE, arch, f)):
                sys.exit("FAILED: %s is missing: powershell -File sapi\\build.ps1 -Dev" % os.path.join(STAGE, arch, f))
    voices = [ln.split("\t")[0] for ln in open(os.path.join(STAGE, "voices.txt"), encoding="utf-8").read().splitlines()]
    for v in (EN, ES):
        if v not in voices:
            print("sapi requests: SKIPPED, the stage has no %s (its firmware is not in the folder)" % v)
            sys.exit(0)
    tmp = tempfile.mkdtemp(prefix="ssi263_sapi_requests_")
    bad = 0
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            fut = {(a, n): pool.submit(harness, a, n, lines, tmp) for a in ARCHES for n, lines in PROCS.items()}
            for a in ARCHES:
                res = checks({n: fut[a, n].result() for n in PROCS})
                for label, ok, what in res:
                    bad += not ok
                    if not ok or "--verbose" in ARGS:
                        print("%-4s %s %s%s" % ("ok" if ok else "FAIL", a, label, ": " + what if what else ""))
                print("sapi requests %s: %d of %d right" % (a, sum(ok for _l, ok, _w in res), len(res)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
        try:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, r"Software\SSI-263 SAPI (development)")
        except OSError:
            pass
    print("sapi requests: %s" % ("ok" if not bad else "%d FAILED" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
