package com.ssi263speech.tts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The settings screen's Speaks and exports and their stops (PreviewRequests.kt).  An independent review: the screen
 * left between the click and the worker's first step, or between that and the render, let the Speak go on in the
 * background (P54's fix covered only a Speak already rendering).  The control, -Pssi263ImportBreak=preview, marks a
 * Speak from its worker and clears the stop in its render, as it was: these cases must FAIL. */
class PreviewRequestsTest {
    private val stopped = mutableListOf<Any>()
    private val requests = PreviewRequests { stopped += it }

    @Test fun theScreenLeftBeforeTheWorkerRunsStopsTheSpeak() {
        val r = requests.begin(preview = true)
        requests.stop(previewsOnly = true)                  // onStop: Home, before the worker's thread ran
        requests.started(r); requests.rendering(r)
        assertTrue("a Speak left before its worker ran went on in the background", requests.cancelled(r))
    }

    @Test fun theScreenLeftBeforeTheRenderStopsTheSpeak() {
        val r = requests.begin(preview = true)
        requests.started(r)
        requests.stop(previewsOnly = true)                  // onStop, between the worker's first step and its render
        requests.rendering(r)
        assertTrue("a Speak left before its render went on in the background", requests.cancelled(r))
    }

    @Test fun anExportDoesNotUndoASpeaksStop() {
        val s = requests.begin(preview = true)
        requests.started(s); requests.rendering(s)          // waiting for the engine behind a screen reader
        requests.stop(previewsOnly = false)                 // Stop
        val e = requests.begin(preview = false)             // Export to WAV..., its button still enabled
        requests.started(e); requests.rendering(e)
        assertTrue("an export's start undid the stop of a Speak still waiting", requests.cancelled(s))
        assertFalse(requests.cancelled(e))
    }

    @Test fun leavingTheScreenStopsTheSpeakNotTheExport() {
        val e = requests.begin(preview = false)
        requests.started(e); requests.rendering(e)
        val s = requests.begin(preview = true)
        requests.started(s); requests.rendering(s)
        requests.stop(previewsOnly = true)
        assertTrue(requests.cancelled(s))
        assertFalse("leaving the screen stopped the person's export", requests.cancelled(e))
        assertEquals("the engine was stopped for another than the Speak", listOf<Any>(s), stopped)
    }

    @Test fun theStopButtonStopsBoth() {
        val e = requests.begin(preview = false)
        requests.started(e); requests.rendering(e)
        val s = requests.begin(preview = true)
        requests.started(s); requests.rendering(s)
        requests.stop(previewsOnly = false)
        assertTrue(requests.cancelled(s))
        assertTrue(requests.cancelled(e))
        requests.end(s); requests.end(e)
        val next = requests.begin(preview = true)
        requests.started(next); requests.rendering(next)
        assertFalse("a new Speak came stopped", requests.cancelled(next))
    }
}
