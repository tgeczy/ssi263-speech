package com.ssi263speech.tts

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** The words the service marks for the unit (SsiWords.kt): each one's place as Android counts it (UTF-16) and as the
 * native side does (UTF-8).  The control, -Pssi263ImportBreak=words, counts the UTF-8 offset in UTF-16 units: the
 * accented cases must FAIL. */
class SsiWordsTest {
    /** every word is a run without white space, and the bytes at its UTF-8 offset are its own */
    private fun held(text: String): List<SsiWords.Word> {
        val utf8 = text.toByteArray(Charsets.UTF_8)
        val words = SsiWords.of(text)
        var last = 0
        for (w in words) {
            assertTrue("in order, apart: $text", w.start >= last && w.end > w.start)
            last = w.end
            val word = text.substring(w.start, w.end)
            assertTrue("no white space in a word: $text", word.none { Character.isWhitespace(it) })
            val bytes = word.toByteArray(Charsets.UTF_8)
            assertArrayEquals("the word's bytes at its offset: $text", bytes,
                              utf8.copyOfRange(w.utf8, minOf(utf8.size, w.utf8 + bytes.size)))
        }
        return words
    }

    @Test fun plainWords() {
        val w = held("one two  three")
        assertEquals(listOf(0, 4, 9), w.map { it.start })
        assertEquals(listOf(3, 7, 14), w.map { it.end })
        assertEquals(listOf(0, 4, 9), w.map { it.utf8 })
    }

    @Test fun edges() {
        assertEquals(0, held("").size)
        assertEquals(0, held("  \n\t ").size)
        assertEquals(listOf(2), held("  lead").map { it.start })
        assertEquals(listOf(5), held("trail  ").map { it.end })
        assertEquals(3, held("line one\nline").size)
    }

    @Test fun accentedLettersAndBeyond() {
        // two bytes a letter, three, and four (two UTF-16 units)
        assertEquals(listOf(0, 6, 13), held("café naïve done").map { it.utf8 })
        assertEquals(listOf(0, 14), held("árvíztűrő tükörfúrógép").map { it.utf8 })
        assertEquals(listOf(0, 10), held("日本語 text").map { it.utf8 })
        val w = held("a 😀 b")
        assertEquals(listOf(0, 2, 5), w.map { it.start })
        assertEquals(listOf(0, 2, 7), w.map { it.utf8 })
    }

    @Test fun aSurrogateWithoutItsPair() {
        // String.toByteArray makes one byte of it: the word after it must still be found
        assertEquals(listOf(0, 2, 4), held("x \uD83D y").map { it.utf8 })
    }
}
