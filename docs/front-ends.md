# The front ends

A front end is the program that turns text into SSI-263 register writes. None of the ones
here was rewritten: each is the device's own code, run on an emulated copy of the machine it
was written for, with the emulated chip in place of the real one. The chip's A/R request
drives each program back, so each one paces itself exactly as it did on the hardware.

**Which CPUs run them.** This page describes the first hosts: Unicorn for the Speak-Out and
the Accent-mini, z180emu for the Braille Lite, and a plain-Python 8085 for the Accent-PC.
Since 0.7, every shipping front end runs instead on MAME's extracted CPU cores
(BSD-3-Clause), in portable C, with no GPL runtime in any package:

| Front end | CPU core (`src/csrc/cpu/`) | Board (`src/csrc/`) |
|---|---|---|
| Speak-Out | MAME's NEC V40 | `speakout/` |
| Braille Lite 2000 and the other Blazie units | MAME's Z180 | `blazie/` |
| Accent-mini | MAME's 8086, under a small PC (`pc86/`) | `accentmini/` |
| Accent-PC | MAME's 8085 | `accentpc/` |
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
- **The Spanish release and commas at digits:** the Spanish Braille Lite 2000 (September 2000)
  reads its own numbers a digit at a time, and its reader goes wrong when two commas touch a
  digit in one sentence. It then says digits again that it had before, and more each time it
  meets such a line: "1, 2, 3, 4, 5, 6" took 6.8 s, then 10.9 s, then 15.0 s; "12, 34, 56"
  took 18.4 s, then said nothing, then 22.5 s. Two decimals in a sentence do it ("3,5 y 4,5"),
  and so does "1,234,567". With the number words on, our words take most digits away, but a
  digit after a letter is left to the unit, so "MP3, MP4, MP5" did it too. The other 21 units
  with a voice read such lists the same every time. The voices therefore send that release
  no comma that touches a digit: one between two digits becomes a space (the unit says "3,5"
  as "tres cinco", never "coma"), one beside a digit gets a space on that side, which keeps
  its pause (`bl_voice.c` `detach_commas`, `blazie.py` `_commas`, the release table's `commas`
  column). This is the emulated unit's behaviour, the same through two hosts; it has not been
  seen on a real Spanish unit.

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

## Aicom Accent-PC

- **The card:** Aicom's plug-in Accent for the PC, which also has a serial port and a power connector for
  stand-alone use; the serial port is how it is driven here. Through 0.9.2 this voice was called "Accent SA":
  the ROMs were taken for the stand-alone box's. They are the card's ([history.md](history.md), 2026-11-20).
  Unlike the mini, it thinks for itself: an
  Intel 8085 runs Aicom's rules from a 64 KB program ROM ("AICOM CORPORATION COPYRIGHT,
  WORLD, 1986, 1987, 1988, 1989"), with two 32 KB exception dictionaries ("AICOM
  COPYRIGHT1" and "2"; letters stored as ASCII minus 21h).
- **The machine:** `src/hosts/accent_pc.py`, on `src/hosts/i8085.py`, a plain-Python 8085
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

## Artic SynPhonix 215 (research host)

The host is `src/hosts/synphonix.py`, in Python on MAME's 8086 (`pc86`). It is a research host only: no C port, no
voice and no front end lists it yet. It runs Artic's own DOS text-to-speech for its internal cards, `VEST.COM`
("Visions Enhanced Speech Translator", version 1.60 English, the "Special Release For Artic Visions" that Business
Vision 3.08 loads first), unchanged. `VEST.COM` is not in the repository; its drop-in folder's README
([`firmware/artic-synphonix/README.txt`](../firmware/artic-synphonix/README.txt)) says what goes there. Its first
clips were heard on 22 November 2026: at XCK = 1 MHz the speed sounded close to a real TransPort's, whose own VEST
should be much the same program. That is an ear, not a measurement.

- **The card:** an ISA card with the Artic-263 and little else. The text-to-speech, its letter-to-sound rules,
  numbers and lexicon are all in `VEST.COM`, a TSR on the PC. Business Vision (`BVISION.COM`) and other programs
  give it text.
- **The machine:** the host loads `VEST.COM` as DOS would, with no switches (IRQ 7, as `BV.BAT`), until it goes
  resident. It hooks INT 8, 9 and the card's IRQ 7. The card, read from VEST's code and confirmed by it speaking
  (addresses are the `.COM`'s, CS:0100 being the file's first byte):

  | Port | What it is |
  |---|---|
  | 2E0h–2E5h | the card's key: "AR" (41h 52h), then four bytes of the card's serial number, each inverted, two decimal digits a byte. VEST takes the first card it finds from 2E0h down to 0E0h in steps of 20h (the MAP jumper) |
  | 2E8h–2ECh | the chip's R0–R4, eight above the key. Every byte is rotated left one bit before it is written (0D27: `NOP; ROL AL,1; RET`): the card's data lines reach the chip one place over. VEST rotates only when the key's first two digits are 25, a "2X5 type" card |
  | 2E8h, read | the switch interface's pedals, bits 6 and 7, read only when Business Vision turns its turbo on; the host reads them high (no pedal pressed), a guess |
  | IRQ 7 | the chip's A/R. The handler (0AFA) sends the next frame, R1 to R4 and then R0, with a delay loop between writes, and ends with a specific EOI (67h). With nothing queued it keeps sending pauses at the slowest rate |

  Before it settles on the ISA card VEST looks for three others: a Micro Channel card through the PS/2 POS
  registers (adapter ID FF70h), a card at 2F8h on IRQ 3, and a key in an option ROM between C000h and EC00h. Which
  SynPhonix models these were is not shown; the host offers none of them.
- **The serial check:** each copy of VEST holds the serial of the card it was sold with, encoded at 09BC. With a key
  of any other serial it says nothing and sends every write to 3F0h instead. About every 143 interrupts the handler
  also re-reads one key byte and ends the program if it looks wrong. The host gives the card the key its copy of VEST
  expects: it finds the comparison's code in the file and reads the encoded serial back, so no serial is written in
  the repository and another copy finds its own card (`tools/check_synphonix_host.py`, "another copy").
- **How programs reach it:** this special release does not install the SONIX software interrupt (the user vectors
  60h–67h). Business Vision instead searches memory, a paragraph at a time from 0100:0, for VEST's signature (words
  0038h at 01E0, 1D2Dh at 01E2, 7A83h at 012A) and calls far, after PUSHF, the address at 01E4 (0558). AL selects
  the function:

  | AL | What it does |
  |---|---|
  | 7 | speak ES:SI, DI characters, with CH, CL, DH and DL the rate, amplitude, pitch and filter offsets VEST adds to its frames |
  | 1 | cut speech: the queue emptied and a silent R0 written |
  | 3 | set those offsets alone |
  | 4, 5 | send at once, or queue, one frame of five register values |
  | 6 | queue a pause |
  | 0 | reset the chip (the queue is left) |
  | 2 | read status: AH = 1 while idle |
  | 0Eh | how the switch interface's pedals act (BL, BH: what each pedal does to the rate and amplitude) |
  | 0Fh | the pedals' last reading |
  | 10h | whether the text module was stopped mid-call |

  AL=4 and AL=5 are raw frames, the only ones in either program: AH goes to R0, BL to R1, BH to R2, CL to R3 and
  CH to R4. AL=4 writes them at once; AL=5 queues them behind speech, with the offsets below added. Business
  Vision itself calls AL=5 (BVISION 5155, not run).
- **What a caller can change:** VEST has no number processor and no in-text commands but two.
  - *Numbers* are always read digit by digit; "$" is read "dollars" before the number, "%" "percent", "&" "and".
    Business Vision's Number Processor is Business Vision's own: the number words ("hundred,", ".- thousand,",
    ".- .- million,") are in `BVISION.COM` and `TVISION.COM`, not in VEST. A voice on VEST would bring its own
    number words, as the SmarTalk's does.
  - *Pitch steps in the text:* a word that is exactly `.+` or `.-` raises or lowers the pitch of what follows by 8
    on R1 (up to 78h), from the start of each call. Business Vision's number words use them.
  - *Offsets, with AL=7 or AL=3:* CH is added to R2's rate nibble (saturating at E); CL to the amplitude (saturating
    at F; 10h and above count down, never below 1); DH, times 8, to R1 (20h and above count down); DL is written as
    R4, the filter. At a rate offset of 5 or more VEST makes the pitch flat by itself.
  - *AH, an option byte with AL=7:* bits 0–3 each switch on one of four exception dictionaries, bit 4 another kind
    of dictionary in the segment passed in BX (its entries not decoded), bit 6 flat inflection (Business Vision's
    "Inflection Flat"), and bit 7 makes VEST queue nothing. The four dictionaries belong to the caller: it writes
    their segments into the text module at 110h–116h (Business Vision at BVISION 106B). Each holds a count (word 0),
    the binary search's first step (word 2), then from index 1 four bytes an entry (the text's offset, the word's
    length, the replacement's length), the word in capitals followed by its replacement, which VEST reads through
    its rules. Only the first word of each call is looked up; Business Vision sends one word a call.
- **Its hooks:** INT 9 cuts speech on Alt (scan code 38h) or Ctrl then X (the words at 1E0h and 1E2h, which
  Business Vision rewrites with its own keys and also uses to find VEST); `/K-` leaves the keyboard alone. INT 8
  is a watchdog: after 10 timer ticks with no A/R it restarts the chip with its start-up writes.
- **Its switches:** `/I=2`, 3, 5 or 7 (the IRQ), `/B=0` to 9 (the queue's size), `/E` (load into EMS), `/K-`, and
  `/!`. Run a second time, it prints a report of the copy already loaded. Its "PortTalk Module Loaded" line only
  says that a program has claimed VEST (18Ch–18Eh, set by Business Vision): VEST parses no PortTalk, and a Ctrl-E in
  its text is dropped.

- **Its start-up:** R2 = F8h, R4 = E9h, R3 = 82h, R0 = EBh, R3 = 52h (mode 3, transitioned inflection), then it
  speaks its own announcement, "Visions Enhanced Speech Translator. 1 point 6 0", with rate offset 2 and filter E6h.
  The host's `say()` uses the same offsets by default, and `boot()` lets the announcement play out silently.
  Typical frames carry articulation 5, amplitude about A, filter E6h and rates B to E.
- **The TransPort, as TVISION drives it.** `TVISION.COM` was run with an emulated COM1, answering it by hand
  (`tools/artic/tvision_serial.py`, research only); what it sent:
  - *Start-up:* 9600 baud, 8 bits (divisor 12, LCR 03h). A space; a BREAK for 5 timer ticks; `^E$ ` (reset);
    `^EN0 ` (the TransPort's number processor off, Business Vision doing its own); `^E~? `, to which it
    wants a reply starting "TP" within about a second (an identity: "Artic TransPort is not responding" otherwise);
    Ctrl-A `Q` and a carriage return; then `^E~*` and three bytes made from the timer tick, a challenge: the
    TransPort answers with its serial number scrambled by them (16 entries of two nibble bytes, with padding), and
    TVISION refuses to load unless it matches the serial in TVISION. In the stand-in that comparison was skipped.
  - *Each utterance:* `^EW ^EIZ ^EQ `, then it waits for one byte back (any but XON or XOFF): by the manuals, wait
    for the speech before it, set index marker Z and ask for it, so the byte comes once the last utterance is
    spoken; then `^EZ` and seven
    characters, three digits (the voice's rate, pitch and volume, in some order), the tone as a letter and two
    characters of option bits (40h plus the bits); then the text one word at a time, each ended by a carriage
    return, with its `.+` and `.-` inside.
  - *Other:* Ctrl-X cuts; `^E%` and a carriage return asks for the switch interface's state, answered ACK, "P" and
    two hex digits (VEST's AL=0Fh on the card); XON and XOFF pace it.
  TVISION sends no raw frames and reads no memory.
- **What Artic's manuals say.** The TransPort's (`TRANSPRT.DOC`, Appendix C, "TransPort Commands", on the
  Business Vision disk), the Mini TransPort's (`MINIPORT.DOC`, Appendix B, "Mini TransPort Speech Commands"), the
  TransType's (`TRANSTYP.DOC`) and the TransBook's (`TRANSBOK.DOC`) give one list, "provided for use by software
  developers". A command is Ctrl-E, a letter and a value, ended by a space or any control character
  (`PORTTALK.DOC`, PortTalk 88 version 1.10, page 2-9):

  | Command | What it does |
  |---|---|
  | R, P, V *n* | rate, pitch, volume, 0 to 9 |
  | T *letter* | tone, A to Z |
  | N, F, A, S *n* | number processor, flat pitch, acronyms, spelling: 0 off, 1 on |
  | H *n* | host time-out, 0 to 9 |
  | I *letter* | set the speech index marker, A to Z |
  | Q | query the index marker |
  | W | wait for speech to complete; "W followed by Q" sends the marker back once the text before it is spoken |
  | $ | reset |
  | Ctrl-X | abort speech (instant flush) |
  | Ctrl-A P | read the turbo port (the pedals) |

  The units also take Accent commands (ESC and letters, as the Accent SA's) with no switch between the two, but
  power up with other defaults than an Accent ("You must send an Accent reset"). PortTalk 1.10 for the SynPhonix
  had R, V, P, T, N and also L (line pauses, 0 to 4), not in the TransPort's list. No manual has `^E~?`, `^E~*`,
  `^EZ`, `^E%` or Ctrl-A Q, nor any phoneme, raw-frame, register or ROM-reading command, nor more about numbers
  than N on and off. The Mini TransPort's manual says VEST is in the unit: "VEST.COM is a permanent part of Mini
  TransPort and starts every time you turn it ON" (`MINIPORT.DOC`, Chapter 1, "Controlling Mini TransPort with
  Business Vision"); the TransPort 615's lists the "VEST Text-to-Speech Algorithm" (`TRANSPRT.DOC`, Appendix D).
- **A Mini TransPort, over its serial port** (22 November 2026, 9600 baud 8N1, `MPSET.DOC`'s default; every byte
  back logged, the speech heard by its owner, not recorded). The unit's software version was not read.
  - `^E~? ` is answered within 60 ms by "TP" and eight digits, with no carriage return. The digits are not written
    here.
  - `^EIZ ^EW ^EQ ` after a text is answered "Z" when the text has been spoken: four times "Hello, this is a test of
    the TransPort." took 2.90 to 2.92 s from its first byte to the "Z". `^EQ` alone answers the last marker set at
    once; W and IZ alone send nothing and change nothing heard.
  - "It costs $12.50 for 1995 items." with `^EN1` was heard as "costs twelve point five zero dollars"; with `^EN0`,
    "dollar one two point five zero", digit by digit with "dollar" first, as VEST reads numbers on the card. How
    "1995" was read was not noted.
  - "one .+ two .+ three .- four", one word a line (as TVISION sends it) and on one line, was heard as "one two
    three four" both times: the marks are not read out. Whether the pitch stepped was not judged.
  - `^EZ ` with no characters after it made the next sentence take 5.2 s instead of 2.1 s, and the speech stayed
    slow until `^E$ `. That fits Z carrying the rate (TVISION's three digits) with nothing read as the slowest;
    Z with digits was not tried.
  - `^E$ ` brings the rate back; the pitch after it was a little lower than at the start of the session.
  - Ctrl-A Q and a carriage return is answered ACK "Q+" CR, then ACK "PFF+" CR; Ctrl-A P and a carriage return
    (the manual's "Read Turbo Port") ACK "PFF+" CR, with nothing pressed. Both are framed as STANDCOM's packets are
    (Ctrl-A, a letter, a carriage return). `^E%`, ended by a carriage return or a space, is not answered: on the
    Mini TransPort the pedals are read with Ctrl-A P. Whether a TransPort 615 answers `^E%` was not tried.
  - Heard against the research host (XCK = 1 MHz, the same sentence, 2.46 s of sound), the speed was the same and
    the unit's pitch lower, after `^E$ `. A different clock would move the speed and the pitch together, so the
    lower pitch is most likely the TransPort's default pitch setting, not its clock. That is an ear and a timing
    through the serial port (which holds the unit's own delays), not a measurement of its audio.
- **The other programs on its disk:** `SETVEST.COM` "calibrates VEST for faster computers" with a number from 1 to
  255, probably the delay between register writes (100 in the file, at 01DE); not checked. `TVISION.COM` is
  Business Vision for the TransPort on a COM port, and the disk's `TVMOD.EXE` (a plain zip) carries `TPSET.COM`,
  Artic's 1992 utility for the TransPort's serial settings.
- **Checks:** `tools/check_synphonix_host.py` (in `nvda/tools/run_tests.py` and `tools/linux_tests.sh` when
  `VEST.COM` is in its folder): VEST loads and finds its card with its own start-up writes, its announcement and a
  sentence with numbers and money give their golden phonemes, a copy with another serial finds its own card, and a
  cut leaves nothing spoken after it. Five must-fail controls (`SYNPHONIX_HOST_BREAK`): a key one digit off, the
  bytes not rotated back, the specific EOI ignored, a cut by AL=0, and the key made once per process.
- **Not known or not done:** the SynPhonix's chip clock, measured (see [open-questions.md](open-questions.md), "The
  clocks"; a Mini TransPort's speed by ear matches 1 MHz); what Business Vision's Rate, Pitch, Volume and Tone
  settings send as offsets; the second dictionary's entries; the order of `^EZ`'s characters and what `~?`'s
  digits and Ctrl-A Q's "Q+" mean; and Business Vision itself, which was not run.
