// The one engine in this process, shared by the TTS service and the settings screen's preview: opens the native
// side on the imported unit files and the APK's Aicom ROMs, lists the voices -- every voice the NVDA add-ons have
// (Tomi, 0.7.5): the Aicom Accent SA always, the default until a Braille Lite is imported (Tomi, 2026-09-30); the
// Accent-mini when this build carries it; the Braille Lite voices, the GW Micro Speak-Out and the Mockingboard once
// their firmware is imported -- and owns the engine for one utterance at a time.  A stop from any thread reaches the render in progress
// (nativeStop); the synthesis thread then cancels, so the unit drops what it has not spoken and the next utterance
// starts clean -- as the speech-dispatcher module does after STOP.
package com.ssi263speech.tts

import android.content.Context
import android.util.Log
import java.util.Locale

object SsiEngine {
    data class VoiceInfo(val index: Int, val name: String, val label: String, val locale: Locale)

    // The names are kept for good: TalkBack stores the one it was given.
    private val ALL = listOf(
        VoiceInfo(SsiNative.ACCENT_SA, "en-US-accentsa", "Aicom Accent SA (English)", Locale("en", "US")),
        VoiceInfo(SsiNative.ACCENT_MINI, "en-US-accentmini", "Aicom Accent-mini (English)", Locale("en", "US")),
        VoiceInfo(SsiNative.ENGLISH, "en-US-braillelite", "Braille Lite 2000 (English)", Locale("en", "US")),
        VoiceInfo(SsiNative.SPANISH, "es-ES-braillelite", "Braille Lite 2000 (español)", Locale("es", "ES")),
        VoiceInfo(SsiNative.SPEAKOUT, "en-US-speakout", "GW Micro Speak-Out (English)", Locale("en", "US")),
        VoiceInfo(SsiNative.MOCKINGBOARD, "en-US-mockingboard", "Mockingboard (Sweet Micro Systems)",
                  Locale("en", "US")))

    private val lock = Any()
    @Volatile private var opened = false

    /** Open the native side on the unit's files and the Aicom ROMs.  False when no voice can speak. */
    fun open(ctx: Context): Boolean {
        if (opened) return true
        synchronized(lock) {
            if (opened) return true
            if (voices(ctx).isEmpty()) { Log.w("SsiEngine", "no voice: no Accent SA ROMs, no firmware imported"); return false }
            if (!SsiNative.nativeOpen(SsiData.dir(ctx).absolutePath)) return false
            val roms = SsiData.accentRoms(ctx)
            if (roms != null && !SsiNative.nativeAccentRoms(roms[0], roms[1], roms[2]))
                Log.e("SsiEngine", "the Accent SA's ROMs were refused")
            SsiData.accentMini(ctx)?.let { dvc ->
                if (!SsiNative.nativeAccentMini(dvc)) Log.e("SsiEngine", "the Accent-mini's SPKEMS.DVC was refused")
            }
            opened = true
            return true
        }
    }

    /** Let the native side go after an import or a removal: the next use opens it on the files now there.  Waits
     * for an utterance in progress. */
    fun reload() = synchronized(lock) {
        if (opened) {
            opened = false                  // a stop from another thread now leaves the engine alone
            SsiNative.nativeClose()
        }
    }

    /** The voices that can speak now: the Accents built in, and the imported Braille Lite voices, Speak-Out and
     * Mockingboard. */
    fun voices(ctx: Context): List<VoiceInfo> = ALL.filter { SsiData.has(ctx, it.index) }

    fun voiceByName(ctx: Context, name: String?): VoiceInfo? = voices(ctx).firstOrNull { it.name == name }

    /** That voice when it can speak; otherwise the first that can (the Accent SA). */
    fun voiceFor(ctx: Context, index: Int): VoiceInfo =
        voices(ctx).let { v -> v.firstOrNull { it.index == index } ?: v.firstOrNull() } ?: ALL[0]

    /** The voice when the user has chosen none: the Braille Lite once its English firmware is imported, else the
     * Accent SA (Tomi: the Accent SA is the default while no Braille Lite firmware is here). */
    fun defaultVoice(ctx: Context): Int =
        if (SsiData.has(ctx, SsiNative.ENGLISH)) SsiNative.ENGLISH else SsiNative.ACCENT_SA

    /** An English voice can speak: the Accents (built in), the imported Braille Lite or Speak-Out. */
    fun english(ctx: Context): Boolean = voices(ctx).any { it.index != SsiNative.SPANISH }

    fun spanish(ctx: Context): Boolean = SsiData.has(ctx, SsiNative.SPANISH)

    /** For a request in English: the chosen voice when it is an English one, else the first English voice. */
    fun englishVoiceFor(ctx: Context, index: Int): VoiceInfo =
        voiceFor(ctx, index).takeIf { it.index != SsiNative.SPANISH }
            ?: voices(ctx).firstOrNull { it.index != SsiNative.SPANISH } ?: ALL[0]

    /** Own the engine for one utterance: every native call but a stop happens inside. */
    fun <T> withEngine(block: () -> T): T = synchronized(lock) { block() }

    /** Boot the chosen voice's unit ahead of the first request, so that request does not wait for it. */
    fun warmUp(ctx: Context) {
        if (!open(ctx)) return
        val s = SsiSettings.snapshot(ctx)
        withEngine {
            configure(s)
            val rc = SsiNative.nativeLoad(voiceFor(ctx, s.voice).index)
            if (rc != 0) Log.w("SsiEngine", "warm-up: ${SsiNative.nativeError()}")
        }
    }

    /** Inside withEngine: the unit's boot settings (a change reboots the units on their next use). */
    fun configure(s: SsiSettings.Snapshot) =
        SsiNative.nativeConfigure(s.sampleRate, if (s.inflection) 1 else 0, s.whine)

    private fun bit(b: Boolean) = if (b) 1 else 0

    /** Inside withEngine: begin an utterance.  0 audio to pull, 1 nothing to say, negative on failure. */
    fun start(voice: VoiceInfo, text: String, s: SsiSettings.Snapshot, requestRate: Int, requestPitch: Int): Int =
        SsiNative.nativeStart(voice.index, text.toByteArray(Charsets.UTF_8), s.rate, s.pitch, s.tone, s.volume,
                              bit(s.shortPauses), bit(s.runAhead), bit(s.numbers), s.soTone, bit(s.soJoin),
                              bit(s.soShortPauses),
                              requestRate, requestPitch)

    /** Inside withEngine: the next PCM bytes; 0 at the end, -2 when stopped. */
    fun pull(out: ByteArray): Int = SsiNative.nativePull(out)

    /** Inside withEngine, after a stop or an abandoned utterance. */
    fun cancel() = SsiNative.nativeCancel()

    /** Any thread. */
    fun stop() { if (opened) SsiNative.nativeStop() }

    fun sampleRate(): Int = if (opened) SsiNative.nativeSampleRate() else 22050
}
