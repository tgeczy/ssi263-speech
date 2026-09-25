# Source Tilt Experiment: the voice is 11 dB short above 800 Hz

**Feature**: `024-mockingboard-speech` | **Issue**: #123 | **Date**: 2026-09-05

**Status: RUN on 2026-09-05. The hypothesis is REFUTED as a single-parameter
change.** Moving the glottal source's break frequency does buy F2 and F3 the
energy they lack, but it pays for that energy out of the 80-200 Hz band at a
rate of roughly 4 dB lost for every 1 dB gained, which is the thin and buzzy
voice `8b4a2556` already repaired once. No code change came out of this. See
**Results** below; the constant is untouched and still 50.0.

What remains open is the deficit itself, which is real and is not the break
frequency's to fix. See **Where to look next**.

Read the provenance section at the end before acting on any number in this file.

---

## The finding

Measured against a recording of a real SSI-263, the synthesized voice carries
about **11 dB less energy above 800 Hz** than the chip does. Two consequences
follow, and both are audible:

- About **99.5% of a spoken line's energy sits below 800 Hz**.
- Sweeping the filter frequency register barely changes the sound.

The second one is the tell. That register scales the whole tract
(`GetFilterFrequencyHz` in `CassoEmuCore/Devices/Mockingboard/Ssi263.cpp`,
against `kNominalFilterHz`, clamped to 0.5x-2.0x in `GenerateSample`), so
sweeping it moves all three resonators together. If moving them is close to
inaudible, little energy is reaching them.

## Why 800 Hz is the line that matters

800 Hz falls almost exactly on the F1/F2 boundary of the chip's own parameter
ROM. Over the 63 phonemes in `s_kPhonemes` that carry nonzero formants:

| Formant | Range | Above 800 Hz |
|---|---|---|
| F1 | 176-731 Hz | 0 of 63 |
| F2 | 722-2408 Hz | 61 of 63 |
| F3 | 1424-2832 Hz | 63 of 63 |

So "11 dB short above 800 Hz" reads as "F2 and F3 are underexcited", and F2/F3
is where nearly all phonetic distinction lives. The same fact explains why
sweeping the filter register accomplishes so little: there is not much energy up
where that sweep would be heard.

## Where the tilt is

The voiced path is a chain of slopes, all in `Ssi263.cpp`:

| Stage | Slope | Constant |
|---|---|---|
| Glottal source, two cascaded one-pole sections | -12 dB/oct | `s_kSourceBreakHz` = 50.0, converted to `m_sourcePole` in `SetSampleRate` |
| Three Klatt-style resonators at F1/F2/F3 | -- | `s_kBandwidthHz` = 60/90/120 |
| Lip radiation, a first difference | +6 dB/oct | normalized by the device rate |
| Output low-pass, two cascaded one-pole sections | -12 dB/oct above its knee | `s_kfOutputLpCoef` = 0.32 |

Net voiced slope is -12 plus 6, so -6 dB/oct. The reference recording, of the
chip speaking at 88.7 Hz, falls at about **-8 dB/oct** from the fundamental.
The slope is therefore close; the deficit is in how much reaches F2 and F3, not
in the far-field slope alone.

**The source tilt is the -12 dB/oct rolloff of the glottal source**, and its
break frequency is the single constant the experiment would move.

## The proposed change

Make the source tilt shallower, or move its break up, so more energy arrives at
F2 and F3. Then re-measure against the reference recording band by band and
compare the per-band mean error against the current figure.

## Why this is gated rather than simply tried

**This exact constant has already caused one regression.** Commit `8b4a2556`
("break the glottal source below the pitch range, not inside it") fixed a break
sitting at 805 Hz, which left the response flat underneath and let lip radiation
tilt the output upward across the whole F1 region: 21 dB short at 80-200 Hz,
9-10 dB heavy from 400 Hz up, heard as a thin and buzzy voice. Raising the break
again moves back toward the failure that was just repaired.

**The tilt is not independent of the constants around it.** All of these were
fitted together against the same recording, and moving the source tilt puts
several of them out of adjustment:

- the F1 compensation, `731.0 / max(F1, 170.0)` in `GenerateExcitation`, which
  exists to offset the radiation tilt for low-F1 vowels
- `s_kfOutputLpCoef`, whose two poles were tightened to match the chip's fall
  above its top resonance
- `s_kfVoicedGain` and `s_kfOutputGain`, which set absolute level
- `s_kfNoiseGain`, `s_kfNoiseLpCoef`, `s_kfFricLpCoef` and
  `s_kFricBandwidthHz` on the parallel fricative branch, since the
  vowel-to-sibilant balance is a ratio between the two paths

A change to the source tilt that does not refit at least the fricative balance
will move the sibilants as a side effect.

## Instruments, all already in the tree

No new tooling is needed. Spec 028 shipped everything this requires:

- `Apple2/Demos/Mockingboard/Speech/pulse-probe.a65` and `phoneme-probe.a65`,
  with committed `.dsk` images under `Apple2/Demos`
- `CASSO_AUDIO_DUMP`, which captures the generated mix as raw float32 stereo
- `CASSO_AUDIO_DUMP_DEVICE`, which captures the far side of the pending queue,
  so the two can be differenced

Derive the capture rate by differencing the lengths of two captures rather than
assuming 48 kHz.

## What would count as success

1. Per-band mean error against the reference recording improves, and no band
   gets worse by more than it gains elsewhere. The fricative work reached a mean
   of 4.9 dB across six bands; that is the number to beat.
2. The share of a line's energy below 800 Hz falls well short of 99.5%.
3. Sweeping the filter frequency register produces an audible change.
4. The voice does not become thin or buzzy again. Compare against `8b4a2556`'s
   before-and-after directly, since that is the specific regression at risk.
5. The sibilants hold. Z against L was 33 dB apart after the fricative work; a
   refit must not close that.

## Results, 2026-09-05

### Method

`pulse-probe.dsk` on `--machine Apple2e`, which holds EH unchanging for several
seconds, captured through `CASSO_AUDIO_DUMP`. The capture rate was derived by
timing the dump file's growth over a measured interval rather than assumed:
**48003 Hz**, so 48 kHz. The steady hold runs 2.0-5.75 s into the capture; every
figure below is the 2.25-5.50 s window, Welch averaged, 8192-point FFT.

Only `s_kSourceBreakHz` changed between runs. Each value got a full rebuild and
its own capture. Formant peaks are read at EH's stored formants, F1 577, F2 1823
and F3 2511 Hz.

Levels are quoted **relative to the F1 peak**, because overall gain is a free
parameter that would be retuned anyway: raising the break lifts the whole output
by up to 24 dB, and renormalizing to equal loudness is what a listener hears.

### The sweep

| `s_kSourceBreakHz` | below 800 Hz | F2 - F1 | F3 - F1 | 80-200 Hz - F1 | rolloff above F1 |
|---|---|---|---|---|---|
| **50 (shipping)** | 98.38% | -19.0 dB | -35.0 dB | **-4.6 dB** | -37.8 dB/oct |
| 100 | 98.18% | -18.8 dB | -34.8 dB | -7.9 dB | -38.0 dB/oct |
| 200 | 96.73% | -16.9 dB | -34.9 dB | -17.7 dB | -6.2 dB/oct |
| 400 | 91.17% | -13.2 dB | -33.8 dB | -29.7 dB | -4.7 dB/oct |
| **805 (known bad)** | 86.44% | -10.3 dB | -27.9 dB | **-31.7 dB** | -1.5 dB/oct |

### The control worked

805 Hz is the value `8b4a2556` removed. Its note records the result as 21 dB
short at 80-200 Hz against the real chip. Measured here, 805 Hz costs 27.1 dB in
that band relative to F1 against the shipping build. Same direction, same order,
so the measurement can see the regression it is supposed to guard against. A
sweep whose known-bad point looked fine would have proved nothing.

### Why the hypothesis fails

The exchange rate is bad everywhere on the curve, and it never improves:

| Step | F2 - F1 gained | 80-200 Hz - F1 lost | Cost per dB of F2 |
|---|---|---|---|
| 50 to 200 | 2.1 dB | 13.1 dB | 6.2 dB |
| 50 to 400 | 5.8 dB | 25.1 dB | 4.3 dB |
| 50 to 805 | 8.7 dB | 27.1 dB | 3.1 dB |

Even at 805 Hz, which is already past the point that shipped once and was
reverted, **F2 is still 10.3 dB under F1**. So the break frequency cannot
deliver an adequate F2 at any setting, never mind one that keeps the
fundamental. There is no value of this constant that is worth having.

An absolute-level check confirms the reading is not an artifact of the relative
measure: from 50 to 805 Hz the 80-200 Hz band moves by -2.7 dB in absolute
terms while the F1 peak rises 24.4 dB and F2 rises 33.1 dB. The band is not
collapsing on its own; everything above it is running away from it. Once the
output is renormalized to equal loudness, that is the same thing.

### What was confirmed, and what was not

- **"99.5% of energy below 800 Hz" is corroborated in substance.** A steady EH
  measures 98.38%. The original figure was quoted for a spoken line rather than
  one held vowel, so the two are not the same measurement, but the deficit is
  real and this large.
- **F2 and F3 are underexcited**, at -19.0 dB and -35.0 dB against F1 for a
  vowel whose formants are only an octave and a half apart.
- **The 11 dB deficit against the real chip is still unverified.** No reference
  recording exists anywhere on this machine. Without it, "closer to the chip"
  cannot be evaluated at all, only "different from where we are".
- **The filter register sweep was not tested.** Both probes write `SFILT` once
  and hold it; measuring that claim needs a probe that steps it.

## Where to look next

The deficit is real, so something else is producing it. In rough order of how
much each could plausibly be worth:

1. **The F1 resonator's bandwidth**, `s_kBandwidthHz[0]` = 60 Hz. A narrow F1
   both concentrates energy at F1 and steepens the skirt that F2 and F3 sit
   under. The measured rolloff above F1 is -37.8 dB/oct, far steeper than the
   -12 the source alone contributes.
2. **The output low-pass.** Two poles at `s_kfOutputLpCoef` = 0.32 break near
   2.9 kHz at 48 kHz, which lands on F3 for most phonemes.
3. **Cascade against parallel for the voiced path.** F2 and F3 currently sit
   behind F1's skirt. The fricative branch was moved to parallel for exactly
   this reason. A cascade is the conventional choice for voiced formants, so
   this is the largest change and the least certain.

Whichever is tried, the two guards this run established should come with it:
the 80-200 Hz band against F1 must not fall far from -4.6 dB, and 805 Hz
belongs in any sweep as the known-bad control.

## Reproducing this

The instruments are in the tree and the analysis needs numpy only. Capture with
`CASSO_AUDIO_DUMP` set, boot `Apple2/Demos/pulse-probe.dsk` on `Apple2e`, take
the 2.25-5.50 s window, and read the F1/F2/F3 peaks against the ROM formants for
the phoneme the probe holds. Derive the rate from the file's growth; do not
assume it.

After any sweep that edits `Ssi263.cpp`, **rebuild before trusting the next
capture**, and rebuild again after restoring the constant. A sweep leaves the
binary holding its last value while the source reads the original, which is a
capture that measures something no longer on disk.

## Provenance

| Claim | Source | Verified |
|---|---|---|
| 11 dB deficit above 800 Hz | earlier session, via handoff 028 | **NO -- no reference recording exists on this machine** |
| 99.5% of energy below 800 Hz | earlier session, via handoff 028 | corroborated 2026-09-05 at 98.38% on a steady vowel, which is a different measurement |
| Filter register sweep is barely audible | earlier session, via handoff 028 | NO, not tested; needs a probe that steps `SFILT` |
| Every figure in Results | measured 2026-09-05 | yes, method above |
| Formant ranges and the counts above 800 Hz | computed from `s_kPhonemes` | yes, 2026-09-05 |
| Chain slopes, constants, clamps | read from `Ssi263.cpp` | yes, 2026-09-05 |
| -8 dB/oct reference, 4.9 dB and 33 dB figures | comments in `Ssi263.cpp` | quoted, not re-measured |
| 805 Hz regression, 21 dB at 80-200 Hz | commit `8b4a2556` | reproduced 2026-09-05 as 27.1 dB |

**The missing reference recording is the one real blocker.** Everything above
measures this build against itself, which is enough to reject a change and not
enough to accept one: it can show a setting is worse, but "closer to a real
SSI-263" is unanswerable without the recording the original measurement used.
Acceptance criterion 1 cannot be evaluated until that file is found.
