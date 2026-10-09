# src/csrc/speakout: the GW Micro Speak-Out board on MAME's V40 core

**The 0.7 release default.** The Speak-Out add-on runs its V40 on this MAME board. This
board replaces the CPU only (Astra and Tomi's scope for this step): the memory, the chip's window and the V40's
interrupt controller and serial unit are reduced exactly as that host reduces them, so any difference from Unicorn is
the CPU's. The firmware (`SPEAKOUT.HEX`) is not in the repository.

| File | What it is for |
|---|---|
| `so_board.h`, `so_board.c` | The board: 1 MB of RAM, the firmware's HEX, the SSI-263 at F000:FE00-FE04 (its writes returned in order, the RAM keeping them), ports routed to the ICU and SCU, power-on through the reset vector, the host's slice-start interrupt offer, running by steps or by clocks, and `so_run_steps_unicorn` (steps counted as Unicorn counts instructions, for the comparison). |
| `so_icu.h`, `so_icu.c` | The V40's interrupt controller as the firmware uses it: mask, in-service set, non-specific EOI at ports 8-9; vectors 8 + IR; an offer taken or dropped at once, as `speakout.py`'s `_try_irq`. |
| `so_scu.h`, `so_scu.c` | The V40's serial receiver: queued whole bytes, one loaded at a time; status bit 1 at port 1, the byte at port 0 (reading it loads the next). |
| `so_hex.h`, `so_hex.c` | Intel HEX into the 1 MB image, as `speakout.py`'s `load_intel_hex`. |
| `test_so_board.c` | The board's rules on small programs (no firmware): 11 tests. |
| `so_controls.py` | Its must-fail controls: each rule undone in a scratch copy, exactly its tests must fail (11). |
| `so_host.h`, `so_host.c` | The host in C: `src/hosts/speakout.py`'s `SpeakOutV40` on mame-steps, line for line -- the chip-time lockstep (offer, chip slice, `max(200, int(1.5e6 x dt))` steps, the writes applied in order), `say`, `busy`, `boot`, `cancel`, `skip` -- around this board and `../ssi263.c`. |
| `so_voice.h`, `so_voice.c` | The voice: the NVDA driver's front end in C (its boot, the ^E settings commands, currencies and `_clean`, a capital's snapped pitch, the speak loop with the lead trim and PCM, cancel and the pitch said again, the sample-rate switch), as `../blazie/bl_voice.h` and `../accentsa/as_voice.h`. For speech-dispatcher, Android and, since 0.7.5, the NVDA add-on itself (through `ssi263speech.dll`): its job API (`sov_begin`, `sov_pitch`, `sov_text`, `sov_end`, `sov_flush`) speaks a whole NVDA sequence as the 0.7.0 Python driver did (`nvda/tools/native_driver_equiv.py`). `nvda/tools/so_voice_equiv.py` holds it to the driver byte for byte, `so_voice_text_equiv.py` its text path. |
| `../translit.h` | The accented letters, first in `so_voice.c`'s text path (header-only, shared with the Braille Lite and the Accents): the firmware drops Latin-1's letters, so a letter beyond ASCII becomes its base letters ("tükör" -> "tukor"), alone its words ("á" -> "a acute"). `nvda/tools/translit_test.py` holds it. |
| `so_render.c` | The voice's C API alone: SPEAKOUT.HEX and a text into a WAV, the chip built in (a smoke test; the same bytes on Windows x64 and Linux arm64). |
| `build_board.py` | Windows build (w64devkit): `nvda/dist/speakout-lib/` gets `test_v40_contract.exe`, `test_so_board.exe`, `so_render.exe`, `x64/` and `x86/speakout_v40.dll` (what the 0.7.0 add-on shipped; now the reference, `nvda/tools/legacy_drivers.py`) and `so_voice.dll` (the host and voice, for the tests; it links `ssi263.dll`). `build_linux.sh` builds the same tests and `libspeakout_v40.so`, and compiles the host and voice; `build_android.sh` compiles the board, host and voice for each ABI. |

The CPU is `../cpu/v40_mame.cpp` (CONTRACT.md 12). Python reaches the board through `src/hosts/speakout_v40.py`;
`SpeakOut()` selects `mame-steps` by default, keeping the established instruction-count
coupling to chip time. Development-only `SSI263_SPEAKOUT_CORE=mame` selects clock
coupling at `SSI263_SPEAKOUT_V40_HZ` (default 8 MHz, an unmeasured hypothesis, not the
release timing). `nvda/tools/speakout_core_compare.py` retains the Unicorn comparison.

## What the firmware touches (measured under Unicorn, boot and two sentences)

Ports: in 0 and 1 (the SCU's data and status), out 1, 2, 3 once each (the SCU's command 35h, mode 4Eh, mask 02h),
out 7 = 76h and out 5 twice (the TCU's counter 1, mode 3, loaded 2000h: set up once, never read), out 8 = 12h then
20h (EOI) at every interrupt, out 9 = 08h (ICW2) then 41h (OCW1), and the V40's system registers FFF0h-FFFEh once
each (OPSEL 0Eh: ICU, TCU and SCU on, DMA off; IULA 08h, TULA 04h, SULA 00h). No other I/O, no word I/O, no HLT, no
software interrupt, no 0Fh (NEC) opcode; REPNE SCASB is its busiest string instruction.

## What a later peripheral step would change

Modelling the V40's peripherals as the chip has them, a separate step once Tomi has listened: bytes arriving at the
SCU's baud rate (from the TCU's counter 1, clocked by the V40's clock or an external one) instead of whole bytes at
slice starts; the ICU's request register, so an interrupt is held until IE allows it instead of being offered once per
slice and dropped; the ICU's full 8259-style priorities and ICW2's vector base. The firmware uses none of the rest
(DMA, the TCU's other counters, the refresh and wait-state registers).
