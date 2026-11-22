// SsiLanguages on the JVM: what Android -- TalkBack, the system's TTS settings -- is told about each language, with
// and without each Blazie unit's firmware imported, from SsiEngine's own voice list.  A language whose firmware is
// not here must never be offered (missing data, below zero); one no voice speaks is not supported.
package com.ssi263speech.tts

import android.speech.tts.TextToSpeech
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SsiLanguagesTest {
    private companion object {
        // release voices as the library's table gives them (the app reads them from SsiNative): the tags with and
        // without a region that the system's languages must find
        val RELEASES = listOf(
            FirmwareImport.Release(10, "bl2_ar_1998_12", "BL2ARB.BNS", "Braille Lite 2000 (Arabic)", "Braille Lite 2000 (العربية)", "ar"),
            FirmwareImport.Release(11, "bl2_ar_ae_1998_12", "BL2UAE.BNS", "Braille Lite 2000 (Arabic, UAE)", "Braille Lite 2000 (العربية، الإمارات)", "ar-AE"),
            FirmwareImport.Release(12, "bl2_pt_br_1998_12", "BL2BPG.BNS", "Braille Lite 2000 (Brazilian Portuguese)", "Braille Lite 2000 (português do Brasil)", "pt-BR"),
            FirmwareImport.Release(13, "bl2_pt_1998_12", "BL2POR.BNS", "Braille Lite 2000 (Portuguese)", "Braille Lite 2000 (português)", "pt"),
            FirmwareImport.Release(14, "bl2_he_1999_07", "BL2HEB.BNS", "Braille Lite 2000 (Hebrew)", "Braille Lite 2000 (עברית)", "he"),
            FirmwareImport.Release(15, "bl2_hu_1999_07", "BL2HUN.BNS", "Braille Lite 2000 (Hungarian)", "Braille Lite 2000 (magyar)", "hu"),
            FirmwareImport.Release(16, "bl2_no_1999_07", "BL2NRW.BNS", "Braille Lite 2000 (Norwegian)", "Braille Lite 2000 (norsk)", "no"))
    }

    init { FirmwareImport.releases = RELEASES }

    private val known = SsiEngine.KNOWN

    /** The voices that can speak with nothing imported (the Accents, the Mockingboards' built-in copies) plus these. */
    private fun with(vararg imported: Int) = known.filter {
        it.index in setOf(SsiNative.ACCENT_PC, SsiNative.ACCENT_MINI, SsiNative.MOCKINGBOARD,
                          SsiNative.MOCKINGBOARD_EARLY) || it.index in imported
    }

    private fun avail(lang: String?, country: String?, voices: List<SsiEngine.VoiceInfo>) =
        SsiLanguages.availability(lang, country, voices, known)

    @Test fun nothingImportedOffersEnglishAlone() {
        val v = with()
        assertEquals(TextToSpeech.LANG_COUNTRY_AVAILABLE, avail("eng", "USA", v))
        assertEquals(TextToSpeech.LANG_AVAILABLE, avail("eng", "GBR", v))
        assertEquals(TextToSpeech.LANG_AVAILABLE, avail("en", "", v))
        for (lang in listOf("spa", "deu", "ita", "slk", "de", "it", "sk", "es", "ger", "slo"))
            assertEquals(lang, TextToSpeech.LANG_MISSING_DATA, avail(lang, "", v))
        assertEquals(TextToSpeech.LANG_NOT_SUPPORTED, avail("fra", "FRA", v))
        assertEquals(TextToSpeech.LANG_NOT_SUPPORTED, avail(null, null, v))
        assertEquals(listOf("eng-USA"), SsiLanguages.tags(v))
        assertNull(SsiLanguages.voiceFor("deu", SsiNative.GERMAN, v))
    }

    @Test fun eachBlazieLanguageOnceItsUnitIsImported() {
        // (voice, ISO 639-2/T, ISO 3166 alpha-3, and the two-letter codes some callers send)
        data class Lang(val voice: Int, val lang: String, val country: String, val lang2: String, val country2: String)
        for ((voice, lang, country, lang2, country2) in listOf(Lang(SsiNative.GERMAN, "deu", "DEU", "de", "DE"),
                Lang(SsiNative.ITALIAN, "ita", "ITA", "it", "IT"), Lang(SsiNative.SLOVAK, "slk", "SVK", "sk", "SK"),
                Lang(SsiNative.SPANISH, "spa", "ESP", "es", "ES"))) {
            val v = with(voice)
            assertEquals(lang, TextToSpeech.LANG_COUNTRY_AVAILABLE, avail(lang, country, v))
            assertEquals(lang, TextToSpeech.LANG_COUNTRY_AVAILABLE, avail(lang2, country2, v))
            assertEquals(lang, TextToSpeech.LANG_AVAILABLE, avail(lang, "", v))
            assertEquals(setOf("eng-USA", "$lang-$country"), SsiLanguages.tags(v).toSet())
            assertEquals(voice, SsiLanguages.voiceFor(lang, SsiNative.ACCENT_PC, v)?.index)
            // the others stay missing: one unit's firmware offers its language alone
            for (other in listOf("spa", "deu", "ita", "slk").filter { it != lang })
                assertEquals("$lang, then $other", TextToSpeech.LANG_MISSING_DATA, avail(other, "", v))
        }
        assertEquals(TextToSpeech.LANG_AVAILABLE, avail("deu", "AUT", with(SsiNative.GERMAN)))
        assertEquals(TextToSpeech.LANG_COUNTRY_AVAILABLE, avail("ger", "DEU", with(SsiNative.GERMAN)))
        assertEquals(TextToSpeech.LANG_COUNTRY_AVAILABLE, avail("slo", "SK", with(SsiNative.SLOVAK)))
    }

    @Test fun aRequestInABlazieLanguageGetsItsUnitAndEnglishTheChosenVoice() {
        val v = with(SsiNative.ENGLISH, SsiNative.GERMAN, SsiNative.SLOVAK)
        assertEquals(SsiNative.GERMAN, SsiLanguages.ownLanguageVoice("deu", SsiNative.ENGLISH, v)?.index)
        assertEquals(SsiNative.SLOVAK, SsiLanguages.ownLanguageVoice("sk", SsiNative.ACCENT_PC, v)?.index)
        assertNull(SsiLanguages.ownLanguageVoice("ita", SsiNative.ENGLISH, v))        // not imported
        assertNull(SsiLanguages.ownLanguageVoice("eng", SsiNative.GERMAN, v))         // English: the chosen voice
        assertNull(SsiLanguages.ownLanguageVoice(null, SsiNative.GERMAN, v))
        // English's default: the chosen voice when it is English, else the first English one -- never German
        assertEquals(SsiNative.ENGLISH, SsiLanguages.voiceFor("eng", SsiNative.ENGLISH, v)?.index)
        assertEquals(SsiNative.ACCENT_PC, SsiLanguages.voiceFor("eng", SsiNative.GERMAN, v)?.index)
    }

    @Test fun theCurrentLanguageIsTheChosenVoices() {
        fun of(index: Int) = known.first { it.index == index }
        assertArrayEquals(arrayOf("deu", "DEU", ""), SsiLanguages.language(of(SsiNative.GERMAN)))
        assertArrayEquals(arrayOf("ita", "ITA", ""), SsiLanguages.language(of(SsiNative.ITALIAN)))
        assertArrayEquals(arrayOf("slk", "SVK", ""), SsiLanguages.language(of(SsiNative.SLOVAK)))
        assertArrayEquals(arrayOf("eng", "USA", ""), SsiLanguages.language(of(SsiNative.ACCENT_PC)))
    }

    @Test fun everyVoiceIsListedOnceWithAnIndexBelowVoices() {
        // the shipped ones and the release voices from 0, then the Sounding Board (0.8.5) at SSA_VOICES -- right after
        // the library's release voices (ssa_engine.c checks that when compiled; this fake lists fewer) -- and the
        // SmarTalk after it: no index moved
        assertEquals(SsiNative.RELEASE_FIRST + RELEASES.size + 2, known.size)
        val before = known.filter { it.index != SsiNative.SOUNDINGBOARD && it.index != SsiNative.SMARTALK }
        assertEquals((0 until SsiNative.RELEASE_FIRST + RELEASES.size).toSet(), before.map { it.index }.toSet())
        assertTrue(known.all { it.index < SsiNative.ALL_VOICES })
        assertEquals(SsiNative.VOICES, SsiNative.SOUNDINGBOARD)
        assertEquals(SsiNative.SOUNDINGBOARD + 1, SsiNative.SMARTALK)
        assertEquals(SsiNative.SMARTALK + 1, SsiNative.ALL_VOICES)
        assertEquals("en-US-soundingboard", known.single { it.index == SsiNative.SOUNDINGBOARD }.name)
        assertEquals("en-US-smartalk", known.single { it.index == SsiNative.SMARTALK }.name)
        assertEquals(known.size, known.map { it.name }.toSet().size)
    }

    @Test fun theSoundingBoardIsListedRightUnderTheSpeakOut() {
        // the voice list and Settings' firmware list, whatever the indexes (Tomi, 2026-10-25); the control
        // -Pssi263ImportBreak=voice-order lists it after the release voices again
        val voices = known.map { it.index }
        assertEquals(SsiNative.SOUNDINGBOARD, voices[voices.indexOf(SsiNative.SPEAKOUT) + 1])
        val imported = FirmwareImport.imported()
        assertEquals(FirmwareImport.SOUNDINGBOARD, imported[imported.indexOf(FirmwareImport.SPEAKOUT) + 1])
        assertEquals(SsiNative.SMARTALK, voices.last())
    }

    // ---- the release voices (the library's table) --------------------------------------------------------------

    @Test fun aReleaseVoiceIsNamedByItsLocale() {
        fun of(index: Int) = known.first { it.index == index }
        assertEquals("hu-HU-braillelite", of(15).name)
        // listed in English: a name in its own script is lost on the voice that reads the list (Tomi: Russian)
        assertEquals("Braille Lite 2000 (Hungarian)", of(15).label)
        assertEquals("Braille Lite 2000 (Arabic, UAE)", of(11).label)
        assertTrue(known.filter { it.index >= SsiNative.RELEASE_FIRST }.all { v -> v.label.all { it.code < 128 } })
        assertEquals("pt-BR-braillelite", of(12).name)
        assertEquals("pt-PT-braillelite", of(13).name)
        assertEquals("ar-braillelite", of(10).name)
        assertEquals("ar-AE-braillelite", of(11).name)
        assertEquals("nb-NO-braillelite", of(16).name)
        assertArrayEquals(arrayOf("hun", "HUN", ""), SsiLanguages.language(of(15)))
    }

    @Test fun aRequestsCountryPicksItsRelease() {
        val v = with(10, 11, 12, 13, 14, 15, 16)
        // Portugal's and Brazil's Portuguese, whichever the chosen voice is
        assertEquals(12, SsiLanguages.ownLanguageVoice("por", SsiNative.ENGLISH, v, "BRA")?.index)
        assertEquals(12, SsiLanguages.ownLanguageVoice("pt", 13, v, "BR")?.index)
        assertEquals(13, SsiLanguages.ownLanguageVoice("pt", 12, v, "PT")?.index)
        assertEquals(13, SsiLanguages.ownLanguageVoice("por", SsiNative.ENGLISH, v, "PRT")?.index)
        // a country neither has: the chosen one when it speaks Portuguese, else the first
        assertEquals(12, SsiLanguages.ownLanguageVoice("pt", 12, v, "AGO")?.index)
        assertEquals(12, SsiLanguages.ownLanguageVoice("pt", SsiNative.ENGLISH, v, "")?.index)
        // the UAE's Arabic for the UAE, the other everywhere else
        assertEquals(11, SsiLanguages.ownLanguageVoice("ara", SsiNative.ENGLISH, v, "ARE")?.index)
        assertEquals(10, SsiLanguages.ownLanguageVoice("ar", SsiNative.ENGLISH, v, "SA")?.index)
        assertEquals(TextToSpeech.LANG_COUNTRY_AVAILABLE, avail("ara", "ARE", v))
        assertEquals(TextToSpeech.LANG_AVAILABLE, avail("ara", "EGY", v))
    }

    @Test fun norwegianAndHebrewByEveryCode() {
        val v = with(14, 16)
        for (lang in listOf("no", "nb", "nor", "nob", "nb-NO", "no_NO"))
            assertEquals(lang, 16, SsiLanguages.ownLanguageVoice(lang, SsiNative.ENGLISH, v, "NO")?.index)
        assertEquals(TextToSpeech.LANG_COUNTRY_AVAILABLE, avail("nor", "NOR", v))
        for (lang in listOf("he", "iw", "heb"))
            assertEquals(lang, 14, SsiLanguages.ownLanguageVoice(lang, SsiNative.ENGLISH, v, "IL")?.index)
        // and missing data, not unsupported, until each is imported
        assertEquals(TextToSpeech.LANG_MISSING_DATA, avail("hun", "HUN", with()))
        assertEquals(TextToSpeech.LANG_MISSING_DATA, avail("no", "NO", with()))
    }
}
