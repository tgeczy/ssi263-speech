/* ssa_jni.c -- the JNI bridge from Kotlin (SsiNative.kt) to the front end (ssa_engine.h).
 *
 * Thin: it marshals strings, the Accents' ROMs and the PCM buffer across the boundary and calls ssa_*, which calls
 * bl_voice, as_voice, so_voice (and am_voice and mb_voice, in a build with the Accent-mini and the Mockingboard) --
 * the same C the speech-dispatcher module and the NVDA add-ons' libraries are built from, linked into this one .so.
 * No IPC.
 *
 * C, not C++: only MAME's CPU cores (the Z180, the 8085, the V40) are C++, linked with a static libc++ inside the .so.
 *
 * One engine per process.  Every call is serialised on the Kotlin side (SsiEngine's lock) except nativeStop, which
 * only sets a flag.
 */
#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include "ssa_engine.h"
#include "ssa_import.h"
#include "blazie/bl_firmware.h"
#include "blazie/bl_state.h"

static ssa_engine *g_engine;
static char g_error[256];

#define FN(name) Java_com_ssi263speech_tts_SsiNative_##name

JNIEXPORT jboolean JNICALL FN(nativeOpen)(JNIEnv *env, jclass cls, jstring jdir)
{
    const char *dir;
    (void)cls;
    if (g_engine) return JNI_TRUE;
    dir = (*env)->GetStringUTFChars(env, jdir, NULL);
    if (!dir) return JNI_FALSE;
    g_engine = ssa_new(dir);
    (*env)->ReleaseStringUTFChars(env, jdir, dir);
    return g_engine ? JNI_TRUE : JNI_FALSE;
}

/* The Accent SA's ROMs (the APK's assets/aicom), copied into the engine. */
JNIEXPORT jboolean JNICALL FN(nativeAccentRoms)(JNIEnv *env, jclass cls, jbyteArray ju2, jbyteArray ju3,
                                                jbyteArray ju4)
{
    jbyteArray arr[3] = {ju2, ju3, ju4};
    jbyte *p[3] = {NULL, NULL, NULL};
    jsize n[3] = {0, 0, 0};
    int i, ok = 0;
    (void)cls;
    if (!g_engine || !ju2 || !ju3 || !ju4) return JNI_FALSE;
    for (i = 0; i < 3; i++) {
        n[i] = (*env)->GetArrayLength(env, arr[i]);
        p[i] = (*env)->GetByteArrayElements(env, arr[i], NULL);
        if (!p[i]) break;
    }
    if (i == 3)
        ok = ssa_set_accent_roms(g_engine, (const unsigned char *)p[0], (size_t)n[0], (const unsigned char *)p[1],
                                 (size_t)n[1], (const unsigned char *)p[2], (size_t)n[2]);
    for (i = 0; i < 3; i++)
        if (p[i]) (*env)->ReleaseByteArrayElements(env, arr[i], p[i], JNI_ABORT);
    return ok ? JNI_TRUE : JNI_FALSE;
}

/* The Accent-mini's SPKEMS.DVC (the APK's assets/aicom, in a build with the Accent-mini), copied into the engine. */
JNIEXPORT jboolean JNICALL FN(nativeAccentMini)(JNIEnv *env, jclass cls, jbyteArray jdvc)
{
    jbyte *p;
    jsize n;
    int ok;
    (void)cls;
    if (!g_engine || !jdvc) return JNI_FALSE;
    n = (*env)->GetArrayLength(env, jdvc);
    if (!(p = (*env)->GetByteArrayElements(env, jdvc, NULL))) return JNI_FALSE;
    ok = ssa_set_accent_mini(g_engine, (const unsigned char *)p, (size_t)n);
    (*env)->ReleaseByteArrayElements(env, jdvc, p, JNI_ABORT);
    return ok ? JNI_TRUE : JNI_FALSE;
}

/* A Mockingboard voice's built-in file (the APK's assets/sweet-micro), copied into the engine; an imported copy wins. */
JNIEXPORT jboolean JNICALL FN(nativeMockingboard)(JNIEnv *env, jclass cls, jint voice, jbyteArray jbin)
{
    jbyte *p;
    jsize n;
    int ok;
    (void)cls;
    if (!g_engine || !jbin) return JNI_FALSE;
    n = (*env)->GetArrayLength(env, jbin);
    if (!(p = (*env)->GetByteArrayElements(env, jbin, NULL))) return JNI_FALSE;
    ok = ssa_set_mockingboard(g_engine, voice, (const unsigned char *)p, (size_t)n);
    (*env)->ReleaseByteArrayElements(env, jbin, p, JNI_ABORT);
    return ok ? JNI_TRUE : JNI_FALSE;
}

/* This build carries the voice's engine (no engine needs to be open). */
JNIEXPORT jboolean JNICALL FN(nativeVoiceBuilt)(JNIEnv *env, jclass cls, jint voice)
{
    (void)env; (void)cls;
    return ssa_voice_built(voice) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL FN(nativeHasVoice)(JNIEnv *env, jclass cls, jint voice)
{
    (void)env; (void)cls;
    return g_engine && ssa_has_voice(g_engine, voice) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL FN(nativeConfigure)(JNIEnv *env, jclass cls, jint rate, jint inflection, jint whine)
{
    (void)env; (void)cls;
    if (g_engine) ssa_configure(g_engine, rate, inflection, whine);
}

JNIEXPORT jint JNICALL FN(nativeSampleRate)(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return g_engine ? ssa_sample_rate(g_engine) : 22050;
}

JNIEXPORT jint JNICALL FN(nativeLoad)(JNIEnv *env, jclass cls, jint voice)
{
    (void)env; (void)cls;
    g_error[0] = 0;
    if (!g_engine) { strcpy(g_error, "the engine is not open"); return -1; }
    return ssa_load(g_engine, voice, g_error, sizeof g_error);
}

JNIEXPORT jstring JNICALL FN(nativeError)(JNIEnv *env, jclass cls)
{
    (void)cls;
    return (*env)->NewStringUTF(env, g_error);
}

JNIEXPORT jint JNICALL FN(nativeStart)(JNIEnv *env, jclass cls, jint voice, jbyteArray jutf8, jint rate, jint pitch,
                                       jint tone, jint volume, jint pack, jint run_ahead, jint numbers,
                                       jint so_tone, jint so_join, jint so_short, jint request_rate,
                                       jint request_pitch)
{
    ssa_settings s;
    jsize n;
    char *utf8;
    int r;
    (void)cls;
    if (!g_engine || !jutf8) return -1;
    n = (*env)->GetArrayLength(env, jutf8);
    utf8 = (char *)malloc((size_t)n + 1);
    if (!utf8) return -1;
    (*env)->GetByteArrayRegion(env, jutf8, 0, n, (jbyte *)utf8);
    utf8[n] = 0;
    s.rate = rate; s.pitch = pitch; s.tone = tone; s.volume = volume; s.pack = pack;
    s.run_ahead = run_ahead; s.numbers = numbers;
    s.so_tone = so_tone; s.so_join = so_join; s.so_short = so_short;
    g_error[0] = 0;
    r = ssa_start(g_engine, voice, utf8, &s, request_rate, request_pitch);
    free(utf8);
    return r;
}

/* Up to out.length / 2 samples as 16-bit little-endian bytes (every Android ABI is little-endian): the byte count,
   0 when the utterance is over, -2 when stopped. */
JNIEXPORT jint JNICALL FN(nativePull)(JNIEnv *env, jclass cls, jbyteArray jout)
{
    static short pcm[8192];
    jsize cap = (*env)->GetArrayLength(env, jout) / 2;
    int n;
    (void)cls;
    if (!g_engine) return -1;
    if (cap > (jsize)(sizeof pcm / sizeof pcm[0])) cap = (jsize)(sizeof pcm / sizeof pcm[0]);
    n = ssa_pull(g_engine, pcm, (int)cap);
    if (n <= 0) return n;
    (*env)->SetByteArrayRegion(env, jout, 0, n * 2, (const jbyte *)pcm);
    return n * 2;
}

JNIEXPORT void JNICALL FN(nativeStop)(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    if (g_engine) ssa_stop(g_engine);
}

JNIEXPORT void JNICALL FN(nativeCancel)(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    if (g_engine) ssa_cancel(g_engine);
}

/* The engine let go, so the next nativeOpen reads the unit's files afresh (after an import or a removal). */
JNIEXPORT void JNICALL FN(nativeClose)(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    ssa_free(g_engine);
    g_engine = NULL;
}

/* ---- the firmware import (FirmwareImport.kt, SsiImport.kt): its own error text, as it runs beside speech ------- */
static char g_import_error[256];
static volatile int g_state_cancel;
static volatile double g_state_done;

JNIEXPORT jstring JNICALL FN(nativeImportError)(JNIEnv *env, jclass cls)
{
    (void)cls;
    return (*env)->NewStringUTF(env, g_import_error);
}

static unsigned char *bytes_of(JNIEnv *env, jbyteArray a, jsize *n)
{
    unsigned char *p;
    *n = (*env)->GetArrayLength(env, a);
    p = (unsigned char *)malloc((size_t)*n + 1);
    if (p) (*env)->GetByteArrayRegion(env, a, 0, *n, (jbyte *)p);
    return p;
}

/* ssa_import.h's ssa_import_firmware, the firmware in these bytes by content: BLV_FW_ENGLISH 0 or _SPANISH 1 (a
   Braille Lite release on the list, written to `out` as a .BNS), SSA_HEX_SPEAKOUT 3 (the known SPEAKOUT.HEX,
   written to `out`) or SSA_FW_MOCKINGBOARD 5 / _EARLY 6 (a known Mockingboard file, from itself or a disk image,
   written to `out`), the label in
   nativeImportError; else negative (_NONE, _REFUSED, _WRITE, _UNKNOWN; SSA_HEX_OTHER -5: an Intel HEX file but not
   the Speak-Out's; SSA_FW_MB_BUILD -6: the Mockingboard's in a build without it), the reason in nativeImportError. */
JNIEXPORT jint JNICALL FN(nativeImportFirmware)(JNIEnv *env, jclass cls, jbyteArray jdata, jstring jout)
{
    jsize n;
    unsigned char *data;
    const char *out;
    int r;
    (void)cls;
    g_import_error[0] = 0;
    data = bytes_of(env, jdata, &n);
    if (!data) { strcpy(g_import_error, "out of memory"); return BLV_FW_WRITE; }
    out = (*env)->GetStringUTFChars(env, jout, NULL);
    r = out ? ssa_import_firmware(data, (long)n, out, g_import_error, sizeof g_import_error) : BLV_FW_WRITE;
    if (out) (*env)->ReleaseStringUTFChars(env, jout, out);
    free(data);
    return r;
}

/* blv_state_check: 1 when the state is the one blv_make_state makes from that release's .BNS, else 0. */
JNIEXPORT jboolean JNICALL FN(nativeStateCheck)(JNIEnv *env, jclass cls, jstring jbns, jstring jstate)
{
    const char *bns, *state;
    int r = 0;
    (void)cls;
    bns = (*env)->GetStringUTFChars(env, jbns, NULL);
    state = (*env)->GetStringUTFChars(env, jstate, NULL);
    if (bns && state)
        r = blv_state_check(bns, state);
    if (bns) (*env)->ReleaseStringUTFChars(env, jbns, bns);
    if (state) (*env)->ReleaseStringUTFChars(env, jstate, state);
    return r ? JNI_TRUE : JNI_FALSE;
}

/* The list of releases the import accepts, by label (bl_firmware.c's KNOWN). */
JNIEXPORT jobjectArray JNICALL FN(nativeKnownFirmware)(JNIEnv *env, jclass cls)
{
    int n = blv_firmware_count(), k;
    jobjectArray out = (*env)->NewObjectArray(env, n, (*env)->FindClass(env, "java/lang/String"), NULL);
    (void)cls;
    for (k = 0; out && k < n; k++)
        (*env)->SetObjectArrayElement(env, out, k, (*env)->NewStringUTF(env, blv_firmware_label(k)));
    return out;
}

static int state_progress(void *ctx, double done)
{
    (void)ctx;
    g_state_done = done;
    return g_state_cancel;
}

/* blv_make_state: 1, or 0 with the reason in nativeImportError.  Long (seconds; Spanish most of a minute): call it
   off the main thread and read nativeStateProgress meanwhile; nativeStateCancel abandons it. */
JNIEXPORT jint JNICALL FN(nativeMakeState)(JNIEnv *env, jclass cls, jstring jfw, jint language, jstring jout)
{
    const char *fw, *out;
    int r = 0;
    (void)cls;
    g_import_error[0] = 0;
    g_state_cancel = 0;
    g_state_done = 0;
    fw = (*env)->GetStringUTFChars(env, jfw, NULL);
    out = (*env)->GetStringUTFChars(env, jout, NULL);
    if (fw && out)
        r = blv_make_state(fw, language, out, state_progress, NULL, g_import_error, sizeof g_import_error);
    if (fw) (*env)->ReleaseStringUTFChars(env, jfw, fw);
    if (out) (*env)->ReleaseStringUTFChars(env, jout, out);
    return r;
}

JNIEXPORT jdouble JNICALL FN(nativeStateProgress)(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return g_state_done;
}

JNIEXPORT void JNICALL FN(nativeStateCancel)(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    g_state_cancel = 1;
}

/* ssa_probe: the samples the unit in `dir` speaks for the text (0 when silent), or -1 with the reason. */
JNIEXPORT jlong JNICALL FN(nativeProbe)(JNIEnv *env, jclass cls, jstring jdir, jint voice, jbyteArray jutf8)
{
    jsize n;
    unsigned char *utf8 = bytes_of(env, jutf8, &n);
    const char *dir;
    long r = -1;
    (void)cls;
    g_import_error[0] = 0;
    if (!utf8) return -1;
    utf8[n] = 0;
    dir = (*env)->GetStringUTFChars(env, jdir, NULL);
    if (dir) r = ssa_probe(dir, voice, (const char *)utf8, NULL, g_import_error, sizeof g_import_error);
    if (dir) (*env)->ReleaseStringUTFChars(env, jdir, dir);
    free(utf8);
    return (jlong)r;
}
