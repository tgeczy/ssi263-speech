# src/csrc/accentz80: the Aicom Accent SA, the stand-alone box, on MAME's Z80 core

The true Accent SA: the serial box with a Z80 in it, not the PC card whose voice carried that name until 0.9.5
(`../accentpc`, the Accent-PC). The machine here is the one Kevin Horton traced from his own unit and wrote down: the
drawing of its board, the equations of its PLD read back from the part, and its memory and I/O maps. The firmware is
Aicom's, read from that unit by him: 4.1 of 2 October 1996, and 4.0 of 15 February 1991, which runs as well.

It is the same firmware design as the PC card's, on another processor: the same serial commands, the same phoneme
interrupt, the same dictionary ROM. So only the machine is new. The host (the lockstep with the chip, say, busy, boot,
cancel) and the voice (settings, numbers, a capital's pitch, index marks, the job API) are the Accent-PC's, given
this board. That the stand-alone takes those commands the same way is checked on its own firmware, not assumed.

## Why it is not a shipped voice

It runs, and it speaks as the Accent-PC does. Measured on our own two units, the same texts down both serial lines
(2026-11-20):

- **The same sounds.** Forty texts (sentences, numbers, money, abbreviations, odd spellings): the same phoneme
  sequence on all forty, 26 of them identical write for write. The rest differ in a few writes that are not
  phonemes: about ten of several hundred to the rate register, or five more pause frames.
- **The same melody and pace.** Nine texts, with clauses and pauses: the phonemes' times agree to 2 ms, the pauses
  between clauses to the millisecond, and of each text's 50 to 250 pitch values none to six differ.
- **One difference: it starts sooner.** At each unit's own processor speed the first phoneme comes a median 182 ms
  after the text here against 461 ms on the PC card; as the voices run their units (the host's `turbo`), 28 ms
  against 61 ms. But the PC card's clock is a guess (`../accentpc/README.md`), and that lead is the host's to give
  either unit.

So a listener would get a second entry that sounds like the first (Tomi listened to both and heard none), with a
second set of ROMs to carry. The project's other pairs earn their two entries by ear: the Sounding Board against
the Speak-Out in pitch and in how a sentence is carried, the two Mockingboards in how long a forced vowel is held.
This pair does not, so the Accent SA is not in `../voices.c`, not in `ssi263speech.dll`, the speech-dispatcher
module or the phones' libraries, and its ROMs are not in the repository:
`firmware/aicom-accent-sa-z80/README.txt` says which files go there. What is here is the machine, kept honest by
its tests on a test library of its own (`azv_create`), and useful as it is: it is the unit a real Accent SA's
recordings can be laid beside. A word that differs between the two firmwares may yet turn up; forty texts are not
the dictionary. If one does, or someone wants the real box by name, a row in `../voices.c` makes it a voice.

| File | What it is for |
|---|---|
| `az_board.h`, `az_board.c` | The machine: U5 at 0000-6FFF, the 6264's upper half at 7000-7FFF, the window at 8000-FFFF by the latch's MODE (U5's upper half, U4's upper half, U4's lower half, the RAM's lower half mirrored); I/O chosen by the port address's HIGH byte, as the PLD decodes it (the SC-02 at 0xxx with its registers straight on A0-A2, the foot switches at 1xxx, the 8251 at 2xxx, the option switches and the control latch at 4xxx); /NMI = the chip's A/R AND the latch's bit 4, taken on the edge; /INT = the 8251's RxRDY. The header says what is the drawing's, what the firmware was seen to do, and what is a guess. |
| `az_host.h`, `az_host.c` | The unit: this board under the Accent host (`../accentpc/as_host.h`), 8 MHz. `azh_create` from memory, `azh_create_dir` from a folder; what comes back is an `as_host`. |
| `az_voice.h`, `az_voice.c` | The voice: the Accent voice (`../accentpc/as_voice.h`) on this unit. `azv_create`; what comes back is an `as_voice`. |
| `az_waiting.inc` | Where each firmware waits (its idle loops and the instructions that close an utterance), keyed by U5's FNV-1a: what `az_board_take_work` counts the firmware's own work against, and so how the voice tells a text that has ended from one whose next clause is still being read (issue #8). **Generated**; a U5 that was not measured has no table and its work is not counted. |
| `az_waiting.c` | The tool that measures it, on the real host: every instruction address in the states where the firmware has, for certain, nothing to read or say. Run by hand when a firmware is added: `az_waiting <U4> <U5> [<U5> ...] > az_waiting.inc`. |
| `az_trace.c` | The C API alone, no Python: the two ROMs from files, a text down the serial line, every chip write with its chip time (and the audio with `--wav`, the voice's view of each block with `--watch`). For the checks, and for laying a recording of a real unit beside the same text. |
| `test_az_board.c` | The board's rules on small Z80 programs in a synthetic U5 (no firmware): 9 tests. |
| `az_controls.py` | Their must-fail controls: each rule undone in a scratch copy, exactly its tests must fail (22). |
| `test_az_firmware.py` | The firmware on the board, through `az_trace`: the greeting it says at power-up, the flush, the ports it reaches, the same writes at any block size, the rate, pitch, voice, monotone and index-mark commands, and 4.0 against 4.1. Three controls (`AZ_FW_BREAK`). Skips, and says so, without the ROMs. |
| `build_board.py` | Windows build (w64devkit): `nvda/dist/accentz80-lib/` gets `test_az_board.exe`, `az_trace.exe`, `az_waiting.exe` and `x64/accent_sa_z80.dll`, a test library that is never shipped (the unit under the Accent voice, with the chip). Linux: `../../../build_linux.sh`, where the unit goes into the test reference `libsd_voices_ref.so` only. |

The CPU is `../cpu/z80_mame.cpp` (CONTRACT.md 15). The 8251 is the Accent-PC's (`../accentpc/as_usart.c`): this
firmware programs it with the same mode word and the same two commands. Issue #8's completion checks run on this unit
too: `../accentpc/test_as_complete.py` with `AS_COMPLETE_UNIT=sa`.

## What the firmware showed on this board

- At power-up, left alone, it says "Accent ready" (the phonemes AE K S EH1 N T, ER R1 EH1 D AY).
- It reaches every device through the port address's high byte and puts the same number on the low byte, except its
  first write after reset, which loads only the high one. That write is the control latch's, not the chip's.
- Its serial handshake is the 8251's RTS, dropped while its buffer is full, as on the PC card. It never reads the
  8251's status and never sends a byte unasked.
- In MODE 3 it touches one byte of the RAM, at 8000h, once at power-up. The rest of that half and its mirrors are
  the PLD's equations, not exercised by anything it does.
- It reads the option switches three times at power-up. None of the four changed what it said on the texts tried;
  what they select is not known here. It did not read the foot switches.
- It reads and writes port 80h once at power-up, where this board has nothing: code it shares with the PC card.
- Between two clauses it goes quiet as the PC card does (8 of the completion test's 64 texts), and the voice waits.
- Firmware 4.0 wrote the same values in the same order as 4.1 on every text tried, within 0.3 ms.

## What is not known

- **The timing against a real unit, finely.** The Z80's 8 MHz and the chip's 1 MHz are read from the drawing, and
  speech is paced by the chip. Checked coarsely, once, against a private recording of a real Accent SA board (the
  lengths of its greeting and of one text, from a sound envelope): they agree closely; the numbers are kept with
  the recording, not here. Phoneme by phoneme it has not been laid side by side, and the text is known only from
  transcription.
- **The option switches' meaning and the foot switches' polarity** (above); the latch's power-up value (taken as 00h).
- **The 8251's select.** The PLD's equations do not make it; "20-2F" is the map's, modelled as the others are.
- **The data bus with nothing driving it** (an undecoded port reads FFh here).
