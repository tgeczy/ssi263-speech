package com.ssi263speech.tts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.io.InputStream

/** The unit screen's "Load programs and files into flash" (UnitFiles.kt, #50): a picked file is read no further than
 * the unit's 2 MB flash -- a 64 MB file is refused having read at most a buffer past it, where it was read whole first
 * (a video picked by mistake filled the heap).  A file of exactly 2 MB, and a small one, come whole.  The control,
 * -Pssi263ImportBreak=flash, reads the whole file first: the large file's case must FAIL. */
class UnitFilesTest {
    /** n bytes, counting how many were taken */
    private class Counted(private val n: Long) : InputStream() {
        var taken = 0L
        override fun read(): Int = if (taken >= n) -1 else { taken++; (taken and 0xFF).toInt() }
        override fun read(b: ByteArray, off: Int, len: Int): Int {
            if (taken >= n) return -1
            val k = minOf(len.toLong(), n - taken).toInt()
            for (i in 0 until k) b[off + i] = ((taken + i + 1) and 0xFF).toByte()
            taken += k
            return k
        }
    }

    @Test fun aLargeFileIsRefusedWithoutReadingItWhole() {
        val big = Counted(64L shl 20)
        try {
            UnitFiles.read(big, "film.mp4")
            fail("a 64 MB file was taken")
        } catch (e: IllegalArgumentException) {
            assertEquals("film.mp4 is larger than the unit's flash", e.message)
        }
        assertTrue("read ${big.taken} bytes of a 64 MB file before refusing it (at most the flash and a buffer)",
            big.taken <= UnitFiles.FLASH + (1 shl 16))
    }

    @Test fun aFileTheFlashHoldsComesWhole() {
        assertEquals(UnitFiles.FLASH, UnitFiles.read(Counted(UnitFiles.FLASH.toLong()), "whole.bin").size)
        val small = UnitFiles.read(Counted(1000), "notes.txt")
        assertEquals(1000, small.size)
        assertEquals(1, small[0].toInt()); assertEquals((1000 and 0xFF).toByte(), small[999])
    }

    @Test fun oneByteMoreIsRefused() {
        try {
            UnitFiles.read(Counted(UnitFiles.FLASH + 1L), "over.bin")
            fail("a file one byte larger than the flash was taken")
        } catch (e: IllegalArgumentException) {
            assertEquals("over.bin is larger than the unit's flash", e.message)
        }
    }
}
