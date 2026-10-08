# Sources and credits

## The die

- **Visual6502.org, [Silicon Systems SSI-263P speech chip die shots](http://www.visual6502.org/images/pages/Silicon_Systems_SSI_263P_die_shots.html).**
  Photographed by Greg James (203 images), corrected and stitched by Christian Sattler,
  from chips an anonymous donor sent. The only physical source for the ROM read and every
  metal trace. The page also hosts the SSI-263A data sheets, the programming guide and an
  SC-02 scan.
- **Casso,** by Rob Elmer ([github.com/relmer/Casso](https://github.com/relmer/Casso), MIT):
  the first read of the ROM, the interleaved decode, and the SC-01 cross-check. Vendored in
  `third_party/casso/` at commit `7a10894` with its license.

## The manufacturers

- **Silicon Systems, SSI-263A data sheet and User's Guide,** in the 1985 and 1986 data books:
  the filter-clock formula fc = XCK / (2 (256 − FF)), linear transitions, the mode chart,
  field names, amplitude defaults, and a worked "Hello" with full register streams (the
  engine's first test, from Astra's checked transcription). Two label errors in the printed
  guide are noted.
- **Votrax, SC-02 data sheet** (12/85): the same chip under Votrax's name.
- **Votrax, SC-01 data sheet** (1980; a later revision reads 10-14 V and adds the SPEECH-PAC board's manual and
  schematic): the 64 phonemes with their lengths at 720 kHz, the two pitch pins, the master clock that moves pitch
  and speed together, and the block diagram of voiced and fricative sources into four filters. The SSI-263's
  ancestor, for comparison.
- **Votrax, "Phonetic Speech Dictionary for the SC-01 Speech Synthesizer":** about 1,400 English words in
  Votrax's own SC-01 phoneme spellings.
- **Maeding, Austin and Maimone, "An integrated phoneme speech synthesizer",** ISSCC 1984,
  [doi:10.1109/ISSCC.1984.1156580](https://doi.org/10.1109/ISSCC.1984.1156580). Licensed to
  Tomi, so it is summarised here, never reproduced. It gave the architecture: the SC-01
  lineage, metal-gate CMOS, a glottal source through five switched-capacitor sections, noise
  shaped into F2 and F5, a high-pass stage with volume, a sample-and-hold, a separate F2Q
  control, and transitions latched in step with the vocal-tract clock.

## Patents

- **US 4,433,210** (the SC-01; Federal Screw Works): cascaded switched-capacitor
  resonators, a stepped glottal pulse, and a high-pass noise shaper with no-frication and
  no-voice gating, used as shapes to look for, not as facts about the SSI-263.
- **US 4,470,150, US 4,264,783, US 4,829,573:** related Votrax designs; none gives the SSI's
  circuit.
- **EP0396141A2** (Schneider, Ott and Jalass: Kraftwerk's Robovox): the lead on the TP1 and
  TP2 test pins as an external excitation input.

## Other sources

- **MAME's `votrax.cpp`,** a die-derived SC-01 model: the interleaving precedent and a
  structural comparison. Its numbers belong to the SC-01.
- **redcedar's Votrax pages** (about 2006-2007): the SC-01 / SC-01-A difference from Jonathan Gevaryahu's 2007 decap,
  and what became of Silicon Systems (TI's storage group, TDK's semiconductor arm).
- **Steve Ciarcia's SSI-263 article, Byte, March 1984:** a project with register listings.
- **Elovitz et al., 1976, the NRL letter-to-sound rules:** the notation of the Braille Lite
  and Speak-Out rule tables.
- **A net.micro Usenet post of 21 March 1985** reporting a reset defect in a 263P design.
- **Zilog's Z8018x manual:** the serial divider that gives the Braille Lite's 6.144 MHz
  CPU clock.
- **The Accent manual:** number reading (section 4.1.4) and speech triggers (3.2.1.14).
- **For the Aicom history:** see [firmware/AICOM.txt](../firmware/AICOM.txt).

## Software

- **Unicorn 2.1.4** (GPLv2) runs the Speak-Out and Accent-mini firmware; its source is pinned
  in `src/csrc/` with a configure fix and the Windows-on-ARM patch.
- **z180emu** (GPLv2, a C port of MAME's Z180) runs the Braille Lite firmware; its core is
  vendored in `third_party/z180emu` with this project's three patches (`PATCHES.md` there), and
  its source ships in the Blazie add-on.
- **MAME's i8085** was the reference for the 8085's interrupt details; the CPU test
  programs TST8080 and 8080PRE checked the core.
- **MAME's i8085** (BSD-3-Clause; Juergen Buchmueller, Roberto Fresca, Grull Osgo), at
  revision 1c924ea7, is extracted into the library's 8085 core (`src/csrc/cpu/`, see
  `mame_i8085/PINNED.txt`), with the changes Intel's MCS-80/85 Family User's Manual decides.

## People

- **Tomi** (Tamas Geczy): the hardware, the recordings, the firmware and documents, the
  direction, and the ears.
- **Astra:** the die, the controls, the reviews, the engine's plan, and the ARM fix.
- **Claude:** the measurements, the firmware emulation, the engine and the add-ons.
- **spacepup:** the Accent SA's ROMs.
- **Listeners** on the fediverse, who heard what the metrics missed. They're thanked here
  without names, as they haven't asked to be named.
