// Who owns the engine's utterance -- the TTS service, or the settings screen's Speak or export -- and the stop that
// must end only its owner's (#51).  The engine has one stop flag (ssa_engine.c e->stop), which ends whichever
// utterance owns the engine at its next pull.  No Android in it, so the JVM tests run it (EngineOwnerTest).
package com.ssi263speech.tts

class EngineOwner(private val nativeStop: () -> Unit) {
    // The owner's check and the native stop are one step under this lock, and so is a new owner's taking: a stop
    // checked before another's utterance was taken, its thread descheduled before it set the flag, ended that
    // utterance (an independent review).  Now such a stop lands before the taking, and the engine's start, which
    // follows it, clears the flag.  Not the engine's lock: a stop must not wait for a unit's first start (up to a
    // minute, the UI thread's onStop and onDestroy among the callers), and the native stop is one atomic store.
    private val lock = Any()
    private var owner: Any? = null

    // Test hook (the screen's --ez stopbreak true, test_engine_stop_owner.py's control): any stop ends whatever
    // utterance owns the engine, as before #51
    @Volatile var stopBreak = false

    // EngineOwnerTest's: run between a stop's check of the owner and its native stop (the thread descheduled there)
    @Volatile internal var afterCheck: (() -> Unit)? = null

    /** `by` owns the utterance about to start (the engine's start then clears a stop flag set before). */
    fun take(by: Any) = synchronized(lock) { owner = by }

    /** Any thread: end `by`'s utterance, if it is the one the engine is speaking; another's goes on. */
    fun stop(by: Any) {
        if (CONTROL == "owner") { stopNow(by); return }   // the JVM tests' control: the check apart, as it was
        synchronized(lock) { stopNow(by) }
    }

    private fun stopNow(by: Any) {
        if (owner === by || stopBreak) {
            afterCheck?.invoke()
            nativeStop()
        }
    }

    private companion object {
        // -Pssi263ImportBreak=owner: EngineOwnerTest's must-fail control
        val CONTROL: String = System.getProperty("ssi263.import.break") ?: ""
    }
}
