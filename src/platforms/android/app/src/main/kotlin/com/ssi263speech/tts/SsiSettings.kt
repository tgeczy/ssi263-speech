// The app's settings, in device-protected storage so the service can read them before the phone is unlocked.
// The scales are the NVDA drivers' (and the speech-dispatcher module's): rate and pitch 0-100 with 50 the unit's
// factory rate and pitch (the Braille Lite's 11 and 16, the Accent SA's 5 and 5, the Speak-Out's 5 and 3), volume
// 0-200 (100 = the desktop voices' level, for every voice), the Braille Lite's tone 0-26 (factory 7), short pauses,
// numbers read as words (on, as its NVDA driver's "Custom number processing") and run ahead (experimental, off), the
// Speak-Out's tone A-Z (0-25, factory I = 8), join phrases and short pauses (both
// on, as its NVDA driver), and the units' own boot settings.
package com.ssi263speech.tts

import android.content.Context
import android.content.SharedPreferences

object SsiSettings {
    private const val PREFS = "ssi263speech"
    const val RATE = "rate"
    const val PITCH = "pitch"
    const val TONE = "tone"
    const val VOLUME = "engine_volume"        // 0.7: 0-200, default DEFAULT_VOLUME (a new key: the old 0-100 default was quieter)
    const val DEFAULT_VOLUME = 150           // +3.5 dB over the desktop level (100): the loudest of 67 lines measured at -1.4 dBFS;
                                             // the Accent SA's loudest of test_volume_headroom.py's at -2.5 dBFS
    const val MAX_VOLUME = 200
    const val SHORT_PAUSES = "short_pauses"
    const val RUN_AHEAD = "run_ahead"          // the Braille Lite's, EXPERIMENTAL: off by default (NVDA's runAhead)
    const val NUMBERS = "numbers"              // "Read numbers as words", the Braille Lite's and the Mockingboard's:
                                               // on (NVDA's numberWords)
    const val SO_TONE = "speakout_tone"        // the Speak-Out's tone, 0-25 = A-Z
    const val SO_DEFAULT_TONE = 8              // I, the box's own
    const val SO_JOIN = "speakout_join"        // "Join phrases"
    const val SO_SHORT_PAUSES = "speakout_short_pauses"   // "Shorten pauses between sentences"
    const val INFLECTION = "inflection"
    const val WHINE = "whine"
    const val SAMPLE_RATE = "sample_rate"
    const val VOICE = "voice"
    const val OVERRIDE_VOICE = "override_voice"

    val SAMPLE_RATES = listOf(11025, 22050, 44100)

    fun prefs(ctx: Context): SharedPreferences =
        SsiData.protectedContext(ctx).getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /** Read together once per utterance.  voice: the one chosen, or SsiEngine.defaultVoice when none was. */
    data class Snapshot(val rate: Int = 50, val pitch: Int = 50, val tone: Int = 7, val volume: Int = DEFAULT_VOLUME,
                        val shortPauses: Boolean = true, val inflection: Boolean = true, val whine: Int = 0,
                        val sampleRate: Int = 22050, val voice: Int = SsiNative.ACCENT_SA,
                        val overrideVoice: Boolean = true, val runAhead: Boolean = false,
                        val soTone: Int = SO_DEFAULT_TONE, val soJoin: Boolean = true, val soShortPauses: Boolean = true,
                        val numbers: Boolean = true)

    fun snapshot(ctx: Context): Snapshot {
        val p = prefs(ctx)
        return Snapshot(
            p.getInt(RATE, 50).coerceIn(0, 100),
            p.getInt(PITCH, 50).coerceIn(0, 100),
            p.getInt(TONE, 7).coerceIn(0, 26),
            p.getInt(VOLUME, DEFAULT_VOLUME).coerceIn(0, MAX_VOLUME),
            p.getBoolean(SHORT_PAUSES, true),
            p.getBoolean(INFLECTION, true),
            p.getInt(WHINE, 0).coerceIn(0, 2),
            p.getInt(SAMPLE_RATE, 22050).takeIf { it in SAMPLE_RATES } ?: 22050,
            p.getInt(VOICE, SsiEngine.defaultVoice(ctx)).coerceIn(SsiNative.ENGLISH, SsiNative.MOCKINGBOARD_EARLY),
            p.getBoolean(OVERRIDE_VOICE, true),
            p.getBoolean(RUN_AHEAD, false),
            p.getInt(SO_TONE, SO_DEFAULT_TONE).coerceIn(0, 25),
            p.getBoolean(SO_JOIN, true),
            p.getBoolean(SO_SHORT_PAUSES, true),
            p.getBoolean(NUMBERS, true))
    }
}
