/* st_voice.c -- see st_voice.h.  MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "st_voice.h"
#include "../accent_text.h"                /* at_say_text: the shared 7-bit text path (translit, currencies, clean-up) */
#include "../numwords.h"                   /* nw_normalise: the number words */

#define BLOCK_S 0.03
#define STEP_S 0.0005
#define QUIET 0.05                          /* smartalk.py's busy() */
#define PATIENCE 1.5
#define CANCEL_LIMIT 0.6                    /* smartalk.py's cancel() */
#define STALL_S 4.0                         /* speaking and silent this long: the box has stalled */
#define LEAD_THRESHOLD 0.003                /* the silence before the first phoneme left out (am_voice.c's) */
#define LEAD_PREROLL 220
#define DEF_STEP 5                          /* the TSR's defaults after power-up: rate 5, pitch 5, tone I, inflection, */
#define DEF_RATE 3                          /* ... except its rate: S_TALK.EXE 1.21 (STH_SHA256) starts at the speed
                                               ESC >3 gives, though TECHMAN.TXT says 5 -- after the boot its
                                               rate-dependent state is ESC >3's, and the two say a text with the same
                                               chip writes (Astra, Reply 251).  So NVDA's 50 is digit 3: the command that
                                               keeps the startup speed the voice was heard and approved at */
#define DEF_TONE 'I'
#define DEF_VOLUME 4                        /* volume 4 */
#define BOX_VOLUME 9                        /* the box's volume here: its top step, as loud as the Speak-Out, the
                                               Accents and the Sounding Board (within 0.2 dB); at 4 it was 4.2 dB under */

struct st_voice {
    st_host *h;
    unsigned char *exe;                                     /* S_TALK.EXE, kept to boot the box again after a fault */
    size_t n_exe;
    double out_rate, gain;
    int rate, pitch, tone, volume, inflection, numbers;
    int sent_rate, sent_pitch, sent_tone, sent_mono, sent_volume;   /* what the TSR has now */
    char **parts;
    int n_parts, next, active, lead, fault, n_commands;
    int snap_until_speech;                                  /* the utterance's first pitch lands at once (stv_speak) */
    double t_start;
    char cmd[32];
    int n_cmd;                                              /* the settings to send before the first part */
    short *pcm;
    int pcm_cap;
    /* index marks (st_voice.h) */
    struct { int id, seq; } *mk;                           /* the marks not yet reached: seq 0 at once, -1 at the
                                                              utterance's end, else the number of its Ctrl-F among
                                                              the utterance's */
    int n_mk, cap_mk, got06;                               /* got06: the Ctrl-Fs the TSR has counted of them */
    ssv_reach *reached;                                    /* what the last stv_render's block reached */
    int n_reached, cap_reached;
    int blk_ran, blk_start, blk_count;                     /* the block the last render made */
};

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

ST_API int stv_step(int value)
{
    value = clamp(value, 0, 100);
    return value <= 50 ? (int)(value * 5 / 50.0 + 0.5) : 5 + (int)((value - 50) * 4 / 50.0 + 0.5);
}

ST_API char stv_tone_letter(int tone) { return tone >= 0 && tone < 26 ? (char)('A' + tone) : DEF_TONE; }

/* NVDA's rate as the TSR's digit: 50 is its power-up speed, digit 3 (DEF_RATE), 0-50 onto 0-3 and 50-100 onto 3-9.
   ST_RATE_BREAK in the environment, the tests' control: stv_step's 50 = 5, as it was, so a rate moved back to 50 is
   faster than a fresh unit's */
ST_API int stv_rate_step(int value)
{
    value = clamp(value, 0, 100);
    if (getenv("ST_RATE_BREAK"))
        return stv_step(value);
    return value <= 50 ? (int)(value * DEF_RATE / 50.0 + 0.5)
                       : DEF_RATE + (int)((value - 50) * (9 - DEF_RATE) / 50.0 + 0.5);
}

/* ---- the parts (mb_voice.c's) --------------------------------------------------------------------------------------- */

static void free_parts(st_voice *v)
{
    int i;
    for (i = 0; i < v->n_parts; i++)
        free(v->parts[i]);
    free(v->parts);
    v->parts = NULL;
    v->n_parts = v->next = 0;
}

typedef struct { char **p; int n, cap; } list;

static int push(list *l, const char *s, int len)
{
    char *c;
    while (len > 0 && s[0] == ' ') { s++; len--; }
    while (len > 0 && s[len - 1] == ' ') len--;
    if (len <= 0)
        return 1;
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 8;
        char **q = (char **)realloc(l->p, (size_t)cap * sizeof *q);
        if (!q) return 0;
        l->p = q;
        l->cap = cap;
    }
    c = (char *)malloc((size_t)len + 1);
    if (!c) return 0;
    memcpy(c, s, (size_t)len);
    c[len] = 0;
    l->p[l->n++] = c;
    return 1;
}

/* where to cut s (n > max) at most max characters in: after the last of `marks` followed by a space, else -1 */
static int cut_after(const char *s, int n, int max, const char *marks)
{
    int i;
    for (i = (max < n ? max : n) - 1; i > 0; i--)
        if (strchr(marks, s[i]) && (i + 1 >= n || s[i + 1] == ' '))
            return i + 1;
    return -1;
}

/* s into parts of at most `max` characters: sentence ends, then clause marks, then a space, then anywhere */
static int split(list *l, const char *s, int n, int max)
{
    while (n > max) {
        int k = cut_after(s, n, max, ".!?");
        if (k < max / 3)
            k = cut_after(s, n, max, ",;:");
        if (k < max / 3) {
            int i;
            k = -1;
            for (i = max - 1; i > 0; i--)
                if (s[i] == ' ') { k = i; break; }
        }
        if (k <= 0)
            k = max;
        if (!push(l, s, k))
            return 0;
        s += k;
        n -= k;
    }
    return push(l, s, n);
}

/* ---- dollars and percent: the TSR's punctuation is off, so it says neither ---------------------------------------- */

static int digit(char c) { return c >= '0' && c <= '9'; }
static int alnum(char c) { return digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/* appends s[a..b) to out at m */
static int put(char *out, int m, const char *s, int a, int b)
{
    memcpy(out + m, s + a, (size_t)(b - a));
    return m + b - a;
}

static int put_str(char *out, int m, const char *s) { return put(out, m, s, 0, (int)strlen(s)); }

/* "$395.00" -> "395 dollars", "$2.63" -> "2 dollars 63 cents", "$.5" -> "50 cents", "$2.635" -> "2.635 dollars":
   numwords' currencies() for the pound (_currency_words), here for the dollar the Accents read themselves and the
   TSR drops; "10%" and "10 %" -> "10 percent".  A malloc'd string, NULL when out of memory. */
static char *dollars_percent(const char *s)
{
    int n = (int)strlen(s), i = 0, m = 0;
    char *out = (char *)malloc((size_t)n * 8 + 64);
    if (!out)
        return NULL;
    while (i < n) {
        if (s[i] == '$') {
            int p = i + 1, w0, w1, f0 = -1, f1 = -1;
            if (p < n && s[p] == ' ')
                p++;
            w0 = p;
            if (p < n && digit(s[p]))
                while (p < n && (digit(s[p]) || (s[p] == ',' && p + 1 < n && digit(s[p + 1]))))
                    p++;
            w1 = p;
            if (p + 1 < n && s[p] == '.' && digit(s[p + 1])) {
                f0 = ++p;
                while (p < n && digit(s[p]))
                    p++;
                f1 = p;
            }
            if (w1 > w0 || f0 >= 0) {
                int whole_zero = 1, k, cents = 0;
                for (k = w0; k < w1; k++)
                    if (digit(s[k]) && s[k] != '0')
                        whole_zero = 0;
                if (m > 0 && alnum(out[m - 1]))
                    out[m++] = ' ';
                if (f0 >= 0 && f1 - f0 > 2) {                 /* read as a number: "2.635 dollars" */
                    m = w1 > w0 ? put(out, m, s, w0, w1) : put_str(out, m, "0");
                    out[m++] = '.';
                    m = put(out, m, s, f0, f1);
                    m = put_str(out, m, " dollars");
                } else {
                    if (f0 >= 0)
                        cents = (s[f0] - '0') * 10 + (f1 - f0 > 1 ? s[f0 + 1] - '0' : 0);
                    if (w1 > w0 && (!whole_zero || !cents)) {
                        m = put(out, m, s, w0, w1);
                        m = put_str(out, m, w1 - w0 == 1 && s[w0] == '1' ? " dollar" : " dollars");
                    }
                    if (cents) {
                        char c[24];
                        if (w1 > w0 && (!whole_zero || !cents))
                            out[m++] = ' ';
                        snprintf(c, sizeof c, "%d %s", cents, cents == 1 ? "cent" : "cents");
                        m = put_str(out, m, c);
                    }
                }
                i = p;
                continue;
            }
        } else if (s[i] == '%' && i > 0 && (digit(s[i - 1]) || (s[i - 1] == ' ' && i > 1 && digit(s[i - 2])))) {
            if (out[m - 1] != ' ')
                out[m++] = ' ';
            m = put_str(out, m, "percent");
            i++;
            continue;
        }
        out[m++] = s[i++];
    }
    out[m] = 0;
    return out;
}

/* ---- contractions: the TSR ends a word at an apostrophe and spells what is left if it has no vowel ("don't" is "don"
   and the letter T, "it's" "it" and S, "I'm" "I" and M; ESC C 1 still spells a single letter, TECHMAN.TXT), a
   listener's report.  An apostrophe between two letters goes, so the word is one ("dont", "its"); the pronouns that
   end in a vowel would then be said short ("Im", "well"), so they are spelt as the TSR says them right (Tomi
   listened, 2026-10-25) ----------------------------------------------------------------------------------------- */

static const char *const CONTRACTIONS[][2] = {
    {"i'm", "ime"}, {"i'll", "ile"}, {"i'd", "ide"}, {"i've", "ive"},
    {"you'll", "yool"}, {"you'd", "yood"}, {"you've", "yoov"}, {"you're", "yoor"},
    {"we'll", "weel"}, {"we'd", "weed"}, {"we've", "weev"}, {"we're", "weer"},
    {"they'll", "thale"}, {"they'd", "thayd"}, {"they've", "thayv"}, {"they're", "there"},
    {"he'll", "heel"}, {"he'd", "heed"}, {"she'll", "sheel"}, {"she'd", "sheed"},
    {"who'll", "hool"}, {"who'd", "hooed"}, {"who's", "hooz"}, {"who've", "hoov"}};

static int letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

/* s[a..b), a word with an apostrophe inside, appended to out at m: its spelling from CONTRACTIONS in the word's case
   (all capitals, a capital first, or none), else the word without its apostrophes */
static int contraction(char *out, int m, const char *s, int a, int b)
{
    size_t k;
    int i, caps = 1;
    for (i = a; i < b; i++)
        if (letter(s[i]) && lower(s[i]) == s[i])
            caps = 0;
    for (k = 0; k < sizeof CONTRACTIONS / sizeof CONTRACTIONS[0]; k++) {
        const char *w = CONTRACTIONS[k][0], *r = CONTRACTIONS[k][1];
        if ((int)strlen(w) != b - a)
            continue;
        for (i = 0; i < b - a && lower(s[a + i]) == w[i]; i++)
            ;
        if (i < b - a)
            continue;
        for (i = 0; r[i]; i++)
            out[m++] = caps || (i == 0 && lower(s[a]) != s[a]) ? (char)(r[i] - 32) : r[i];
        return m;
    }
    for (i = a; i < b; i++)
        if (s[i] != '\'')
            out[m++] = s[i];
    return m;
}

/* the words with an apostrophe between two letters, as above; any other apostrophe ("rock 'n roll", "the dogs'")
   stays.  A malloc'd string, NULL when out of memory.  ST_TEXT_BREAK=1, the tests' control: the text as it is. */
static char *contractions(const char *s)
{
    int n = (int)strlen(s), i = 0, m = 0;
    char *out = (char *)malloc((size_t)n + 1);
    if (!out)
        return NULL;
    while (i < n) {
        int j = i, inner = 0;
        if (!letter(s[i]) || getenv("ST_TEXT_BREAK")) {
            out[m++] = s[i++];
            continue;
        }
        while (j < n && (letter(s[j]) || (s[j] == '\'' && j + 1 < n && letter(s[j + 1])))) {
            inner |= s[j] == '\'';
            j++;
        }
        m = inner ? contraction(out, m, s, i, j) : put(out, m, s, i, j);
        i = j;
    }
    out[m] = 0;
    return out;
}

/* the text the TSR is given for utf8: the shared path (no number words), the contractions, dollars and percent, then
   the number words */
static char *voice_text(const char *utf8, int numbers)
{
    char *a = at_say_text(utf8 ? utf8 : "", 0), *b, *c;
    if (!a)
        return NULL;
    c = contractions(a);
    free(a);
    if (!(a = c))
        return NULL;
    b = dollars_percent(a);
    free(a);
    if (!b || !numbers)
        return b;
    c = nw_normalise(b);
    free(b);
    return c;
}

/* the voice's text for utf8, as parts (a malloc'd array of malloc'd strings in *out); the count, -1 out of memory */
static int make_parts(const char *utf8, int numbers, char ***out)
{
    list l = {NULL, 0, 0};
    char *t = voice_text(utf8, numbers);
    int n;
    if (!t)
        return -1;
    n = (int)strlen(t);
    if (!split(&l, t, n, STV_PART)) {
        int i;
        for (i = 0; i < l.n; i++) free(l.p[i]);
        free(l.p);
        free(t);
        return -1;
    }
    free(t);
    *out = l.p;
    return l.n;
}

/* ---- index marks (st_voice.h) -------------------------------------------------------------------------------------- */
/* the tests' controls (nvda/tools/st_marks.py; an app never sets it): 1 reports every mark with the first block; 2
   gives the TSR a Ctrl-F for every one of the caller's marks; 3 leaves the marks after a cancel; 4 puts the Ctrl-F
   inside the marked word, after its first letter */
ST_API int stv_marks_break = 0;

static int count_words(const char *t)
{
    int n = 0, i = 0;
    while (t[i]) {
        while (t[i] == ' ') i++;
        if (!t[i]) break;
        n++;
        while (t[i] && t[i] != ' ') i++;
    }
    return n;
}

/* the k-th word (from 0) of the parts taken as one text: a copy, malloc'd; NULL past the end */
static char *nth_word(char **parts, int n_parts, int k)
{
    int p, i;
    for (p = 0; p < n_parts; p++)
        for (i = 0; parts[p][i]; ) {
            int a;
            while (parts[p][i] == ' ') i++;
            if (!parts[p][i]) break;
            a = i;
            while (parts[p][i] && parts[p][i] != ' ') i++;
            if (k-- == 0) {
                char *w = (char *)malloc((size_t)(i - a) + 1);
                if (w) { memcpy(w, parts[p] + a, (size_t)(i - a)); w[i - a] = 0; }
                return w;
            }
        }
    return NULL;
}

/* the word of the whole text a mark at byte `at` of the source stands before (0 to nw): the source up to the mark
   -- from the start of the run of text it stands in -- made the voice's text by itself, its words held against the
   whole's (bl_voice.c's way) */
static int mark_word(const char *utf8, int at, int numbers, char **parts, int n_parts, int nw)
{
    int len = (int)strlen(utf8), j = 0, pnw, i = 0;
    char *prefix, *pt;
    if (at <= 0) return 0;
    if (at > len) at = len;
    if (at < len)
        while (at > 0 && (unsigned char)utf8[at] > ' ' && (unsigned char)utf8[at - 1] > ' ') at--;
    while (at > 0 && at < len && ((unsigned char)utf8[at] & 0xC0) == 0x80) at--;
    prefix = (char *)malloc((size_t)at + 1);
    if (!prefix) return 0;
    memcpy(prefix, utf8, (size_t)at);
    prefix[at] = 0;
    pt = voice_text(prefix, numbers);
    free(prefix);
    if (!pt) return 0;
    pnw = count_words(pt);
    for (j = 0; j < pnw && j < nw; j++) {
        char *w = nth_word(parts, n_parts, j);
        int a, same;
        while (pt[i] == ' ') i++;
        a = i;
        while (pt[i] && pt[i] != ' ') i++;
        same = w && (int)strlen(w) == i - a && !memcmp(w, pt + a, (size_t)(i - a));
        free(w);
        if (!same) break;
    }
    free(pt);
    return j;
}

/* The parts with the TSR's index mark, Ctrl-F, for each of the caller's marks: directly after the word BEFORE the
   marked one, in whichever part that word is -- the TSR takes a Ctrl-F for a space, and "word<06> next" leaves its
   speech as it is where "word <06>next" can move a 4.3 ms pause at its two fastest rates (measured).  The parts
   themselves are the unmarked text's: no mark moves where the text is cut.  seqs as accent_text.h's at_say_marked.
   0 when out of memory (the parts are then as they were, every seq 0). */
static int mark_parts(const char *utf8, int numbers, char **parts, int n_parts, const ssv_mark *marks, int n_marks,
                      int *seqs, int *n06)
{
    int nw = 0, p, i, w, last = 0, count = 0, *mk;
    *n06 = 0;
    for (i = 0; i < n_marks; i++) seqs[i] = 0;
    for (p = 0; p < n_parts; p++) nw += count_words(parts[p]);
    mk = (int *)calloc((size_t)nw + 2, sizeof(int));           /* per word: the Ctrl-Fs for it, then their number */
    if (!mk) return 0;
    for (i = 0; i < n_marks; i++) {
        w = mark_word(utf8, marks[i].at, numbers, parts, n_parts, nw);
        if (w < last) w = last;                                /* in the caller's order, never back */
        last = w;
        seqs[i] = w;
        if (w > 0 && w < nw && (!mk[w] || stv_marks_break == 2)) mk[w]++;
    }
    for (p = 0, w = 0; p < n_parts; p++) {                     /* w: the number of the word a part's scan is at */
        const char *t = parts[p];
        int n = (int)strlen(t), m = 0, k;
        char *out = (char *)malloc((size_t)n + (size_t)n_marks + 1);
        if (!out) { free(mk); for (i = 0; i < n_marks; i++) seqs[i] = 0; return 0; }
        for (i = 0; i < n; ) {
            if (t[i] == ' ') { out[m++] = t[i++]; continue; }
            if (stv_marks_break == 4 && mk[w] > 0) {           /* the control: inside the marked word */
                out[m++] = t[i++];
                for (k = mk[w]; k > 0; k--) out[m++] = 0x06;
            }
            while (i < n && t[i] != ' ') out[m++] = t[i++];
            w++;                                               /* that word is written: the next one's marks follow it */
            if (w < nw && mk[w] > 0 && stv_marks_break != 4)
                for (k = mk[w]; k > 0; k--) out[m++] = 0x06;
        }
        out[m] = 0;
        free(parts[p]);
        parts[p] = out;
    }
    for (w = 1; w < nw; w++)
        if (mk[w]) { count += mk[w]; mk[w] = count; }
    for (i = 0; i < n_marks; i++)
        seqs[i] = seqs[i] <= 0 ? 0 : seqs[i] >= nw ? -1 : mk[seqs[i]];
    *n06 = count;
    free(mk);
    return 1;
}

ST_API int stv_parts_marked(const char *utf8, int numbers, const ssv_mark *marks, int n_marks, char *out, int cap)
{
    char **p = NULL;
    int n = make_parts(utf8, numbers, &p), i, len = 0, n06, *seqs;
    if (n < 0)
        return -1;
    seqs = (int *)malloc(sizeof(int) * (size_t)(n_marks > 0 ? n_marks : 1));
    if (seqs) mark_parts(utf8, numbers, p, n, marks, n_marks, seqs, &n06);
    free(seqs);
    for (i = 0; i < n; i++)
        len += (int)strlen(p[i]) + (i ? 1 : 0);
    if (out && len < cap) {
        out[0] = 0;
        for (i = 0; i < n; i++) {
            if (i) strcat(out, "|");
            strcat(out, p[i]);
        }
    }
    for (i = 0; i < n; i++) free(p[i]);
    free(p);
    return len;
}

ST_API int stv_parts(const char *utf8, int numbers, char *out, int cap)
{
    char **p = NULL;
    int n = make_parts(utf8, numbers, &p), i, len = 0;
    if (n < 0)
        return -1;
    for (i = 0; i < n; i++)
        len += (int)strlen(p[i]) + (i ? 1 : 0);
    if (out && len < cap) {
        out[0] = 0;
        for (i = 0; i < n; i++) {
            if (i) strcat(out, "|");
            strcat(out, p[i]);
        }
    }
    for (i = 0; i < n; i++) free(p[i]);
    free(p);
    return len;
}

/* ---- the voice ------------------------------------------------------------------------------------------------------ */

/* a box booted: S_TALK.EXE loaded, then ST_RUN's lines played out silently; NULL with the reason in err */
static st_host *boot(const unsigned char *exe, size_t n, double out_rate, char *err, int errlen)
{
    st_host *h = sth_create(exe, n, NULL, out_rate, err, errlen);
    if (!h)
        return NULL;
    sth_boot(h, STH_BOOT_LIMIT);
    if (sth_fault(h)) {
        if (err && errlen > 0) snprintf(err, (size_t)errlen, "S_TALK.EXE failed at boot: %s", sth_error(h));
        sth_destroy(h);
        return NULL;
    }
    return h;
}

/* what the TSR has after power-up: rate 3's speed (DEF_RATE), pitch 5, tone I, its inflection, volume 4 */
static void defaults_sent(st_voice *v)
{
    v->sent_rate = getenv("ST_RATE_BREAK") ? DEF_STEP : DEF_RATE;
    v->sent_pitch = DEF_STEP;
    v->sent_tone = DEF_TONE;
    v->sent_mono = 0;
    v->sent_volume = DEF_VOLUME;
}

ST_API st_voice *stv_create(const unsigned char *exe, size_t n, double out_rate, char *err, int errlen)
{
    st_voice *v = (st_voice *)calloc(1, sizeof *v);
    if (!v || !(v->exe = (unsigned char *)malloc(n ? n : 1))) {
        free(v);
        if (err && errlen > 0) snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    memcpy(v->exe, exe, n);
    v->n_exe = n;
    v->h = boot(exe, n, out_rate, err, errlen);
    if (!v->h) {
        free(v->exe);
        free(v);
        return NULL;
    }
    v->out_rate = out_rate;
    defaults_sent(v);
    stv_set(v, 50, 50, DEF_TONE - 'A', 100, 1, 1);
    return v;
}

/* after a fault: a new box from the kept S_TALK.EXE (am_voice.c's restart); 0 when that failed too */
static int restart(st_voice *v)
{
    st_host *h = boot(v->exe, v->n_exe, v->out_rate, NULL, 0);
    if (!h)
        return 0;
    sth_destroy(v->h);
    v->h = h;
    defaults_sent(v);
    v->fault = 0;
    return 1;
}

ST_API void stv_destroy(st_voice *v)
{
    if (!v)
        return;
    free_parts(v);
    sth_destroy(v->h);
    free(v->exe);
    free(v->pcm);
    free(v->mk);
    free(v->reached);
    free(v);
}

ST_API st_host *stv_host(st_voice *v) { return v->h; }

/* rate and pitch clamped to 0-100, volume to 0-200 (above 100 louder, ssi_pcm16 clipping at full scale, as sov_set),
   a tone outside A-Z ignored */
ST_API void stv_set(st_voice *v, int rate, int pitch, int tone, int volume, int inflection, int numbers)
{
    v->rate = clamp(rate, 0, 100);
    v->pitch = clamp(pitch, 0, 100);
    if (tone >= 0 && tone < 26)
        v->tone = tone;
    v->volume = clamp(volume, 0, 200);
    v->inflection = inflection != 0;
    v->numbers = numbers != 0;
}

/* the settings that differ from what the TSR has, as its commands, into v->cmd */
static void commands(st_voice *v, int pitch_offset)
{
    int rate = stv_rate_step(v->rate), pitch = stv_step(v->pitch + pitch_offset), mono = !v->inflection;
    char tone = stv_tone_letter(v->tone);
    v->n_cmd = 0;
    if (rate != v->sent_rate) {
        v->n_cmd += snprintf(v->cmd + v->n_cmd, sizeof v->cmd - (size_t)v->n_cmd, "\x1b>%d", rate);
        v->sent_rate = rate;
    }
    if (pitch != v->sent_pitch) {
        v->n_cmd += snprintf(v->cmd + v->n_cmd, sizeof v->cmd - (size_t)v->n_cmd, "\x1bP%d", pitch);
        v->sent_pitch = pitch;
    }
    if (tone != v->sent_tone) {
        v->n_cmd += snprintf(v->cmd + v->n_cmd, sizeof v->cmd - (size_t)v->n_cmd, "\x1b{%c", tone);
        v->sent_tone = tone;
    }
    if (mono != v->sent_mono) {
        v->n_cmd += snprintf(v->cmd + v->n_cmd, sizeof v->cmd - (size_t)v->n_cmd, "\x1bm%d", mono);
        v->sent_mono = mono;
    }
    if (BOX_VOLUME != v->sent_volume) {             /* once after each boot: the box's own volume */
        v->n_cmd += snprintf(v->cmd + v->n_cmd, sizeof v->cmd - (size_t)v->n_cmd, "\x1b<%d", BOX_VOLUME);
        v->sent_volume = BOX_VOLUME;
    }
}

/* the next part to the TSR, with the commands before the first, ended by its carriage return; 0 when none is left */
static int say_next(st_voice *v)
{
    char *s, *buf;
    int len, k = 0;
    if (v->next >= v->n_parts)
        return 0;
    s = v->parts[v->next++];
    len = (int)strlen(s);
    buf = (char *)malloc((size_t)(v->n_cmd + len + 2));
    if (!buf) {
        v->fault = 1;
        return 0;
    }
    if (v->n_cmd) {
        memcpy(buf, v->cmd, (size_t)v->n_cmd);
        k = v->n_cmd;
        v->n_cmd = 0;
    }
    memcpy(buf + k, s, (size_t)len);
    k += len;
    buf[k++] = '\r';
    sth_say(v->h, (const unsigned char *)buf, k);
    free(buf);
    if (sth_fault(v->h))
        v->fault = 1;
    return 1;
}

ST_API int stv_speak(st_voice *v, const char *utf8, int pitch_offset)
{
    return stv_speak_marked(v, utf8, pitch_offset, NULL, 0);
}

ST_API int stv_speak_marked(st_voice *v, const char *utf8, int pitch_offset, const ssv_mark *marks, int n_marks)
{
    char **p = NULL;
    int n, i, n06 = 0, *seqs = NULL;
    if (stv_fault(v) && !restart(v))
        return 0;
    if (v->active || sth_busy(v->h, QUIET, 0.0))
        stv_cancel(v);
    free_parts(v);
    v->n_mk = v->n_reached = v->got06 = 0;
    n = make_parts(utf8, v->numbers, &p);
    if (!marks || n_marks <= 0)
        n_marks = 0;
    if (n_marks) {                                    /* (out of memory: spoken without marks) */
        seqs = (int *)malloc(sizeof(int) * (size_t)n_marks);
        if (n_marks > v->cap_mk) {
            void *q = realloc(v->mk, sizeof *v->mk * (size_t)n_marks);
            if (q) { v->mk = q; v->cap_mk = n_marks; }
        }
        if (!seqs || n_marks > v->cap_mk) n_marks = 0;
    }
    if (n_marks) {
        for (i = 0; i < n_marks; i++) seqs[i] = 0;
        if (n > 0) mark_parts(utf8, v->numbers, p, n, marks, n_marks, seqs, &n06);
        for (i = 0; i < n_marks; i++) { v->mk[i].id = marks[i].id; v->mk[i].seq = seqs[i]; }
        v->n_mk = n_marks;
    }
    free(seqs);
    if (n <= 0) {                                     /* nothing to say: its marks with the next render */
        free(p);
        return 0;
    }
    v->parts = p;
    v->n_parts = n;
    v->gain = v->volume / 100.0;
    commands(v, pitch_offset);
    v->n_commands = v->n_cmd;
    v->t_start = sth_get_double(v->h, "time");
    v->active = 1;
    v->lead = 1;
    /* host feature, not chip behaviour (so_voice.c's and am_voice.c's snap, here on every utterance): the TSR writes
       its pitch targets in the slowest glide field (R1's low bits 0) and in steps of several counts, so the chip slid
       into each utterance -- a capital's raised pitch, a new pitch setting or just the next phrase heard as a slow
       swoop.  Two changes of target start an utterance: the PA that starts speech (written as the carriage return is
       taken, inside say_next) carries the LAST utterance's pitch base, from wherever that one ended; the first
       phoneme, in the first render, carries this one's (a capital's, a new setting's).  Each lands at once: the snap
       armed before the text goes in, and again after.  The glides inside the utterance stay the TSR's own (released
       once its first phoneme is loaded, stv_render) */
    ssi263_set_snap_pitch(sth_chip(v->h), 1);
    v->snap_until_speech = 1;
    say_next(v);
    ssi263_set_snap_pitch(sth_chip(v->h), 1);
    if (v->fault) {
        v->active = 0;
        return 0;
    }
    return 1;
}

static int render_block(st_voice *v, const short **pcm, int *done)
{
    const double *y;
    int n, start = 0, count;
    *pcm = v->pcm;
    *done = 0;
    v->blk_ran = v->blk_start = v->blk_count = 0;
    if (!v->active || v->fault) {
        v->active = 0;
        *done = 1;
        return 0;
    }
    if (v->next < v->n_parts && sth_queued(v->h) < STV_LOW)
        say_next(v);
    n = sth_run(v->h, BLOCK_S, STEP_S, &y);
    if (n < 0 || v->fault) {
        v->fault = 1;
        v->active = 0;
        *done = 1;
        return 0;
    }
    if (v->lead) {                                    /* the silence before the first phoneme (am_voice.c) */
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
        if (!q) {
            v->fault = 1;
            v->active = 0;
            *done = 1;
            return 0;
        }
        v->pcm = q;
        v->pcm_cap = count;
    }
    *pcm = v->pcm;
    if (count > 0)
        ssi_pcm16(y + start, count, v->gain, v->pcm);
    v->blk_ran = 1;
    v->blk_start = start;
    v->blk_count = count > 0 ? count : 0;
    if (v->snap_until_speech && sth_get_double(v->h, "last_speech") >= v->t_start) {
        /* the first phoneme is set up; a leftover snap would flatten a glide later (so_voice.c) */
        ssi263_set_snap_pitch(sth_chip(v->h), 0);
        v->snap_until_speech = 0;
    }
    if (v->next >= v->n_parts && !sth_busy(v->h, QUIET, PATIENCE)) {
        v->active = 0;                                /* the parts stay until the next speak: stv_get_int counts them */
        *done = 1;
    } else {
        double last = sth_get_double(v->h, "last_speech");
        double now = sth_get_double(v->h, "time");
        if (sth_speaking(v->h) && now - (last > v->t_start ? last : v->t_start) > STALL_S) {
            v->fault = 1;                             /* speaking and silent: the box has stalled */
            v->active = 0;
            *done = 1;
        }
    }
    return count > 0 ? count : 0;
}

/* What the block just made has reached: the TSR counts a Ctrl-F as it writes what follows it; the k-th counted is the
   k-th sent.  With `done` the rest come with the block's last sample. */
static void report_marks(st_voice *v, int returned, int done)
{
    const sth_reply *rp = NULL;
    int nr = v->blk_ran ? sth_replies(v->h, &rp) : 0, i, j = 0, kept = 0, last = 0;
    v->n_reached = 0;
    if (!v->n_mk) return;
    if (v->n_mk > v->cap_reached) {
        ssv_reach *r = (ssv_reach *)realloc(v->reached, sizeof(ssv_reach) * (size_t)v->n_mk);
        if (!r) { v->n_mk = 0; return; }
        v->reached = r;
        v->cap_reached = v->n_mk;
    }
    for (i = 0; i < v->n_mk; i++) {
        int seq = v->mk[i].seq, s = 0, exact = 0;
        if (stv_marks_break == 1 || seq == 0) { s = 0; exact = seq == 0; }
        else if (seq > 0 && seq <= v->got06 + nr) {          /* counted in this block: by its place among them */
            j = seq - v->got06 - 1;
            s = (j >= 0 ? rp[j].sample : 0) - v->blk_start;
            exact = 1;
        } else if (done) { s = returned; exact = seq == -1; }
        else {                                               /* not yet */
            v->mk[kept++] = v->mk[i];
            continue;
        }
        if (s < last) s = last;
        if (s > returned) s = returned;
        last = s;
        v->reached[v->n_reached].id = v->mk[i].id;
        v->reached[v->n_reached].sample = s;
        v->reached[v->n_reached].exact = exact;
        v->n_reached++;
    }
    v->got06 += nr;
    v->n_mk = kept;
}

ST_API int stv_render(st_voice *v, const short **pcm, int *done)
{
    int n = render_block(v, pcm, done);
    if (v->fault) { v->n_mk = v->n_reached = 0; return n; }
    report_marks(v, n, *done);
    return n;
}

ST_API int stv_reached(st_voice *v, const ssv_reach **out)
{
    *out = v->reached;
    return v->n_reached;
}

ST_API int stv_marks_exact(const st_voice *v)
{
    (void)v;
    return 1;
}

ST_API void stv_cancel(st_voice *v)
{
    if (stv_marks_break != 3)
        v->n_mk = v->n_reached = 0;                          /* no mark of a cancelled text is reported */
    free_parts(v);
    v->active = 0;
    v->n_cmd = 0;
    if (v->snap_until_speech) {
        ssi263_set_snap_pitch(sth_chip(v->h), 0);
        v->snap_until_speech = 0;
    }
    if (!v->fault) {
        sth_cancel(v->h, CANCEL_LIMIT);
        if (sth_fault(v->h))
            v->fault = 1;
    }
}

ST_API int stv_fault(const st_voice *v) { return v->fault || sth_fault(v->h); }

ST_API int stv_get_int(const st_voice *v, const char *name)
{
    if (!strcmp(name, "parts")) return v->n_parts;
    if (!strcmp(name, "sent")) return v->next;
    if (!strcmp(name, "commands")) return v->n_commands;
    return -1;
}
