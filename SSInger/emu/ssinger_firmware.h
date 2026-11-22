/* ssinger_firmware.h -- clean-room MIDI-to-SC02 translator.
 *
 * Implements EP0396141A2's MIDI interpretation in C (the original
 * 6502 ROM / Atari software is lost; Phase 2 moves this into a
 * 6502-resident ROM image behind the same ssinger_sc_write sink).
 *
 * Default: embodiment 1. Phoneme channel N: Note On -> Phoneme register
 * (velocity ignored -- the pitch channel owns volume); Note Off -> Pause
 * (code 0, silent with any latched amplitude). Pitch channel N+1:
 * Note On -> Inflection (A4 = 440 Hz) + velocity -> Amplitude, both
 * latched (Note Off ignored); pitch is monophonic, latest note wins.
 * Pitch Bend -> Filter Frequency
 * (patent map) or master clock (Polaxis map); Mod wheel -> Articulation
 * (patent) or Filter (Polaxis); CC2 -> inflection fine, CC3 -> rate,
 * CC64 -> internal/external carrier (Polaxis map). Embodiment 2
 * (expander): Program Change -> phoneme.
 *
 * SC-02 phoneme codes/mnemonics below match the die-derived ROM order
 * (third_party/casso/.../Ssi263.cpp:24-88): 00 PA, 01 E .. 0x3F LB.
 * The default note table follows the patent's Fig. 2 span for the first
 * 54 codes (notes 36-89, dark->bright vowels, voiced, voiceless,
 * plosives; 90-93 the 4 spare keys = PA) and puts the chip's other nine
 * codes (M .. LB) on notes 94-102, so all 64 are playable.
 * Polaxis anchors note 36 = U (code 0x16), so their table is arranged
 * phonetically rather than in chip order; until Fig. 2 is transcribed
 * the table stays user-editable (ssinger_set_note_phoneme).
 */
#ifndef SSINGER_FIRMWARE_H
#define SSINGER_FIRMWARE_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>

#ifndef SSINGER_MATH_PI
#define SSINGER_MATH_PI 3.14159265358979323846
#endif

/* SC-02 register addresses as seen on the bus (ssi263.h:104). */
#define SG_SC_R0 0  /* DUR + phoneme */
#define SG_SC_R1 1  /* inflection low (+ glide bits in glide mode) */
#define SG_SC_R2 2  /* rate + inflection high */
#define SG_SC_R3 3  /* articulation + amplitude + CTL/power */
#define SG_SC_R4 4  /* filter frequency */

#define SG_PHONEME_PAUSE 0x00
#define SG_NVOICES_MAX 4

/* Phoneme mnemonics in chip-code order, for piano-roll naming. */
static const char *const ssinger_phoneme_names[64] = {
    "PA","E","E1","Y","YI","AY","IE","I","A","AI","EH","EH1","AE","AE1",
    "AH","AH1","AW","O","OU","OO","IU","IU1","U","U1","UH","UH1","UH2",
    "UH3","ER","R","R1","R2","L","L1","LF","W","B","D","KV","P","T","K",
    "HV","HVC","HF","HFC","HN","Z","S","J","SCH","V","F","THV","TH",
    "M","N","NG",":A",":OH",":U",":UH","E2","LB"
};

typedef enum {
    SG_MAP_PATENT = 0,  /* bend->filter, mod->articulation */
    SG_MAP_POLAXIS = 1  /* bend->master clock, CC2->inflection, mod->filter, CC3->rate */
} ssinger_ctlmap_t;

typedef enum {
    SG_EMB_PHONEME_PITCH = 0,  /* embodiment 1: ch N phoneme, ch N+1 pitch */
    SG_EMB_EXPANDER = 1        /* embodiment 2: ch N pitch, Program Change phoneme */
} ssinger_embodiment_t;

typedef struct ssinger_cfg {
    int base_channel;      /* 0-15; phoneme ch N, pitch ch N+1 (default 0 = ch 1/2) */
    int nvoices;           /* 1 or 4 (quad: pairs base..base+7, i.e. 1/3/5/7 + 2/4/6/8) */
    ssinger_embodiment_t embodiment;
    ssinger_ctlmap_t ctlmap;
    double xck_hz;         /* nominal chip clock (default 1 MHz) */
    int rate;              /* R2 high nibble 0-15 (default 8) */
    int articulation;      /* R3 ART 0-7 (default 5) */
    int filter_ff;         /* R4 center 0-255 (default 0xE4) */
    int glide;             /* R1 low 3 bits 0-7 (default 4) */
    int dur;               /* R0 DUR 0-3 (default 2) */
    int vel_curve;         /* 0 linear, 1 log (:L: 0-9 style) */
    double bend_range_st;  /* Polaxis-map bend full-scale in semitones (default 24) */
    /* Tour rig: each chip's filter, in R4 steps from chip 1's ([0] is
     * always 0). The filter control (mod wheel, or the patent map's pitch
     * wheel) moves every chip and keeps this spread. */
    int filter_off[SG_NVOICES_MAX];
} ssinger_cfg_t;

typedef struct ssinger_voice {
    int phoneme;       /* current phoneme code (0 = PA) */
    int note_held;     /* phoneme stack non-empty (for A/R re-trigger) */
    int pitch_note;    /* latched pitch-channel note, -1 none */
    /* Monophonic last-note priority on the phoneme channel (performance:
     * releasing a key falls back to the still-held one). The pitch channel
     * latches instead (releases ignored), so it needs no stack. */
#define SG_HELD_MAX 16
    uint8_t phon_notes[SG_HELD_MAX];
    uint8_t phon_vel[SG_HELD_MAX];
    int n_phon;
    int cc2;           /* inflection fine 0-127 (64 center) */
    int amplitude;     /* current R3 amplitude nibble */
    int inflection;    /* current 12-bit I */
    int filter_ff;     /* current R4 (bend/mod offsets applied) */
    int carrier_ext;   /* CC64 state */
} ssinger_voice_t;

/* the tests' controls (tests/test_6850_midi.c; nothing else sets them): 1 puts the old behaviour back */
static int ssinger_routing_break = 0;  /* #82: a routing change and All Notes Off release nothing */
static int ssinger_params_break = 0;   /* #83: the parameters compared with the live values */

typedef struct ssinger_state {
    ssinger_cfg_t cfg;
    ssinger_voice_t v[SG_NVOICES_MAX];
    int8_t note2phon[128];  /* note -> phoneme code, -1 = unmapped */
    /* MIDI parser */
    uint8_t running;
    uint8_t msg[2];
    int need, got;
    int in_sysex;
    /* Sinks (bus implements these) */
    void (*sc_write)(int voice, int addr, int value, void *ctx);
    void (*xck_write)(double hz, void *ctx);
    void *sink_ctx;
    double nominal_xck;    /* Master-knob base clock (default 1 MHz) */
    int bend_val;          /* last 14-bit pitch bend, 8192 = center */
    /* Chip 1's filter before clamping: the last of the FF setting, the
     * patent bend (FF + offset) or the Polaxis wheel (absolute) -- the
     * latest writer wins, as on one chip. Chip k gets it + filter_off[k]. */
    int ff_common;
    /* the Rate, Glide and Articulation parameters as last applied (-1: none yet): a controller (CC3, the Patent
     * map's mod wheel) changes the live cfg values, and only a parameter that itself moved overrides it (#83) */
    int param_rate, param_glide, param_art;
} ssinger_state_t;

static void ssinger_default_cfg(ssinger_cfg_t *c)
{
    c->base_channel = 0;
    c->nvoices = 1;
    c->embodiment = SG_EMB_PHONEME_PITCH;
    c->ctlmap = SG_MAP_PATENT;
    c->xck_hz = 1000000.0;
    c->rate = 8;
    c->articulation = 5;
    c->filter_ff = 0xE4;
    c->glide = 4;
    c->dur = 2;
    c->vel_curve = 0;
    c->bend_range_st = 24.0;
    memset(c->filter_off, 0, sizeof(c->filter_off));
}

/* Default note table, all 64 chip codes:
 *   notes 36..89  -> codes 1..54 (E .. TH) in chip order: vowels, R/L/W,
 *                    voiced stops, unvoiced stops, H sounds, fricatives
 *                    (the patent's Fig. 2 span, unchanged since 0.7.0);
 *   notes 90..93  -> PA (the patent's 4 spare keys);
 *   notes 94..102 -> codes 55..63 (M N NG :A :OH :U :UH E2 LB): the
 *                    nasals, German vowels, E2 and LB, in chip order,
 *                    above the spare keys so no earlier key moves. */
#define SG_NOTE_FIRST 36
#define SG_NOTE_SPARE 90
#define SG_NOTE_EXTRA 94
static void ssinger_default_table(ssinger_state_t *s)
{
    int i;
    for (i = 0; i < 128; i++)
        s->note2phon[i] = -1;
    for (i = 0; i < 54; i++)
        s->note2phon[SG_NOTE_FIRST + i] = (int8_t)(i + 1);
    for (i = SG_NOTE_SPARE; i < SG_NOTE_EXTRA; i++)
        s->note2phon[i] = SG_PHONEME_PAUSE;
    for (i = 55; i < 64; i++)
        s->note2phon[SG_NOTE_EXTRA + i - 55] = (int8_t)i;
}

static int sg_clamp_ff(int ff)
{
    return ff < 0 ? 0 : (ff > 255 ? 255 : ff);
}

/* Chip v's filter: the shared filter plus its tour-rig offset. */
static int ssinger_chip_ff(const ssinger_state_t *s, int v)
{
    return sg_clamp_ff(s->ff_common + (v > 0 ? s->cfg.filter_off[v] : 0));
}

static void ssinger_reset(ssinger_state_t *s)
{
    int i;
    for (i = 0; i < SG_NVOICES_MAX; i++) {
        s->v[i].phoneme = SG_PHONEME_PAUSE;
        s->v[i].note_held = 0;
        s->v[i].pitch_note = -1;
        s->v[i].n_phon = 0;
        s->v[i].cc2 = 64;
        s->v[i].amplitude = 0;
        s->v[i].inflection = 0;
        s->v[i].filter_ff = s->cfg.filter_ff;
        s->v[i].carrier_ext = 0;
    }
    s->ff_common = s->cfg.filter_ff;
    s->param_rate = s->param_glide = s->param_art = -1;
    for (i = 0; i < SG_NVOICES_MAX; i++)
        s->v[i].filter_ff = ssinger_chip_ff(s, i);
    s->running = 0;
    s->need = s->got = 0;
    s->in_sysex = 0;
    s->nominal_xck = 1000000.0;
    s->bend_val = 8192;
}

static void ssinger_set_note_phoneme(ssinger_state_t *s, int note, int phon)
{
    if (note >= 0 && note < 128)
        s->note2phon[note] = (int8_t)(phon & 0x3F);
}

/* Mnemonic -> code (case-insensitive), -1 if unknown. */
static int ssinger_phoneme_code(const char *name)
{
    int i, j;
    char up[8];
    for (i = 0; i < 7 && name[i]; i++)
        up[i] = (char)toupper((unsigned char)name[i]);
    up[i] = 0;
    for (j = 0; j < 64; j++) {
        const char *m = ssinger_phoneme_names[j];
        int k = 0;
        while (up[k] && m[k] && up[k] == m[k])
            k++;
        if (!up[k] && !m[k])
            return j;
    }
    return -1;
}

/* Note name -> MIDI number (C4 = 60), -1 on error. Sharps and flats. */
static int ssinger_note_name(const char *name)
{
    static const int8_t semi[7] = { 9, 11, 0, 2, 4, 5, 7 }; /* A B C D E F G */
    int s, oct, n;
    if (!name || !name[0])
        return -1;
    if (toupper((unsigned char)name[0]) < 'A' || toupper((unsigned char)name[0]) > 'G')
        return -1;
    s = semi[toupper((unsigned char)name[0]) - 'A'];
    name++;
    if (*name == '#') {
        s++;
        name++;
    } else if (*name == 'b' || *name == 'B') {
        s--;
        name++;
    }
    if (!*name)
        return -1;
    oct = atoi(name);
    if ((oct == 0 && name[0] != '0') && !(name[0] == '-' || name[0] == '+'))
        return -1;
    n = (oct + 1) * 12 + s;
    return (n >= 0 && n < 128) ? n : -1;
}

/* Load a map file (lines "NAME=PHONEME", "#" comments) into the table.
 * overlay = 0 replaces the whole table first, 1 merges onto the current
 * one. Returns applied lines, or -1 if the file cannot be opened.
 * Firmware-level API (covered by tests); the VST itself uses only the
 * compiled-in default -- note_map.txt is its reference doc, not input. */
static int ssinger_load_map(ssinger_state_t *s, const char *path, int overlay)
{
    FILE *f = fopen(path, "r");
    char line[128];
    int applied = 0, i;
    if (!f)
        return -1;
    if (!overlay)
        for (i = 0; i < 128; i++)
            s->note2phon[i] = -1;
    while (fgets(line, sizeof(line), f)) {
        char *eq, *nm, *ph, *end;
        int note, code;
        /* '#' opens a comment at the line's start or after a space; right
         * after a letter it is a sharp (C#2=E1). */
        for (end = line; *end && *end != '\r' && *end != '\n'; end++)
            if (*end == '#' && (end == line || end[-1] == ' ' || end[-1] == '\t'))
                break;
        *end = 0;
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        nm = line;
        ph = eq + 1;
        while (*nm == ' ' || *nm == '\t')
            nm++;
        while (*ph == ' ' || *ph == '\t')
            ph++;
        end = nm + strlen(nm);
        while (end > nm && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = 0;
        end = ph + strlen(ph);
        while (end > ph && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = 0;
        if (!*nm || !*ph)
            continue;
        note = ssinger_note_name(nm);
        code = ssinger_phoneme_code(ph);
        if (note < 0 || code < 0)
            continue;
        s->note2phon[note] = (int8_t)code;
        applied++;
    }
    fclose(f);
    return applied;
}

/* Equal-tempered frequency for a MIDI note. */
static double ssinger_note_freq(int note)
{
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

/* 12-bit inflection for a frequency at the given XCK: F0 = XCK/(8(4096-I)).
 * CC2 fine adds +/-1 semitone across its 0..127 range (64 = center). */
static int ssinger_freq_to_inflection(double f, double xck, int cc2)
{
    double ft = f * pow(2.0, (cc2 - 64) / (12.0 * 64.0));
    long i;
    if (!(ft > 0.0))
        return 0;
    i = (long)(4096.0 - xck / (8.0 * ft) + 0.5);
    if (i < 0)
        i = 0;
    if (i > 4095)
        i = 4095;
    return (int)i;
}

/* Effective master clock: the nominal (Master-knob) base bent by the last
 * Polaxis-map pitch bend. Recomputed absolutely from the base every time,
 * so center always returns exactly and repeated bends never drift -- the
 * clock stays performance-ready. Pitch follows XCK like the hardware
 * (F0 = XCK/(8(4096-I))), so a bent clock transposes in exact semitones
 * with no inflection rewrite needed. */
static double ssinger_effective_xck(const ssinger_state_t *s)
{
    if (s->cfg.ctlmap == SG_MAP_POLAXIS) {
        double st = (s->bend_val - 8192) / 8192.0 * s->cfg.bend_range_st;
        return s->nominal_xck * pow(2.0, st / 12.0);
    }
    return s->nominal_xck;
}

/* Master-knob base in: the effective clock (base through the current bend,
 * if any) hits the chips. */
static void ssinger_set_nominal_xck(ssinger_state_t *s, double hz)
{
    if (!(hz > 0.0))
        return;
    s->nominal_xck = hz;
    if (s->xck_write)
        s->xck_write(ssinger_effective_xck(s), s->sink_ctx);
}

static int ssinger_note_to_inflection(const ssinger_state_t *s, int voice)
{
    const ssinger_voice_t *v = &s->v[voice];
    if (v->pitch_note < 0)
        return v->inflection;
    return ssinger_freq_to_inflection(ssinger_note_freq(v->pitch_note),
                                      s->cfg.xck_hz, v->cc2);
}

/* Velocity 1..127 -> amplitude nibble 1..15. */
static int ssinger_vel_to_amp(const ssinger_state_t *s, int vel)
{
    int a;
    if (vel <= 0)
        return 0;
    if (vel > 127)
        vel = 127;
    if (s->cfg.vel_curve == 1) {
        /* Logarithmic :L: 0-9 style: amp = 15*log10(1+9v/127). */
        a = (int)(15.0 * log10(1.0 + 9.0 * vel / 127.0) + 0.5);
    } else {
        a = 1 + (vel * 14) / 127;
    }
    if (a < 1)
        a = 1;
    if (a > 15)
        a = 15;
    return a;
}

static void ssinger_emit(ssinger_state_t *s, int voice, int addr, int value)
{
    if (s->sc_write)
        s->sc_write(voice, addr, value & 0xFF, s->sink_ctx);
}

/* R3 groups ART (bits 4-6) with amplitude (bits 0-3); CTL bit 7 stays 0
 * (powered, mode latched by the bus on the CTL 1->0 edge). */
static void ssinger_write_r3(ssinger_state_t *s, int voice, int amp)
{
    ssinger_voice_t *v = &s->v[voice];
    v->amplitude = amp & 0x0F;
    ssinger_emit(s, voice, SG_SC_R3, ((s->cfg.articulation & 7) << 4) | v->amplitude);
}

static void ssinger_write_r0(ssinger_state_t *s, int voice, int phon)
{
    s->v[voice].phoneme = phon & 0x3F;
    ssinger_emit(s, voice, SG_SC_R0, ((s->cfg.dur & 3) << 6) | (phon & 0x3F));
}

/* Split a 12-bit inflection across R1/R2-low and emit both (R2 keeps rate).
 *
 * R1 encoding follows the hardware (RESEARCH.md section 3): outside the
 * pitch-glide mode (DUR=3) every R1 bit is pitch, so R1 carries the full
 * (I>>3) and the write round-trips exactly -- earlier firmware ORed the
 * glide param into R1's low 3 bits in all modes, detuning every note whose
 * (I>>3)&7 differed from the glide setting (up to hundreds of cents).
 * In DUR=3 the engine reads (R1>>3)*64 and the low bits are glide speed,
 * so pitch there is inherently coarse and glide keeps the bits. */
/* Glide mode reaches only I = I11*2048 + 64k + j (k 0-31 from R1, j 0-7 from
 * R2): the nearest of those to I in pitch, i.e. in period (4096 - I). Cutting
 * I5..I3 off instead put every note flat, up to 605 cents at A#5. */
static int ssinger_glide_nearest(int infl)
{
    int best = infl & 0x807, hi, k, j, v, d, bd = 1 << 30;
    for (hi = 0; hi <= 0x800; hi += 0x800)
        for (k = 0; k < 32; k++) {
            j = (infl & 0xFFF) - hi - 64 * k;
            j = j < 0 ? 0 : j > 7 ? 7 : j;
            v = hi + 64 * k + j;
            d = v > infl ? v - infl : infl - v;
            /* the same count error weighs more where the period is short */
            d = (int)((double)d * 4096.0 / (double)(4096 - (v > infl ? v : infl)));
            if (d < bd) {
                bd = d;
                best = v;
            }
        }
    return best;
}

static void ssinger_write_inflection(ssinger_state_t *s, int voice, int infl)
{
    int r1, r2lo;
    s->v[voice].inflection = infl & 0xFFF;
    if ((s->cfg.dur & 3) == 3) {
        infl = ssinger_glide_nearest(infl & 0xFFF);
        r1 = (((infl >> 6) & 0x1F) << 3) | (s->cfg.glide & 7);
    } else
        r1 = (infl >> 3) & 0xFF;
    r2lo = (((infl >> 11) & 1) << 3) | (infl & 7);
    ssinger_emit(s, voice, SG_SC_R1, r1);
    ssinger_emit(s, voice, SG_SC_R2, ((s->cfg.rate & 15) << 4) | r2lo);
}

/* Re-trigger the held phoneme (host service of the chip's A/R request:
 * sustains sung notes exactly like the original firmware looping on A/R). */
static void ssinger_service_request(ssinger_state_t *s, int voice)
{
    if (s->v[voice].note_held)
        ssinger_write_r0(s, voice, s->v[voice].phoneme);
}

/* Which voice does this channel address? Returns -1 for foreign channels.
 * *is_phoneme distinguishes the N (phoneme) / N+1 (pitch) slots. */
static int ssinger_channel_voice(const ssinger_state_t *s, int ch, int *is_phoneme)
{
    int rel = ch - s->cfg.base_channel;
    if (s->cfg.nvoices == 1) {
        if (rel == 0) {
            *is_phoneme = (s->cfg.embodiment == SG_EMB_PHONEME_PITCH);
            return 0;
        }
        if (rel == 1) {
            *is_phoneme = (s->cfg.embodiment != SG_EMB_PHONEME_PITCH);
            return 0;
        }
        return -1;
    }
    /* Quad: speech on base, base+2, ... / pitch on base+1, base+3, ... */
    if (rel < 0 || rel >= 2 * s->cfg.nvoices)
        return -1;
    *is_phoneme = ((rel & 1) == 0);
    if (s->cfg.embodiment != SG_EMB_PHONEME_PITCH)
        *is_phoneme = !*is_phoneme;
    return rel / 2;
}

/* Mono-stack push: drop any older copy of the note (re-trigger moves to the
 * top); overfill drops the oldest. Returns the new depth. */
static int sg_held_push(uint8_t *ns, uint8_t *vs, int n, int note, int vel)
{
    int i, w = 0;
    for (i = 0; i < n; i++)
        if (ns[i] != note) {
            ns[w] = ns[i];
            vs[w] = vs[i];
            w++;
        }
    if (w >= SG_HELD_MAX) {
        memmove(ns, ns + 1, (size_t)(SG_HELD_MAX - 1));
        memmove(vs, vs + 1, (size_t)(SG_HELD_MAX - 1));
        w = SG_HELD_MAX - 1;
    }
    ns[w] = (uint8_t)note;
    vs[w] = (uint8_t)vel;
    return w + 1;
}

/* Release: drop every copy of the note. Returns the new depth. */
static int sg_held_drop(uint8_t *ns, uint8_t *vs, int n, int note)
{
    int i, w = 0;
    for (i = 0; i < n; i++)
        if (ns[i] != note) {
            ns[w] = ns[i];
            vs[w] = vs[i];
            w++;
        }
    return w;
}

static void ssinger_note_on(ssinger_state_t *s, int voice, int is_phoneme,
                            int note, int vel)
{
    ssinger_voice_t *v = &s->v[voice];
    int top, before;
    if (is_phoneme) {
        /* Articulation only: velocity never touches volume (the pitch
         * channel owns it, so the two can never fight); releases fall
         * back across held notes, the last one writing Pause. */
        int phon = (note >= 0 && note < 128) ? s->note2phon[note] : -1;
        if (vel == 0) {
            before = v->n_phon;
            v->n_phon = sg_held_drop(v->phon_notes, v->phon_vel, v->n_phon, note);
            if (v->n_phon == before)
                return; /* stray release: not held, nothing changes */
            if (v->n_phon > 0) {
                top = v->n_phon - 1;
                phon = s->note2phon[v->phon_notes[top]];
                if (phon < 0)
                    phon = SG_PHONEME_PAUSE; /* table edited under us */
                v->note_held = 1;
                ssinger_write_r0(s, voice, phon);
            } else {
                v->note_held = 0;
                ssinger_write_r0(s, voice, SG_PHONEME_PAUSE);
            }
            return;
        }
        if (phon < 0)
            return;
        v->n_phon = sg_held_push(v->phon_notes, v->phon_vel, v->n_phon, note, vel);
        v->note_held = 1;
        ssinger_write_r0(s, voice, phon);
    } else {
        /* Dynamics + pitch, latched: note-ons set inflection and amplitude
         * together, releases are ignored (both stay until the next pitch
         * note, latest wins). */
        if (vel == 0)
            return;
        v->pitch_note = note;
        ssinger_write_inflection(s, voice, ssinger_note_to_inflection(s, voice));
        ssinger_write_r3(s, voice, ssinger_vel_to_amp(s, vel));
    }
}

/* The filter control moves every chip at once: chip 1 to `common`
 * (clamped), each other chip to common + its offset. One chip (SEQ) is
 * the same single R4 write as ever. */
static void ssinger_filter_all(ssinger_state_t *s, int common)
{
    int v;
    s->ff_common = common;
    for (v = 0; v < s->cfg.nvoices && v < SG_NVOICES_MAX; v++) {
        s->v[v].filter_ff = ssinger_chip_ff(s, v);
        ssinger_emit(s, v, SG_SC_R4, s->v[v].filter_ff);
    }
}

/* Host FF setting (chip 1's filter): resets the shared filter for all. */
static void ssinger_set_filter(ssinger_state_t *s, int ff)
{
    s->cfg.filter_ff = ff & 0xFF;
    ssinger_filter_all(s, s->cfg.filter_ff);
}

/* Host per-chip setting (tour rig): chip `voice` (1..3) sits `off` R4
 * steps from chip 1. Writes that chip's R4 only; with one chip, or for
 * chip 1, it is stored and does nothing. */
static void ssinger_set_filter_offset(ssinger_state_t *s, int voice, int off)
{
    if (voice <= 0 || voice >= SG_NVOICES_MAX)
        return;
    if (off < -255)
        off = -255;
    if (off > 255)
        off = 255;
    s->cfg.filter_off[voice] = off;
    if (voice < s->cfg.nvoices) {
        s->v[voice].filter_ff = ssinger_chip_ff(s, voice);
        ssinger_emit(s, voice, SG_SC_R4, s->v[voice].filter_ff);
    }
}

static void ssinger_pitch_bend(ssinger_state_t *s, int voice, int value)
{
    /* value: 14-bit, 8192 = center. */
    if (s->cfg.ctlmap == SG_MAP_POLAXIS) {
        /* Coarse pitch by clocking the chip (tour rig): absolute from the
         * nominal base, so the held pitch transposes exactly and center
         * always lands back on nominal. No inflection rewrite: with I
         * computed for the nominal clock, F0 scales with XCK. */
        s->bend_val = value;
        if (s->xck_write)
            s->xck_write(ssinger_effective_xck(s), s->sink_ctx);
    } else {
        /* Patent map: bend drives vocal-tract length (Filter Frequency),
         * on every chip of the tour rig at once. */
        ssinger_filter_all(s, s->cfg.filter_ff + (value - 8192) * 64 / 8192);
    }
    (void)voice;
}

/* every phoneme held on `voice` (-1: on every voice) let go, PA written: All Notes Off, and a routing change, after
   which the held keys' note-offs come on a channel that no longer reaches them (#82) */
static void ssinger_release_all(ssinger_state_t *s, int voice)
{
    int v;
    for (v = 0; v < s->cfg.nvoices && v < SG_NVOICES_MAX; v++) {
        if (voice >= 0 && v != voice)
            continue;
        if (s->v[v].n_phon > 0 || s->v[v].note_held) {
            s->v[v].n_phon = 0;
            s->v[v].note_held = 0;
            ssinger_write_r0(s, v, SG_PHONEME_PAUSE);
        }
    }
}

/* the Phoneme channel and the MIDI embodiment, as the editor or the host sets them: a change lets go of every held
   phoneme first.  It was written under the held key, whose note-off then came on the old channel (dropped) or as a
   pitch release (ignored): the phoneme sang on for good, and panic could not stop it (#82) */
static void ssinger_set_routing(ssinger_state_t *s, int base_channel, ssinger_embodiment_t embodiment)
{
    if ((base_channel != s->cfg.base_channel || embodiment != s->cfg.embodiment) && !ssinger_routing_break)
        ssinger_release_all(s, -1);
    s->cfg.base_channel = base_channel;
    s->cfg.embodiment = embodiment;
}

static void ssinger_cc(ssinger_state_t *s, int voice, int cc, int val)
{
    int i;
    switch (cc) {
    case 1: /* Mod wheel: every chip of the tour rig at once. */
        if (s->cfg.ctlmap == SG_MAP_POLAXIS) {
            /* Polaxis: filter (7-bit). */
            ssinger_filter_all(s, val * 255 / 127);
        } else {
            /* Patent: interpolation speed (Articulation), one setting
             * for all chips, so all chips take it now. */
            s->cfg.articulation = (val * 8 / 128) & 7;
            for (i = 0; i < s->cfg.nvoices && i < SG_NVOICES_MAX; i++)
                ssinger_write_r3(s, i, s->v[i].amplitude);
        }
        break;
    case 2: /* CC2: inflection fine (Polaxis; harmless under patent map). */
        s->v[voice].cc2 = val;
        if (s->v[voice].pitch_note >= 0)
            ssinger_write_inflection(s, voice, ssinger_note_to_inflection(s, voice));
        break;
    case 3: /* CC3: control/transition rate (Polaxis) -> RATE. */
        s->cfg.rate = (val * 16 / 128) & 15;
        ssinger_write_inflection(s, voice, s->v[voice].inflection);
        break;
    case 120: /* All Sound Off and All Notes Off: the voice's held phonemes let go (#82). */
    case 123:
        if (!ssinger_routing_break)
            ssinger_release_all(s, voice);
        break;
    case 64: /* Sustain: internal/external carrier. */
        s->v[voice].carrier_ext = (val >= 64) ? 1 : 0;
        break;
    default:
        break;
    }
}

/* the Rate, Glide and Articulation parameters, applied each block: each written only when it itself moved.
   Compared with the live values, CC3's rate and the Patent mod wheel's articulation were put back to the knob's at
   the next block (#83; ssinger_params_break 1, the test's control: compared so); and with one test for Rate and
   Glide, Glide moved alone put the rate knob's value over CC3's (a review; ssinger_params_break 2: one test) */
static void ssinger_apply_rate_params(ssinger_state_t *s, int rate, int glide, int art)
{
    int v, n = s->cfg.nvoices < SG_NVOICES_MAX ? s->cfg.nvoices : SG_NVOICES_MAX;
    int rate_moved = ssinger_params_break == 1 ? rate != s->cfg.rate : rate != s->param_rate;
    int glide_moved = ssinger_params_break == 1 ? glide != s->cfg.glide : glide != s->param_glide;
    if (ssinger_params_break == 2)
        rate_moved = glide_moved = rate_moved || glide_moved;
    if (rate_moved) {
        s->param_rate = rate;
        s->cfg.rate = rate & 15;
    }
    if (glide_moved) {
        s->param_glide = glide;
        s->cfg.glide = glide & 7;
    }
    if (rate_moved || glide_moved)
        for (v = 0; v < n; v++)
            ssinger_write_inflection(s, v, s->v[v].inflection);
    if (ssinger_params_break == 1 ? art != s->cfg.articulation : art != s->param_art) {
        s->param_art = art;
        s->cfg.articulation = art & 7;
        for (v = 0; v < n; v++)
            ssinger_write_r3(s, v, s->v[v].amplitude);
    }
}

static void ssinger_program_change(ssinger_state_t *s, int voice, int prog)
{
    if (s->cfg.embodiment == SG_EMB_EXPANDER) {
        /* Program number -> phoneme via the same translation table. */
        int phon = s->note2phon[prog & 0x7F];
        if (phon >= 0) {
            s->v[voice].note_held = 1;
            ssinger_write_r0(s, voice, phon);
        }
    }
}

/* One raw MIDI byte from the 6850. Handles running status, SysEx skip and
 * single-byte realtime. */
static void ssinger_midi_byte(ssinger_state_t *s, uint8_t b)
{
    int ch, voice, is_phoneme, type;
    if (b >= 0xF8)
        return; /* realtime: no effect on parser state */
    if (b == 0xF0) {
        s->in_sysex = 1;
        s->need = s->got = 0;
        return;
    }
    if (s->in_sysex) {
        if (b == 0xF7)
            s->in_sysex = 0;
        return;
    }
    if (b & 0x80) {
        if (b >= 0xF0) {
            /* System common (non-realtime): swallow data bytes, clear
             * running status. */
            s->running = 0;
            s->need = (b == 0xF1 || b == 0xF3) ? 1 : (b == 0xF2 ? 2 : 0);
            s->got = 0;
            return;
        }
        s->running = b;
        type = b & 0xF0;
        s->need = (type == 0xC0 || type == 0xD0) ? 1 : ((type == 0x80 || type == 0x90 ||
                    type == 0xA0 || type == 0xB0 || type == 0xE0) ? 2 : 0);
        s->got = 0;
        return;
    }
    /* Data byte. */
    if (s->running == 0 || s->running >= 0xF0) {
        if (s->need > 0 && s->running == 0) {
            if (++s->got >= s->need) {
                s->need = s->got = 0;
            }
        }
        return;
    }
    s->msg[s->got++] = b;
    if (s->got < s->need)
        return;
    s->got = 0;
    ch = s->running & 0x0F;
    type = s->running & 0xF0;
    voice = ssinger_channel_voice(s, ch, &is_phoneme);
    if (type == 0xC0) {
        if (voice >= 0)
            ssinger_program_change(s, voice, s->msg[0] & 0x7F);
        return;
    }
    if (type == 0xD0)
        return; /* channel pressure: no SC-02 target; consumed */
    if (voice < 0)
        return;
    switch (type) {
    case 0x80:
        ssinger_note_on(s, voice, is_phoneme, s->msg[0] & 0x7F, 0);
        break;
    case 0x90:
        ssinger_note_on(s, voice, is_phoneme, s->msg[0] & 0x7F, s->msg[1] & 0x7F);
        break;
    case 0xB0:
        ssinger_cc(s, voice, s->msg[0] & 0x7F, s->msg[1] & 0x7F);
        break;
    case 0xE0:
        ssinger_pitch_bend(s, voice, (s->msg[1] << 7) | s->msg[0]);
        break;
    default:
        break; /* poly pressure: consumed */
    }
}

#endif /* SSINGER_FIRMWARE_H */
