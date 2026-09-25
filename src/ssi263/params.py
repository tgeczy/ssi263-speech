"""Every uncertain constant of the chip model, each tagged with what it rests on.

Source tags (message-from-claude-2.md section 26.1):
  A      SSI-263A documents: 1985/86 Silicon Systems data books, Votrax SC-02 sheet 12/85
  ISSCC  Maeding, Austin & Maimone, ISSCC 1984 (family architecture, not our mask)
  P      the Visual6502 SSI-263P die (cell readings, visible-metal graphs)
  BL     Braille Lite 2000 audio, MASTER capture 2026-09-24 (one unit, its firmware and
         its capture path; silicon revision not established)
  SC01   borrowed from MAME's SC-01 model -- a different chip; a baseline, never a fact
  GUESS  placeholder; nothing measured yet

A parameter set is a plain dict, so a fit or an alternative reading is just a
dict of overrides.  Nothing in chip.py may hard-code a number that belongs here.
"""

DEFAULTS = {
    # ---- clock and timing ------------------------------------------------------
    "xck_hz": (1_000_000.0, "BL",
               "effective XCK; the BL F0 law is exactly XCK/(8(4096-I)) at 1 MHz"),
    "frame_xck_cycles": (4096, "A", "frame = 4096 (16 - R) / XCK"),
    "pitch_xck_div": (8, "A", "F0 = XCK / (8 (4096 - I))"),
    "phoneme_timer_from": ("load", "GUESS",
                           "phoneme duration counted from the reg-0 load, not from the request.  "
                           "Documentation only: not consumed by chip.py"),

    # ---- inflection (R1[2:0] glide field) ----------------------------------------
    "glide_xck_cycles_per_count": (128, "BL",
                                   "field 0: one I count per 128 (16 - R) XCK cycles; "
                                   "slope x (16 - R) = 61-62 ms/s measured (candidate law)"),
    "glide_field_mult": ((1.000, 0.962, 1.226, 2.462, 2.092, 2.462, 2.462, 2.462), "BL+ACCENT",
                         "fields 0 1 2 4 5 measured at rate 2 (4.46 4.29 5.47 9.33 10.98 ms/s; "
                         "direction confounded).  Fields 3 6 7 (only the Accent uses them: field 7 "
                         "carries 2/3 of its pitch writes) were GUESS extrapolations 1.66 2.85 3.25, "
                         "which made Tomi's 'wobbly vowels'.  Set to field 5's 2.462 on Accent DEV "
                         "(tools/accent_glide_variants.py, accent-demo.wav opening): F0 travel inside "
                         "voiced stretches median 0.17 / 90th 1.42 / max 2.64 st vs the real 0.17 / "
                         "1.10 / 2.82 (was 0.78 / 6.05 / 8.90); 1.66, 1.0, 0.6, 0.35 all fit worse.  "
                         "Read: the fast fields top out at field 5's rate"),

    # ---- articulation / transitions ------------------------------------------------
    "art_codes_per_frame": ((1.4, 1.8, 2.3, 2.9, 3.4, 4.0, 4.6, 5.1), "BL",
                            "R3[6:4] slew of the internal parameter counters, codes per FRAME: "
                            "the BL glides scale with (16 - R) at fixed articulation (berry R->E1 "
                            "~180 ms at rate 2, ~70 ms at rate 10; about 3 frames), so they run on "
                            "the frame clock (A: 'all internal attribute transitioning is performed "
                            "relative to the Speech Rate Register').  Setting 5 = 4.0 (berry and very, dev); the "
                            "other settings are GUESS"),
    "art_amp_mult": (6.0, "BL",
                     "VA and FA counters move this many times faster than the formant counters: "
                     "x3 is MAME's SC-01 tick ratio (amplitudes 625 Hz, formants 208 Hz).  Dev "
                     "(tools/noise_decay.py): at x1 the S noise ran 116 ms into the next vowel, "
                     "at x3 16 ms; the unit's falls ~36 ms before the aligned boundary, so >= 3 "
                     "is needed and finer is below the alignment's resolution.  Tomi heard x1 as "
                     "'extra breath underneath' the T.  v0.9: x6, from the T/P burst width in "
                     "tools/stop_profile.py (unit burst ~1 frame, -17 dB into the vowel; x3 left "
                     "it ~2 frames: Tomi's 'thickened' T, strongest at slow rates).  A fitted "
                     "global slew, not an identified 6:1 clock; it speeds VA and FA together, "
                     "and matched voiced controls have not checked that coupling"),
    "latch_quantize": ("round", "GUESS",
                       "the analog controls see a 4-bit code (ISSCC: four control bits per "
                       "filter); internal counters are finer.  'round' or 'floor'"),
    "amp_slew_per_phoneme": (1.0, "GUESS",
                             "A: amplitude moves linearly 'at rate dependent on the phoneme "
                             "duration setting'; full scale per phoneme duration here"),

    # ---- filters: switched-capacitor sections at fc ----------------------------------
    # LDI mapping: (2 sin(pi f/fc))^2 = K (C0 + sum w_i b_i).  Binary weights reproduce
    # the BL audio (free fits: F2 0.96 2.27 3.77 7.96, F3 0.89 2.00 3.96 8.02 units).
    "f1_K": (0.00255, "BL", "tone 13 pitch 32 LPC late medians, dev reps, 25 entries"),
    "f1_C0": (3.38, "BL", "fixed capacitance in unit caps"),
    "f1_w": ((1.0, 2.0, 4.0, 8.0), "P+BL",
             "row assignment from the P decode (b0 = PAR row); binary AREAS are not measured, "
             "only compatible with the BL fit.  BL audio fit gives b0 = 0.0 +- 0.3 unit; "
             "direct pairs mixed (section 26.3).  Alternative reading: (0, 2, 4, 8)"),
    "f2_K": (0.02837, "BL", "as f1_K"),
    "f2_C0": (1.75, "BL", ""),
    "f2_w": ((1.0, 2.0, 4.0, 8.0), "P+BL",
             "rows from the P decode; binary areas compatible with the BL fit, not measured"),
    "f3_K": (0.03795, "BL", "as f1_K"),
    "f3_C0": (4.07, "BL", ""),
    "f3_w": ((1.0, 2.0, 4.0, 8.0), "P+BL",
             "rows from the P decode; binary areas compatible with the BL fit, not measured"),
    "f4_ratio": (0.163, "BL",
                 "fixed placeholder at the clock-scaled 4.3 kHz (tone 13) pole cluster; "
                 "identity unproven, and ISSCC Fig. 3 lists an F4 control"),
    "f5_ratio": (0.24, "BL", "fixed placeholder at the ~0.24 fc cluster; identity unproven"),
    "section_numerator": ("bilinear", "GUESS",
                          "ISSCC: low-pass sections.  'bilinear' puts the null at fc/2, as "
                          "MAME's SC-01 filters do; 'allpole' leaves the band up to fc/2 open"),
    "bw_ratio": ((0.0025, 0.0040, 0.0060, 0.0300, 0.0600), "GUESS",
                 "B/fc per section; code-independent, as a fixed damping capacitor gives"),
    "nas_f2_bw_gain": (0.5, "GUESS",
                       "NAS -> F2Q is a tracing hypothesis: F2 bandwidth x (1 + gain x NAS)"),

    # ---- closure (ISSCC Fig. 3: closure ramp timing -> VOL) -----------------------
    "closure_enable": (True, "P+BL",
                       "the b00-clear GROUPING (B D P T K HVC HFC) is P; reading it as a closure "
                       "gate is a functional hypothesis supported by BL levels, not a traced net"),
    "closure_delay_frames": (1.0, "BL",
                             "frames after the load before the tract closes.  With the release "
                             "1 frame before the end, T and P run: preceding vowel fades, silence, "
                             "burst -- the unit's order (tools/stop_profile.py; only 4 T and 2 P "
                             "tokens reach vowels in dev).  Mid-segment dev levels for T/P/K are "
                             "now 14-35 dB below the unit's: an open conflict with that profile"),
    "closure_release_frames": (1.25, "BL",
                               "the closure reopens this many frames before the stop's end, so "
                               "its own noise is the burst into the next phoneme.  Dev "
                               "(tools/stop_profile.py): on the unit T and P are silent mid-"
                               "segment and their 5-9 kHz burst peaks at 0.75-0.88 of the "
                               "segment.  0 = never reopen within the phoneme (v0.2-v0.6, "
                               "which put the hiss first: 'Hello STomi')"),
    "closure_release_b01": (True, "P+BL",
                            "only closure phonemes with ROM b01 = 1 (B D P T K) release before their "
                            "end; the b01 = 0 closures (HVC HFC, the H family's holds) stay shut.  "
                            "Dev: K -> HVC is silent on the unit (37 tokens); Tomi heard v0.7's HFC "
                            "release before P as an extra 'd'"),
    "closure_hold_delay_frames": (0.0, "BL",
                                  "closure delay for the b01 = 0 holds (HVC HFC): they close at "
                                  "once.  Dev: K -> HVC silent on the unit from its start"),
    "release_lookahead": (True, "BL",
                          "a b01 stop releases early (closure_release_frames before its end) only "
                          "when the NEXT phoneme is open: closure-clear, voiced and noiseless (vowels, "
                          "R L W M N, KV).  Before PA, a fricative or another closure it stays shut.  "
                          "Dev (tools/early_release_table.py), early-window rise over the closure "
                          "floor, unit: T -> open +32, T -> PA +3.5 (106 tokens), P -> PA +4.5, "
                          "K -> HVC +4.3, T -> fricative +2.2 dB; the firmware's writes are the same "
                          "either way ('kit' and 'give' write K identically).  Tomi heard it as the "
                          "extra soft G in 'program' (K HVC) and a 'd' before 'manager's' J.  HOW the "
                          "chip knows the next phoneme is not established: the data sheet has A/R only "
                          "once the duration is exceeded.  Modelled by lookahead_lead_frames"),
    "lookahead_lead_frames": (0.5, "GUESS",
                              "the model's device for release_lookahead: A/R rises this many frames "
                              "(scaled like the release) before a releasing stop's release point, and "
                              "the host's writes are held until the stop's duration is up, so the "
                              "phoneme timeline is unchanged.  Only the release decision sees them"),
    "fricative_precharge": (False, "BL+EAR",
                            "a stop held before a fricative (D -> J: the 'dge' of manager, storage) "
                            "charges the fricative's noise behind its closed gate over its last "
                            "closure_release_frames, so the frication starts at full level when the "
                            "gate opens.  Dev (tools/affricate_onset.py, D -> J, 2-6 kHz): the unit "
                            "goes from -33 to -2 dB within ~5 ms of the release; without it ours "
                            "reached -6 dB 15 ms after the load.  Tomi first heard that as 'zsh' for 'dzs', but in the A/B/C "
                            "(2026-09-25) chose B, held WITHOUT the precharge: 'the B held one gets it'.  "
                            "Off; the ear over the onset metric"),
    "late_release_burst": (True, "BL",
                           "a stop held to its end (release_lookahead) releases at the next load if "
                           "that phoneme does not close: its noise target, FA x route, is the burst, "
                           "decaying at the amplitude rate.  Dev (late window, end -0.35..+0.6 "
                           "frames): unit T -> PA +16, P -> PA +21, K -> PA +4.9, B -> PA +4.2 dB, "
                           "which scales with the stop's FA (15, 15, 4, 0)"),
    "closure_onto_silence": (True, "BL",
                             "a closure phoneme that loads onto a silent tract (amplitude down, "
                             "or voice and noise faded out) closes at once, with no "
                             "closure_delay_frames: there is no vowel to fade into it.  Dev "
                             "(tools/stop_after_pause.py, first-frame peak re the line): line-"
                             "initial B on the unit -34 dB (106), ours -7.8 with the delay, -55 "
                             "without; D -30 / -8.4 / -55 (32).  The delay voiced the stop's own "
                             "target for a frame: a listener heard it on the Accent as a click in "
                             "the T of 'still' (PA'3 at amplitude 0, then D'0)"),
    "burst_hold_frames": (0.3, "BL",
                          "None: after the release the stop's noise stays until the next load "
                          "(v0.7-v0.9).  A number: the noise target returns to 0 this many "
                          "frames after the release, so the burst ends before the vowel"),
    "burst_tail_level": (0.3, "BL",
                         "after burst_hold_frames the stop's noise target drops to this fraction "
                         "of FA (its aspiration into the next phoneme), not to 0.  v0.10 global "
                         "fit (release 1.25, hold 0.3, tail 0.3) on tools/burst_tail.py: bursts "
                         "at -0.9 (T), -1.0 (P), -1.15 (K) frames against the unit's -1.15, -0.68, "
                         "-0.99; T's tail into the vowel still 11-17 dB cleaner than the unit's, "
                         "inside the boundary-alignment uncertainty"),
    "closure_noise_at_release": (True, "BL",
                                 "a closure phoneme's own noise starts only at its release (per-path "
                                 "mode): on the unit the preceding vowel fades into the stop with no "
                                 "stop noise, then silence, then the burst (tools/stop_profile.py); "
                                 "like the SC-01's delayed FA onset (MAME: rom_vd)"),
    "closure_timing": ("fraction", "BL",
                       "'frames': delay and release in frames (v0.7-v0.10).  'fraction': the same "
                       "values scaled by (phoneme frames / 4), so a 2-frame D'2 closes as a "
                       "4-frame stop does, only faster.  Dev (tools/closure_check.py): the unit's "
                       "D'2 reaches -31 dB mid-segment; with 'frames' a 2-frame stop never closed"),
    "closure_floor_db": (None, "GUESS",
                         "None: the closure mutes fully.  A value: VOL dips only to this level"),
    "closure_target": ("vol", "ISSCC",
                       "'vol' (ISSCC draws closure ramp -> VOL) or 'va' (voiced path only); "
                       "neither fits every stop on dev levels (see tools/compare_dev.py)"),
    "closure_ramp_ms": (4.0, "GUESS", "ramp time down (closure) and back up (next phoneme)"),
    "closure_reopen": (False, "BL",
                       "True: a closure phoneme reopens the tract during its delay (the cloud "
                       "Claude's review patch).  False: consecutive closure phonemes stay shut.  "
                       "Dev: K -> HVC (hard g, 37 tokens) is silent on the unit (-34.5 dB); "
                       "D'2 -> D'2 (18) is quieter than usual (-16 dB).  K->T, P->T untested"),

    # ---- sources ------------------------------------------------------------------
    "pulse_place": ("fractional", "GUESS",
                    "glottal pulse split between the two nearest fc samples, or 'nearest'"),
    "va_law": ("linear15", "SC01", "gain = VA / 15 (the law chip.py uses; not switchable yet)"),
    "fa_law": ("linear15", "SC01", "gain = FA / 15 (the law chip.py uses; not switchable yet)"),
    "lfsr_bits": (15, "SC01", "noise shift register length"),
    "noise_clock_ratio": (1.0, "GUESS",
                          "noise shift clock / fc; ISSCC shows common timing logic, not equality"),
    "noise_shaper": (("bp", 0.28, 0.15), "BL",
                     "current implementation: a two-pole BAND-PASS at 0.28 fc, B 0.15 fc, fitted on "
                     "dev fricative spectra (shape and coefficients are a fit, not a traced "
                     "circuit).  Other forms: (f/fc, B/fc) low-pass resonator, or 'flat'"),
    "noise_into_f2": (0.5, "ISSCC+GUESS", "injection point from Fig. 1; weight GUESS (route 'fixed')"),
    "noise_into_f5": (0.5, "ISSCC+GUESS", "injection point from Fig. 1; weight GUESS (route 'fixed')"),
    "noise_route": ("b02", "BL",
                    "'fixed': the two weights above for every phoneme.  'b02': ROM b02 picks "
                    "the injection (b02 = 0 exactly for S Z TH THV T -> F5; else F2); a "
                    "hypothesis, the SSI's stand-in for the SC-01's FC balance"),
    "noise_f5_point": ("input", "GUESS",
                       "where the F5 noise joins: 'input' (filtered by F5's low-pass) or "
                       "'output' (summed after F5, keeping the shaper's top octave)"),
    "noise_route_mode": ("per_path", "BL",
                         "'switch': the b02 route changes at the phoneme load and the one FA "
                         "level feeds it (v0.3-v0.5).  'per_path': each injection point has its "
                         "own level gliding at the amplitude rate toward FA x weight, so a stop's "
                         "noise dies away on its own path instead of being re-routed through the "
                         "next vowel's F2 (the 'ch' heard after T).  Dev (tools/fricative_spectra, "
                         "release_spectra): T centroid 6.1 kHz vs the unit's 6.9 (switch: 3.3); the "
                         "T release no longer over-fills 2.5-4 kHz, though it is now ~7 dB too "
                         "strong at 6-9 kHz.  Models continuity across loads; two physical "
                         "amplitude counters have not been observed"),
    "noise_route_b02": (((0.0, 1.0), (1.0, 0.25)), "BL",
                        "(w_F2, w_F5) for b02 = 0 and for b02 = 1, route 'b02'.  The grouping is "
                        "P; the weights (incl. 0.25) are a fit, not a switch or capacitor ratio.  "
                        "Dev fricative "
                        "spectra (tools/fricative_spectra.py): S centroid 5.7 vs 5.95 kHz real; "
                        "SCH/J/HF 2.5-4 kHz as real but short of the real 4-6 kHz share"),
    "noise_gain": (0.012, "BL",
                   "noise level against the pulse train; set on dev lines (section B inv-r2 "
                   "reps 0-1) so vowels sit at the line maximum, as on the unit"),

    # ---- output stage ---------------------------------------------------------------
    "hp_ratio": (0.004, "GUESS", "ISSCC: high-pass removes DC and carries VOL; cutoff / fc"),
    "vol_law": ("linear15", "GUESS", "gain = A / 15"),
    "sample_hold": ("zoh", "ISSCC+GUESS",
                    "the S/H BLOCK is ISSCC; the full-period hold at fc is a GUESS "
                    "(what chip.py does; not switchable yet)"),
    "carrier_rel_db": (-60.0, "BL",
                       "fc/8 line re a loud vowel's RMS; idle tails at tone 13 measure "
                       "-57 to -66 dB, the same at volumes 2, 4, 6 (so after the volume stage)"),
    "carrier_h2_db": (-10.0, "BL", "fc/4 line re the fc/8 line"),
    "carrier_when_powered_down": (False, "GUESS", "A: power-down turns the analog circuits off"),
    "output_oversample": (4, "GUESS", "area-sampling rate / host rate before the anti-alias FIR"),
    "output_lowpass_hz": (20000.0, "GUESS",
                          "anti-alias cutoff of the host-rate conversion (a listener's DAC/ADC)"),
    "output_gain": (3.0, "GUESS", "overall scale only; set from Hello stage 1 peaks (not from any recording)"),
}


class Params(dict):
    """dict of name -> value, built from DEFAULTS plus overrides."""

    def __init__(self, overrides=None):
        super().__init__({k: v[0] for k, v in DEFAULTS.items()})
        for k, v in (overrides or {}).items():
            if k not in DEFAULTS:
                raise KeyError("unknown parameter %r" % k)
            self[k] = v

    @staticmethod
    def tag(name):
        return DEFAULTS[name][1]

    def manifest(self):
        """For JSON sidecars: every value with its source tag and note, marking overrides."""
        return {k: {"value": self[k], "tag": DEFAULTS[k][1], "note": DEFAULTS[k][2],
                    "overridden": self[k] != DEFAULTS[k][0]} for k in DEFAULTS}
