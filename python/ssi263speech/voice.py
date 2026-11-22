"""Text to speech through an emulated Blazie Braille Lite: its own firmware turns the text into phonemes and drives
the SSI-263, exactly as in the NVDA add-on and the speech-dispatcher module (the same C, bl_voice).

The firmware is not included (it is Blazie's): pass the folder holding BL2ENG.BNS + bl2_2003_warm.state (English)
or BL2SPA.BNS + bl2spa_fresh.state (Spanish); 0.8 adds German (Lite2000_de.BNS + lite2000_de_fresh.state), Italian
(Lite2000_it.BNS + lite2000_it_fresh.state) and the Slovak Braille 'n Speak 2000 (BS2SLL.BNS + bs2sll_fresh.state)
-- for instance the NVDA add-on's engine folder, which names them so -- and 17 more languages from the release table
(src/csrc/blazie/bl_languages.txt: "hu", "tr", "pt-BR" ...), each its image and state in the folder's bl2000/.

    v = BrailleLite("path/to/firmware", rate=22050)
    pcm = v.speak("Hello there.")                    # 16-bit mono PCM (bytes)
"""
import ctypes
import os

from . import _native

# language: (firmware, state, text code page (bl_voice.h BLV_*), the unit never echoes ^F (voices.c bl_no_echo))
FILES = {"en": ("BL2ENG.BNS", "bl2_2003_warm.state", 0, False),
         "es": ("BL2SPA.BNS", "bl2spa_fresh.state", 13, False),
         "de": ("Lite2000_de.BNS", "lite2000_de_fresh.state", 1, False),
         "it": ("Lite2000_it.BNS", "lite2000_it_fresh.state", 14, True),
         "sk": ("BS2SLL.BNS", "bs2sll_fresh.state", 7, True)}      # 852, o circumflex as its capital


def _release_files(language):
    """A release table voice by its language: (bl2000/<file>, bl2000/<release id>.state, its code page, False -- the
    voice reads its own echo from the release, bl_voice.c)."""
    lib = _native.voice_lib()
    for name in ("blv_release_voice_key", "blv_release_tag", "blv_release_file", "blv_release_id"):
        getattr(lib, name).restype = ctypes.c_char_p
        getattr(lib, name).argtypes = [ctypes.c_int]
    lib.blv_release_text.argtypes = [ctypes.c_int]
    for k in range(lib.blv_firmware_count()):
        if lib.blv_release_voice_key(k) and lib.blv_release_tag(k).decode().lower() == language.lower():
            return (os.path.join("bl2000", lib.blv_release_file(k).decode()),
                    os.path.join("bl2000", lib.blv_release_id(k).decode() + ".state"), lib.blv_release_text(k), False)
    raise KeyError("ssi263speech: no Braille Lite voice for %r (the languages: %s and the release table's)" % (
        language, ", ".join(sorted(FILES))))


def languages():
    """Every language a BrailleLite can speak, the release table's included (its firmware still has to be there)."""
    lib = _native.voice_lib()
    for name in ("blv_release_voice_key", "blv_release_tag"):
        getattr(lib, name).restype = ctypes.c_char_p
        getattr(lib, name).argtypes = [ctypes.c_int]
    return sorted(FILES) + sorted(lib.blv_release_tag(k).decode() for k in range(lib.blv_firmware_count())
                                  if lib.blv_release_voice_key(k))


class BrailleLite:
    """One Braille Lite unit booted as the screen-reader drivers boot it.  rate: the output sample rate (Hz)."""

    def __init__(self, firmware_dir, language="en", rate=22050, inflection=True, idle_noise=0):
        fw, st, codepage, no_echo = FILES[language] if language in FILES else _release_files(language)
        paths = [os.path.join(firmware_dir, fw), os.path.join(firmware_dir, st)]
        for p in paths:
            if not os.path.isfile(p):
                raise FileNotFoundError("ssi263speech: %s not found (the firmware is not included)" % p)
        self._lib = _native.voice_lib()
        err = ctypes.create_string_buffer(256)
        # os.fsencode: UTF-8 on Windows (PEP 529), as the C library's bl_fopen.h reads a path with a high byte; strict
        # "mbcs" raised on a folder named outside the code page, and handed on ANSI bytes that bl_fopen.h could read
        # as another name ("Ã©" as "é")
        self._v = self._lib.blv_create(os.fsencode(paths[0]), os.fsencode(paths[1]), codepage,
                                       float(rate), 1 if inflection else 0, int(idle_noise), err, 256)
        if not self._v:
            raise RuntimeError("ssi263speech: the unit did not boot: %s" % err.value.decode("latin-1", "replace"))
        if no_echo:
            self._lib.bh_set_int(self._lib.blv_host(self._v), b"no_echo", 1)
        self.rate = rate
        self.set()

    def close(self):
        if getattr(self, "_v", None):   # __init__ may have refused before the unit was made
            self._lib.blv_destroy(self._v)
            self._v = None

    def __del__(self):
        self.close()

    def set(self, rate=50, pitch=50, tone=7, volume=100, short_pauses=True):
        """NVDA's scales: rate, pitch and volume 0-100 (50, 50 = the unit's factory rate and pitch); tone 0-26."""
        self._lib.blv_set(self._v, int(rate), int(pitch), int(tone), int(volume), 1 if short_pauses else 0)

    def speak(self, text):
        """The whole utterance as 16-bit mono PCM (bytes)."""
        return b"".join(self.stream(text))

    def stream(self, text):
        """The utterance block by block, as the unit speaks it."""
        self._lib.blv_speak(self._v, text.encode("utf-8"))
        pcm, done = ctypes.POINTER(ctypes.c_short)(), ctypes.c_int(0)
        while not done.value:
            n = self._lib.blv_render(self._v, ctypes.byref(pcm), ctypes.byref(done))
            if n:
                yield ctypes.string_at(pcm, 2 * n)

    def cancel(self):
        self._lib.blv_cancel(self._v)
