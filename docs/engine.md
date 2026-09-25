# SSI-263 chip engine, v0.12

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
- **Analog latches.** The sections see 4-bit latched codes, while the internal transition counters move linearly (fully internal linear transitioning, per the A data sheet).
- **Transitions run on the frame clock (v0.2), 4.0 codes per frame at articulation 5 (provisional).** `tools/transition_speed.py` v2 (ramp fit, no cap, synthetic self-test within 8 %) gives these R/ER → E1 10–90 % times, unit against engine:

  | Word | Rate 2 | Rate 10 | Rate 14 |
  |---|---|---|---|
  | berry | 115 / 141 ms | 38 / 42 ms | 13 / 10 ms |
  | very | 118 / 109 ms | 48 / 35 ms | 18 / 10 ms |

  - The unit's times scale roughly with (16 − R).
  - Other articulation settings are guesses.
  - v0.1 used a fixed 20 ms and stepped audibly; Tomi heard a wobble.
- **Amplitude glides (v0.5).** VA and FA glide 3× faster than formants, as in the SC-01's tick structure.
  - At 1× the S noise ran 116 ms into the next vowel (`tools/noise_decay.py`); Tomi heard it as "extra breath underneath" the T.
  - At 3× the tail is 16 ms.
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

- the source of F4, and the meaning of NAS and F2Q (both are placeholders);
- the flag bits b01..b04;
- the glottal pulse shape;
- the noise clock, and the shaper's coefficients;
- the S/H aperture, and clock feedthrough beyond the carrier line;
- power-down and reset details. The A-sheet PD/RST semantics are not assumed for the P die.
