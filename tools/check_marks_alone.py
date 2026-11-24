"""Sentence marks alone are not a stall, on every voice in the voice table (src/csrc/voices.h).

A text of sentence marks with no word among them -- a row of dots, a table of contents' leaders, ". . . ." -- keeps a
unit working through pauses and saying nothing for a while.  Three voices took that for a stalled card and cut the text
past 4 s (the SynPhonix, d047c31; the Accent-mini and the Accent-PC: their voice's watchdog), "there" lost from
"Hi. . . . there."; the English Braille Lite said "Hi", nothing more, and nothing of the next text (0.9.5, measured).

For every voice whose firmware is here, at rate 0 (the slowest: the longest pauses): "Hi" + 40 lone periods +
"there." must hold at least VOICED of the voiced sound "Hi there." holds (10 ms frames over 1500), and "Hello world."
said after it at the voice's default rate as much as it holds said alone.

    python tools/check_marks_alone.py [library] [--firmware DIR] [--only ID,...]

Controls: SPV_STALL_BREAK=1 (sp_voice.c: a stall judged by silence) -- synphonix:vest must fail;
ACCENT_STALL_BREAK=1 (am_voice.c, as_voice.c) -- accentmini:mini and accentmini:sa must fail.
"""
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_cancel_head as c  # noqa: E402  (its library, firmware and --only arguments, its say())

TEXT = "Hi" + " ." * 40 + " there."
VOICED = 0.8


def voiced_ms(x, sr):
    n = int(0.01 * sr)
    return 10 * sum(1 for k in range(0, len(x), n) if max((abs(a) for a in x[k:k + n]), default=0) > 1500)


def main():
    lib = c.load()
    fw = c.FW.encode("utf-8")
    results, ran = [], 0
    for i in range(lib.ssv_count()):
        vid = lib.ssv_voice_info(i).contents.id.decode()
        if c.ONLY and vid not in c.ONLY:
            continue
        if not lib.ssv_engine_built(i) or not lib.ssv_available(i, fw):
            print("skip  %-28s no engine or no firmware here" % vid)
            continue
        err = ctypes.create_string_buffer(256)
        v = lib.ssv_create(i, fw, None, err, 256)
        if not v:
            print("FAIL  %-28s not made: %s" % (vid, err.value.decode("latin-1")))
            results.append(False)
            continue
        try:
            s = c.Settings()
            lib.ssv_defaults(i, ctypes.byref(s))
            sr = lib.ssv_sample_rate(v)
            default_rate = s.rate
            hello = voiced_ms(c.say(lib, v, s, "Hello world."), sr)
            s.rate = 0
            ref = voiced_ms(c.say(lib, v, s, "Hi there."), sr)
            got = voiced_ms(c.say(lib, v, s, TEXT), sr)
            s.rate = default_rate
            after = voiced_ms(c.say(lib, v, s, "Hello world."), sr)
            ok = got >= VOICED * ref and after >= VOICED * hello
            ran += 1
            results.append(ok)
            print("%-5s %-28s voiced ms: 'Hi' + 40 periods + 'there.' %d against 'Hi there.' %d; 'Hello world.' "
                  "after it %d against %d" % ("ok" if ok else "FAIL", vid, got, ref, after, hello))
            sys.stdout.flush()
        finally:
            lib.ssv_destroy(v)
    if not ran:
        print("skip  no voice's firmware in %s" % c.FW)
        return 0
    print("marks alone: %s" % ("all %d voices passed" % ran if all(results) else "%d FAILED" % results.count(False)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
