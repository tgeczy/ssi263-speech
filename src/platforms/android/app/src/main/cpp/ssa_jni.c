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
#include <stdio.h>
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

/* The Accent-PC's ROMs (the APK's assets/aicom), copied into the engine. */
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

/* The SmarTalk's built-in S_TALK.EXE (the APK's assets/smartalk), copied into the engine; an imported copy wins. */
JNIEXPORT jboolean JNICALL FN(nativeSmarTalk)(JNIEnv *env, jclass cls, jbyteArray jexe)
{
    jbyte *p;
    jsize n;
    int ok;
    (void)cls;
    if (!g_engine || !jexe) return JNI_FALSE;
    n = (*env)->GetArrayLength(env, jexe);
    if (!(p = (*env)->GetByteArrayElements(env, jexe, NULL))) return JNI_FALSE;
    ok = ssa_set_smartalk(g_engine, (const unsigned char *)p, (size_t)n);
    (*env)->ReleaseByteArrayElements(env, jexe, p, JNI_ABORT);
    return ok ? JNI_TRUE : JNI_FALSE;
}

/* The SynPhonix's built-in VEST.COM (the APK's assets/synphonix), copied into the engine; an imported copy wins. */
JNIEXPORT jboolean JNICALL FN(nativeSynPhonix)(JNIEnv *env, jclass cls, jbyteArray jcom)
{
    jbyte *p;
    jsize n;
    int ok;
    (void)cls;
    if (!g_engine || !jcom) return JNI_FALSE;
    n = (*env)->GetArrayLength(env, jcom);
    if (!(p = (*env)->GetByteArrayElements(env, jcom, NULL))) return JNI_FALSE;
    ok = ssa_set_synphonix(g_engine, (const unsigned char *)p, (size_t)n);
    (*env)->ReleaseByteArrayElements(env, jcom, p, JNI_ABORT);
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
                                       jint tone, jint volume, jint pack, jint run_ahead, jint line_lift,
                                       jint numbers,
                                       jint so_tone, jint so_join, jint so_short, jint request_rate,
                                       jint request_pitch, jintArray jmarks)
{
    ssa_settings s;
    jsize n, n_marks = 0;
    char *utf8;
    jint *marks = NULL;
    int r;
    (void)cls;
    if (!g_engine || !jutf8) return -1;
    n = (*env)->GetArrayLength(env, jutf8);
    utf8 = (char *)malloc((size_t)n + 1);
    if (!utf8) return -1;
    (*env)->GetByteArrayRegion(env, jutf8, 0, n, (jbyte *)utf8);
    utf8[n] = 0;
    s.rate = rate; s.pitch = pitch; s.tone = tone; s.volume = volume; s.pack = pack;
    s.run_ahead = run_ahead; s.line_lift = line_lift; s.numbers = numbers;
    s.so_tone = so_tone; s.so_join = so_join; s.so_short = so_short;
    g_error[0] = 0;
    if (jmarks && (n_marks = (*env)->GetArrayLength(env, jmarks)) > 0
            && (marks = (jint *)malloc(sizeof(jint) * (size_t)n_marks)))
        (*env)->GetIntArrayRegion(env, jmarks, 0, n_marks, marks);
    r = ssa_start_marked(g_engine, voice, utf8, &s, request_rate, request_pitch, (const int *)marks,
                         marks ? (int)n_marks : 0);
    free(marks);
    free(utf8);
    return r;
}

/* The marks the audio pulled so far has reached and that were not given yet: (mark, frame) pairs into out; the
   number of pairs (ssa_engine.h ssa_reached). */
JNIEXPORT jint JNICALL FN(nativeReached)(JNIEnv *env, jclass cls, jintArray jout)
{
    jint pairs[128];
    jsize cap;
    int n;
    (void)cls;
    if (!g_engine || !jout) return 0;
    cap = (*env)->GetArrayLength(env, jout) / 2;
    if (cap > 64) cap = 64;
    n = ssa_reached(g_engine, (int *)pairs, (int)cap);
    if (n > 0)
        (*env)->SetIntArrayRegion(env, jout, 0, 2 * n, pairs);
    return n;
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

/* ssa_import.h's ssa_import_voice, the firmware in these bytes by content: SSA_ENGLISH 0, _SPANISH 1, _GERMAN 7,
   _ITALIAN 8 or _SLOVAK 9 (a Blazie release on the list, its language as the app's voice, written to `out` as a
   .BNS), a release voice from SSA_RELEASE_FIRST 10 (Hungarian ...: the release table's, the same way),
   SSA_HEX_SPEAKOUT 3 (the known SPEAKOUT.HEX,
   written to `out`) or SSA_FW_MOCKINGBOARD 5 / _EARLY 6 (a known Mockingboard file, from itself or a disk image,
   written to `out`), or SSA_FW_RELEASE 100 + a release table row no voice speaks (the 640, Japanese, the betas:
   written to `out`, for the unit screen alone -- nativeReleaseRow names it), the label in
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
    r = out ? ssa_import_voice(data, (long)n, out, g_import_error, sizeof g_import_error) : BLV_FW_WRITE;
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

/* A release a phone's voice speaks: a shipped voice's language, or a release voice's row. */
static int has_voice(int k) { return blv_firmware_language(k) >= 0 || ssa_release_voice(k) >= 0; }

/* The list of releases the import accepts, by label (bl_firmware.c's KNOWN): only those with a voice -- the
   emulator's own releases are never offered or named to the phone's voices. */
JNIEXPORT jobjectArray JNICALL FN(nativeKnownFirmware)(JNIEnv *env, jclass cls)
{
    int n = 0, k, i = 0;
    jobjectArray out;
    (void)cls;
    for (k = 0; k < blv_firmware_count(); k++)
        n += has_voice(k);
    out = (*env)->NewObjectArray(env, n, (*env)->FindClass(env, "java/lang/String"), NULL);
    for (k = 0; out && k < blv_firmware_count(); k++)
        if (has_voice(k))
            (*env)->SetObjectArrayElement(env, out, i++, (*env)->NewStringUTF(env, blv_firmware_label(k)));
    return out;
}

/* The release voices (ssa_engine.h's, from SSA_RELEASE_FIRST, in their order), the one list the app's are made from
   (SsiNative.releases): per voice "<index>\t<id>\t<file>\t<name>\t<voice name>\t<tag>" -- the release's id (its
   state is <id>.state), the file a person brings, its name in English, the voice's in its own language, its BCP 47
   tag (bl_languages.txt). */
JNIEXPORT jobjectArray JNICALL FN(nativeReleaseVoices)(JNIEnv *env, jclass cls)
{
    int v, n = 0, i = 0;
    char row[512];
    jobjectArray out;
    (void)cls;
    for (v = SSA_RELEASE_FIRST; v < SSA_VOICES; v++)
        n += ssa_voice_release(v) >= 0;
    out = (*env)->NewObjectArray(env, n, (*env)->FindClass(env, "java/lang/String"), NULL);
    for (v = SSA_RELEASE_FIRST; out && v < SSA_VOICES; v++) {
        int k = ssa_voice_release(v);
        if (k < 0) continue;
        snprintf(row, sizeof row, "%d\t%s\t%s\t%s\t%s\t%s", v, blv_release_id(k), blv_release_file(k),
                 blv_release_name(k), blv_release_voice_name(k), blv_release_tag(k));
        (*env)->SetObjectArrayElement(env, out, i++, (*env)->NewStringUTF(env, row));
    }
    return out;
}

/* The releases no voice speaks, kept for the unit screen alone (the Braille 'n Speak 640, Japanese, the betas), in
   the table's order: per row "<id>\t<file>\t<name>" -- its file under units/ is <id>.BNS, the file a person brings,
   its name.  Setup's list names them behind its own button (SettingsActivity), as the release voices are. */
JNIEXPORT jobjectArray JNICALL FN(nativeUnitReleases)(JNIEnv *env, jclass cls)
{
    int k, n = 0, i = 0;
    char row[512];
    jobjectArray out;
    (void)cls;
    for (k = 0; k < blv_firmware_count(); k++)
        n += blv_firmware_language(k) == BLV_FW_NONE && !blv_release_voice_key(k);
    out = (*env)->NewObjectArray(env, n, (*env)->FindClass(env, "java/lang/String"), NULL);
    for (k = 0; out && k < blv_firmware_count(); k++) {
        if (blv_firmware_language(k) != BLV_FW_NONE || blv_release_voice_key(k)) continue;
        snprintf(row, sizeof row, "%s\t%s\t%s", blv_release_id(k), blv_release_file(k), blv_release_name(k));
        (*env)->SetObjectArrayElement(env, out, i++, (*env)->NewStringUTF(env, row));
    }
    return out;
}

/* A release table row, for one the unit screen keeps alone (nativeImportFirmware's SSA_FW_RELEASE + row):
   "<id>\t<name>", its file under units/ being <id>.BNS; null for no such row. */
JNIEXPORT jstring JNICALL FN(nativeReleaseRow)(JNIEnv *env, jclass cls, jint k)
{
    char row[512];
    const char *id = blv_release_id(k), *name = blv_release_name(k);
    (void)cls;
    if (!id || !name)
        return NULL;
    snprintf(row, sizeof row, "%s\t%s", id, name);
    return (*env)->NewStringUTF(env, row);
}

static int state_progress(void *ctx, double done)
{
    (void)ctx;
    g_state_done = done;
    return g_state_cancel;
}

/* blv_make_state for a Blazie voice's unit (SSA_ENGLISH ... SSA_SLOVAK), with its language's recipe
   (ssa_blazie_recipe), or blv_make_release_state for a release voice's (its row's recipe): 1, or 0 with the reason in
   nativeImportError.  Long (seconds; Spanish most of a minute): call it off the main thread and read
   nativeStateProgress meanwhile; nativeStateCancel abandons it. */
JNIEXPORT jint JNICALL FN(nativeMakeState)(JNIEnv *env, jclass cls, jstring jfw, jint voice, jstring jout)
{
    const char *fw, *out;
    int r = 0, recipe = ssa_blazie_recipe(voice), release = ssa_voice_release(voice);
    (void)cls;
    g_import_error[0] = 0;
    g_state_cancel = 0;
    g_state_done = 0;
    if (recipe < 0 && release < 0) {
        snprintf(g_import_error, sizeof g_import_error, "voice %d has no state to make", voice);
        return 0;
    }
    fw = (*env)->GetStringUTFChars(env, jfw, NULL);
    out = (*env)->GetStringUTFChars(env, jout, NULL);
    if (fw && out && release >= 0)
        r = blv_make_release_state(fw, release, out, state_progress, NULL, g_import_error, sizeof g_import_error);
    else if (fw && out)
        r = blv_make_state(fw, recipe, out, state_progress, NULL, g_import_error, sizeof g_import_error);
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
