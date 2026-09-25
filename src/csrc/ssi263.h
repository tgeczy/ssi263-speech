/* ssi263 -- the engine's SSI-263 chip model in portable C99.

   A line-by-line port of ssi263/chip.py (engine v0.10), which stays the reference:
   research and tuning happen there, and tools/check_native_core.py holds this port to
   it (request timing and chip time exact, audio within float noise, PCM identical).
   This core is what the NVDA add-ons run, and what a SAPI or Linux front end would.

   The host writes registers (ssi263_write), watches the A/R request (ssi263_request),
   and pulls audio at its own rate (ssi263_run, ssi263_run_until_request).  The chip
   knows phoneme codes, never words; the ROM table and every parameter come from the
   caller (ssi263/native.py builds them from params.py and data/rom_bits.csv). */
#ifndef SSI263_H
#define SSI263_H

#ifdef _WIN32
#define SSI263_API __declspec(dllexport)
#else
#define SSI263_API __attribute__((visibility("default")))
#endif

/* Every constant of params.py.  All doubles, so the layout is the same for every
   compiler and for Python's ctypes: switches are 0/1 (or small codes), noted below. */
typedef struct ssi263_params {
    double xck_hz;
    double frame_xck_cycles;
    double pitch_xck_div;
    double glide_xck_cycles_per_count;
    double glide_field_mult[8];
    double art_codes_per_frame[8];
    double art_amp_mult;
    double latch_round;                  /* latch_quantize: 1 "round" (half to even), 0 "floor" */
    double amp_slew_per_phoneme;
    double f_K[3];                       /* F1 F2 F3 */
    double f_C0[3];
    double f_w[3][4];
    double f4_ratio;
    double f5_ratio;
    double section_bilinear;             /* section_numerator: 1 "bilinear", 0 "allpole" */
    double bw_ratio[5];
    double nas_f2_bw_gain;
    double closure_enable;
    double closure_delay_frames;
    double closure_release_frames;
    double closure_release_b01;
    double closure_hold_delay_frames;
    double has_burst_hold;               /* burst_hold_frames is not None */
    double burst_hold_frames;
    double burst_tail_level;
    double closure_noise_at_release;
    double closure_fraction;             /* closure_timing: 1 "fraction", 0 "frames" */
    double has_closure_floor;            /* closure_floor_db is not None */
    double closure_floor_db;
    double closure_va;                   /* closure_target: 1 "va", 0 "vol" */
    double closure_ramp_ms;
    double closure_reopen;
    double pulse_fractional;             /* pulse_place: 1 "fractional", 0 "nearest" */
    double lfsr_bits;
    double noise_clock_ratio;
    double noise_shaper;                 /* 0 "flat", 1 (f/fc, B/fc) low-pass, 2 ("bp", f/fc, B/fc) */
    double noise_shaper_ratio;
    double noise_shaper_bw;
    double noise_into_f2;
    double noise_into_f5;
    double noise_route_b02;              /* noise_route: 1 "b02", 0 "fixed" */
    double noise_route_b02_w[2][2];      /* (w_F2, w_F5) for b02 = 0, 1 */
    double noise_f5_output;              /* noise_f5_point: 1 "output", 0 "input" */
    double noise_per_path;               /* noise_route_mode: 1 "per_path", 0 "switch" */
    double noise_gain;
    double hp_ratio;
    double carrier_rel_db;
    double carrier_h2_db;
    double carrier_when_powered_down;
    double output_oversample;
    double output_lowpass_hz;
    double output_gain;
    double release_lookahead;            /* a b01 stop releases early only into an open phoneme */
    double lookahead_lead_frames;        /* A/R that far ahead of the release point; writes held */
    double late_release_burst;           /* a held stop's noise is the burst at the next load */
    double fricative_precharge;          /* a stop held before a fricative charges its noise */
    double closure_onto_silence;         /* a closure loaded onto silence closes at once */
} ssi263_params;

/* One ROM entry per phoneme code 0-63, 9 bytes: F1 F2 F3 NAS VA FA (4-bit fields as
   read by the candidate decode), then the flags closure_clear (b00), class1 (b01),
   class2 (b02). */
#define SSI263_ROM_BYTES (64 * 9)

typedef struct ssi263 ssi263;

SSI263_API int ssi263_params_size(void);
SSI263_API ssi263 *ssi263_new(const ssi263_params *p, const unsigned char *rom, double out_rate);
SSI263_API void ssi263_free(ssi263 *c);

/* addr = RS2..RS0 as on the bus: 0-3 select R0-R3, 4-7 select R4. */
SSI263_API void ssi263_write(ssi263 *c, int addr, int value);
SSI263_API int ssi263_reg(const ssi263 *c, int i);
/* A/R asserted (the active-low pin is LOW): timer done, powered, and a mode with A/R.
   Under release_lookahead also ahead of a releasing stop's end; writes made then are held
   until the stop's duration is up, and A/R drops once the next phoneme is held. */
SSI263_API int ssi263_request(const ssi263 *c);
/* Chip time aligned with the audio already returned to the host. */
SSI263_API double ssi263_time(const ssi263 *c);
SSI263_API int ssi263_mode(const ssi263 *c);            /* -1 before the first CTL 1 -> 0 */
/* Host feature, not chip behaviour: the next CHANGE of pitch target lands at once. */
SSI263_API void ssi263_set_snap_pitch(ssi263 *c, int on);
SSI263_API int ssi263_get_snap_pitch(const ssi263 *c);

/* Exactly n host-rate samples into out (room for n + 1).  Returns the count. */
SSI263_API long ssi263_run(ssi263 *c, long n, double *out);
/* Up to n host samples, stopping when A/R is asserted.  out needs room for n + 1. */
SSI263_API long ssi263_run_until_request(ssi263 *c, long n, double *out);
/* Advance time with no sound: timer, A/R, counters, amplitude and glide move; filter
   ringing, the held sample and the host-rate filter's history are dropped. */
SSI263_API void ssi263_skip(ssi263 *c, double seconds);

/* Internal state, for the equivalence check (see ssi263.c for the order). */
SSI263_API int ssi263_state(const ssi263 *c, double *out, int cap);

/* The host-rate stage alone (also used by ssi263/dsp.py's C backend). */
SSI263_API int ssi_fir_decimate(const double *fine, int n, const double *taps, int ntaps,
                                double *tail, int *phase, int os, double *out);
SSI263_API void ssi_pcm16(const double *y, int n, double gain, short *out);

#endif
