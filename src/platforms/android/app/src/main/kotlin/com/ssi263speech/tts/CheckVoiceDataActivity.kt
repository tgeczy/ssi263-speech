// The framework fires ACTION_CHECK_TTS_DATA to ask whether the engine's voice data is usable.  English is always
// there: the Accent-PC is built in.  Spanish, German, Italian and Slovak are there once the user has imported that
// Blazie unit's firmware (the settings screen, which also answers INSTALL_TTS_DATA); until then each is reported
// unavailable, never available (SsiLanguages).  ISO-3 locales, not voice names, as TGSpeechBox's CheckVoiceData.
package com.ssi263speech.tts

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.speech.tts.TextToSpeech

class CheckVoiceDataActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val available = ArrayList(SsiLanguages.tags(SsiEngine.voices(this)))
        val unavailable = ArrayList(SsiLanguages.tags(SsiEngine.KNOWN).filter { it !in available })
        val data = Intent().apply {
            putStringArrayListExtra(TextToSpeech.Engine.EXTRA_AVAILABLE_VOICES, available)
            putStringArrayListExtra(TextToSpeech.Engine.EXTRA_UNAVAILABLE_VOICES, unavailable)
        }
        setResult(if (available.isNotEmpty()) TextToSpeech.Engine.CHECK_VOICE_DATA_PASS
                  else TextToSpeech.Engine.CHECK_VOICE_DATA_FAIL, data)
        finish()
    }
}
