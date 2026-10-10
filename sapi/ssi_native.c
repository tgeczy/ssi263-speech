/* ssi_native.c -- see ssi_native.h. */
#include <stdio.h>
#include <string.h>

#include "ssi_native.h"

int ssi_load(ssi_api *api, const wchar_t *dir, char *err, int errlen)
{
    wchar_t path[MAX_PATH + 32];
    HMODULE h;
    FARPROC p;
    memset(api, 0, sizeof *api);
    if (wcslen(dir) > MAX_PATH) { snprintf(err, errlen, "the folder's name is too long"); return 0; }
    wcscpy(path, dir);
    wcscat(path, L"\\ssi263speech.dll");
    /* the DLL's own folder first for anything it imports (it imports only the system's) */
    h = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) { snprintf(err, errlen, "could not load ssi263speech.dll (error %lu)", GetLastError()); return 0; }
#define GET(field, name) \
    if (!(p = GetProcAddress(h, name))) { \
        snprintf(err, errlen, "ssi263speech.dll has no %s", name); FreeLibrary(h); memset(api, 0, sizeof *api); return 0; } \
    memcpy(&api->field, &p, sizeof p);
    GET(count, "ssv_count")
    GET(info, "ssv_voice_info")
    GET(find, "ssv_find")
    GET(available, "ssv_available")
    GET(boot_defaults, "ssv_boot_defaults")
    GET(defaults, "ssv_defaults")
    GET(bank_new, "ssv_bank_new")
    GET(bank_free, "ssv_bank_free")
    GET(bank_boot, "ssv_bank_boot")
    GET(bank_voice, "ssv_bank_voice")
    GET(speak, "ssv_speak")
    GET(render, "ssv_render")
    GET(cancel, "ssv_cancel")
#undef GET
    api->dll = h;
    return 1;
}

void ssi_options_defaults(ssi_options *o)
{
    o->sample_rate = 22050;
    o->inflection = 1;
    o->whine = 0;
    o->accent_inflection = 100;
    o->run_ahead = 0;
    o->line_lift = 0;
    o->numbers = 1;                                          /* blazie.py: numberWords, defaultVal=True */
}

static int clamp(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

void ssi_boot(const ssi_options *o, ssv_boot *b)
{
    b->sample_rate = o->sample_rate;                         /* d._set_sampleRate(OPTIONS["rate"]), every driver */
    b->inflection = o->inflection != 0;                      /* blazie: d._set_voiceInflection */
    b->whine = o->whine >= 0 && o->whine <= 2 ? o->whine : 0;   /* blazie: d._set_whine */
}

void ssi_settings(const ssi_api *api, int i, const ssi_options *o, int rate, int pitch, int volume, ssv_settings *s)
{
    api->defaults(i, s);
    s->inflection = clamp(o->accent_inflection, 0, 100);     /* accentmini: d._set_inflection */
    s->run_ahead = o->run_ahead != 0;                        /* blazie: d._set_runAhead */
    s->line_lift = o->line_lift != 0;                        /* blazie: d._set_lineLift */
    if (api->info(i)->engine == SSV_BLAZIE)                  /* blazie: d._set_numberWords; the Accents' and the
                                                                Mockingboard's stay theirs (ssv_defaults: on) */
        s->numbers = o->numbers != 0;
    s->rate = clamp(rate, 0, 100);                           /* d._set_rate(max(0, min(100, rate))) */
    s->pitch = clamp(pitch, 0, 100);
    s->volume = clamp(volume, 0, 100);
}

int ssi_voice(const ssi_api *api, const char *id, const char *fwdir)
{
    int i = api->find(id), n = api->count(), k;
    size_t m;
    const char *colon = strchr(id, ':');
    if (i >= 0 && api->available(i, fwdir)) return i;
    m = colon ? (size_t)(colon - id + 1) : strlen(id);
    for (k = 0; k < n; k++)                                  /* the module's first voice that is there */
        if (!strncmp(api->info(k)->id, id, m) && api->available(k, fwdir)) return k;
    for (k = 0; k < n; k++)
        if (api->available(k, fwdir)) return k;
    return -1;
}

int ssi_sapi_rate(long r)
{
    r = r < -10 ? -10 : r > 10 ? 10 : r;
    return (int)((r + 10) * 5);
}

int ssi_sapi_pitch(long p)
{
    p = p < -10 ? -10 : p > 10 ? 10 : p;
    return (int)(50 + p * 5);
}

int ssi_ansi_path(const wchar_t *path, char *out, int cap)
{
    wchar_t s[MAX_PATH * 2];
    const wchar_t *use = path;
    BOOL lossy = FALSE;
    int n;
    DWORD k = GetShortPathNameW(path, s, (DWORD)(sizeof s / sizeof s[0]));
    if (k && k < sizeof s / sizeof s[0]) use = s;            /* 8.3 names: ANSI-safe wherever they exist */
    n = WideCharToMultiByte(CP_ACP, 0, use, -1, out, cap, NULL, &lossy);
    return n > 0 && !lossy;
}
