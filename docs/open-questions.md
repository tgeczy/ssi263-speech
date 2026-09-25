# Open questions

What isn't known yet, as of 0.5.0. Each one says where it would be settled: the die, the
recordings, or the ear.

## On the die

- **Which way round are the bits?** "Oval = 1" is a convention that fits the decode well,
  not a traced fact. Column = 63 − code rests on Casso's reading of the decoder; neither
  later reader has traced the decoder independently.
- **What each ROM row drives.** The interleaved field layout fits the data; none of it is
  traced from row to latch to analog control. In particular: is PAR F1's lowest bit? Does
  NAS drive F2Q? Where do the flags b00–b04 go: a closure gate, noise routing, or the
  no-frication and no-voice gates an SC-01 patent shows? Is there a closure-delay counter?
- **Where F4 comes from, and whether F5 is fixed.** No ROM field supplies F4, and the
  design paper's two figures disagree about F5. What are the clock-scaled features at about
  0.163 × fc and 0.24 × fc?
- **Which block is which filter,** and the capacitor plates, switches and amplifier nodes
  of each. The agreed next target is the output chain: from the output pin through the
  buffer, the sample-and-hold and the high-pass with volume.
- **The pads.** Which pads are TP1, TP2 and the audio output; the two possible underpasses
  in the north-west; a second reading of the seven pad routes.
- **The TP pins' insertion point:** before or after voice amplitude and the closure gate,
  and what they reveal about the glottal pulse's shape.
- **Revisions.** Is the photographed P die equivalent to the later AP? Which one is in the
  Braille Lite?
- **A better picture.** The full 17,265 × 14,313 master, or other layers, would help every
  item above.

## On the chip model

- **How the chip knows what comes next.** The real chip releases a stop early only into a
  vowel-like sound, though the firmware's writes are identical either way. The engine
  models it with an early request and held writes, which changes the bus protocol; Astra
  suggests comparing a release triggered by the next load.
- **The Speak-Out's longer phrases.** Real Speak-Out phrases run 11–13 % longer than ours,
  while the Braille Lite matches the data sheet to 0.5 %. Next: model access timing within
  a CPU batch rather than scaling durations.
- **Dark J and SH at 4–6 kHz,** and stop releases 11–41 dB short at 4–9 kHz. The noise enters
  at F2 and F5, and the two paths can cancel by phase, so more F5 weight isn't simply more
  power. The SC-01's high-pass noise shaper is worth testing on the recordings.
- **The B release in "robotic":** the real Accent has about 30 ms of broadband noise there;
  ours has none.
- **Voiced closures** on the unit show a voice bar at about −14 to −19 dB re the line; ours
  are silent. First, separate real harmonics from the fading vowel and the idle clock lines.
- **The bass:** the engine is 2.7–6 dB short below 300 Hz against three different units.
  The glottal pulse or the high-pass stage.
- **Transitions:** constant slew or fixed duration? Articulation settings other than 5 are
  unmeasured, and the glide fields 3, 6 and 7 are a fit from the Accent.
- **The engine's floor** is digital silence, where the unit has a capture floor.

## On the front ends

- **The Braille Lite's "?"** never rises on the real unit, but the emulated firmware raises
  it, and places its punctuation tokens differently.
- **Volume above 7** compresses on the unit; where is unknown.
- **The Accent SA's clock** (3.072 MHz is a guess) and its first-word delay.
