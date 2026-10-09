# src/csrc/accentmini: the Aicom Accent-mini as a portable-C voice

The Accent-mini's text-to-speech ran on the PC: Aicom's DOS driver SPKEMS.DVC ("Accent-EMS", V4.5, 1986-1993),
with its rules in expanded memory, driving the card's AI901 (SSI-263 compatible). This folder runs that driver
without Python, the same way the Python host does: on MAME's 8086 (`../pc86`, `../cpu/i86_mame.cpp`), under a
host that stands in for DOS, the BIOS, the EMS manager and the PIC. The NVDA driver's front end runs around it.
It is for front ends without Python: the Linux speech-dispatcher module and Android. It works the way
`../blazie/bl_voice.c` does for the Braille Lite and `../accentsa/as_voice.c` does for the Accent SA. The driver is
not built in. It is read from a file or given in memory (`firmware/aicom-accent-mini/SPKEMS.DVC`, in the repository
with `firmware/AICOM.txt`).

| File | What it is for |
|---|---|
| `am_host.h`, `am_host.c` | The host: `src/hosts/accent.py`'s `Accent`, line for line. It loads the EXE-format driver at 0800:0000 with its relocations, runs INIT as DOS would, and provides LIM EMS 3.2/4.0 as far as the driver asks (the page frame copied in and out, 56h's alter-and-call, 4Eh/47h/48h). It provides the DOS calls the driver makes and the card (data 3EFh, control 3EEh, A/R on IRQ2 through a one-request PIC). Text is printed to the driver's LPT through INT 17h. The lockstep with the chip uses `cpu_ips` 5,000,000 instructions per chip second, a policy kept from the Unicorn host and not a clock. It covers calls that block or run in the background (`say(background=True)`), `_settle`, `_drop_call`, boot, cancel and busy. Every rounding and order of operations is Python's. |
| `am_voice.h`, `am_voice.c` | The voice: the NVDA driver's "mini" front end (`nvda/accent/synthDrivers/accentmini.py`). That covers boot, the settings commands sent only when they change (rate R0-H, pitch P0-9, voice V0-9 as the variant, inflection M1-M4/M0), the text rules, and a capital's pitch with snap_pitch and its restore. It also covers the 30 ms speak loop with the lead trim, gain and watchdog, and cancel with `_resend_pitch`. After a host fault, the next utterance restarts the card, as the driver does. API: `amv_create`/`amv_create_mem`, `amv_set`, `amv_speak`, `amv_render`, `amv_cancel`, `amv_fault`, `amv_destroy`; and a whole NVDA sequence as the driver speaks it (several texts, capitals, the lead trim once per job): `amv_begin`, `amv_pitch`, `amv_text`, `amv_end`, `amv_flush` -- the NVDA add-on's driver since 0.7.5. |
| `../accent_text.h`, `../accent_text.c` | Shared with `as_voice.c` (one NVDA driver speaks both Aicom voices). It holds `_clean`, `_numbers` and `_grouped`, the settings mapping (`_accent_pitch`, `_accent_settings`) and the `_ESC` rule both hosts use for `say(speech=None)`. Number words and currencies come from `../numwords.c`. |
| `../translit.h` | The accented letters, first on every voice's text (header-only, shared with the Braille Lite and the Speak-Out): a letter the 7-bit firmware lacks becomes its base letters ("tükör" -> "tukor"), alone its words ("á" -> "a acute"). `nvda/tools/translit_test.py` holds it; `../test_translit.c` is its C test. |
| `am_render.c` | The C API alone: the driver from a file and a text into a WAV, with no Python (`am_render SPKEMS.DVC "text" out.wav [rate]`). It is `accent.py`'s `__main__`. |
| `build_am.py` | Windows build (w64devkit) into `nvda/dist/accentmini-lib/`: `am_render.exe`, and `x64/` and `x86/accent_mini.dll`, which import `ssi263.dll` as `bl.dll` does. The NVDA add-on does not carry it: since 0.7.5 it speaks through `ssi263speech.dll` (`../build_ssi263speech.py`), the same sources. `../../../build_linux.sh` builds the objects and `am_render`. `build_android.sh` compiles the objects, which are not linked yet. |

## Held to the NVDA driver

`nvda/tools/am_voice_equiv.py` (in `run_tests.py`) drives the real NVDA driver under the stand-in NVDA and
`accent_mini.dll` on the same 24 utterances. It requires every PCM byte the driver feeds its player to equal what
`amv_render` returns. The utterances cover numbers on and off, punctuation, Unicode and other currencies, a capital
and the utterance after it, rate/pitch/voice/inflection at both ends, volume 0, 35 and 100, and all three sample
rates (11025, 22050, 44100), each on a new card as the driver reboots it. They also include a cancel
mid-utterance, plain and during a capital (which sends the pitch again), followed by the next utterance. The test
also checks the text sent for 4,000 random texts, with numbers on and off. Its controls must fail as named:
`AM_EQUIV_BREAK=cancel` cancels one block late in C, and `AM_EQUIV_BREAK=numbers` flips number processing in C.
Each control runs only the 15 cases at 22050 Hz: 11 of 15 stay identical for the cancel, 2 of 15 for numbers, and
the random texts differ too.

First results (2026-10-01): 24 of 24 utterances and 4,000 of 4,000 texts are identical. `am_render` matches
`accent.py` run on the C chip byte for byte at 44100 and 11025, and its WAVs are bit-identical between Windows x64
(w64devkit gcc) and Linux arm64 (gcc 14, a Raspberry Pi 5).

## Not the driver's

- **INIT runs.** The add-on restores a pickled snapshot of the machine after INIT (`SPKEMS.state`), which C cannot
  read. The C voice runs INIT itself. It is the same machine: the snapshot's chip writes are all at time 0, and the
  driver's PCM equals the C voice's. A Python comparison of a state boot against an INIT boot also gives identical PCM.
- **Volume above 100** is allowed, as in `as_voice` (Android's gain). The NVDA driver stops at 100.
- **A cancel after the utterance has finished does nothing**, as the driver's cancel between jobs. `as_voice` flushes
  even then.
- **Index commands, and more than one pitch command per utterance**, are not modelled. A capital is one offset before
  the text, as in `as_voice`.
- **Speed:** the policy of 5 M instructions per chip second means the 8086 runs about 5 M steps for every second of
  speech. On the desktop that is several times faster than real time. On a phone it has not been measured yet.
