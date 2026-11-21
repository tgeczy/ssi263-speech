/*
 * sd_ssi263 -- a speech-dispatcher output module: the NVDA add-ons' voices through the emulated SSI-263, one process,
 * no Python -- the Blazie Braille Lite 2000 (English, Spanish), the Aicom Accent-PC, and the Accent-mini and GW Micro
 * Speak-Out when built with them.  Each runs its own firmware on its own emulated processor; sd_voices.h is the table
 * of voices and their engines, and this file speaks the protocol.
 *
 * The module protocol and its audio (705 blocks, HDLC-escaped, played and flushed by the server) follow
 * speech-dispatcher's module_process.c; the reply lines are TGSpeechBox's sd_tgsb's, proven on speech-dispatcher 0.11.
 * Synthesis is synchronous: stdin is polled for STOP and PAUSE between blocks, and either cancels the voice's unit (its
 * unspoken text dropped) so the next message starts clean; both are answered 703, on which the server speaks a paused
 * message again from its start on RESUME (stop_event below says why not 704).  SET synthesis_voice picks a voice by
 * name (LIST VOICES); SET language picks one of that language unless the current voice already speaks it.
 *
 * Config (argv[1], speech-dispatcher's module config; then this user's own file, whose keys win:
 * $XDG_CONFIG_HOME/ssi263-speech/sd_ssi263.conf, else ~/.config/ssi263-speech/sd_ssi263.conf; every key optional):
 *   SSI263DataDir "/usr/local/share/ssi263-speech"   the firmware: BL2ENG.BNS + bl2_2003_warm.state [+ BL2SPA.BNS +
 *                              bl2spa_fresh.state], aicom-accent-sa/, aicom-accent-mini/, gw-micro-speakout/
 *   SSI263SampleRate 22050     11025 | 22050 | 44100, as the add-ons (any other value: 22050); every voice
 *   the Braille Lite's:
 *   SSI263Inflection 1         the unit's voice inflection (status menu)
 *   SSI263Whine "off"          off | hiss | whine: the unit's idle sound
 *   SSI263Tone 7               0-26, the unit's tone (factory 7)
 *   SSI263ShortPauses 1        sentences packed onto one line from the second on (the NVDA driver's default)
 *   SSI263RunAhead 0           1: the NVDA driver's "Run the unit ahead" (EXPERIMENTAL, off by default; with short
 *                              pauses on only, as there): blv_set_run_ahead
 *   SSI263LineLift 0           1: the NVDA driver's "Lift line starts (as note-taking mode)" (off by default): a line
 *                              spoken after a cancel or a pause is lifted as the unit lifts a line moved to
 *                              (blv_set_line_lift)
 *   SSI263BrailleLiteNumbers 1 the NVDA driver's custom number processing (its default: on): numbers read as words,
 *                              Spain's way for the Spanish unit ("1.234.567", "3,5"); 0: the firmware reads them
 *   the Accents' (the SA and the mini):
 *   SSI263AccentInflection 100 0-100, the add-on's inflection slider (100 full intonation ... 0 monotone; five steps)
 *   SSI263AccentNumbers 1      the add-on's custom number processing
 *   SSI263AccentMiniVoice 5    0-9, the Accent-mini's voice characteristic (the add-on's variant)
 *   the Speak-Out's:
 *   SSI263SpeakOutTone I       A-Z (or 0-25), the box's tone (the add-on's variant; I by default)
 *   SSI263SpeakOutJoin 1       the add-on's "Join phrases"
 *   SSI263SpeakOutShortPauses 1  the add-on's "Shorten pauses between sentences"
 * Rate, pitch and volume come from SSIP for every voice.  SSI263_DATADIR overrides the data folder.  The firmware is
 * not part of this program: it is loaded from there.  `sd_ssi263 --voices` lists the voices built in.
 *
 * Index marks: the message's <mark name="..."/> are reported (700) when speech reaches them, each between the audio
 * before it and the audio after it: where the unit's own index mark says on a Braille Lite whose release was measured
 * for them, estimated on the other voices (../../csrc/marks.h, docs/index-marks.md).
 *
 * Test hooks: SD_SSI263_TEST_NO_MARKS=1 gives the voice no marks and reports none; SD_SSI263_TEST_MARKS_LATE=1 sends
 * each block's audio before its marks; SD_SSI263_TEST_NO_CANCEL=1 leaves the unit uncancelled on STOP and PAUSE; SD_SSI263_TEST_OLD_REPLIES=1
 * answers DEBUG and QUIT as up to 0.9.1 (old_replies below); SD_SSI263_TEST_PAUSE_704=1 answers a PAUSE with 704
 * (stop_event below); sd_voices.c's drop a key on its way to the voice -- the harness's controls must fail.
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "sd_voices.h"

/* ---- stdin: one raw buffer for lines and the STOP poll (never stdio on stdin) ----------------------------------- */
static char *rbuf;
static size_t rlen, rcap;
static int r_eof;

static void rappend(const char *p, size_t n)
{
    if (rlen + n + 1 > rcap) {
        size_t cap = rcap ? rcap : 8192;
        while (cap < rlen + n + 1) cap *= 2;
        rbuf = (char *)realloc(rbuf, cap);
        if (!rbuf) exit(1);
        rcap = cap;
    }
    memcpy(rbuf + rlen, p, n);
    rlen += n;
    rbuf[rlen] = 0;
}

/* the next line (without \r\n) into a malloc'd string; NULL at EOF */
static char *readline_sd(void)
{
    for (;;) {
        char *nl = rbuf ? (char *)memchr(rbuf, '\n', rlen) : NULL;
        if (nl) {
            size_t n = (size_t)(nl - rbuf);
            char *line = (char *)malloc(n + 1);
            memcpy(line, rbuf, n);
            line[n] = 0;
            if (n && line[n - 1] == '\r') line[n - 1] = 0;
            memmove(rbuf, nl + 1, rlen - n - 1);
            rlen -= n + 1;
            rbuf[rlen] = 0;
            return line;
        }
        if (r_eof) return NULL;
        {
            char buf[4096];
            ssize_t k = read(STDIN_FILENO, buf, sizeof buf);
            if (k <= 0) { r_eof = 1; continue; }
            rappend(buf, (size_t)k);
        }
    }
}

/* A control (test_sd_ssi263.py, tools/linux_tests.sh): the replies as they were up to 0.9.1 -- STOP and PAUSE looked
   for anywhere in what waits, "DEBUG ON <file>" an unknown command, QUIT unanswered, every voice listed as "MALE1". */
static int old_replies(void) { return getenv("SD_SSI263_TEST_OLD_REPLIES") != NULL; }

/* What waits on stdin for the message being spoken: nothing, a STOP (or CANCEL, or the end of stdin), or a PAUSE alone.
   Reads what is there into the buffer and leaves it for readline_sd.  Whole lines only: the words were looked for
   anywhere in the buffer, so a "DEBUG ON" whose file name held one of them stopped the speech. */
enum { GO = 0, STOPPED, PAUSED };
static int poll_stop(void)
{
    fd_set f;
    struct timeval tv = {0, 0};
    const char *p, *nl;
    int paused = 0;
    FD_ZERO(&f);
    FD_SET(STDIN_FILENO, &f);
    if (select(STDIN_FILENO + 1, &f, NULL, NULL, &tv) > 0) {
        char buf[4096];
        ssize_t k = read(STDIN_FILENO, buf, sizeof buf);
        if (k <= 0) { r_eof = 1; return STOPPED; }
        rappend(buf, (size_t)k);
    }
    if (!rbuf) return GO;
    if (old_replies())
        return strstr(rbuf, "STOP") || strstr(rbuf, "CANCEL") || strstr(rbuf, "PAUSE") ? STOPPED : GO;
    for (p = rbuf; (nl = (const char *)memchr(p, '\n', (size_t)(rbuf + rlen - p))) != NULL; p = nl + 1) {
        size_t n = (size_t)(nl - p);
        if (n && p[n - 1] == '\r') n--;
        if ((n == 4 && !memcmp(p, "STOP", 4)) || (n == 6 && !memcmp(p, "CANCEL", 6))) return STOPPED;
        if (n == 5 && !memcmp(p, "PAUSE", 5)) paused = 1;
    }
    return paused ? PAUSED : GO;
}

static void send_line(const char *s)
{
    fputs(s, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

/* 705: 16-bit mono little-endian, the payload HDLC-escaped (0x7D and '\n' -> 0x7D, byte ^ 0x20) */
/* 700: speech has reached the mark of that name.  Sent between the audio before it and the audio after it: the
   server holds it in its queue with them and reports it when its playback gets there. */
static void send_mark(const char *name)
{
    printf("700-%s\n700 INDEX MARK\n", name);
    fflush(stdout);
}

static void send_audio(const short *pcm, int n, int rate)
{
    const unsigned char *p = (const unsigned char *)pcm, *end = p + (size_t)n * 2;
    printf("705-bits=16\n705-num_channels=1\n705-sample_rate=%d\n705-num_samples=%d\n705-big_endian=0\n705-AUDIO",
           rate, n);
    fputc(0, stdout);
    for (; p < end; p++) {
        if (*p == 0x7D || *p == '\n') { fputc(0x7D, stdout); fputc(*p ^ 0x20, stdout); }
        else fputc(*p, stdout);
    }
    fputs("\n705 AUDIO\n", stdout);
    fflush(stdout);
}

/* ---- text: SSML tags dropped, XML's five entities decoded; its index marks kept -------------------------------- */
/* The message's marks: <mark name="..."/>, the server's own (after each sentence, for PAUSE) and a client's (Orca
   puts one before every word).  Each is kept with the place it stood at in the text as it is spoken, and reported
   when speech reaches that place (speak() below; ../../csrc/marks.h). */
static ssv_mark *marks;                 /* .at: bytes of the stripped text before it; .id: its index in names */
static char **mark_names;
static int n_marks, cap_marks;

static void forget_marks(void)
{
    int k;
    for (k = 0; k < n_marks; k++) free(mark_names[k]);
    n_marks = 0;
}

/* r at a '<': when the tag is a mark with a name, the mark is kept, standing `at` bytes into the spoken text */
static void keep_mark(const char *r, int at)
{
    const char *end = strchr(r, '>'), *p, *q;
    char quote, *name;
    if (!end || strncmp(r, "<mark", 5) || (r[5] != ' ' && r[5] != '\t' && r[5] != '/')) return;
    for (p = r + 5; p < end; p++)
        if (!strncmp(p, "name", 4) && (p[-1] == ' ' || p[-1] == '\t')) break;
    if (p >= end) return;
    p += 4;
    while (p < end && (*p == ' ' || *p == '=')) p++;
    if (p >= end || (*p != '"' && *p != '\'')) return;
    quote = *p++;
    for (q = p; q < end && *q != quote; q++) ;
    if (q >= end) return;
    if (n_marks == cap_marks) {
        int cap = cap_marks ? cap_marks * 2 : 64;
        ssv_mark *m = (ssv_mark *)realloc(marks, sizeof *m * (size_t)cap);
        char **nm = m ? (char **)realloc(mark_names, sizeof *nm * (size_t)cap) : NULL;
        if (m) marks = m;
        if (!nm) return;
        mark_names = nm;
        cap_marks = cap;
    }
    name = (char *)malloc((size_t)(q - p) + 1);
    if (!name) return;
    memcpy(name, p, (size_t)(q - p));
    name[q - p] = 0;
    mark_names[n_marks] = name;
    marks[n_marks].at = at;
    marks[n_marks].id = n_marks;
    n_marks++;
}

static void strip_ssml(char *s)
{
    static const char *ent[][2] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}};
    char *r = s, *w = s;
    int tag = 0, k;
    forget_marks();
    while (*r) {
        if (*r == '<') { keep_mark(r, (int)(w - s)); tag = 1; r++; continue; }
        if (*r == '>' && tag) { tag = 0; r++; continue; }
        if (tag) { r++; continue; }
        if (*r == '&') {
            for (k = 0; k < 5; k++) {
                size_t n = strlen(ent[k][0]);
                if (!strncmp(r, ent[k][0], n)) { *w++ = ent[k][1][0]; r += n; break; }
            }
            if (k < 5) continue;
        }
        *w++ = *r++;
    }
    *w = 0;
}

/* ---- config ------------------------------------------------------------------------------------------------------ */
static sd_settings conf;

static void read_config(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[1200], key[128], val[1024];
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        if (sscanf(line, "%127s \"%1023[^\"]\"", key, val) != 2 && sscanf(line, "%127s %1023s", key, val) != 2)
            continue;
        if (!strcmp(key, "SSI263DataDir")) snprintf(conf.datadir, sizeof conf.datadir, "%s", val);
        else if (!strcmp(key, "SSI263SampleRate")) conf.sample_rate = atoi(val);
        else if (!strcmp(key, "SSI263Inflection")) conf.inflection = atoi(val) != 0;
        else if (!strcmp(key, "SSI263Tone")) conf.tone = atoi(val);
        else if (!strcmp(key, "SSI263ShortPauses")) conf.short_pauses = atoi(val) != 0;
        else if (!strcmp(key, "SSI263RunAhead")) conf.run_ahead = atoi(val) != 0;
        else if (!strcmp(key, "SSI263LineLift")) conf.line_lift = atoi(val) != 0;
        else if (!strcmp(key, "SSI263BrailleLiteNumbers")) conf.numbers = atoi(val) != 0;
        else if (!strcmp(key, "SSI263Whine")) conf.whine = !strcmp(val, "hiss") ? 1 : !strcmp(val, "whine") ? 2 : 0;
        else if (!strcmp(key, "SSI263AccentInflection")) conf.accent_inflection = atoi(val);
        else if (!strcmp(key, "SSI263AccentNumbers")) conf.accent_numbers = atoi(val) != 0;
        else if (!strcmp(key, "SSI263AccentMiniVoice")) conf.accent_voice = atoi(val);
        else if (!strcmp(key, "SSI263SpeakOutTone")) {
            char c = val[0];               /* a letter A-Z, as the box names its tones, or 0-25 */
            conf.speakout_tone = c >= 'A' && c <= 'Z' && !val[1] ? c - 'A' : c >= 'a' && c <= 'z' && !val[1] ? c - 'a'
                                 : atoi(val);
        }
        else if (!strcmp(key, "SSI263SpeakOutJoin")) conf.speakout_join = atoi(val) != 0;
        else if (!strcmp(key, "SSI263SpeakOutShortPauses")) conf.speakout_short_pauses = atoi(val) != 0;
    }
    fclose(f);
}

/* This user's own settings, read after the module config so their keys win (as TGSpeechBox's sd_tgsb): editable
 * without root, and kept when the voice is reinstalled. */
static void read_user_config(void)
{
    char p[1200];
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (xdg && *xdg)
        snprintf(p, sizeof p, "%s/ssi263-speech/sd_ssi263.conf", xdg);
    else if (home && *home)
        snprintf(p, sizeof p, "%s/.config/ssi263-speech/sd_ssi263.conf", home);
    else
        return;
    read_config(p);
}

static int clamp(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

static void check_config(void)
{
    if (conf.sample_rate != 11025 && conf.sample_rate != 22050 && conf.sample_rate != 44100) {
        fprintf(stderr, "sd_ssi263: SSI263SampleRate %d is not one of 11025, 22050, 44100: using 22050\n",
                conf.sample_rate);
        conf.sample_rate = 22050;
    }
    conf.accent_inflection = clamp(conf.accent_inflection, 0, 100);
    conf.accent_voice = clamp(conf.accent_voice, 0, 9);
    conf.speakout_tone = clamp(conf.speakout_tone, 0, 25);
}

static void find_datadir(void)
{
    const char *env = getenv("SSI263_DATADIR");
    if (env && *env) { snprintf(conf.datadir, sizeof conf.datadir, "%s", env); return; }
    if (*conf.datadir) return;
    snprintf(conf.datadir, sizeof conf.datadir, "/usr/local/share/ssi263-speech");
    if (access("/usr/local/share/ssi263-speech/BL2ENG.BNS", R_OK) != 0 &&
        access("/usr/share/ssi263-speech/BL2ENG.BNS", R_OK) == 0)
        snprintf(conf.datadir, sizeof conf.datadir, "/usr/share/ssi263-speech");
}

/* ---- the voices (sd_voices.h): the first one there at INIT, the others on first use ------------------------------- */
static int cur_voice = 0;
static int ssip_rate = 0, ssip_pitch = 0, ssip_volume = 100;

static int to100(int ssip)                           /* SSIP -100..100 -> NVDA's 0..100 (0 -> 50) */
{
    int v = (ssip + 100) / 2;
    return v < 0 ? 0 : v > 100 ? 100 : v;
}

/* How well a voice's tag answers the language asked for: 3 the same tag (ar-AE, pt-BR), 2 the language's own with
   no region (pt for pt-PT, en for en-US), 1 another region of the language, 0 another language.  Without case,
   "_" as "-": clients send "en", "en-US", "pt_BR". */
static int lang_match(const char *tag, const char *lang)
{
    char a[16], b[16];
    size_t k, pa, pb;
    for (k = 0; k + 1 < sizeof a && tag[k]; k++) a[k] = tag[k] == '_' ? '-' : (char)tolower((unsigned char)tag[k]);
    a[k] = 0;
    for (k = 0; k + 1 < sizeof b && lang[k]; k++) b[k] = lang[k] == '_' ? '-' : (char)tolower((unsigned char)lang[k]);
    b[k] = 0;
    pa = strcspn(a, "-");
    pb = strcspn(b, "-");
    if (pa != pb || strncmp(a, b, pa)) return 0;
    return !strcmp(a, b) ? 3 : !a[pa] ? 2 : 1;
}

/* SET language: the voice that answers it best (lang_match), the first such in the table; the current voice is
   kept when it answers as well (an Accent user's "en" or "en-US" keeps the Accent).  It compared the first two
   letters and took the first voice: ar-AE got the plain Arabic unit, pt and pt-PT the Brazilian one (#48;
   SD_SSI263_TEST_LANG_TWO=1, a control: as it was). */
static void choose_language(const char *lang)
{
    int i, best = 0, at = -1;
    if (strlen(lang) < 2) return;
    if (getenv("SD_SSI263_TEST_LANG_TWO")) {
        if (!strncmp(sdv_language(cur_voice), lang, 2)) return;
        for (i = 0; i < sdv_count(); i++)
            if (!strncmp(sdv_language(i), lang, 2) && sdv_available(i, &conf)) { cur_voice = i; return; }
        return;
    }
    for (i = 0; i < sdv_count(); i++) {
        int m = lang_match(sdv_language(i), lang);
        if (m > best && sdv_available(i, &conf)) { best = m; at = i; }
    }
    if (at >= 0 && lang_match(sdv_language(cur_voice), lang) < best)
        cur_voice = at;
}

/* What ends a message that a STOP or a PAUSE cut short: 703 for both.  The protocol has 704 for a pause, and
   speech-dispatcher's own modules send it; they also report index marks (700), which this module did not when this
   was measured (it does now, speak() below: whether 704 can be answered with them is not measured yet).  On
   704 the server waits for the playback to reach a mark before it calls the message paused, so with none it never
   does, and RESUME speaks nothing (measured on speech-dispatcher 0.11.1 with the server playing the audio:
   test_real_server.sh).  On 703 it stops, keeps the message it had put on its paused list, and on RESUME speaks it
   again from the start.  (SD_SSI263_TEST_PAUSE_704=1, a control: the 704 that 3b6ea48 answered for two days.) */
static const char *stop_event(int how)
{
    return how == PAUSED && getenv("SD_SSI263_TEST_PAUSE_704") ? "704 PAUSE" : "703 STOP";
}

/* ---- SPEAK ------------------------------------------------------------------------------------------------------- */
/* ssml: the text is SSML (SPEAK); CHAR and KEY come as the bare character or key name, a "<" or "&" among them
   (speech-dispatcher 0.11.1, seen through a module that logged its input), and were stripped as SSML: a "<" was
   taken for a tag and spoken as nothing (#47; SD_SSI263_TEST_CHAR_SSML=1, a control: stripped, as they were) */
static void speak(char *text, int ssml)
{
    char err[256];
    const short *pcm;
    int done = 0, stopped = 0, v = cur_voice;
    if (ssml || getenv("SD_SSI263_TEST_CHAR_SSML"))
        strip_ssml(text);
    else
        forget_marks();                              /* no marks in it; none left from before */
    if (sdv_load(v, &conf, err, sizeof err) != 0) {
        fprintf(stderr, "sd_ssi263: %s: %s\n", sdv_name(v), err);
        send_line("301 ERROR CANT SPEAK");
        return;
    }
    stopped = poll_stop();
    if (stopped) {                                   /* already stopped (key repeat): nothing to say */
        send_line("200 OK SPEAKING");
        send_line("701 BEGIN");
        send_line(stop_event(stopped));
        return;
    }
    send_line("200 OK SPEAKING");
    send_line("701 BEGIN");
    if (sdv_speak_marked(v, &conf, to100(ssip_rate), to100(ssip_pitch), to100(ssip_volume), text,
                         getenv("SD_SSI263_TEST_NO_MARKS") ? NULL : marks, n_marks, err, sizeof err) < 0)
        done = 1;
    while (!done) {
        const ssv_reach *r;
        int n = sdv_render(v, &pcm, &done), k = sdv_reached(v, &r), j, from = 0;
        /* each mark at its sample: the block's audio up to it, the mark, then the rest (a test's control sends
           the block whole first) */
        if (getenv("SD_SSI263_TEST_MARKS_LATE") && n > 0) { send_audio(pcm, n, conf.sample_rate); from = n; }
        for (j = 0; j < k; j++) {
            int at = r[j].sample < from ? from : r[j].sample > n ? n : r[j].sample;
            if (at > from) { send_audio(pcm + from, at - from, conf.sample_rate); from = at; }
            if (r[j].id >= 0 && r[j].id < n_marks) send_mark(mark_names[r[j].id]);
        }
        if (n > from)
            send_audio(pcm + from, n - from, conf.sample_rate);
        if (!done && (stopped = poll_stop()) != GO) break;
    }
    if (stopped) {
        if (!getenv("SD_SSI263_TEST_NO_CANCEL"))
            sdv_cancel(v);
        send_line(stop_event(stopped));
    } else {
        send_line("702 END");
    }
}

static char *read_text(void)
{
    char *text = (char *)calloc(1, 1), *line;
    size_t n = 0;
    while ((line = readline_sd()) != NULL) {
        const char *s = line;
        size_t k;
        if (!strcmp(line, ".")) { free(line); break; }
        if (s[0] == '.') s++;                        /* a leading dot is escaped as two */
        k = strlen(s);
        text = (char *)realloc(text, n + k + 2);
        if (n) text[n++] = ' ';
        memcpy(text + n, s, k);
        n += k;
        text[n] = 0;
        free(line);
    }
    return text;
}

static void set_param(char *key, const char *val)
{
    char *k;
    for (k = key; *k; k++) *k = (char)(*k >= 'A' && *k <= 'Z' ? *k + 32 : *k);
    if (!strcmp(key, "rate")) ssip_rate = atoi(val);
    else if (!strcmp(key, "pitch")) ssip_pitch = atoi(val);
    else if (!strcmp(key, "volume")) ssip_volume = atoi(val);
    else if (!strcmp(key, "synthesis_voice")) {     /* "NULL" or a name we do not have: no change */
        int i = sdv_find(val);
        if (i >= 0 && sdv_available(i, &conf)) cur_voice = i;
    } else if (!strcmp(key, "language")) {
        choose_language(val);
    }
}

int main(int argc, char **argv)
{
    char *cmd, err[256];
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    signal(SIGPIPE, SIG_IGN);
    sdv_defaults(&conf);
    if (argc > 1 && !strcmp(argv[1], "--voices")) {  /* the voices built in, for tools/package_linux.sh */
        int i;
        for (i = 0; i < sdv_count(); i++) printf("%s\t%s\t%s\n", sdv_engine(i), sdv_language(i), sdv_name(i));
        return 0;
    }
    if (argc > 1) read_config(argv[1]);
    read_user_config();
    check_config();
    find_datadir();
    cmd = readline_sd();
    if (!cmd || strcmp(cmd, "INIT")) return 1;
    free(cmd);
    for (cur_voice = 0; cur_voice < sdv_count() && !sdv_available(cur_voice, &conf); cur_voice++)
        ;
    if (cur_voice == sdv_count() || sdv_load(cur_voice, &conf, err, sizeof err) != 0) {
        char msg[1400];
        if (cur_voice == sdv_count())
            snprintf(msg, sizeof msg, "399-no voice's files are in the data folder %s", conf.datadir);
        else
            snprintf(msg, sizeof msg, "399-%s (data folder %s)", err, conf.datadir);
        send_line(msg);
        send_line("399 ERR CANT INIT MODULE");
        return 1;
    }
    send_line("299-the Braille Lite 2000, the Accents and the Speak-Out through an emulated SSI-263");
    send_line("299 OK LOADED SUCCESSFULLY");
    while ((cmd = readline_sd()) != NULL) {
        if (!strcmp(cmd, "SPEAK") || !strcmp(cmd, "CHAR") || !strcmp(cmd, "KEY") || !strcmp(cmd, "SOUND_ICON")) {
            int is_key = !strcmp(cmd, "KEY"), icon = !strcmp(cmd, "SOUND_ICON");
            char *text;
            send_line("202 OK RECEIVING MESSAGE");
            text = read_text();
            if (is_key) {
                char *c;
                for (c = text; *c; c++) if (*c == '_') *c = ' ';
            }
            if (icon || !*text) {                    /* no sound icons: an empty message still completes */
                send_line("200 OK SPEAKING");
                send_line("701 BEGIN");
                send_line("702 END");
            } else {
                speak(text, !strcmp(cmd, "SPEAK"));
            }
            free(text);
        } else if (!strcmp(cmd, "STOP") || !strcmp(cmd, "CANCEL") || !strcmp(cmd, "PAUSE")) {
            /* between messages (during one, speak() catches it): the unit is done, but the listener interrupted --
               the last message still playing, or a key pressed after it -- so the next is spoken as after a
               cancel (its line-start lift), as the NVDA driver's cancel() arms it when idle.  It was dropped, and
               the line moved to came out unlifted (#46; SD_SSI263_TEST_IDLE_STOP=drop, a control: dropped) */
            const char *b = getenv("SD_SSI263_TEST_IDLE_STOP");
            if (!b || strcmp(b, "drop"))
                sdv_interrupted(cur_voice);
        } else if (!strcmp(cmd, "SET")) {
            char *line;
            send_line("202 OK RECEIVING MESSAGE");
            while ((line = readline_sd()) != NULL) {
                char *eq;
                if (!strcmp(line, ".")) { free(line); break; }
                eq = strchr(line, '=');
                if (eq) { *eq = 0; set_param(line, eq + 1); }
                free(line);
            }
            send_line("203 OK SETTINGS RECEIVED");
        } else if (!strncmp(cmd, "DEBUG", 5) && !old_replies()) {
            /* "DEBUG ON <file>" or "DEBUG OFF", on one line (speech-dispatcher's output_send_debug), answered as
               its own modules answer (module_process.c).  This module keeps no log of its own -- what it has to say
               goes to stderr, which the server logs -- so there is nothing to switch. */
            char word[16] = "", on[8] = "";
            sscanf(cmd, "%15s %7s", word, on);
            if (!strcmp(word, "DEBUG") && !strcmp(on, "ON")) send_line("200 OK DEBUGGING ON");
            else if (!strcmp(word, "DEBUG") && !strcmp(on, "OFF")) send_line("200 OK DEBUGGING OFF");
            else send_line("302 ERROR BAD SYNTAX");
        } else if (!strcmp(cmd, "AUDIO") || !strcmp(cmd, "LOGLEVEL")) {
            char *line;
            send_line("202 OK RECEIVING MESSAGE");
            while ((line = readline_sd()) != NULL) {
                int end = !strcmp(line, ".");
                free(line);
                if (end) break;
            }
            send_line("203 OK SETTINGS RECEIVED");
        } else if (!strcmp(cmd, "LIST VOICES")) {
            int i;
            /* name, language, variant, the variant empty.  Every voice carried "MALE1" there: one of SSIP's generic
               voice types, and no variant of anything.  By PR 29's report a newer Orca then showed no voice names in
               its person list; with the column empty speech-dispatcher's clients read "no variant" and list the voices
               by name (test_real_server.sh).  (old_replies(), a control: the MALE1 it was.) */
            for (i = 0; i < sdv_count(); i++)
                if (sdv_available(i, &conf))
                    printf("200-%s\t%s\t%s\n", sdv_name(i), sdv_language(i), old_replies() ? "MALE1" : "");
            send_line("249 OK VOICES LISTED");
        } else if (!strcmp(cmd, "QUIT")) {
            if (!old_replies())                      /* the server waits for it (output_close's SEND_CMD) */
                send_line("210 OK QUIT");
            free(cmd);
            break;
        } else {
            send_line("300 ERR UNKNOWN COMMAND");
        }
        free(cmd);
    }
    sdv_destroy_all();
    return 0;
}
