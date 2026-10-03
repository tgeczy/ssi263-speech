# src/csrc/accentsa: the Aicom Accent SA board on MAME's 8085 core

**The 0.7 release default.** The Accent SA voice uses this C board on MAME's 8085.
The Python 8085 is retained only as a development reference. This folder provides
the machine and host in C for front ends that run without Python. The board is `accent_sa.py`'s machine
with nothing changed; the host is its lockstep with the chip, line for line. Aicom's ROMs are not built in: they are
read from a folder or given in memory (`firmware/aicom-accent-sa`, in the repository with `firmware/AICOM.txt`).

| File | What it is for |
|---|---|
| `as_board.h`, `as_board.c` | The machine: u2's lower half at 0000-77FF, 2 KB of RAM at 7800-7FFF, the 32 KB window at 8000-FFFF banked by port 40h (u2's upper half, u3, u4, nothing); the SSI-263 at ports 03-07, reversed, A/R in bit 7 of port 07; port 40h's latch (bank, and bit 4 gating A/R onto TRAP) and switches; the 8085 through `../cpu/cpu.h`; the TRAP gate, RxRDY on RST 6.5, RST 7.5's edge; and `as_board_run`, which counts T-states as the Python host does (below). The chip is the caller's, through two callbacks, so a write lands inside the slice and IN 07h reads A/R then. |
| `as_usart.h`, `as_usart.c` | The 8251 as the firmware uses it: a mode word after reset, commands (RTS, internal reset), status 85h + RxRDY, the host's bytes queued and loaded one at a time while RTS is up. |
| `as_host.h`, `as_host.c` | The host: `accent_sa.py`'s `AccentSA` in C (say, run, skip, busy, boot, cancel, speaking), around the board and an SSI-263 (`../ssi263.h`): the caller's, or one made from the built-in defaults. ROMs from a folder (`ash_create_dir`) or memory (`ash_create`). The API of `accent_sa.dll` / `libaccent_sa.so`. |
| `as_voice.h`, `as_voice.c` | The Accent SA as a voice, for front ends without Python (Android's built-in voice, and since 0.7.5 the NVDA add-on through `ssi263speech.dll`, with its job API `asv_begin` ... `asv_flush` and `asv_set_voice`): the NVDA Accent driver's "sa" front end in C, as `../blazie/bl_voice.c` is the Braille Lite's -- boot, settings commands sent when they change, currencies, `_clean` and number words (`../accent_text.c`, shared with the Accent-mini's `../accentmini/am_voice.c`, and `../numwords.c`), a capital's pitch with snap_pitch, the 30 ms speak loop with the lead trim, the gain and the settle between clauses (issue #8), cancel. Held to the driver byte for byte by `src/platforms/android/test/test_android_native.py`. |
| `as_render.c` | The C API alone: the ROMs from a folder and a text into a WAV, no Python (`as_render <folder> "text" out.wav`). |
| `test_as_board.c` | The board's rules on small programs in a synthetic u2 (no firmware): 12 tests, including I/O, memory and register effects at an acceptance-only slice boundary. |
| `test_as_complete.py` | Issue #8: a text is done only when the unit has nothing left to say. Between clauses the firmware goes quiet (speaking flag down, no phoneme) while it reads the next one, and `asv_render` called that done; it now holds a quiet block in which the 8085 ran any of its own code (outside its waiting loops: `as_board.c`'s `WAITING`, the host's `work`) and gives the held blocks out only if speech follows; `asv_set_settle` caps the wait (6 s) and a done at the cap is reported (`asv_limit`), never plain. Checks, through the job API and `asv_speak`: a real done (API results, no fault, inside the budget); the host run on 4 s after it speaks nothing; a text that never paused keeps its audio and its done's chip time; the same with the blocks shifted 0-27 ms; a cancel or new text while blocks are held lets nothing through; the cap reported. Controls (`AS_COMPLETE_BREAK=1`, `never`, `held`, `limit`) must fail. Run by `nvda/tools/run_tests.py` and `tools/linux_tests.sh`. |
| `trace_idle_writes.py` | Issue #8, preserved: the corpus session in which, at rate 100, some jobs had a write 2.3-5.1 s after their done. Replays it (offline, about a minute) to the first such job and prints its run-on: on this branch, the firmware re-initialising the chip with its speaking flag down (a phoneme 19h, then PAs), not the rest of a text; on 0.7.5's library the first such job is the issue itself. Its trigger is not known. |
| `as_controls.py` | Its must-fail controls: each rule undone in a scratch copy, exactly its tests must fail (17). Run by hand, as `so_controls.py`: seventeen builds. |
| `compare_accent_sa.py` | The C host against the Python host (the reference) on a scripted session: every write's value and chip time, the audio, and the counting events, identical; the core's own counting reported and classified. `ACCENTSA_COMPARE_FLIP=1`: its must-fail control. |
| `build_board.py` | Windows build (w64devkit): `nvda/dist/accentsa-lib/` gets `test_as_board.exe`, `as_render.exe`, `x64/` and `x86/accent_sa.dll` (importing `ssi263.dll`, as `bl.dll` does). `../../../build_linux.sh` builds `test_as_board`, `as_render` and `libaccent_sa.so`. |

The CPU is `../cpu/i8085_mame.cpp` (CONTRACT.md's 8085 clauses). Python reaches the host through
`src/hosts/accent_sa_c.py`; `AccentSA()` returns it by default. The add-on carries
the module and both x86/x64 DLLs. Development builds may select a library through
`SSI263_ACCENT_SA_DLL` or use the research tree's `nvda/dist/accentsa-lib/`.

## Two numbers that are not the unit's

- **`cpu_hz` = 3,072,000 is a guess** (a 6.144 MHz crystal, halved by the 8085). The unit's clock has not been measured.
- **`turbo` = 8 is a host feature**: while the firmware reads a sentence, before its first phoneme, the 8085 runs eight
  times faster, so speech starts sooner. Speech itself is paced by A/R and does not change.

Both are `accent_sa.py`'s, kept so that the C host is the same host; neither is a claim about the hardware.

## Counting: the host's, not the core's (2026-09-30)

The two 8085s count the same: `../cpu/trace_i8085.py` found every instruction's and every acceptance's T-states equal
on this firmware. The hosts differ in how a slice ends, and `as_board_run` counts as the Python host does
(`python_slices`, the default):

- **An acceptance at the end of a slice.** `i8085.py`'s `run()` tests its budget between an interrupt's acceptance and
  the next instruction, so an acceptance that reaches the budget ends the slice, and the vector's first instruction (a
  JMP at every vector of u2) runs in the next slice. The core's step is the acceptance and the instruction together.
  The board uses `i8085_step_slice` to stop before the instruction and its effects. The earlier cost-only carry
  executed it too early; a synthetic OUT/STA/MVI boundary test now prevents that regression. It happens when a
  command arrives on the serial line (the scripted session: twice; random
  sessions with ESC R/P between texts: 115 times in 40).
- **A TRAP raised in EI's shadow.** The Python core samples nothing in the instruction after EI, TRAP included; the chip
  (and the core, CONTRACT.md 5) takes TRAP at once. The board holds such a TRAP through that instruction. Never seen
  on this firmware (0 in every session run), kept for exactness, with its test and control.
- A HALT: the Python core ends its slice there, the core runs 4-T HALT slots. Nothing carries between slices, so the
  two agree; the firmware never halts.

With `SSI263_ACCENT_SA_SLICES=chip` the core's own semantics stand (experimental). On every session compared, even
with 115 carried instructions, no write moved: the firmware waits on A/R or the serial port before any write, which
absorbs the 10 T-states. So this counting is exact but, on this firmware, not audible.

## The C host against the Python host (first comparison, 2026-09-30)

`compare_accent_sa.py` (its three parts, the same SSI263C model on both sides): 5,206 writes, every value and every
chip time identical, the audio identical (791 blocks); two acceptances ended the Python host's slice, and the board
carried two. `--quick` (run_tests): 1,761 writes, one carried, the same; on 32-bit Python 3.7 with the x86 DLL too.
Two longer random sessions with commands between texts (a scratch search, seeds 27 and 36): 19,812 and 21,727 writes,
4 and 5 carried, identical. The add-on's Accent SA voice on the C host (driver_sim, 64- and 32-bit): all 33 scenarios
pass, and 32 of them give the same PCM as on the Python host byte for byte (the 33rd is the cancel timed by the wall
clock). The C host runs the full session in about half a second where the Python host takes 8 to 16.

The counting check needs the session to hold a carried acceptance, and where one falls depends on the chip's timing.
If a retune of the chip (`params.py`) moves it out of the short `commands` part, `compare_accent_sa.py --find-split 300`
prints the shortest command-and-text sessions that hold one on the C host; put one in its `COMMANDS`.

## Open questions

- Android (2026-09-30): the built-in default voice. `build_android.sh` builds the core with the NDK's clang++ and
  libc++ static inside the one `.so`; the board, host and `as_voice` as here. On the A024 a unit boots in 2.4 ms.
- Linux: `libaccent_sa.so` takes the chip's functions from the `libssi263speech.so` that `ssi263/native.py` loaded
  (made global by `accent_sa_c.py` first). Built by `build_linux.sh`, not yet run on Linux.
- Accepting it: the add-on keeps the Python 8085 until Tomi and Astra accept this one.
