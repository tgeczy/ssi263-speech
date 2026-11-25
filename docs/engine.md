# SSI-263 chip engine, v0.22

Paths on this page are relative to `src/`, except `tools/`, which sits at the repository root. Run the tools from the root. How the engine got here, and who found what, is in [history.md](history.md).

This is a sound-generating model of the SSI-263 chip, built from its documented architecture. **It is not a verified recreation.** v0 exists to put the whole pipeline in place: registers → controller → switched-capacitor vocal tract → output stage → WAV. That lets each part be measured against the Braille Lite recordings and replaced.

The chip knows phoneme codes and register values, never words. Front ends drive it through `ssi263/drivers.py` (the data book's register streams and the MASTER lines) and through the four firmware hosts in `hosts/` ([front-ends.md](front-ends.md)).

## Since v0.5 (details: `HOLDOUT.md` and [history.md](history.md))

- **v0.6:** per-path noise glides.
- **v0.7:** stop order. The tract closes, then the stop's own noise waits for the release.
- **v0.8:** only b01 = 1 stops release; the HVC and HFC holds stay shut.
- **v0.9:** amplitude glide ×6.
- **v0.10:**
  - release 1.25 frames before the end, then a 0.3-frame burst decaying to 0.3 of FA (`tools/burst_tail.py`);
  - closure timing scaled to the phoneme's length, so a 2-frame D′2 closes as it does on the unit (`tools/closure_check.py`);
  - the Speak-Out host steps at 0.5 ms (`tools/speakout_timing.py`);
  - tag and note corrections from Astra's Reply 27.
- **v0.11:** a b01 stop releases early only when the next phoneme is open (a vowel, R, L, W, M, N): `release_lookahead`, with the held stop's noise as its burst at the next load (`late_release_burst`). The extra soft G in "program" and the "d" before the J in "manager" went away. How the chip could know the next phoneme is not established; the model's device is an early A/R with the host's writes held to the stop's end (see Astra's Reply 28 for a load-triggered alternative).
- **v0.12:** a closure that loads onto silence closes at once (`closure_onto_silence`). The one-frame delay voiced B and D after every pause; on the unit that first frame is silent (`tools/stop_after_pause.py`).
- **v0.13:** F1 glides 2.25 times as fast; the voice source is 0.8 of the old impulse plus 0.2 of MAME's SC-01 stepped glottal wave; the Braille Lite add-on adds a gentle 5 kHz roll-off after the chip, matching the unit's line out (where that roll-off comes from isn't traced).
- **v0.14:** in Z, J, V and voiced TH the noise swells with each voice pulse.
- **v0.15 (0.7.6), the hard G (`hold_release`):** since 0.6.0 the K of the firmwares' G (K HVC on the Braille Lite, KV HVC on the Speak-Out) was held and HVC's gate stayed shut, so "guess" said "ess". Now an HVC after K or KV (the firmware's PA prime between them or not) asks early for the next phoneme through `release_lookahead`'s machinery, and opens its gate `hold_release_frames` (0.5) before its end with its own `hold_release_ramp_ms` (2 ms) ramp, only when the held writes load an open phoneme. Before D, PA, a fricative or a pause, or with no answer in time, it stays shut as before. Only holds of at least `hold_release_min_frames` (2) frames qualify -- 4 - DR, or 1 in mode 1 -- so the Accents' one-frame KV KV HVC′3 HF keeps 0.7.5's output exactly: the bounded scope of this modelled fix, not a duration rule found in the silicon (Astra, Reply 151). A power-down ends the K/KV context and any hold state, after the held writes land. HVC's body, noise and timing are unchanged; `releases` (the b01 stops' delay, noise and burst) is untouched. Chosen by ear (Tomi's "C_050_sharp", Astra's Replies 146-150), not traced: a modelled rule. The firmware's write values are unchanged; with the early A/R, the writes it held land together at HVC's end, so the PA prime's ~0.66 ms is gone and what follows comes that much earlier, as after every b01 stop. `tools/check_hard_g.py` watches the gate in the chip's state; `hold_release` False is the 0.7.5 engine, bit for bit.
- **0.8, the BASIC tones (`mode1_rate15_formants`):** the Braille Lite's BASIC Sound command plays each note as M in mode 1 at speech rate 15 (articulation 0), with PA between notes. Through 0.7 the formants glided into M at every start, a bright burst the real chip does not make (Jayson heard it as a crackle in chime.bas; on Tomi's unit the starts carry +0.2 to +0.5 dB of 3-10 kHz over the steady note, ours +7 to +8). Now, in mode 1 at rate 15 only, a sounding phoneme's formants (F1 F2 F3 NAS) land at once, and PA holds the tract until the VA and FA latches are both zero, then retunes (`pa_wait`). Retuning PA at once (`immediate`, Astra's preview C) cleaned the starts but put a click at every note's end while M was still fading (Tomi's ear; +5 to +12 dB). Tomi on CHIME and Pinball with `pa_wait`: "sounds perfect" (2026-10-11; Astra, Replies 177-179). A bounded behavioural rule, not a traced gate: the real chip's rate-14 and slower transitions stay bright (50-190 ms), so nothing else changes, and the firmwares' speech runs in mode 3. `slew` is the 0.7 engine, bit for bit. `tools/check_tone_tract.py` watches the tract in the chip's state and the sound.
- **v0.16, F1 from the real chip:** Tomi played register programs on his own SSI-263: ROM vowel pairs that differ only in the F1 code. F1's rule (`f1_K`, `f1_C0`) and its bandwidth (`bw_ratio[0]`) were fitted to one pair, E/AY, and then predicted the second pair, AI/:A, and the older session's vowels below 1 kHz. Exactly those three values changed, tagged REG. It is acoustic evidence for the F1 rule, not a die trace (what PAR drives is still open). Still off: M, N and NG fit in shape but sit 3-6 dB low below 1 kHz.
- **v0.17, the noise path (`noise_path` 'own'):** the noise runs through F2..F5 sections of its own, beside the voice's cascade, which takes no noise; the two are summed before the output high-pass. The voice is v0.16's, sample for sample, and `shared` is v0.16 unchanged. Tomi heard fricatives come closer ("judges" regains its grit on the Speak-Out; the Accent unchanged). The measured noise shapes improved; some levels are still too quiet (F, TH, Z). A model construction, not an identified circuit.
- **v0.18, the BASIC tones (`mode1_rate15_formants` 'pa_slew'):** as `pa_wait`, but once the VA and FA latches reach zero, the formants slew to PA's instead of retuning at once, which removed a faint click at the ends of the Pinball lesson's notes (Tomi's ear). Mode 1 at rate 15 only; `pa_wait` is v0.17. Not a traced circuit.
- **v0.19, the volume moves smoothly (`amp_steps` 'smooth'):** the R3 amplitude reaches the output volume as its slew, not as the slew's nearest whole code. The Accent writes a new amplitude with nearly every phoneme load, and each whole-code step was a small gain step in the running waveform: Tomi heard a grainy ticking, different on every vowel, on the Accent voices only; his own chip, speaking the Accent's writes, has none. On that comparison he also heard "no volume wobble either"; whether this answers the older Accent volume wobble in general is not established. A voice that holds one amplitude (the Speak-Out, the Braille Lite) is unchanged once it has settled; this was not shown for start-up or later amplitude changes. It does not measure a continuous volume DAC, and the slew speed is still a guess. The volume stays after the tract, on voice and noise alike: moving it before the tract brought the P and the Z out too strongly. `code` is v0.18.
- **v0.20, the glide law (`glide_field_mult` = 8 / (8 − G)):** in mode 3 the pitch moves one count per 16 (8 − G)(16 − R) XCK cycles, measured on Tomi's chip in a 426-trial register-program session (every glide field, up and down, and several rates; the fastest fields within the measurement limits Astra stated in Reply 208), and confirmed independently by Astra. Fields 0, 2, 4 and 5 (the Speak-Out's and the Blazie's) change by under 10%; field 7, which carries two thirds of the Accent's pitch writes, glides about 3.25 times faster than v0.19's value, fitted to the real Accent card's demo. Tomi heard the law on his chip's own takes of the Accent's sentences: "no wobbling returns or regression". Whether the Accent card's chip, as the card drives it, glides as his does is open; a different clock alone would not explain a difference, since XCK cancels from the period's glide slope (Astra, Reply 247).
- **v0.21, F4 on its own capacitor bank (`f4_tuning` 'code'):** through v0.20 the fourth formant was a fixed placeholder, because no ROM field supplies it. In the SC-02 prototype's schematic, drawn by Kevin Horton ([PDF](http://blog.kevtris.org/blogfiles/sc-02_Final_Schematic_V1.00.pdf)) and read into a net list by Henri Asseily ([issue 31](https://github.com/tgeczy/ssi263-speech/issues/31)), two latches take the same 4-bit code in the same scan slot: one drives F3's capacitor bank and the other F4's, which has about twice the fixed capacitance, so the same code moves F4 over a smaller range. The same reading of his F1, F2 and F3 banks gives the fixed capacitance we had fitted from recordings months before (1.56, 1.79 and 3.90 units against our 1.55, 1.75 and 4.07), so the production chip seems to have kept the prototype's ratios. Now the voice path's F4 is tuned by the F3 code on that bank (`f4_C0`, `f4_w`) and retuned wherever F3 is. Its scale and width (`f4_K`, `f4_bw`) come from one pair of ROM vowels on Tomi's chip that differ in that code alone (LF and OU); the width is the least founded number. Before any score was looked at, the law was used to predict 24 held phonemes of the real chip against AH: 23 of 25 comparisons came out better than the fixed F4 and one worse (:UH), the misfit fell from 4.5 to 3.2 dB and from 0.44 to 0.19 rad in phase, and three deliberately wrong code maps all scored worse than the fixed F4. Tomi heard it against v0.20 and the real unit on development words and sentences: no colouring; to his ear the voiced TH of "this" is nearer the unit, which no measurement of ours shows yet. Not known: the section's true width and gain, whether the noise path's F4 moves too (it is left fixed), and why :UH got worse. `f4_tuning` 'fixed' is v0.20 (`tools/check_f4_law.py`).
- **v0.22 (0.9.6), a phoneme loaded at amplitude zero is silent (`amp_zero_sources`):** firmwares write a voiced phoneme at R3 amplitude 0 before a sound -- the SynPhonix's VEST before a bare E (66 ms), the Hungarian and nine other Braille Lite languages before a word (OU, 82 ms), the English unit before some E's. Through v0.21 that phoneme was heard, fading from the previous level (a "duck" or a "spit" before the sound; listeners heard it, Timothy among them, and Tomi). Tomi played VEST's exact writes on his own chip (`REG_vest_e`, `REG_vest_e2`, Claude's letters 280-283): the real chip is silent there, and its E starts 60 ms after the A's from the same point where ours started at once. Now a phoneme loaded while R3's amplitude is zero has no voice or noise excitation (the filters keep their tails); a later positive R3 restores what that load suppressed. Writes to a phoneme already sounding keep v0.21's interpolation, including a zero and a rise without a load: the follow-up sessions (`REG_astra265_volume`, its normal-speed repeat) show R3 alone can silence and restore sound, but not how fast, and a slower ramp tried on the Accent's sentences added waver and ducks to Tomi's ear (letter 285). Tomi approved it by ear on every changed voice. Not known: the amplitude scanner's real timing, and the recovery's. `amp_zero_sources` False is v0.21 (`tools/check_amp_zero.py`; the page: [amplitude-zero-candidate.md](amplitude-zero-candidate.md)).

## Layout

| Path | What it is |
|---|---|
| `ssi263/chip.py` | The chip: `write(addr, value)`, `request` (A/R), `run(s)`, `run_until_request()` |
| `ssi263/params.py` | Every uncertain constant, tagged A / ISSCC / P / BL / SC01 / GUESS |
| `ssi263/rom.py` | Raw ROM bits, plus a swappable reading of them (the candidate decode) |
| `ssi263/drivers.py` | Hosts: data-book Hello rows, and MASTER lines from the emulated Blazie stream |
| `data/rom_bits.csv` | Built by `tools/make_rom_bits.py` from Astra's reconciled reading of the P die. It matches Casso's table in all 1856 cells |
| `tools/render.py` | Renders the milestone WAVs, each with a JSON sidecar, into `investigation/out/` |
| `tools/compare_dev.py` | Engine against the real unit, on development lines only |
| `HOLDOUT.md`, `holdout_lines.txt` | The frozen hold-out set, and the rules for it |
| `ssi263/dsp.py` | The host-rate stage (4x FIR, decimation, PCM): numpy reference, or the C one |
| `csrc/ssi263.c`, `.h` | The chip in portable C99, a line-by-line port of `chip.py` with every parameter switch |
| `ssi263/native.py` | `SSI263C`: the C chip behind `chip.py`'s interface (ctypes; Python 3.7-3.13, 32/64-bit) |
| `tools/check_native_core.py` | Holds the C chip to `chip.py`; the add-on builds run it on both architectures |
| `tools/check_hard_g.py` | The hard G (`hold_release`), watched in the chip's state: Python, C and the live Braille Lite |
| `tools/check_tone_tract.py` | The BASIC tones (`mode1_rate15_formants`): note starts and ends in mode 1 at rate 15, in the chip's state and its sound |

## The C core

`chip.py` stays the reference: research, tuning and review happen there.
`csrc/ssi263.c` runs the same model in C99, with parameters and the ROM reading passed in
from `params.py` and `rom.py`. The NVDA add-ons run it, and it is the layer a SAPI or Linux
front end would sit on, as with the pcf8200 library.

`tools/check_native_core.py` holds the two together. It runs seeded random host sessions
(every register, power and mode changes, run, run_until_request, skip, snap_pitch) under the
defaults and under each parameter switch flipped. After every operation:
- A/R, chip time and every counter must be equal, exactly;
- audio must agree within 1e-9 (measured: 4e-15);
- the PCM must be identical.

Speed on the old NVDA's Python (3.7, 32-bit) is 15x realtime for the Speak-Out session and
12x for the Braille Lite one, against 2.7x and 2.2x with the Python chip. Build:
`python csrc/build_native.py`, which uses w64devkit, statically: the DLLs import only
KERNEL32 and msvcrt.

Run the tools with 64-bit Python, numpy, scipy and soundfile:

```
python tools/make_rom_bits.py
python tools/render.py
python tools/compare_dev.py --lines 60 [--set name=value ...]
```

## What the model does

- **Filter clock.** The vocal tract runs as a discrete-time system at fc = XCK / (2 (256 − FF)). The coefficients depend only on capacitor ratios, so FF changes the tract's sample rate and nothing else. Every resonance therefore scales exactly with the tone setting, as measured on the unit.
- **Formant sections.** F1, F2 and F3 follow one shared rule, with binary-weighted capacitor arrays: (2 sin(π f / fc))² = K (C0 + Σ wᵢbᵢ). That is two numbers per filter, not a table per phoneme.
  - The rule was fitted on development repetitions at tone 13. It predicts the same-condition held-out repetitions (H2) to 21 / 52 / 65 cents RMS (F3 / F2 / F1).
  - Those are the same words at the same settings, so this tests measurement stability, not generalisation. f ∝ fc is built into the structure; the real generalisation test is H3 (tone 19), not yet run.
  - With the bit weights left free, F2 and F3 come out close to 1, 2, 4, 8, which is independent support for the candidate bit order.
  - F1's lowest bit, the PAR row, comes out near 0. That question is open (message §26.3).
  - Since v0.16, F1's K, C0 and bandwidth come from controlled F1-only vowel pairs on the real chip (see above). That fits the rule acoustically; it doesn't trace PAR on the die.
- **Analog latches.** The sections see 4-bit latched codes, while the internal transition counters move linearly (fully internal linear transitioning, per the A data sheet).
- **Transitions run on the frame clock (v0.2), 4.0 codes per frame at articulation 5 (provisional).** `tools/transition_speed.py` v2 (ramp fit, no cap, synthetic self-test within 8 %) gives these R/ER → E1 10–90 % times, unit against engine:

  | Word | Rate 2 | Rate 10 | Rate 14 |
  |---|---|---|---|
  | berry | 115 / 141 ms | 38 / 42 ms | 13 / 10 ms |
  | very | 118 / 109 ms | 48 / 35 ms | 18 / 10 ms |

  - The unit's times scale roughly with (16 − R).
  - Other articulation settings are guesses.
  - v0.1 used a fixed 20 ms and stepped audibly; Tomi heard a wobble.
- **Amplitude glides (v0.5, v0.9).** VA and FA glide 6× faster than formants. This is a fitted global slew, not an identified 6:1 clock.
  - v0.5 set 3×, the SC-01's tick ratio. At 1× the S noise ran 116 ms into the next vowel (`tools/noise_decay.py`); Tomi heard it as "extra breath underneath" the T. At 3× the tail is 16 ms.
  - v0.9 raised it to 6×, from the T and P burst width (`tools/stop_profile.py`): the unit's burst lasts about one frame, and 3× left about two, Tomi's "thick" T.
- **Host interface (v0.4).**
  - Output is block-size invariant: a pending held sample carries over between calls.
  - `request` means A/R is asserted (the pin is active-low). It is gated by power and mode; mode 0 never requests.
  - `write()` takes RS2..RS0, and addresses 4–7 select R4.
- **Timing.** Frame = 4096 (16 − R) / XCK, and phoneme = frame × (4 − DR), per the A data sheet. A/R rises at the end of each phoneme.
- **Inflection.** F0 = XCK / (8 (4096 − I)). The glide is linear in period at the measured Braille Lite rates, which scale with 1/(16 − R).
- **Noise path (v0.3).** Noise goes through a band-pass shaper (0.28 × fc), then into F2 and/or F5 (ISSCC, Fig. 1). ROM bit b02 chooses the injection point, a hypothesis supported by the audio:
  - b02 = 0 (exactly S, Z, TH, THV and T) goes to F5 only;
  - every other noise phoneme goes to F2 plus 0.25 of F5.
  
  On dev lines, S's spectral centroid is now 5.91 kHz, against 5.95 kHz on the unit. v0.2 put S near 2.6 kHz, and Tomi heard "shix" for "six".

  Since v0.17 the noise has F2..F5 sections of its own rather than entering the voice's, with its own F2/F5 weights per noise class and its own levels (see above).
- **Output stage.** High-pass with volume, then a closure ramp, then S/H at fc, then area sampling and an anti-alias FIR to the host rate.
- **The carrier.** An fc/8 line (plus fc/4) is present whenever the chip is powered, and does not follow volume, as measured.

## Hosts: firmware that drives the chip (`hosts/`)

Firmware is never stored here; each host reads the user's own file.

**`hosts/speakout.py`** runs the Speak-Out's 1995 NEC V40 firmware under Unicorn. You pass the path to your own `SPEAKOUT.HEX`.
- The host provides the V40's interrupt controller (ports 8–9) and serial unit (ports 0–3).
- The SSI-263 sits at F000:FE00–FE04, and the chip's A/R feeds the firmware's INT 0Ch.
- Text goes in with `say()`; the audio comes from this engine.
- The firmware boots, resets the chip, speaks its own greeting, then speaks any text it is given.
- XCK is 1 MHz, so its default tone register E4 gives fc = 17.9 kHz.
- **Checked against a real Speak-Out recording** (H4, `HOLDOUT.md`). The words are identical, F0 is 88.72 against 88.78 Hz (confirming 1 MHz to 0.07 %), and Tomi hears "the exact tone of the speakout".

## Development comparison (60 lines; `investigation/out/compare_dev_v0.3.txt`)

**Vowels** sit within 0–3 dB of the real unit, relative to each line's loudest phoneme. **So do most consonants** since v0.2 and v0.3: SCH, HF, Z, J, F, D′2, M and N are within about 4 dB.

Still off, engine minus real in dB:

| Phoneme | Difference |
|---|---|
| S | +7 (1 token) |
| B | +5.5 |
| K | −6.5 |
| T | −7.5 |
| P | −11 |
| THV′1 | −10 |
| V′1 | −6.5 |
| HVC | −21 |

Most of these involve the unfinished closure.

- **Closure (v0.4).** The delay is 2 frames, and consecutive closure phonemes stay shut, so the tract does not reopen.
  - Stops after non-stops sit within about 4 dB of the unit.
  - K → HVC, the hard g, is silent on the unit, as here.
  - D′2 → D′2 is −16 dB on the unit; here it is silent.
  - K → T and P → T ("act", "kept") are untested on the unit.
- **Spectrum.** In the speech bands the engine is 6 dB short at 100–300 Hz and 5 dB over at 3.5–4.5 kHz.
  - The overall spectrum above about 5 kHz is dominated by vowels, and there it sits near the line-in floor.
  - Fricative segments on their own do carry real energy at 6–9 kHz, and bursts up to 14 kHz. See `investigation/out/fricative_spectra_v0.3.txt`.
- **The capture path is not modelled yet.** The real side includes the Braille Lite's output path, which must go in a separate stage, not into the chip. The tone ladder (tones 1, 7, 13, 25) is what separates the two: chip features move with fc, the output path's do not.

## Not modelled yet

- F4's width and gain (its centre follows the F3 code since v0.21; the width is one vowel pair's), whether the noise path's F4 moves, and the meaning of NAS and F2Q (both are placeholders);
- the flag bits b01..b04;
- the glottal pulse shape;
- the noise clock, and the shaper's coefficients;
- the S/H aperture, and clock feedthrough beyond the carrier line;
- power-down and reset details. The A-sheet PD/RST semantics are not assumed for the P die.
