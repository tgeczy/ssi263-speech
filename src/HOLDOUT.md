# Hold-out set for the SSI-263 engine — frozen 2026-09-24

These MASTER recordings are never used to set, fit or choose any engine parameter. They are only for checking the engine after a change has been made on other grounds.

`holdout_lines.txt` lists the MASTER script lines, 342 of 2243:
- **H1: section J**, script lines 6259–6461 (66 lines). These are sentences with punctuation, letter names and digits, at Tomi's settings (tone 13, pitch 32, volume 6, rate 10), and the sentences again at rate 2.
- **H2: section B, trial inv-r2, reps 2–3** (242 lines). This is the same split `phoneme_tracks.py` already uses.
- **H3: every tone-19 line outside J** (34 lines). A whole filter-clock setting stays unseen.

**Development material** is every other MASTER line, plus the data book's Hello register streams, which are not recordings. `tools/compare_dev.py` draws only from section B, inv-r2, reps 0–1.

## Fingerprints

| File | sha256 |
|---|---|
| `holdout_lines.txt` | d991a03f0c18e085310769de268cffb62e1aafa3a4adc206e6542581d196ee4b |
| `blite_sweep/scripts/MASTER_capture.txt` | 407f93437090b5fa3ff5651cb86b78e3fb6bdb45c028ac4f07f2a234bff1dbd7 |
| `blite_sweep/scripts/MASTER_predictions.jsonl` | f4014d47790d779ace200744e2efe2adfb3d464483328848bd4ae06420ca5ba6 |
| `blite_sweep/sessions/MASTER/utterances.jsonl` | 9c24b3defbffc3f3e6591b9070bab6616fe518b8a59f4eec77678eb4b927a8cd |
| `blite_sweep/sessions/MASTER/master.wav` | e7cc0c4d7bbaa3e6452ad4408e268275bc3988e6747022841c3bc9e3bb3250e5 |

## Rules

1. **No parameter may be set from a hold-out comparison.** A change made after seeing one must be justified by development data or a document, and the log below must say so.
2. **Renders of hold-out lines are evaluations.** Each one gets a sidecar marked `"holdout": true`.
3. **Adding lines is allowed; removing them is not.** Growing the set needs a new fingerprint and an entry below.

## Log

**2026-09-24, Claude: one look before the freeze.** At the very first v0 defaults, I compared the long-term spectrum of line 6306 (H1), engine against real. The engine was 15–25 dB too bright in 1.5–4.5 kHz and short of energy below 700 Hz.

Two defaults changed soon after, before this file existed, and that look informed them:
- the sections gained a (1 + z⁻¹)² numerator, a true low-pass;
- the F4/F5 placeholder bandwidths widened from 0.008/0.012 to 0.03/0.06 × fc.

Every later change used development lines only:
- the output anti-alias stage;
- the noise gain, 0.08 → 0.006;
- the closure ramp.

**2026-09-24, Claude: one evaluation of H2.** The F1–F3 capacitor rule was fitted on development repetitions only (`phoneme_targets.csv`, tone-13 dev medians). It was then scored once against the H2 repetition medians from the same file: 21 / 52 / 65 cents RMS for F3 / F2 / F1. Nothing was refitted afterwards.

Line 6306 should count as **seen once** when the v0 results are judged. H2 has been evaluated once, as above. No other hold-out line has been rendered or compared.

**2026-09-24, Claude: v0.2, prompted by Tomi.** Tomi listened to the v0.1 render of line 6306 and reported "a weird wobble" at "three". Two suspects were measured on development material only:
- **Glottal-pulse placement:** a timbre ripple of 0.17 dB on a synthetic sustained E. Ruled out.
- **Formant transition speed,** on the R/ER → E1 moves in "berry" and "very" (dev lines 462, 2513, 3414, 578, 2501, 3411; `tools/transition_speed.py`). The real glides take about 3 frames and scale with (16 − R), where v0.1 took a fixed 20 ms.

The transition clock moved to frames: 4.0 codes per frame at articulation 5. Line 6306 was then re-rendered for evaluation only; the v0.1 renders are kept as `*-v0.1.wav`.

**2026-09-24, Claude: v0.3, prompted by Tomi.** Tomi's second listen to line 6306 (the v0.2 render): the S sounds are lispy, "shix" for "six". The fix was measured on development fricatives only (`tools/fricative_spectra.py`, section B inv-r2 reps 0–1):
- The unit's S has its energy at 6–9 kHz (centroid 5.95 kHz); SCH, J and HF sit at 2.5–4 kHz.
- ROM b02 is 0 exactly for S, Z, TH, THV and T, so it now selects the noise injection: F5 for b02 = 0; F2 plus 0.25 of F5 for b02 = 1.
- The noise shaper became a band-pass at 0.28 × fc (B 0.15 × fc), and the noise gain 0.012 (set on dev levels).

Line 6306 was re-rendered for evaluation only; the v0.2 renders are kept as `*-v0.2.wav`.

**2026-09-24, Claude: reclassifications after Astra's Reply 26.**
- **Line 6306** has informed choices and repeated listening. It is now a **disclosed development and regression example**, not hold-out. It stays in `holdout_lines.txt`, so `compare_dev.py` still skips it, but judgements should not treat it as unseen.
- **H3 (tone 19)** is reserved from this fit. Some tone-19 observations existed earlier in the investigation (§16–18), so it is not historically unseen.
- **H2** tests same-condition repeatability, not generalisation.

**2026-09-24, Claude: v0.4.** The closure defaults were set on dev lines (`tools/compare_dev.py`, 120 lines, stops split by the preceding phoneme):
- **Delay: 2 frames.** Every stop after a non-stop now sits within about 4 dB of the unit.
- **Reopen: off.** K → HVC is silent on the unit (37 tokens). The review patch's reopen stays available as `closure_reopen`.

**Controller fixes from Reply 26 and the cloud review:** a pending held sample (block-size invariant to 1e-16), request status gated by power and mode, several pulses per tick, and addresses 4–7 mapped to R4. `transition_speed.py` v2 removes the 120 ms cap and passes a synthetic self-test.

**2026-09-24, Claude: v0.5, prompted by Tomi.** On v0.3 and v0.4, Tomi heard "extra breath underneath" the T. The cause, measured on dev S → vowel boundaries (`tools/noise_decay.py`): the noise amplitude glided at the slow formant rate. It ran 116 ms into the vowel, where the unit's has gone by the boundary.

The fix is `art_amp_mult` = 3, following the SC-01's amplitude/formant tick ratio: the tail is now 16 ms. Dev levels stayed within the v0.4 figures, and S improved from −5.4 to −1.6 dB.

**2026-09-24, Claude: H4, an external recording.** Tomi's direct recording of a real Speak-Out is `Y:\content from streamers\DecTalk archive\Synthesizers\speakout.wav` (2004; 11,025 Hz; 2.05 s; "speak out ready" … "not charging"). It is reserved as test material, and nothing will be tuned on it.

**First evaluation** (`hosts/speakout.py` at v0.5 defaults, boot greeting):
- **Words:** identical; the firmware's own "not charging" status.
- **F0:** 88.72 Hz real, 88.78 Hz engine. XCK = 1 MHz is confirmed to 0.07 %.
- **Timing:** the engine's first phrase is about 10 % shorter, and its pause is 0.25 s against the real 0.16 s.
- **Spectrum:** the engine is short of bass by about 5 dB and bright at 1–3 kHz by 3–6 dB, the same deviation as against the Braille Lite. Two units and two capture chains agreeing points at the chip model, most likely the glottal pulse shape.

**2026-09-24, Claude: H5–H7, more external recordings** (Tomi's archive, same folder). All are reserved test material; nothing will be tuned on them.
- **H5** `bs2english.wav`: a Braille 'n Speak 2000 saying "braille n speak two thousand ready, help nine pages".
- **H6** `TNS.wav`: a Type 'n Speak.
- **H7** `bs2spanish.wav`: a Spanish Braille 'n Speak 2000.

**Pitch at the Blazie reset default.** H5 is flat at 81.45 Hz and H6 at 81.43 Hz. Blazie's r1 45h predicts 81.38 Hz at 1 MHz, so both units ran the same clock and default pitch as Tomi's Braille Lite after its warm reset.

**H5 against the engine,** driven by the emulated 2003 Braille Lite firmware's stream for the same words, at the reset defaults (tone 7, pitch 45h, volume 6, rate 11):
- **F0:** 81.40 Hz.
- **Spectral peaks:** 441 / 2164 / 2562 Hz against the real 452 / 2164 / 2487.
- **Bands (300 Hz–3.5 kHz):** within 1–3 dB, with the bass (80–300 Hz) 2.7 dB short.
- **Durations:** not comparable. The recording is cut at both ends, and the boot greeting's own phoneme string need not match the speech-box rules.

**Correction to the H4 note above.** The 1–3 kHz brightness appears against the Speak-Out clip only. What recurs across three units is the bass shortfall: 2.7 dB (H5), about 5 dB (H4) and 5–6 dB (Braille Lite dev). That points at the glottal source or the high-pass, not a recording chain.

**2026-09-24, Claude: v0.6, prompted by listeners.** A listener on Mastodon heard "a weird chih thing" in a published engine clip, and Tomi heard T as "ch". The fix was measured on dev only:
- **The v0.5 T** had a 1–10 kHz centroid of 3.3 kHz, against the unit's 6.9 (`fricative_spectra.py`).
- **Its release** over-filled 2.5–4 kHz by 3.3 dB (`release_spectra.py`). The b02 route switched to the vowel's F2 path at the load while T's noise was still fading.
- **The fix:** `noise_route_mode` = `per_path`. Each injection point has its own level, gliding at the amplitude rate. T's centroid is now 6.1 kHz, and dev levels are unchanged.

**2026-09-24, Claude: v0.7, prompted by Tomi.** On v0.6 Tomi heard "Hello STomi", "stwo" and "rashte", a hiss before every T. `tools/stop_profile.py` shows why (dev T/P → vowel, unit against engine):
- **On the unit,** the vowel fades, the stop is silent mid-segment, and its 5–9 kHz burst peaks at 0.75–0.88 of the segment.
- **In the engine,** the order was reversed: noise first, then silence.

**The fix:**
- the closure now reopens 1 frame before the stop's end (`closure_release_frames`);
- the stop's own noise waits for that release (`closure_noise_at_release`);
- the closure delay is 1 frame.

**Open conflict:** mid-segment dev levels for T, P and K are now 14–35 dB under the unit's median. Only 4 T and 2 P dev tokens reach a vowel, so the profile rests on few tokens. The Speak-Out self-test (H8, external, `so-selftest.wav`) is reserved like H4.

**2026-09-24, Claude: v0.8, prompted by Tomi.** On v0.7 Tomi heard a "d" before every P. The Speak-Out writes P as HFC + P, and v0.7 released HFC's own burst before P's. Fix, from ROM flags and dev data:
- only closures with b01 = 1 (B D P T K) release before their end;
- the b01 = 0 holds (HVC, HFC) stay shut and close at once.

Dev: K → HVC is now silent, as on the unit (37 tokens).

**2026-09-24, Claude: v0.9, prompted by Tomi and a listener.** On v0.7 and v0.8 the T sounded "thickened, mouthful", strongest at slow rates; a listener said "it's trying so hard". `tools/stop_profile.py` on dev: the unit's T burst lasts about 1 frame and is down 17 dB into the vowel, where the engine's lasted about 2 frames.

**Fix:** `art_amp_mult` 3 → 6, giving a 1-frame burst that is 13 dB down into the vowel.

**Rejected:** joining the F5 noise after F5 (`noise_f5_point` 'output'). It made S and Z 11–12 dB too bright at 9–14 kHz.

**2026-09-24, Claude: v0.10, after Astra's Reply 27,** on dev lines only.
- **Burst and tail** are measured separately (`burst_tail.py`), with the burst located in each signal's own audio. The unit's T burst peaks 1.15 frames before the boundary; v0.9 put it at the boundary, and its noise ran into the vowel (the "thick" T).
- **Global fit:** release 1.25 frames before the end, a 0.3-frame burst, then 0.3 of FA as aspiration. Bursts now land at −0.9 / −1.0 / −1.15 frames (T / P / K) against the unit's −1.15 / −0.68 / −0.99.
- **Closure timing** is scaled to the phoneme's length (`closure_check.py`). The unit's 2-frame D′2 closes to −31 dB, which the frame-based rule could not.
- **Rejected:** a −26 dB closure floor. It matched the depth of B and D but let HVC leak after K.

**2026-09-24, Claude: packaging only (the NVDA add-ons' 0.2), no change to the sound and nothing tuned.** Tomi asked for Windows 7 / NVDA 2021.1 support with no VC runtime.
- The output stage (4x FIR + decimation, PCM) moves to `ssi263/dsp.py`. numpy stays the reference; its output is bit-identical to v0.10's (max diff 0 on a 9.4 s Speak-Out session).
- A C path (`csrc/ssi263dsp.c`, `_bin/{x64,x86}/ssi263dsp.dll`) serves the add-ons. It is within 8.9e-16 of numpy, with byte-identical PCM, on Python 3.13 x64 and 3.7 x86.
- `rom.py` reads its CSV by hand, because NVDA 2021–2023 ship no `csv`.
- `hosts/ucmini.py` plus a static Unicorn build replace the unicorn package. The chip writes and audio are identical.

**2026-09-24, Claude: Blazie host only, no change to the chip.** `hosts/blazie.py` takes `menu=`: letters typed in the unit's 345-chord speech menu before speech-box mode. The NVDA add-on uses punctuation none, because the unit spoke the symbols NVDA passes through ("dash dash", "left paren left paren"), and it toggles the numbers mode. The warm snapshot reads every number digit by digit. In full-numbers mode the firmware is right up to 999,999,999,999 and has no "trillion": 10^12 comes out as "one billion" (phoneme dump). The research boots are unchanged (menu=()).

**2026-09-24 (Thursday night), Claude: the chip in C, performance and portability only, no change to the sound.** Tomi: "eventually we'll want the entire thing in portable-C". `csrc/ssi263.c` is a line-by-line C99 port of `chip.py` v0.10, with every parameter switch; `ssi263/native.py` (`SSI263C`) runs it behind the same interface. `chip.py` stays the reference. `tools/check_native_core.py` checks it: 126 random sessions, each switch flipped, 391 s of audio. The internal state is equal after every operation, audio within 4.4e-15, PCM identical, on 3.13 x64, 3.13 x86 and 3.7 x86. The firmware sessions give identical chip writes (Speak-Out, 2026 writes) and identical PCM, and so does the add-on driver audio against 0.2.1. Two things are easy to get wrong in a port. Python's round() is half-to-even, so the C code uses rint(). Bandpass's gain uses CPython's own complex division.

**2026-09-25, Claude: the Accent front end, H9, and glide fields 3, 6 and 7.** `hosts/accent.py` runs Aicom's SPKEMS.DVC (Accent-mini, V4.5) under Unicorn. Tomi's archive has two real Accent recordings:
- `accent-demo.wav` (105 s demo dialogue). Its opening, "Hi, I am Accent. Have you ever heard a computer talking?", is **DEV** material for the Accent front end, at Tomi's suggestion.
- `accent.wav` (0.88 s, "Accent ready", the same greeting SPKEMS speaks at boot) is **H9, hold-out**.

Tomi heard "wobbly vowels" not in the originals. Pitch jitter and level wobble matched the real opening (0.25 dB), but F0 travel inside voiced stretches did not: median 0.78 / 90th 6.05 / max 8.90 semitones against the real 0.17 / 1.10 / 2.82.

The Accent puts glide field 7 on 2/3 of its pitch writes, and fields 3 and 6 on a few. No other front end uses those fields (Speak-Out: 0 and 4; Blazie: 0, 2 and 5), and their multipliers were GUESS extrapolations (1.66, 2.85, 3.25). A scan on DEV (`tools/accent_glide_variants.py`) found only 2.462 (field 5's measured value) matching: 0.17 / 1.42 / 2.64. Values of 1.66, 1.0, 0.6 and 0.35 all fit worse. Slowing every field instead would break the measured BL glides.

**Set:** fields 3, 6 and 7 = 2.462. Speak-Out and Blazie sessions are bit-identical before and after, and `check_native_core` passes.

**H9, checked once after the change** (`tools/accent_holdout.py`), for the real card, the old fields and the new ones:
- F0: 108 / 108 / 108 Hz.
- Largest travel inside a voiced stretch: 0.17 / 3.71 / 0.17 st.
- Greeting length: 0.88 / 0.88 / 0.88 s.

**2026-09-25, Claude: Accent host and add-on, no change to the chip.**
- **The host fix.** Tomi's first install went silent after he changed the variant. Under ESC =K (the power-up default), a speech option command blocks inside INT 17h until the speech buffer drains. `_far_call` gave up after 50M instructions and left the driver's reentrancy byte (6Bh) set, so every later INT 17h looked nested and never started speech. `_far_call` now runs 1M instructions, then keeps the chip and IRQ2 running while the driver waits. The demo render is bit-identical before and after.
- **The add-on.** The Accent add-on reads numbers as words, behind a checkbox. The Accent reads 3-digit and 5+-digit numbers digit by digit, and only comma-grouped numbers get hundreds (manual 4.1.4). Dollar amounts are left to the Accent, with thousands commas added.
- **The loudness dip, investigated on DEV only (the demo opening); nothing changed.** Tomi heard a dip after the L in "colon". The driver writes an amplitude per phoneme: 12 on stressed vowels, 5–6 on L and weak vowels, 4 on N. The phoneme-scale level flutter is 0.97 / 3.23 / 6.13 dB (median / 90th / 99th) on the real card and 1.44 / 4.28 / 8.51 here (`tools/accent_level_flutter.py`, `tools/accent_amp_law.py`).
  - A global `amp_slew_per_phoneme` of 0.25 reaches 1.20, but it puts the Braille Lite's first phoneme 6.3 dB under the unit (−1.1 now), so it is rejected.
  - The Braille Lite volume-sweep amplitude law does not help.
  - Slowing only code-to-code changes, which only the Accent makes, reaches 1.21 / 3.82 / 7.08.
  - Tomi heard A/B/C renders, and judged a shallower dip "a little better" but the dip itself the Accent's own. Kept as is.
  - H9 was not read.

**2026-09-25, Claude: repository layout only, no change to the sound.** `engine/` became `src/`, and its README became `docs/engine.md`. The Accent demo render is bit-identical before and after. All three add-ons build, with `check_native_core` passing on both Pythons, and `driver_sim.py` passes on NVDA 2026 and 2021.1 with all three add-ons in one process.

**2026-09-26, Claude: Accent host fix (add-on 0.3.6), no change to the chip.**
- **The freeze.** Tomi's Accent went silent at random while he scrolled through Mastodon. `say()` far-called into the driver from wherever the CPU was, and about 1 `run()` block in 400 ends inside the driver's IRQ handler, which runs from the EMS page at D000 and was then partway through its register writes. The far call abandoned the handler: its page map stayed swapped in, the chip's registers were half written, and its frame was left on the stack. The card never spoke again.
- **Reproduced:** 300 scroll-like say/run/cancel cycles, 3 seeds. Every run went silent after its one mid-handler cancel. With `_settle()`, which lets the handler run to its IRET before `say()` touches a register, all 3 keep talking.
- **Checks:** the demo render is bit-identical before and after.
- **Also in the add-on:** "~" becomes a space. "~/" opens the Accent's phoneme input (manual 4.2), which swallowed everything until the next "~".

**2026-09-26, Claude: Accent host, the stale-IRQ freeze (add-on 0.3.8), no change to the chip.** 0.3.6 did not fix Tomi's freeze. 0.3.7's log caught it: the CPU was idle and the card "speaking", with IRQ2 in service and another latched.
- **The sequence.** Long Mastodon items (380-1,070 characters) fill the driver's buffer, so INT 17h blocks and serves the card by polling. The host's PIC kept an edge latched after that request had been served, and delivered it later. The handler read status, found no request, and returned without an EOI (3AF4 -> 3B6A), so IRQ2 stayed in service for good.
- **The fix.** A real 8259 needs the request held until acknowledged; a dropped one is a spurious IRQ 7. `_try_irq` now drops the latch when the line is low.
- **Checks.** Before the fix, long items with quick cancels froze 4 of 5 seeded runs on the first item. After it, all 5 runs got through 200 items. The demo render is bit-identical.

**2026-09-26, Claude: v0.11, stop release, prompted by Tomi.** Tomi heard an extra soft G in "program" (K HVC) and a "d" before the J of "manager" (D'2 J) on the Braille Lite add-on. Measured on dev only.
- **The firmware writes are identical either way.** K in "kit" and in "give" is the same DP 29 / RE 28 / TA 54 / FF ED.
- **The unit releases a stop early only into an open phoneme** (`tools/early_release_table.py`). Early-window rise over the closure floor, unit / engine v0.10:

  | Stop and what follows | Unit | v0.10 |
  |---|---|---|
  | T -> open | +32 | +33 |
  | T -> PA (106 tokens) | +3.5 | +33 |
  | P -> PA | +4.5 | +30 |
  | K -> HVC | +4.3 | +42 |
  | T -> fricative | +2.2 | +33 |

- **The early timing itself is right** (`tools/burst_to_voicing.py`, which needs no alignment): burst to voicing is 1.07-1.18 frames for T and P, on the unit and in the engine.
- **The alignment is not the cause** (`tools/onset_offset.py`): fricative-to-vowel voicing onsets sit at -0.01 frames on the unit.
- **How the chip knows the next phoneme is not established.** The data sheet has A/R only once the duration is exceeded.
- **The model, `release_lookahead`:** A/R rises `lookahead_lead_frames` (0.5) ahead of a b01 stop's release point. The host's writes are held until the stop's end, so the timeline is unchanged. The stop releases early only if the held R0 is open (closure-clear, VA > 0, FA = 0). Otherwise it stays shut, and with `late_release_burst` its noise is the burst at the next load.
- **`fricative_precharge`, built and switched off by ear.** It charges a following fricative's noise behind the closed gate. It brought the D -> J onset from +15 to +5 ms (`tools/affricate_onset.py`). Tomi chose the version without it in the A/B/C ("the B held one gets it").
- **`check_native_core`:** equal on 3.13 x64 and x86, with 3 new switch sets.
- **Open:**
  - the absolute burst level at a pause (`tools/late_release_levels.py`: the unit's T/P -> PA read about 0 dB re the line, ours -17 to -19; the window may catch the next word);
  - B/D -> PA reads 4-5 dB loud;
  - the unit's voiced closures leak a voice bar (about -14 to -19 dB), ours are silent.
- **Hold-out:** not read.

**2026-09-26, Claude: v0.12, a stop that starts on silence, prompted by a listener.** A listener heard a click in the T of "still" on the Accent ("Well, that was better, but I still don't like your robotic intonation", from accent-demo.wav, now DEV). This is a sound change, so the renders are not bit-identical.
- **What it was** (`tools/accent_spec.py`, waveform and spectrogram pictures, no listening). The Accent spells the T as D'3 D'3 PA'3 D'0, with the PA's amplitude at 0. A closure phoneme runs `closure_delay_frames` (1) of open tract before it shuts, so a vowel can fade into it. After that silent PA there is no vowel, and the open tract voiced D's own target for 10 ms at the vowel's level: a "tock" between the S and the I. The real card is silent there.
- **Not only the Accent** (`tools/stop_after_pause.py`, first-frame peak in dB re the line's loud level, dev only):

  | Line-initial stop | Unit | v0.11 | v0.12 |
  |---|---|---|---|
  | B (106) | -34.4 | -7.8 | -55.0 |
  | D (32) | -30.0 | -8.4 | -55.2 |
  | K P T | -31 to -34 | -54 to -56 | the same |

  Every B or D after silence blipped, in all three add-ons. The voiceless stops never did (VA 0). The unit's -30 to -34 dB is its capture floor, and our -55 is our carrier: a separate open item, not evidence either way. Stops after a vowel are unchanged (-0.4 to +1.4 dB, both versions).
- **The model, `closure_onto_silence`.** A closure phoneme that loads onto a silent tract closes at once, with no delay. Silent means the amplitude is below 0.5, or voice and noise have both faded out. On the demo phrase plus a stop-heavy test sentence, all 21 PA -> stop loads count as silent. S -> D does not, so the S noise still fades into the D.
- **Checks.** `check_native_core`: equal on 3.13 x64 and x86, with one new switch set. `tools/accent_compare.py`: F0 span 90th 2.21 st, max 8.62 st, the same with the rule off. That is drift since the glide fit (1.42 / 2.64), not v0.12.
- **Open: the B of "robotic".** The real Accent's B release has about 30 ms of broadband noise (1-5.5 kHz); ours has none, since the ROM reading gives B FA 0. On dev, the 25 ms after every stop's end is 11-41 dB short at 4-9 kHz (`tools/release_spectra.py`, which now also lists B). A sharper gate reopen (`closure_ramp_ms` 1 and 0.25) changes nothing. This joins the dark-noise question. Not tuned.
- **Hold-out:** not read.
