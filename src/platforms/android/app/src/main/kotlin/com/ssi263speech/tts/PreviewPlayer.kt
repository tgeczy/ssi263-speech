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
    // Each Speak and export is a request, begun at the click on the UI thread: a stop reaches it from there, and it
    // owns the engine while it renders (PreviewRequests.kt)
    private val requests = PreviewRequests { r -> SsiEngine.stop(r) }

    /** UI thread, at the click, before the worker starts: the request a Speak (`preview`) or an export goes by. */
    fun begin(preview: Boolean): PreviewRequests.Request = requests.begin(preview)

    /** One render: the samples, their rate, and whether it ran to its end (a Stop cuts it short). */
    class Render(val pcm: ByteArray, val rate: Int, val finished: Boolean, val voice: SsiEngine.VoiceInfo)

    /** Blocking: call off the UI thread, with `req` from begin(true).  -> samples spoken, or -1 when the engine could
     * not speak. */
    fun speak(ctx: Context, text: String, req: PreviewRequests.Request): Int {
        requests.started(req)
        try { return speakNow(ctx, text, req) }
        finally { requests.end(req) }
    }

    private fun speakNow(ctx: Context, text: String, req: PreviewRequests.Request): Int {
        val r = render(ctx, text, req) ?: return -1
        try { FileOutputStream(File(ctx.filesDir, "last-render.wav")).use { WavExport.write(it, r.pcm, r.rate) } }
        catch (e: Exception) { Log.w("Preview", "wav not written", e) }
        Log.i("Preview", "${r.voice.name}: ${r.pcm.size / 2} samples at ${r.rate} Hz" +
              if (requests.cancelled(req)) ", stopped" else "")
        if (r.pcm.isNotEmpty() && !requests.cancelled(req)) play(r.pcm, r.rate, req)
        return r.pcm.size / 2
    }

    /** Blocking: call off the UI thread.  The text rendered and written to `out` as a WAV; null when the engine could
     * not speak, else the render (nothing is written when it was stopped before its end).  `req` from begin(false). */
    fun export(ctx: Context, text: String, out: OutputStream, req: PreviewRequests.Request): Render? {
        requests.started(req)
        try {
            val r = render(ctx, text, req) ?: return null
            if (r.finished) WavExport.write(out, r.pcm, r.rate)
            Log.i("Preview", "export, ${r.voice.name}: ${r.pcm.size / 2} samples at ${r.rate} Hz, finished ${r.finished}")
            return r
        } finally { requests.end(req) }
    }

    /** The chosen voice at the app's settings, a request at 100%, under the engine's lock. */
    private fun render(ctx: Context, text: String, req: PreviewRequests.Request): Render? {
        if (!SsiEngine.open(ctx)) return null
        requests.rendering(req)
        val s = SsiSettings.snapshot(ctx)
        val voice = SsiEngine.voiceFor(ctx, s.voice)
        var rate = 22050
        var finished = false
        val pcm = SsiEngine.withEngine {
            SsiEngine.configure(s)
            rate = SsiNative.nativeSampleRate()
            // stopped while it waited for the engine (behind a screen reader's utterance): the unit is not started
            if (requests.cancelled(req)) return@withEngine ByteArray(0)
            val started = SsiEngine.start(req, voice, text, s, 100, 100)
            if (started < 0) { Log.w("Preview", "start: ${SsiNative.nativeError()}"); return@withEngine null }
            val all = ByteArrayOutputStream()
            if (started == 1) { finished = true; return@withEngine all.toByteArray() }
            val bytes = ByteArray(8192)
            while (!requests.cancelled(req)) {
                val n = SsiEngine.pull(bytes)
                if (n <= 0) { finished = n == 0; break }
                all.write(bytes, 0, n)
            }
            if (!finished) SsiEngine.cancel()
            all.toByteArray()
        } ?: return null
        return Render(pcm, rate, finished, voice)
    }

    /** Any thread: a Speak, if one was begun -- the screen left (Home, another app): it went on talking from the
     * background (Panthera's audit, P54).  An export goes on: it is the person's file being written. */
    fun stopPreview() = requests.stop(previewsOnly = true)

    /** Any thread: every Speak and export begun (never another user's utterance: #51). */
    fun stop() {
        requests.stop(previewsOnly = false)
        // the stopbreak control (#51's): the screen's stop reaches the engine whether or not it began anything, as it
        // did, so it ends whatever utterance owns it -- the screen reader's too
        if (SsiEngine.stopBreak) SsiEngine.stop(this)
    }

    // MODE_STREAM, written in pieces so a stop cuts it short.  Accessibility usage, as a screen reader's speech.
    private fun play(pcm: ByteArray, rate: Int, req: PreviewRequests.Request) {
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
            while (off < pcm.size && !requests.cancelled(req)) {
                val w = track.write(pcm, off, minOf(8192, pcm.size - off))   // blocks, pacing playback
                if (w <= 0) break
                off += w
            }
            // a write only queues the audio: wait for the playback head, with a deadline
            val frames = off / 2
            val deadline = android.os.SystemClock.elapsedRealtime() + frames * 1000L / rate + 2000
            while (!requests.cancelled(req) && track.playbackHeadPosition < frames &&
                   android.os.SystemClock.elapsedRealtime() < deadline) Thread.sleep(20)
            Log.i("Preview", "played ${minOf(track.playbackHeadPosition, frames)} of ${pcm.size / 2} samples" +
                  if (requests.cancelled(req)) ", stopped" else "")
        } finally {
            try { track.stop() } catch (e: Exception) {}
            track.release()
        }
    }
}
