/* ssp_speech.h -- one Apple speech request spoken: its segments (ssp_ssml.h) one after another through Android's
 * front end (ssa_engine.h), each as a request of its own, with the silence after each, pulled block by block.
 *
 * Each segment's audio is ssa_start + ssa_pull's for that text, unchanged -- the desktop's PCM -- so the only samples
 * this adds are the zeros of the pauses, at the voice's sample rate.  The speech extension pulls as the system asks
 * for audio, so the first sound comes after the unit's first block, not after the whole request.
 *
 * Not thread-safe (one caller pulls), except ssp_speech_stop, which any thread may call: the pull in progress returns
 * -2 before its next block, and no later segment starts.  Plain C, no Apple frameworks: the host-side test compiles
 * it.  MIT.
 */
#ifndef SSP_SPEECH_H
#define SSP_SPEECH_H

#include "ssa_engine.h"
#include "ssp_ssml.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ssp_speech ssp_speech;

/* A player over e (not owned). */
ssp_speech *ssp_speech_new(ssa_engine *e);
void ssp_speech_free(ssp_speech *p);

/* Starts the request r (copied) on the voice with the app's settings s, the request's own rate and pitch on top of
   them (ssa_map.h).  Abandons a request still running.  0: there is audio (or silence) to pull; 1: nothing at all. */
int ssp_speech_start(ssp_speech *p, int voice, const ssp_request *r, const ssa_settings *s);

/* Up to cap samples into out: the count; 0 once the request has finished; -2 when stopped. */
int ssp_speech_pull(ssp_speech *p, short *out, int cap);

/* Any thread. */
void ssp_speech_stop(ssp_speech *p);

/* A segment the unit could not speak (ssa_start's -1), counted since the start: the extension logs it. */
int ssp_speech_errors(const ssp_speech *p);

/* The test's control (test_apple_speech.c sets it; the app never does): 1 skips the pauses' silence; 2 forgets a stop
   at the next segment (a cancelled request's later segments would still be spoken). */
extern int ssp_speech_break;

#ifdef __cplusplus
}
#endif
#endif
