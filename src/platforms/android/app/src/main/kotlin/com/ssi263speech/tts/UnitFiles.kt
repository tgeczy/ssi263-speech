// The unit screen's "Load programs and files into flash": a picked file's bytes, read no further than the unit's
// flash holds (no Android in it, so the JVM tests run it).  It read the whole file and only then compared it with
// 2 MB: a video or a disk image picked by mistake filled the heap first, and an OutOfMemoryError took the app -- the
// TTS service with it -- down instead of saying the file is too large (#50).
package com.ssi263speech.tts

import java.io.InputStream

object UnitFiles {
    // The JVM tests' must-fail control: -Pssi263ImportBreak=flash reads the whole file before the size test, as it was
    private val CONTROL = System.getProperty("ssi263.import.break") ?: ""

    /** The unit's whole flash: no file larger can be loaded into it. */
    const val FLASH = 2 * 1024 * 1024

    /** The stream's bytes, or IllegalArgumentException("<name> is larger than the unit's flash") as soon as more than
     * [FLASH] have come: at most a buffer past it is ever read. */
    fun read(input: InputStream, name: String): ByteArray {
        if (CONTROL == "flash") {
            val all = input.readBytes()
            if (all.size > FLASH) throw IllegalArgumentException("$name is larger than the unit's flash")
            return all
        }
        val out = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(1 shl 16)
        while (true) {
            val n = input.read(buffer)
            if (n < 0) break
            out.write(buffer, 0, n)
            if (out.size() > FLASH) throw IllegalArgumentException("$name is larger than the unit's flash")
        }
        return out.toByteArray()
    }
}
