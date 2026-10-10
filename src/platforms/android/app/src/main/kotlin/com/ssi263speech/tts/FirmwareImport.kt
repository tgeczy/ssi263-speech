// The firmware a person brings, and what is in it: the Braille Lite's (Blazie's), the Speak-Out's (GW Micro's
// SPEAKOUT.HEX) and the Mockingboard's (Sweet Micro Systems' text-to-speech, mockingboard-tts-1.1.bin).  The app
// carries none of them, so each user imports their own copy, the way outspoken and Panthera take their engine data --
// one "Import firmware" for all, told apart by content.  Nothing here touches Android, so the JVM tests (src/test)
// run it as it is; the bytes themselves are judged by the native side (bl_firmware.c, ssa_import.c) behind
// [Identify], by content and never by name.
//
// Only Braille Lite releases on the native side's list are taken -- each one booted and heard before it was listed --
// only the Speak-Out's known SPEAKOUT.HEX (its sha256; Tomi, 0.7.5: "we just need to accept the .hex firmware"), only
// the Mockingboard's known file (its sha256; Tomi, 2026-10-09: imported as Blazie's is), and never a unit's state:
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
    const val NONE = -1                     // no firmware in the bytes
    const val REFUSED = -2                  // Blazie firmware, but another unit's: not one the voice can run
    const val UNKNOWN = -4                  // Braille Lite 2000 firmware, but not a release on the list
    const val OTHER_HEX = -5                // an Intel HEX file, but not the Speak-Out's SPEAKOUT.HEX (damaged, another)
    const val NOT_BUILT = -6                // the Mockingboard's file, but this copy of the app has no Mockingboard

    /** The files each imported voice needs, by its index: the Braille Lite's firmware and the state made from it; the
     * Speak-Out's HEX; the Mockingboard's file. */
    val FILES = mapOf(ENGLISH to listOf("BL2ENG.BNS", "bl2_2003_warm.state"),
                      SPANISH to listOf("BL2SPA.BNS", "bl2spa_fresh.state"),
                      SPEAKOUT to listOf("SPEAKOUT.HEX"),
                      MOCKINGBOARD to listOf("mockingboard-tts-1.1.bin"))
    /** The imported voices, in the order an import takes them. */
    val IMPORTED = listOf(ENGLISH, SPANISH, SPEAKOUT, MOCKINGBOARD)
    /** The voices whose firmware is taken as it is, with no state to make. */
    val NO_STATE = setOf(SPEAKOUT, MOCKINGBOARD)

    /** The JVM tests' controls (`gradlew testDebugUnitTest -Pssi263ImportBreak=<value>`; never set on a phone):
     * `1` looks at a zip's top only -- no folder down, no add-on layout -- so the layout tests must fail; `state` stops
     * knowing a state file, so the state tests must fail; `speakout` drops what the native side says is the
     * Speak-Out's, so the Speak-Out tests must fail; `mockingboard` the same for the Mockingboard's. */
    private val CONTROL = System.getProperty("ssi263.import.break") ?: ""

    const val STATE_SIZE = 786432           // battery-backed RAM + file flash: every state bl_save_state writes
    const val MAX_SOURCE = 64L shl 20       // an add-on is a few megabytes; firmware a few hundred kilobytes
    const val MAX_ENTRY = 4 shl 20          // an update program is half a megabyte

    const val NO_FIRMWARE_ZIP = "This zip does not contain Braille Lite, Speak-Out or Mockingboard firmware."
    const val NO_FIRMWARE_FILE = "This file does not contain Braille Lite, Speak-Out or Mockingboard firmware."
    /** Tomi's words, for a state picked on its own. */
    const val STATE_FILE = "This is a state file, not firmware. Please import only firmware files, or zips " +
        "containing them, with this tool."
    const val WHAT_TO_CHOOSE = "Choose the Braille Lite 2000's firmware: its update program (such as blt2000.exe), " +
        "the BL2ENG.BNS or BL2SPA.BNS inside it, a zip holding them at its top or one folder down, or the NVDA " +
        "add-on (.nvda-addon), which carries both. Or the Speak-Out's: GW Micro's SPEAKOUT.HEX, or the speakout.zip " +
        "holding it. Or the Mockingboard's: mockingboard-tts-1.1.bin, Sweet Micro Systems' text-to-speech, made " +
        "from the Mockingboard Developers Toolkit disk."
    const val ONLY_THE_HEX = "Only GW Micro's SPEAKOUT.HEX, as it came, can be imported for the Speak-Out."

    /** What the native side says about some bytes: SsiImport's in the app, a fake in the tests. */
    interface Identify {
        /** Find the firmware in `data`: when it is a Braille Lite release on the list, write it to `out` as a .BNS --
         * [ENGLISH] or [SPANISH] -- or when it is the Speak-Out's SPEAKOUT.HEX or the Mockingboard's file, write it
         * to `out` -- [SPEAKOUT], [MOCKINGBOARD] -- with the label; or [NONE], [REFUSED], [UNKNOWN], [OTHER_HEX] or
         * [NOT_BUILT] with the reason. */
        fun firmware(data: ByteArray, out: File): Pair<Int, String>

        /** The releases on the list, by label. */
        fun known(): List<String>
    }

    fun languageName(language: Int) = when (language) {
        SPANISH -> "Spanish"; SPEAKOUT -> "Speak-Out"; MOCKINGBOARD -> "Mockingboard"; else -> "English" }

    /** A unit's state, by its content: the size every state has.  Never imported, whatever its name. */
    fun isState(data: ByteArray) = CONTROL != "state" && data.size == STATE_SIZE

    /** One firmware the import will bring in: a Braille Lite release on the list, the Speak-Out's HEX or the
     * Mockingboard's file. */
    class Found(
        val language: Int,                  // the voice: [ENGLISH], [SPANISH], [SPEAKOUT] or [MOCKINGBOARD]
        val from: String,                   // where it was: the file's name, or its path in the zip
        val firmware: File,                 // the .BNS or the HEX, written into the staging folder
        val label: String,                  // the release, in words (the native side's list)
    )

    /** Either `refusal` says why nothing can be imported, in words for the person holding the phone, or `found`
     * lists what will be.  `notes` are things worth saying either way. */
    class Plan(val found: List<Found>, val refusal: String?, val notes: List<String> = emptyList())

    private class Candidate(val language: Int, val from: String, val file: File, val label: String)

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
        val notBuilt = ArrayList<Pair<String, String>>()     // ... the Mockingboard's, in a copy without the voice
        val states = ArrayList<String>()
        val deep = ArrayList<String>()
        private var n = 0

        /** One file's bytes, by the native side's eyes; true when it held firmware of any kind. */
        fun judge(from: String, bytes: ByteArray): Boolean {
            val out = File(staging, "candidate${n++}.bin")
            val (language, text) = id.firmware(bytes, out).let {
                if ((CONTROL == "speakout" && it.first == SPEAKOUT) ||
                    (CONTROL == "mockingboard" && it.first == MOCKINGBOARD)) NONE to "" else it
            }
            when {
                language >= 0 -> firmware.add(Candidate(language, from, out, text))
                language == REFUSED -> refused.add(from)
                language == UNKNOWN -> unknown.add(from)
                language == OTHER_HEX -> otherHex.add(from to text)
                language == NOT_BUILT -> notBuilt.add(from to text)
                else -> return false
            }
            return true
        }

        /** The zip's entries at its top, one folder down, and where the NVDA add-on keeps the unit's files. */
        fun zip(name: String, data: ByteArray, at: Int, nested: Boolean) {
            try {
                ZipInputStream(ByteArrayInputStream(data, at, data.size - at)).use { zip ->
                    while (true) {
                        val e = zip.nextEntry ?: break
                        if (e.isDirectory) continue
                        val path = e.name.replace('\\', '/').trimStart('/')
                        val parts = path.split('/').filter { it.isNotEmpty() }
                        if (parts.isEmpty() || parts.contains("__MACOSX")) continue
                        val addon = parts.size == 3 && parts[0].equals("synthDrivers", true) &&
                            parts[1].equals("_ssi263_blazie", true)
                        val tooDeep = if (CONTROL == "1") parts.size > 1 else parts.size > 2 && !addon
                        if (tooDeep) {
                            if (parts.last().lowercase().let { it.endsWith(".bns") || it.endsWith(".exe") ||
                                    it.endsWith(".hex") || it.endsWith(".bin") })
                                deep.add(path)
                            continue
                        }
                        val bytes = readUpTo(zip, MAX_ENTRY) ?: continue
                        val from = if (nested) "$name, $path" else path
                        if (isState(bytes)) { states.add(from); continue }
                        if (!judge(from, bytes) && !nested && zipStart(bytes) >= 0)
                            zip(path, bytes, zipStart(bytes), nested = true)
                    }
                }
            } catch (e: IOException) {
                if (!nested) throw e
            }
        }

        fun plan(zip: Boolean): Plan {
            val notes = ArrayList<String>()
            val found = ArrayList<Found>()
            // One release per language: the list's first (the newest) when a source holds two.
            val rank = id.known()
            for (language in IMPORTED) {
                val c = firmware.filter { it.language == language }.minByOrNull { rank.indexOf(it.label) } ?: continue
                found.add(Found(language, c.from, c.file, c.label))
            }
            val kept = found.map { it.firmware }.toSet()
            for (c in firmware) if (c.file !in kept) {
                c.file.delete()
                val f = found.first { it.language == c.language }
                if (c.label != f.label)             // the same release twice (an update program and its .BNS) is one
                    notes.add("${c.from} is another ${languageName(c.language)} release (${c.label}); only " +
                        "${f.from} (${f.label}) is imported, as the unit holds one ${languageName(c.language)} " +
                        "release at a time.")
            }
            for (from in unknown) notes.add("$from is Braille Lite 2000 firmware of a release this app does not " +
                "know; it is left out.")
            for ((from, why) in otherHex + notBuilt) notes.add("$from is $why; it is left out.")
            if (states.isNotEmpty()) notes.add("${states.joinToString(", ")} ${if (states.size == 1) "is a state " +
                "file" else "are state files"}, not firmware, and ${if (states.size == 1) "is" else "are"} not " +
                "used: this phone prepares the unit's state itself from the firmware.")
            if (found.isNotEmpty()) return Plan(found, null, notes)

            if (states.isNotEmpty() && unknown.isEmpty() && refused.isEmpty() && deep.isEmpty() && otherHex.isEmpty() &&
                notBuilt.isEmpty())
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
                notBuilt.isNotEmpty() -> notBuilt.joinToString(" ") { (from, why) -> "$from is $why." }
                zip -> NO_FIRMWARE_ZIP
                else -> NO_FIRMWARE_FILE
            })
            for (from in refused)
                refusal.append(" $from is Blazie firmware, but not a Braille Lite 2000 release this voice can run.")
            if (states.isNotEmpty())
                refusal.append(" ${states.joinToString(", ")}: a state file, not firmware; this phone makes the " +
                    "unit's state itself.")
            if (deep.isNotEmpty())
                refusal.append(" Firmware is looked for only at the top of the zip and one folder down; " +
                    "${deep.first()} is deeper than that.")
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
