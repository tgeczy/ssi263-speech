// The system TTS service: TalkBack and every other app speak through here.  The request's rate and pitch go on top
// of the app's sliders (the C side maps them, ssa_map.h); its volume Android applies to its own audio track.  The
// audio streams to the framework block by block as the unit speaks, and a stop lands between blocks.
package com.ssi263speech.tts

import android.media.AudioFormat
import android.speech.tts.SynthesisCallback
import android.speech.tts.SynthesisRequest
import android.speech.tts.TextToSpeech
import android.speech.tts.TextToSpeechService
import android.speech.tts.Voice
import android.util.Log

class SsiTtsService : TextToSpeechService() {

    @Volatile private var stopRequested = false
    @Volatile private var stopAt = 0L

    override fun onCreate() {
        super.onCreate()
        // Boot the unit when the service binds, so the first utterance does not wait for it.
        Thread({ try { SsiEngine.warmUp(applicationContext) } catch (e: Throwable) {
            Log.e(TAG, "warm-up failed", e) } }, "ssi263-warmup").start()
    }

    // SsiLanguages: English with the Accents built in; every other Blazie language (Spanish, German, Italian, Slovak,
    // and the release voices': Hungarian, Turkish, ...) once its unit's firmware is imported -- until then supported
    // but its data missing, so nothing offers them.
    private fun availability(lang: String?, country: String?): Int =
        SsiLanguages.availability(lang, country, SsiEngine.voices(this), SsiEngine.KNOWN)

    override fun onIsLanguageAvailable(lang: String?, country: String?, variant: String?): Int =
        availability(lang, country)

    // The language last loaded (onLoadLanguage, onLoadVoice), as TGSpeechBox keeps its current one: the voice for it
    // while it can speak; until one is loaded, the chosen voice's (voiceFor falls back to one that can).
    @Volatile private var loaded: String? = null

    override fun onGetLanguage(): Array<String> =
        SsiLanguages.language(loaded?.let { SsiEngine.voiceByName(this, it) }
            ?: SsiEngine.voiceFor(this, SsiSettings.snapshot(this).voice))

    override fun onLoadLanguage(lang: String?, country: String?, variant: String?): Int {
        val r = availability(lang, country)
        if (r >= 0) SsiEngine.voiceForLanguage(this, lang, SsiSettings.snapshot(this).voice, country)
            ?.let { loaded = it.name }
        return r
    }

    override fun onGetVoices(): MutableList<Voice> = SsiEngine.voices(this).map {
        Voice(it.name, it.locale, Voice.QUALITY_NORMAL, Voice.LATENCY_NORMAL, false, emptySet())
    }.toMutableList()

    override fun onIsValidVoiceName(name: String?): Int =
        if (SsiEngine.voiceByName(this, name) != null) TextToSpeech.SUCCESS else TextToSpeech.ERROR

    override fun onLoadVoice(name: String?): Int =
        onIsValidVoiceName(name).also { if (it == TextToSpeech.SUCCESS) loaded = name }

    override fun onGetDefaultVoiceNameFor(lang: String?, country: String?, variant: String?): String? {
        if (lang != null && availability(lang, country) < 0) return null
        val chosen = SsiSettings.snapshot(this).voice
        return (if (lang == null) SsiEngine.voiceFor(this, chosen)
                else SsiEngine.voiceForLanguage(this, lang, chosen, country))?.name
    }

    override fun onStop() {
        stopAt = android.os.SystemClock.elapsedRealtime()
        stopRequested = true
        SsiEngine.stop()
    }

    override fun onSynthesizeText(request: SynthesisRequest, callback: SynthesisCallback) {
        android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_AUDIO)
        val text = request.charSequenceText?.toString() ?: ""
        // No voice at all (an APK without the Accent-PC's ROMs, no firmware imported): the voice data is not installed.
        if (!SsiEngine.open(this)) { callback.error(TextToSpeech.ERROR_NOT_INSTALLED_YET); return }
        val s = SsiSettings.snapshot(this)

        // A request in a Blazie language other than English (Spanish, German, ..., Hungarian, Brazil's Portuguese)
        // gets that language's unit when it is here -- its country's first (SsiLanguages.voiceFor), the chosen voice
        // when it is one of them.  Otherwise the voice chosen in the settings wins unless the user turned that off:
        // a screen reader asks for a default once and sends it every time.
        val requested = SsiEngine.voiceByName(this, request.voiceName)
        val voice = SsiLanguages.ownLanguageVoice(request.language, s.voice, SsiEngine.voices(this), request.country)
            ?: when {
            s.overrideVoice -> SsiEngine.voiceFor(this, s.voice)
            else -> requested ?: SsiEngine.voiceFor(this, s.voice)
        }

        SsiEngine.withEngine {
            SsiEngine.configure(s)
            val rate = SsiNative.nativeSampleRate()
            if (callback.start(rate, AudioFormat.ENCODING_PCM_16BIT, 1) != TextToSpeech.SUCCESS) return@withEngine
            if (text.isBlank()) { callback.done(); return@withEngine }
            // Armed only now, when this utterance becomes the one a stop should end: a stop aimed at the previous
            // one can arrive while this one is being set up (TGSpeechBox's stale-stop guard).
            stopRequested = false
            // With the setting on, every word's start is a mark for the unit: where it has index marks of its own,
            // the framework is told which word is being spoken (rangeStart; TalkBack takes it, and apps that
            // highlight what is read).  Off by default: the marks can move a unit's pauses.
            val words = if (s.wordMarks) SsiWords.of(text) else emptyList()
            val started = SsiEngine.start(voice, text, s, request.speechRate, request.pitch,
                                          if (words.isEmpty()) null else IntArray(words.size) { words[it].utf8 })
            if (started < 0) {
                Log.w(TAG, "start failed: ${SsiNative.nativeError()}")
                callback.error(TextToSpeech.ERROR_SYNTHESIS)
                return@withEngine
            }
            if (started == 1) { callback.done(); return@withEngine }
            val size = minOf(8192, callback.maxBufferSize) and 1.inv()
            if (size <= 0) { SsiEngine.cancel(); callback.error(TextToSpeech.ERROR_OUTPUT); return@withEngine }
            val bytes = ByteArray(size)
            var total = 0
            var finished = false
            val reach = IntArray(2 * 32)
            while (!stopRequested) {
                val n = SsiEngine.pull(bytes)
                if (n <= 0) { finished = n == 0; break }
                // the words this audio reaches, each at its frame: told before the audio is handed on
                var k = SsiEngine.reached(reach)
                while (k > 0) {
                    for (i in 0 until k) {
                        val w = words.getOrNull(reach[2 * i]) ?: continue
                        callback.rangeStart(reach[2 * i + 1], w.start, w.end)
                    }
                    k = SsiEngine.reached(reach)
                }
                if (callback.audioAvailable(bytes, 0, n) != TextToSpeech.SUCCESS) break   // the framework let go
                total += n
            }
            // Stopped or abandoned: the unit drops the rest, so none of it reaches the next utterance.
            val loopEnd = android.os.SystemClock.elapsedRealtime()
            if (!finished) SsiEngine.cancel()
            val now = android.os.SystemClock.elapsedRealtime()
            val late = if (stopRequested) " ${now - stopAt} ms after the stop (the unit's cancel ${now - loopEnd} ms)" else ""
            // the request's rate and pitch (percent, 100 = normal): what TalkBack asks for, e.g. for a capital letter
            Log.i(TAG, "${voice.name}: ${total / 2} samples at $rate Hz, stopped=${!finished}$late, " +
                  "request rate ${request.speechRate} pitch ${request.pitch}")
            // A stop is an interruption, not a failure: screen readers treat an error as a failed utterance.
            callback.done()
        }
    }

    private companion object { const val TAG = "SsiTts" }
}
