// The settings screen's "Speak": the same engine path as the TTS service (the chosen voice, the app's settings, a
// request at 100%), rendered under the engine's lock -- the unit runs many times faster than real time -- and played
// after it is released, so a screen reader speaking through this engine meanwhile is not held up (as outspoken's
// preview).  The render is kept as last-render.wav in the app's files: a debug build's
// `adb shell run-as com.ssi263speech.tts cat files/last-render.wav` pulls it off the device.  "Export to WAV…" is
// the same render, written where the person chose (WavExport.kt) instead of played.
package com.ssi263speech.tts

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.util.Log
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.OutputStream

object PreviewPlayer {
    @Volatile private var stopRequested = false
    @Volatile private var previewing = false        // a Speak (its render and its playback), not an export

    /** One render: the samples, their rate, and whether it ran to its end (a Stop cuts it short). */
    class Render(val pcm: ByteArray, val rate: Int, val finished: Boolean, val voice: SsiEngine.VoiceInfo)

    /** Blocking: call off the UI thread.  -> samples spoken, or -1 when the engine could not speak. */
    fun speak(ctx: Context, text: String): Int {
        previewing = true
        try { return speakNow(ctx, text) }
        finally { previewing = false }
    }

    private fun speakNow(ctx: Context, text: String): Int {
        val r = render(ctx, text) ?: return -1
        try { FileOutputStream(File(ctx.filesDir, "last-render.wav")).use { WavExport.write(it, r.pcm, r.rate) } }
        catch (e: Exception) { Log.w("Preview", "wav not written", e) }
        Log.i("Preview", "${r.voice.name}: ${r.pcm.size / 2} samples at ${r.rate} Hz")
        if (r.pcm.isNotEmpty() && !stopRequested) play(r.pcm, r.rate)
        return r.pcm.size / 2
    }

    /** Blocking: call off the UI thread.  The text rendered and written to `out` as a WAV; null when the engine could
     * not speak, else the render (nothing is written when it was stopped before its end). */
    fun export(ctx: Context, text: String, out: OutputStream): Render? {
        val r = render(ctx, text) ?: return null
        if (r.finished) WavExport.write(out, r.pcm, r.rate)
        Log.i("Preview", "export, ${r.voice.name}: ${r.pcm.size / 2} samples at ${r.rate} Hz, finished ${r.finished}")
        return r
    }

    /** The chosen voice at the app's settings, a request at 100%, under the engine's lock. */
    private fun render(ctx: Context, text: String): Render? {
        if (!SsiEngine.open(ctx)) return null
        stopRequested = false
        val s = SsiSettings.snapshot(ctx)
        val voice = SsiEngine.voiceFor(ctx, s.voice)
        var rate = 22050
        var finished = false
        val pcm = SsiEngine.withEngine {
            SsiEngine.configure(s)
            rate = SsiNative.nativeSampleRate()
            val started = SsiEngine.start(PreviewPlayer, voice, text, s, 100, 100)
            if (started < 0) { Log.w("Preview", "start: ${SsiNative.nativeError()}"); return@withEngine null }
            val all = ByteArrayOutputStream()
            if (started == 1) { finished = true; return@withEngine all.toByteArray() }
            val bytes = ByteArray(8192)
            while (!stopRequested) {
                val n = SsiEngine.pull(bytes)
                if (n <= 0) { finished = n == 0; break }
                all.write(bytes, 0, n)
            }
            if (!finished) SsiEngine.cancel()
            all.toByteArray()
        } ?: return null
        return Render(pcm, rate, finished, voice)
    }

    /** Any thread: a Speak, if one runs -- the screen left (Home, another app): it went on talking from the
     * background (Panthera's audit, P54).  An export goes on: it is the person's file being written. */
    fun stopPreview() { if (previewing) stop() }

    /** Any thread: the preview's or the export's render, if one runs (never another user's utterance: #51). */
    fun stop() {
        stopRequested = true
        SsiEngine.stop(PreviewPlayer)
    }

    // MODE_STREAM, written in pieces so a stop cuts it short.  Accessibility usage, as a screen reader's speech.
    private fun play(pcm: ByteArray, rate: Int) {
        val min = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT)
        val track = AudioTrack.Builder()
            .setAudioAttributes(AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_ASSISTANCE_ACCESSIBILITY)
                .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
            .setAudioFormat(AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(rate)
                .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
            .setBufferSizeInBytes(maxOf(min, rate))
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()
        try {
            track.play()
            var off = 0
            while (off < pcm.size && !stopRequested) {
                val w = track.write(pcm, off, minOf(8192, pcm.size - off))   // blocks, pacing playback
                if (w <= 0) break
                off += w
            }
            // a write only queues the audio: wait for the playback head, with a deadline
            val frames = off / 2
            val deadline = android.os.SystemClock.elapsedRealtime() + frames * 1000L / rate + 2000
            while (!stopRequested && track.playbackHeadPosition < frames &&
                   android.os.SystemClock.elapsedRealtime() < deadline) Thread.sleep(20)
            Log.i("Preview", "played ${minOf(track.playbackHeadPosition, frames)} of ${pcm.size / 2} samples" +
                  if (stopRequested) ", stopped" else "")
        } finally {
            try { track.stop() } catch (e: Exception) {}
            track.release()
        }
    }
}
