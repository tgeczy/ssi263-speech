/* ssi_serve.c -- sapi/ssi_serve.py's pipe protocol over the native voices, in C: the SAPI engine's voices as a
 * program, for sapi/test_native.py to hold to ssi_serve.py byte for byte over the same wire -- as outspoken-nvda's
 * osp_serve.c is held to its osp_serve.py.  The SAPI DLL itself runs the voices in-process (ssi263_sapi.cpp); this
 * host loads the same ssi263speech.dll the same way (ssi_native.c), from its own folder, and maps every setting with
 * the same code.  A test and build tool only (--list writes the installer's voices.txt): it is never installed.
 *
 * Requests arrive on stdin, framed, little-endian:
 *
 *     'OSP4' | seq | rate | pitch | volume | namelen | textlen | name | text
 *
 * and a cancel is 'OSPC' | seq, acting only when its seq is the one rendering (one that arrives first answers its
 * request empty).  The response is 'OSPR' | status, then PCM in chunks -- u32 frame count, then frames*2 bytes of
 * 16-bit mono at the --rate -- and a zero frame count to finish.  rate/pitch/volume are the drivers' 0-100.
 *
 *   ssi263_serve.exe --serve [--firmware <dir>] [--rate 11025|22050|44100] [--inflection 1|0]
 *                    [--whine off|hiss|whine] [--accent-inflection 0..100] [--run-ahead 1|0] [--bl-numbers 1|0]
 *   ssi263_serve.exe --list [--firmware <dir>]        one voice per line: "id<TAB>name<TAB>language", UTF-8
 *   ssi263_serve.exe --files [--firmware <dir>]       the firmware those voices need, one path per line (the stage)
 *   ssi263_serve.exe --user-voices                    the voices no package carries, each with its file (voices.txt)
 *
 * The firmware folder defaults to ..\firmware beside this program's folder (the installed layout: {app}\x64\, and
 * {app}\firmware).  Test hooks, for test_native.py's must-fail controls only: SSI263_SERVE_BREAK=setting drops the
 * dialog's settings (inflection, whine, Accent intonation, run ahead) on their way to the voices; =voice swaps each
 * voice for its sibling (English and Spanish, Accent-mini and SA); =numbers turns the drivers' number words off;
 * =bl-numbers ignores --bl-numbers (the Braille Lite's number words stay at their default, on).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#include "ssi_native.h"

#define REQ 0x4F535034u            /* 'OSP4' */
#define RSP 0x4F535052u            /* 'OSPR' */
#define CANCEL 0x4F535043u         /* 'OSPC' */
#define DEADLINE_MS 120000u        /* ssi_serve.py's: an utterance still rendering after two minutes is cancelled */

typedef struct req {
    unsigned seq;
    int rate, pitch, volume;
    char *name, *text;
    struct req *next;
} req;

static HANDLE in_h, out_h;
static CRITICAL_SECTION lock;
static HANDLE ready;               /* an event: a request queued, or the input ended */
static req *head, *tail;
static int input_ended;
static volatile LONG current;      /* the seq rendering, 0 for none */
static volatile LONG cancel_now;
#define MAXCANCELLED 256
static unsigned cancelled[MAXCANCELLED];
static int ncancelled;

static int exact_read(void *p, DWORD n)
{
    unsigned char *b = (unsigned char *)p;
    DWORD done = 0, got;
    while (done < n) {
        if (!ReadFile(in_h, b + done, n - done, &got, NULL) || !got) return 0;
        done += got;
    }
    return 1;
}

static void write_all(const void *p, DWORD n)
{
    const unsigned char *b = (const unsigned char *)p;
    DWORD done = 0, put;
    while (done < n) {
        if (!WriteFile(out_h, b + done, n - done, &put, NULL) || !put) ExitProcess(0);   /* the reader has gone */
        done += put;
    }
}

static void write_u32(unsigned v) { write_all(&v, 4); }

static int was_cancelled(unsigned seq, int drop)
{
    int k;
    for (k = 0; k < ncancelled; k++)
        if (cancelled[k] == seq) {
            if (drop) cancelled[k] = cancelled[--ncancelled];
            return 1;
        }
    return 0;
}

static DWORD WINAPI reader(LPVOID unused)
{
    (void)unused;
    for (;;) {
        unsigned magic, seq, nv, nt;
        int f[3];
        req *r;
        if (!exact_read(&magic, 4)) break;
        if (magic == CANCEL) {
            if (!exact_read(&seq, 4)) break;
            EnterCriticalSection(&lock);
            if (ncancelled < MAXCANCELLED) cancelled[ncancelled++] = seq;
            if ((LONG)seq == current) InterlockedExchange(&cancel_now, 1);
            LeaveCriticalSection(&lock);
            continue;
        }
        if (magic != REQ || !exact_read(&seq, 4) || !exact_read(f, 12) || !exact_read(&nv, 4) || !exact_read(&nt, 4))
            break;
        r = (req *)calloc(1, sizeof *r);
        if (!r || !(r->name = (char *)malloc(nv + 1)) || !(r->text = (char *)malloc(nt + 1))) break;
        if (!exact_read(r->name, nv) || !exact_read(r->text, nt)) break;
        r->name[nv] = 0;
        r->text[nt] = 0;
        r->seq = seq;
        r->rate = f[0];
        r->pitch = f[1];
        r->volume = f[2];
        EnterCriticalSection(&lock);
        if (tail) tail->next = r; else head = r;
        tail = r;
        LeaveCriticalSection(&lock);
        SetEvent(ready);
    }
    EnterCriticalSection(&lock);
    input_ended = 1;
    LeaveCriticalSection(&lock);
    SetEvent(ready);
    return 0;
}

static req *next_request(void)
{
    for (;;) {
        req *r = NULL;
        int end;
        EnterCriticalSection(&lock);
        if (head) {
            r = head;
            head = r->next;
            if (!head) tail = NULL;
        }
        end = input_ended;
        LeaveCriticalSection(&lock);
        if (r || end) return r;
        WaitForSingleObject(ready, 500);
    }
}

static void free_request(req *r)
{
    free(r->name);
    free(r->text);
    free(r);
}

/* SSI263_SERVE_BREAK=voice: the sibling of voice i (a control) */
static int sibling(const ssi_api *api, int i, const char *fwdir)
{
    static const char *pairs[][2] = {{"blazie:blazie", "blazie:blazie_es"}, {"accentmini:mini", "accentmini:sa"}};
    int k, j;
    for (k = 0; k < 2; k++)
        for (j = 0; j < 2; j++)
            if (!strcmp(api->info(i)->id, pairs[k][j])) {
                int o = api->find(pairs[k][1 - j]);
                return o >= 0 && api->available(o, fwdir) ? o : i;
            }
    return i;
}

static void usage(void)
{
    fputs("ssi263_serve.exe --serve|--list [--firmware <dir>] [--rate 11025|22050|44100] [--inflection 1|0] "
          "[--whine off|hiss|whine] [--accent-inflection 0..100] [--run-ahead 1|0] [--bl-numbers 1|0]\n", stderr);
}

int wmain(int argc, wchar_t **argv)
{
    ssi_api api;
    ssi_options o;
    ssv_boot boot;
    ssv_bank *bank;
    wchar_t exe[MAX_PATH], fwdir_w[MAX_PATH * 2], *slash;
    char fwdir[MAX_PATH * 2], err[256];
    const char *brk = getenv("SSI263_SERVE_BREAK");
    int k, list = 0, serve = 0;
    req *r;

    ssi_options_defaults(&o);
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    if ((slash = wcsrchr(exe, L'\\')) != NULL) *slash = 0;
    _snwprintf(fwdir_w, sizeof fwdir_w / sizeof fwdir_w[0], L"%ls\\..\\firmware", exe);
    for (k = 1; k < argc; k++) {
        const wchar_t *a = argv[k], *v = k + 1 < argc ? argv[k + 1] : L"";
        if (!wcscmp(a, L"--serve")) serve = 1;
        else if (!wcscmp(a, L"--list")) list = 1;
        else if (!wcscmp(a, L"--files")) list = 2;
        else if (!wcscmp(a, L"--user-voices")) list = 3;
        else if (!wcscmp(a, L"--firmware")) { wcsncpy(fwdir_w, v, MAX_PATH * 2 - 1); k++; }
        else if (!wcscmp(a, L"--inflection")) { o.inflection = wcscmp(v, L"0") && wcscmp(v, L"off") && wcscmp(v, L"false"); k++; }
        else if (!wcscmp(a, L"--whine")) {
            if (!wcscmp(v, L"off")) o.whine = 0; else if (!wcscmp(v, L"hiss")) o.whine = 1; else if (!wcscmp(v, L"whine")) o.whine = 2;
            k++;
        }
        else if (!wcscmp(a, L"--rate")) {
            int x = _wtoi(v);
            if (x == 11025 || x == 22050 || x == 44100) o.sample_rate = x;
            k++;
        }
        else if (!wcscmp(a, L"--accent-inflection")) {
            if (*v >= L'0' && *v <= L'9') { int x = _wtoi(v); o.accent_inflection = x < 0 ? 0 : x > 100 ? 100 : x; }
            k++;
        }
        else if (!wcscmp(a, L"--run-ahead")) { o.run_ahead = !wcscmp(v, L"1") || !wcscmp(v, L"on") || !wcscmp(v, L"true"); k++; }
        else if (!wcscmp(a, L"--bl-numbers")) {
            if (!(brk && !strcmp(brk, "bl-numbers")))         /* the control: the value ignored */
                o.numbers = wcscmp(v, L"0") && wcscmp(v, L"off") && wcscmp(v, L"false");
            k++;
        }
    }
    if (!list && !serve) { usage(); return 2; }
    if (!ssi_load(&api, exe, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 1; }
    if (!ssi_lib_path(fwdir_w, fwdir, sizeof fwdir)) { fputs("the firmware folder's name cannot be opened\n", stderr); return 1; }
    out_h = GetStdHandle(STD_OUTPUT_HANDLE);
    in_h = GetStdHandle(STD_INPUT_HANDLE);
    if (list == 3) {               /* the voices no package carries: "id<TAB>name<TAB>language<TAB>data:<file>", each
                                      registered only where a user's data folder holds its file (register.ps1) */
        for (k = 0; k < api.count(); k++)
            if (SSV_USER_DATA(api.info(k))) {
                const ssv_info *in = api.info(k);
                char line[512];
                int n = snprintf(line, sizeof line, "%s\t%s\t%s\tdata:%s\n", in->id, in->name, in->lang, in->files[0]);
                write_all(line, (DWORD)n);
            }
        return 0;
    }
    if (list) {
        for (k = 0; k < api.count(); k++)
            if (api.available(k, fwdir)) {
                const ssv_info *in = api.info(k);
                char line[512];
                int n = snprintf(line, sizeof line, "%s\t%s\t%s\n", in->id, in->name, in->lang), j;
                if (list == 1) write_all(line, (DWORD)n);
                /* a voice no package carries (voices.h SSV_USER_DATA): never in the stage, whatever the folder holds;
                   SSI263_SERVE_BREAK=userdata, build.ps1's control, lists it as the others */
                if (list == 2 && SSV_USER_DATA(in) && !(brk && !strcmp(brk, "userdata")))
                    continue;
                for (j = 0; list == 2 && in->files[j]; j++) {
                    n = snprintf(line, sizeof line, "%s\n", in->files[j]);
                    write_all(line, (DWORD)n);
                }
            }
        return 0;
    }
    if (brk && !strcmp(brk, "setting")) {                     /* the control: the dialog's settings dropped */
        int rate = o.sample_rate;
        ssi_options_defaults(&o);
        o.sample_rate = rate;
    }
    ssi_boot(&o, &boot);
    bank = api.bank_new(fwdir);
    if (!bank) { fputs("out of memory\n", stderr); return 1; }
    api.bank_boot(bank, &boot);
    InitializeCriticalSection(&lock);
    ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    CloseHandle(CreateThread(NULL, 0, reader, NULL, 0, NULL));

    while ((r = next_request()) != NULL) {
        int i, status = 0, said = 0;
        ssv_voice *v = NULL;
        ssv_settings s;
        EnterCriticalSection(&lock);
        if (was_cancelled(r->seq, 1)) {
            LeaveCriticalSection(&lock);
            write_u32(RSP); write_u32(0); write_u32(0);
            free_request(r);
            continue;
        }
        InterlockedExchange(&cancel_now, 0);
        InterlockedExchange(&current, (LONG)r->seq);
        LeaveCriticalSection(&lock);
        i = ssi_voice(&api, r->name, fwdir);
        if (i >= 0 && brk && !strcmp(brk, "voice"))
            i = sibling(&api, i, fwdir);
        if (i < 0 || !(v = api.bank_voice(bank, i, err, sizeof err))) {
            if (i >= 0) fprintf(stderr, "%s\n", err);
            status = 1;
        }
        write_u32(RSP); write_u32((unsigned)status);
        if (status) {
            InterlockedExchange(&current, 0);
            free_request(r);
            continue;
        }
        ssi_settings(&api, i, &o, r->rate, r->pitch, r->volume, &s);
        if (brk && !strcmp(brk, "numbers"))                  /* the control: the drivers' number words off */
            s.numbers = 0;
        said = api.speak(v, &s, r->text, 0);
        if (said > 0) {
            DWORD t0 = GetTickCount();
            int done = 0;
            while (!done && !cancel_now) {
                const short *pcm;
                int n = api.render(v, &pcm, &done);
                if (n > 0 && !cancel_now) {
                    write_u32((unsigned)n);
                    write_all(pcm, (DWORD)n * 2);
                }
                if (GetTickCount() - t0 > DEADLINE_MS) break;
            }
            if (!done) api.cancel(v);
        }
        EnterCriticalSection(&lock);
        InterlockedExchange(&current, 0);
        was_cancelled(r->seq, 1);
        LeaveCriticalSection(&lock);
        write_u32(0);
        free_request(r);
    }
    api.bank_free(bank);
    return 0;
}
