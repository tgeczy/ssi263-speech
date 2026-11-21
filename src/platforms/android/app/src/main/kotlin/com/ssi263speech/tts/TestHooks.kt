// When the two exported screens -- the settings (the app's launcher) and the Blazie unit screen -- take what an intent
// asks of them (no Android in it, so the JVM tests run it).  Any installed app can start an exported screen with any
// extras: `removefirmware` deleted the imported firmware without a word, `import` imported a file it named without
// asking, and the test hooks (autospeak, setvoice, setrunahead, ttstest, autostart, the tests' *break controls) did
// what they do for anyone -- and on every rebuild of the screen (the screen turned, dark mode), the same intent again
// (Panthera's audit, P40; Tomi chose, 2026-11-21: debug-only hooks).
package com.ssi263speech.tts

object TestHooks {
    // The JVM tests' must-fail control: -Pssi263ImportBreak=hooks takes every intent as before -- any build, every
    // start, no confirm
    private val CONTROL = System.getProperty("ssi263.import.break") ?: ""

    /** A test hook: a debug build's (the device tests'), and on the screen's fresh start only, never its rebuild. */
    fun on(debuggable: Boolean, fresh: Boolean): Boolean = if (CONTROL == "hooks") true else debuggable && fresh

    /** The adb route (`--es import <file>`, `--ez removefirmware true`): on a fresh start only. */
    fun route(fresh: Boolean): Boolean = CONTROL == "hooks" || fresh

    /** ... and with its confirm dialog first in a release build, where another app may have sent it; a debug build's
     * device tests type the command, which is the consent. */
    fun confirm(debuggable: Boolean): Boolean = if (CONTROL == "hooks") false else !debuggable
}
