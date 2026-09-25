# The four front ends

A front end is the program that turns text into SSI-263 register writes. None of the four
here was rewritten: each is the device's own code, run on an emulated copy of the machine it
was written for, with the emulated chip in place of the real one. The chip's A/R request
drives each program back, so each one paces itself exactly as it did on the hardware.

All four write the same five registers:

| Register | Bits | Meaning |
|---|---|---|
| R0 | 7:6, 5:0 | duration mode, phoneme code |
| R1 | 7:0 | inflection (pitch) I10–I3; in the mode the speech boxes use, bits 2:0 are the glide rate and 7:3 the glided target |
| R2 | 7:4, 3:0 | speech rate; inflection I11 (bit 3) and I2–I0 |
| R3 | 7, 6:4, 3:0 | CTL (power down / mode latch), articulation rate, amplitude |
| R4 | 7:0 | filter frequency (FF): the switched-capacitor clock, fc = XCK / (2 (256 − FF)) |

Naming rule, adopted after the documents disagreed with each other: registers and bits, not
document labels.

## Speak-Out (GW Micro, 1995)

- **The box:** a talking box by Daniel Weirich (hardware) and Douglas Geoffray (software) of
  GW Micro. The same tables, byte for byte, are in GW Micro's Sounding Board card driver.
- **The firmware:** `SPEAKOUT.HEX`, Intel HEX for an NEC V40 (an 8088-compatible with its
  own interrupt controller and serial unit), loaded at 0000:0100–7F6D.
- **Inside:** 1152 letter-to-sound rules in the NRL notation (Elovitz, 1976) at 0x5318, and an
  86-entry allophone table at 0x7BD1 that turns internal phonemes plus context into R0 bytes
  with their duration bits. Settings: rate from a table at 0x388E into R2's high nibble,
  pitch n into R1's high nibble as n+2, volume n into the amplitude as n+3, tone letter into
  R4 as DBh + letter.
- **The machine:** `src/hosts/speakout.py`, under Unicorn. The V40's interrupt controller is
  reduced to mask, in-service and EOI; its serial unit takes text on IR1 (INT 9); the chip's
  A/R is IR4 (INT 0Ch); the SSI-263 is memory-mapped at F000:FE00–FE04 (R0–R4).
- **Checks:** its own "speak out ready" greeting against a real Speak-Out recording: F0
  88.72 Hz real, 88.78 Hz emulated, which confirmed XCK = 1 MHz. Its self-test's section
  timing matches the real box to about 0.1–0.2 s.
- **Open:** real Speak-Out phrases run 11–13 % longer than ours, while the Braille Lite
  matches the data sheet to 0.5 %. Host scheduling was measured and does not explain it.

## Braille Lite 2000, speech-box mode (Blazie Engineering)

- **The unit:** an 18-cell Braille Lite 2000 from before the Millennium, the model with the
  SSI-263 (the Millennium used a DoubleTalk). In speech-box mode it speaks what arrives on
  its serial port, as an external synthesizer.
- **The firmware:** the June 2003 build, `BL2ENG.BNS`: Z80-family code for a Z180, not
  compressed. Its front end is plain data: about 1190 rules in the form
  `left-context [letters] right-context = phoneme bytes`, the same NRL notation, where the
  phoneme bytes are R0 values (low 6 bits the phoneme, top 2 bits the duration mode).
- **The machine:** z180emu, with this project's board file `bns.c`, built as `bns_live.exe`
  and driven over a pipe by `src/hosts/blazie.py`. The SSI-263 is on I/O ports C0–C4 (R0–R4)
  and A/R is wired to /INT1. The interrupt handler sends one phoneme per interrupt and, before
  every byte, rewrites R3, R2, R1 and R4, then primes R0 with C0h (PA) about 0.5 ms before
  the real phoneme. The CPU clock is 6.144 MHz (the serial divisor gives 9600 baud, the
  timer 10 Hz). A warm snapshot of the booted unit starts it in well under a second, and
  the boot types the unit's own speech-menu chords (punctuation none, full numbers).
- **First ear check:** the rules have three malformed records that the emulator predicted
  never fire, so "correct" should come out as K OU ER R EH K T. On the real unit it does:
  "core-wrecked".
- **Protocol lessons from the real unit:** it holds each line and speaks it when the next
  transmission arrives, while ^E settings act at once (so every line was spoken with the
  next line's settings until the harness sent an empty flush line after each one). ^X cuts
  only the current word. The firmware reads numbers in full up to 999,999,999,999, but has
  no "trillion".

## Aicom Accent-mini

- **The card:** the Accent-mini (and its siblings, the 1600, XE, SX and L40) carried only the
  AI901, Aicom's name for the SSI-263. The text-to-speech ran on the PC, in Aicom's DOS
  device driver `SPKEMS.DVC` ("Accent-EMS" V4.5, 1986–1993), which keeps its rules in
  expanded memory. It shares most of its code with the Messenger-IC's driver.
- **The machine:** `src/hosts/accent.py` loads the driver as DOS would, under Unicorn: an
  INIT request packet to the character device "SPK", a fake EMM.SYS ("EMMXXXX0", page frame
  D000h, the EMS functions it calls, including 56h Alter Page Map & Call), an AT machine ID,
  and BIOS IRQ vectors that send EOI. Text goes in the way a screen reader sent it: INT 17h,
  one character at a time, to the LPT the driver answers for.
- **The card's ports** (Accent-mini defaults): 3EFh data; 3EEh control, where (68h|reg) then
  (70h|reg) latches the data into a register and bit 7 is the interrupt enable (C0h while
  speaking, 40h when done); 3EEh read is status, bits 0–1 set on A/R. IRQ2 = A/R AND the
  enable, latched on the rising edge, and dropped if the line goes low before it is served,
  as a real 8259 does.
- **Hard-won details:** under its power-up mode (ESC =K) a speech option command blocks
  inside INT 17h until the speech buffer drains, so the host must keep the chip and IRQ
  running while the driver waits. The driver's INIT (0.58 s, 43,000 EMS calls) runs once at
  build time and is saved, so the add-on starts in about 25 ms.
- **Checks:** its "Accent ready" greeting matches the real card's recording in pitch, length
  and glide. The Accent reads 100 as "one zero zero", which its manual confirms is the
  Accent's own behaviour.

## Aicom Accent SA

- **The box:** the stand-alone, serial Accent. Unlike the mini, it thinks for itself: an
  Intel 8085 runs Aicom's rules from a 64 KB program ROM ("AICOM CORPORATION COPYRIGHT,
  WORLD, 1986, 1987, 1988, 1989"), with two 32 KB exception dictionaries ("AICOM
  COPYRIGHT1" and "2"; letters stored as ASCII minus 21h).
- **The machine:** `src/hosts/accent_sa.py`, on `src/hosts/i8085.py`, a plain-Python 8085
  written for it (it passes the TST8080 and 8080PRE CPU tests; MAME's i8085 was the
  reference for what RIM returns after a TRAP). The map, read from the code and confirmed by
  the firmware speaking:

  | Address or port | What it is |
  |---|---|
  | 0000–77FF | the program ROM's lower half |
  | 7800–7FFF | 2 KB of RAM (stack at 8000h) |
  | 8000–FFFF | a 32 KB window, banked by port 40h bits 0–1: 0 = the program ROM's upper half, 1 and 2 = the dictionaries, 3 = empty |
  | ports 03–07 | the SSI-263, reversed: 03 = R4 ... 07 = R0; reading 07 gives A/R in bit 7 |
  | ports 20h/21h | an 8251 USART (data; mode, command, status). RxRDY is RST 6.5; RTS is its flow control |
  | port 40h | out: the bank, and bit 4, which gates A/R onto TRAP; in: option switches |
  | port 80h | a parallel input on RST 5.5 (unused here) |

- **The key to it speaking:** TRAP is A/R AND port 40h bit 4. The firmware sets the bit when
  it has phonemes queued and clears it when the queue runs dry; its TRAP handler (4E68h)
  writes the next five registers and refills the queue. With A/R wired straight to TRAP, an
  idle request walks the queue's read count past its write count, and it never speaks.
- **Its power-up:** it times a clock on RST 7.5 to choose a mode. With none, it runs in the
  mode the boot code calls 3. It says "ACCENT READY." (the message table at 681Dh also holds
  its version history string).
- **Speed:** the 8085's clock is a guess, 3.072 MHz. The SA reads a whole sentence before its
  first phoneme, which takes about half a second at that speed; the host runs the CPU eight
  times faster only in that gap, so speech timing, which A/R paces, is unchanged.
- **Behaviour:** the same ESC command set as the mini (rate, pitch, voice, inflection; ESC =F
  and ESC =M; Ctrl-X flushes), and it also reads 100 as "one zero zero".
