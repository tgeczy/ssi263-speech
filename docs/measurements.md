# Measuring a real SSI-263

The model is checked against one real chip above all: Tomi's own Braille Lite 2000, driven
in speech-box mode over its serial port and recorded from its line out. This page records
what was measured, how, and the lessons that cost the most to learn.

## The unit and the harness

- **The unit:** an 18-cell Braille Lite 2000 from before the Millennium; the later Millennium
  used a DoubleTalk, so a "Braille Lite recording" is only SSI-263 data if the model is right.
- **The harness** (a separate tool, not in this repository) sends text and settings over the
  serial port and records the line out at 44.1 kHz. Settings are the unit's own: ^E n T for
  tone 1–25, ^E n P for pitch 1–63, ^E n V for volume 1–16, ^E n E for rate 1–16, ^X to
  flush, and ^F as a marker the unit echoes back when a line is done.
- **The trap that cost a day:** the unit holds each line and speaks it only when the next
  transmission arrives, while ^E settings act the moment they arrive. So every line was
  being spoken with the next line's settings, and the markers ran one behind. The fix is a
  flush protocol: an empty line right after each line.
- **Analysis hygiene:** always high-pass at about 40 Hz before choosing windows. The unit
  makes a DC step at every utterance onset, and it once looked like a "saturation ceiling".

## What the silicon showed (2026-09-22)

- **Pitch:** F0 = 1953.125 / (32 − ⌊cmd/2⌋) Hz, to ±3 cents from 63 Hz to 1953 Hz. That is
  the data sheet's XCK / (8 (4096 − I)) with XCK = 1 MHz, with pitch commands in pairs, so the
  unit has 32 pitches.
- **Tone:** a global stretch of every resonance, about 4.1× from tone 1 to 25. A 96 kHz
  capture showed the voice imaged on both sides of the switched-capacitor clock, at
  fc = 500 kHz / (32 − tone), within about 0.1 % for tones 1–20: so tone t writes
  FF = 224 + t, and the whole vocal tract follows fc.
- **In-chip aliasing:** at the highest pitches, excitation harmonics fold around the filter
  clock back into the audio band (at pitch 62/63, about 57 % of the 100–6000 Hz power sits
  on the predicted clock lines). A candidate for the "grit" of very high pitches.
- **The idle chip** keeps its old tone and pitch until the next line starts, and its clock
  leaks into the silence as a family of lines, fc/2 to fc/64. The model carries an idle
  carrier for it.
- **The glide:** pitch glides linearly in period, starting with the next line and running
  through silence until it reaches its target. Its slope scales with the speech rate:
  field 0 × (16 − R) ≈ 62 ms/s.

## The MASTER capture (2026-09-24)

The unit's battery lasts one session, and recharging wears the unit, so everything went into
one unattended run: 2243 lines, about 78 minutes, recorded 2026-09-24 from 07:35 to 08:38 with
every line confirmed and no missed markers. Sections, in order: a frozen reference set;
reset-default material; a phoneme inventory at rate 2 (55 audible code and duration cases in
121 words, four repetitions); the H words; rate 10; timing at rates 1–15; pitch; tones 1, 7,
19 and 25; the glide-rate law; punctuation fields; sentences, letters and digits.

Before the run, every one of the 2243 lines was replayed through the emulated firmware, and a
fake unit rehearsed drops, hangs and resumes. The audio outputs were frozen with checksums
the same day.

## Development and hold-out

- **Development material:** section B's inventory, repetitions 0–1 only, plus the other
  sections where a tool says so. Every chip parameter was fitted on these.
- **Hold-out:** sections H1–H3 of the capture, and external recordings H4–H9 (a real
  Speak-Out, other Blazie units, and the Accent). Nothing is tuned on them. A change may be
  checked against them once, after it was made on development grounds, and `src/HOLDOUT.md`
  logs every such look.
- **The ear decides** when a metric and Tomi's listening disagree. It has been right every
  time so far.

## Lessons

- **A shift needs a negative control.** A notch that "moved F3 by 100 Hz" turned out to move
  any resonance about that much on its own. Every spectral manipulation now gets a no-effect
  control before its result is read.
- **Top-N lists can't prove absence.** "No poles at 4.3 kHz" came from a top-N export; the
  full list said "rarely", not "never".
- **LPC is biased on this voice.** An analysis-by-synthesis harmonic fit recovered a known
  pole within 3 Hz where LPC was off. With it, F1–F3 follow fc within 1–2 % across tones.
- **Measure the listener's latency,** not the audio's lead-in: when the audible block is
  actually fed to the sound card is what a screen-reader user hears.
