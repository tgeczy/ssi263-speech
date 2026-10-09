/* so_voice.c -- see so_voice.h.  Each part names the driver function it ports (nvda/speakout/synthDrivers/
 * speakout.py); change them together.  nvda/tools/so_voice_equiv.py compares the two byte for byte. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "so_voice.h"
#include "../numwords.h"
#include "../translit.h"

/* the driver's constants */
#define BLOCK_S 0.03
#define STEP_S 0.0005
#define LEAD_THRESHOLD 0.003
#define LEAD_PREROLL 220
#define BOOT_S 0.3                      /* SpeakOut.boot()'s default */
#define CANCEL_LIMIT 0.6
#define DEFAULT_TONE 8                  /* "i" */
static const char TONES[] = "abcdefghijklmnopqrstuvwxyz";

typedef struct {
    int rate, pitch, tone, word, sentence;          /* _box_settings(): R, P, T, W, I */
} box_settings;

struct so_voice {
    so_host *h;
    ssi263 *chip;
    char *hex;                                       /* the firmware's text, kept for a reboot */
    size_t hex_len;
    double out_rate;
    int rate, pitch, tone, volume, join, short_pauses;   /* NVDA's scales */
    box_settings sent;                               /* _sent */
    int sent_valid;                                  /* _sent is not None */
    int base_pitch, cur_pitch;                       /* settings[1], _cur_pitch */
    int pitch_dirty, snap_until_speech;
    int lead, active, fault;
    int job;                                         /* inside sov_begin ... sov_end: the driver's whole sequence */
    double t_start, gain;
    short *pcm;
    int pcm_cap;
};

/* ---- _boot ---------------------------------------------------------------------------------------------------- */
static so_host *boot(const so_voice *v, double out_rate, char *err, int errlen)
{
    static const unsigned char PUNCT_NONE[] = {0x05, 'M', 'n'};  /* punctuation: none -- NVDA speaks symbols itself */
    const double *y;
    so_host *h = soh_create(v->hex, v->hex_len, NULL, out_rate, err, errlen);
    if (!h)
        return NULL;
    soh_boot(h, BOOT_S);
    soh_say(h, PUNCT_NONE, (int)sizeof PUNCT_NONE);
    soh_run(h, 0.05, STEP_S, &y);
    return h;
}

SO_API so_voice *sov_create_hex(const char *hex, size_t len, double out_rate, char *err, int errlen)
{
    so_voice *v = (so_voice *)calloc(1, sizeof *v);
    if (!v || !(v->hex = (char *)malloc(len ? len : 1))) {
        free(v);
        if (err && errlen > 0) snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    memcpy(v->hex, hex, len);
    v->hex_len = len;
    v->h = boot(v, out_rate, err, errlen);
    if (!v->h) {
        free(v->hex);
        free(v);
        return NULL;
    }
    v->chip = soh_chip(v->h);
    v->out_rate = out_rate;
    /* __init__'s defaults: rate 50 (box 5), pitch 50 (box 3), volume 100, tone i, join and short pauses on */
    v->rate = 50; v->pitch = 50; v->volume = 100; v->tone = DEFAULT_TONE; v->join = 1; v->short_pauses = 1;
    return v;
}

SO_API so_voice *sov_create(const char *firmware, double out_rate, char *err, int errlen)
{
    FILE *f = fopen(firmware, "rb");
    long len;
    char *text;
    so_voice *v;
    if (!f) {
        if (err && errlen > 0) snprintf(err, (size_t)errlen, "cannot open the firmware (SPEAKOUT.HEX)");
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)
            || !(text = (char *)malloc(len ? (size_t)len : 1))) {
        fclose(f);
        if (err && errlen > 0) snprintf(err, (size_t)errlen, "cannot read the firmware");
        return NULL;
    }
    if (fread(text, 1, (size_t)len, f) != (size_t)len) {
        free(text);
        fclose(f);
        if (err && errlen > 0) snprintf(err, (size_t)errlen, "cannot read the firmware");
        return NULL;
    }
    fclose(f);
    v = sov_create_hex(text, (size_t)len, out_rate, err, errlen);
    free(text);
    return v;
}

SO_API void sov_destroy(so_voice *v)
{
    if (!v) return;
    soh_destroy(v->h);
    free(v->hex);
    free(v->pcm);
    free(v);
}

SO_API so_host *sov_host(so_voice *v) { return v->h; }
SO_API ssi263 *sov_chip(so_voice *v) { return v->chip; }
SO_API double sov_sample_rate(const so_voice *v) { return v->out_rate; }
SO_API int sov_fault(const so_voice *v) { return v->fault; }

/* ---- the setters: rate and pitch clamped to 0-100, volume to 0-200 (above 100 louder, ssi_pcm16 clipping at full
   scale, as asv_set and blv_set: Android's slider; the driver stops at 100), a tone outside A-Z ignored ------------ */
static int clamp100(int x) { return x < 0 ? 0 : x > 100 ? 100 : x; }

SO_API void sov_set(so_voice *v, int rate, int pitch, int tone, int volume, int join, int short_pauses)
{
    v->rate = clamp100(rate);
    v->pitch = clamp100(pitch);
    if (tone >= 0 && tone < 26)
        v->tone = tone;
    v->volume = volume < 0 ? 0 : volume > 200 ? 200 : volume;
    v->join = join != 0;
    v->short_pauses = short_pauses != 0;
}

/* ---- _switch_rate: a new box at the new rate; every setting sent again ------------------------------------------- */
SO_API int sov_set_sample_rate(so_voice *v, double out_rate)
{
    char err[128];
    so_host *h;
    if (out_rate == v->out_rate)
        return 0;
    if (v->active)
        sov_cancel(v);
    h = boot(v, out_rate, err, (int)sizeof err);
    if (!h)
        return -1;
    soh_destroy(v->h);
    v->h = h;
    v->chip = soh_chip(h);
    v->out_rate = out_rate;
    v->sent_valid = 0;
    v->pitch_dirty = 0;                     /* (the driver leaves _snap_until_speech as it was) */
    v->fault = 0;
    return 0;
}

/* ---- _box_pitch, _box_settings (int() of a positive float = floor) ----------------------------------------------- */
static int box_pitch(int p)
{
    p = clamp100(p);
    return p <= 50 ? (int)(p * 3 / 50.0 + 0.5) : 3 + (int)((p - 50) * 6 / 50.0 + 0.5);   /* 50 -> 3 */
}

SO_API int sov_pitch_step(int pitch) { return box_pitch(pitch); }

static box_settings settings_of(const so_voice *v)
{
    box_settings s;
    s.rate = (int)(v->rate * 9 / 100.0 + 0.5);                 /* 50 -> 5, the default */
    s.pitch = box_pitch(v->pitch);
    s.tone = v->tone;
    s.word = v->join ? 0 : 1;                                  /* word delay 0 when joining */
    s.sentence = v->short_pauses ? 0 : 1;                      /* sentence delay 0 when shortening */
    return s;
}

/* ---- the text: _translit (translit.h), currencies, _clean, strip --------------------------------------------- */

/* _clean: Latin-1 text with no control characters (^E and ^X are the box's commands) */
static int clean(const unsigned *in, int n, unsigned char *out)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        unsigned c = in[i];
        const char *r;
        if (c < 32 || c == 127) { out[m++] = ' '; continue; }
        if (c < 256) { out[m++] = (unsigned char)c; continue; }
        switch (c) {
        case 0x2018: case 0x2019: r = "'"; break;
        case 0x201C: case 0x201D: r = "\""; break;
        case 0x2013: case 0x2014: r = "-"; break;
        case 0x2026: r = "..."; break;
        default: r = " "; break;
        }
        while (*r) out[m++] = (unsigned char)*r++;
    }
    return m;
}

/* str.strip() after _clean: the only whitespace left is the space, U+0085 and U+00A0 */
static int strip_space(unsigned char c) { return c == ' ' || c == 0x85 || c == 0xA0; }

/* the bytes box.say() gets for one text item: the text, then "\r"; malloc'd, the length in *len (0: nothing to say) */
static unsigned char *say_text(const char *utf8, int *len)
{
    size_t n = strlen(utf8);
    unsigned *t = (unsigned *)malloc((n + 1) * sizeof(unsigned)), *u;
    unsigned char *c;
    int k, m, a, b;
    *len = 0;
    if (!t) return NULL;
    k = nw_utf8(utf8, t);
    /* translit.h first: the letters the box's 7-bit alphabet lacks (its firmware drops Latin-1's), as base letters
       (or a lone one's words) */
    u = (unsigned *)malloc((size_t)tl_room(k) * sizeof(unsigned));
    if (!u) { free(t); return NULL; }
    k = tl_apply(t, k, tl_known_ascii, u);
    free(t);
    t = u;
    u = (unsigned *)malloc(((size_t)k * 16 + 64) * sizeof(unsigned));
    if (!u) { free(t); return NULL; }
    m = nw_currencies(t, k, u);                                /* "£2.63": the firmware reads only "$" */
    free(t);
    c = (unsigned char *)malloc((size_t)m * 3 + 2);            /* "…" -> "..." */
    if (!c) { free(u); return NULL; }
    m = clean(u, m, c);
    free(u);
    for (a = 0; a < m && strip_space(c[a]); a++) ;
    for (b = m; b > a && strip_space(c[b - 1]); b--) ;
    if (b > a) {
        memmove(c, c + a, (size_t)(b - a));
        c[b - a] = '\r';
        *len = b - a + 1;
    }
    return c;
}

SO_API int sov_say_bytes(const char *utf8, unsigned char *out, int cap)
{
    int n;
    unsigned char *s = say_text(utf8, &n);
    if (!s) return -1;
    if (out && n <= cap) memcpy(out, s, (size_t)n);
    free(s);
    return n;
}

/* ---- _speakJob / _speakItems ----------------------------------------------------------------------------------- */
static void say_str(so_voice *v, const char *s)
{
    soh_say(v->h, (const unsigned char *)s, (int)strlen(s));
}

static void say_pitch(so_voice *v, int p)                      /* box.chip.snap_pitch = True; box.say("\x05P%d") */
{
    char cmd[16];
    ssi263_set_snap_pitch(v->chip, 1);
    snprintf(cmd, sizeof cmd, "\x05P%d", p);
    say_str(v, cmd);
}

/* _speakJob's finally: the user's pitch again, only after the capital's audio exists */
static void restore_pitch(so_voice *v)
{
    if (v->cur_pitch != v->base_pitch) {
        say_pitch(v, v->base_pitch);
        v->cur_pitch = v->base_pitch;
        v->pitch_dirty = 1;
    }
}

/* a text's end: in a job the pitch stays as the sequence left it until sov_end (the driver's finally) */
static void finish(so_voice *v)
{
    v->active = 0;
    if (!v->job)
        restore_pitch(v);
}

/* the items: PitchCommand(offset) -- how NVDA marks a capital: an offset on the user's own 0-100 pitch */
static void pitch_item(so_voice *v, int pitch_offset)
{
    int want = box_pitch(v->pitch + pitch_offset);
    if (want != v->cur_pitch) {
        say_pitch(v, want);
        v->cur_pitch = want;
        v->pitch_dirty = 1;
    }
}

/* _speakJob's start: a faulted box rebooted, the settings sent when they changed, the lead trim armed */
static void begin(so_voice *v)
{
    box_settings s;
    if (v->active)
        sov_cancel(v);
    if (v->fault) {                         /* the driver's "speech failed; rebooting the emulated box" */
        char err[128];
        so_host *h = boot(v, v->out_rate, err, (int)sizeof err);
        if (h) {
            soh_destroy(v->h);
            v->h = h;
            v->chip = soh_chip(h);
            v->sent_valid = 0;
            v->fault = 0;
        }
    }
    s = settings_of(v);
    if (!v->sent_valid || memcmp(&s, &v->sent, sizeof s)) {
        char cmd[64];
        snprintf(cmd, sizeof cmd, "\x05R%d\x05P%d\x05T%c\x05W%d\x05I%d", s.rate, s.pitch, TONES[s.tone], s.word,
                 s.sentence);
        say_str(v, cmd);
        v->sent = s;
        v->sent_valid = 1;
    }
    v->base_pitch = v->cur_pitch = s.pitch;
    v->gain = v->volume / 100.0;
    v->lead = 1;                            /* nothing audible fed yet in this utterance */
}

/* a text item: t_start = box.chip.time; box.say(text + "\r") */
static void text_item(so_voice *v, const unsigned char *bytes, int n)
{
    v->t_start = ssi263_time(v->chip);
    soh_say(v->h, bytes, n);
    v->active = 1;
}

SO_API int sov_speak(so_voice *v, const char *utf8, int pitch_offset)
{
    unsigned char *text;
    int n;
    v->job = 0;
    begin(v);
    if (pitch_offset)
        pitch_item(v, pitch_offset);
    text = say_text(utf8, &n);
    if (!text || !n) {
        free(text);
        restore_pitch(v);
        return 0;
    }
    text_item(v, text, n);
    free(text);
    return 1;
}

/* ---- a job: the NVDA driver's whole speech sequence (so_voice.h) ------------------------------------------------ */
SO_API void sov_begin(so_voice *v)
{
    v->job = 0;
    begin(v);
    v->job = 1;
}

SO_API void sov_pitch(so_voice *v, int pitch_offset)
{
    pitch_item(v, pitch_offset);
}

SO_API int sov_text(so_voice *v, const unsigned char *bytes, int n)
{
    if (n <= 0)
        return 0;
    text_item(v, bytes, n);
    return 1;
}

SO_API void sov_end(so_voice *v)
{
    v->active = 0;
    v->job = 0;
    restore_pitch(v);                       /* _speakJob's finally */
}

SO_API void sov_flush(so_voice *v)
{
    soh_cancel(v->h, CANCEL_LIMIT);         /* _run: box.cancel() */
    if (v->pitch_dirty && v->sent_valid) {  /* _resend_pitch (none after a reboot) */
        char cmd[16];
        ssi263_set_snap_pitch(v->chip, 1);
        v->snap_until_speech = 1;
        snprintf(cmd, sizeof cmd, "\x05P%d", v->sent.pitch);
        say_str(v, cmd);
        v->cur_pitch = v->sent.pitch;
    }
}

/* one pass of _speakItems' loop: box.run(BLOCK_S), _trim_lead, pcm16, the snap check, busy() */
SO_API int sov_render(so_voice *v, const short **pcm, int *done)
{
    const double *y;
    int n, start = 0, count;
    *pcm = v->pcm;
    *done = 0;
    if (!v->active) { *done = 1; return 0; }
    n = soh_run(v->h, BLOCK_S, STEP_S, &y);
    if (n < 0) { finish(v); *done = 1; return 0; }
    if (v->lead) {                                             /* _trim_lead: the box's reading time is only delay */
        int k;
        start = n;
        for (k = 0; k < n; k++)
            if (y[k] > LEAD_THRESHOLD || y[k] < -LEAD_THRESHOLD) {
                start = k > LEAD_PREROLL ? k - LEAD_PREROLL : 0;
                v->lead = 0;
                break;
            }
    }
    count = n - start;
    if (count > v->pcm_cap) {
        short *q = (short *)realloc(v->pcm, (size_t)count * sizeof(short));
        if (!q) { finish(v); *done = 1; return 0; }
        v->pcm = q;
        v->pcm_cap = count;
    }
    *pcm = v->pcm;
    if (count > 0)
        ssi_pcm16(y + start, count, v->gain, v->pcm);
    if (v->snap_until_speech && soh_get_double(v->h, "last_speech") >= v->t_start) {
        /* the first phoneme is set up; a leftover snap would flatten a glide later */
        ssi263_set_snap_pitch(v->chip, 0);
        v->snap_until_speech = 0;
    }
    if (!soh_busy(v->h, SOH_QUIET, SOH_PATIENCE)) {
        v->pitch_dirty = 0;                                    /* the box has read everything sent so far */
        finish(v);
        *done = 1;
    } else if (soh_fault(v->h)) {                              /* never in the driver: a faulted V40 stays busy */
        v->fault = 1;
        v->active = 0;
        *done = 1;
    }
    return count > 0 ? count : 0;
}

/* cancel(), then _run's flush: box.cancel(), and _resend_pitch when a pitch command may have been dropped.  With no
   utterance in progress the driver's worker is idle and a cancel leaves the box alone: so does this. */
SO_API void sov_cancel(so_voice *v)
{
    if (!v->active)
        return;
    sov_end(v);                                                /* _speakJob's finally */
    sov_flush(v);
}
