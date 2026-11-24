// Raw JNI binding to libssi263speech.so (cpp/ssa_jni.c): the Aicom Accent-PC -- its own 8085 firmware on MAME's 8085
// core -- the Braille Lite voices -- the unit's firmware on an emulated Z180 -- the GW Micro Speak-Out -- its firmware
// on MAME's V40 -- the Mockingboard -- Sweet Micro Systems' text-to-speech on a 6502 (Fake6502's instructions) -- and,
// in a build that has it, the Aicom Accent-mini, each driving the SSI-263 model; the same C the
// speech-dispatcher module and the NVDA add-ons' libraries are built from.  Not thread-safe: SsiEngine
// serialises every call except nativeStop, which only sets a flag.
package com.ssi263speech.tts

object SsiNative {
    init {
        System.loadLibrary("ssi263speech")
        // the release voices, from the library's release table: the one list the app's are made from
        FirmwareImport.releases = nativeReleaseVoices().map { row ->
            val c = row.split('\t')
            FirmwareImport.Release(c[0].toInt(), c[1], c[2], c[3], c[4], c[5])
        }
        // ... and the releases no voice speaks, for the unit screen alone
        FirmwareImport.units = nativeUnitReleases().map { row ->
            val c = row.split('\t')
            FirmwareImport.UnitRelease(c[0], c[2], c[1])
        }
    }

    /** Loads the library (and with it the release voices), if nothing has yet. */
    fun load() {}

    const val ENGLISH = 0                  // the Braille Lite, English (imported)
    const val SPANISH = 1                   // the Braille Lite, Spanish (imported)
    const val ACCENT_PC = 2                 // the Aicom Accent-PC (built in)
    const val SPEAKOUT = 3                  // the GW Micro Speak-Out (imported)
    const val ACCENT_MINI = 4               // the Aicom Accent-mini (built in, when the build carries it)
    const val MOCKINGBOARD = 5              // the Mockingboard, Sweet Micro Systems' (built in; an import replaces it)
    const val MOCKINGBOARD_EARLY = 6        // the Mockingboard, early: disk 1's text-to-speech (the same way)
    const val GERMAN = 7                    // the Braille Lite, German (imported; 0.8)
    const val ITALIAN = 8                   // the Braille Lite, Italian (imported)
    const val SLOVAK = 9                    // the Braille 'n Speak 2000, Slovak (imported)
    const val RELEASE_FIRST = 10            // ssa_engine.h's SSA_RELEASE_FIRST: the release voices (Hungarian ...) on
    const val VOICES = 27                   // ssa_engine.h's SSA_VOICES: the release voices end below it (10 + 17)
    const val SOUNDINGBOARD = 27            // ssa_engine.h's SSA_SOUNDINGBOARD: GW Micro's Sounding Board (imported;
                                            // 0.8.5), after the release voices so no saved voice moves
    const val SMARTALK = 28                 // ssa_engine.h's SSA_SMARTALK: Automated Functions' SmarTalk (imported),
                                            // after the Sounding Board
    const val SYNPHONIX = 29                // ssa_engine.h's SSA_SYNPHONIX: the Artic SynPhonix (imported or built in;
                                            // 0.9.5), after the SmarTalk
    const val ALL_VOICES = 30               // ssa_engine.h's SSA_ALL_VOICES: every index is below it

    /** Open the engine on the folder holding the unit's files (once per process; later calls keep it). */
    external fun nativeOpen(dataDir: String): Boolean

    /** The Accent-PC's ROMs (u2 64 KB, u3 and u4 32 KB each), copied into the open engine.  False on the wrong sizes. */
    external fun nativeAccentRoms(u2: ByteArray, u3: ByteArray, u4: ByteArray): Boolean

    /** The Accent-mini's SPKEMS.DVC, copied into the open engine.  False when this build has no Accent-mini. */
    external fun nativeAccentMini(dvc: ByteArray): Boolean

    /** A Mockingboard voice's built-in file (the APK's assets/sweet-micro), copied into the open engine; an imported
     * copy in the data folder wins.  False when it is not that voice's file or this build has no Mockingboard. */
    external fun nativeMockingboard(voice: Int, bin: ByteArray): Boolean

    /** The SmarTalk's built-in S_TALK.EXE (the APK's assets/smartalk), copied into the open engine; an imported copy
     * wins.  False when it is not S_TALK.EXE 1.21 (by its sha256) or this build has no SmarTalk. */
    external fun nativeSmarTalk(exe: ByteArray): Boolean

    /** The SynPhonix's built-in VEST.COM (the APK's assets/synphonix), copied into the open engine; an imported copy
     * wins.  False when it is not the repository's copy (by its sha256) or this build has no SynPhonix. */
    external fun nativeSynPhonix(com: ByteArray): Boolean

    /** This build carries the voice's engine (no engine need be open): every voice but the Accent-mini and the
     * Mockingboards always. */
    external fun nativeVoiceBuilt(voice: Int): Boolean

    /** The voice can speak: its files are in the data folder, or its ROMs were handed over. */
    external fun nativeHasVoice(voice: Int): Boolean

    /** The settings a unit boots with: sample rate (11025, 22050, 44100), inflection 0/1, whine 0 off, 1 hiss,
     * 2 whine.  A change shuts the booted units down; the next use boots them again. */
    external fun nativeConfigure(sampleRate: Int, inflection: Int, whine: Int)

    external fun nativeSampleRate(): Int

    /** Boot the voice's unit now, if it is not yet.  0, or negative with the reason in [nativeError]. */
    external fun nativeLoad(voice: Int): Int

    external fun nativeError(): String

    /** Begin an utterance (UTF-8).  The app's settings on the NVDA drivers' scales: rate, pitch, volume (every voice);
     * tone, pack, runAhead, lineLift, numbers (the Braille Lite's; numbers the Mockingboard's too); soTone, soJoin, soShort (the Speak-Out's).  requestRate,
     * requestPitch: the request's percentages (100 = normal), put on top by the C side (ssa_map.h, ssa_engine.h).
     * marks: UTF-8 offsets into the text the service wants to be told of when speech reaches them (its words'
     * starts), or null; nativeReached gives them back.
     * 0 when there is audio to pull, 1 when there is nothing to say, negative on failure. */
    external fun nativeStart(voice: Int, utf8: ByteArray, rate: Int, pitch: Int, tone: Int, volume: Int, pack: Int,
                             runAhead: Int, lineLift: Int, numbers: Int, soTone: Int, soJoin: Int, soShort: Int,
                             requestRate: Int, requestPitch: Int, marks: IntArray?): Int

    /** The marks the audio pulled so far has reached and that were not given yet, as pairs in `out`: the mark's
     * number among nativeStart's, and its frame in the utterance's audio.  The number of pairs.  Ask after every
     * pull, before handing its audio on.  Only a voice whose unit has index marks of its own reaches any. */
    external fun nativeReached(out: IntArray): Int

    /** Fill `out` with 16-bit little-endian PCM: the byte count, 0 when the utterance is over, -2 when stopped. */
    external fun nativePull(out: ByteArray): Int

    /** Any thread: a pull in progress returns -2 before its next block. */
    external fun nativeStop()

    /** The synthesis thread, after a stop: the unit drops what it has not spoken, so the next utterance is clean. */
    external fun nativeCancel()

    /** Let the engine go, so the next [nativeOpen] reads the unit's files afresh (after an import or a removal). */
    external fun nativeClose()

    // ---- the firmware import (bl_firmware.h, ssa_import.h, bl_state.h, ssa_probe) ----------------------------------

    const val FW_NONE = -1
    const val FW_REFUSED = -2
    const val FW_UNKNOWN = -4
    const val FW_OTHER_HEX = -5
    const val FW_NOT_BUILT = -6
    const val FW_OTHER_DISK = -7
    const val FW_RELEASE = 100              // ssa_import.h's SSA_FW_RELEASE: + a row no voice speaks (the unit screen's)

    /** Find the firmware in these bytes by its content: a Blazie ROM image, when it is a release on the list
     * (bl_firmware.c's), written to `out` as a .BNS -- its voice: [ENGLISH], [SPANISH], [GERMAN], [ITALIAN] or [SLOVAK]
     * (ssa_import.c maps the list's languages to these) -- or GW Micro's SPEAKOUT.HEX (Intel
     * HEX, the known sha256: ssa_import.c), written to `out` as it is -- [SPEAKOUT] -- or the Mockingboard's
     * mockingboard-tts-1.1.bin (its sha256), written to `out` as it is, or made from the Developers Toolkit's disk
     * image (mb_dsk.h) -- [MOCKINGBOARD], or the early one, [MOCKINGBOARD_EARLY]; the label in [nativeImportError].  Else [FW_NONE] when there is no firmware
     * in them, [FW_REFUSED] when it is another Blazie unit's (or a listed release in a language this app has no voice
     * for), [FW_UNKNOWN] when it is a Braille Lite 2000 release not
     * on the list, [FW_OTHER_HEX] when it is an Intel HEX file but not the Speak-Out's (damaged, or another),
     * [FW_NOT_BUILT] when it is the Mockingboard's in a build without that voice, [FW_OTHER_DISK] when it is a 140 KB
     * disk image but not the toolkit's -- the reason in [nativeImportError].  [FW_RELEASE] plus a release table row
     * no voice speaks (the Braille 'n Speak 640, Japanese, the betas): written to `out`, kept for the unit screen
     * alone ([nativeReleaseRow] names it). */
    external fun nativeImportFirmware(data: ByteArray, out: String): Int

    /** A release table row: "<id>\t<name>" (its file under units/ is <id>.BNS), or null. */
    external fun nativeReleaseRow(row: Int): String?

    /** The releases on the list, by label: those a voice speaks, the release voices' among them. */
    external fun nativeKnownFirmware(): Array<String>

    /** The release voices (ssa_jni.c's): per voice "<index>\t<id>\t<file>\t<name>\t<voice name>\t<tag>". */
    external fun nativeReleaseVoices(): Array<String>

    /** The releases no voice speaks, kept for the unit screen alone (ssa_jni.c's): per row "<id>\t<file>\t<name>". */
    external fun nativeUnitReleases(): Array<String>

    /** The state at `state` is the one [nativeMakeState] makes from the release at `bns`. */
    external fun nativeStateCheck(bns: String, state: String): Boolean

    /** Make a Blazie voice's unit state from its firmware, as the shipped one was made (the voice's language's
     * recipe: ssa_blazie_recipe): 1, or 0 with the reason in [nativeImportError].  Seconds long: never on the main
     * thread. */
    external fun nativeMakeState(firmware: String, voice: Int, out: String): Int

    /** How far [nativeMakeState] has come, 0..1; any thread. */
    external fun nativeStateProgress(): Double

    /** Any thread: [nativeMakeState] stops at its next report and fails with "cancelled". */
    external fun nativeStateCancel()

    /** Boot the voice's unit from the files in `dir` and speak the text: the samples (0 when it stays silent), or -1
     * with the reason in [nativeImportError]. */
    external fun nativeProbe(dir: String, voice: Int, utf8: ByteArray): Long

    external fun nativeImportError(): String
}

/** SsiNative.load, from the objects the JVM tests use too (SsiEngine, SsiData): there, with no library, nothing is
 * loaded and the tests give FirmwareImport.releases their own list.  On a phone a library that fails to load is logged
 * here and fails again at the first call that needs it.  Outside the object: touching it is what loads the library. */
fun loadNativeIfThere() {
    try { SsiNative.load() } catch (e: Throwable) {
        try { android.util.Log.w("SsiNative", "no native library: ${e.message}") } catch (t: Throwable) {}
    }
}