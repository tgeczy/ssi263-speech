# How the SSI-263 learned to speak again

This is the record of how a chip that nobody had emulated faithfully came to speak in NVDA,
bit by bit, and who found what. It was pieced together from the working letters between
Claude and Astra, the engine log (`src/HOLDOUT.md`) and the die-review packets, so that
none of it gets lost between conversations.

It isn't a scoreboard. Most of what worked came out of a loop: one side measured or
proposed, the other reproduced it, bounded it, predicted from it or corrected it, and the
result was written down with its confidence attached. Both sides withdrew claims along the
way, and those withdrawals are part of the record too.

## Who did what

- **Tomi** (Tamas Geczy) owns the project. He drove the real hardware, recorded it, found
  the firmware and documents, bought the chip's design paper, set the direction ("the bare
  chip first"), and listened. Every engine change from v0.2 on started with something he
  or a listener heard.
- **Astra** read the phoneme ROM from the die photographs, traced the die's metal, froze
  predictions before experiments, built the negative controls that kept the measurements
  honest, reviewed every engine version, planned the engine's structure, read the
  manufacturer documents and patents, and found the Windows-on-ARM fix.
- **Claude** measured the real chip through Tomi's Braille Lite, ran the four devices'
  firmware under emulation, made the sealed second read of the ROM, and wrote the engine,
  its C port, the firmware hosts and the NVDA add-ons.
- **Listeners** heard what the metrics missed: a "chih" in the T, a T "trying so hard", a
  click in "still".
- **Rob Elmer's Casso** made the first read of the ROM, and **Visual6502.org** made the
  die photographs everything was read from.
- **Kevin Horton** drew the schematic of the SC-02's discrete prototype, and **Henri Asseily** read it into a
  net list: the chip's circuit as its designers first built it, which gave the engine its fourth formant.

## Before this project

- **1980.** Votrax, of Troy, Michigan, publishes the SC-01 data sheet:
  - 64 phonemes on a 6-bit code, each with a fixed length at the standard 720 kHz clock (most full vowels 185 ms);
  - two pins for four pitch levels;
  - a master clock that moves pitch and speed together.

  The SSI-263 later puts pitch, speed, length, loudness and filter all under the host's control.
- **About 1982.** The SC-01-A changes "the parameters ... for a few phonemes, ostensibly to increase sound quality
  and to remove some DC bias from the output" (Jonathan Gevaryahu, from the 2007 decap). The data sheet's own cover
  photo shows an SC-01-A dated 8235.
- **1984.** Silicon Systems' SSI-263 is described at ISSCC (Maeding, Austin and Maimone).
  It descends from Votrax's SC-01, and Votrax sells it as the SC-02. The chip photographed
  later is marked "SSI 263P / P 8404".
- **1989.** The chip is sold as TDK's 78A263A: a Heath PC Voice Card carries a 78A263A-P with a 1989 date code.
  Artic Technologies' own Artic-263, the SynPhonix cards' chip, turns up with 1988, 1991 and 1992 date codes (see
  the family below).
- **1991.** The Braille 'n Speak 640 ships, around October, and work begins on the
  pronunciation fixes in its firmware, reaching users in the first updates (1991 or summer
  1992). **Cathy Hall** wrote them. Few people outside the company knew her work, and a
  former colleague asked, in 2026, that her name be recorded. Heading the list of
  mispronounced words she was given was "David", then said with a short a. The June 2003
  Braille Lite firmware this project runs still carries her fix: "David" comes out
  D A A E V I D, with a long a.
- **1996.** Texas Instruments buys Silicon Systems from TDK. TI's Integration magazine of July 1996 reports the
  signing of the agreement, for about $575 million, and a TI press release of 3 March 1997 says "In July, 1996, TI
  acquired Silicon Systems Incorporated", by then a maker of chips "primarily aimed at the hard disk drive and
  removable storage marketplaces". Not all of it went: "SSi's Communications Products Division and TDK Systems
  Division are not part of the transaction." Neither notice mentions the speech chip. Asked in 2006, neither TI
  nor TDK's semiconductor arm (later Teridian) could say anything about the 263 (redcedar's Votrax pages).
- **2007.** Jonathan Gevaryahu decaps the SC-01 and SC-01-A and reads their ROMs, the ground for MAME's die-derived
  SC-01 model.
- **Visual6502.org** photographs an SSI-263P die from chips an anonymous donor sent: Greg
  James shoots 203 images, Christian Sattler stitches them, and a 7000 × 5803 picture goes
  online with the data sheets.
- **September 2020.** Kevin Horton, who has reverse engineered and cloned most of Votrax's speech hardware, draws
  out the SC-02's discrete prototype, the board of some two hundred logic chips and an analog filter on which
  Votrax built the chip before it was silicon: seven sheets, [sc-02_Final_Schematic_V1.00.pdf](http://blog.kevtris.org/blogfiles/sc-02_Final_Schematic_V1.00.pdf).
  He builds a working clone of it that plugs in where an SC-02 goes, and later a smaller one with the logic in an
  FPGA. He remembers the prototype's ROM data as the same as the chip's.
- **August 2026.** Casso, Rob Elmer's Apple II emulator, reads the ROM's 1,856 cells off
  that picture for its Mockingboard speech, and on 2026-08-26 decodes them as
  significance-interleaved 4-bit fields (the layout the SC-01's decap had shown). Its
  code-to-hertz table is borrowed from the SC-01.

## The SSI-263's family, year by year

Every device we know of that spoke through the SSI-263, or through the same chip under another name: Votrax's
SC-02, TDK's 78A263A, Artic's Artic-263, Aicom's AI901, and the Semtech-marked SSI 263-2-2. Each entry says what puts
it in the family:

- **marking:** the chip read from a photograph of the board;
- **firmware:** the device's own program writes the chip's registers (and, where it says so, runs in this project);
- **document:** a manual, article, paper or patent names the chip;
- **ear:** someone who knows the voice recognised it in a recording.

A board's copyright year is when it was laid out, not when it shipped. A chip's date code is the year and week it
was made: 8406 is 1984, week 6. A device sits under the earliest year we can show. If you have a board photo, a
manual, a recording or firmware for anything here, or for something missing, please get in touch: the gaps are
listed under "The family of devices" in [open-questions.md](open-questions.md).

### 1982

- **Votrax's SC-02 prototype** (document). A large board marked "VOTRAX (c) 1982" is the SC-02 built from discrete
  parts before it became one chip: CMOS analog switches, op-amps and mica capacitors for the switched-capacitor
  filters, and a board of CMOS logic for the controller. ReActiveMicro published its photographs. A prototype can
  differ from the final chip, so it is a lead for the die, not a description of it.

### 1983 and 1984

- **Sweet Micro Systems' Mockingboards** (marking, firmware). Sweet Micro's first speech boards used the Votrax
  SC-01 (see "Not the SSI-263" below). From 1983 its boards carry the SSI-263: the Mockingboard A and C
  (SSP-2000C, "(c) 1983") hold an SSI 263P dated 8406, with room for a second speech chip; the Mockingboard B was the
  speech chip alone, as an upgrade. Sweet Micro's Developer's Toolkit text-to-speech ("version 1.1", March 1985),
  and the earlier text-to-speech on its disks 1 and 2, are the two Mockingboard voices in this project. The board
  lineage follows the Nerdly Pleasures blog (2022) and the boards' own silkscreens.
- **The Mockingboard D** (marking, firmware). An external box for the Apple IIc, on its serial port. Its board
  (SSP 3000 Rev B) has its own 6803 processor, 1 KB of RAM, AY-3-8913 sound chips and an SSI 263P dated 8431. Its
  2 KB program begins "(C)1984 SMSP": it takes commands from the IIc into a 16-byte buffer and writes the speech
  chip itself, on its own timer. The Developer's Toolkit disk carries the IIc's side, an "IIC TTS DRIVER" that
  sends the speech over the IIc's serial port at 9600 baud. Not built here yet.
- **The Sweet Talker II** (document, firmware). Steve Ciarcia's SSI-263 article in Byte (March 1984) builds this
  Apple card for Micromint, with a simple low-pass filter before its amplifier, and shows a Mockingboard with two
  SSI-263s that can "sing along in stereo". Micromint's own disk, "Sweettalker II, Revision 2.0", says whose voice
  it had: "(C) 1984 Micromint Inc. (by lic.)", "Algorithm (C) 1984 Sweet Micro Systems Inc." Its text-to-speech is
  Sweet Micro's. Set beside the earlier Mockingboard text-to-speech this project runs, the translator and the
  inflection routine are the same code but for a dozen bytes and one starting value (the filter frequency, 231
  where the Mockingboard's disk has 230, which makes the chip's filter clock 4% faster), and the 9,036-byte rule
  table is the same but for three rules: the Mockingboard's has one for 'S and one for Y before PING that this
  one lacks, and this one has one more for D. Only the driver is new: the Mockingboard's, rewritten to reach the
  chip at the card's slot address with no 6522 beside it. As a voice it is the early Mockingboard's, three rules
  and one filter step apart. The disk also carries songs. Read from the disk, not run here.
- **Micromint's LIS'NER 1000** (document, firmware). A speech-recognition card for the Apple II around General
  Instrument's SP1000. Its disk (Revision 1.0, dated August and September 1984) says "complete software is
  currently available for the optional SSI263 speech synthesis chip which provides unlimited vocabulary and text
  to speech", and the Sweet Talker II disk sets itself up for either card.
- **Speech Systems' Super Voice** (document, firmware). A speech cartridge for the Tandy Color Computer. Its disk's
  programs, by Del Software of Westford, Massachusetts, carry dates from April 1984 to February 1985: a
  text-to-speech translator with an exception dictionary, songs "sung by 'Super Voice'", and a demonstration
  "showing speech obtained by controlling all registers of SSI-263". Its manual calls the SSI-263 "the heart of
  the Super Voice", "memory mapped into &HFF80", and the programs write $FF80 to $FF84. One of its modes is a
  screen echo, speaking each character or word as it is printed: "It is therefore possible for sight impaired
  persons to hear the keyboard as they are writing their own BASIC programs." Read from the disk, not run here.
- **Thayer's Quest** (document). RDI's 1984 laserdisc game has an SSI-263 on its CPU board. Its service manual shows
  the voice filtered on the way out (a 3.1 kHz low-pass and a high-pass into the mixer), which is the board's sound,
  not the chip's.
- **Artic's first SynPhonix** (document). Artic Technologies' 1995 hardware manual dates the SynPhonix 200, the
  original card for the PC and XT, to 1984, and says every SynPhonix model uses the Artic-263. Artic's own
  advertisement in PC Magazine (16 October 1984) agrees on the year and names the chip: a "Speech Articulator for
  the IBM & Apple" with an "SSI263 Phoneme Synthesizer" and its SONIX software, at $125, from Auburn Heights,
  Michigan. The card was advertised there by 12 June 1984, with its SONIX-88 speech operating system. We have seen no chip from that first model.
  Artic's founders included Tim Gargagliano, its founder and president from 1984 to 1999 (his own professional
  profile), and Dale McDaniel, a founding partner and its vice president of marketing (AccessWorld, September 2000;
  see 1997 to 2003).
- **Atari's 1450XLD** (document). A computer with speech built in, never released. Atari's voice handler
  specification of 24 May 1984 says its "speech synthesis hardware ... consists of an SSI-263 speech synthesis chip
  supplied by Silicon Systems Incorporated", "also known as the SC-02", with its five registers in the 6502's
  memory from $D100 and its A/R pin on an interrupt line.

### 1985

- **The SSI-263A.** Chips marked SSI 263AP appear from 1985 (N8513 on a Phasor, S8545 on a Mockingboard M), and the
  1985 Silicon Systems data book documents the A. A Usenet post of March 1985 reports a reset defect in a 263P
  design. Whether the A changed anything a listener could hear is open.
- **The Mockingboard M** (marking). SSP2000 Rev D, "(c) 1985": one speech socket, an input for the Apple's own
  speaker, and the only Mockingboard with a 3.5 mm jack; it came with Bank Street Music Writer. The photographed
  boards hold an SSI 263P dated 8431 and an SSI 263AP dated S8545.
- **Votrax's SC-02 data sheet** (document, December 1985): the same chip under Votrax's name.
- **Arctec's Gemini robot** (document). Arctec Systems of Columbia, Maryland, drew this robot's schematics from
  September 1984; its wiring diagram is dated March 1985 and its block diagram March 1986. About sixty were built,
  by the account of Robot Workshop, which restores them. Its voice has a computer of its own: the Technical Reference (for "Gemini Robot System Version 1.0") describes a Rockwell R65C02 at 1 MHz
  with 32 KB of ROM, "a third generation SSI263 speech synthesizer" with its registers at 6000 to 6004, and an
  AY-3-8913 for sound. Its ROM speaks text sent to it, in five voices (two male, a villain's, a child's, an
  alien's), and plays songs "with vocals". The "Voice I/O, Sound" schematic agrees: an "SSI-263" selected at
  $6000, clocked by the 65C02's φ2 from a 1 MHz crystal with DIV2 grounded, its A/R on the interrupt line and both
  test pins left unconnected. The voice passes one resistor and capacitor (2.7 kΩ, written by hand over a printed
  220 Ω, and 0.047 µF: a low-pass near 1.25 kHz) into an LM386 and an 8-ohm speaker. That filter is the board's
  sound, not the chip's.
- **Votrax's Votalker IB and AP** (document). Votrax's own advertisements announce the two cards, the IB for the
  IBM PC and XT and the AP for the Apple II, from June 1985 (PC Magazine, 11 June: "When Votrax developed the
  SC02 speech synthesis chip ..."). Byte's of January 1986 gives the IB at $249 and the AP at $179, a "Newly
  Designed Circuit Board with Advanced SC-02 Speech Chip", four voice patterns set by switches on the board, and
  a "Sophisticated Text-to-Speech Translator Diskette"; it lists the Votalker C-64 among Votrax's other
  products, with no chip named. Byte's bonus edition of June 1987 still advertises the IB, with "the new SC-02
  Speech Chip": "32 inflections and 4,100 pitch settings", song "in five octaves" and sound effects "in 16
  amplitudes". The 4,100 is the chip's 12-bit inflection (4,096 steps) and the 16 its 4-bit amplitude; the 32 and
  the five octaves are the card's software's, not read here. The IB's software was Artic's. The National Braille Press's guide to speech
  software (1987) speaks of "the SONIX and TTS programs which are sold with the internal speech cards", the
  SynPhonix 200 and the Votalker IB, and of PortTalk sold by "Artic Technologies or Votrax, Inc."; it adds that
  Artic's own screen reader works with the Votalker IB, "although the speech sounds slightly different". A 1986
  thesis built on the card (see 1986) cites its 1985 manual as Votrax's and Artic Technologies', and loads its
  "Speech Operating System KERNEL" by typing `SONIX`.
- **Artic's Text-to-Speech:88** (document). PC Magazine's new-products pages (11 June 1985) list Artic's
  text-to-speech as a program of its own, "designed to be used with speech synthesizer boards using the SSI-263
  synthesizer chip", at $50 alone or $195 with the SynPhonix 200, with exception tables, and working with the
  "Sonix:88 operating system". All three of Artic's programs "can, with modifications, be made to work with other
  8088-based" SSI-263 products.

### 1986

- **Applied Engineering's Phasor** (marking). An Apple card, "(c) 1986", with four AY sound chips and two speech
  sockets; the photographed board holds an SSI 263AP dated N8513, with its second socket empty.
- **The SmallTalk voice module for the Epson HX-20** (marking). "HX-20 VOICE MODULE REV C", with an SSI 263AP dated
  S8601, an LM386 amplifier and a 3.579545 MHz crystal; it came with an expanded-memory board marked "(c) 1986
  Computer Aids Corp". The photographs and notes are Scott M. Baker's.
- **Heath's HERO 2000** (document, firmware). Heath and Zenith's robot trainer, built around an 8088 with eleven
  slave processors, at $3,000 as a kit. Modern Electronics (February 1986) says its "'voice' is based on a 263A
  synthesizer system that produces human-like speech, music and special effects", with text-to-speech in the
  robot's ROM, and lists the chip's controls as the SSI 263A's data sheet does: 64 phonemes, four durations, 16
  rates, 4096 inflection levels, 16 amplitudes, eight articulation rates and a vocal tract filter of more than
  250 settings. A public archive of Heath's HERO ROM files agrees. Its input/output decoder table gives ports 50h
  to 57h to the "SSI-263", on a select line named "SC-02"; and the robot's two 32 KB ROMs (copyright Heath
  Company, 1985 and 1986) write ports 51h to 54h one after another, hold letter-to-sound rules that end in the
  chip's phoneme codes, and spell their own messages in its phoneme names. Read from the files, not run here. We
  have not seen the chip on a board.
- **Spanish rules for the SC-02, in a thesis** (document). Joaquin Vila's 1986 thesis at the University of
  Missouri-Rolla, with Arlan DeKock, is a rule editor and parser for any language in the Latin alphabet, built on
  "a Votalker IB, which embodies the Votrax SC-02 phoneme synthesizer" (see 1985). It prints 68 letter-to-sound
  rules for Spanish, each with its phonemes and their settings, and its BASIC listings show how a program
  reached Artic's `SONIX`: they find the kernel through the PC's user interrupt vectors and call its "SONIX-88
  entry points", among them `SSI263.BUFFER` and `SSI263.DIRECT`, each with the chip's five register values.
- **SCAT, the Slot-Buster's screen reader** (document, firmware). Raised Dot Computing's notes on BEX 2.2, its
  braille and speech program for the Apple II (1986), say it "now has the SCAT program for the Slotbuster
  installed": RC Systems' SSI 263A card (see 1987). BEX's boot disks survive with SCAT and a `SCAT TABLE` of
  pronunciation exceptions for BASIC and DOS words, each a word in brackets and its sounds in a letter code. The
  same notes describe "Spanish Modified" BEX disks that "boot up with the Spanish Rules File for the SlotBuster",
  kept in that table; the three copies of the table we have are identical, and English. Read from the disks, not
  run here.
- **Aicom's Accents** (marking, firmware). Aicom's copyright runs from 1986. Its chip is the AI901, an SSI-263: an
  Accent PC card's chip reads "AI901 / AICOM CORP / SA9048". The Accent User's Manual (version 4.0, February 1991)
  covers the Accent-PC, the Accent-PS/2 for Micro Channel, the Accent-mini and the stand-alone Accent-SA; the family
  also had the 1600 and XE for Toshiba laptops. The Accent-SA's own 8085 program (copyright 1986–1989) and the
  Accent-mini's DOS driver (version 4.5, 1986–1993) both run in this project.
- **Mannell's measurements** (document). In 1986 Mannell measured every SC-02 vowel's formants from
  spectrograms and mapped them onto Australian English.

### 1987

- **The Braille 'n Speak** (firmware). Blazie Engineering's talking braille notetaker; Blazie marked its tenth
  anniversary with the summer 1997 updates. Every Blazie firmware this project runs writes an SSI-263: the Braille
  'n Speak 640 and 2000, the Type 'n Speak, and the Braille Lite 2000 and its language updates, from 1997 to June
  2003. We haven't seen the speech chip of the first units.
- **Heath's PC Voice Card** (marking, document). A PC card whose board is marked 051587, likely 15 May 1987; the
  photographed card holds TDK's 78A263A-P, dated 8908 under a Silicon Systems logo, with a 4 MHz oscillator, a
  74LS74 flip-flop and an LM386. Modern Electronics (April 1988) reviewed Heath's HV-2000 Computer Voice kit, a
  half-size card at $89.95 with those same parts, and printed its speech circuit: "Heart of the Heathkit Voice is
  an SSI-263 IC", drawn as an SSI263A. The flip-flop halves the 4 MHz to 2 MHz at the chip's clock pin, and the
  chip's DIV2 pin is tied high, the setting its data sheet gives for clocks of 1.5 to 2 MHz, so the chip runs as
  it would from 1 MHz with DIV2 low. TP1 and TP2 are left unconnected, and the output goes through 3.3 kΩ and
  0.1 µF to a line jack and the LM386. The kit's software was a DOS driver (`VOICE.SYS`), a `SPEAK` command and a
  list of up to a hundred exception words (`WORDS.EXE`). The clock is the review's drawing of the kit, not a
  measurement of the photographed card.
- **Chip Talk** (document). Dave Prochnow's book of speech projects (TAB, 1987) builds a minimal SSI 263A card for
  the Apple IIe, and covers the Mockingboard B and RC Systems' Slot-Buster, a multilingual SSI 263A text-to-speech
  card.
- **The SynPhonix range in the Trace Center's resource books** (document). The 1987 book lists the SynPhonix 100
  for the Apple II, the 200 for the PC ($225) and the jr for the PCjr. The 1988 update adds the 210 ($295), "an
  updated version of the SynPhonix 200" with a treble control and a built-in "Artic Switch Interface", and the 220
  for Toshiba's 1100, 1200 and 3100 laptops ($325). The treble knob and the switch socket are the ones on the
  SP-21X board (see 1991). The books don't name a chip. They also list Votrax's Votalker AP for the Apple II
  ($179), Votalker C-64 and Votalker IB for the PC ($249), cards that speak text "according to the synthesizer's
  pre-programmed phonetic rules", again with no chip named (Votrax's own advertisements name it for the AP and
  the IB: see 1985).
- **CHARLY, a Spanish voice for MSX** (firmware, document). The Spanish magazine MSX Extra reviewed "CHARLY, el
  nuevo sintetizador de voz de Spectravídeo" in December 1987 (issue 38, page 13): a cartridge for MSX computers
  with its program on cassette or disk and a bingo game that calls its numbers aloud, for 9,900 pesetas. The
  advertisement in the same issue, from Control Time of Barcelona, calls it "el primer sintetizador de voz para
  MSX". Neither names a chip. The files of the cartridge's disk survive beside a retyped instruction sheet signed
  SVI España; what ties them to CHARLY is that the sheet and the review give the same three commands with the same
  ranges, for speed (0 to 15), amplitude (0 to 15) and articulation (0 to 7). The disk's program, `SPA.SYN`, is
  1,352 bytes of Z80 code and tables loading at 8100h. It writes five ports in a row, 08h to 0Ch, starts by
  writing 80h to the fourth and C0h to the first, and feeds the chip from the MSX's interrupt, testing bit 7 of a
  read of the first port. Its letter table holds the SSI-263's phoneme numbers, and the three commands set the
  chip's rate, amplitude and articulation fields. The Spanish is spoken letter by letter with a handful of rules
  (ce and ci, ge and gi, gue, güe, que, ch, ll, ñ, and a "México" said with a jota); z and the soft c are an S,
  and Control with Stop silences it. The sheet and the disk differ in places: the sheet prints the entry address
  as B100h where the disk's own BASIC program uses 8100h, both the sheet and the review give articulation's
  starting value as 6 where the program starts at 5, and the disk's BASIC program is dated 1993 and has an error
  trap the sheet doesn't show. The program also handles letters Spanish does not use: it upper-cases ö, and its
  list of vowels includes Ä and Ö. Its demonstration is named `KOE.BAS`. We don't know who wrote it, or the
  cartridge's clock. Read from the disk's files, not run here; we have not seen the chip on a board.
- **An SSI 263 AP dated N8751** (marking), in a seller's photograph of six chips (see 1989).

### 1988

- **Sprechi for the Amstrad CPC** (document). A German text-to-speech for an SSI 263 wired to the CPC through a Z80
  PIO, published as a type-in in CPC Amstrad International (special issue 7) by Jan-Mirko Maczewski and Jens
  Heimsoeth. Its numbers, symbols and question rise are German, and its article says the chip "even does German
  umlauts". It also advises buying an SSI 263A if possible, "since it sounds a little better than the SSI 263,
  though the difference really isn't very large" (our translation): the only claim we know of, by ear, that sets
  the two apart. It was not only a type-in: the program is on the magazine's own companion disk for that issue (the
  "Databox", kept in the CPC wiki's archive of them) and on a second disk image kept by CPCrulez, the same bytes in
  both. `SPRECHI.RSX` is 4,096 bytes of Z80 code loading at 8000h, with its generator listing, two demonstrations
  and a song beside it; it reaches the chip through the PIO at F8F0h to F8F3h. The listings are dated 1987 and spell
  the second author Heimsoth. Read from the disks, not run here.
- **GW Micro's Sounding Board** (firmware). A PC card sold from 1988 to 1996, with LT and XE versions for Toshiba
  laptops; the text-to-speech ran on the PC, in two DOS programs by Douglas Geoffray. Version 2.7 runs in this
  project.

### 1989

- **TDK's 78A263A** (marking): see the Heath card above.
- **A Votrax-marked SC-02** (marking) with the date code SA8912.
- **Three names, one run of lot numbers** (marking). A ReActiveMicro sale photograph shows six chips side by side:
  three marked ARTIC 263 and dated SN8848, lot 48814; a 78A263A-P dated SN8908 under Silicon Systems' logo, lot
  51920; a VOTRAX SC-02 dated SA8912, lot 52797; and an SSI 263 AP dated N8751. So Artic's name was on the chip by
  late 1988, and within about five months three brands' chips carry lot numbers that rise with their dates. The
  markings are read from a small photograph.
- **Kraftwerk's Robovox** (document). Patent EP0396141A2, filed in May 1989 by Florian Schneider, Gert Joachim Ott and
  Gert Jalass: an SC-02 / SSI 263A singing in real time over MIDI, with outside sound fed into the chip's test pins.
  It can be heard on The Mix (1991).
- **King's talking videotex** (document). An experimental reader of videotex pages for blind users in Australia,
  with "a SSI-263 chip housed in a purpose-built unit" on the PC's printer port, given an Australian accent.

### 1990

- **A 263 in orbit: DOVE** (document). The amateur radio satellite DOVE-OSCAR 17, the "Digital Orbiting Voice
  Encoder", was launched on 22 January 1990 to speak to schoolchildren on 145.825 MHz. Its speech module holds "a
  Votrax SC-02 phoneme speech synthesizer" and a digital-to-analog converter, run by a Motorola 68HC11 (Robert
  Diersing, "1993-94 Report on DOVE Recovery Activities", AMSAT-NA Space Symposium, 1994). The chip was first tried
  from orbit in 1992, with the phrase "You are listening to DOVE Microsat". By then the SC-02 was "now called the
  Artic Technologies 263A" (Jim White, the 1993 symposium), and the team bought Artic's Sonix speech development
  system, "an IBM-PC compatible card using the 263A and a speech file builder/editor". Diersing wrote a program to
  pull the phrases out of the Sonix database into the 68HC11's program, and tested it on a 68HC11 board wired to a
  263A as in the spacecraft. On 11 June 1994 the satellite repeated "Hi, this is DOVE in space" for a minute, with
  rate, amplitude and inflection now set phoneme by phoneme. White recalled in 2015 that the message took some two
  hundred hours to get right, and ran only a few months before the voice hardware failed. Four seconds of it, as
  received on the ground, are kept on [DD1US's satellite sounds page](http://www.dd1us.de/spacesounds%202c.html).
  The plan reached blind readers soon after the launch. Issue 3 of 1990 of Vakok Világa, the Hungarian
  magazine for the blind, carried "A beszélő Galamb" ("The talking dove") by András Arató and Teréz Vaspöri
  ([from page 21 of the scan](https://epa.oszk.hu/01400/01434/00253/pdf/EPA01434_vakok_vilaga_1990_03.pdf#page=21)).
  It names the "Votrax SC-02" as the synthesizer that was to speak the telemetry, sets it apart from the recorder
  that would play children's taped messages, and credits both to Bob McGwier, N4HY. The authors compare it with the
  formant synthesizers of their own BraiLab talking computers, and add: "DOVE's designers also thought of blind
  children listening to the messages. We think it likely that the Votrax circuit reporting the telemetry will be
  familiar to them, since this circuit was built into a great many older American aids for the blind" (our
  translation).
- **A university teaching card** (document). A German university's microprocessor lab drove an SSI 263 from a 6802
  for its students; the card's schematic is dated April 1990 (Harald Brandau), the lab sheet 2003. It has no
  low-pass filter at all.
- **The Accent PC's AI901** (marking) is dated SA9048 (1990, week 48).
- **Burke & Burke's CyberVoice** (document). A speech cartridge for the Tandy Color Computer under OS-9 Level 2,
  for shipment "in late October of 1990" at $74.95. Its maker's advertisement says "We've mated a high-tech SC-02
  speech synthesizer with an entirely new kind of OS9 device driver" that lets the user write the pronunciation
  rules. The software alone ($24.95) drove Speech Systems' Super Voice, itself an SSI-263 cartridge (see 1983 and
  1984).
- **The SynPhonix range, and a cousin** (document). A December 1990 note for the JAWS screen reader lists the
  SynPhonix 200, 210 and 215 for desktop PCs and the 220 and 225 for Toshiba laptops. The text-to-speech was a DOS
  program on the PC: `SONIX.COM` and `TTS.COM` on the older cards, `SONIXTTS.COM` on the 210. It calls Votrax's
  VoTalker a "cousin", "very similar to the 200", run the same way (it was: the Votalker IB ran Artic's `SONIX`,
  see 1985).

### 1991

- **A repeater that says "nuk, nuk, nuk"** (firmware, document). Rick Swenton, WA1LMV, built an amateur radio
  repeater controller on an 8085 board salvaged from a blood analyzer, with an SSI-263 on an expansion board of his
  own at C000h. He has published its photographs, its story and its assembly source on
  [his own page](https://www.swenton.com/ham-radio/my-ham-radio-page). A screen photographed on 23 February 1991,
  version 1.0, already lists "Voice ID", "Voice Time" and "Nuk, Nuk"; the source is version 15, of October 1994.
  Every word it speaks is a table tuned by hand, each phoneme with its own filter, inflection and amplitude (fifty
  tables), which gave the repeater what he calls its "accent". At noon and midnight it played Curly's "nuk, nuk,
  nuk, nuk" from the Three Stooges: twenty frames of the chip. The routine that plays a table sets every frame's
  duration bits itself, so the tables' own duration bits are not what the chip was sent. The source is his, with all
  rights reserved: we point to it and copy nothing from it. No recording has turned up, and he can no longer find
  the expansion board's diagram, so the chip's clock is not known.
- **The Braille 'n Speak 640**: see "Before this project".
- **Artic's SynPhonix SP-21X** (marking). Revision 3, "COPYRIGHT 1987,91 BY ARTIC TECHNOLOGIES": an ISA card with
  the speaker on the card, treble and volume knobs, and a 15-pin socket for Artic's switches. Two photographed cards
  carry ARTIC 263 dated SA9117 and ARTIC263 dated CD9226 (1992). Its 8 KB EPROM holds only an 8-byte key, repeated,
  which Artic's software, serialised to each card, appears to check. VEST (see 1992) does check one: six bytes at the card's
  I/O address, "AR" and four of serial, against the serial stored in the program. That it reads this EPROM is likely
  but not shown.
- **The BrailleMate** (ear, document). Telesensory's hand-held braille notetaker, described in 1991 as a
  computerised Perkins brailler that also kept notes, a calendar and a phone directory. A September 1993 product
  list gives it a braille keyboard and a single 8-dot braille cell, an "optional built-in speech synthesizer", model
  DM1A-01 at $1,595, and a plug for a Navigator braille display of 20 to 80 cells. It took Artic's commands:
  MicroTalk's ASAP manual (September 1991) lists "TSI's BrailleMate" among the synthesizers that use the PortTalk
  command set, which Artic wrote for its own synthesizers; the National Federation of the Blind's braille
  technology evaluation of December 1993 says it "may be used as a speech synthesizer for a computer with any
  screen-reading program that supports a PORTTALK device". Telesensory's DOS screen reader, Screen Power Speech 2.0A
  (1994), drives it as a synthesizer and speaks of its "External Speech Synthesizer mode"; both manuals are preserved
  in Nick Stockton's vintage-access archive. **Tomi recognised the SSI-263 in Telesensory's BrailleMate tutorial
  recordings**, preserved in Jake Gross's Datajake archive. Artic's command set fits an Artic-263 inside, but that is
  an inference: no board photograph or firmware has surfaced yet.

### 1992

- **Artic keeps the chip alive** (document). The National Federation of the Blind's Computer Science Update (June
  1992) reports that Artic's principals had designed the 263 at Votrax's research lab, that Silicon Systems made it
  under licence, and that when Votrax folded and Silicon Systems announced the chip's end, Artic took over its
  distribution and support. It names Aicom's Accent as a 263-based competitor. Artic's own 1995 manual says the
  Artic-263 "was developed by the founders of Artic Technologies".
- **One chip, three names?** The SC-02 (SA8912), the AI901 (SA9048, lot 18611) and the Artic 263 (SA9117, lot 18573)
  carry the same marking format, with lot numbers only 38 apart. That suggests one factory and one die under three
  brands, as the six chips of 1987 to 1989 do (see 1989). It is an inference from the markings, not a die
  comparison.
- **Artic's TransPort** (document). A serial speech box whose hardware manual (copyright 1992) lists the Model 615's
  "Artic 263 Speech Chip", Artic's VEST text-to-speech and PortTalk commands, with 256 KB of system ROM. Artic's own
  newsletter of August 1992 agrees: "All Transport synthesizers use the Artic 263 speech chip." The SynPhonix cards
  ran their text-to-speech on the PC; here `VEST.COM` is "permanently resident" in the box, and the 615 can load
  Artic's Business Vision screen reader onto a PC from its own ROM. The 611 is the same without Business Vision. An
  owner writing on misc.handicap in October 1992 found its speech indistinguishable from the SynPhonix card's unless
  turned all the way up. It cost $1,295.
- **Artic's VEST on disk** (firmware). `VEST.COM` 1.60 English, "Visions Enhanced Speech Translator", the "Special
  Release For Artic Visions" (copyright 1984 to 1992), survives on a Business Vision 3.08 disk (1993), loaded by its
  `BV.BAT` before the screen reader. It is the whole text-to-speech, rules, numbers and lexicon, resident on the PC;
  the SynPhonix 215 is the chip and a key. It finds the card by a six-byte key ("AR" and four bytes of serial number)
  at 2E0h or one of 16 places below it, writes the chip's registers eight above, each byte rotated one bit for the
  card's wiring, and sends one frame per A/R request on IRQ 7. Each copy holds the serial of the card it was sold
  with and stays silent on any other. It runs in this project's research host (see
  [front-ends.md](front-ends.md), "Artic SynPhonix 215").

### 1993 to 1996

- **Still in the catalogue in 1993** (document, marking). Silicon Systems, "A TDK Group Company", issued a data
  sheet for the "SSI 78A263A Phoneme Speech Synthesizer" dated February 1993, and a chip offered for sale in 2026
  is marked 78A263A with the date code CD9319, which reads as week 19 of 1993 in the format of the SynPhonix's
  CD9226. So the chip was documented, and apparently made, a year after the report that Silicon Systems had
  announced its end (see 1992). Who ordered that run isn't shown.
- **A screen reader's list is not a chip list.** MicroTalk's ASAP screen reader was built for MicroTalk's own
  DoubleTalk-based LiteTalk and LapTalk. Its manual of September 1991 describes support for the Accents, the
  Audapter, the Braille 'n Speak (from its June 1991 ROM, in speech-box mode), the DECtalk, the Echo PC, the Sounding Board, the SynPhonix,
  the VoTalker, and PortTalk synthesizers such as the BrailleMate. The Echo is a TMS5220 and the DECtalk is DEC's
  own, so the list says what ASAP supported, not what was inside. One line in it is a lead, though: the VoTalker
  needed `SONIX.COM` and `TTS.COM` to talk, and Sonix is an Artic trademark, the same family as the SynPhonix's
  `SONIXTTS.COM`.
- **GW Micro's Speak-Out** (firmware). A serial speech box (hardware by Daniel Weirich, software by Douglas
  Geoffray); its program, version 1.2, is dated May 1995. It carries the Sounding Board's rules and allophone tables
  byte for byte, and the two send the chip the same frames. It runs in this project.
- **Automated Functions' SmarTalk** (firmware). A five-ounce SSI-263 box on the PC's parallel port, from Arlington,
  Virginia (1995), with nothing to think with: its text-to-speech is a DOS program, `S_TALK.EXE` 1.21 (November
  1995), which screen readers fed in the Accent's commands or the SmarTalk's own. No document describes the box's
  wiring; RetroBunn worked it out from the program's own code, and the disk's own test program reports "FINAL
  SMARTALK STATUS IS GOOD" against the emulated box. Its makers' disk, preserved in Nick Stockton's vintage-access archive with recordings of a real
  SmarTalk, says "Please feel free to share the new version of the SmarTalk software with anyone who needs it", so it is the one program in this project shared by
  its makers' own words.
- **Artic's 1995 range** (document). The SynPhonix manual (fourth edition, September 1995) lists the 200, 210, 212
  and 215 ISA cards and the P27 PCMCIA card, all with the Artic-263. The Mini TransPort (manual of March 1995),
  "Artic's smallest external speech synthesizer", takes Artic's PortTalk commands and the Accent's; its manual never
  names its chip. A June 1996 newsletter gives two models: one mainly a synthesizer, and the Deluxe, which also keeps
  to-do lists and phone books. One unit, recorded from its line output, announces itself as "Mini TransPort Deluxe,
  Model 622, TransOS for MiniPort, version 1.22", the version Artic's site still listed as current in May 1998. Its system ROM
  isn't reachable by the user: updates came on disk and were written through Artic's `MINICOM` program.
- **Artic's small notetakers** (document). A brochure page reproduced in a 1996 guide for university teachers
  ([ERIC ED404827, page 89](https://files.eric.ed.gov/fulltext/ED404827.pdf#page=90)) pictures three together: the
  SQWERT, "a speaking QWERTY notetaker synthesizer", the BraillePAD and the ErgoBraille, with "High Speed Artic
  Speech" and over 700K of file space. "All three devices also act as a TURBO SPEECH SYNTHESIZER FOR USE WITH ANY PC
  SCREEN ACCESS PROGRAM." It gives Artic's address as 55 Park Street, Troy, Michigan, with a telephone number in area
  code 810, the one before 248. It doesn't name the chip. Their firmware and the one recording are under 1997.

### 1997 to 2003

- **A 263 with Semtech's name** (marking). A photograph of an opened Braille 'n Speak 2000 shows its speech chip
  marked "SSI 263-2-2 / SEMTECH": the name on that unit's chip is Semtech's, not Silicon Systems'. A marking says
  whose name is on the package, not who made the die. Semtech did have a wafer fab that took outside work, from
  October 1995 to April 2001. Its agreement to merge with Gamma Inc., "dba ECI Semiconductor",
  is dated 4 October 1995; Semtech's annual report for that year describes ECI as an analog semiconductor maker
  whose main business was foundry wafers, and renames it Semtech Santa Clara. On 19 March 2001 Semtech announced
  it was "exiting the foundry services business", and by 25 April it had sold the Santa Clara fab to STI Foundry
  Inc. So between those two dates Semtech could make another company's chip to order, and may have made this
  one; it is not evidence of where the chip was made, or for whom. A date code on a Semtech-marked 263 would test
  it: this one's date line is not visible in the photograph.
- **Artic's Spirit** (document). The Spirit 600 and 600B, a portable serial synthesizer from Artic (its manual's 248
  telephone area code dates it to 1997 or later), with "a TRUE phoneme speech synthesis chip" that blends phonemes
  rather than playing samples; it takes the TransPort's commands and the Accent SA's. The manual doesn't name the
  chip. It also mentions Artic's "family of combination synthesizer notetakers for BRAILLE and QWERTY".
- **Artic's notetakers** (document). Artic's 1997 catalogue sells the TransType 2000 and BrailleDesk 2000 and a "new
  family of low cost talking note takers": the SQWERT (QWERTY), BraillePad and ErgoBraille (in a brochure by 1996:
  see 1993 to 1996). All five double as external
  synthesizers with "TransPort and Accent emulation". Artic's site listed their firmware in May 1998: 2.17 for the
  TransType and BrailleDesk, 2.33 for the other three. Nothing we have read names their chip.
- **Blazie's last updates.** The firmware this project runs comes from Blazie's updates of 1997 to June 2003, the
  last English Braille Lite 2000 build.
- **The end of the Accents.** By September 2000, a mailing-list post reports, Aicom had stopped making the Accent SA,
  PC, Messenger and mini.
- **Artic winds down** (document). AccessWorld's "Is Artic Running Hot or Cold?" (Deborah Kendrick, September 2000)
  finds the company, by then at 1000 John R. Road in Troy, Michigan, missing from the 1999 conventions of the American
  Council of the Blind and the National Federation of the Blind, its website not updated since 1998, its staff down
  from eight to six, and Dale McDaniel, a founding partner, gone. Kathy Gargagliano, its vice president of
  operations, answered "Artic is not dead": it was still selling synthesizers and notetakers, with no updates under
  way, and a $999 "talking out of the box" computer, the i-Talk. We have found nothing about Artic after 2000, nor
  when it closed.

### Not the SSI-263, though often mixed up

- **Sweet Micro's Speech I and Sound/Speech I** (1982): the Votrax SC-01-A (dated 8311 on the photographed
  board), the chip MAME emulates.
- **Votrax's Type 'n Talk and Personal Speech System:** the SC-01-A, in MAME's drivers for both. Sinclair Answers'
  second issue (about 1985) reviews them with the Spectrum and gives the Type 'n Talk a Z80; MAME's driver has
  an MC6802, and the Z80 is the Personal Speech System's.
- **The Braille Lite M20 and M40:** a DoubleTalk.
- **Aicom's Accent Messenger** (1993): an NEC µPD77P25 signal processor running a software synthesizer, not an AI901.
- **The Echo:** a TMS5220, a linear-prediction chip.
- **HP's 27201A Speech Output Module:** it plays downloaded encoded sound, with no phonemes.

## 2026-09-16: "that's not the SSI-263"

A small harness compiled Casso's model into a DLL and rendered "hello Tomi" and a few
vowels and fricatives. Tomi's verdict: too soft, not the harsh, robotic chip he grew up
with, and it said "hello" cleanly where he remembers a "shello". The ROM table was probably
close; the sound model was not. That set the direction: check the ROM independently, and
study the analog side on a real chip.

## 2026-09-22: a real chip on a wire

- **The working agreement** (Tomi): "I drive the hardware. You drive the images and the
  repo." Claude owned anything that had to come out of real silicon; Astra owned the die
  photographs and the repository.
- **The chip is in Tomi's Braille Lite 2000,** driven in speech-box mode over a serial port
  and recorded from its line out. (Claude first called it a "40"; Tomi counted 18 cells.)
- **Astra's first review** of Casso's read: the cells are visible and a real extraction is
  possible; re-applying Casso's field map reproduces all 384 decoded values; and two
  contradictions in Casso's write-up (HV and HVC differ in three bits, not one; "PAR" is
  called both non-acoustic and F1's lowest bit). From then on the raw rows were kept apart
  from what they are thought to mean. Astra also listed the model's gaps: no closures,
  three resonators where the manufacturer describes five, pitch clamped at 400 Hz.
- **The trap** (Claude, Reply 2): the unit holds each line until the next one arrives, but
  applies settings at once, so every early recording had carried the next line's settings.
  Claude withdrew every claim built on them and switched to a flush protocol. The clean
  data gave the real pitch law: F0 = 1953.125 / (32 − ⌊cmd/2⌋) Hz, a 1 MHz chip clock, 32
  pitches, confirmed from 63 to 1953 Hz by three estimators and a blind octave check.
  Astra showed the first fit's "1.0027 MHz" was an artefact of the pitch tracker's 10-cent
  grid.
- **Tone is the filter clock.** Claude proposed that tone t sets the switched-capacitor
  clock to fc = 500 kHz / (32 − t). **Astra froze a table of predicted clock-image lines for
  all 25 tones before the session ran**: the project's first pre-registered test. The
  lines landed where predicted, and a 96 kHz capture then read the clock directly, within
  about 0.15 % for 16 of 20 tones.
- **The "grit" of high pitches** turned out to be clock images: harmonics folding around
  the filter clock into F1. Between lines, the chip's clock leaks into the silence as a
  divider chain, fc/2 to fc/64. Tomi asked for that faint carrier to stay in the sound.
- **The glide:** pitch glides linearly in period, at a speed that scales with speech
  rate. Astra's capped-gap audit dissolved an apparent "half-speed" exception.

## 2026-09-23: the front end, found and running

- **The rules were on floppies.** Tomi's archive held Blazie update disks carrying the
  Braille 'n Speak firmware, uncompressed. Its letter-to-sound rules are plain data in the
  NRL notation (Elovitz, 1976), and each rule's output is SSI-263 register-0 bytes. Claude
  decoded them; Astra checked that the firmware's 64 phoneme indices match the ROM's code
  order, and cautioned that table bytes aren't bus writes until the code that sends them
  is traced.
- **The Braille Lite 2000's own firmware runs.** Claude wrote a board file for z180emu and
  ran it: the chip is on five ports, its A/R request is the CPU's INT1, and the handler
  sends one phoneme per interrupt, priming each with a brief PA. Astra reran it and
  reproduced the register events exactly.
- **The first ear check.** The emulator predicted that three malformed rules never fire,
  so "correct" should come out as "core-wrecked". Tomi typed it on the real unit and heard
  exactly that.
- **Settings and clocks.** Claude read the whole settings layer out of the firmware (the
  pitch command's low bit lands in the glide-rate field, which is why pitch commands come in
  pairs) and derived the CPU clock, 6.144 MHz. Astra recovered all 148 settings-to-register
  mappings independently and confirmed the firmware writes the filter register as
  224 + tone.
- **An emulator bug, found by Astra's control.** Astra suggested a pacing test: the same
  input, sent slower. It led Claude to a z180emu bug that dropped serial bytes, and several
  earlier "findings" were withdrawn as its artefacts.
- **The glide-rate field is real** (G02, recorded to Astra's protocol with predictions
  frozen first): field 4 glides about 2.1 times as fast as field 0. Punctuation turned out
  to depend on the unit's inflection setting, which Tomi had switched off.
- **Two reads of the ROM begin.** Tomi proposed reading the ROM from the photograph with
  numpy; Astra set up the lattice. Claude made a blind second read and sealed it with hashes
  before seeing Astra's.

## 2026-09-24: one recording, one ROM, and an engine

- **The MASTER capture.** The unit's battery lasts one session, so everything left went
  into one unattended run: 2,243 lines, every one confirmed, recorded 07:35 to 08:38. It
  falsified a hypothesis Claude had frozen predictions for, showed the unit's "?" never
  rises, and measured the glide law across rates.
- **The ROM, reconciled.** Astra froze an independent cell pass, then opened Claude's
  sealed read. They agreed on 1,854 of 1,856 cells and on all 896 empty spacer rows. Astra
  looked at the two disputed cells in larger crops, both came out as in Claude's read, and
  Claude confirmed. The reconciled read matches Casso's cell for cell: three readers, one
  pattern. (What the cells *mean* electrically is still open; see [die.md](die.md).)
- **The resonance search, and why it paused.** Claude built estimators; Astra built
  controls with known answers, and they caught real problems: synthetic poles scaled
  exactly came back unequal, a notch moved F3 by 100 Hz with no carrier present, and half a
  hertz of pitch error moved a pole by 100 Hz. Claude retracted the claims those controls
  broke, built a joint model that measures pitch to 0.006 Hz, and proposed pausing
  audio-only resonance fitting until the die could say what the filters are. The outputs
  were frozen with checksums.
- **Tracing the die's metal.** Astra mapped the analog section (five repeated blocks,
  named by position only) and traced visible metal in one of them: 36 nodes, 34 links.
  Claude reproduced exactly that count independently, from its own crops, before opening
  Astra's overlay. Pass 2 reached 55 nodes and 50 links, all cross-checked.
- **The documents.** Tomi saved the 1985 and 1986 Silicon Systems data books: the
  filter-clock formula, linear transitions, and a worked "Hello" with full register
  streams. Astra's checked transcription of it became the engine's first test. Tomi bought
  the design paper, which both read: five switched-capacitor sections, noise into F2 and
  F5, a sample-and-hold, metal-gate CMOS, and a disagreement between its figures about F5.
  Astra settled which die was photographed (263P, P 8404) and found a 1985 report of a
  263P reset defect.
- **The engine.** Astra proposed a five-point build plan; Claude agreed and built v0:
  every uncertain constant in `params.py` with its source tag, and the tract run at fc so
  tone scaling comes from the structure. With the bit weights left free, F2 and F3 came out
  binary, 1:2:4:8, as switched-capacitor arrays should. It rendered the data book's
  "Hello", and Tomi called it "remarkably accurate": the listening milestone. Every one of
  the 1,856 ROM bits in the engine matched the reconciled read in Astra's review.
- **v0.2 to v0.10, by ear.** Tomi heard a wobble at "three" (transitions moved to the frame
  clock), "shix" for "six" (ROM bit b02 turned out to pick where noise enters), extra
  breath under the T, "Hello STomi" (the stop order was backwards), a "d" before P, and a
  thick T. Each was fixed on development data only and logged. Astra's review of v0.9
  predicted that short stops would never close under the model; the recordings showed they
  do, and v0.10 fixed it.
- **A second front end.** The Speak-Out's own firmware ran under Unicorn and greeted in
  "the exact tone of the speakout", confirming the 1 MHz clock on an independent unit.
  Its phrases run 11 to 13 % longer than ours; Astra wrote a tiny x86 program showing the
  host's scheduling had to be ruled out first, and it's still open.
- **The chip in C and the add-ons.** Tomi asked for portable C; the chip was ported line
  by line, identical in state and sound, and the NVDA add-ons went down to 1 MB with no
  numpy.

## 2026-09-25: four front ends, and public

- **The Accent-mini.** Aicom's own DOS driver ran under Unicorn with emulated expanded
  memory and spoke "Accent ready" as the real card does. Tomi heard wobbly vowels; only
  the Accent uses three glide fields that had been guesses, and one value fit. Two freezes
  and a slow start were found and fixed in the host.
- **The TP pins.** Tomi found Kraftwerk's Robovox patent, which feeds outside audio into
  the chip's "do not use" test pins. Our note to Astra read the data sheet as putting TP2
  at the noise source. Astra corrected it: both manufacturer diagrams draw TP1 and TP2
  into the glottal source, and the patent's own figure wires only TP2. Astra also traced
  seven pad routes and brought an SC-01 patent's high-pass noise shaper as a lead for the
  dark J and SH.
- **v0.11 and v0.12.** Tomi heard an extra soft G in "program" and a "d" before the J in
  "manager": the real chip releases a stop early only into a vowel-like sound. A listener
  heard a click in "still": every B or D after a pause was voicing for a frame, where the
  real chip is silent.
- **The Accent SA.** The stand-alone Accent's own 8085 firmware and dictionaries, shared by
  spacepup, ran on an emulated 8085 once Claude found that its TRAP is gated by a port bit
  the firmware controls. It says "ACCENT READY." on its own.
- **Windows on ARM.** The Speak-Out and Accent add-ons crashed NVDA on ARM machines. Astra
  traced it to Unicorn's jumps out of generated code unwinding through the Windows runtime
  under x64 emulation, patched it, and a tester confirmed it on their ARM machine.
- **Public.** The repository went public with the 0.5.0 add-ons.

## 2026-09-26 and 27: listeners, a new die image, and a fuller voice

- **A faster top rate.** A listener found the Braille Lite "quite slow, even at rate 100 %".
  The firmware takes its rate command modulo 16, so rate 16 spoke at rate 10's speed. The
  top of NVDA's scale is now rate 15.
- **Two tester reports, both found in the code.** With number processing on, "$25" lost its
  "dollars": the digits became words first, and the firmware drops a "$" in front of words.
  Money now goes to the firmware as it is. And an utterance's last syllable sometimes came
  out with the next one. A slider fuzz test showed why: a cancel after the unit had finished
  left its flush line held, and the next utterance then ended at its first long pause.
- **A second, sharper die photo.** siliconpr0n's SSI-263P map (photograph by Ogun, 2011,
  CC BY-SA) is about three times the resolution of the Visual6502 image. Astra found
  countable unit-capacitor arrays in it; Claude checked the counts from separate crops.
  They are counts, not yet capacitor ratios.
- **The clock lines, confirmed.** In the unit's idle output, lines at fc/4 and 15fc/64 sit
  within 0.1 Hz of the prediction at 22 of the 25 tones.
- **Engine v0.13, by ear.** F1 now glides 2.25 times as fast, which fixed a jump Tomi heard in
  "file" without making F2 step. The voice source became 0.8 of the old impulse plus 0.2 of
  MAME's SC-01 stepped glottal wave. After it, the Braille Lite add-on applies a gentle
  5 kHz roll-off that matches the unit's line out (where that roll-off comes from isn't traced). Against the unit's long-term spectrum the
  error fell from 4.6 to 1.2 dB. Tomi's verdict on the A/B: "9E really wins."

## 2026-09-28: the unit's two speech-box modes, and listeners in an A/B

- **Two ways into speech-box mode.** All six dots without space enters it in word mode, which
  was Tomi's habit and how the MASTER recordings were made. Space plus all six dots, then L,
  gives line mode, which is what the add-on uses. A short recording in each mode (M03, the same
  30 lines, predictions written down first) settled an old puzzle. In word mode almost every "?"
  rise and "." fall is lost, and sentences come out with about twice as much silence. In line
  mode every question rises and every statement falls, and the add-on's pitch and timing follow
  the unit through them. The "questions never rise" finding from MASTER was the mode, not the
  chip.
- **Listeners compared the unit and the add-on word by word** (M04, Tomi's clip). Several said
  they doubt they could tell them apart blind. Their precise notes are nearly all about stops
  (B, G, final T and D), which is the next thing being measured.

## 2026-11-20: the "Accent SA" was the Accent-PC

From 0.5.0 through 0.9.2 one of the two Accent voices was called the Accent SA, and the entries above call it
that. It runs the Accent-PC's firmware. What that rests on:

- The ROMs (`u2`, `u3`, `u4`) are the ones Scott Baker published from his own card, and his notes describe a
  plug-in PC card: an I/O port at 2A8h and DIP switches beside the address comparator.
- Aicom's manual (4.0, February 1991) gives the Accent-PC a serial port and a power connector "for stand-alone
  operation" beside its PC bus, and the Accent-SA a 2 KB text buffer. Our firmware's machine has 2 KB of RAM in
  all, and its code reads and writes a second, parallel host port that our serial host never needed.
- Kevin Horton owns a real Accent SA and sent its picture: a Z80, two ROMs, firmware 4.0 of 1991 and 4.1 of 1996.
  Ours is 8085 code, copyright through 1989.

So the names people see say Accent-PC from 0.9.5. Nothing about the sound changed. The voice's internal names
(`accentmini:sa`, the `accentsa` folder, the firmware folder) stay as they were, so that no saved choice is lost, and
speech-dispatcher still takes the old name. The true Accent SA is a different machine and would be a voice of its own.
On 2026-11-22 the code's own names followed (Tomi): the folder `src/csrc/accentsa` became `accentpc`, and the Python
hosts, the research library, the test switches and the voice constants say Accent-PC; the saved names above stay.

## 2026-11-22: Artic's own VEST speaks

A Business Vision 3.08 disk brought the missing piece for an Artic voice: `VEST.COM` 1.60, the text-to-speech the
SynPhonix cards ran on the PC. Read from its code, then run on MAME's 8086 with an emulated SynPhonix 215 around it
(`src/hosts/synphonix.py`), it found the card, spoke its own announcement and then any text, with its own number
reading. What it rests on, and what is left:

- The card was worked out from VEST alone: the key it looks for and where, the chip eight ports above, each byte
  rotated one bit, IRQ 7 per request, and the entry Business Vision calls ([front-ends.md](front-ends.md)).
- VEST refuses any card but the one it was sold with. The host reads the expected serial out of the program and
  gives the emulated card that key, so the repository holds no serial and any copy of VEST 1.60 finds its card.
- At XCK = 1 MHz its speed was heard as close to a real TransPort's, whose resident VEST should be much the same
  program. That is an ear, not a measurement: the SynPhonix's clock is still open.
- Not yet: a C port, a voice, Business Vision's own settings, and VEST's PortTalk module.

**Who speaks for Artic.** VEST is Artic Technologies' work ("(C)Copyright 1984,85,88,89,90,92 ... All Rights
Reserved"). Artic sells nothing today and seems to have closed long ago (see 1997 to 2003), and no one we know of
has taken its programs on. The people who could say yes or no to sharing VEST are its founders, Tim Gargagliano and
Dale McDaniel (see 1984). We have tried to reach Tim Gargagliano and had no answer. On 23 November 2026 `VEST.COM`
came into the repository with its serial set to 99999999, so that it names no card and no owner, and with its
notice (`firmware/artic-synphonix/README.txt`), as Aicom's software is kept. If you speak for Artic, we would be glad
to hear from you: open an issue, or write to the maintainer at the e-mail address on his GitHub profile. We will do
as you ask, and remove it if you want it removed.

## Engine versions at a glance

| Version | Heard by | Change |
|---|---|---|
| v0 | Astra's build plan | Tract at fc, source-tagged constants, binary capacitor rule, hold-out frozen |
| v0.2 | Tomi: a wobble at "three" | Transitions on the frame clock |
| v0.3 | Tomi: "shix" for "six" | ROM bit b02 selects where noise enters |
| v0.4 | Astra's review, and a separate Claude review | Controller fixes; closures held |
| v0.5 | Tomi: breath under the T | Amplitude glides faster than formants |
| v0.6 | A listener: "a weird chih thing"; Tomi: T as "ch" | Noise glides per path |
| v0.7 | Tomi: "Hello STomi" | The stop closes, then releases before its end |
| v0.8 | Tomi: a "d" before P | Only true stops release; the H holds stay shut |
| v0.9 | Tomi: a thick T; a listener: "trying so hard" | Amplitude glide ×6 |
| v0.10 | Astra's predicted consequence | Burst and aspiration rule; closure scaled to length |
| v0.11 | Tomi: "program", "manager" | Early release only into an open phoneme |
| v0.12 | A listener: a click in "still" | A stop that starts on silence closes at once |
| v0.13 | Tomi: the jump in "file"; the unit's fuller low end | F1 glides ×2.25; a partly stepped voice source; the Braille Lite's 5 kHz roll-off |
| v0.14 | A listener: the real chip's Z "rattles"; Tomi: ours was S-like | In Z, J, V and voiced TH the noise swells with each voice pulse |
| v0.15 | Timothy Wynn: "guess" sounded like "ess"; Tomi chose the clearer, consistent hard G; Claude implementation and Astra review | A K/KV-led HVC of at least two frames can open for its last half-frame into an open successor, with a 2 ms opening ramp; short Accent holds stay unchanged |
| v0.16 | Tomi's register programs on his real chip: ROM vowel pairs that differ only in F1; Astra (OpenAI Codex) fitted one pair, Claude checked the predictions independently, Tomi chose it by ear | F1's code-to-frequency rule and bandwidth, fitted to E/AY alone; it predicts AI/:A and the older vowels' low end |
| v0.17 | Tomi: fricatives closer to the chip; his ear on Astra's isolated noise-path audition ("judges" regains its grit on the Speak-Out, the Accent unchanged); Astra (OpenAI Codex) built and calibrated it, Claude integrated it | The noise takes F2..F5 sections of its own (Astra's earlier noise fit), the voice's sections none: the voice is v0.16's sample for sample |
| v0.18 | Tomi: a faint click at the ends of the Pinball lesson's right-side notes; Astra traced it to the tract retuning at once while the filters still rang, and Tomi heard both of Astra's fixes remove it | In the BASIC tones (mode 1, rate 15), PA's tract is slewed to once the sound has stopped, not retuned at once |
| v0.19 | Tomi: the Accent voices tick and rattle as they talk, a different grain on every vowel, and the Speak-Out and Braille Lite don't; Claude rendered the Accent's own writes with one suspect removed per file, and Tomi heard the ticking go only when the volume stopped stepping; his chip on the same writes has none | The volume follows the amplitude's slew instead of jumping a whole code at a time |
| v0.20 | Tomi recorded 426 pitch glides on his real chip through register programs; Claude measured the law and Astra (OpenAI Codex) confirmed it independently; Tomi heard it beside his chip on the Accent's sentences | The pitch glides one count per 16 (8 − G)(16 − R) clock cycles in every glide setting, replacing a table partly fitted, partly guessed |
| v0.21 | Kevin Horton drew the SC-02 prototype's schematic; Henri Asseily (Appletini) read it into a net list and showed in issue 31 that F3 and F4 share one code on two capacitor banks; Claude predicted held vowels of Tomi's chip from that bank before scoring them, and Astra (OpenAI Codex) reproduced it independently; Tomi heard it beside his unit, and caught a fault in the first listening clips that the numbers had missed | The fourth formant moves with the sound instead of standing still: it takes the third formant's code on a capacitor bank of its own |
| v0.22 | Listeners, Timothy among them, heard a ghost before the SynPhonix's bare E, and Tomi a "spit" before H on the Hungarian Braille Lite; Tomi played the firmware's exact chip writes on his real chip; Astra (OpenAI Codex) built the change and narrowed it to loads, Claude verified it on every platform, and Tomi approved it by ear | A phoneme a firmware loads at volume zero is silent, as on the real chip, instead of a fading sound before the word: the SynPhonix's E, and the starts of words on the Hungarian, Slovak, Czech, Polish, Russian and other Braille Lite voices |

## Add-on releases at a glance

The add-ons have version numbers of their own, apart from the engine's. Both tables grow
with each release.

| Release | Asked for or heard by | Change |
|---|---|---|
| 0.1.0 | Tomi, for the first testers | Speak-Out and Braille Lite add-ons: each device's own firmware, emulated, driving the chip model |
| 0.1.2 | Tomi's first try in NVDA: "they work!" | The Braille Lite starts at the unit's factory settings; no scraps of earlier speech when tabbing fast |
| 0.1.3 | Tomi: gaps between NVDA's phrases | Capital pitch change; "Join phrases" |
| 0.1.4 | Tomi: a pause at "Synthesizer:" | Lines split only at sentence ends; "Shorten pauses between sentences" |
| 0.2.0 | Tomi: NVDA 2021.1 and Windows 7, no runtime | No numpy: 1 MB instead of 13; NVDA 2021.1 and later, 32- and 64-bit |
| 0.2.1 | Tomi | Capital pitch never left raised after a cancel; symbols read once, not twice; **custom number processing** for the Braille Lite, whose own reading stops short of trillions |
| 0.3.0 | Tomi: "the entire thing in portable C" | The chip in C, identical to the Python in state and sound (engine v0.10); 12–15 times real time on old NVDA |
| 0.3.1 | Tomi: the Braille Lite wouldn't load beside the Speak-Out | Each add-on keeps its own copy of the engine |
| 0.3.2 | Tomi | The Braille Lite speaks 0.3 s after it's picked, down from 1.1 s |
| 0.3.3 | Withdrawn: Tomi liked 0.3.2 better | A faster rules pass that made the gaps longer |
| 0.3.4 | Tomi: "fixed it in 0.3.4" | Leading silence dropped; the Braille Lite answers a key in 12–22 ms |
| 0.3.5 | Tomi: the Braille Lite stopped mid-post | Long lines no longer end speech early; **the Accent-mini joins** as a third add-on |
| 0.3.6–0.4.0 | Tomi: the Accent-mini froze while scrolling, and went silent after a variant change | Accent-mini only: both freezes fixed, numbers as words, faster long items, first sound in about 25 ms instead of 600 |
| 0.5.0 | Tomi, and listeners | **The Accent SA** as a second Accent voice; engine v0.11 and v0.12; Windows on ARM fixed; the repository goes public |
| 0.6.0 | Tomi, a listener, and a tester | Engine v0.13 (with a bounded T/P/K precharge on the Braille Lite); the Braille Lite's top rates fixed, its dollar amounts read, no utterance cut short after a cancel, and a "Sample rate" setting in all three (11, 22 or 44 kHz; 22 kHz by default); the Braille Lite's tones run 0-26 (tone 0 added), its voice inflection can be switched off, an optional hiss or whine generated from the chip's clock, and **a Spanish voice** with NVDA's automatic language switching |
| 0.7.0 | Tomi: every shipping CPU on MAME, no GPL runtime | Every unit on MAME's extracted CPU cores (BSD-3-Clause) and our own MIT code; **the Blazie emulator** (the Braille Lite 2000 and the Type 'n Speak, English and Spanish, with a Braille 'n Speak 2000 preview) for Windows and Linux; Linux, Android and Python packages |
| 0.7.5 | Tomi: "all voices, no exceptions" | Every voice on NVDA, SAPI, Linux and Android from one native C library, checked byte for byte against 0.7.0's drivers; SAPI without Python (and on 32-bit Windows); the Speak-Out imported from `SPEAKOUT.HEX` and the Accent-mini built in on Android; the BT Speak and BT Braille front end |
| 0.7.6 | Tomi's ear and Astra's review; Jayson (issue #3); issue #8 | The hard G (`hold_release`): "guess" no longer says "ess"; the emulator's clock keeps this year's calendar across the unit's New Year; the Accent SA's text is done only when the unit has nothing left to say, so dialog labels ending in a colon are no longer cut off |
| 0.7.7 | Garrett: Linux left the SSI-263 as the only synthesizer (issue #10, his fix in PR #11) | speech-dispatcher keeps your other synthesizers; Blazie's Simon game plays again |
| 0.8.0 | Tomi, and the anonymous donors of Blazie's language updates | One NVDA add-on for every unit; **the Mockingboard**, in two versions; the Braille Lite 2000 in 19 languages; engine v0.18 (fuller vowels, grittier fricatives, clean BASIC tones) |
| 0.8.5 | Tomi, and a Braille 'n Speak 640 owner (issue #15) | **GW Micro's Sounding Board** as a voice; the Braille 'n Speak 640 in the emulator, with the first Polish Braille 'n Speak firmware we know of; the double-speed upgrade; a unit screen on Android; pitch glides on the law measured on a real chip (engine v0.20) |
| 0.9.0 | RetroBunn (PR #24); Steve (PR #23) | **Automated Functions' SmarTalk** on every platform; the Mockingboard starts each text clean; long texts on the Sounding Board and Speak-Out speak to the end; double speed no longer holds a vowel; the BT Speak's Blazie menus follow the device's own |
| 0.9.1 | Tom and Tyler, listeners; Steve (PR #26) | The SmarTalk says contractions as words; at slow rates the Speak-Out and Sounding Board no longer start a letter with the end of the last; the release builds the BT Speak package itself |
| 0.9.2 | Daniel (PR #29); Zachary (PR #27); Steve (PR #28) | The SAPI voices follow a program's volume, its changes of rate and pitch inside a text, spelling and skipping by sentence; Orca shows the voices by name; an update over a running voice on Linux finishes; the emulator keeps the owner's name and all 768 KB of a unit's memory |
| 0.9.5 | Tomi; Guillem (PR #90); RetroBunn (PR #91); aaron-gh (issue #30); Kevin Horton and Henri Asseily (issue #31) | **Artic's SynPhonix** (its own VEST.COM) and **Speech Systems' Super Voice** (its program downloaded or imported, never shipped) on every platform; engine v0.21, the fourth formant tuned by the third's code (the SC-02 prototype's drawing); the units' own index marks, and say all's lines kept together; long numbers digit by digit; the Accent SA named the Accent-PC; a full audit (59 issues) and a second, independent review before release, with two checks every voice must pass: a cut-off text never comes back, a row of punctuation never stops a voice |
| 0.9.6 | Listeners, Timothy among them; Tomi, on his own chip | Engine v0.22: a phoneme a firmware loads at volume zero is silent, as on the real chip -- the ghost before the SynPhonix's bare E and the blip before words on the Hungarian, Slovak, Czech, Polish, Russian and other Braille Lite voices are gone |

## 0.7 migration credits (2026-09-30, release preparation)

Tomi asked for every shipping CPU to use MAME, without an opt-in or a GPL runtime.
Claude implemented the extracted cores and portable boards and is integrating the
Linux, Android and Python distributions. Astra (OpenAI Codex) reviewed the CPU and
host contracts, reproduced the interrupt-slice and completion edges, and implemented:

- the Accent SA fix that defers the vector instruction's effects across a host slice
  boundary (`6f588c7`, originally `12806c7`), with regression and mutation checks;
- the Windows MAME defaults, native builds and three 0.7.0 add-on packages
  (`816aebc`), including the shared release builder and MIT/BSD notices;
- independent package checks outside the research checkout, covering all four voices
  on 64-bit Python 3.13 and 32-bit Python 3.7 with NVDA stand-ins.

The checks establish packaged driver operation; they do not replace Tomi's listening
or claim that release preparation is complete. Our own code is MIT; the extracted
MAME components retain their BSD-3-Clause notices, and the firmware remains separate.

## Withdrawn along the way

Being wrong in writing, and saying so, was part of the method. A few of each:

- **Claude withdrew:** the first exponential pitch law and everything else recorded before
  the line-hold trap was found; "tone 25 saturates" (the window caught an onset pop); the
  capture-folding explanation of the unit's clock lines; "the notch shows F3 at 3.1–3.2 kHz";
  "the 4.3 kHz feature is solved"; "F5 is probably fixed".
- **Astra withdrew or corrected:** a first lattice placement that failed its own spacer
  controls; two of its own frozen ROM cells, after the larger-context review; an
  over-strong reading of the documents' articulation text.
- **The rule that came out of it:** every claim carries its source, every spectral trick
  gets a no-effect control, predictions are frozen before the data, and hold-out material
  is looked at once, in writing.
