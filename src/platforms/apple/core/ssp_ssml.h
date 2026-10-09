/* ssp_ssml.h -- an Apple speech request's SSML, as the voice speaks it: the text in segments, each with the pause
 * after it, and the request's rate, pitch and volume.
 *
 * VoiceOver and every AVSpeechSynthesizer client hand an AVSpeechSynthesisProviderRequest's text over as SSML. Its
 * pauses are VoiceOver's phrasing -- an item's name and its hint, a heading and its level -- and the app's Pause mode
 * scales them, as TGSpeechBox's speech extension does (its TGSBAudioUnit.swift: extractSegments, scaledBreakMs,
 * scaledSentencePauseMs, the end-of-request pause):
 *
 *   <break time="800.0ms"/>, "0.8s" or bare milliseconds; or strength= none 0, x-weak 100, weak 200, medium 350
 *       (the default), strong 500, x-strong 800; at most 2000 ms
 *   </s> and </p>: 300 ms (iOS 27 VoiceOver separates an element's parts as sentences: "Dock" / "Messages")
 *   the request's end, when it ends in speech: 300 ms (VoiceOver cancels the tail the moment the user moves on)
 *
 * each times the Pause mode's scale -- off 0 %, short 50 % (the default), long 100 % -- with any pause that is not
 * dropped kept at 30 ms or more, so a short setting compresses VoiceOver's boundaries instead of erasing them.  A
 * boundary with no text before it folds into the one before, the longer pause winning, so
 * "</s><s><break time=\"800ms\"/></s>" is one 800 ms gap, not 300 + 800.
 *
 * Each segment's text is spoken by the unit exactly as a request with that text alone (ssp_speech.h), so the voice's
 * PCM stays the desktop's; only the silence between segments is the Apple app's.  The units' own pauses (the Braille
 * Lite's short pauses, the Speak-Out's join phrases and shortened sentence pauses) are their settings, untouched.
 *
 * The rate and pitch come as Android's percentages (100 = normal; ssa_map.h puts them on the sliders) and the volume
 * as an amplitude factor: VoiceOver's speech volume rotor arrives only as decibels, volume="-6.0206003dB" for 50 %.
 *
 * Plain C, no Apple frameworks: the host-side test (test/test_apple_speech.c) compiles it.  MIT.
 */
#ifndef SSP_SSML_H
#define SSP_SSML_H

#ifdef __cplusplus
extern "C" {
#endif

#define SSP_PAUSE_OFF 0
#define SSP_PAUSE_SHORT 1                  /* the default */
#define SSP_PAUSE_LONG 2

#define SSP_BREAK_MAX_MS 2000
#define SSP_SENTENCE_MS 300                /* at long */
#define SSP_REQUEST_END_MS 300             /* at long */
#define SSP_PAUSE_FLOOR_MS 30

typedef struct {
    char *text;                            /* UTF-8, tags stripped, entities decoded, white space collapsed; "" none */
    int pause_ms;                          /* the silence after it */
} ssp_segment;

typedef struct {
    ssp_segment *seg;
    int n;
    int rate;                              /* percent, 100 = normal (prosody rate) */
    int pitch;                             /* percent, 100 = normal (prosody pitch) */
    double volume;                         /* amplitude, 1 = normal (prosody volume), 0..2 */
} ssp_request;

/* The pause mode's scale in percent: 0, 50 or 100 (anything else is the short default's). */
int ssp_pause_scale(int pause_mode);

/* Parses ssml (UTF-8, NUL-terminated; plain text is taken as it is) at the pause mode into out.  0, or -1 out of
   memory (out then empty).  A request with no text at all keeps its pauses in out->seg (one empty segment): a
   break-only spacer. */
int ssp_parse(const char *ssml, int pause_mode, ssp_request *out);
void ssp_request_free(ssp_request *r);

/* The total text of a request: 1 when any segment has some. */
int ssp_has_text(const ssp_request *r);
/* Every pause added up, in ms. */
int ssp_total_pause_ms(const ssp_request *r);

/* The test's control (test_apple_speech.c sets it; the app never does): 1 drops every pause (VoiceOver's boundaries
   run together, as before TGSpeechBox's fix); 2 ignores the pause mode (every pause at its long length). */
extern int ssp_ssml_break;

#ifdef __cplusplus
}
#endif
#endif
