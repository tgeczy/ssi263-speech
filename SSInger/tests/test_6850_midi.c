/* test_6850_midi.c -- offline checks for the SSInger emulator cores.
 *
 * 1. MC6850: reset/control programming, RDRF/TDRE status, Rx IRQ line,
 *    overrun + framing flags, transmit path.
 * 2. Translator vectors: patent embodiment-1 mapping (phoneme ch N,
 *    pitch ch N+1 at A=440, wheels, CC2/CC3/CC64, program-change
 *    embodiment 2, running status/sysex robustness).
 * 3. Bus: MIDI wire byte -> ACIA IRQ -> translator -> SC-02 latch, CPU
 *    socket map reads/writes, A/R sustain service.
 * 4. Engine smoke: real ssi263 chip speaks through the bus path; amplitude
 *    latches, inflection round-trips, mono priority.
 *
 * Build: gcc -std=c99 -I SSInger -I src/csrc tests/test_6850_midi.c
 *        src/csrc/ssi263.c -lm
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "emu/mc6850.h"
#include "emu/ssinger_firmware.h"
#include "emu/ssinger_bus.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

/* ---- 6850 ------------------------------------------------------------ */

static void test_acia_reset(void)
{
    mc6850_t m;
    mc6850_init(&m);
    CHECK(mc6850_read_status(&m) == M6850_SR_TDRE, "power-on status 0x%02X", mc6850_read_status(&m));
    CHECK(mc6850_irq_level(&m) == 0, "power-on IRQ quiet");
    mc6850_receive_byte(&m, 0x90, 0); /* in reset: ignored */
    CHECK(!(mc6850_read_status(&m) & M6850_SR_RDRF), "bytes ignored while in reset");
    mc6850_write_ctrl(&m, 0x03); /* master reset, explicit */
    CHECK(mc6850_read_status(&m) == M6850_SR_TDRE, "post-reset status");
    mc6850_write_ctrl(&m, 0x95); /* /16, 8N1, no Tx IRQ, Rx IRQ on */
    CHECK(mc6850_read_status(&m) == M6850_SR_TDRE, "programmed status 0x%02X", mc6850_read_status(&m));
}

static int irq_edges;
static void irq_probe(int level, void *ctx)
{
    (void)ctx;
    irq_edges += level ? 1000 : 1; /* thousands = asserts, ones = clears */
}

static void test_acia_rx_irq(void)
{
    mc6850_t m;
    mc6850_init(&m);
    m.irq_cb = irq_probe;
    m.irq_ctx = 0;
    irq_edges = 0;
    mc6850_write_ctrl(&m, 0x03);
    mc6850_write_ctrl(&m, 0x95);
    mc6850_receive_byte(&m, 0x90, 0);
    CHECK(mc6850_read_status(&m) == (M6850_SR_IRQ | M6850_SR_TDRE | M6850_SR_RDRF),
          "rx status 0x%02X", mc6850_read_status(&m));
    CHECK(irq_edges == 1000, "IRQ asserted once (%d)", irq_edges);
    CHECK(mc6850_read_data(&m) == 0x90, "data read back");
    CHECK(!(mc6850_read_status(&m) & M6850_SR_RDRF), "RDRF cleared by read");
    CHECK(!(mc6850_read_status(&m) & M6850_SR_IRQ), "IRQ cleared by read");
    CHECK(irq_edges == 1001, "IRQ cleared once (%d)", irq_edges);
}

static void test_acia_overrun_framing(void)
{
    mc6850_t m;
    mc6850_init(&m);
    mc6850_write_ctrl(&m, 0x03);
    mc6850_write_ctrl(&m, 0x95);
    mc6850_receive_byte(&m, 0x3C, 0);
    mc6850_receive_byte(&m, 0x40, 0); /* lost: RDRF still held */
    CHECK(mc6850_read_status(&m) & M6850_SR_OVRN, "overrun flagged");
    CHECK(mc6850_read_data(&m) == 0x3C, "older byte kept");
    CHECK(!(mc6850_read_status(&m) & M6850_SR_OVRN), "overrun cleared by read");
    mc6850_receive_byte(&m, 0x40, 1); /* bad stop bit */
    CHECK(mc6850_read_status(&m) & M6850_SR_FE, "framing error flagged");
    CHECK(mc6850_read_data(&m) == 0x40, "framed byte still delivered");
}

static void test_acia_tx(void)
{
    mc6850_t m;
    mc6850_init(&m);
    mc6850_write_ctrl(&m, 0x03);
    mc6850_write_ctrl(&m, 0x35); /* 8N1, Tx IRQ enabled, Rx IRQ off */
    mc6850_write_data(&m, 0xFE);
    CHECK(!(mc6850_read_status(&m) & M6850_SR_TDRE), "TDRE clears on write");
    CHECK(!(mc6850_read_status(&m) & M6850_SR_IRQ), "no IRQ while busy");
    mc6850_tx_done(&m);
    CHECK(mc6850_read_status(&m) & M6850_SR_TDRE, "TDRE back on done");
    CHECK(mc6850_read_status(&m) & M6850_SR_IRQ, "Tx-empty IRQ when enabled");
}

/* ---- translator -------------------------------------------------------- */

typedef struct {
    int n;
    int voice[64], addr[64], value[64];
    double xck;
} sc_log_t;

static void sc_probe(int voice, int addr, int value, void *ctx)
{
    sc_log_t *l = (sc_log_t *)ctx;
    if (l->n < 64) {
        l->voice[l->n] = voice;
        l->addr[l->n] = addr;
        l->value[l->n] = value;
    }
    l->n++;
}

static void xck_probe(double hz, void *ctx)
{
    ((sc_log_t *)ctx)->xck = hz;
}

static void fw_init(ssinger_state_t *s, sc_log_t *l)
{
    memset(l, 0, sizeof(*l));
    ssinger_default_cfg(&s->cfg);
    s->sc_write = sc_probe;
    s->xck_write = xck_probe;
    s->sink_ctx = l;
    ssinger_default_table(s);
    ssinger_reset(s);
}

static void feed(ssinger_state_t *s, const uint8_t *bytes, int n)
{
    int i;
    for (i = 0; i < n; i++)
        ssinger_midi_byte(s, bytes[i]);
}

static void test_embodiment1(void)
{
    ssinger_state_t s;
    sc_log_t l;
    const uint8_t on[] = { 0x90, 60, 100 };
    const uint8_t off[] = { 0x80, 60, 0 };
    const uint8_t pitch[] = { 0x91, 69, 90 };
    const uint8_t pitchoff[] = { 0x81, 69, 0 };
    int r0 = -1, r3 = -1, i, r1 = -1, r2 = -1;
    fw_init(&s, &l);
    feed(&s, on, 3);
    for (i = 0; i < l.n; i++) {
        if (l.addr[i] == SG_SC_R0)
            r0 = l.value[i];
        if (l.addr[i] == SG_SC_R3)
            r3 = l.value[i];
    }
    /* Note 60 -> table code 25 (0x19 UH1); DUR=2 in high bits. Phoneme
     * velocity never touches volume (pitch owns it). */
    CHECK(r0 == ((2 << 6) | 25), "phoneme R0=0x%02X", r0);
    CHECK(r3 == -1, "phoneme writes no R3");
    l.n = 0;
    feed(&s, off, 3);
    r0 = -1;
    for (i = 0; i < l.n; i++)
        if (l.addr[i] == SG_SC_R0)
            r0 = l.value[i];
    CHECK(r0 == ((2 << 6) | SG_PHONEME_PAUSE), "note-off writes PA, R0=0x%02X", r0);
    /* Pitch: A4 440 Hz @1 MHz -> I=3812 -> R1=220, R2=rate|0x0C. */
    l.n = 0;
    feed(&s, pitch, 3);
    for (i = 0; i < l.n; i++) {
        if (l.addr[i] == SG_SC_R1)
            r1 = l.value[i];
        if (l.addr[i] == SG_SC_R2)
            r2 = l.value[i];
    }
    CHECK(r1 == (((3812 >> 3) & 0xF8) | 4), "inflection R1=%d", r1);
    CHECK(r2 == ((8 << 4) | 0x0C), "inflection R2=0x%02X", r2);
    l.n = 0;
    feed(&s, pitchoff, 3);
    CHECK(l.n == 0, "pitch-off latched: no writes");
}

static void test_wheels_and_cc(void)
{
    ssinger_state_t s;
    sc_log_t l;
    const uint8_t bend[] = { 0xE0, 0x00, 0x40 }; /* center */
    const uint8_t bendUp[] = { 0xE0, 0x00, 0x50 }; /* +2048 -> FF +16 */
    const uint8_t mod[] = { 0xB0, 1, 127 };
    const uint8_t cc2[] = { 0xB0, 2, 96 };
    const uint8_t cc3[] = { 0xB0, 3, 120 };
    const uint8_t cc64[] = { 0xB0, 64, 100 };
    int i, r4 = -1;
    fw_init(&s, &l);
    /* Patent map: bend center leaves FF at default. */
    feed(&s, bend, 3);
    for (i = 0; i < l.n; i++)
        if (l.addr[i] == SG_SC_R4)
            r4 = l.value[i];
    CHECK(r4 == 0xE4, "bend center keeps FF=0xE4 (got 0x%02X)", r4);
    l.n = 0;
    feed(&s, bendUp, 3);
    r4 = -1;
    for (i = 0; i < l.n; i++)
        if (l.addr[i] == SG_SC_R4)
            r4 = l.value[i];
    CHECK(r4 == 0xE4 + 16, "bend up raises FF (got 0x%02X)", r4);
    /* Patent map: mod -> articulation 7. */
    l.n = 0;
    feed(&s, mod, 3);
    CHECK(s.cfg.articulation == 7, "mod max sets ART=7 (got %d)", s.cfg.articulation);
    /* CC2/CC3/CC64 are map-independent. */
    feed(&s, cc2, 3);
    CHECK(s.v[0].cc2 == 96, "CC2 stored (%d)", s.v[0].cc2);
    feed(&s, cc3, 3);
    CHECK(s.cfg.rate == 15, "CC3 sets rate (%d)", s.cfg.rate);
    feed(&s, cc64, 3);
    CHECK(s.v[0].carrier_ext == 1, "CC64 selects external carrier");
}

static void test_polaxis_map(void)
{
    ssinger_state_t s;
    sc_log_t l;
    const uint8_t bend[] = { 0xE0, 0x00, 0x60 };
    const uint8_t mod[] = { 0xB0, 1, 64 };
    fw_init(&s, &l);
    s.cfg.ctlmap = SG_MAP_POLAXIS;
    feed(&s, bend, 3);
    CHECK(l.xck > 1000000.0, "polaxis bend raises XCK (%f)", l.xck);
    {
        /* Absolute from nominal: center returns exactly, repeats don't drift. */
        const uint8_t center[] = { 0xE0, 0x00, 0x40 };
        double once;
        l.xck = 0.0;
        feed(&s, bend, 3);
        once = l.xck;
        feed(&s, bend, 3);
        CHECK(l.xck == once, "repeat bend is stable (%f)", l.xck);
        feed(&s, center, 3);
        CHECK(l.xck == 1000000.0, "bend center returns to nominal (%f)", l.xck);
    }
    l.n = 0;
    feed(&s, mod, 3);
    CHECK(l.n == 1 && l.addr[0] == SG_SC_R4, "polaxis mod writes R4");
    CHECK(l.value[0] == 64 * 255 / 127, "polaxis mod scales FF (%d)", l.value[0]);
}

static void test_embodiment2_and_parser(void)
{
    ssinger_state_t s;
    sc_log_t l;
    const uint8_t pc[] = { 0xC0, 40 };
    const uint8_t junk1[] = { 0xF0, 0x41, 0x10, 0xF7, /* sysex */
                              0x90, 60, 100, 62, 90, /* running status 2nd note */
                              0xF8 };                /* realtime ignored */
    const uint8_t junk2[] = { 0x80, 60, 0 };
    const uint8_t junk3[] = { 0x80, 62, 0 };
    int i, r0 = -1;
    fw_init(&s, &l);
    s.cfg.embodiment = SG_EMB_EXPANDER;
    feed(&s, pc, 2);
    for (i = 0; i < l.n; i++)
        if (l.addr[i] == SG_SC_R0)
            r0 = l.value[i];
    /* Program 40 -> table[40] = code 5 (AY). */
    CHECK(r0 == ((2 << 6) | 5), "program change selects phoneme, R0=0x%02X", r0);
    /* Parser robustness in embodiment 1. */
    fw_init(&s, &l);
    feed(&s, junk1, (int)sizeof(junk1));
    CHECK(s.v[0].note_held == 1, "running-status note held");
    CHECK(s.v[0].phoneme == s.note2phon[62], "2nd note phoneme %d", s.v[0].phoneme);
    feed(&s, junk2, (int)sizeof(junk2));
    CHECK(s.v[0].note_held == 1, "first off keeps running-status 2nd note");
    CHECK(s.v[0].phoneme == s.note2phon[62], "falls back to 2nd note %d", s.v[0].phoneme);
    feed(&s, junk3, (int)sizeof(junk3));
    CHECK(s.v[0].note_held == 0, "last note-off releases");
    CHECK(s.v[0].phoneme == SG_PHONEME_PAUSE, "last note-off writes PA");
    /* Editable table (e.g. Polaxis anchor: note 36 = U, code 0x16). */
    fw_init(&s, &l);
    ssinger_set_note_phoneme(&s, 36, 0x16);
    {
        const uint8_t u[] = { 0x90, 36, 100 };
        feed(&s, u, 3);
        CHECK(s.v[0].phoneme == 0x16, "remapped note 36 -> U");
    }
    CHECK(strcmp(ssinger_phoneme_names[0x16], "U") == 0, "phoneme name table");
}

static void test_map_file(void)
{
    ssinger_state_t s;
    sc_log_t l;
    const char *path = "ssinger_test_map.tmp";
    FILE *f = fopen(path, "w");
    int n;
    CHECK(f != 0, "map file created");
    fputs("# comment line\n\nh2o=EH\n", f);
    fputs("C4 = uh1\n", f);   /* spaces + lowercase */
    fputs("Db4=SCH\n", f);    /* flat */
    fputs("bogus line\n", f);
    fputs("C9=ZZZ\n", f);     /* unknown phoneme */
    fputs("H4=E\n", f);       /* unknown note */
    fclose(f);
    fw_init(&s, &l);
    n = ssinger_load_map(&s, path, 0);
    CHECK(n == 2, "2 lines applied (%d)", n);
    CHECK(s.note2phon[60] == 0x19, "C4 -> UH1");
    CHECK(s.note2phon[61] == 0x32, "Db4 -> SCH");
    CHECK(s.note2phon[36] == -1, "replace mode clears the rest");
    n = ssinger_load_map(&s, path, 1);
    CHECK(n == 2, "overlay re-applies (%d)", n);
    CHECK(ssinger_note_name("A4") == 69, "A4=69");
    CHECK(ssinger_note_name("C-1") == 0, "C-1=0");
    CHECK(ssinger_note_name("G9") == 127, "G9=127");
    CHECK(ssinger_note_name("C#-1") == 1, "C#-1=1");
    CHECK(ssinger_note_name("X4") < 0, "bad letter rejected");
    CHECK(ssinger_note_name("C") < 0, "missing octave rejected");
    CHECK(ssinger_phoneme_code("uh1") == 0x19, "case-insensitive code");
    CHECK(ssinger_phoneme_code(":OH") == 0x3B, "colon code");
    CHECK(ssinger_phoneme_code("NOPE") < 0, "unknown code rejected");
    CHECK(ssinger_load_map(&s, "no-such-file.tmp", 0) < 0, "missing file errors");
    remove(path);
}

static void test_quad_channels(void)
{
    ssinger_state_t s;
    sc_log_t l;
    const uint8_t v3[] = { 0x94, 60, 100 }; /* ch5 phoneme (3rd pair) */
    const uint8_t p4[] = { 0x97, 69, 100 }; /* ch8 pitch (4th pair) */
    fw_init(&s, &l);
    s.cfg.nvoices = 4;
    feed(&s, v3, 3);
    CHECK(l.n > 0 && l.voice[0] == 2, "ch5 reaches voice 2 (got %d writes)", l.n);
    l.n = 0;
    feed(&s, p4, 3);
    CHECK(l.n > 0 && l.voice[0] == 3, "ch8 reaches voice 3");
}

/* ---- bus + engine ------------------------------------------------------ */

static void test_bus_path(void)
{
    ssinger_bus_t b;
    ssi263_params p;
    const uint8_t seq[] = { 0x90, 48, 110, 0x91, 57, 100 };
    int i, ok;
    ssi263_default_params(&p);
    ok = ssinger_bus_init(&b, 44100.0, 1, &p, ssi263_default_rom());
    CHECK(ok, "bus init");
    CHECK(mc6850_read_status(&b.acia) == (M6850_SR_TDRE),
          "acia programmed, status 0x%02X", mc6850_read_status(&b.acia));
    for (i = 0; i < (int)sizeof(seq); i++)
        ssinger_bus_midi_byte(&b, seq[i]);
    CHECK(b.regs_mirror[0][0] == ((2 << 6) | (48 - 36 + 1)),
          "bus latch R0=0x%02X", b.regs_mirror[0][0]);
    CHECK(b.irq_to_cpu == 0, "IRQ drains fully (level %d)", b.irq_to_cpu);
    /* CPU socket: RAM round-trip, ACIA status, mode switch, SC mirror. */
    ssinger_bus_cpu_write(&b, 0x0200, 0xA5);
    CHECK(ssinger_bus_cpu_read(&b, 0x0200) == 0xA5, "RAM round-trip");
    CHECK(ssinger_bus_cpu_read(&b, 0x1000) == M6850_SR_TDRE, "ACIA status via bus");
    CHECK(ssinger_bus_cpu_read(&b, 0x1800) == SG_MODE_SEQ, "mode switch reads SEQ");
    CHECK(ssinger_bus_cpu_read(&b, 0x1400) == (uint8_t)b.regs_mirror[0][0],
          "SC-02 buffer mirrors R0");
    ssinger_bus_cpu_write(&b, 0x1000, 0x03);
    CHECK(mc6850_read_status(&b.acia) == M6850_SR_TDRE, "CPU can reset ACIA");
    ssinger_bus_free(&b);
}

static void test_engine_smoke(void)
{
    ssinger_bus_t b;
    ssi263_params p;
    static double out[44100];
    double peak = 0.0;
    long i;
    const uint8_t seq[] = { 0x90, 44, 120, 0x91, 62, 110 };
    ssi263_default_params(&p);
    CHECK(ssinger_bus_init(&b, 44100.0, 1, &p, ssi263_default_rom()), "smoke bus init");
    for (i = 0; i < (long)sizeof(seq); i++)
        ssinger_bus_midi_byte(&b, seq[i]);
    ssinger_bus_render(&b, 22050, out);
    ssinger_bus_service_all(&b); /* A/R sustain like the firmware loop */
    ssinger_bus_render(&b, 22050, out);
    for (i = 0; i < 44100; i++) {
        double a = out[i] < 0 ? -out[i] : out[i];
        if (a > peak)
            peak = a;
    }
    CHECK(peak > 1e-4, "chip speaks through bus path (peak %f)", peak);
    /* XCK retune is in-place and exact (no rebuild, registers kept). */
    ssinger_bus_xck_write(2000000.0, &b);
    CHECK(b.xck_hz == 2000000.0, "XCK retuned");
    CHECK(b.regs_mirror[0][0] == ((2 << 6) | (44 - 36 + 1)), "registers survive retune");
    ssinger_bus_free(&b);
}

/* Mirrors SSIngerProcessor's render loop (events, then ssinger_bus_run with
 * the service tick inside). Amplitude latches like the hardware: velocity
 * writes R3 at once, no VST-side slew. */
static void test_vst_render_path(void)
{
    ssinger_bus_t b;
    ssi263_params p;
    static double out[512];
    double e = 0.0, efirst = 0.0;
    int i, k;
    const uint8_t pon[] = { 0x91, 45, 100 };
    const uint8_t non[] = { 0x90, 45, 110 };
    ssi263_default_params(&p);
    CHECK(ssinger_bus_init(&b, 44100.0, 1, &p, ssi263_default_rom()), "vstpath init");
    for (i = 0; i < 3; i++)
        ssinger_bus_midi_byte(&b, pon[i]);
    for (i = 0; i < 3; i++)
        ssinger_bus_midi_byte(&b, non[i]);
    for (k = 0; k < 86; k++) {
        ssinger_bus_run(&b, 512, out);
        for (i = 0; i < 512; i++) {
            e += out[i] * out[i];
            if (k == 0)
                efirst += out[i] * out[i];
        }
    }
    CHECK(sqrt(e / (86.0 * 512)) > 0.02, "VST render path sings (rms %f)",
          sqrt(e / (86.0 * 512)));
    /* First 11.6 ms still ramps the formant latches from silence, but the
     * voice is present immediately (far above the ~0.0001 carrier bleed:
     * no amplitude gating). */
    CHECK(sqrt(efirst / 512) > 0.002, "first block already singing (rms %f)",
          sqrt(efirst / 512));
    ssinger_bus_free(&b);
}

/* Pitch owns volume and latches it: pitch velocity lands at once and no
 * release or phoneme event moves it; phoneme events never write R3. */
static void test_amp_latch(void)
{
    ssinger_bus_t b;
    ssi263_params p;
    const uint8_t phon[] = { 0x90, 48, 127 };
    const uint8_t phonoff[] = { 0x80, 48, 0 };
    const uint8_t pitch[] = { 0x91, 60, 100 };
    const uint8_t pitchoff[] = { 0x81, 60, 0 };
    int i, want = (1 + (100 * 14) / 127);
    ssi263_default_params(&p);
    CHECK(ssinger_bus_init(&b, 44100.0, 1, &p, ssi263_default_rom()), "latch bus init");
    for (i = 0; i < 3; i++)
        ssinger_bus_midi_byte(&b, phon[i]);
    CHECK((b.regs_mirror[0][3] & 0x0F) == 0, "phoneme moves no amp (stays %d)",
          b.regs_mirror[0][3] & 0x0F);
    for (i = 0; i < 3; i++)
        ssinger_bus_midi_byte(&b, pitch[i]);
    CHECK((b.regs_mirror[0][3] & 0x0F) == want,
          "pitch vel latches amp at once (got %d)", b.regs_mirror[0][3] & 0x0F);
    for (i = 0; i < 3; i++)
        ssinger_bus_midi_byte(&b, pitchoff[i]);
    CHECK((b.regs_mirror[0][3] & 0x0F) == want, "pitch-off keeps amp");
    for (i = 0; i < 3; i++)
        ssinger_bus_midi_byte(&b, phonoff[i]);
    CHECK((b.regs_mirror[0][3] & 0x0F) == want, "phoneme-off keeps amp (PA gates)");
    ssinger_bus_free(&b);
}

/* Pitch encoding round-trips through R1/R2 for every MIDI note (DUR != 3):
 * reassembling the emitted registers with the engine formula must give
 * back the exact 12-bit inflection, i.e. optimal half-LSB tuning. */
static void test_inflection_exact(void)
{
    ssinger_state_t s;
    sc_log_t l;
    int note;
    fw_init(&s, &l);
    for (note = 0; note < 128; note++) {
        uint8_t on[] = { 0x91, (uint8_t)note, 100 };
        uint8_t off[] = { 0x81, (uint8_t)note, 0 };
        int i, r1 = -1, r2 = -1, want, got;
        l.n = 0;
        feed(&s, on, 3);
        for (i = 0; i < l.n; i++) {
            if (l.addr[i] == SG_SC_R1)
                r1 = l.value[i];
            if (l.addr[i] == SG_SC_R2)
                r2 = l.value[i];
        }
        CHECK(r1 >= 0 && r2 >= 0, "note %d emits R1+R2", note);
        want = ssinger_freq_to_inflection(ssinger_note_freq(note), s.cfg.xck_hz, 64);
        /* Engine formula, non-glide mode (ssi263.c update_inflection). */
        got = r1 * 8 + (((r2 >> 3) & 1) * 2048 + (r2 & 7));
        CHECK(got == want, "note %d round-trips I (want %d got %d)", note, want, got);
        l.n = 0;
        feed(&s, off, 3);
    }
}

/* Glide mode (DUR 3) reaches only I = I11*2048 + 64k + j: every note must
 * land on the nearest of those in pitch (spacepup: "it glides while doing the
 * off tuneness"). Control: cutting I5..I3 off, as 3bc29e3 did, put every note
 * flat -- 113 cents on average over C2-C6, 605 at A#5. */
static double cents_between(int a, int b)
{
    return 1200.0 * log2((4096.0 - a) / (4096.0 - b));
}

static void test_glide_tuning(void)
{
    ssinger_state_t s;
    sc_log_t l;
    int note, n = 0;
    double sum = 0, worst = 0, sum_cut = 0;
    fw_init(&s, &l);
    s.cfg.dur = 3;
    s.cfg.glide = 2;
    for (note = 36; note <= 84; note++) {
        uint8_t on[] = { 0x91, (uint8_t)note, 100 };
        uint8_t off[] = { 0x81, (uint8_t)note, 0 };
        int i, r1 = -1, r2 = -1, want, got, hi, k, j, best = 0;
        double bc = 1e9, c;
        l.n = 0;
        feed(&s, on, 3);
        for (i = 0; i < l.n; i++) {
            if (l.addr[i] == SG_SC_R1)
                r1 = l.value[i];
            if (l.addr[i] == SG_SC_R2)
                r2 = l.value[i];
        }
        want = ssinger_freq_to_inflection(ssinger_note_freq(note), s.cfg.xck_hz, 64);
        if (want < 2048) {                       /* below the chip's I11 range: not a glide-mode note */
            l.n = 0;
            feed(&s, off, 3);
            continue;
        }
        /* Engine formula, glide mode (ssi263.c update_inflection, mode 3). */
        got = (r1 >> 3) * 64 + ((r2 >> 3) & 1) * 2048 + (r2 & 7);
        for (hi = 0; hi <= 2048; hi += 2048)
            for (k = 0; k < 32; k++)
                for (j = 0; j < 8; j++) {
                    c = fabs(cents_between(want, hi + 64 * k + j));
                    if (c < bc) {
                        bc = c;
                        best = hi + 64 * k + j;
                    }
                }
        CHECK((r1 & 7) == 2, "note %d keeps the glide rate in R1 (R1 %02X)", note, r1);
        CHECK(fabs(fabs(cents_between(want, got)) - bc) < 0.5, "note %d lands on the nearest glide-mode pitch "
              "(I %d, %+.1f cents; the nearest is I %d, %.1f cents)", note, got, cents_between(want, got), best, bc);
        sum += fabs(cents_between(want, got));
        sum_cut += fabs(cents_between(want, (want & 0x807) + (((want >> 6) & 0x1F) * 64)));
        if (fabs(cents_between(want, got)) > worst)
            worst = fabs(cents_between(want, got));
        n++;
        l.n = 0;
        feed(&s, off, 3);
    }
    printf("glide-mode tuning, %d notes: mean %.0f cents off (cutting the bits: %.0f), worst %.0f\n",
           n, sum / n, sum_cut / n, worst);
    CHECK(sum / n < 0.6 * (sum_cut / n), "nearest beats cutting the bits by far (%.0f vs %.0f cents)",
          sum / n, sum_cut / n);
}

/* Full-keyboard frequency sweep (notes 36..96 through the bus path):
 * the emitted R1/R2, reassembled per the engine formula, must land within
 * 12 cents of 12TET at 1 MHz. Fails by hundreds of cents if R1's low bits
 * carry glide instead of pitch (up to +1311 cents at note 96); fixed, the
 * worst note is +9.5 cents (note 90, the chip's own top-range step). */
static void test_pitch_sweep(void)
{
    ssinger_state_t s;
    sc_log_t l;
    int note;
    fw_init(&s, &l);
    for (note = 36; note <= 96; note++) {
        uint8_t on[] = { 0x91, (uint8_t)note, 100 };
        uint8_t off[] = { 0x81, (uint8_t)note, 0 };
        int i, r1 = -1, r2 = -1, I;
        double f, fp, cents;
        l.n = 0;
        feed(&s, on, 3);
        for (i = 0; i < l.n; i++) {
            if (l.addr[i] == SG_SC_R1)
                r1 = l.value[i];
            if (l.addr[i] == SG_SC_R2)
                r2 = l.value[i];
        }
        CHECK(r1 >= 0 && r2 >= 0, "note %d emits R1+R2", note);
        I = r1 * 8 + (((r2 >> 3) & 1) * 2048 + (r2 & 7));
        CHECK(I > 0 && I < 4096, "note %d I in range (%d)", note, I);
        f = ssinger_note_freq(note);
        fp = s.cfg.xck_hz / (8.0 * (4096.0 - I));
        cents = 1200.0 * log(fp / f) / log(2.0);
        if (cents < 0)
            cents = -cents;
        CHECK(cents <= 12.0, "note %d in tune (%d cents, I=%d)", note, (int)cents, I);
        l.n = 0;
        feed(&s, off, 3);
    }
}

/* Pitch channel latches inflection + amplitude (performance): latest
 * note-on wins, releases change nothing. */
static void test_pitch_latch(void)
{
    ssinger_state_t s;
    sc_log_t l;
    const uint8_t c4[] = { 0x91, 60, 100 };
    const uint8_t e4[] = { 0x91, 64, 110 };
    const uint8_t offE[] = { 0x81, 64, 0 };
    const uint8_t offC[] = { 0x80, 60, 0 };
    int e4infl, e4amp;
    fw_init(&s, &l);
    feed(&s, c4, 3);
    CHECK(s.v[0].pitch_note == 60, "first pitch latched");
    feed(&s, e4, 3);
    CHECK(s.v[0].pitch_note == 64, "latest pitch wins");
    e4infl = s.v[0].inflection;
    e4amp = s.v[0].amplitude;
    CHECK(e4infl == ssinger_freq_to_inflection(ssinger_note_freq(64), s.cfg.xck_hz, 64),
          "latched note sets its inflection");
    CHECK(e4amp == ssinger_vel_to_amp(&s, 110), "latched note sets its amp");
    l.n = 0;
    feed(&s, offE, 3);
    CHECK(l.n == 0, "pitch release writes nothing");
    CHECK(s.v[0].pitch_note == 64, "pitch stays latched");
    CHECK(s.v[0].inflection == e4infl && s.v[0].amplitude == e4amp,
          "inflection+amp stay latched");
    feed(&s, offC, 3);
    CHECK(s.v[0].pitch_note == 64, "older release changes nothing");
}

/* Nominal base + bend combine absolutely (studio clock discipline). */
static void test_clock_base_and_bend(void)
{
    ssinger_state_t s;
    sc_log_t l;
    const uint8_t up[] = { 0xE0, 0x00, 0x60 };   /* +12 st at range 24 */
    const uint8_t center[] = { 0xE0, 0x00, 0x40 };
    const uint8_t pbend[] = { 0xE0, 0x00, 0x60 };
    fw_init(&s, &l);
    s.cfg.ctlmap = SG_MAP_POLAXIS;
    ssinger_set_nominal_xck(&s, 2000000.0);
    CHECK(l.xck == 2000000.0, "centered base lands at once (%f)", l.xck);
    feed(&s, up, 3);
    CHECK(l.xck == 4000000.0, "bend scales from the base (%f)", l.xck);
    feed(&s, center, 3);
    CHECK(l.xck == 2000000.0, "center returns to the base (%f)", l.xck);
    /* Patent map: bend owns the filter, never the clock. */
    fw_init(&s, &l);
    feed(&s, pbend, 3);
    CHECK(l.xck == 0.0, "patent bend leaves XCK alone (%f)", l.xck);
}

/* A note played while the clock is moved sounds transposed by the move, like
 * a note already held: the translator tunes against its fixed reference
 * clock and the chip's clock does the transposing (F0 = XCK/(8(4096-I))).
 * Regression: the bus copied the moved clock into that reference, so a note
 * played during a held +12 st bend came out un-bent (440 Hz, not 880) and
 * dropped an octave when the bend returned; the Master clock never reached
 * new notes at all. */
static double bus_f0(const ssinger_bus_t *b)
{
    int r1 = b->regs_mirror[0][1], r2 = b->regs_mirror[0][2];
    int I = ((r2 >> 3) & 1) * 2048 + (r2 & 7) + r1 * 8;   /* mode 2 (DUR 2) */
    return b->xck_hz / (8.0 * (4096 - I));
}

static void bus_midi3(ssinger_bus_t *b, int a, int c, int d)
{
    ssinger_bus_midi_byte(b, (uint8_t)a);
    ssinger_bus_midi_byte(b, (uint8_t)c);
    ssinger_bus_midi_byte(b, (uint8_t)d);
}

static double cents(double f, double want)
{
    return 1200.0 * log(f / want) / log(2.0);
}

static void test_new_note_follows_clock(void)
{
    ssinger_bus_t b;
    ssi263_params p;
    ssi263_default_params(&p);
    CHECK(ssinger_bus_init(&b, 44100.0, 1, &p, ssi263_default_rom()), "clock-follow bus init");
    b.fw.cfg.ctlmap = SG_MAP_POLAXIS;
    bus_midi3(&b, 0x90, 44, 100);                  /* a phoneme */
    bus_midi3(&b, 0x91, 69, 100);                  /* A4 */
    CHECK(fabs(cents(bus_f0(&b), 440.0)) < 12.0, "A4 at rest (%f Hz)", bus_f0(&b));
    bus_midi3(&b, 0xE0, 0x00, 0x60);               /* bend +12 st (range 24) */
    CHECK(fabs(cents(bus_f0(&b), 880.0)) < 12.0, "held A4 bent +12 (%f Hz)", bus_f0(&b));
    bus_midi3(&b, 0x81, 69, 0);
    bus_midi3(&b, 0x91, 69, 100);                  /* A4 played during the bend */
    CHECK(fabs(cents(bus_f0(&b), 880.0)) < 12.0, "A4 played during +12 bend (%f Hz)", bus_f0(&b));
    bus_midi3(&b, 0xE0, 0x00, 0x40);               /* bend back to centre */
    CHECK(fabs(cents(bus_f0(&b), 440.0)) < 12.0, "that A4 after the bend returns (%f Hz)", bus_f0(&b));
    ssinger_bus_master_write(2000000.0, &b);       /* Master clock +12 st */
    bus_midi3(&b, 0x81, 69, 0);
    bus_midi3(&b, 0x91, 69, 100);
    CHECK(fabs(cents(bus_f0(&b), 880.0)) < 12.0, "A4 played after Master +12 (%f Hz)", bus_f0(&b));
    CHECK(b.fw.cfg.xck_hz == 1000000.0, "reference clock unmoved (%f)", b.fw.cfg.xck_hz);
    ssinger_bus_free(&b);
}

/* ssi263_set_xck on a fresh chip == a fresh ssi263_new at that clock:
 * byte-identical output (the narrowed exactness claim in ssi263.h). */
static void test_xck_fresh_exact(void)
{
    ssi263_params p, q;
    const unsigned char *rom = ssi263_default_rom();
    ssi263 *a, *b;
    static double oa[8192], ob[8192];
    static const int prog[5][2] = {
        { 0, (2 << 6) | 25 }, { 1, 220 }, { 2, (8 << 4) | 0x0C },
        { 4, 0xE4 }, { 3, (5 << 4) | 15 }
    };
    long i, n;
    int k;
    ssi263_default_params(&p);
    q = p;
    q.xck_hz = 2000000.0;
    a = ssi263_new(&p, rom, 44100.0);
    b = ssi263_new(&q, rom, 44100.0);
    CHECK(a && b, "fresh chips");
    if (!a || !b) {
        if (a)
            ssi263_free(a);
        if (b)
            ssi263_free(b);
        return;
    }
    ssi263_set_xck(a, 2000000.0); /* before any writes: nothing started */
    for (k = 0; k < 5; k++) {
        ssi263_write(a, prog[k][0], prog[k][1]);
        ssi263_write(b, prog[k][0], prog[k][1]);
    }
    n = ssi263_run(a, 8192, oa);
    CHECK(n == 8192, "chip A renders");
    n = ssi263_run(b, 8192, ob);
    CHECK(n == 8192, "chip B renders");
    for (i = 0; i < 8192; i++)
        if (oa[i] != ob[i]) {
            CHECK(0, "retune byte-identical (first diff %ld: %a vs %a)", i, oa[i], ob[i]);
            break;
        }
    CHECK(i == 8192, "fresh retune == fresh chip at clock");
    ssi263_free(a);
    ssi263_free(b);
}

/* Filter slew: R4 targets latch at once, the chip walks to them (~100 ms
 * full scale) so wheel/bend sweeps glide instead of zippering. */
static void test_filter_slew(void)
{
    ssinger_bus_t b;
    ssi263_params p;
    const uint8_t max[] = { 0xB0, 1, 127 };
    int i;
    ssi263_default_params(&p);
    CHECK(ssinger_bus_init(&b, 44100.0, 1, &p, ssi263_default_rom()), "ffslew init");
    b.fw.cfg.ctlmap = SG_MAP_POLAXIS; /* mod -> filter */
    for (i = 0; i < 3; i++)
        ssinger_bus_midi_byte(&b, max[i]);
    CHECK(b.ff_tgt[0] == 255, "target latches at once (%d)", b.ff_tgt[0]);
    CHECK(b.ff_out[0] == 0xE4, "chip still at start (0x%02X)", b.ff_out[0]);
    CHECK(b.regs_mirror[0][4] == 255, "mirror holds target");
    ssinger_bus_slew_filters(&b, 0.01); /* 10 ms: partway */
    CHECK(b.ff_out[0] > 0xE4 && b.ff_out[0] < 255, "mid-slew (0x%02X)", b.ff_out[0]);
    CHECK(ssi263_reg(b.chip[0], 4) == b.ff_out[0], "chip tracks slew");
    ssinger_bus_slew_filters(&b, 1.0);
    CHECK(b.ff_out[0] == 255, "converges");
    CHECK(ssi263_reg(b.chip[0], 4) == 255, "chip lands");
    ssinger_bus_free(&b);
}

/* ---- host conditions (any DAW, any OS) ------------------------------- */

/* A short sung phrase with a filter sweep (patent map: bend = R4), as
 * timestamped MIDI, the way a DAW hands it over. */
typedef struct {
    long pos; /* sample position at 44.1 kHz; scaled for other rates */
    uint8_t b[3];
} song_ev_t;

static const song_ev_t song[] = {
    { 1000, { 0x91, 57, 100 } },  /* pitch A3 */
    { 1000, { 0x90, 45, 110 } },  /* phoneme */
    { 20000, { 0xE0, 0, 0x10 } }, /* bend down: filter slews */
    { 30000, { 0x90, 50, 110 } },
    { 30500, { 0x80, 45, 0 } },
    { 40000, { 0xE0, 0, 0x70 } }, /* bend up */
    { 45000, { 0x91, 64, 90 } },
    { 60000, { 0x80, 50, 0 } },
    { 61000, { 0x81, 57, 0 } },
};
#define SONG_N ((int)(sizeof(song) / sizeof(song[0])))

/* SSIngerProcessor::processBlock, minus JUCE: per host block, render up to
 * each event, feed its bytes, render the rest -- through ssinger_bus_run. */
static void host_render(ssinger_bus_t *b, int block, double rate_scale,
                        long total, double *out)
{
    long t = 0;
    int e = 0, i;
    while (t < total) {
        long end = t + block, last = t;
        if (end > total)
            end = total;
        while (e < SONG_N && (long)(song[e].pos * rate_scale) < end) {
            long pos = (long)(song[e].pos * rate_scale);
            if (pos > last)
                ssinger_bus_run(b, pos - last, out + last);
            for (i = 0; i < 3; i++)
                ssinger_bus_midi_byte(b, song[e].b[i]);
            last = pos > last ? pos : last;
            e++;
        }
        if (end > last)
            ssinger_bus_run(b, end - last, out + last);
        t = end;
    }
}

/* The same song at any host block size gives the same samples. Regression:
 * the filter slew and A/R service ran at block edges, so a bend at 512
 * samples/block and at 4096 rendered differently (max diff 1.6, the R4
 * glide stepping every 93 ms at 4096). */
static void test_block_size_invariance(void)
{
    static double ref[44100 * 2], got[44100 * 2];
    const int blocks[] = { 1, 7, 64, 1000, 4096, 8192 };
    const long total = 44100 * 2;
    ssinger_bus_t b;
    ssi263_params p;
    int k;
    long i, first;
    double e = 0.0;
    ssi263_default_params(&p);
    CHECK(ssinger_bus_init(&b, 44100.0, 1, &p, ssi263_default_rom()), "blocksize init");
    host_render(&b, 512, 1.0, total, ref);
    CHECK(b.render_cap == 4096, "run never grows the scratch buffer (%ld)", b.render_cap);
    ssinger_bus_free(&b);
    for (i = 0; i < total; i++)
        e += ref[i] * ref[i];
    CHECK(sqrt(e / total) > 0.02, "song sings (rms %f)", sqrt(e / total));
    for (k = 0; k < (int)(sizeof(blocks) / sizeof(blocks[0])); k++) {
        CHECK(ssinger_bus_init(&b, 44100.0, 1, &p, ssi263_default_rom()), "blocksize init %d", blocks[k]);
        host_render(&b, blocks[k], 1.0, total, got);
        ssinger_bus_free(&b);
        first = -1;
        for (i = 0; i < total && first < 0; i++)
            if (got[i] != ref[i])
                first = i;
        CHECK(first < 0, "block %d renders the same as 512 (first diff at sample %ld)", blocks[k], first);
    }
}

/* 44.1 / 48 / 96 kHz: the chip keeps its own time, sings at a like level,
 * and every sample is finite and in range. */
static void test_sample_rates(void)
{
    static double out[96000 * 2];
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    double rms[3];
    int r;
    for (r = 0; r < 3; r++) {
        ssinger_bus_t b;
        ssi263_params p;
        long total = (long)(rates[r] * 2), i, bad = 0;
        double e = 0.0, pk = 0.0;
        ssi263_default_params(&p);
        CHECK(ssinger_bus_init(&b, rates[r], 1, &p, ssi263_default_rom()), "rate %.0f init", rates[r]);
        host_render(&b, 480, rates[r] / 44100.0, total, out);
        for (i = 0; i < total; i++) {
            if (!(out[i] == out[i]) || out[i] > 1e6 || out[i] < -1e6)
                bad++;
            e += out[i] * out[i];
            if (fabs(out[i]) > pk)
                pk = fabs(out[i]);
        }
        rms[r] = sqrt(e / total);
        CHECK(bad == 0, "rate %.0f: all samples finite (%ld bad)", rates[r], bad);
        CHECK(pk < 4.0, "rate %.0f: peak in range (%f)", rates[r], pk);
        CHECK(fabs(ssi263_time(b.chip[0]) - 2.0) < 0.01, "rate %.0f: chip time follows the host (%f s)",
              rates[r], ssi263_time(b.chip[0]));
        ssinger_bus_free(&b);
    }
    CHECK(rms[1] > rms[0] * 0.8 && rms[1] < rms[0] * 1.25, "48k level like 44.1k (%f vs %f)", rms[1], rms[0]);
    CHECK(rms[2] > rms[0] * 0.8 && rms[2] < rms[0] * 1.25, "96k level like 44.1k (%f vs %f)", rms[2], rms[0]);
}

/* No notes: the chip's own idle floor only (measured -79 dBFS peak at the
 * power-on registers, the carrier bleed of test_vst_render_path), never a
 * held tone; and after the last release it falls to that floor or below. */
static void test_idle_floor(void)
{
    static double out[96000];
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    int r;
    for (r = 0; r < 3; r++) {
        ssinger_bus_t b;
        ssi263_params p;
        long n = (long)rates[r], i;
        double pk = 0.0;
        ssi263_default_params(&p);
        CHECK(ssinger_bus_init(&b, rates[r], 1, &p, ssi263_default_rom()), "idle init");
        ssinger_bus_run(&b, n, out);
        for (i = 0; i < n; i++)
            if (fabs(out[i]) > pk)
                pk = fabs(out[i]);
        CHECK(pk < 3.2e-4, "rate %.0f: idle under -70 dBFS (peak %g)", rates[r], pk);
        ssinger_bus_free(&b);
    }
    {
        static double song_out[44100 * 3];
        ssinger_bus_t b;
        ssi263_params p;
        long total = 44100 * 3, i;
        double pk = 0.0;
        ssi263_default_params(&p);
        CHECK(ssinger_bus_init(&b, 44100.0, 1, &p, ssi263_default_rom()), "release init");
        host_render(&b, 512, 1.0, total, song_out);
        for (i = 44100 * 2; i < total; i++)
            if (fabs(song_out[i]) > pk)
                pk = fabs(song_out[i]);
        CHECK(pk < 3.2e-4, "a second after the last release: under -70 dBFS (peak %g)", pk);
        ssinger_bus_free(&b);
    }
}

/* Phoneme channel likewise falls back across overlapping notes. */
static void test_phoneme_mono_priority(void)
{
    ssinger_state_t s;
    sc_log_t l;
    const uint8_t a[] = { 0x90, 60, 100 };
    const uint8_t b[] = { 0x90, 62, 110 };
    const uint8_t offB[] = { 0x80, 62, 0 };
    const uint8_t offA[] = { 0x80, 60, 0 };
    fw_init(&s, &l);
    feed(&s, a, 3);
    CHECK(s.v[0].phoneme == s.note2phon[60], "first phoneme held");
    feed(&s, b, 3);
    CHECK(s.v[0].phoneme == s.note2phon[62], "second phoneme takes priority");
    feed(&s, offB, 3);
    CHECK(s.v[0].phoneme == s.note2phon[60], "release falls back to held phoneme");
    CHECK(s.v[0].note_held == 1, "still held");
    feed(&s, offA, 3);
    CHECK(s.v[0].phoneme == SG_PHONEME_PAUSE, "last release writes PA");
    CHECK(s.v[0].note_held == 0, "released");
}

/* ---- all 64 phonemes (spacepup: "only 54 or 56 of the 63 or 64") ------ */

/* The first chip code no note reaches, or -1 if all 64 are reachable. */
static int first_unreachable(const int8_t *table)
{
    int seen[64] = { 0 }, n, c;
    for (n = 0; n < 128; n++)
        if (table[n] >= 0 && table[n] < 64)
            seen[table[n]] = 1;
    for (c = 0; c < 64; c++)
        if (!seen[c])
            return c;
    return -1;
}

static void test_all_phonemes_reachable(void)
{
    ssinger_state_t s;
    sc_log_t l;
    int8_t old[128], cut[128];
    int n, c, moved = 0, missing;
    fw_init(&s, &l);
    missing = first_unreachable(s.note2phon);
    CHECK(missing < 0, "every chip code reachable from a note (%s is not)",
          missing < 0 ? "-" : ssinger_phoneme_names[missing]);
    /* Nobody's songs change: the 0.7.0 keys keep their codes. */
    for (n = 0; n < 128; n++)
        old[n] = -1;
    for (n = 36; n <= 89; n++)
        old[n] = (int8_t)(n - 35);
    for (n = 90; n <= 93; n++)
        old[n] = SG_PHONEME_PAUSE;
    for (n = 0; n < 128; n++)
        if (old[n] >= 0 && s.note2phon[n] != old[n]) {
            moved++;
            CHECK(0, "note %d moved from %s to %s", n, ssinger_phoneme_names[old[n]],
                  s.note2phon[n] < 0 ? "nothing" : ssinger_phoneme_names[s.note2phon[n]]);
        }
    CHECK(moved == 0, "%d old keys moved", moved);
    /* The nine the 0.7.0 table missed sit on notes 94-102 in chip order. */
    for (c = 0x37; c <= 0x3F; c++)
        CHECK(s.note2phon[94 + c - 0x37] == c, "note %d sings %s (got %d)", 94 + c - 0x37,
              ssinger_phoneme_names[c], s.note2phon[94 + c - 0x37]);
    /* Controls: the checker names what a table leaves out. */
    missing = first_unreachable(old);
    CHECK(missing == 0x37, "control: the 0.7.0 table misses M first (got %s)",
          missing < 0 ? "none" : ssinger_phoneme_names[missing]);
    memcpy(cut, s.note2phon, sizeof(cut));
    for (n = 0; n < 128; n++)
        if (cut[n] == 0x3F)
            cut[n] = -1;
    missing = first_unreachable(cut);
    CHECK(missing == 0x3F, "control: dropping LB is named (got %s)",
          missing < 0 ? "none" : ssinger_phoneme_names[missing]);
    /* And every code really reaches R0 through MIDI, not just the table. */
    for (c = 0; c < 64; c++) {
        int note = -1;
        for (n = 0; n < 128 && note < 0; n++)
            if (s.note2phon[n] == c)
                note = n;
        if (note < 0)
            continue;
        {
            const uint8_t on[] = { 0x90, (uint8_t)note, 100 };
            const uint8_t off[] = { 0x80, (uint8_t)note, 0 };
            l.n = 0;
            feed(&s, on, 3);
            CHECK(l.n >= 1 && l.addr[l.n - 1] == SG_SC_R0 && (l.value[l.n - 1] & 0x3F) == c,
                  "note %d writes %s to R0", note, ssinger_phoneme_names[c]);
            feed(&s, off, 3);
        }
    }
}

/* Embodiment 2: Program Change picks the phoneme through the same table. */
static void test_expander_all_phonemes(void)
{
    ssinger_state_t s;
    sc_log_t l;
    int seen[64] = { 0 }, p, c, missing = -1;
    fw_init(&s, &l);
    s.cfg.embodiment = SG_EMB_EXPANDER;
    for (p = 0; p < 128; p++) {
        const uint8_t pc[] = { 0xC0, (uint8_t)p };
        l.n = 0;
        feed(&s, pc, 2);
        if (l.n >= 1 && l.addr[l.n - 1] == SG_SC_R0)
            seen[l.value[l.n - 1] & 0x3F] = 1;
    }
    for (c = 0; c < 64 && missing < 0; c++)
        if (!seen[c])
            missing = c;
    CHECK(missing < 0, "every chip code reachable by Program Change (%s is not)",
          missing < 0 ? "-" : ssinger_phoneme_names[missing]);
}

/* note_map.txt documents the compiled-in table: loading it must give the
 * same 128 entries (the test runs in the SSInger folder). */
static void test_note_map_doc(void)
{
    ssinger_state_t s, d;
    sc_log_t l, m;
    int n, diff = 0;
    fw_init(&s, &l);
    fw_init(&d, &m);
    CHECK(ssinger_load_map(&d, "note_map.txt", 0) > 0, "note_map.txt loads");
    for (n = 0; n < 128; n++)
        if (s.note2phon[n] != d.note2phon[n]) {
            diff++;
            CHECK(0, "note_map.txt note %d says %d, the firmware %d", n, d.note2phon[n], s.note2phon[n]);
        }
    CHECK(diff == 0, "note_map.txt matches the firmware (%d notes differ)", diff);
}

/* ---- the tour rig's filters (spacepup) ------------------------------- */

/* Bitmask of voices 0..nv-1 that got no R4 write in the log. */
static int r4_missing(const sc_log_t *l, int nv)
{
    int got = 0, i;
    for (i = 0; i < l->n && i < 64; i++)
        if (l->addr[i] == SG_SC_R4)
            got |= 1 << l->voice[i];
    return ((1 << nv) - 1) & ~got;
}

static int r4_of(const sc_log_t *l, int voice)
{
    int i, v = -1;
    for (i = 0; i < l->n && i < 64; i++)
        if (l->addr[i] == SG_SC_R4 && l->voice[i] == voice)
            v = l->value[i];
    return v;
}

static void test_tour_filter_wheel(void)
{
    ssinger_state_t s;
    sc_log_t l;
    const uint8_t chans[] = { 0xB0, 0xB3, 0xB4, 0xB7 }; /* chip 1 phoneme .. chip 4 pitch */
    int k, v;
    /* Polaxis map (the plugin's default): the mod wheel is the filter. */
    fw_init(&s, &l);
    s.cfg.nvoices = 4;
    s.cfg.ctlmap = SG_MAP_POLAXIS;
    for (k = 0; k < 4; k++) {
        const uint8_t mod[] = { chans[k], 1, (uint8_t)(100 - 10 * k) };
        l.n = 0;
        feed(&s, mod, 3);
        CHECK(r4_missing(&l, 4) == 0, "mod wheel on ch %d reaches every chip's filter (missing mask %X)",
              (chans[k] & 15) + 1, r4_missing(&l, 4));
        for (v = 0; v < 4; v++)
            CHECK(r4_of(&l, v) == (100 - 10 * k) * 255 / 127, "ch %d wheel: chip %d R4=%d", (chans[k] & 15) + 1,
                  v + 1, r4_of(&l, v));
    }
    /* Patent map: the pitch wheel is the filter; it moves all four too. */
    fw_init(&s, &l);
    s.cfg.nvoices = 4;
    {
        const uint8_t bendUp[] = { 0xE2, 0x00, 0x50 }; /* chip 2's phoneme channel, +16 */
        feed(&s, bendUp, 3);
        CHECK(r4_missing(&l, 4) == 0, "patent bend reaches every chip's filter (missing mask %X)",
              r4_missing(&l, 4));
        for (v = 0; v < 4; v++)
            CHECK(r4_of(&l, v) == 0xE4 + 16, "patent bend: chip %d R4=%d", v + 1, r4_of(&l, v));
    }
    /* One chip: unchanged, one R4 write for chip 1 only. */
    fw_init(&s, &l);
    s.cfg.ctlmap = SG_MAP_POLAXIS;
    {
        const uint8_t mod[] = { 0xB0, 1, 64 };
        feed(&s, mod, 3);
        CHECK(l.n == 1 && l.voice[0] == 0 && l.value[0] == 64 * 255 / 127, "one chip: one R4 write (%d)", l.n);
    }
    /* Control: a log where only chip 1 followed the wheel is caught. */
    memset(&l, 0, sizeof(l));
    l.n = 1;
    l.addr[0] = SG_SC_R4;
    l.voice[0] = 0;
    CHECK(r4_missing(&l, 4) == 0xE, "control: chip-1-only wheel leaves chips 2-4 missing (mask %X)",
          r4_missing(&l, 4));
}

/* "Chip 2/3/4 filter offset": each moves only its chip; the wheel keeps
 * the spread; chip 1's FF setting moves all; one chip ignores them. */
static void test_tour_filter_offsets(void)
{
    ssinger_state_t s;
    sc_log_t l;
    const uint8_t mod[] = { 0xB2, 1, 100 }; /* chip 2's phoneme channel */
    const int w = 100 * 255 / 127;          /* 200 */
    int v, k;
    fw_init(&s, &l);
    s.cfg.nvoices = 4;
    s.cfg.ctlmap = SG_MAP_POLAXIS;
    for (k = 1; k < 4; k++) {
        l.n = 0;
        ssinger_set_filter_offset(&s, k, -20 * k);
        CHECK(l.n == 1 && l.addr[0] == SG_SC_R4 && l.voice[0] == k, "chip %d offset writes chip %d only (%d writes)",
              k + 1, k + 1, l.n);
        CHECK(l.value[0] == 0xE4 - 20 * k, "chip %d R4=%d", k + 1, l.value[0]);
    }
    l.n = 0;
    feed(&s, mod, 3);
    for (v = 0; v < 4; v++)
        CHECK(r4_of(&l, v) == w - 20 * v, "wheel keeps the spread: chip %d R4=%d (want %d)", v + 1, r4_of(&l, v),
              w - 20 * v);
    /* Clamped at the ends, per chip. */
    l.n = 0;
    ssinger_set_filter_offset(&s, 3, 100);
    CHECK(r4_of(&l, 3) == 255, "offset clamps at 255 (%d)", r4_of(&l, 3));
    /* Chip 1's FF setting moves every chip, offsets kept. */
    l.n = 0;
    ssinger_set_filter(&s, 0x80);
    CHECK(r4_of(&l, 0) == 0x80 && r4_of(&l, 1) == 0x80 - 20 && r4_of(&l, 2) == 0x80 - 40 && r4_of(&l, 3) == 0x80 + 100,
          "FF setting moves all (%d %d %d %d)", r4_of(&l, 0), r4_of(&l, 1), r4_of(&l, 2), r4_of(&l, 3));
    /* Patent map: the bend is the filter; offsets ride on it the same way. */
    s.cfg.ctlmap = SG_MAP_PATENT;
    {
        const uint8_t bendUp[] = { 0xE6, 0x00, 0x50 }; /* chip 4's phoneme channel, +16 */
        l.n = 0;
        feed(&s, bendUp, 3);
        CHECK(r4_of(&l, 0) == 0x90 && r4_of(&l, 1) == 0x90 - 20 && r4_of(&l, 2) == 0x90 - 40 &&
                  r4_of(&l, 3) == 0x90 + 100,
              "patent bend keeps the spread (%d %d %d %d)", r4_of(&l, 0), r4_of(&l, 1), r4_of(&l, 2), r4_of(&l, 3));
    }
    /* Patent map: the mod wheel is articulation, shared, so all chips take it. */
    {
        const uint8_t art[] = { 0xB4, 1, 127 };
        int r3s = 0, i;
        l.n = 0;
        feed(&s, art, 3);
        for (i = 0; i < l.n; i++)
            if (l.addr[i] == SG_SC_R3 && ((l.value[i] >> 4) & 7) == 7)
                r3s |= 1 << l.voice[i];
        CHECK(r3s == 0xF, "patent mod wheel: ART 7 on every chip (mask %X)", r3s);
    }
    /* A reset (power-on, preset load) starts each chip at FF + its offset. */
    ssinger_reset(&s);
    CHECK(s.v[0].filter_ff == 0x80 && s.v[1].filter_ff == 0x80 - 20 && s.v[3].filter_ff == 0x80 + 100,
          "reset seeds the offsets (%d %d %d)", s.v[0].filter_ff, s.v[1].filter_ff, s.v[3].filter_ff);
    /* One chip: the offsets do nothing at all. */
    fw_init(&s, &l);
    s.cfg.ctlmap = SG_MAP_POLAXIS;
    ssinger_set_filter_offset(&s, 1, 50);
    ssinger_set_filter_offset(&s, 3, -50);
    CHECK(l.n == 0, "one chip: offsets write nothing (%d)", l.n);
    {
        const uint8_t m1[] = { 0xB0, 1, 100 };
        feed(&s, m1, 3);
        CHECK(l.n == 1 && l.voice[0] == 0 && l.value[0] == w, "one chip: wheel as before (%d writes, R4=%d)", l.n,
              l.value[0]);
    }
}

/* ---- Phoneme DUR 3 = the pitch-glide mode (spacepup: Glide never glided) */

/* The chip's pitch state: [0] where the I10..I3 part is now, [1] its target. */
static void chip_pitch(ssinger_bus_t *b, double *now, double *target)
{
    double st[64];
    ssi263_state(b->chip[0], st, 64);
    *now = st[2];
    *target = st[3];
}

static void test_dur_glide_mode(void)
{
    static double out[44100];
    ssinger_bus_t b;
    ssi263_params p;
    double now, tgt, tgt60;
    int r1;
    ssi263_default_params(&p);
    CHECK(ssinger_bus_init(&b, 44100.0, 1, &p, ssi263_default_rom()), "glide init");
    CHECK(ssi263_mode(b.chip[0]) == 2, "powers up in mode 2 (got %d)", ssi263_mode(b.chip[0]));
    bus_midi3(&b, 0x91, 60, 100);
    bus_midi3(&b, 0x90, 48, 100);
    ssinger_bus_run(&b, 441, out);

    /* The knob to 3, the way the plug-in turns it: the chip re-latches. */
    b.fw.cfg.glide = 2;
    ssinger_bus_set_dur(&b, 3);
    CHECK(ssi263_mode(b.chip[0]) == 3, "DUR 3 puts the chip in the pitch-glide mode (got mode %d)",
          ssi263_mode(b.chip[0]));
    r1 = b.regs_mirror[0][1];
    CHECK((r1 & 7) == 2, "R1's low bits carry the glide rate (R1 %02X)", r1);
    CHECK((b.regs_mirror[0][0] >> 6) == 3 && (b.regs_mirror[0][0] & 0x3F) == b.fw.v[0].phoneme,
          "the held phoneme starts again (R0 %02X)", b.regs_mirror[0][0]);
    chip_pitch(&b, &now, &tgt60);
    CHECK(now == tgt60 && tgt60 == (double)(((ssinger_glide_nearest(b.fw.v[0].inflection) >> 6) & 0x1F) * 64),
          "the held note's pitch lands at once, at its coarse glide-mode value (%.0f, target %.0f)", now, tgt60);

    /* Legato to the octave: the chip glides there, it doesn't jump. */
    bus_midi3(&b, 0x91, 72, 100);
    bus_midi3(&b, 0x81, 60, 0);
    chip_pitch(&b, &now, &tgt);
    CHECK(tgt > tgt60, "the octave's target is higher (%.0f vs %.0f)", tgt, tgt60);
    ssinger_bus_run(&b, 441, out);   /* 10 ms */
    chip_pitch(&b, &now, &tgt);
    CHECK(now > tgt60 && now < tgt, "10 ms in, the pitch is on its way (%.0f, from %.0f to %.0f)",
          now, tgt60, tgt);
    ssinger_bus_run(&b, 44100, out);
    chip_pitch(&b, &now, &tgt);
    CHECK(now == tgt, "a second in, it has arrived (%.0f of %.0f)", now, tgt);

    /* Back out of 3, even with the knob on 0: mode 2, never 0 (no sustain) or 1. */
    ssinger_bus_set_dur(&b, 0);
    CHECK(ssi263_mode(b.chip[0]) == 2, "DUR 0 after 3 is mode 2 again (got mode %d)", ssi263_mode(b.chip[0]));
    r1 = b.regs_mirror[0][1];
    CHECK(r1 == ((b.fw.v[0].inflection >> 3) & 0xFF), "R1 is all pitch again (R1 %02X)", r1);
    bus_midi3(&b, 0x91, 60, 100);
    bus_midi3(&b, 0x81, 72, 0);
    chip_pitch(&b, &now, &tgt);
    CHECK(now == tgt, "outside glide mode a new note lands at once (%.0f of %.0f)", now, tgt);
    ssinger_bus_run(&b, 4410, out);
    CHECK((b.regs_mirror[0][0] >> 6) == 0, "the held phoneme keeps sustaining with DUR 0's bits (R0 %02X)",
          b.regs_mirror[0][0]);

    /* DUR 0 to 2 stays in mode 2: only the next phoneme's duration bits change. */
    ssinger_bus_set_dur(&b, 1);
    CHECK(ssi263_mode(b.chip[0]) == 2, "DUR 1 keeps mode 2 (got mode %d)", ssi263_mode(b.chip[0]));
    ssinger_bus_free(&b);

    /* Control: 0.8.0 only stored the knob (cfg.dur = 3); the chip stayed in mode 2,
     * which the mode check above rejects. */
    CHECK(ssinger_bus_init(&b, 44100.0, 1, &p, ssi263_default_rom()), "glide control init");
    b.fw.cfg.dur = 3;
    bus_midi3(&b, 0x91, 60, 100);
    CHECK(ssi263_mode(b.chip[0]) == 2, "control: storing DUR 3 alone leaves mode 2 (got %d)",
          ssi263_mode(b.chip[0]));
    ssinger_bus_free(&b);
}

/* ---- tour-rig loudness (spacepup: "much quieter than solo") ----------- */

/* A sung line on the first `nsing` chips (channel pairs 1/2, 3/4, ...),
 * same notes on each; returns RMS and peak of the bus mix. */
static void sing_line(int nvoices, int nsing, int vel, double *rms, double *peak)
{
    static double out[44100 * 2];
    static const int phon[] = { 44, 48, 51, 58 };
    static const int mel[] = { 57, 60, 64, 62 };
    ssinger_bus_t b;
    ssi263_params p;
    long t = 0, i;
    int k, c;
    double e = 0.0, pk = 0.0;
    ssi263_default_params(&p);
    CHECK(ssinger_bus_init(&b, 44100.0, nvoices, &p, ssi263_default_rom()), "line init");
    for (k = 0; k < 4; k++) {
        for (c = 0; c < nsing; c++) {
            bus_midi3(&b, 0x91 + 2 * c, mel[k], vel);
            bus_midi3(&b, 0x90 + 2 * c, phon[k], 100);
        }
        ssinger_bus_run(&b, 19845, out + t);
        t += 19845;
        for (c = 0; c < nsing; c++)
            bus_midi3(&b, 0x80 + 2 * c, phon[k], 0);
        ssinger_bus_run(&b, 2205, out + t);
        t += 2205;
    }
    for (i = 0; i < t; i++) {
        e += out[i] * out[i];
        if (fabs(out[i]) > pk)
            pk = fabs(out[i]);
    }
    *rms = sqrt(e / t);
    *peak = pk;
    ssinger_bus_free(&b);
}

static void test_tour_loudness(void)
{
    double rs, ps, rt, pt, r4, p4, rf, pf;
    sing_line(1, 1, 100, &rs, &ps);
    sing_line(4, 1, 100, &rt, &pt);
    printf("level: solo %.2f dB rms (%.2f peak); tour rig, one chip %.2f dB rms (%.2f peak)\n",
           20 * log10(rs), 20 * log10(ps), 20 * log10(rt), 20 * log10(pt));
    CHECK(fabs(20 * log10(rt / rs)) < 1.0, "tour rig, one chip, as loud as solo (%.2f dB apart)",
          20 * log10(rt / rs));
    /* Four chips at full, in unison (the worst case: they add in phase). */
    sing_line(1, 1, 127, &rf, &pf);
    sing_line(4, 4, 127, &r4, &p4);
    printf("level: solo at full %.2f dB peak; four chips at full %.2f dB peak (%.2f rms)\n",
           20 * log10(pf), 20 * log10(p4), 20 * log10(r4));
    CHECK(p4 < 1.0, "four chips at full stay under 0 dBFS (%.2f dB)", 20 * log10(p4));
    /* Controls. The 0.7.0 mix divided by the chip count: one chip of four
     * sat 12 dB down, which the 1 dB check rejects. A plain sum of four
     * unison chips would clip, which the 0 dBFS check rejects. */
    CHECK(fabs(20 * log10((rt / 4) / rs)) >= 1.0, "control: the divided mix fails the 1 dB check");
    CHECK(4 * pf >= 1.0, "control: a plain sum of four would clip (%.2f dB)", 20 * log10(4 * pf));
}

/* #82: a phoneme held while the routing (Phoneme channel, MIDI embodiment) changes is released then, as its own
   note-off can no longer reach it (sent on the old channel, dropped); and All Notes Off releases a voice's notes.
   SSINGER_ROUTING_BREAK=1 (the control): the routing written as it was, and CC 123 ignored */
static void test_routing_change_releases(void)
{
    ssinger_state_t s;
    sc_log_t l;
    const uint8_t on[] = { 0x90, 60, 100 };
    const uint8_t old_off[] = { 0x80, 60, 0 };
    const uint8_t on2[] = { 0x92, 60, 100 };
    const uint8_t all_off[] = { 0xB2, 123, 0 };
    int i, r0;
    fw_init(&s, &l);
    if (getenv("SSINGER_ROUTING_BREAK"))
        ssinger_routing_break = 1;
    feed(&s, on, 3);
    CHECK(s.v[0].note_held == 1, "the phoneme held on channel 1");
    ssinger_set_routing(&s, 2, s.cfg.embodiment);   /* Phoneme channel moved to 3 under the held key */
    feed(&s, old_off, 3);                            /* the key let go: its note-off on the old channel */
    CHECK(s.v[0].note_held == 0 && s.v[0].n_phon == 0, "a routing change releases the held phoneme (held %d, %d notes)",
          s.v[0].note_held, s.v[0].n_phon);
    l.n = 0;
    feed(&s, on2, 3);                                /* a note on the new channel, then All Notes Off there */
    feed(&s, all_off, 3);
    r0 = -1;
    for (i = 0; i < l.n && i < 64; i++)
        if (l.addr[i] == SG_SC_R0)
            r0 = l.value[i] & 0x3F;
    CHECK(s.v[0].note_held == 0 && r0 == SG_PHONEME_PAUSE, "All Notes Off releases it (held %d, last R0 code %d)",
          s.v[0].note_held, r0);
    ssinger_routing_break = 0;
}

int main(void)
{
    test_acia_reset();
    test_acia_rx_irq();
    test_acia_overrun_framing();
    test_acia_tx();
    test_embodiment1();
    test_wheels_and_cc();
    test_polaxis_map();
    test_embodiment2_and_parser();
    test_map_file();
    test_quad_channels();
    test_bus_path();
    test_engine_smoke();
    test_xck_fresh_exact();
    test_amp_latch();
    test_inflection_exact();
    test_pitch_sweep();
    test_pitch_latch();
    test_phoneme_mono_priority();
    test_clock_base_and_bend();
    test_new_note_follows_clock();
    test_filter_slew();
    test_vst_render_path();
    test_block_size_invariance();
    test_sample_rates();
    test_idle_floor();
    test_all_phonemes_reachable();
    test_expander_all_phonemes();
    test_note_map_doc();
    test_tour_filter_wheel();
    test_routing_change_releases();
    test_tour_filter_offsets();
    test_tour_loudness();
    test_dur_glide_mode();
    test_glide_tuning();
    printf("%s: %d checks, %d failures\n",
           failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
