"""sd_ssi263 against speech-dispatcher's module protocol, with every message's audio checked byte for byte.

The harness plays the server: it starts the module, speaks the protocol on its stdin and stdout, and decodes the 705
AUDIO blocks.  A reference bl_voice in libssi263speech.so (ctypes), driven through the same calls, must give the
same PCM:

  speak    a message, rendered whole
  stop     STOP after 5 audio blocks, then a new message: the reference renders the same number of blocks, cancels,
           and speaks the new one -- the module must have cancelled the unit, or its leftovers reach the next message
  set      rate, pitch and volume from SET (SSIP -100..100 -> the driver's 0..100)
  spanish  language=es switches to the Spanish unit (when its files are there)
  key      KEY space speaks "space"

the protocol's corners, as speech-dispatcher 0.11.1's server sends them (output.c) and its own modules answer them
(module_process.c):

  pause    PAUSE after 5 audio blocks: the unit cancelled as for a STOP; pause_ev: the message ended with 704 PAUSE (on
           703 the server gives the message up, on 704 it keeps it for RESUME); p_after: the next message whole
  char     CHAR a speaks "a"; icon: a SOUND_ICON (there are none) is answered and ends with no audio
  unknown  an unknown command is answered 300, and the module goes on
  debug    "DEBUG ON <file>" and "DEBUG OFF" are answered 200 OK DEBUGGING ON / OFF, a bad one 302
  dbg_mid  a "DEBUG ON" that arrives mid-message, its file's name holding PAUSE and STOP: the message is spoken whole
           (the words are commands only as whole lines), and the DEBUG answered after it
  quit     QUIT is answered 210 OK QUIT, and the module exits with 0

and the config: the module file and this user's own (which wins), the sample rate and the hiss -- and the
EXPERIMENTAL run ahead (SSI263RunAhead 1, blv_set_run_ahead), against a reference voice running ahead too:

  ra_speak a message whole, with the same phonemes as the lockstep's (the reference's own write log: the module's PCM
           is the reference's, byte for byte), its audio not the lockstep's (the run-ahead path taken)
  ra_dflt  no key: the first message above was the lockstep's, not run ahead's
  ra_stop  STOP mid-message (as "stop", 12 blocks in), and ra_after: the next message whole, its phonemes those of it said alone --
           nothing of the cancelled text
  ra_sys   the key in the module file runs ahead; ra_user0: this user's "SSI263RunAhead 0" wins over it

and the Braille Lite's number words (the NVDA driver's "Custom number processing", bl_numbers through
blv_set_numbers), against a reference with them on or off -- each reference pair must differ on the text, or the
check could not tell; the references have them on as the module's default does:

  num_dflt no key: the default is the driver's (numberWords' defaultVal and self._numbers in blazie.py, read here):
           "1,234,567", "3.5" and "$12.50" as the reference with that setting says them
  num_es   no key, the Spanish unit: Spain's "1.234.567" and "3,5" (tres coma cinco)
  num_user SSI263BrailleLiteNumbers 0 in the module file, this user's 1 over it: on
  num_off  this user's SSI263BrailleLiteNumbers 0: off -- the firmware reads the digits; num_es_off: the Spanish too

The other voices (0.7.1, "every voice on Linux"), each against its own voice driven directly through the same calls
(libsd_voices_ref.so, beside the module: the module's own engine objects, ctypes), in a session of their own:

  voices   LIST VOICES: every voice built in whose files are there, by the add-ons' names, with its language --
           the Accent SA always (Aicom's ROMs are in the repository)
  <v>_speak, <v>_stop, <v>_after, <v>_set   as above, on that voice (SET synthesis_voice), its unit one for the session
  <v>_bl   the Braille Lite again in the same session (SET synthesis_voice), and <v>_back: that voice once more,
           its unit as it was left; <v>_lang: SET language=en keeps that voice (it speaks English)
  <v>_44k  SSI263SampleRate 44100 reaches it
  as_infl  SSI263AccentInflection 0 reaches the Accent SA (the reference at 0 must differ from the default's)
where <v> is as (the Accent SA), am (the Accent-mini) and so (the Speak-Out) -- the last two when built in
(build_linux.sh: their sources in the tree) and their files are there.  The data folder the module gets is a fresh
one: the Braille Lite's files from <data folder>, and aicom-accent-sa/, aicom-accent-mini/ and gw-micro-speakout/ from
<data folder> when they are there, else from the repository's firmware/ (SPEAKOUT.HEX also from
<data folder>/../speakout-firmware: it is never in the repository).

    python3 test_sd_ssi263.py <sd_ssi263> <libssi263speech.so> <data folder>
    SD_SSI263_TEST_NO_CANCEL=1 in the environment: the module leaves the unit uncancelled -- "stop" must FAIL
    SD_SSI263_TEST_OLD_REPLIES=1: PAUSE, DEBUG and QUIT answered as up to 0.9.1 -- pause_ev, debug, dbg_mid and quit
                                  must FAIL
    SD_SSI263_TEST_IGNORE_RUN_AHEAD=1: the module drops SSI263RunAhead -- the ra_ checks must FAIL
    SD_SSI263_TEST_IGNORE_LINE_LIFT=1: the module drops SSI263LineLift -- lift must FAIL
    SD_SSI263_TEST_IGNORE_BL_NUMBERS=1: the module drops SSI263BrailleLiteNumbers (the default reaches the unit) --
                                        num_off and num_es_off must FAIL
    SD_SSI263_TEST_IGNORE_ACCENT_INFLECTION=1: the module drops SSI263AccentInflection -- as_infl must FAIL
"""
import ctypes
import os
import re
import subprocess
import sys
import tempfile

MODULE, LIB, DATA = sys.argv[1:4]
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
REF_LIB = os.path.join(os.path.dirname(os.path.abspath(MODULE)), "libsd_voices_ref.so")
LONG = ("This is a long message for the stop test, with a comma or two, that keeps going well past the moment "
        "the harness says stop. It has a second sentence as well.")
lib = ctypes.CDLL(LIB)
lib.blv_create.restype = ctypes.c_void_p
lib.blv_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_double, ctypes.c_int,
                           ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
lib.blv_set.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 5
lib.blv_speak.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lib.blv_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)),
                           ctypes.POINTER(ctypes.c_int)]
lib.blv_cancel.argtypes = [ctypes.c_void_p]
lib.blv_destroy.argtypes = [ctypes.c_void_p]
lib.blv_set_run_ahead.argtypes = [ctypes.c_void_p, ctypes.c_int]
lib.blv_set_line_lift.argtypes = [ctypes.c_void_p, ctypes.c_int]
lib.blv_set_numbers.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
NUMBERS_FN = ctypes.cast(lib.bl_numbers, ctypes.c_void_p)     # bl_numbers.h: the driver's _numbers in C
lib.blv_host.restype = ctypes.c_void_p
lib.blv_host.argtypes = [ctypes.c_void_p]
lib.bh_set_int.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]


class BhWrite(ctypes.Structure):                 # bl_host.h's bh_write
    _fields_ = [("t", ctypes.c_double), ("reg", ctypes.c_int), ("val", ctypes.c_int)]


lib.bh_writes.restype = ctypes.c_int
lib.bh_writes.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(BhWrite))]
lib.bh_clear_writes.argtypes = [ctypes.c_void_p]


# ---- the reference -----------------------------------------------------------------------------------------------
# 0.8: the other units by language: their files (the module's flat names), code page (bl_voice.h) and whether they
# echo ^F (voices.c bl_no_echo)
UNITS = {"de": ("Lite2000_de.BNS", "lite2000_de_fresh.state", 1, False),
         "it": ("Lite2000_it.BNS", "lite2000_it_fresh.state", 14, True),
         "sk": ("BS2SLL.BNS", "bs2sll_fresh.state", 7, True)}


class Ref:
    def __init__(self, spanish=False, rate=22050, whine=0, run_ahead=0, numbers=1, line_lift=0, lang=None):
        fw, st = (("BL2SPA.BNS", "bl2spa_fresh.state") if spanish else ("BL2ENG.BNS", "bl2_2003_warm.state"))
        enc, no_echo = (13 if spanish else 0), False      # bl_voice.h BLV_CP850ES / BLV_LATIN1
        if lang:
            fw, st, enc, no_echo = UNITS[lang]
            numbers = 0                                 # the unit reads its own numbers (voices.c)
        err = ctypes.create_string_buffer(256)
        self.v = lib.blv_create(os.path.join(DATA, fw).encode(), os.path.join(DATA, st).encode(), enc,
                                float(rate), 1, whine, err, 256)
        assert self.v, err.value
        if no_echo:
            lib.bh_set_int(lib.blv_host(self.v), b"no_echo", 1)
        lib.blv_set_run_ahead(self.v, run_ahead)
        lib.blv_set_line_lift(self.v, line_lift)
        lib.blv_set_numbers(self.v, NUMBERS_FN if numbers else None)
        self.host = lib.blv_host(self.v)
        lib.bh_set_int(self.host, b"log_writes", 1)    # the chip's writes, for the phonemes (nothing else changes)
        self.ctrl = 0
        self.phonemes = []

    def _drain(self):
        """The phonemes in the writes logged since the last drain -- run_ahead_driver.py's reading: a register-0 write
        while the control register's bit 7 is clear, its code not a pause."""
        p, out = ctypes.POINTER(BhWrite)(), []
        for i in range(lib.bh_writes(self.host, ctypes.byref(p))):
            if p[i].reg == 3:
                self.ctrl = p[i].val
            elif p[i].reg == 0 and not self.ctrl & 0x80 and p[i].val & 0x3F:
                out.append(p[i].val & 0x3F)
        lib.bh_clear_writes(self.host)
        return out

    def say(self, text, rate=0, pitch=0, volume=100, blocks=None):
        """PCM of the message; with `blocks`, only that many non-empty blocks, then a cancel.  self.phonemes: the
        message's, up to its end or the cancel."""
        to100 = lambda s: max(0, min(100, (s + 100) // 2))      # noqa: E731
        lib.blv_set(self.v, to100(rate), to100(pitch), 7, to100(volume), 1)
        self._drain()
        lib.blv_speak(self.v, text.encode("utf-8"))
        pcm, done, out, n_blocks = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), [], 0
        while not done.value:
            n = lib.blv_render(self.v, ctypes.byref(pcm), ctypes.byref(done))
            if n:
                out.append(ctypes.string_at(pcm, 2 * n))
                n_blocks += 1
                if blocks is not None and n_blocks == blocks and not done.value:
                    self.phonemes = self._drain()
                    lib.blv_cancel(self.v)
                    break
        else:
            self.phonemes = self._drain()
        return b"".join(out)


# ---- the other voices: the module's data folder, and each voice driven directly ------------------------------------
TO100 = lambda s: max(0, min(100, (s + 100) // 2))      # noqa: E731  (sd_ssi263.c's to100)
FW_DIRS = {"aicom-accent-sa": ("u2.BIN", "u3.BIN", "u4.BIN"), "aicom-accent-mini": ("SPKEMS.DVC",),
           "gw-micro-speakout": ("SPEAKOUT.HEX",),
           "sweet-micro-mockingboard": ("mockingboard-tts-1.1.bin", "mockingboard-tts-early.bin"),
           "automated-functions-smartalk": ("S_TALK.EXE",)}       # in the repository: always here
FW_EACH = {"sweet-micro-mockingboard"}      # folders whose files are separate voices: each file found on its own


def voices_data():
    """A fresh folder for the module: <data folder>'s files, and each firmware folder from the first place that has
    all its files -- <data folder>/<sub>, the repository's firmware/<sub>, <data folder>/../speakout-firmware; in a
    FW_EACH folder, each file from the first place that has it (the Mockingboard's two versions are two voices)."""
    out = tempfile.mkdtemp(prefix="sd_ssi263_data_")
    for name in os.listdir(DATA):
        if name not in FW_DIRS:
            os.symlink(os.path.join(DATA, name), os.path.join(out, name))
    for sub, files in FW_DIRS.items():
        cands = (os.path.join(DATA, sub), os.path.join(ROOT, "firmware", sub),
                 os.path.join(DATA, "..", "speakout-firmware"))
        if sub in FW_EACH:
            for f in files:
                for cand in cands:
                    if os.path.isfile(os.path.join(cand, f)):
                        os.makedirs(os.path.join(out, sub), exist_ok=True)
                        os.symlink(os.path.abspath(os.path.join(cand, f)), os.path.join(out, sub, f))
                        break
            continue
        for cand in cands:
            if all(os.path.isfile(os.path.join(cand, f)) for f in files):
                os.symlink(os.path.abspath(cand), os.path.join(out, sub))
                break
    return out


VDATA = voices_data()
ref_lib = ctypes.CDLL(REF_LIB)
BUILT = [line.split("\t") for line in
         subprocess.run([MODULE, "--voices"], stdout=subprocess.PIPE, check=True).stdout.decode("utf-8").splitlines()]
BUILT_NAMES = [b[2] for b in BUILT]
_P, _I, _D, _S = ctypes.c_void_p, ctypes.c_int, ctypes.c_double, ctypes.c_char_p


def _api(prefix, create_args, set_args):
    f = lambda n: getattr(ref_lib, prefix + n)      # noqa: E731
    f("create").restype = _P
    f("create").argtypes = create_args
    f("set").argtypes = [_P] + [_I] * set_args
    f("speak").argtypes = [_P, _S, _I]
    f("render").argtypes = [_P, ctypes.POINTER(ctypes.POINTER(ctypes.c_short)), ctypes.POINTER(_I)]
    f("cancel").argtypes = [_P]
    f("destroy").argtypes = [_P]
    return f


# Each voice: its name, its engine's prefix, how its unit is made, and its settings from the config's defaults (or
# the keys given) -- the calls sd_voices.c makes.
ENGINES = {"as": ("Accent SA",), "am": ("Accent-mini",), "so": ("Speak-Out",), "mb": ("Mockingboard",),
           "mbe": ("Mockingboard, early",), "st": ("SmarTalk (Automated Functions)",)}


class EngineRef:
    def __init__(self, key, rate=22050, accent_inflection=100):
        self.key, self.name = key, ENGINES[key][0]
        err = ctypes.create_string_buffer(256)
        if key == "as":
            self.f = _api("asv_", [_S, ctypes.c_size_t, _S, ctypes.c_size_t, _S, ctypes.c_size_t, _D, _S, _I], 5)
            roms = [open(os.path.join(VDATA, "aicom-accent-sa", n), "rb").read() for n in FW_DIRS["aicom-accent-sa"]]
            self.v = self.f("create")(roms[0], len(roms[0]), roms[1], len(roms[1]), roms[2], len(roms[2]),
                                      float(rate), err, 256)
        elif key == "am":
            self.f = _api("amv_", [_S, _D, _S, _I], 6)
            self.v = self.f("create")(os.path.join(VDATA, "aicom-accent-mini", "SPKEMS.DVC").encode(), float(rate),
                                      err, 256)
        elif key == "mb":
            self.f = _api("mbv_", [_S, ctypes.c_size_t, _D, _S, _I], 4)
            ref_lib.mbv_create_dir.restype = _P              # from its folder (mb_voice.h), not _api's create
            ref_lib.mbv_create_dir.argtypes = [_S, _D, _S, _I]
            self.v = self.f("create_dir")(os.path.join(VDATA, "sweet-micro-mockingboard").encode(), float(rate),
                                          err, 256)
        elif key == "mbe":
            # from the file's bytes (mbv_create, as voices.c makes it): mbv_create_dir would take the 1.1 file, which
            # shares the folder; the host picks the early layout by the bytes' sha256
            self.f = _api("mbv_", [_S, ctypes.c_size_t, _D, _S, _I], 4)
            with open(os.path.join(VDATA, "sweet-micro-mockingboard", "mockingboard-tts-early.bin"), "rb") as fh:
                self.image = fh.read()
            self.v = self.f("create")(self.image, len(self.image), float(rate), err, 256)
        elif key == "st":
            # from the file's bytes (stv_create, as voices.c makes it)
            self.f = _api("stv_", [_S, ctypes.c_size_t, _D, _S, _I], 6)
            with open(os.path.join(VDATA, "automated-functions-smartalk", "S_TALK.EXE"), "rb") as fh:
                self.image = fh.read()
            self.v = self.f("create")(self.image, len(self.image), float(rate), err, 256)
        else:
            self.f = _api("sov_", [_S, _D, _S, _I], 6)
            self.v = self.f("create")(os.path.join(VDATA, "gw-micro-speakout", "SPEAKOUT.HEX").encode(),
                                      float(rate), err, 256)
        assert self.v, err.value
        self.accent_inflection = accent_inflection

    def _set(self, rate, pitch, volume):
        r, p, v = TO100(rate), TO100(pitch), TO100(volume)
        if self.key == "as":        # asv_set(rate, pitch, inflection, volume, numbers)
            self.f("set")(self.v, r, p, self.accent_inflection, v, 1)
        elif self.key == "am":      # amv_set(rate, pitch, inflection, volume, numbers, voice 5)
            self.f("set")(self.v, r, p, self.accent_inflection, v, 1, 5)
        elif self.key in ("mb", "mbe"):     # mbv_set(rate, pitch, volume, numbers)
            self.f("set")(self.v, r, p, v, 1)
        elif self.key == "st":      # stv_set(rate, pitch, tone I, volume, inflection, numbers): its defaults
            self.f("set")(self.v, r, p, 8, v, 1, 1)
        else:                       # sov_set(rate, pitch, tone I, volume, join, short pauses)
            self.f("set")(self.v, r, p, 8, v, 1, 1)

    def say(self, text, rate=0, pitch=0, volume=100, blocks=None):
        """PCM of the message; with `blocks`, only that many non-empty blocks, then a cancel (as Ref.say)."""
        self._set(rate, pitch, volume)
        self.f("speak")(self.v, text.encode("utf-8"), 0)
        pcm, done, out, n_blocks = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0), [], 0
        while not done.value:
            n = self.f("render")(self.v, ctypes.byref(pcm), ctypes.byref(done))
            if n:
                out.append(ctypes.string_at(pcm, 2 * n))
                n_blocks += 1
                if blocks is not None and n_blocks == blocks and not done.value:
                    self.f("cancel")(self.v)
                    break
        return b"".join(out)

# ---- the server's side of the protocol --------------------------------------------------------------------------
class Module:
    def __init__(self, conf=None, user_conf=None):
        """conf: the module config's text (argv[1]); user_conf: this user's own file's text.  Every module gets a
        fresh HOME, so a real ~/.config/ssi263-speech on the test machine can never change what it says."""
        home = tempfile.mkdtemp(prefix="sd_ssi263_home_")
        env = dict(os.environ, SSI263_DATADIR=VDATA, HOME=home)
        env.pop("XDG_CONFIG_HOME", None)
        args = [MODULE]
        if conf is not None:
            path = os.path.join(home, "ssi263.conf")
            with open(path, "w") as f:
                f.write(conf)
            args.append(path)
        if user_conf is not None:
            os.makedirs(os.path.join(home, ".config", "ssi263-speech"))
            with open(os.path.join(home, ".config", "ssi263-speech", "sd_ssi263.conf"), "w") as f:
                f.write(user_conf)
        self.rates = set()
        self.p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=env)

    def send(self, *lines):
        self.p.stdin.write(("\n".join(lines) + "\n").encode("utf-8"))
        self.p.stdin.flush()

    def line(self):
        s = self.p.stdout.readline()
        if not s:
            raise RuntimeError("the module exited")
        return s.rstrip(b"\n")

    def reply(self):
        """Lines up to the final one (NNN-... continues, NNN ... ends)."""
        out = []
        while True:
            s = self.line()
            out.append(s)
            if len(s) > 3 and s[3:4] == b" ":
                return out

    def audio_block(self, first):
        """A 705 block whose first header line was already read: returns its PCM."""
        n = None
        s = first
        while not s.startswith(b"705-AUDIO"):
            if s.startswith(b"705-num_samples="):
                n = int(s.split(b"=")[1])
            if s.startswith(b"705-sample_rate="):
                self.rates.add(int(s.split(b"=")[1]))
            s = self.line()
        data = s[len(b"705-AUDIO") + 1:] + b"\n"   # the payload starts after the NUL, and may contain no raw '\n'
        while not data.endswith(b"\n705 AUDIO\n"):
            data += self.p.stdout.readline()
        raw, out, i = data[:-len(b"\n705 AUDIO\n")], bytearray(), 0
        while i < len(raw):
            if raw[i] == 0x7D:
                out.append(raw[i + 1] ^ 0x20)
                i += 2
            else:
                out.append(raw[i])
                i += 1
        assert len(out) == 2 * n, (len(out), n)
        return bytes(out)

    def speak(self, text, cmd="SPEAK", stop_after=None, stop_cmd="STOP"):
        """Returns (pcm, the event that ended it, blocks).  stop_after: that many audio blocks in, stop_cmd is sent."""
        self.send(cmd)
        assert self.line() == b"202 OK RECEIVING MESSAGE"
        self.send(text, ".")
        assert self.line() == b"200 OK SPEAKING"
        pcm, blocks = [], 0
        while True:
            s = self.line()
            if s == b"701 BEGIN":
                continue
            if s.startswith(b"705-"):
                pcm.append(self.audio_block(s))
                blocks += 1
                if stop_after is not None and blocks == stop_after:
                    self.send(stop_cmd)
                continue
            if s in (b"702 END", b"703 STOP", b"704 PAUSE"):
                return b"".join(pcm), s.decode(), blocks
            raise RuntimeError("unexpected %r" % s)

    def set(self, **kw):
        self.send("SET")
        assert self.line() == b"202 OK RECEIVING MESSAGE"
        self.send(*["%s=%s" % kv for kv in kw.items()] + ["."])
        assert self.line() == b"203 OK SETTINGS RECEIVED"


def same(label, got, want):
    ok = got == want
    k = next((i for i in range(0, min(len(got), len(want)), 2) if got[i:i + 2] != want[i:i + 2]),
             min(len(got), len(want)))
    print("%-8s module %6d samples, reference %6d: %s" % (label, len(got) // 2, len(want) // 2,
                                                        "identical" if ok else "DIFFER at sample %d" % (k // 2)))
    return ok


m = Module()
m.send("INIT")
init = m.reply()
assert init[-1] == b"299 OK LOADED SUCCESSFULLY", init
m.send("LIST VOICES")
voices = [v.decode("utf-8") for v in m.reply()]
print("voices:", "; ".join(v[4:].replace("\t", " / ") for v in voices[:-1]))
listed = [v[4:].split("\t")[0] for v in voices[:-1]]
# the add-ons' voices, by their names, with their languages: those built in whose files are there, in this order
EXPECTED = [("Braille Lite 2000", "en-US", ("BL2ENG.BNS", "bl2_2003_warm.state")),
            ("Braille Lite 2000 (Spanish)", "es-ES", ("BL2SPA.BNS", "bl2spa_fresh.state")),
            ("Braille Lite 2000 (German)", "de-DE", ("Lite2000_de.BNS", "lite2000_de_fresh.state")),
            ("Braille Lite 2000 (Italian)", "it-IT", ("Lite2000_it.BNS", "lite2000_it_fresh.state")),
            ("Braille 'n Speak 2000 (Slovak)", "sk-SK", ("BS2SLL.BNS", "bs2sll_fresh.state")),
            ("Accent SA", "en-US", tuple("aicom-accent-sa/" + f for f in FW_DIRS["aicom-accent-sa"])),
            ("Accent-mini", "en-US", ("aicom-accent-mini/SPKEMS.DVC",)),
            ("Speak-Out", "en-US", ("gw-micro-speakout/SPEAKOUT.HEX",)),
            # 0.8.5's GW Micro Sounding Board, right under the Speak-Out (sd_voices.c), its two drivers in the data folder
            ("Sounding Board (GW Micro)", "en-US", ("gw-micro-sounding-board/SBLOAD.COM",
                                                    "gw-micro-sounding-board/SB.COM")),
            ("Mockingboard", "en-US", ("sweet-micro-mockingboard/mockingboard-tts-1.1.bin",)),
            ("Mockingboard, early", "en-US", ("sweet-micro-mockingboard/mockingboard-tts-early.bin",))]
# 0.8: then the release table's Braille Lite voices (sd_voices.c SSV_BLAZIE_RELEASE_SD_ROWS, in the table's order),
# each its image and state in the data folder's bl2000/
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "csrc", "blazie"))
import gen_languages  # noqa: E402
EXPECTED += [(r["vname"], r["tag"], ("bl2000/" + r["file"], "bl2000/%s.state" % r["id"]))
             for r in gen_languages.rows()[0] if r["voice"].startswith("blazie_")]
# then Automated Functions' SmarTalk (sd_voices.c, last), its S_TALK.EXE in the data folder
EXPECTED += [("SmarTalk (Automated Functions)", "en-US", ("automated-functions-smartalk/S_TALK.EXE",))]
want = ["200-%s\t%s\tMALE1" % (n, lang) for n, lang, files in EXPECTED
        if n in BUILT_NAMES and all(os.path.isfile(os.path.join(VDATA, f)) for f in files)] + ["249 OK VOICES LISTED"]
ok = voices == want and "Accent SA" in listed and set(BUILT_NAMES) <= {e[0] for e in EXPECTED}
print("voices   %d listed, built in: %s: %s" % (len(listed), ", ".join(BUILT_NAMES),
                                                "as expected" if ok else "NOT as expected: %r" % want))
ref = Ref()
results = [ok]

HELLO = "Hello from Linux & speech-dispatcher."
pcm, ev, _ = m.speak("<speak>Hello from Linux &amp; speech-dispatcher.</speak>")
results.append(same("speak", pcm, ref.say(HELLO)) and ev == "702 END")
hello_default = pcm

pcm1, ev1, blocks = m.speak(LONG, stop_after=5)
pcm2, ev2, _ = m.speak("And the next message.")
first = ref.say(LONG, blocks=blocks)
results.append(ev1 == "703 STOP" and same("stop", pcm1, first))
results.append(same("after", pcm2, ref.say("And the next message.")) and ev2 == "702 END")

m.set(rate=40, pitch=-30, volume=-20)
pcm, ev, _ = m.speak("Faster, lower and quieter.")
results.append(same("set", pcm, ref.say("Faster, lower and quieter.", rate=40, pitch=-30, volume=-20)))
m.set(rate=0, pitch=0, volume=100)

pcm, ev, _ = m.speak("space", cmd="KEY")
results.append(same("key", pcm, ref.say("space")))

if os.path.isfile(os.path.join(DATA, "BL2SPA.BNS")):
    m.set(language="es")
    pcm, ev, _ = m.speak("Mañana, ¿qué tal?")
    es_ref = Ref(spanish=True)                  # one unit, as the module's: the second time follows the first
    results.append(same("spanish", pcm, es_ref.say("Mañana, ¿qué tal?")))
    # the name it had until the four were named in English, as a client may have saved it: still the Spanish voice
    # (sd_voices.c FORMER).  From English and the default first, so a name ignored would stay on the English voice
    m.set(language="en")
    m.set(synthesis_voice="Braille Lite 2000")
    m.set(synthesis_voice="Braille Lite 2000 (español)")
    pcm, ev, _ = m.speak("Mañana, ¿qué tal?")
    results.append(same("former", pcm, es_ref.say("Mañana, ¿qué tal?")))
    m.set(language="es")                        # as the check before left it, for those after
# the 0.8 units, each when its files are there: chosen by its language, spoken as its own reference
for lang, text in (("de", "Guten Tag, schön."), ("it", "Buongiorno, città."), ("sk", "Dobrý deň, čaj.")):
    if os.path.isfile(os.path.join(DATA, UNITS[lang][0])):
        m.set(language=lang)
        pcm, ev, _ = m.speak(text)
        results.append(same(lang, pcm, Ref(lang=lang).say(text)) and ev == "702 END")

m.send("QUIT")
m.p.wait(timeout=10)


# ---- the protocol's corners, as speech-dispatcher 0.11.1's server sends them and its own modules answer them
# (src/server/output.c, src/modules/module_process.c) ----------------------------------------------------------------
def said(label, ok, what):
    print("%-8s %s: %s" % (label, what, "as expected" if ok else "NOT as expected"))
    return ok


# a session of its own for the PAUSE: its unit is cancelled mid-message, and nothing else is to depend on that
p = Module()
p.send("INIT")
assert p.reply()[-1] == b"299 OK LOADED SUCCESSFULLY"
pref = Ref()
pcm1, ev1, pblocks = p.speak(LONG, stop_after=5, stop_cmd="PAUSE")
results.append(same("pause", pcm1, pref.say(LONG, blocks=pblocks)))
results.append(said("pause_ev", ev1 == "704 PAUSE", "a PAUSE after %d blocks ended the message with %s" % (pblocks, ev1)))
pcm2, ev2, _ = p.speak("And the next message.")
results.append(same("p_after", pcm2, pref.say("And the next message.")) and ev2 == "702 END")
p.send("QUIT")
p.p.wait(timeout=10)

p = Module()
p.send("INIT")
assert p.reply()[-1] == b"299 OK LOADED SUCCESSFULLY"
pref = Ref()
pcm, ev, _ = p.speak("a", cmd="CHAR")
results.append(same("char", pcm, pref.say("a")) and ev == "702 END")
pcm, ev, n = p.speak("bell", cmd="SOUND_ICON")
results.append(said("icon", (pcm, ev) == (b"", "702 END"), "a SOUND_ICON ended with %s and %d audio blocks" % (ev, n)))
p.send("FROBNICATE")
r = p.reply()
results.append(said("unknown", r == [b"300 ERR UNKNOWN COMMAND"], "an unknown command answered %r" % r))
got = []
for line in ("DEBUG ON /tmp/sd_ssi263.log ", "DEBUG OFF ", "DEBUG MAYBE"):     # the server's own, a space at their end
    p.send(line)
    got.append(p.reply()[-1].decode())
results.append(said("debug", got == ["200 OK DEBUGGING ON", "200 OK DEBUGGING OFF", "302 ERROR BAD SYNTAX"],
                    "DEBUG ON, DEBUG OFF and a bad one answered %s" % got))
# a DEBUG that arrives while a message is spoken, its file's name holding the words: the message is spoken whole, and
# the DEBUG answered after it (the last audio check of this session: where it fails, the unit was cancelled)
pcm, ev, _ = p.speak(LONG, stop_after=5, stop_cmd="DEBUG ON /tmp/PAUSE-or-STOP.log ")
r = p.reply()[-1].decode()
ok = same("dbg_mid", pcm, pref.say(LONG))
results.append(said("dbg_mid", ok and (ev, r) == ("702 END", "200 OK DEBUGGING ON"),
                    "a DEBUG ON mid-message: the message ended with %s, the DEBUG answered %s" % (ev, r)))
p.send("QUIT")
try:
    q = p.line().decode()
except RuntimeError:
    q = "nothing"
code = p.p.wait(timeout=10)
results.append(said("quit", (q, code) == ("210 OK QUIT", 0), "QUIT answered %s, and the module exited with %d" % (q, code)))


# ---- the config: the module file, this user's own file (which wins), and a rate we do not offer -------------------
def configured(label, conf, user_conf, rate, whine=0, run_ahead=0, line_lift=0):
    c = Module(conf, user_conf)
    c.send("INIT")
    assert c.reply()[-1] == b"299 OK LOADED SUCCESSFULLY"
    pcm, _ev, _n = c.speak("Is it ready?")
    c.send("QUIT")
    c.p.wait(timeout=10)
    ok = c.rates == {rate}
    print("%-8s audio blocks declare %s Hz (want %d)" % (label, sorted(c.rates), rate))
    return same(label, pcm, Ref(rate=rate, whine=whine, run_ahead=run_ahead,
                                line_lift=line_lift).say("Is it ready?")) and ok


results.append(configured("conf44", "SSI263SampleRate 44100\n", None, 44100))
results.append(configured("user", "SSI263SampleRate 11025\n", "SSI263SampleRate 44100\n", 44100))
results.append(configured("badrate", "SSI263SampleRate 48000\n", None, 22050))
results.append(configured("userhiss", None, 'SSI263Whine "hiss"\n', 22050, whine=1))


# ---- run ahead (EXPERIMENTAL, off by default): this user's SSI263RunAhead 1 -----------------------------------------
def phonemes_same(label, got, want, what):
    ok = got == want
    print("%-8s %d phonemes, %s %d: %s" % (label, len(got), what, len(want), "the same" if ok else "DIFFERENT"))
    return ok


lock, ahead = Ref(), Ref(run_ahead=1)
lock_hello = lock.say(HELLO)
lock_ph = lock.phonemes
r = Module(user_conf="SSI263RunAhead 1\n")
r.send("INIT")
assert r.reply()[-1] == b"299 OK LOADED SUCCESSFULLY"
pcm, ev, _ = r.speak(HELLO)
want = ahead.say(HELLO)
ok = same("ra_speak", pcm, want) and ev == "702 END"
ok = phonemes_same("ra_speak", ahead.phonemes, lock_ph, "the lockstep's") and ok
print("ra_speak the run-ahead reference's audio %s the lockstep's" % ("differs from" if want != lock_hello else
                                                                       "is IDENTICAL to"))
results.append(ok and want != lock_hello)
print("ra_dflt  no SSI263RunAhead: the first message is %s" % ("the lockstep's" if hello_default == lock_hello and
                                                              hello_default != want else "NOT the lockstep's"))
results.append(hello_default == lock_hello and hello_default != want)

NEXT = "And the next message."
pcm1, ev1, ra_blocks = r.speak(LONG, stop_after=12)     # ~0.35 s in: where a cancel once leaked (blazie.py)
pcm2, ev2, _ = r.speak(NEXT)
r.send("QUIT")
r.p.wait(timeout=10)
results.append(ev1 == "703 STOP" and same("ra_stop", pcm1, ahead.say(LONG, blocks=ra_blocks)))
ok = same("ra_after", pcm2, ahead.say(NEXT)) and ev2 == "702 END"
alone = Ref()
alone.say(NEXT)
results.append(phonemes_same("ra_after", ahead.phonemes, alone.phonemes, "said alone") and ok)

# the module file's key, and this user's 0 over it; the two references must differ, or neither check could tell
ra_q, lock_q = Ref(run_ahead=1).say("Is it ready?"), Ref().say("Is it ready?")
print("ra_sys   \"Is it ready?\": run ahead's reference %s the lockstep's" % ("differs from" if ra_q != lock_q else
                                                                             "is IDENTICAL to"))
results.append(configured("ra_sys", "SSI263RunAhead 1\n", None, 22050, run_ahead=1) and ra_q != lock_q)
results.append(configured("ra_user0", "SSI263RunAhead 1\n", "SSI263RunAhead 0\n", 22050))

# ---- the line-start lift (off by default): the module file's SSI263LineLift 1 -------------------------------------
# the module's first message is fresh, so it is lifted; the references must differ, or the check could not tell
lift_q, flat_q = Ref(line_lift=1).say("Is it ready?"), Ref().say("Is it ready?")
print("lift     \"Is it ready?\": the lifted reference %s the unlifted one" % ("differs from" if lift_q != flat_q
                                                                         else "is IDENTICAL to"))
results.append(configured("lift", "SSI263LineLift 1\n", None, 22050, line_lift=1) and lift_q != flat_q)

# ---- the Braille Lite's number words: the driver's default, the key, this user's key over the module file's -------
def driver_numbers_default():
    """blazie.py's: numberWords' defaultVal and self._numbers in __init__ (they must agree)."""
    src = open(os.path.join(ROOT, "nvda", "blazie", "synthDrivers", "blazie.py"), encoding="utf-8").read()
    setting = re.search(r'BooleanDriverSetting\("numberWords", [^\n]*?defaultVal=(True|False)\)', src).group(1)
    init = re.search(r"^\s+self\._numbers = (True|False)\b", src, re.M).group(1)
    assert setting == init, (setting, init)
    return setting == "True"


NUM_EN = "It is 1,234,567 steps, 3.5 miles and $12.50."
NUM_ES = "Son 1.234.567 pasos y 3,5 kilos."
SPANISH_HERE = os.path.isfile(os.path.join(DATA, "BL2SPA.BNS"))
DRIVER_NUMBERS = driver_numbers_default()
en_on, en_off = Ref(numbers=1).say(NUM_EN), Ref(numbers=0).say(NUM_EN)
print("num      the driver's default: number words %s; with them the English reference %s the one without" % (
    "on" if DRIVER_NUMBERS else "off", "differs from" if en_on != en_off else "is IDENTICAL to"))
es_on = es_off = None
if SPANISH_HERE:
    es_on, es_off = Ref(spanish=True, numbers=1).say(NUM_ES), Ref(spanish=True, numbers=0).say(NUM_ES)
    print("num      with them the Spanish reference %s the one without" % (
        "differs from" if es_on != es_off else "is IDENTICAL to"))


def numbers_session(conf=None, user_conf=None):
    """NUM_EN on the English unit, and NUM_ES on the Spanish one when it is there: (English PCM, Spanish PCM)."""
    n = Module(conf, user_conf)
    n.send("INIT")
    assert n.reply()[-1] == b"299 OK LOADED SUCCESSFULLY"
    en, _ev, _b = n.speak(NUM_EN)
    es = None
    if SPANISH_HERE:
        n.set(language="es")
        es, _ev, _b = n.speak(NUM_ES)
    n.send("QUIT")
    n.p.wait(timeout=10)
    return en, es


en, es = numbers_session()
results.append(same("num_dflt", en, en_on if DRIVER_NUMBERS else en_off) and en_on != en_off)
if SPANISH_HERE:
    results.append(same("num_es", es, es_on if DRIVER_NUMBERS else es_off) and es_on != es_off)
en, es = numbers_session("SSI263BrailleLiteNumbers 0\n", "SSI263BrailleLiteNumbers 1\n")
results.append(same("num_user", en, en_on) and en_on != en_off)
en, es = numbers_session(None, "SSI263BrailleLiteNumbers 0\n")
results.append(same("num_off", en, en_off) and en_on != en_off)
if SPANISH_HERE:
    results.append(same("num_es_off", es, es_off) and es_on != es_off)


# ---- the other voices, each in a session of its own: speak, stop, after, set; the Braille Lite and back; language --
def voice_session(key):
    name = ENGINES[key][0]
    ref_v, bl = EngineRef(key), Ref()
    s = Module()
    s.send("INIT")
    assert s.reply()[-1] == b"299 OK LOADED SUCCESSFULLY"
    s.set(synthesis_voice=name)
    ok = []
    pcm, ev, _ = s.speak("<speak>Hello from Linux &amp; speech-dispatcher.</speak>")
    want = ref_v.say(HELLO)
    # the guard: the voice's reference is not the Braille Lite's, or a module stuck on the Braille Lite could pass
    print("%-8s the %s reference %s the Braille Lite's" % (key + "_speak", name,
                                                       "differs from" if want != lock_hello else "is IDENTICAL to"))
    apart = True
    if key == "mbe" and ENGINES["mb"][0] in listed:
        # and the early one's is not the 1.1's, or a module that loaded the 1.1 file for both could pass
        apart = want != EngineRef("mb").say(HELLO)
        print("%-8s the %s reference %s the 1.1 Mockingboard's" % (key + "_speak", name,
                                                                "differs from" if apart else "is IDENTICAL to"))
    ok.append(same(key + "_speak", pcm, want) and ev == "702 END" and want != lock_hello and apart)
    pcm1, ev1, n = s.speak(LONG, stop_after=5)
    pcm2, ev2, _ = s.speak("And the next message.")
    ok.append(ev1 == "703 STOP" and same(key + "_stop", pcm1, ref_v.say(LONG, blocks=n)))
    ok.append(same(key + "_after", pcm2, ref_v.say("And the next message.")) and ev2 == "702 END")
    s.set(rate=40, pitch=-30, volume=-20)
    pcm, ev, _ = s.speak("Faster, lower and quieter.")
    ok.append(same(key + "_set", pcm, ref_v.say("Faster, lower and quieter.", rate=40, pitch=-30, volume=-20)))
    s.set(rate=0, pitch=0, volume=100)
    s.set(synthesis_voice="Braille Lite 2000")
    pcm, ev, _ = s.speak("Back to the Braille Lite.")
    ok.append(same(key + "_bl", pcm, bl.say("Back to the Braille Lite.")) and ev == "702 END")
    s.set(synthesis_voice=name)
    pcm, ev, _ = s.speak("And the other voice again.")
    ok.append(same(key + "_back", pcm, ref_v.say("And the other voice again.")) and ev == "702 END")
    s.set(language="en")
    pcm, ev, _ = s.speak("Still the same voice.")
    ok.append(same(key + "_lang", pcm, ref_v.say("Still the same voice.")) and ev == "702 END")
    s.send("QUIT")
    s.p.wait(timeout=10)
    # the sample rate reaches it
    c = Module(conf="SSI263SampleRate 44100\n")
    c.send("INIT")
    assert c.reply()[-1] == b"299 OK LOADED SUCCESSFULLY"
    c.set(synthesis_voice=name)
    pcm, _ev, _n = c.speak("Is it ready?")
    c.send("QUIT")
    c.p.wait(timeout=10)
    print("%-8s audio blocks declare %s Hz (want 44100)" % (key + "_44k", sorted(c.rates)))
    ok.append(same(key + "_44k", pcm, EngineRef(key, rate=44100).say("Is it ready?")) and c.rates == {44100})
    return ok


# the Accent-mini's, the Speak-Out's, the two Mockingboards' and the SmarTalk's checks are counted apart, so the
# controls' counts (tools/linux_tests.sh) hold whether or not those voices are built in
others = []
for key in ("as", "am", "so", "mb", "mbe", "st"):
    if ENGINES[key][0] in listed:
        (results if key == "as" else others).extend(voice_session(key))
    else:
        print("%-8s %s: not built in or its files are not there -- not checked" % (key, ENGINES[key][0]))

# the Accent's own key: this user's SSI263AccentInflection 0, against a reference at 0 that must differ from the default
a = Module(user_conf="SSI263AccentInflection 0\n")
a.send("INIT")
assert a.reply()[-1] == b"299 OK LOADED SUCCESSFULLY"
a.set(synthesis_voice="Accent SA")
pcm, _ev, _n = a.speak("Is it ready? It is.")
a.send("QUIT")
a.p.wait(timeout=10)
flat, full = EngineRef("as", accent_inflection=0).say("Is it ready? It is."), EngineRef("as").say("Is it ready? It is.")
print("as_infl  the reference at inflection 0 %s the default's" % ("differs from" if flat != full else "is IDENTICAL to"))
results.append(same("as_infl", pcm, flat) and flat != full)

print("%d of %d checks passed (stop after %d blocks, %d with run ahead)" % (sum(results), len(results), blocks,
                                                                          ra_blocks))
if others:
    print("the Accent-mini's, the Speak-Out's and the Mockingboards' (1.1 and early): %d of %d checks passed" % (
        sum(others), len(others)))
sys.exit(0 if all(results) and all(others) else 1)
