/*
 * sd_ssi263 -- a speech-dispatcher output module: the NVDA add-ons' voices through the emulated SSI-263, one process,
 * no Python -- the Blazie Braille Lite 2000 (English, Spanish), the Aicom Accent SA, and the Accent-mini and GW Micro
 * Speak-Out when built with them.  Each runs its own firmware on its own emulated processor; sd_voices.h is the table
 * of voices and their engines, and this file speaks the protocol.
 *
 * The module protocol and its audio (705 blocks, HDLC-escaped, played and flushed by the server) follow
 * speech-dispatcher's module_process.c; the reply lines are TGSpeechBox's sd_tgsb's, proven on speech-dispatcher 0.11.
 * Synthesis is synchronous: stdin is polled for STOP between blocks, and a STOP cancels the voice's unit (its unspoken
 * text dropped) so the next message starts clean.  SET synthesis_voice picks a voice by name (LIST VOICES); SET
 * language picks one of that language unless the current voice already speaks it.
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
 * Test hooks: SD_SSI263_TEST_NO_CANCEL=1 leaves the unit uncancelled on STOP; sd_voices.c's drop a key on its way to
 * the voice -- the harness's controls must fail.
 */
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

/* STOP (or CANCEL / PAUSE) waiting on stdin?  Reads what is there into the buffer, leaves it for readline_sd. */
static int poll_stop(void)
{
    fd_set f;
    struct timeval tv = {0, 0};
    FD_ZERO(&f);
    FD_SET(STDIN_FILENO, &f);
    if (select(STDIN_FILENO + 1, &f, NULL, NULL, &tv) > 0) {
        char buf[4096];
        ssize_t k = read(STDIN_FILENO, buf, sizeof buf);
        if (k <= 0) { r_eof = 1; return 1; }
        rappend(buf, (size_t)k);
    }
    return rbuf && (strstr(rbuf, "STOP") || strstr(rbuf, "CANCEL") || strstr(rbuf, "PAUSE"));
}

static void send_line(const char *s)
{
    fputs(s, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

/* 705: 16-bit mono little-endian, the payload HDLC-escaped (0x7D and '\n' -> 0x7D, byte ^ 0x20) */
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

/* ---- text: SSML tags dropped, XML's five entities decoded -------------------------------------------------------- */
static void strip_ssml(char *s)
{
    static const char *ent[][2] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}};
    char *r = s, *w = s;
    int tag = 0, k;
    while (*r) {
        if (*r == '<') { tag = 1; r++; continue; }
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

/* SET language: a voice of that language, unless the current one already speaks it (an Accent user's "en" keeps the
   Accent); the first one there otherwise.  Compared on the language's first two letters, as speech-dispatcher's
   clients send "en", "en-US" or "es". */
static void choose_language(const char *lang)
{
    int i;
    if (strlen(lang) < 2) return;
    if (!strncmp(sdv_language(cur_voice), lang, 2)) return;
    for (i = 0; i < sdv_count(); i++)
        if (!strncmp(sdv_language(i), lang, 2) && sdv_available(i, &conf)) { cur_voice = i; return; }
}

/* ---- SPEAK ------------------------------------------------------------------------------------------------------- */
static void speak(char *text)
{
    char err[256];
    const short *pcm;
    int done = 0, stopped = 0, v = cur_voice;
    strip_ssml(text);
    if (sdv_load(v, &conf, err, sizeof err) != 0) {
        fprintf(stderr, "sd_ssi263: %s: %s\n", sdv_name(v), err);
        send_line("301 ERROR CANT SPEAK");
        return;
    }
    if (poll_stop()) {                               /* already stopped (key repeat): nothing to say */
        send_line("200 OK SPEAKING");
        send_line("701 BEGIN");
        send_line("703 STOP");
        return;
    }
    send_line("200 OK SPEAKING");
    send_line("701 BEGIN");
    if (sdv_speak(v, &conf, to100(ssip_rate), to100(ssip_pitch), to100(ssip_volume), text, err, sizeof err) < 0)
        done = 1;
    while (!done) {
        int n = sdv_render(v, &pcm, &done);
        if (n > 0)
            send_audio(pcm, n, conf.sample_rate);
        if (!done && poll_stop()) { stopped = 1; break; }
    }
    if (stopped) {
        if (!getenv("SD_SSI263_TEST_NO_CANCEL"))
            sdv_cancel(v);
        send_line("703 STOP");
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
                speak(text);
            }
            free(text);
        } else if (!strcmp(cmd, "STOP") || !strcmp(cmd, "CANCEL") || !strcmp(cmd, "PAUSE")) {
            /* only between messages here (during one, speak() catches it): nothing is playing */
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
        } else if (!strcmp(cmd, "AUDIO") || !strcmp(cmd, "LOGLEVEL") || !strcmp(cmd, "DEBUG")) {
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
            for (i = 0; i < sdv_count(); i++)
                if (sdv_available(i, &conf))
                    printf("200-%s\t%s\tMALE1\n", sdv_name(i), sdv_language(i));
            send_line("249 OK VOICES LISTED");
        } else if (!strcmp(cmd, "QUIT")) {
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
