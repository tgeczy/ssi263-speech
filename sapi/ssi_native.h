/* ssi_native.h -- the SAPI engine's side of the native voices: ssi263speech.dll found beside the caller and loaded
 * (LoadLibrary, so the release build of the same exports drops in), and the settings dialog's values and a request's
 * rate, pitch and volume put on the voice table (src/csrc/voices.h) exactly as sapi/ssi_serve.py put them on the NVDA
 * drivers.  Shared by the SAPI DLL (ssi263_sapi.cpp, MSVC) and the native serve host (ssi_serve.c, w64devkit), so
 * the host that sapi/test_native.py holds to ssi_serve.py byte for byte maps settings with the DLL's own code.
 *
 * Plain C, Windows only.  MIT.
 */
#ifndef SSI_NATIVE_H
#define SSI_NATIVE_H

#include <windows.h>
#include "../src/csrc/voices.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The voice table's functions, from the DLL. */
typedef struct {
    HMODULE dll;
    int (*count)(void);
    const ssv_info *(*info)(int i);
    int (*find)(const char *id);
    int (*available)(int i, const char *fwdir);
    void (*boot_defaults)(ssv_boot *b);
    void (*defaults)(int i, ssv_settings *s);
    ssv_bank *(*bank_new)(const char *fwdir);
    void (*bank_free)(ssv_bank *k);
    void (*bank_boot)(ssv_bank *k, const ssv_boot *b);
    ssv_voice *(*bank_voice)(ssv_bank *k, int i, char *err, int errlen);
    int (*speak)(ssv_voice *v, const ssv_settings *s, const char *utf8, int pitch_offset);
    int (*render)(ssv_voice *v, const short **pcm, int *done);
    void (*cancel)(ssv_voice *v);
} ssi_api;

/* Loads dir\ssi263speech.dll (dir: a folder, no trailing backslash) and resolves every function.  1, or 0 with the
   reason in err (the API then all NULL). */
int ssi_load(ssi_api *api, const wchar_t *dir, char *err, int errlen);

/* The settings dialog's values (HKCU "Software\SSI-263 SAPI", sapi/settings.ps1), as ssi_serve.py's command line took
   them: --rate, --inflection, --whine, --accent-inflection, --run-ahead, --bl-numbers. */
typedef struct {
    int sample_rate;               /* SampleRate: 11025, 22050 (default), 44100 */
    int inflection;                /* Inflection: the Braille Lite's on/off (1) */
    int whine;                     /* Whine: 0 off, 1 hiss, 2 whine */
    int accent_inflection;         /* AccentInflection: 0-100 (100) */
    int run_ahead;                 /* RunAhead: the Braille Lite's (0) */
    int numbers;                   /* BrailleLiteNumbers: the Braille Lite's number words, its driver's numberWords (1);
                                      the Accents keep their own driver's default, the Mockingboard its voice's */
} ssi_options;
void ssi_options_defaults(ssi_options *o);

/* ssi_serve.py's driver(): the boot settings every unit gets (the rate the DLL declared to SAPI, the Braille Lite's
   inflection and whine), and the utterance's settings for voice i -- the driver's defaults, the dialog's Accent
   intonation and Braille Lite run ahead and number words, and the request's rate, pitch and volume (NVDA's 0-100,
   clamped as the drivers' setters clamp them). */
void ssi_boot(const ssi_options *o, ssv_boot *b);
void ssi_settings(const ssi_api *api, int i, const ssi_options *o, int rate, int pitch, int volume, ssv_settings *s);

/* The request's voice id ("<driver module>:<voice>") as ssi_serve.py resolved it: the voice when it is there, else
   the first of its module's that is, else the first voice there at all.  -1 when none is. */
int ssi_voice(const ssi_api *api, const char *id, const char *fwdir);

/* A folder's name as the C library's fopen takes it (the ANSI code page; its 8.3 name when it has one, so a name
   outside the code page still opens).  1, or 0 when it cannot be said. */
int ssi_ansi_path(const wchar_t *path, char *out, int cap);

/* SAPI's own scales onto NVDA's 0-100, linearly (ssi263_sapi.cpp's, from before): rate -10..10 -> 0..100, the pitch's
   MiddleAdj -10..10 -> 0..100; SAPI zero is the middle of the NVDA slider. */
int ssi_sapi_rate(long sapi_rate);
int ssi_sapi_pitch(long middle_adj);

#ifdef __cplusplus
}
#endif
#endif
