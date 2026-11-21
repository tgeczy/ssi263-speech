// The one engine in this process, shared by the TTS service and the settings screen's preview: opens the native
// side on the imported unit files and the APK's Aicom ROMs, lists the voices -- every voice the NVDA add-ons have
// (Tomi, 0.7.5): the Aicom Accent-PC always, the default until a Braille Lite is imported (Tomi, 2026-09-30); the
// Accent-mini when this build carries it; the Braille Lite voices (English, Spanish, German, Italian), the Slovak
// Braille 'n Speak 2000, the GW Micro Speak-Out and the Mockingboard once their firmware is imported; each voice's
// language what SsiLanguages tells Android -- and owns the engine for one utterance at a time.  A stop from any thread reaches the render in progress
// (nativeStop); the synthesis thread then cancels, so the unit drops what it has not spoken and the next utterance
// starts clean -- as the speech-dispatcher module does after STOP.
package com.ssi263speech.tts

import android.content.Context
import android.util.Log
import java.util.Locale

object SsiEngine {
    data class VoiceInfo(val index: Int, val name: String, val label: String, val locale: Locale)

    init { loadNativeIfThere() }           // the release voices (FirmwareImport.releases) come with the library

    // The names are kept for good: TalkBack stores the one it was given.
    private val SOUNDINGBOARD_VOICE =
        VoiceInfo(SsiNative.SOUNDINGBOARD, "en-US-soundingboard", "GW Micro Sounding Board (English)", Locale("en", "US"))
    private val SHIPPED = listOf(
        VoiceInfo(SsiNative.ACCENT_SA, "en-US-accentsa", "Aicom Accent-PC (English)", Locale("en", "US")),
        VoiceInfo(SsiNative.ACCENT_MINI, "en-US-accentmini", "Aicom Accent-mini (English)", Locale("en", "US")),
        VoiceInfo(SsiNative.ENGLISH, "en-US-braillelite", "Braille Lite 2000 (English)", Locale("en", "US")),
        VoiceInfo(SsiNative.SPANISH, "es-ES-braillelite", "Braille Lite 2000 (Spanish)", Locale("es", "ES")),
        VoiceInfo(SsiNative.SPEAKOUT, "en-US-speakout", "GW Micro Speak-Out (English)", Locale("en", "US")),
        // GW Micro's Sounding Board, the Speak-Out's voice on the card's own DOS drivers (imported only), listed right
        // under it whatever its index (SsiNative.SOUNDINGBOARD, after the release voices: Tomi, 2026-10-25)
        SOUNDINGBOARD_VOICE,
        VoiceInfo(SsiNative.MOCKINGBOARD, "en-US-mockingboard", "Mockingboard (Sweet Micro Systems)",
                  Locale("en", "US")),
        VoiceInfo(SsiNative.MOCKINGBOARD_EARLY, "en-US-mockingboard-early", "Mockingboard, early (Sweet Micro Systems)",
                  Locale("en", "US")),
        VoiceInfo(SsiNative.GERMAN, "de-DE-braillelite", "Braille Lite 2000 (German)", Locale("de", "DE")),
        VoiceInfo(SsiNative.ITALIAN, "it-IT-braillelite", "Braille Lite 2000 (Italian)", Locale("it", "IT")),
        VoiceInfo(SsiNative.SLOVAK, "sk-SK-braillenspeak", "Braille 'n Speak 2000 (Slovak)", Locale("sk", "SK")))

    /** Every voice: the shipped ones, then the release voices -- the library's table, in its order, each named by its
     * locale ("hu-HU-braillelite", "pt-BR-braillelite", "ar-braillelite") and listed by its language in English
     * ("Braille Lite 2000 (Hungarian)"): its own name in its own script ("русский") is lost on whichever voice reads
     * the list -- the English Braille Lite said "Braille Lite radio button" (Tomi). */
    private val ALL: List<VoiceInfo>
        get() = (if (FirmwareImport.CONTROL == "voice-order") SHIPPED - SOUNDINGBOARD_VOICE else SHIPPED) +
            FirmwareImport.releases.map { r ->
                val locale = SsiLanguages.locale(r.tag)
                VoiceInfo(r.voice, SsiLanguages.voiceTag(locale) + "-braillelite", "Braille Lite 2000 (${r.language})",
                          locale)
            } + (if (FirmwareImport.CONTROL == "voice-order") listOf(SOUNDINGBOARD_VOICE) else emptyList()) +
            AFTER_RELEASES

    /** The voices after the release table's: Automated Functions' SmarTalk, imported or built in (its S_TALK.EXE). */
    private val AFTER_RELEASES = listOf(
        VoiceInfo(SsiNative.SMARTALK, "en-US-smartalk", "SmarTalk (Automated Functions)", Locale("en", "US")))

    /** Every voice this app has, whether or not it can speak now (SsiLanguages: a language whose firmware is not here
     * is missing data, not unsupported). */
    val KNOWN: List<VoiceInfo> get() = ALL

    private val lock = Any()
    @Volatile private var opened = false

    /** Open the native side on the unit's files and the Aicom ROMs.  False when no voice can speak. */
    fun open(ctx: Context): Boolean {
        if (opened) return true
        synchronized(lock) {
            if (opened) return true
            if (voices(ctx).isEmpty()) { Log.w("SsiEngine", "no voice: no Accent-PC ROMs, no firmware imported"); return false }
            if (!SsiNative.nativeOpen(SsiData.dir(ctx).absolutePath)) return false
            val roms = SsiData.accentRoms(ctx)
            if (roms != null && !SsiNative.nativeAccentRoms(roms[0], roms[1], roms[2]))
                Log.e("SsiEngine", "the Accent-PC's ROMs were refused")
            SsiData.accentMini(ctx)?.let { dvc ->
                if (!SsiNative.nativeAccentMini(dvc)) Log.e("SsiEngine", "the Accent-mini's SPKEMS.DVC was refused")
            }
            for (voice in FirmwareImport.BUILT_IN) SsiData.builtIn(ctx, voice)?.let { bin ->
                val ok = if (voice == FirmwareImport.SMARTALK) SsiNative.nativeSmarTalk(bin)
                         else SsiNative.nativeMockingboard(voice, bin)
                if (!ok) Log.e("SsiEngine", "the built-in file of voice $voice was refused")
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

    /** That voice when it can speak; otherwise the first that can (the Accent-PC). */
    fun voiceFor(ctx: Context, index: Int): VoiceInfo =
        voices(ctx).let { v -> v.firstOrNull { it.index == index } ?: v.firstOrNull() } ?: ALL[0]

    /** The voice when the user has chosen none: the Braille Lite once its English firmware is imported, else the
     * Accent-PC (Tomi: the Accent-PC is the default while no Braille Lite firmware is here). */
    fun defaultVoice(ctx: Context): Int =
        if (SsiData.has(ctx, SsiNative.ENGLISH)) SsiNative.ENGLISH else SsiNative.ACCENT_SA

    /** A voice of that language ("en", "deu", ...) can speak now. */
    fun speaks(ctx: Context, lang: String): Boolean = SsiLanguages.voicesOf(lang, voices(ctx)).isNotEmpty()

    /** For a request in that language: the chosen voice when it speaks it, else the first voice that does; null when
     * none can (SsiLanguages.voiceFor). */
    fun voiceForLanguage(ctx: Context, lang: String?, index: Int, country: String? = null): VoiceInfo? =
        SsiLanguages.voiceFor(lang, index, voices(ctx), country)

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

    /** Who owns the utterance being spoken: the TTS service, or the settings screen's preview or export.  The
     * engine has one stop flag, which ends whichever utterance owns it at its next pull: a stop must be its owner's
     * (#51: the screen's preview stop, run by its onDestroy and Stop, cut a screen reader's utterance; a screen
     * reader's stop of an utterance waiting behind a preview cut the preview, and an export wrote nothing). */
    @Volatile private var owner: Any? = null

    // Test hook (the screen's --ez stopbreak true, test_engine_stop_owner.py's control): any stop ends whatever
    // utterance owns the engine, as it was
    @Volatile var stopBreak = false

    /** Inside withEngine: begin an utterance for `by` (the one whose stop ends it).  0 audio to pull, 1 nothing to
     * say, negative on failure. */
    fun start(by: Any, voice: VoiceInfo, text: String, s: SsiSettings.Snapshot, requestRate: Int, requestPitch: Int,
              marks: IntArray? = null): Int {
        owner = by
        return SsiNative.nativeStart(voice.index, text.toByteArray(Charsets.UTF_8), s.rate, s.pitch, s.tone, s.volume,
                              bit(s.shortPauses), bit(s.runAhead), bit(s.lineLift), bit(s.numbers), s.soTone,
                              bit(s.soJoin),
                              bit(s.soShortPauses),
                              requestRate, requestPitch, marks)
    }

    /** Inside withEngine, after a pull: the marks reached, as (mark, frame) pairs in `out`; how many pairs. */
    fun reached(out: IntArray): Int = SsiNative.nativeReached(out)

    /** Inside withEngine: the next PCM bytes; 0 at the end, -2 when stopped. */
    fun pull(out: ByteArray): Int = SsiNative.nativePull(out)

    /** Inside withEngine, after a stop or an abandoned utterance. */
    fun cancel() = SsiNative.nativeCancel()

    /** Any thread: end `by`'s utterance, if it is the one the engine is speaking; another's goes on. */
    fun stop(by: Any) { if (opened && (owner === by || stopBreak)) SsiNative.nativeStop() }

    fun sampleRate(): Int = if (opened) SsiNative.nativeSampleRate() else 22050
}
