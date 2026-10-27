# The front ends

A front end is the program that turns text into SSI-263 register writes. None of the ones
here was rewritten: each is the device's own code, run on an emulated copy of the machine it
was written for, with the emulated chip in place of the real one. The chip's A/R request
drives each program back, so each one paces itself exactly as it did on the hardware.

**Which CPUs run them.** This page describes the first hosts: Unicorn for the Speak-Out and
the Accent-mini, z180emu for the Braille Lite, and a plain-Python 8085 for the Accent SA.
Since 0.7, every shipping front end runs instead on MAME's extracted CPU cores
(BSD-3-Clause), in portable C, with no GPL runtime in any package:

| Front end | CPU core (`src/csrc/cpu/`) | Board (`src/csrc/`) |
|---|---|---|
| Speak-Out | MAME's NEC V40 | `speakout/` |
| Braille Lite 2000 and the other Blazie units | MAME's Z180 | `blazie/` |
| Accent-mini | MAME's 8086, under a small PC (`pc86/`) | `accentmini/` |
| Accent SA | MAME's 8085 | `accentsa/` |
| Sounding Board | MAME's 8086, under the same small PC | `soundingboard/` |
| SmarTalk | MAME's 8086, under the same small PC | `smartalk/` |
| Mockingboard | Fake6502 (public domain, from Jayson Smith's EchoTalk) | `mockingboard/` (see [mockingboard-firmware.md](mockingboard-firmware.md)) |

The earlier hosts remain only as development references and legacy checks. What each
firmware does, and what the machine around it must provide, is unchanged, so the sections
below still hold.

All of them write the same five registers:

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
  GW Micro. The same tables, byte for byte, are in GW Micro's Sounding Board card driver
  (below).
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

### The Sounding Board (GW Micro's PC card, 1988–96)

- **The card:** an ISA card with an SSI-263 (the LT and XE were versions for Toshiba
  laptops). The desktop card also has 16 KB of RAM that SBLOAD can load into (`/!MS`), and
  it and the LT have a tone knob that, the manual says, boosts a high frequency range.
  The text-to-speech ran on the PC, in two DOS TSRs by Douglas Geoffray, version 2.7:
  `SBLOAD.COM` ("Text-To-Speech in DOS") and `SB.COM` (the card driver). Both are packed
  .COM files that unpack themselves as they start.
- **The same front end:** between them the two programs carry the Speak-Out's rule and
  allophone tables byte for byte (SB.COM 1000–16EB is the Speak-Out's 5318–5A03, SBLOAD
  1A9D–3C5F is 5A03–7BC5, SBLOAD 3C60–3F52 is 7BD0–7EC2). Fed the same text, the two hosts
  send the chip the same frames, register for register (134 frames for a two-sentence test,
  381 for one full of numbers, dates, money and acronyms). The settings match too: rate 5,
  pitch 3, volume 9, tone I.
- **The card's ports:** R0–R4 at base+0 to base+4, base 300h by default (`/!Bn`, n × 16);
  A/R on IRQ2 by default (`/!In`: 2, 3, 4, 5 or 7). The IRQ handler sends one frame per
  request exactly as the Speak-Out's does (R0 = 00, R1, R2, R3, R4, then the phoneme byte),
  and the start-up writes the Speak-Out's sequence (R2 F8, R4 E9, R3 82, R0 EB, R3 52). The
  XE (`/!BT`) maps the registers out of order into the COM2 range: R0 2FA, R1 2FB, R2 2F8,
  R3 2F9, R4 2FE.
- **The machine:** `src/hosts/soundingboard.py` loads SBLOAD and then SB as DOS would, each
  running until it stays resident, on MAME's 8086 (`pc86.py`) or Unicorn, with a BIOS timer
  (SB waits for 18 ticks at start-up) and enough of DOS for the two. Text goes in as a
  screen reader sent it: INT 17h to LPT3. It greets "Sounding board version 2.7" as it
  loads. `tools/check_soundingboard.py` keeps the identity as a test: three sentences through
  both hosts, frame for frame (the Speak-Out's idle frames between utterances left out), and
  a control that must differ.
- **Where the products differ:** not in the rules. On the hardware, the card's analog output
  and tone knob, and how fast the host answers each A/R (a PC against the Speak-Out's V40).
  Older driver versions (Sounding Board 2.0 and earlier) are not checked.

## Braille Lite 2000, speech-box mode

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
- **Two speech-box modes:** all six dots without space enters word mode; space plus all six
  dots, then L, gives line mode, which the add-on uses. In word mode almost every "?" rise and
  "." fall is lost; in line mode they come through, and the add-on follows the unit (M03,
  2026-09-28, in [history.md](history.md)).
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

## Automated Functions SmarTalk

The host is `src/hosts/smartalk.py`, in Python on MAME's 8086 (`pc86`), and `src/csrc/smartalk/st_host.c`,
the same host in C, line for line (`tools/check_st_host.py`: every chip write the same). The voice,
`st_voice.h`, gives it NVDA's settings and the shared text path; the 0.8 add-on carries it as the "SmarTalk"
firmware type when `S_TALK.EXE` is in the local firmware folder (`tools/check_st_voice.py`). Since 0.9.0 it is a
voice on every platform: NVDA, SAPI, Linux, Android (the GitHub APK carries it) and the Apple app (where it is
imported). RetroBunn worked out the box from the TSR's own code and brought it in (PR #24); the disk's own
`SMARTEST.EXE` 1.12 runs its hardware checks against the emulated box and reports "FINAL SMARTALK STATUS IS
GOOD". `S_TALK.EXE` is kept in the repository, unchanged, because its makers' disk invites sharing it; its notice
is in [`firmware/automated-functions-smartalk/README.txt`](../firmware/automated-functions-smartalk/README.txt).

- **The box:** a five-ounce SSI-263 on the PC's parallel port (Automated Functions, Arlington,
  Virginia, 1995), with nothing to think with: the text-to-speech is their DOS TSR, version
  1.21 (1995-11-15). `S_TALK.EXE` is the whole of it in conventional memory, its rules and
  exception dictionary included; `XS_TALK.EXE` is the same with the tables in XMS. A screen
  reader prints to it on INT 17h, in the Accent's commands or SmarTalk's own (`TECHMAN.TXT`
  on its disk).
- **The machine:** the host loads `S_TALK.EXE` as DOS would, with the command line the
  installer writes (`L1 I7 T0 F3 S1`), until it goes resident. It hooks INT 8, 9, 14h, 16h,
  17h, 28h and the port's IRQ7. The box, read from the TSR's code and confirmed by it
  speaking (addresses are the EXE image's):

  | Line | What it is |
  |---|---|
  | data (378h) | the byte for the chip |
  | control bit 0 | /STROBE: the data latches into the selected register when it is released |
  | control bits 1–3 | the register, from the table at 0C2D: R0..R4 = 03, 01, 07, 05, 0D (with the strobe bit). As RS0 = NOT bit 1, RS1 = bit 2, RS2 = bit 3 they are 0–3 and 7; the SSI-263 takes any address with RS2 set as R4 |
  | control bit 4 | the port's interrupt enable, set after each R0 |
  | ACK (status bit 6) | A/R, high while the chip requests; IRQ7 on its rising edge |
  | ERROR (bit 3) | INIT (control bit 2), looped back: every 6 timer ticks while idle (54 while speaking) the timer handler flips INIT and expects ERROR to follow. An unplugged port, ERROR floating high, fails every other check |
  | SELECT (bit 4) | high once the box is ready; the power-up waits for it |
  | BUSY, PAPER-OUT | read as a powered box's levels (bits 7 = 0, 5 = 1); what drives them is not known |

- **The key to it speaking:** the rules ignore every character until a `{` arrives (15F5).
  The batch file's last program, `ST_RUN.EXE` ("allows SmarTalk to speak"), sends it: with
  the tables in conventional memory it prints "{ smartalk ready" and "Using conventional
  memory." to the TSR. `ST_RUN` is 286 code (POPA), which the 8086 core refuses, so the
  host's `boot()` sends the same two lines.
- **Its power-up:** data 00 and control 0B for 20 timer ticks, then data 3F for 20 more
  (the data lines likely power the box), then R4 = E3, R2 = F8, and the mode set (R0 = C0,
  R3 = DA then 5A). The TSR also calibrates its strobe delay against the timer.
- **Behaviour:** one R0 write per phoneme (the Accent-mini sends most phonemes two or three
  times), and short pauses: a sentence ends with a couple of PA phonemes. The Accent's
  ESC R, P and V map onto its own rate, pitch and tone (V5 is tone I, R4 = E4); at rate 9 it
  goes monotone. Ctrl-X flushes at once, and the settings stay. It reads one- and two-digit
  numbers as words, longer ones digit by digit, and with its punctuation off (the default) it says
  no "$", "%" or decimal point, so the voice turns dollar amounts and percents into words before
  the shared number words.
- **The voice's settings:** NVDA's rate and pitch on its 0–9 (`ESC >`, `ESC P`). Pitch 50 is its 5. Rate 50 is
  its power-up speed: the manual says rate 5, but after its boot `S_TALK.EXE` 1.21 speaks at the speed `ESC >3`
  gives, and a real SmarTalk recording is much closer to that speed, so 0–50 maps onto 0–3 and 50–100 onto
  3–9 (0.9.0). The variant is its tone A–Z (`ESC {`, I by default), "Voice inflection" as `ESC m`, and the
  volume as the audio's gain. The box's own volume is set to 9, its top step (`ESC <9`): at its
  default, 4, it was 4.2 dB under the Speak-Out, the Accents and the Sounding Board, and at 9 it is
  within 0.2 dB of them. Each command is sent only when it changes.
- **Its glides, and the snap:** the TSR runs the chip in mode 3 (transitioned inflection) and
  writes every pitch target with R1's low bits 0, the slowest glide field, in jumps of several
  steps. On the measured glide law a real chip slides just as the emulated one does, so inside a
  phrase the pitch lags its targets: that is the TSR's own sound and is kept. At a phrase's start
  it was a swoop: speech starts with a PA carrying the last phrase's pitch base, then the first
  phoneme carries this one's, so each phrase slid in from where the last ended, and a capital or a
  new pitch setting slid up late. The voice snaps both of those changes (the host feature the
  Speak-Out and Accent voices use for capitals), and nothing after the first phoneme.
  Text goes in parts of up to 160 characters, the next while about 12 phonemes are still queued,
  so no call waits on the TSR's 1,200-byte buffer.
- **Contractions (0.9.1):** the TSR ends a word at an apostrophe and spells what is left if it
  has no vowel, so "don't" came out as "don" and the letter T, and "it's" as "it" and S. A
  listener heard it. The voice now drops an apostrophe between two letters before the TSR sees
  the text, and respells the forms that would still come out wrong (I'm, I'll, we'll, they're,
  who'd and others) the way the TSR says them right; other apostrophes, as in "rock 'n roll",
  are left alone. The TSR's own setting for speaking all-consonant words (`ESC C 1`) was tried:
  it still spelled single letters and made acronyms worse, so it stays off.
  `tools/check_st_voice.py` checks all of this, with a control (`ST_TEXT_BREAK=1`) that puts the
  old text back.
