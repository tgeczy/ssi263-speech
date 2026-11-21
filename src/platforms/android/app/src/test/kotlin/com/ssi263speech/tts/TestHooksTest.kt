package com.ssi263speech.tts

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** What the exported screens take from an intent (TestHooks.kt; Panthera's audit P40): a test hook only in a debug
 * build and only on a fresh start; the adb route's import and removal only on a fresh start, asking first in a release
 * build.  The control, -Pssi263ImportBreak=hooks, takes every intent as before: these cases must FAIL. */
class TestHooksTest {
    @Test fun aReleaseBuildTakesNoTestHook() {
        assertFalse("a release build ran a test hook another app could send", TestHooks.on(debuggable = false, fresh = true))
        assertFalse(TestHooks.on(debuggable = false, fresh = false))
    }

    @Test fun aDebugBuildTakesItsHooksOnAFreshStartOnly() {
        assertTrue(TestHooks.on(debuggable = true, fresh = true))
        assertFalse("a test hook ran again when the screen was rebuilt", TestHooks.on(debuggable = true, fresh = false))
    }

    @Test fun theAdbRouteRunsOnceAndAsksFirstInARelease() {
        assertTrue(TestHooks.route(fresh = true))
        assertFalse("an import or a removal ran again when the screen was rebuilt", TestHooks.route(fresh = false))
        assertTrue("a release build imported or removed firmware without asking", TestHooks.confirm(debuggable = false))
        assertFalse(TestHooks.confirm(debuggable = true))
    }
}
