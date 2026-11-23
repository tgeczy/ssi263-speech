# src/csrc/synphonix: Artic's SynPhonix 215, through Artic's own VEST.COM, on MAME's 8086

The SynPhonix 215 is an ISA card with an Artic-263 (an SSI-263) and little else: its text-to-speech is Artic's DOS
TSR `VEST.COM` 1.60 ("Visions Enhanced Speech Translator", the "Special Release For Artic Visions" that Business Vision
3.08 loads first), which runs on the PC. This folder is that card and its program in C, held write for write to the
research host it ports, `src/hosts/synphonix.py`. How the card and VEST work, as read from VEST's code:
[`docs/front-ends.md`](../../../docs/front-ends.md), "Artic SynPhonix 215".

`VEST.COM` is Artic's work: it goes in `firmware/artic-synphonix/` (its README says which file), and nothing here
builds it in. Each copy holds the serial of the card it was sold with; the host reads the key that copy expects from
the copy's own serial check, so any copy of VEST 1.60 finds its card and no serial is written here.

| File | What it does |
|---|---|
| `sp_host.h`, `sp_host.c` | The host: `synphonix.py`'s `SynPhonix` in C, line for line, on `../pc86` (MAME's 8086). The bare PC (BIOS, the DOS services VEST asks for, the PIC), the card (the key at 2E0h, the chip eight above with each byte rotated back, A/R on IRQ 7 and VEST's specific EOI), loading `VEST.COM` until it stays resident, finding its entry as Business Vision does, and the far calls: say (AL=7, with VEST's rate, amplitude, pitch and filter offsets), cancel (AL=1) and any other. `SYNPHONIX_HOST_BREAK` (`key`, `rotate`, `eoi`, `cancel`) puts the Python host's must-fail controls back here too. |
| `test_sp_host.c` | The C host driven as `tools/check_sp_host.py` drives the Python one: every chip write printed, for the comparison. |

Checks (both need `VEST.COM` in its drop-in folder and skip without it; Windows, built with w64devkit):

- `tools/check_sp_host.py`: the C host against `synphonix.py`: boot, three sentences (numbers, money, a time), a
  cancel mid-sentence and the sentence after it, every chip write the same (register, value, chip time).
  `SP_HOST_BREAK=1`, its control: the reference's chip runs 1 % fast, and the writes differ.
- `tools/check_synphonix_host.py`: the Python host's own check and its five controls.

Not yet here: the voice (its front end, number words and settings) and the platforms.
