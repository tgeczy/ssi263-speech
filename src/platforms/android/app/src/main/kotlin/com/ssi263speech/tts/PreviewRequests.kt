// The settings screen's Speaks and exports, and the stops that end them: the Stop button (any of them), the screen
// left (a Speak only: P54).  No Android in it, so the JVM tests run it (PreviewRequestsTest).
//
// Each request carries its own stop, set at the click and never cleared.  They shared one flag, which the render
// cleared, and a Speak was marked as running by its worker: the screen left between the click and the worker's first
// step, or between that and the render, went unheard or was undone, and the Speak talked on in the background; an
// export's render undid the Stop of a Speak still waiting for the engine (an independent review).
package com.ssi263speech.tts

class PreviewRequests(private val stopEngine: (Any) -> Unit) {
    /** One Speak (`preview`) or export: the engine's owner while it renders, so a stop ends this one only. */
    class Request(val preview: Boolean) {
        @Volatile internal var cancelled = false
    }

    private val live = ArrayList<Request>()          // begun and not yet ended; guarded by itself

    /** UI thread, at the click: from here a stop reaches it, before its worker has run. */
    fun begin(preview: Boolean): Request =
        Request(preview).also { if (CONTROL != "preview") synchronized(live) { live += it } }

    /** The worker's first step (the control: a Speak marked as running only here, as it was). */
    fun started(r: Request) { if (CONTROL == "preview") synchronized(live) { live += r } }

    /** The render's, before it waits for the engine (the control: the one shared flag cleared here, as it was). */
    fun rendering(r: Request) {
        if (CONTROL == "preview") synchronized(live) { for (x in live) x.cancelled = false }
    }

    /** The worker's last step. */
    fun end(r: Request) = synchronized(live) { live -= r }

    fun cancelled(r: Request): Boolean = r.cancelled

    /** Any thread: every Speak and export, or (`previewsOnly`: the screen left) the Speaks. */
    fun stop(previewsOnly: Boolean) {
        val hit = synchronized(live) { live.filter { !previewsOnly || it.preview } }
        for (r in hit) {
            r.cancelled = true
            stopEngine(r)
        }
    }

    private companion object {
        // -Pssi263ImportBreak=preview: PreviewRequestsTest's must-fail control
        val CONTROL: String = System.getProperty("ssi263.import.break") ?: ""
    }
}
