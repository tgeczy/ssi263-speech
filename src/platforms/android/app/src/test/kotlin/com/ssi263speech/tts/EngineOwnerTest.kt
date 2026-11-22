package com.ssi263speech.tts

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/** The engine's one stop flag and who may set it (EngineOwner.kt, #51).  `flag` stands for ssa_engine.c's e->stop: a
 * native stop sets it, an utterance's start clears it after its owner is taken.  The control,
 * -Pssi263ImportBreak=owner, checks the owner apart from the stop, as it was: the stale-stop case must FAIL. */
class EngineOwnerTest {
    private val flag = AtomicBoolean(false)
    private val owners = EngineOwner { flag.set(true) }
    private val preview = Any()
    private val service = Any()

    private fun start(by: Any) { owners.take(by); flag.set(false) }

    @Test fun aStopEndsItsOwnersUtteranceOnly() {
        start(preview)
        owners.stop(service)
        assertFalse("the service's stop ended the preview's utterance", flag.get())
        owners.stop(preview)
        assertTrue("the preview's own stop did not end its utterance", flag.get())
    }

    /** An independent review: the preview's stop passes its owner check, is descheduled, and the TTS service's
     * utterance starts meanwhile; the stop, resumed, must not end the service's utterance. */
    @Test fun aStaleStopDoesNotEndTheNextOwnersUtterance() {
        start(preview)
        val checked = CountDownLatch(1)
        val started = CountDownLatch(1)
        owners.afterCheck = {
            checked.countDown()
            started.await(500, TimeUnit.MILLISECONDS)       // the service's start, if it can come meanwhile
        }
        val stopper = Thread { owners.stop(preview) }
        val speaker = Thread {
            checked.await(5, TimeUnit.SECONDS)
            start(service)
            started.countDown()
        }
        stopper.start(); speaker.start()
        stopper.join(5000); speaker.join(5000)
        owners.afterCheck = null
        assertFalse("the preview's stop, checked before the service's start, ended the service's utterance", flag.get())
    }
}
