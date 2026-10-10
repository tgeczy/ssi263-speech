// Raw JNI binding to libssi263speech.so (cpp/ssa_jni.c): the Aicom Accent SA -- its own 8085 firmware on MAME's 8085
// core -- the Braille Lite voices -- the unit's firmware on an emulated Z180 -- the GW Micro Speak-Out -- its firmware
// on MAME's V40 -- the Mockingboard -- Sweet Micro Systems' text-to-speech on a 6502 (Fake6502's instructions) -- and,
// in a build that has it, the Aicom Accent-mini, each driving the SSI-263 model; the same C the
// speech-dispatcher module and the NVDA add-ons' libraries are built from.  Not thread-safe: SsiEngine
// serialises every call except nativeStop, which only sets a flag.
package com.ssi263speech.tts

object SsiNative {
    init {
        System.loadLibrary("ssi263speech")
    }

    const val ENGLISH = 0                   // the Braille Lite, English (imported)
    const val SPANISH = 1                   // the Braille Lite, Spanish (imported)
    const val ACCENT_SA = 2                 // the Aicom Accent SA (built in)
    const val SPEAKOUT = 3                  // the GW Micro Speak-Out (imported)
    const val ACCENT_MINI = 4               // the Aicom Accent-mini (built in, when the build carries it)
    const val MOCKINGBOARD = 5              // the Mockingboard, Sweet Micro Systems' (imported)

    /** Open the engine on the folder holding the unit's files (once per process; later calls keep it). */
    external fun nativeOpen(dataDir: String): Boolean

    /** The Accent SA's ROMs (u2 64 KB, u3 and u4 32 KB each), copied into the open engine.  False on the wrong sizes. */
    external fun nativeAccentRoms(u2: ByteArray, u3: ByteArray, u4: ByteArray): Boolean

    /** The Accent-mini's SPKEMS.DVC, copied into the open engine.  False when this build has no Accent-mini. */
    external fun nativeAccentMini(dvc: ByteArray): Boolean

    /** This build carries the voice's engine (no engine need be open): every voice but the Accent-mini and the
     * Mockingboard always. */
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
     * tone, pack, runAhead, numbers (the Braille Lite's; numbers the Mockingboard's too); soTone, soJoin, soShort (the Speak-Out's).  requestRate,
     * requestPitch: the request's percentages (100 = normal), put on top by the C side (ssa_map.h, ssa_engine.h).
     * 0 when there is audio to pull, 1 when there is nothing to say, negative on failure. */
    external fun nativeStart(voice: Int, utf8: ByteArray, rate: Int, pitch: Int, tone: Int, volume: Int, pack: Int,
                             runAhead: Int, numbers: Int, soTone: Int, soJoin: Int, soShort: Int,
                             requestRate: Int, requestPitch: Int): Int

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

    /** Find the firmware in these bytes by its content: the Braille Lite ROM image, when it is a release on the list
     * (bl_firmware.c's), written to `out` as a .BNS -- [ENGLISH] or [SPANISH] -- or GW Micro's SPEAKOUT.HEX (Intel
     * HEX, the known sha256: ssa_import.c), written to `out` as it is -- [SPEAKOUT] -- or the Mockingboard's
     * mockingboard-tts-1.1.bin (its sha256), written to `out` as it is -- [MOCKINGBOARD]; the label in
     * [nativeImportError].  Else [FW_NONE] when there is no firmware in them, [FW_REFUSED] when it is another Blazie
     * unit's, [FW_UNKNOWN] when it is a Braille Lite 2000 release not on the list, [FW_OTHER_HEX] when it is an Intel
     * HEX file but not the Speak-Out's (damaged, or another), [FW_NOT_BUILT] when it is the Mockingboard's in a build
     * without that voice -- the reason in [nativeImportError]. */
    external fun nativeImportFirmware(data: ByteArray, out: String): Int

    /** The releases on the list, by label. */
    external fun nativeKnownFirmware(): Array<String>

    /** The state at `state` is the one [nativeMakeState] makes from the release at `bns`. */
    external fun nativeStateCheck(bns: String, state: String): Boolean

    /** Make the unit's state from its firmware, as the shipped one was made: 1, or 0 with the reason in
     * [nativeImportError].  Seconds long: never on the main thread. */
    external fun nativeMakeState(firmware: String, language: Int, out: String): Int

    /** How far [nativeMakeState] has come, 0..1; any thread. */
    external fun nativeStateProgress(): Double

    /** Any thread: [nativeMakeState] stops at its next report and fails with "cancelled". */
    external fun nativeStateCancel()

    /** Boot the voice's unit from the files in `dir` and speak the text: the samples (0 when it stays silent), or -1
     * with the reason in [nativeImportError]. */
    external fun nativeProbe(dir: String, voice: Int, utf8: ByteArray): Long

    external fun nativeImportError(): String
}
