# Open questions

What isn't known yet. Written at 0.5.0 (engine v0.12), brought up to date for 0.8 (engine
v0.18), and given the family of devices at 0.9.1. Where later work answered or narrowed a
question, the item says so and points to [history.md](history.md). Each one says where it
would be settled: the die, the recordings, a board, a document, or the ear.

If you can help with any of these (a photograph of a board, a manual, a firmware or update
disk, a recording of a real unit), please open an issue or get in touch. Describe firmware by
its name, size and SHA-256 rather than attaching it: the project doesn't take firmware from
contributors, but it very much wants to know what exists.

## Settled since 0.5.0

- **The Braille Lite's "?"** (listed below under the front ends until 0.8). The unit has two
  speech-box modes. Word mode (all six dots, no space) was how the MASTER recordings were made,
  and it loses almost every "?" rise and "." fall. Line mode (space plus all six dots, then L)
  is what the add-on uses: there every question rises and every statement falls, and the
  add-on follows the unit (M03, 2026-09-28). "Questions never rise" was the mode, not the
  chip, and not a reason for a different pitch mechanism.

## On the die

- **Which way round are the bits?** "Oval = 1" is a convention that fits the decode well,
  not a traced fact. Column = 63 − code rests on Casso's reading of the decoder; neither
  later reader has traced the decoder independently.
- **What each ROM row drives.** The interleaved field layout fits the data; none of it is
  traced from row to latch to analog control. (Engine v0.16 fitted F1's code-to-frequency
  rule and bandwidth from ROM vowel pairs that differ only in F1, played on the real chip:
  acoustic evidence for the F1 rule, not a trace of what PAR drives.) In particular: is PAR
  F1's lowest bit? Does NAS drive F2Q? Where do the flags b00–b04 go: a closure gate, noise routing, or the
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
  Braille Lite? The same question runs through every name the chip was sold under: the SSI
  263P (dated 8404 to 8431 on the boards we've seen), the 263AP (8513 to 8601), Votrax's SC-02,
  TDK's 78A263A, Artic's Artic-263, Aicom's AI901, and the Semtech-made SSI 263-2-2 in a late
  Braille 'n Speak 2000. Their marking format and lot numbers suggest one die (see
  [history.md](history.md)), but none has been compared on the die. Mask revision marks at the
  die's edge would tell revisions apart; so would the same register writes played on each chip
  and recorded.
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
  power. The SC-01's high-pass noise shaper is worth testing on the recordings. Since v0.17
  the noise runs through F2..F5 sections of its own, beside the voice's (the voice is v0.16's
  sample for sample). The measured noise shapes came much closer, while some levels are still
  off (F, TH and Z too quiet). That is a model construction, not an identified circuit:
  separate voice and noise poles are not proven.
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
    with the direction of travel confounded. Setting 3 falling now measures about 7.5 ms/s at
    rate 2 (F01's stacked "?", two independent fits), where the engine borrows setting 5's 11.
    Settings 6 and 7 are still set to setting 5's speed, fitted on the Accent; setting 7 rising is
    fast, but its speed isn't measured yet.
  - *Target 0*: the User Guide gives the target range as "0 to 1F (lowest to highest,
    0 = silent)". The recordings disagree so far: an old pitch-command-1 take (target 0) is voiced
    at 61 Hz, and in F01 the fifth stacked "?" (which wraps to target 0) sends the pitch steeply
    down to about 104 Hz, voiced, before the next target takes over. Neither shows silence, and the
    guide's wording is unexplained.
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

## The family of devices

The timeline is in [history.md](history.md), "The SSI-263's family, year by year". What it
can't say yet:

- **The BrailleMate's insides.** Tomi recognised the SSI-263 by ear in Telesensory's tutorial
  recordings, and a 1993 product list calls its speech an "optional built-in speech
  synthesizer". MicroTalk's ASAP manual (1991) says it takes Artic's PortTalk command set, which
  fits an Artic-263 and Artic's text-to-speech inside, but doesn't prove either: another maker
  could have adopted PortTalk, as ASAP says several did. No board photograph, ROM or update disk
  has turned up. Telesensory's Screen Power Speech 2.0A has six builds, one per synthesizer; the
  BrailleMate's is most likely `SPS.002`, the only one besides the Accent SA's that opens a
  serial port (an inference, not a trace); its serial writes should be PortTalk. Settled by: a
  board photo, a ROM dump or an update disk; tracing `SPS.002`'s serial writes.
- **The VoTalker.** Votrax's VoTalker appears in MicroTalk's ASAP manual (1991) and as a build
  of ETS's Enable Reader (1986). ASAP says it needed `SONIX.COM` and `TTS.COM` to talk, and Sonix
  is an Artic trademark, as in the SynPhonix's `SONIXTTS.COM`: a strong hint that the VoTalker
  ran Artic's software, and perhaps carried Artic's chip. Which chip it used isn't known to us.
  Settled by: a board photo, its manual, or its software disk.
- **The first Braille 'n Speak** (1987) and the first Braille Lite: we have seen chips and
  firmware only from 1997 on. Settled by: board photos of early units.
- **Artic's later synthesizers and notetakers.** The Mini TransPort (1995) and the Spirit 600
  and 600B (1997 or later) never name their chip; the Mini TransPort's manual says only that
  "Artic-263" is an Artic trade name, and the Spirit's speaks of "a TRUE phoneme speech synthesis
  chip". The Spirit's manual also mentions Artic's braille and QWERTY notetakers with built-in
  synthesizers; which ones, and with what chip, we don't know. The Mini TransPort's firmware is
  "user upgradeable via a diskette", so an update disk may survive. Settled by: board photos,
  or that disk.
- **The clocks.** The chip's clock (XCK) sets both its pitch and its filters, so every board's
  clock matters. Known: 1 MHz on the Braille Lite and the Speak-Out, from the pitch of real
  recordings. Not known: the SynPhonix (a 74HCT161 divider, probably from the
  PC's bus clock), the Heath card (a 4 MHz oscillator and a flip-flop), the HX-20 module (a
  3.579545 MHz crystal, maybe divided), the CPC's Sprechi board, and the Accent SA's own CPU
  clock (3.072 MHz is a guess). Settled by: tracing the boards, or the pitch of a real recording
  against the register values that made it.
- **Sweet Micro's foreign-language rule tables.** Chip Talk (1987) says Sweet Micro sold
  "Foreign Language Rule Tables", German among them, for the Mockingboard's SSI-263. We haven't
  found the disk. Nor have we found the software for RC Systems' multilingual Slot-Buster.
- **The Mockingboard D's timing.** The D runs the same text-to-speech as the other Mockingboards
  on the IIc, but its own 6803 writes the chip, on its own timer. Whether that changes the timing
  enough to hear is open until the D is built: a 65C02 for the IIc's driver, the IIc's serial
  chip, a 6803 and the AY chips around our SSI-263.

## On the front ends

- **Volume above 7** compresses on the unit; where is unknown.
- **The Accent SA's clock** (3.072 MHz is a guess) and its first-word delay.
- **The SmarTalk's BUSY and PAPER-OUT lines** read as a powered box's levels, but what drives
  them is not known. Two short real SmarTalk recordings exist (11 kHz, line in); they confirm
  its power-up speed but are too narrow for its fricatives. A wider recording of a real box
  would settle its tone and noise levels.
