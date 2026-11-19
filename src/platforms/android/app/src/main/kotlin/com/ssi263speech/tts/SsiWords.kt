package com.ssi263speech.tts

/**
 * The words of a request's text, for SynthesisCallback.rangeStart: each run of characters that is not white space,
 * with its place in the text as the framework counts it (UTF-16) and as the native side does (UTF-8, the bytes
 * String.toByteArray makes: a surrogate without its pair becomes one byte there).
 */
internal object SsiWords {
    class Word(val start: Int, val end: Int, val utf8: Int)

    // the JVM test's control (SsiWordsTest): the UTF-8 offset counted in UTF-16 units
    private val CONTROL = System.getProperty("ssi263.import.break") ?: ""

    fun of(text: String): List<Word> {
        val out = ArrayList<Word>()
        var i = 0
        var bytes = 0
        var start = -1
        var startBytes = 0
        while (i < text.length) {
            val cp = text.codePointAt(i)
            if (Character.isWhitespace(cp)) {
                if (start >= 0) { out.add(Word(start, i, startBytes)); start = -1 }
            } else if (start < 0) {
                start = i
                startBytes = bytes
            }
            bytes += if (CONTROL == "words") Character.charCount(cp) else when {
                cp < 0x80 || cp in 0xD800..0xDFFF -> 1
                cp < 0x800 -> 2
                cp < 0x10000 -> 3
                else -> 4
            }
            i += Character.charCount(cp)
        }
        if (start >= 0) out.add(Word(start, text.length, startBytes))
        return out
    }
}
