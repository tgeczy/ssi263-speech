// The firmware a person brings, and what is in it: the Braille Lite's and the Slovak Braille 'n Speak 2000's
// (Blazie's: English, Spanish, German, Italian, Slovak), the Speak-Out's (GW Micro's
// SPEAKOUT.HEX) and the Mockingboard's (Sweet Micro Systems' text-to-speech: mockingboard-tts-1.1.bin and the early
// mockingboard-tts-early.bin).  The app carries neither of the first two, so each user imports their own copy, the
// way outspoken and Panthera take their engine data; the GitHub APK carries both Mockingboard files (Tomi,
// 2026-10-10), and an import of either replaces its copy -- one "Import firmware" for all, told apart by content.  Nothing here touches Android, so the JVM tests (src/test)
// run it as it is; the bytes themselves are judged by the native side (bl_firmware.c, ssa_import.c) behind
// [Identify], by content and never by name.
//
// Only Braille Lite releases on the native side's list are taken -- each one booted and heard before it was listed --
// only the Speak-Out's known SPEAKOUT.HEX (its sha256; Tomi, 0.7.5: "we just need to accept the .hex firmware"), only
// the Mockingboard's two known files (their sha256; from themselves or a disk image), and never a unit's state:
// the app always makes its own from the firmware (Tomi, 2026-09-30).
package com.ssi263speech.tts

import java.io.ByteArrayInputStream
import java.io.File
import java.io.IOException
import java.util.zip.ZipInputStream

object FirmwareImport {
    const val ENGLISH = 0                   // the Braille Lite, English (SsiNative.ENGLISH)
    const val SPANISH = 1                   // the Braille Lite, Spanish (SsiNative.SPANISH)
    const val SPEAKOUT = 3                  // the GW Micro Speak-Out (SsiNative.SPEAKOUT)
    const val MOCKINGBOARD = 5              // the Mockingboard (SsiNative.MOCKINGBOARD)
    const val MOCKINGBOARD_EARLY = 6        // the Mockingboard, early (SsiNative.MOCKINGBOARD_EARLY)
    const val GERMAN = 7                    // the Braille Lite, German (SsiNative.GERMAN; 0.8)
    const val ITALIAN = 8                   // the Braille Lite, Italian (SsiNative.ITALIAN)
    const val SLOVAK = 9                    // the Braille 'n Speak 2000, Slovak (SsiNative.SLOVAK)
    const val SOUNDINGBOARD = 27            // GW Micro's Sounding Board (SsiNative.SOUNDINGBOARD; 0.8.5): two files --
    const val SB_LOAD = 90                  // ... SBLOAD.COM (ssa_import.h SSA_FW_SBLOAD), and
    const val SB_COM = 91                   // ... SB.COM (SSA_FW_SB): found apart, imported only together
    const val SMARTALK = 28                 // Automated Functions' SmarTalk (SsiNative.SMARTALK): its S_TALK.EXE
    const val NONE = -1                     // no firmware in the bytes
    const val REFUSED = -2                  // Blazie firmware, but another unit's: not one the voice can run
    const val UNKNOWN = -4                  // Braille Lite 2000 firmware, but not a release on the list
    const val OTHER_HEX = -5                // an Intel HEX file, but not the Speak-Out's SPEAKOUT.HEX (damaged, another)
    const val NOT_BUILT = -6                // the Mockingboard's file, but this copy of the app has no Mockingboard
    const val OTHER_DISK = -7               // a 140 KB Apple II disk image, but not one of the two Mockingboard ones
    const val UNIT_RELEASE = 100            // + a release table row no voice speaks (SsiNative.FW_RELEASE): the 640,
                                            // Japanese, the betas -- kept for the Braille Lite unit screen alone

    /** A release no voice speaks yet, kept for the unit screen alone (Apple's units/ the same): `id` names its file
     * there, `name` is the release in English ("Braille 'n Speak 640 (English)"). */
    class UnitRelease(val id: String, val name: String, val bring: String = "") {
        /** Where it is kept in the unit folder: the unit screen's catalog (emu_catalog.c) finds it one folder down;
         * no state is made -- the unit screen makes its own on first start. */
        val file get() = "units/$id.BNS"
    }

    fun isUnitRelease(found: Int) = found >= UNIT_RELEASE

    /** The files each imported voice needs, by its index (ssa_engine.c's): a Blazie unit's firmware and the state
     * made from it -- 0.8's under the NVDA add-on's flat names, the German and Italian update disks both calling theirs
     * Lite2000.BNS --; the Speak-Out's HEX; the Mockingboard's file. */
    val FILES = mapOf(ENGLISH to listOf("BL2ENG.BNS", "bl2_2003_warm.state"),
                      SPANISH to listOf("BL2SPA.BNS", "bl2spa_fresh.state"),
                      GERMAN to listOf("Lite2000_de.BNS", "lite2000_de_fresh.state"),
                      ITALIAN to listOf("Lite2000_it.BNS", "lite2000_it_fresh.state"),
                      SLOVAK to listOf("BS2SLL.BNS", "bs2sll_fresh.state"),
                      SPEAKOUT to listOf("SPEAKOUT.HEX"),
                      MOCKINGBOARD to listOf("mockingboard-tts-1.1.bin"),
                      MOCKINGBOARD_EARLY to listOf("mockingboard-tts-early.bin"),
                      SOUNDINGBOARD to listOf("SBLOAD.COM", "SB.COM"),
                      SMARTALK to listOf("S_TALK.EXE"))
    /** The imported voices, in the order an import takes them and Settings lists them: the Sounding Board right
     * after the Speak-Out (Tomi, 2026-10-25), whatever its index. */
    val IMPORTED = listOf(ENGLISH, SPANISH, GERMAN, ITALIAN, SLOVAK, SPEAKOUT, SOUNDINGBOARD, MOCKINGBOARD,
                          MOCKINGBOARD_EARLY, SMARTALK)
    /** The Blazie units: a state made from each one's firmware. */
    val BLAZIE = setOf(ENGLISH, SPANISH, GERMAN, ITALIAN, SLOVAK)
    /** The voices whose firmware is taken as it is, with no state to make. */
    val NO_STATE = setOf(SPEAKOUT, MOCKINGBOARD, MOCKINGBOARD_EARLY, SOUNDINGBOARD, SMARTALK)
    /** A voice of two files (the Sounding Board's drivers): each file's code, in [FILES]' order. */
    val PARTS = mapOf(SOUNDINGBOARD to listOf(SB_LOAD, SB_COM))
    const val SOUNDINGBOARD_LABEL = "GW Micro Sounding Board 2.7: SBLOAD.COM and SB.COM"
    /** The voices the APK also carries a copy of (the Mockingboards' in assets/sweet-micro, the SmarTalk's in
     * assets/smartalk: SsiData.BUILT_IN_DIR): an import replaces it. */
    val BUILT_IN = setOf(MOCKINGBOARD, MOCKINGBOARD_EARLY, SMARTALK)

    /** A release voice: a Braille Lite 2000 language update the release table names a voice for (bl_languages.txt:
     * Hungarian, Turkish, ...), its index ssa_engine.h's from SSA_RELEASE_FIRST.  `id` names its state, `file` is the
     * file a person brings (the firmware list's), `name` the release in English ("Braille Lite 2000 (Hungarian)"),
     * `voiceName` the voice's (the table's vname, "Braille Lite 2000 (Hungarian)"), `tag` its BCP 47 language. */
    class Release(val voice: Int, val id: String, val file: String, val name: String, val voiceName: String,
                  val tag: String) {
        val files get() = listOf(file, "$id.state")
        /** The language in English, from the name: "Hungarian", "Arabic, UAE", "French" (the BL2FRE release's). */
        val language get() = name.substringAfter('(').substringBeforeLast(')').replace(Regex(", BL2[A-Z0-9]+$"), "")
    }

    /** The release voices, from the library's release table -- never a list kept here: set by SsiNative when the
     * library loads (nativeReleaseVoices), by the JVM tests to a list of their own. */
    @Volatile var releases: List<Release> = emptyList()

    fun release(voice: Int): Release? = releases.firstOrNull { it.voice == voice }

    /** The releases no voice speaks, for the unit screen alone, from the same table (nativeUnitReleases): Setup names
     * them behind their own button, `bring` being the file a person brings. */
    @Volatile var units: List<UnitRelease> = emptyList()

    /** The files an imported voice needs ([FILES], a release voice's .BNS and state); null for a voice with none. */
    fun files(voice: Int): List<String>? = FILES[voice] ?: release(voice)?.files

    /** The file a person brings for each imported voice, for the firmware list (the content, not the name, is what
     * the import goes by); a release voice's is the table's. */
    private val BRING = mapOf(ENGLISH to "BL2ENG.BNS", SPANISH to "BL2SPA.BNS", GERMAN to "Lite2000.BNS (German)",
                              ITALIAN to "Lite2000.BNS (Italian)", SLOVAK to "BS2SLL.BNS", SPEAKOUT to "SPEAKOUT.HEX",
                              MOCKINGBOARD to "the Mockingboard Developers Toolkit disk",
                              MOCKINGBOARD_EARLY to "Mockingboard disk 1",
                              SOUNDINGBOARD to "GW Micro's sb.zip (SBLOAD.COM and SB.COM)",
                              SMARTALK to "S_TALK.EXE")

    fun bring(voice: Int): String? = release(voice)?.file ?: BRING[voice]

    /** Every imported voice, in the order an import takes them: [IMPORTED], then the release voices. */
    fun imported(): List<Int> = (if (CONTROL == "voice-order") IMPORTED - SOUNDINGBOARD + SOUNDINGBOARD else IMPORTED) +
        (if (CONTROL == "release") emptyList() else releases.map { it.voice })

    /** A Blazie unit, a state made from its firmware: [BLAZIE] and the release voices. */
    fun isBlazie(voice: Int) = voice in BLAZIE || release(voice) != null

    /** The JVM tests' controls (`gradlew testDebugUnitTest -Pssi263ImportBreak=<value>`; never set on a phone):
     * `1` looks at a zip's top only -- no folder below it -- so the layout tests must fail; `unopened` drops a zip
     * inside a zip in the source in silence, so its test must fail; `state` stops
     * knowing a state file, so the state tests must fail; `state-sizes` knows a state by its first size alone, as
     * before the larger states, so their tests must fail; `speakout` drops what the native side says is the
     * Speak-Out's, so the Speak-Out tests must fail; `mockingboard` the same for the Mockingboard's; `release` leaves
     * the release voices out of the import, so their tests must fail; `units` drops a release no voice speaks (the
     * 640, Japanese) as before 0.8.5, so the unit screen's import tests must fail; `soundingboard` drops what the native
     * side says are the Sounding Board's two drivers, so its tests must fail; `smartalk` the same for the SmarTalk's
     * S_TALK.EXE; `voice-order` lists the Sounding Board after the release voices again, not under the Speak-Out
     * (SsiEngine's voices, [imported]). */
    internal val CONTROL = System.getProperty("ssi263.import.break") ?: ""

    const val STATE_SIZE = 786432           // battery-backed RAM + file flash: the state bl_save_state writes at first
    /** Every size a state has (bl_board.h bl_save_state): the RAM's first 256 KB or all 768 KB (once the unit's files
     *  reach past 7FFFFh; the Spanish setup fills it), then the file flash's first 512 KB or all 2 MB. */
    val STATE_SIZES = setOf(STATE_SIZE, 1310720, 2359296, 2883584)
    const val STATE_TAIL = 64               // ... and the emulator's clock after them (bl_clock.h, bl_rtc6242.h)
    private val STATE_TAILS = listOf("BLCLOCK1".toByteArray(Charsets.US_ASCII),
                                     "BLR6242".toByteArray(Charsets.US_ASCII) + 1)
    const val MAX_SOURCE = 64L shl 20       // an add-on is a few megabytes; firmware a few hundred kilobytes
    const val MAX_ENTRY = 4 shl 20          // an update program is half a megabyte
    const val MAX_INNER = 32 shl 20         // a zip in a zip: the add-on (5.5 MB in 0.8.0), the emulator's download

    const val NO_FIRMWARE_ZIP = "This zip does not contain Braille Lite, Braille 'n Speak, Speak-Out or Mockingboard " +
        "firmware."
    /** A zip inside a zip in the source: the import opens one level of zips, so its firmware is not looked at. */
    const val UNOPENED = "%s: a zip inside a zip in this one, which the import does not open, so it is left out; " +
        "unzip it once, and import it or the zip around it again."
    const val NO_FIRMWARE_FILE = "This file does not contain Braille Lite, Braille 'n Speak, Speak-Out or " +
        "Mockingboard firmware."
    /** Tomi's words, for a state picked on its own. */
    const val STATE_FILE = "This is a state file, not firmware. Please import only firmware files, or zips " +
        "containing them, with this tool."
    const val WHAT_TO_CHOOSE = "Choose the Braille Lite 2000's firmware: its update program (such as blt2000.exe), " +
        "the BL2ENG.BNS (English), BL2SPA.BNS (Spanish) or Lite2000.BNS (German, Italian) inside it, a zip holding " +
        "them in any of its folders, or the NVDA add-on (.nvda-addon) or the Blazie emulator's Windows " +
        "download (blazie-emu-...-windows.zip), which carry them all. Or a Braille " +
        "Lite 2000 language update, such as BL2HUN.BNS (Hungarian) or BL2TUR.BNS (Turkish): the Setup page lists " +
        "them all, under Show all Braille Lite importable languages. Or the Slovak " +
        "Braille 'n Speak 2000's: BS2SLL.BNS. Or the Speak-Out's: GW Micro's SPEAKOUT.HEX, or the speakout.zip " +
        "holding it. Or the Mockingboard's: mockingboard-tts-1.1.bin, Sweet Micro Systems' text-to-speech, made " +
        "from the Mockingboard Developers Toolkit disk, or that disk's image itself (.dsk, .do or .po); for the " +
        "early Mockingboard, mockingboard-tts-early.bin or the image of Mockingboard disk 1."
    const val ONLY_THE_HEX = "Only GW Micro's SPEAKOUT.HEX, as it came, can be imported for the Speak-Out."

    /** What the native side says about some bytes: SsiImport's in the app, a fake in the tests. */
    interface Identify {
        /** Find the firmware in `data`: when it is a Blazie release on the list, write it to `out` as a .BNS --
         * [ENGLISH], [SPANISH], [GERMAN], [ITALIAN] or [SLOVAK] -- or when it is the Speak-Out's SPEAKOUT.HEX, the Mockingboard's file or the
         * disk image holding it, write it (the Mockingboard's: the file) to `out` -- [SPEAKOUT], [MOCKINGBOARD],
         * [MOCKINGBOARD_EARLY] -- with the label; or [NONE], [REFUSED], [UNKNOWN], [OTHER_HEX], [NOT_BUILT] or
         * [OTHER_DISK] with the reason. */
        fun firmware(data: ByteArray, out: File): Pair<Int, String>

        /** The releases on the list, by label. */
        fun known(): List<String>

        /** A [UNIT_RELEASE] row, by the release table's words; null for none. */
        fun unitRelease(row: Int): UnitRelease? = null
    }

    fun languageName(language: Int) = release(language)?.language ?: when (language) {
        SPANISH -> "Spanish"; GERMAN -> "German"; ITALIAN -> "Italian"; SLOVAK -> "Slovak"
        SPEAKOUT -> "Speak-Out"; MOCKINGBOARD -> "Mockingboard"
        MOCKINGBOARD_EARLY -> "early Mockingboard"; SOUNDINGBOARD -> "Sounding Board"; SMARTALK -> "SmarTalk"
        else -> "English" }

    /** The unit's name in words: "German Braille Lite", "Slovak Braille 'n Speak 2000", a release voice's as the
     * table names it ("Braille Lite 2000 (Hungarian)"); the Speak-Out and the Mockingboards by their own. */
    fun unitName(language: Int) = release(language)?.name ?: when (language) {
        !in BLAZIE -> languageName(language)
        SLOVAK -> "Slovak Braille 'n Speak 2000"
        else -> "${languageName(language)} Braille Lite" }

    /** A unit's state, by its content: a size a state has ([STATE_SIZES]), or one with a clock's 64 bytes after it.
     *  Never imported, whatever its name. */
    fun isState(data: ByteArray): Boolean {
        if (CONTROL == "state") return false
        if (CONTROL == "state-sizes") return data.size == STATE_SIZE
        if (data.size in STATE_SIZES) return true
        val body = data.size - STATE_TAIL
        return body in STATE_SIZES && STATE_TAILS.any { magic -> magic.indices.all { data[body + it] == magic[it] } }
    }

    /** One firmware the import will bring in: a Braille Lite release on the list, the Speak-Out's HEX or the
     * Mockingboard's file. */
    class Found(
        val language: Int,                  // the voice: one of [IMPORTED]
        val from: String,                   // where it was: the file's name, or its path in the zip
        val firmware: File,                 // the .BNS or the HEX, written into the staging folder
        val label: String,                  // the release, in words (the native side's list)
        val unit: UnitRelease? = null,      // a release kept for the unit screen alone ([UNIT_RELEASE]), not a voice
        val parts: List<File> = emptyList(),  // a two-file voice's other files, in [FILES]' order (the Sounding Board)
    )

    /** Either `refusal` says why nothing can be imported, in words for the person holding the phone, or `found`
     * lists what will be.  `notes` are things worth saying either way. */
    class Plan(val found: List<Found>, val refusal: String?, val notes: List<String> = emptyList())

    /** What a finished import says: the voices that speak now, and the units kept for the unit screen alone said to
     * be no voice (Tomi, 2026-10-18: a Braille 'n Speak 640 imported alone is "not a voice", never "the voice speaks
     * now"). */
    fun doneMessage(found: List<Found>): String {
        val voices = found.filter { it.unit == null }.map { it.label }
        val units = found.filter { it.unit != null }.map { it.label }
        val said = ArrayList<String>()
        if (voices.isNotEmpty())
            said.add("Imported ${voices.joinToString(" and ")}. " +
                (if (voices.size == 1) "The voice speaks now." else "The voices speak now."))
        if (units.isNotEmpty())
            said.add((if (voices.isEmpty()) "Imported " else "Also imported ") + units.joinToString(" and ") +
                " for the Blazie Engineering unit screen. " + (if (units.size == 1) "Note: this one cannot be used " +
                "as a voice; it runs on the unit screen." else "Note: these cannot be used as voices; they run on " +
                "the unit screen."))
        return said.joinToString(" ")
    }

    private class Candidate(val language: Int, val from: String, val file: File, val label: String,
                            val unit: UnitRelease? = null)

    /** Everything a source's bytes hold, judged into `staging` (emptied first). */
    fun inspect(name: String, data: ByteArray, staging: File, id: Identify): Plan {
        if (staging.exists()) staging.deleteRecursively()
        staging.mkdirs()
        val sink = Sink(staging, id)
        if (zipAt(data, 0)) {
            sink.zip(name, data, 0, nested = false)
            return sink.plan(zip = true)
        }
        if (isState(data)) return Plan(emptyList(), STATE_FILE)
        sink.judge(name, data)
        if (sink.firmware.isEmpty() && sink.refused.isEmpty() && sink.unknown.isEmpty()) {
            // An update program that is a zip behind its own code (blt2000.exe): its first entry onwards.
            val at = zipStart(data)
            if (at > 0) try {
                sink.zip(name, data, at, nested = false)
                return sink.plan(zip = false)
            } catch (e: IOException) { /* bytes that only looked like a zip: no firmware, said below */ }
        }
        return sink.plan(zip = false)
    }

    private class Sink(val staging: File, val id: Identify) {
        val firmware = ArrayList<Candidate>()
        val refused = ArrayList<String>()
        val unknown = ArrayList<String>()
        val otherHex = ArrayList<Pair<String, String>>()     // where, and the native side's reason
        val withReason = ArrayList<Pair<String, String>>()   // ... the Mockingboard's in a copy without the voice,
                                                             // another disk image
        val states = ArrayList<String>()
        val unopened = ArrayList<String>()                   // a zip inside a zip inside the source
        private var n = 0

        /** One file's bytes, by the native side's eyes; true when it held firmware of any kind. */
        fun judge(from: String, bytes: ByteArray): Boolean {
            val out = File(staging, "candidate${n++}.bin")
            val (language, text) = id.firmware(bytes, out).let {
                if ((CONTROL == "speakout" && it.first == SPEAKOUT) ||
                    (CONTROL == "mockingboard" && it.first in setOf(MOCKINGBOARD, MOCKINGBOARD_EARLY)) ||
                    (CONTROL == "soundingboard" && it.first in PARTS.getValue(SOUNDINGBOARD)) ||
                    (CONTROL == "smartalk" && it.first == SMARTALK)) NONE to "" else it
            }
            when {
                isUnitRelease(language) -> {
                    // the unit screen's alone; the `units` control drops it, as before 0.8.5
                    val unit = if (CONTROL == "units") null else id.unitRelease(language - UNIT_RELEASE)
                    if (unit != null) firmware.add(Candidate(language, from, out, text, unit))
                    else { out.delete(); unknown.add(from) }
                }
                language >= 0 -> firmware.add(Candidate(language, from, out, text))
                language == REFUSED -> refused.add(from)
                language == UNKNOWN -> unknown.add(from)
                language == OTHER_HEX -> otherHex.add(from to text)
                language == NOT_BUILT || language == OTHER_DISK -> withReason.add(from to text)
                else -> return false
            }
            return true
        }

        /** Every entry of the zip, in whatever folders it sits (Tomi, 2026-10-18: anyone's own archive -- gwmicro/sb.zip,
         * blazie/braille-lite/BL2ENG.BNS -- imports as the add-on and the emulator's download do), each judged by its
         * content alone, never by its name or its folder; a zip in it is opened too, one level. */
        fun zip(name: String, data: ByteArray, at: Int, nested: Boolean) {
            try {
                ZipInputStream(ByteArrayInputStream(data, at, data.size - at)).use { zip ->
                    while (true) {
                        val e = zip.nextEntry ?: break
                        if (e.isDirectory) continue
                        val path = e.name.replace('\\', '/').trimStart('/')
                        val parts = path.split('/').filter { it.isNotEmpty() }
                        if (parts.isEmpty() || parts.contains("__MACOSX")) continue
                        if (CONTROL == "1" && parts.size > 1) continue      // the control: the zip's top alone
                        // a zip in it may be as big as the add-on (5.5 MB); nothing else that big is firmware
                        val bytes = readUpTo(zip, if (nested) MAX_ENTRY else MAX_INNER) ?: continue
                        val from = if (nested) "$name, $path" else path
                        val inner = zipAt(bytes, 0)
                        if (bytes.size > MAX_ENTRY && !inner) continue
                        if (isState(bytes)) { states.add(from); continue }
                        // a zip is opened, not judged whole; an update program is judged, then opened when it holds
                        // no firmware itself (a zip behind its own code)
                        if (inner || (!judge(from, bytes) && zipStart(bytes) >= 0)) {
                            if (!nested) zip(path, bytes, zipStart(bytes), nested = true)
                            else if (CONTROL != "unopened") unopened.add(from)   // the control: in silence again
                        }
                    }
                }
            } catch (e: IOException) {
                if (!nested) throw e
            } catch (e: RuntimeException) {
                // an entry's name that is no text (IllegalArgumentException): within a file, bytes that only looked
                // like a zip (the collection's unzip.BNS, Blazie's unzip program); the source itself, a damaged zip --
                // said in words, never with its bytes as the reason
                if (!nested) throw IOException("it is a damaged zip: a file name in it cannot be read")
            }
        }

        fun plan(zip: Boolean): Plan {
            val notes = ArrayList<String>()
            val found = ArrayList<Found>()
            // One release per language: the list's first (the newest) when a source holds two.
            val rank = id.known()
            for (language in imported()) {
                val codes = PARTS[language]
                if (codes != null) {                // a two-file voice: each file once, imported only with both
                    val parts = codes.map { code -> firmware.firstOrNull { it.language == code } }
                    val have = parts.filterNotNull()
                    if (have.size == parts.size)
                        found.add(Found(language, have.joinToString(" and ") { it.from }, have[0].file,
                                        SOUNDINGBOARD_LABEL, parts = have.drop(1).map { it.file }))
                    else for (c in have) {
                        val missing = FILES.getValue(language)[parts.indexOf(null)]
                        withReason.add(c.from to "the ${languageName(language)}'s ${FILES.getValue(language)[
                            parts.indexOf(c)]}, but its $missing is needed too (both are in GW Micro's sb.zip)")
                    }
                    continue
                }
                val c = firmware.filter { it.language == language }.minByOrNull { rank.indexOf(it.label) } ?: continue
                found.add(Found(language, c.from, c.file, c.label))
            }
            // then the releases no voice speaks, for the unit screen alone: each once
            for (language in firmware.map { it.language }.filter { isUnitRelease(it) }.distinct()) {
                val c = firmware.first { it.language == language }
                found.add(Found(language, c.from, c.file, c.label, c.unit))
            }
            val kept = found.flatMap { listOf(it.firmware) + it.parts }.toSet()
            for (c in firmware) if (c.file !in kept) {
                c.file.delete()
                val f = found.firstOrNull { it.language == c.language } ?: continue   // a two-file voice's part
                if (c.label != f.label)             // the same release twice (an update program and its .BNS) is one
                    notes.add("${c.from} is another ${languageName(c.language)} release (${c.label}); only " +
                        "${f.from} (${f.label}) is imported, as the unit holds one ${languageName(c.language)} " +
                        "release at a time.")
            }
            for (from in unknown) notes.add("$from is Braille Lite 2000 firmware of a release this app does not " +
                "know; it is left out.")
            for ((from, why) in otherHex + withReason) notes.add("$from is $why; it is left out.")
            if (states.size > 3) notes.add("${states.size} state files (${states[0]} and others) are not firmware, " +
                "and are not used: this phone prepares each unit's state itself from its firmware.")
            else if (states.isNotEmpty()) notes.add("${states.joinToString(", ")} ${if (states.size == 1) "is a state " +
                "file" else "are state files"}, not firmware, and ${if (states.size == 1) "is" else "are"} not " +
                "used: this phone prepares the unit's state itself from the firmware.")
            // a zip inside a zip in the source, beside what was taken: said, never dropped in silence
            if (found.isNotEmpty() && unopened.isNotEmpty()) notes.add(UNOPENED.format(unopened.joinToString(", ")))
            if (found.isNotEmpty()) return Plan(found, null, notes)

            if (states.isNotEmpty() && unknown.isEmpty() && refused.isEmpty() && unopened.isEmpty() && otherHex.isEmpty() &&
                withReason.isEmpty())
                return Plan(emptyList(), "This ${if (zip) "zip" else "file"} holds ${if (states.size == 1)
                    "a state file (${states[0]})" else "state files (${states.joinToString(", ")})"}, not firmware. " +
                    "Please import only firmware files, or zips containing them, with this tool.")
            val refusal = StringBuilder(when {
                unknown.isNotEmpty() -> "${unknown.joinToString(", ")} ${if (unknown.size == 1) "is" else "are"} " +
                    "Braille Lite 2000 firmware, but not a release this app knows, so it cannot be imported: a " +
                    "release for another country may be laid out differently, and only releases tested with this " +
                    "voice are taken. The releases this app knows: ${rank.joinToString("; ")}."
                otherHex.isNotEmpty() -> otherHex.joinToString(" ") { (from, why) -> "$from is $why." } +
                    " " + ONLY_THE_HEX
                withReason.isNotEmpty() -> withReason.joinToString(" ") { (from, why) -> "$from is $why." }
                zip -> NO_FIRMWARE_ZIP
                else -> NO_FIRMWARE_FILE
            })
            for (from in refused)
                refusal.append(" $from is Blazie firmware, but not a Braille Lite 2000 release this voice can run.")
            if (states.isNotEmpty())
                refusal.append(" ${states.joinToString(", ")}: a state file, not firmware; this phone makes the " +
                    "unit's state itself.")
            if (unopened.isNotEmpty()) refusal.append(" ").append(UNOPENED.format(unopened.joinToString(", ")))
            refusal.append(" ").append(WHAT_TO_CHOOSE)
            return Plan(emptyList(), refusal.toString())
        }
    }

    private fun readUpTo(zip: ZipInputStream, cap: Int): ByteArray? {
        val out = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(1 shl 16)
        while (true) {
            val n = zip.read(buffer)
            if (n < 0) break
            out.write(buffer, 0, n)
            if (out.size() > cap) return null
        }
        return out.toByteArray()
    }

    private fun zipAt(data: ByteArray, at: Int) = at >= 0 && at + 4 <= data.size && data[at] == 'P'.code.toByte() &&
        data[at + 1] == 'K'.code.toByte() && data[at + 2] == 3.toByte() && data[at + 3] == 4.toByte()

    /** Where the first zip entry starts in `data`, or -1. */
    fun zipStart(data: ByteArray): Int {
        for (i in 0..data.size - 4) if (zipAt(data, i)) return i
        return -1
    }
}
