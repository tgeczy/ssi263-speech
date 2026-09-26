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
  unmeasured.
- **The pitch glide.** Listeners hear the Blazie add-on as flatter than the unit, and the "?"
  stacks plateau. Every piece of the glide, with its source and what would settle it:
  - *Which bits do what* (settled): in transitioned inflection, I10–I6 (R1 bits 7–3) set one of
    32 targets and I5–I3 (R1 bits 2–0) the rate of change; I11 and I2–I0 (R2's low bits) always
    act at once. Source: the A data sheet, and the 1986 User Guide's "Slope of Inflection (I5–I3),
    0 to 7". The recordings agree: changing only R1's lowest bit changes the glide speed and not
    where it ends (G02). The Byte 1984 article's parameter list puts "the rate of inflection
    transition" in the speech-rate register's low bits; its own register table and G02 contradict
    that, so it is set aside.
  - *Rate scales with speech rate* (measured, R01: slope × (16 − R) ≈ 62 ms/s). The data sheet's
    "speech rate does not affect inflection" is read as the pitch *value*; "all internal
    attribute transitioning is performed relative to the Speech Rate Register" covers the speed.
  - *Speed of each setting* (partly measured): settings 0, 1, 2, 4 and 5 were measured at rate 2,
    with the direction of travel confounded. Settings 3, 6 and 7 are set to setting 5's speed,
    fitted on the Accent. The Braille Lite's own "?" stacks step through all eight settings, and
    its sentence-final fall uses setting 2: the F01 recording (stacked "?" at rate 10 and rate 2,
    "Battery low." at two rates) measures them.
  - *Target 0*: the User Guide gives the target range as "0 to 1F (lowest to highest,
    0 = silent)". The engine plays target 0 as a very low pitch. The Braille Lite's fifth stacked
    "?" wraps to target 0, so F01 tests it.
  - *Constant slope, not fixed time* (measured for setting 0): in G01 the distance did not change
    the slope (16↔32 arrives in 0.75 s at rate 2, 16↔48 in 1.65 s). The other settings are
    assumed to behave the same. Still open: the die path from R1's latch through the rate
    divider to the 12-bit pitch counter, which would explain how the chip does it.
- **Amplitude transitions.** The data sheet says amplitude moves "at rate dependent on the
  phoneme duration setting"; the engine does that for the amplitude register, but moves the
  phonemes' own voice and noise amplitudes at a fitted 6× the formant speed. Stop bursts and
  the T that listeners hear as a flap depend on it. Next: development T tokens grouped by their
  duration bits, unit against engine; then the die (what clocks the amplitude counters).
- **The engine's floor** is digital silence, where the unit has a capture floor.

## On the front ends

- **The Braille Lite's "?"** never rises on the real unit, but the emulated firmware raises
  it, and places its punctuation tokens differently.
- **Volume above 7** compresses on the unit; where is unknown.
- **The Accent SA's clock** (3.072 MHz is a guess) and its first-word delay.
