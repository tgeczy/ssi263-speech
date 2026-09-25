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

## Before this project

- **1984.** Silicon Systems' SSI-263 is described at ISSCC (Maeding, Austin and Maimone).
  It descends from Votrax's SC-01, and Votrax sells it as the SC-02. The chip photographed
  later is marked "SSI 263P / P 8404".
- **Visual6502.org** photographs an SSI-263P die from chips an anonymous donor sent: Greg
  James shoots 203 images, Christian Sattler stitches them, and a 7000 × 5803 picture goes
  online with the data sheets.
- **August 2026.** Casso, Rob Elmer's Apple II emulator, reads the ROM's 1,856 cells off
  that picture for its Mockingboard speech, and on 2026-08-26 decodes them as
  significance-interleaved 4-bit fields (the layout the SC-01's decap had shown). Its
  code-to-hertz table is borrowed from the SC-01.

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
