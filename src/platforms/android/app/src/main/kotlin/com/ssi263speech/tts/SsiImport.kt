// The import on the phone: a source's bytes (a file the system picker handed over, or a path from adb), judged by
// FirmwareImport with the native side's eyes, then brought in -- each Braille Lite unit's state made from its
// firmware on this phone (bl_state.c) and checked; the Speak-Out's HEX and the Mockingboard's file taken as they
// are -- each unit made to speak once,
// and only then moved into place beside the voice.
package com.ssi263speech.tts

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.io.IOException
import java.io.InputStream

object SsiImport {
    /** The native side's judgement (bl_firmware.c, ssa_import.c, through ssa_jni.c). */
    object NativeIdentify : FirmwareImport.Identify {
        override fun firmware(data: ByteArray, out: File): Pair<Int, String> {
            val r = SsiNative.nativeImportFirmware(data, out.absolutePath)
            return r to SsiNative.nativeImportError()       // the release's label, or the reason
        }

        override fun known(): List<String> = SsiNative.nativeKnownFirmware().toList()
    }

    class Source(val name: String, val size: Long, val open: () -> InputStream)

    /** A file by path: the adb route. */
    fun source(file: File) = Source(file.name, file.length()) { file.inputStream() }

    /** A file the system picker handed over; its name and size come from the provider when it says them. */
    fun source(ctx: Context, uri: Uri): Source {
        var name = uri.lastPathSegment?.substringAfterLast('/') ?: "file"
        var size = -1L
        try {
            ctx.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE),
                null, null, null)?.use { c ->
                if (c.moveToFirst()) {
                    c.getColumnIndex(OpenableColumns.DISPLAY_NAME).takeIf { it >= 0 }
                        ?.let { i -> if (!c.isNull(i)) name = c.getString(i) }
                    c.getColumnIndex(OpenableColumns.SIZE).takeIf { it >= 0 }
                        ?.let { i -> if (!c.isNull(i)) size = c.getLong(i) }
                }
            }
        } catch (e: Exception) { /* the name is a courtesy */ }
        return Source(name, size) { ctx.contentResolver.openInputStream(uri) ?: throw IOException("cannot open $name") }
    }

    /** The whole source, which is small: firmware is a few hundred kilobytes, an add-on a few megabytes. */
    fun read(source: Source): ByteArray {
        if (source.size > FirmwareImport.MAX_SOURCE) throw IOException(tooBig(source))
        source.open().use { input ->
            val out = java.io.ByteArrayOutputStream()
            val buffer = ByteArray(1 shl 16)
            while (true) {
                val n = input.read(buffer)
                if (n < 0) break
                out.write(buffer, 0, n)
                if (out.size() > FirmwareImport.MAX_SOURCE) throw IOException(tooBig(source))
            }
            return out.toByteArray()
        }
    }

    private fun tooBig(source: Source) = "${source.name} is larger than 64 MB, so it is not Braille Lite, " +
        "Speak-Out or Mockingboard firmware, an update for it or the NVDA add-on."

    fun inspect(ctx: Context, source: Source): FirmwareImport.Plan =
        FirmwareImport.inspect(source.name, read(source), SsiData.staging(ctx), NativeIdentify)

    fun probeText(language: Int) = if (language == FirmwareImport.SPANISH) "Hola." else "Hello."

    /** The unit's name in the import's messages. */
    fun unitName(language: Int) =
        if (language in FirmwareImport.NO_STATE) FirmwareImport.languageName(language) else "${FirmwareImport.languageName(language)} Braille Lite"

    class Cancelled : IOException("cancelled")

    /** An import in flight, held outside the screen so a screen rebuilt underneath it finds it again. */
    class Job(val plan: FirmwareImport.Plan) {
        @Volatile var step = ""              // what is happening, in words
        @Volatile var cancelled = false
        @Volatile var result: Result<List<String>>? = null
        @Volatile var listener: ((Job) -> Unit)? = null

        /** 0..1 across the whole import (the states are nearly all of it; blv_make_state reports every 10 million
         * instructions, so Spanish's bar moves 115 times). */
        fun progress(): Double = (made + SsiNative.nativeStateProgress() * making) / total
        @Volatile private var made = 0.0
        @Volatile private var making = 0.0
        private val total: Double = plan.found.sumOf { weight(it.language).toDouble() }.coerceAtLeast(1.0)

        /** English's state is 150 million instructions, Spanish's 1150 million; the Speak-Out and the Mockingboard
         * have no state to make, only their check. */
        private fun weight(language: Int) = when (language) {
            FirmwareImport.SPANISH -> 1150; FirmwareImport.SPEAKOUT, FirmwareImport.MOCKINGBOARD -> 15; else -> 150 }

        fun start(ctx: Context) = Thread({
            result = runCatching { commit(ctx.applicationContext) }
            listener?.invoke(this)
        }, "ssi263-import").start()

        fun cancel() {
            cancelled = true
            SsiNative.nativeStateCancel()
        }

        /** Each unit made ready in the staging folder -- a Braille Lite's state always made here, from its firmware,
         * never taken from the source -- then all of them moved into place together. */
        private fun commit(ctx: Context): List<String> {
            val staging = SsiData.staging(ctx)
            val ready = File(staging, "ready")
            ready.deleteRecursively()
            ready.mkdirs()
            val labels = ArrayList<String>()
            try {
                for (f in plan.found) {
                    val name = unitName(f.language)
                    val files = FirmwareImport.FILES.getValue(f.language)
                    val firmware = File(ready, files[0])
                    if (!f.firmware.renameTo(firmware)) f.firmware.copyTo(firmware, overwrite = true)
                    val w = weight(f.language).toDouble()
                    if (f.language !in FirmwareImport.NO_STATE) {
                        val state = File(ready, files[1])
                        step = "Preparing the $name unit"
                        listener?.invoke(this)
                        making = w
                        val ok = SsiNative.nativeMakeState(firmware.absolutePath, f.language, state.absolutePath) == 1
                        made += w                   // before `making` drops, so the bar never steps back
                        making = 0.0
                        if (cancelled) throw Cancelled()
                        if (!ok) throw IOException("The $name unit could not be prepared: ${SsiNative.nativeImportError()}")
                        // Each release on the list makes the state it was tested with, byte for byte.
                        if (!SsiNative.nativeStateCheck(firmware.absolutePath, state.absolutePath))
                            throw IOException("The $name unit prepared on this phone is not the one the voice was " +
                                "tested with, so it was not imported.")
                    } else {
                        made += w
                    }
                    step = "Checking that the $name unit speaks"
                    listener?.invoke(this)
                    val samples = SsiNative.nativeProbe(ready.absolutePath, f.language,
                        probeText(f.language).toByteArray(Charsets.UTF_8))
                    if (samples < 0) throw IOException("The $name unit would not start: ${SsiNative.nativeImportError()}")
                    if (samples == 0L) throw IOException("The $name unit started but stayed silent, so this " +
                        "firmware was not imported.")
                    File(ready, files[0] + SsiData.LABEL).writeText(f.label)
                    labels.add(f.label)
                    if (cancelled) throw Cancelled()
                }
                step = "Moving the firmware into place"
                listener?.invoke(this)
                SsiData.install(ctx, ready)
                return labels
            } finally {
                staging.deleteRecursively()
            }
        }
    }

    /** One import at a time, whichever screen shows it. */
    @Volatile var job: Job? = null
}
