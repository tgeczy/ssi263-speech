# src/csrc/supervoice: Speech Systems' Super Voice (1984), speaking through our SSI-263

The Super Voice was a speech cartridge for the Tandy Color Computer with an SSI-263 at FF80h to FF84h. This folder
runs its own text-to-speech, SUPERTOK ("V5.0A", by Del Software for Speech Systems), unchanged, on MAME's 6809
(`../cpu/m6809_mame.cpp`) and our chip.

**It is in the voice table** (0.9.5): `supervoice:supertok`, "Speech Systems Super Voice", engine `SSV_SUPERVOICE`,
on every platform -- and no package carries its program (`voices.h`'s `SSV_USER_DATA`): each person downloads or
imports it, `sv_dsk.h` taking it off the disk ([docs/super-voice.md](../../../docs/super-voice.md) says how, platform by
platform). Tomi has heard its first clips (19 November 2026: close to the Mockingboard in sound, and a first word that
climbed in pitch, remedied below); nobody has judged it as a voice through a screen reader yet.

Nothing of the Color Computer is used: no BASIC ROM, no boot. The program was run with every instruction outside it
trapped, and on the path the host uses it calls no ROM routine and reads none of BASIC's variables.

The program file is not in the repository, nor in any package. `../../../firmware/speech-systems-super-voice/README.txt`
says which file goes there and why it is not here; `../../../docs/super-voice.md` says where the disk is kept.

| File | What it is for |
|---|---|
| `sv_host.h`, `sv_host.c` | The host: 64 KB of RAM and the chip, nothing else. `SUPERTOK.BIN` placed as LOADM places it (only the known file, by sha256); a text given as Extended BASIC's USR gives a string (the manual's own example: the entry at 5805h, the descriptor in X); the 6809 and the chip in step, the chip brought up to the 6809's time at every access; D7 of a read high while the chip requests. The header says which numbers are the host's and not the cartridge's: the chip's time standing still until the program first waits for it (so speech starts at once), every text finding the program as loaded, cancel and the chip rested after it, a text's pitch landing at once, and the guards (a call that waits for ever, a program that leaves its own code). |
| `sv_voice.h`, `sv_voice.c` | The voice: NVDA's rate and pitch onto the program's own control codes (`^S` 1-16, `^IT` 1-32; at 50 and 50 nothing is added, so the default voice is the cartridge's own), the volume as gain, a capital's pitch; the shared 7-bit text path (`../accent_text.c`); four rules for what this program does with a screen reader's text (its control character `^` and its "quote" taken out, a hyphen made a space unless a digit follows, a space before a period that follows a digit, or it says "point"); long text in parts of at most 200 characters, cut at sentence ends, then clause marks, then spaces. Numbers are the program's own by default. |
| `test_sv_voice.c` | The voice on the real program (12 tests): the setting steps, the text path, the four rules, the splitting, nothing added at the defaults, rate, pitch and a capital at the chip, speaking, 600 characters in parts, "Page 1." without "point", cancel then speak, speaking over a text, the volume. |
| `sv_render.c` | The C API alone: the firmware folder and a text into a WAV (`sv_render <folder> "text" out.wav [--log]`). |
| `sv_dsk.h`, `sv_dsk.c` | The program from what a person brings: `SUPERTOK.BIN` itself, or the Disk BASIC image holding it (the Color Computer Archive's `SUPERV.DSK`) -- the directory read, the granules followed through the allocation table (track 17 skipped), the last sector's length from the directory; accepted only when it is the known program, by its sha256. Every platform's download and import goes through it (`tools/check_sv_voice.py` tests it on a disk laid out around the program; its controls `SV_DSK_BREAK=track17` and `=last`). |
| `test_sv_host.c` | The host on the real program (10 tests): the golden phonemes of two texts with the program's opening and closing writes, speech starting at once (and at the machine's 103 ms with the skip off), a text after another one the same as on a new host, the program's control codes reaching the chip, a sentence cut at eleven places and the text after each (whole, on time, nothing of the cut phoneme before it), a text's first phoneme on its pitch (no climb), the same writes whatever the block size, the guard on a chip that never requests, the refusals and the 240 characters, a changed byte refused. Skips (77) without the file. |
| `sv_controls.py` | Both tests as they are, then their must-fail controls (11 host rules, 11 voice rules): each rule undone in a scratch copy of its file, exactly its tests must fail. Run by `nvda/tools/run_tests.py` and `tools/linux_tests.sh` when the file is in its drop-in folder. |

## What the program does, and what the host rests on

- **The interface** is the manual's (p.19): `DEFUSR1=&H5805 : X$=USR1(PHRASE$)`, "multi-line input", which speaks one
  line and returns. The program copies the text to 02DDh on (BASIC's line buffer), converts as it speaks, and waits
  for the chip by reading FF80h in a loop before each phoneme. Its other entries, 5800h and 5807h, are the keyboard
  modes: they call BASIC's line input and character output, and are not used.
- **The settings** are control codes in the text, an up arrow (5Eh) and a letter (the manual, pp.21-22): `^Vn` volume
  0-15, `^Sn` rate 1-16, `^ITn,m` inflection 1-32 and its speed 1-8, `^An` articulation 1-8, `^Fn` filter 1-255,
  `^C` the defaults, `^P` phonemes by name, `^N` sung notes. The test checks three of them at the chip.
- **The clock**, 894,886.25 Hz for the 6809's E and for the chip, is inferred, not measured: the program's own note
  table is in tune at that clock (A = 55 Hz within 1.04 cents over its lowest octave) and almost two semitones sharp
  at 1 MHz. The cartridge connector carries that clock (E on pin 6, "0.89 MHz", Q on pin 7: Tandy's Color Computer
  Technical Reference Manual, 1981, Table 5), which shows it was at hand, not that the chip was wired to it. No
  schematic or measurement of a cartridge was found.
- **D7 high is a request**: Steve Ciarcia's article on the chip (Byte, March 1984) says so, and the program's loop
  (`TST $FF80`, `BPL`) waits for exactly that.
- **A cut text poisons the next one** on the machine: the program keeps the last phoneme it wrote and writes it
  again while it sets the chip's mode, so after a cut in a long pause the chip stops requesting and the next call
  never returns. That is why every text starts from the program as loaded.
- **A cut phoneme stays in the chip.** Cancel takes the amplitude away as the program's own end does, but the phoneme
  runs on, and the next text's opening gives the amplitude back: measured, the next text came up to 82 ms late with
  the old phoneme sounding first. So after a cut the host loads a pause and runs the chip's time to its end,
  unheard, before the next text. That text's first phoneme is then written at once, and it starts from silence:
  the same quiet rise at every cut tried, whatever was cut.
- **The first word climbed.** The chip glides to a text's pitch from wherever its counter was, the bottom of its
  range on a new chip. Tomi heard it in the first clips, as on the Mockingboard before the same remedy: until a
  text's first phoneme, its pitch lands at once.

## Not done

- The golden phonemes are this core's own: no second 6809 has run the program.
- No recording of a real Super Voice has been compared, and none was found.
- The chip's wiring (which clock pin, DIV2) is unknown; for the chip model both candidates are the same number.
