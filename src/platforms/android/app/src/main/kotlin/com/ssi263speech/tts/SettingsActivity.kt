// The app's screen, laid out as outspoken's: two pages behind two plain buttons (a tab bar is hard to hit on a small
// screen, and TalkBack reads a selected/unselected button pair well).  Setup: what this is, the firmware import (the
// Braille Lite's, the Speak-Out's and the Mockingboards', one button, told apart by content), a preview, the way to the
// system's TTS settings, the licences and source.  Voice settings: the voice (the built-in Accents and Mockingboards,
// and the Braille Lite voices and the Speak-Out once imported) and the units' own settings -- every voice's NVDA
// add-on settings (Tomi, 0.7.5).
package com.ssi263speech.tts

import android.app.Activity
import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.StatFs
import android.provider.Settings
import android.util.Log
import android.widget.ProgressBar
import java.io.File
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast

class SettingsActivity : Activity() {

    private lateinit var ui: SettingsWidgets
    private lateinit var status: TextView
    private lateinit var sampleText: EditText
    private lateinit var speakButton: Button
    private lateinit var setupTab: Button
    private lateinit var voiceTab: Button
    private var pages: List<View> = emptyList()
    private var voiceButton: Button? = null
    private lateinit var firmwareStatus: TextView
    private lateinit var importStatus: TextView
    private lateinit var removeButton: Button
    private var importDialog: AlertDialog? = null
    private var importBar: ProgressBar? = null
    private var importMessage: TextView? = null
    private var importAnnounced = -1
    private var importAnnouncedAt = 0L
    private var importLogged = 0
    private val ticker = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        ui = SettingsWidgets(this)
        val setupPage = ui.column().also { buildSetup(it) }
        val voicePage = ui.column().also { buildVoice(it) }

        setupTab = Button(this).apply { text = "Setup"; setOnClickListener { show(0) } }
        voiceTab = Button(this).apply { text = "Voice settings"; setOnClickListener { show(1) } }
        val wide = resources.configuration.screenWidthDp >= 340
        val tabs = LinearLayout(this).apply {
            orientation = if (wide) LinearLayout.HORIZONTAL else LinearLayout.VERTICAL
            addView(setupTab, tabParams(wide))
            addView(voiceTab, tabParams(wide))
        }
        val holder = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(ScrollView(this@SettingsActivity).apply { addView(setupPage) })
            addView(ScrollView(this@SettingsActivity).apply { addView(voicePage) })
        }
        pages = listOf(holder.getChildAt(0), holder.getChildAt(1))
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(tabs)
            addView(holder)
        }
        // Android 15 draws every activity edge to edge: without the insets the tab strip sits under the status bar.
        root.setOnApplyWindowInsetsListener { view, insets ->
            val bars = systemBarInsets(insets)
            view.setPadding(bars[0], bars[1], bars[2], bars[3])
            insets
        }
        setContentView(root)
        show(savedInstanceState?.getInt("page") ?: 0)
        refreshStatus()

        // Test hook: `am start -n com.ssi263speech.tts/.SettingsActivity --ez autospeak true [--es text "..."]`
        // speaks without navigating to the button.  Harmless in normal use (the extra is never set).
        if (intent?.getBooleanExtra("autospeak", false) == true) {
            intent.getStringExtra("text")?.let { sampleText.setText(it) }
            speak()
        }
        // Test hook: `--ei setvoice <n>` chooses the voice (0 Braille Lite English, 1 Spanish, 2 Accent SA, 3 Speak-Out,
        // 4 Accent-mini, 5 Mockingboard, 6 Mockingboard early; -1 forgets the choice, so the default applies), as the Voice button would;
        // test_device_service.py puts it back after.
        intent?.takeIf { it.hasExtra("setvoice") }?.getIntExtra("setvoice", -1)?.let { v ->
            val p = SsiSettings.prefs(this).edit()
            (if (v < 0) p.remove(SsiSettings.VOICE) else p.putInt(SsiSettings.VOICE, v)).commit()
            Log.i("SsiSettings", "voice set by the test hook: $v -> ${SsiEngine.voiceFor(this, SsiSettings.snapshot(this).voice).name}")
            refreshStatus()
        }
        // Test hook: `--ei setrunahead <0|1>` sets the Braille Lite's run ahead, as its check box would (-1 forgets it:
        // off, the default); test_device_service.py puts it back after.
        intent?.takeIf { it.hasExtra("setrunahead") }?.getIntExtra("setrunahead", -1)?.let { v ->
            val p = SsiSettings.prefs(this).edit()
            (if (v < 0) p.remove(SsiSettings.RUN_AHEAD) else p.putBoolean(SsiSettings.RUN_AHEAD, v == 1)).commit()
            Log.i("SsiSettings", "run ahead set by the test hook: ${SsiSettings.snapshot(this).runAhead}")
        }
        // Test hook: the TTS service through Android's client (TtsSelfTest.kt).
        if (intent?.getBooleanExtra("ttstest", false) == true) {
            TtsSelfTest(this, intent.getStringExtra("text") ?: sampleText.text.toString(),
                intent.getFloatExtra("rate", 1f), intent.getFloatExtra("pitch", 1f),
                intent.getBooleanExtra("aloud", false), intent.getIntExtra("stop", 0)).run()
        }

        // An import already running -- this screen was rebuilt underneath it -- shows its progress again.
        // Otherwise, the adb route, as outspoken's: `am start ... --es import /path/to/file` (or a content: URI)
        // imports without the confirm dialog, typing the command being the consent.  `--ez removefirmware true`
        // removes what was imported.
        SsiImport.job?.let { attachImport(it) } ?: intent?.getStringExtra("import")?.let { arg ->
            val source = if (arg.startsWith("content:") || arg.startsWith("file:"))
                SsiImport.source(this, Uri.parse(arg)) else SsiImport.source(File(arg))
            importFrom(source, confirm = false)
        }
        if (intent?.getBooleanExtra("removefirmware", false) == true) removeFirmware(confirm = false)
    }

    override fun onResume() {
        super.onResume()
        refreshStatus()
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQUEST_FIRMWARE) return
        val uri = data?.data
        if (resultCode != RESULT_OK || uri == null) { importStatus.text = "No file chosen."; return }
        importFrom(SsiImport.source(this, uri), confirm = true)
    }

    override fun onSaveInstanceState(out: Bundle) {
        out.putInt("page", if (pages.getOrNull(1)?.visibility == View.VISIBLE) 1 else 0)
        super.onSaveInstanceState(out)
    }

    override fun onDestroy() {
        PreviewPlayer.stop()
        SsiImport.job?.listener = null
        ticker.removeCallbacksAndMessages(null)
        importDialog?.dismiss(); importDialog = null
        super.onDestroy()
    }

    @Suppress("DEPRECATION")
    private fun systemBarInsets(insets: WindowInsets): IntArray =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val bars = insets.getInsets(WindowInsets.Type.systemBars())
            intArrayOf(bars.left, bars.top, bars.right, bars.bottom)
        } else {
            intArrayOf(insets.systemWindowInsetLeft, insets.systemWindowInsetTop,
                       insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
        }

    private fun tabParams(wide: Boolean) =
        if (wide) LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        else LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)

    private fun show(page: Int) {
        pages.forEachIndexed { i, v -> v.visibility = if (i == page) View.VISIBLE else View.GONE }
        setupTab.isSelected = page == 0
        voiceTab.isSelected = page == 1
        // A screen-reader user whose focus is still on the tab hears nothing of the swap unless it is announced.
        setupTab.contentDescription = "Setup, tab 1 of 2"
        voiceTab.contentDescription = "Voice settings, tab 2 of 2"
        try { window.decorView.announceForAccessibility(if (page == 0) "Setup page" else "Voice settings page") }
        catch (e: Throwable) { /* a courtesy, never a failure */ }
    }

    // ---- page 1: setup ---------------------------------------------------------------------------------------------

    private fun buildSetup(root: LinearLayout) {
        root.addView(TextView(this).apply { text = "SSI-263 Speech"; textSize = 26f; gravity = Gravity.CENTER })
        root.addView(ui.body(
            "Talking hardware of the 1980s, emulated around a model of the Silicon Systems SSI-263 speech chip. " +
            "Each voice is the device's own firmware, running live: the rules, the number reading and the " +
            "inflection are its own, and nothing is recorded. They are the same voices as the NVDA add-ons, " +
            "byte for byte."))
        root.addView(ui.body(
            "\nBuilt in: the Aicom Accent SA, a 1980s speech synthesizer, its own 8085 firmware and dictionary ROMs " +
            "coming with this app; the Aicom Accent-mini, its DOS driver on an emulated PC, when this copy of the " +
            "app carries it; and the Sweet Micro Systems Mockingboard, the Apple II's speech card, its own " +
            "text-to-speech on an emulated 6502 -- version 1.1 from the Developers Toolkit, and the early one from " +
            "Mockingboard disk 1 -- when this copy carries them. They speak English, and the Accent SA is the voice " +
            "you hear until you import a Braille Lite."))
        root.addView(ui.body(
            "\nTo add: the Blazie Braille Lite 2000 in speech-box mode, its June 2003 firmware on an emulated Z180, " +
            "in English and Spanish; and the GW Micro Speak-Out, its own firmware on an emulated V40, in English."))

        root.addView(ui.heading("Firmware to import"))
        root.addView(ui.body(
            "The Braille Lite's firmware is Blazie's, and the Speak-Out's GW Micro's: neither can come with this " +
            "app, so import your own copy. One button takes any of them; the app tells them apart by what is in the " +
            "file. " +
            "For the Braille Lite, choose the Braille Lite 2000's update program (such as blt2000.exe), the " +
            "BL2ENG.BNS (English) or BL2SPA.BNS (Spanish) inside it, a zip holding them, or the NVDA add-on " +
            "(.nvda-addon), which carries both. This phone then prepares the unit once, as the add-on's was " +
            "prepared: a few seconds for English, about a minute for Spanish. For the Speak-Out, choose GW Micro's " +
            "SPEAKOUT.HEX, or the speakout.zip holding it; it is checked and ready in a moment. The Mockingboards " +
            "can be imported too, in place of the copies built in, or when this copy of the app has none: the " +
            "Mockingboard Developers Toolkit's disk image (.dsk) or the mockingboard-tts-1.1.bin made from it, and " +
            "Mockingboard disk 1's image or mockingboard-tts-early.bin for the early one; each is checked and ready " +
            "in a moment too. The files stay in this app's protected storage."))
        firmwareStatus = ui.body("")
        root.addView(firmwareStatus)
        root.addView(Button(this).apply { text = "Import firmware…"; setOnClickListener { pickFile() } })
        removeButton = Button(this).apply { text = "Remove firmware…"; setOnClickListener { removeFirmware(true) } }
        root.addView(removeButton)
        importStatus = ui.body("")
        root.addView(importStatus)

        root.addView(ui.heading("Status"))
        status = ui.body("")
        root.addView(status)

        root.addView(ui.heading("Try it"))
        val sampleLabel = ui.body("Text to speak").also { root.addView(it) }
        sampleText = EditText(this).apply {
            id = View.generateViewId()
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE
            setText("Hello there. This is SSI-263 Speech, speaking on your phone. You owe 1,234 dollars.")
            textSize = 15f
        }
        sampleLabel.labelFor = sampleText.id
        root.addView(sampleText)
        speakButton = Button(this).apply { text = "Speak"; setOnClickListener { speak() } }
        root.addView(speakButton)
        root.addView(Button(this).apply { text = "Stop"; setOnClickListener { PreviewPlayer.stop() } })

        root.addView(ui.body("\nThen choose this engine in the system's Text-to-speech settings:"))
        root.addView(Button(this).apply { text = "Open Text-to-speech settings"; setOnClickListener { openTtsSettings() } })

        root.addView(ui.heading("Licenses and source"))
        root.addView(ui.body(
            "This app is free software under the MIT License; its emulated processors are MAME's, under their " +
            "BSD-3-Clause licenses, and the Mockingboard's 6502 is Mike Chambers' Fake6502 (public domain), by way " +
            "of Jayson Smith's EchoTalk (BSD-3-Clause). Its source is at github.com/tgeczy/ssi263-speech. The " +
            "Accent SA's firmware is Aicom's, carried with a notice, and is not covered by those licenses. The " +
            "Mockingboard's text-to-speech is Sweet Micro Systems': it is not ours, it is here so the card can " +
            "speak again, it will be removed if its rights holders ask, and it is not covered by the MIT license. " +
            "The app carries no Braille Lite or Speak-Out firmware: the copy you import is Blazie's or GW Micro's, " +
            "is not covered by them either, and never leaves this phone."))
        root.addView(Button(this).apply { text = "Licenses and source"; setOnClickListener { showLicenses() } })
    }

    private fun refreshStatus() {
        val voices = SsiEngine.voices(this)
        val labels = FirmwareImport.IMPORTED.mapNotNull { SsiData.label(this, it) }
        firmwareStatus.text = if (labels.isEmpty()) "No firmware imported."
            else labels.joinToString("\n") { "$it, imported." }
        removeButton.isEnabled = labels.isNotEmpty()
        val current = SsiEngine.voiceFor(this, SsiSettings.snapshot(this).voice)
        status.text = if (voices.isEmpty()) "No voice can speak: this copy of the app is missing the Accent SA."
            else "Voices: " + voices.joinToString(", ") { it.label } + ". Speaking with: ${current.label}."
        speakButton.isEnabled = voices.isNotEmpty()
        voiceButton?.text = "Voice: " + current.label
    }

    // ---- the firmware import (FirmwareImport.kt, SsiImport.kt), as outspoken's zip import ------------------------

    /** The system's file picker, on any file: firmware comes as .BNS, .exe, .zip and .nvda-addon alike. */
    private fun pickFile() {
        if (SsiImport.job != null) { toast("An import is already running."); return }
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
        }
        try { startActivityForResult(intent, REQUEST_FIRMWARE) }
        catch (e: ActivityNotFoundException) {
            AlertDialog.Builder(this).setTitle("No file picker on this device")
                .setMessage("This device has no file picker, so there is no way to choose the firmware on it.")
                .setPositiveButton("Close", null).show()
        }
    }

    /** Read what the source holds, say what will happen, and -- with `confirm`, once OK is pressed -- do it. */
    private fun importFrom(source: SsiImport.Source, confirm: Boolean) {
        if (SsiImport.job != null) { toast("An import is already running."); return }
        importStatus.text = "Checking ${source.name}…"
        val checking = AlertDialog.Builder(this).setTitle("Checking the file")
            .setMessage("Looking for firmware in ${source.name}…").setCancelable(false).create()
            .also { it.show() }
        Thread({
            val plan = try { SsiImport.inspect(this, source) }
                catch (e: Exception) { FirmwareImport.Plan(emptyList(), "${source.name} could not be read: ${e.message}") }
            runOnUiThread {
                checking.dismiss()
                if (isFinishing || isDestroyed) return@runOnUiThread
                val free = try { StatFs(filesDir.absolutePath).availableBytes } catch (e: Exception) { -1L }
                val refusal = plan.refusal ?: if (free in 0 until (16L shl 20))
                    "Not enough room: the firmware and the unit's state need about 3 MB, and this device has " +
                    "${free shr 10} KB free." else null
                if (refusal != null) {
                    // Said in the status line (it stays on the page), in a dialog, and aloud: a screen reader reads
                    // the dialog's title, so the reason is announced as well, as the import's other outcomes are.
                    importStatus.text = "Not imported: $refusal"
                    Log.i("SsiImport", "refused ${source.name}: $refusal")
                    AlertDialog.Builder(this).setTitle("Cannot import ${source.name}").setMessage(refusal)
                        .setPositiveButton("Close", null).show()
                    try { importStatus.announceForAccessibility(refusal) } catch (e: Throwable) {}
                    return@runOnUiThread
                }
                confirmImport(plan, confirm)
            }
        }, "ssi263-inspect").start()
    }

    private fun confirmImport(plan: FirmwareImport.Plan, confirm: Boolean) {
        val lines = plan.found.map { f ->
            val name = FirmwareImport.languageName(f.language)
            "Will import ${f.label} (from ${f.from}). " +
                (when (f.language) {
                    SsiNative.SPANISH -> "The unit is then prepared on this phone, which takes about a minute."
                    in FirmwareImport.NO_STATE -> "It is then checked on this phone, which takes a moment."
                    else -> "The unit is then prepared on this phone, which takes a few seconds."
                }) +
                (if (SsiData.imported(this, f.language)) " The $name firmware already here will be replaced."
                 else if (SsiData.has(this, f.language)) " It is used instead of the copy built into this app."
                 else "")
        }
        val message = (lines + plan.notes).joinToString("\n\n")
        if (!confirm) { startImport(plan); return }
        AlertDialog.Builder(this).setTitle("Import the firmware?").setMessage(message)
            .setPositiveButton("OK") { _, _ -> startImport(plan) }
            .setNegativeButton("Cancel") { _, _ -> importStatus.text = "Not imported." }
            .show()
    }

    private fun startImport(plan: FirmwareImport.Plan) {
        val job = SsiImport.Job(plan)
        SsiImport.job = job
        attachImport(job)
        job.start(this)
    }

    /** Show a running import's progress on this screen, whichever screen instance this is. */
    private fun attachImport(job: SsiImport.Job) {
        importDialog?.dismiss()
        val box = ui.column()
        importMessage = ui.body("Importing the firmware…").also { box.addView(it) }
        importBar = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            max = 1000
            contentDescription = "Import progress"
            box.addView(this)
        }
        box.addView(ui.body("Keep the app open until it finishes."))
        importDialog = AlertDialog.Builder(this).setTitle("Importing the firmware").setView(box).setCancelable(false)
            .setNegativeButton("Cancel") { _, _ -> job.cancel(); importStatus.text = "Cancelling…" }
            .create().also { it.show() }
        importAnnounced = -1
        importLogged = 0
        importAnnouncedAt = android.os.SystemClock.elapsedRealtime()   // the dialog is being read out now
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        job.listener = { j -> runOnUiThread { showImportProgress(j) } }
        tick(job)
    }

    /** The state is made in native code that reports how far it is, so the screen asks four times a second. */
    private fun tick(job: SsiImport.Job) {
        showImportProgress(job)
        if (job.result == null) ticker.postDelayed({ tick(job) }, 250)
    }

    private fun showImportProgress(job: SsiImport.Job) {
        if (isFinishing || isDestroyed || SsiImport.job !== job) return
        if (job.result != null) { finishImport(job); return }
        val permille = (job.progress() * 1000).toInt().coerceIn(0, 1000)
        importBar?.progress = permille
        importMessage?.text = "${job.step.ifEmpty { "Importing the firmware" }}… ${permille / 10}%"
        // The bar's course in the log, a line per tenth (adb logcat -s SsiImport): how a device test sees it move.
        if (permille / 100 > importLogged) {
            importLogged = permille / 100
            Log.i("SsiImport", "progress ${permille / 10}%: ${job.step}")
        }
        // A screen reader hears the dialog once; the percentage moving is silent unless said.  Every fifth, and no
        // closer than 5 seconds apart: Spanish's minute is said four times, English's few seconds once at most.
        val fifth = permille / 200
        val now = android.os.SystemClock.elapsedRealtime()
        if (fifth > importAnnounced && fifth in 1..4 && now - importAnnouncedAt >= 5000) {
            importAnnounced = fifth
            importAnnouncedAt = now
            try { importMessage?.announceForAccessibility("${fifth * 20} percent") } catch (e: Throwable) {}
            Log.i("SsiImport", "announced ${fifth * 20} percent")
        }
    }

    private fun finishImport(job: SsiImport.Job) {
        job.listener = null
        SsiImport.job = null
        ticker.removeCallbacksAndMessages(null)
        importDialog?.dismiss(); importDialog = null
        window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val result = job.result ?: return
        result.onSuccess { labels ->
            refreshStatus()
            importStatus.text = "Imported ${labels.joinToString(" and ")}. The voice speaks now."
            Log.i("SsiImport", importStatus.text.toString())
            try { importStatus.announceForAccessibility(importStatus.text) } catch (e: Throwable) {}
        }.onFailure { e ->
            refreshStatus()
            if (e is SsiImport.Cancelled) {
                importStatus.text = "Import cancelled. Nothing was changed."
            } else {
                Log.e("SsiImport", "import failed", e)
                importStatus.text = "Import failed. Nothing was changed."
                AlertDialog.Builder(this).setTitle("Import failed").setMessage(e.message ?: e.toString())
                    .setPositiveButton("Close", null).show()
            }
            try { importStatus.announceForAccessibility(importStatus.text) } catch (e: Throwable) {}
        }
    }

    private fun removeFirmware(confirm: Boolean) {
        if (SsiImport.job != null) { toast("An import is running."); return }
        val remove = {
            PreviewPlayer.stop()
            SsiData.remove(this)
            refreshStatus()
            importStatus.text = "The imported firmware was removed. The Braille Lite and Speak-Out voices cannot " +
                "speak until it is imported again; the Accent SA speaks meanwhile, and the Mockingboards with the " +
                "copies built into this app, when it has them."
            try { importStatus.announceForAccessibility(importStatus.text) } catch (e: Throwable) {}
        }
        if (!confirm) { remove(); return }
        AlertDialog.Builder(this).setTitle("Remove the imported firmware?")
            .setMessage("The imported Braille Lite, Speak-Out and Mockingboard firmware goes, and the Braille Lite " +
                "and Speak-Out voices cannot speak until you import it again. The built-in voices stay: the Accents, " +
                "and the Mockingboards' own copies when this app has them.")
            .setPositiveButton("Remove") { _, _ -> remove() }
            .setNegativeButton("Cancel", null).show()
    }

    private fun toast(text: String) = Toast.makeText(this, text, Toast.LENGTH_SHORT).show()

    private fun speak() {
        val text = sampleText.text.toString().ifBlank { "Hello there." }
        speakButton.isEnabled = false
        status.text = "Speaking…"
        Thread({
            val n = PreviewPlayer.speak(this, text)
            runOnUiThread {
                if (isFinishing || isDestroyed) return@runOnUiThread
                speakButton.isEnabled = true
                status.text = if (n < 0) "The voice could not start: ${SsiNative.nativeError()}" else "Spoke $n samples."
            }
        }, "ssi263-preview").start()
    }

    private fun openTtsSettings() {
        for (i in listOf(Intent("com.android.settings.TTS_SETTINGS"), Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS),
                         Intent(Settings.ACTION_SETTINGS))) {
            try { i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK); startActivity(i); return }
            catch (e: Exception) { /* try the next */ }
        }
        Toast.makeText(this, "Couldn't open settings on this device.", Toast.LENGTH_SHORT).show()
    }

    /** outspoken's flow: the distribution notice, then every licence the APK carries. */
    private fun showLicenses() {
        fun read(name: String) = assets.open("licenses/$name").bufferedReader().use { it.readText() }
        fun title(name: String) = name.removeSuffix(".txt").replace('-', ' ')
        AlertDialog.Builder(this).setTitle("Licenses and source").setMessage(read("DISTRIBUTION.txt"))
            .setPositiveButton("Close", null)
            .setNeutralButton("Full licenses") { _, _ ->
                val files = assets.list("licenses").orEmpty().filter { it != "DISTRIBUTION.txt" }.sorted()
                AlertDialog.Builder(this).setTitle("Full licenses")
                    .setItems(files.map { title(it) }.toTypedArray()) { _, which ->
                        AlertDialog.Builder(this).setTitle(title(files[which])).setMessage(read(files[which]))
                            .setPositiveButton("Close", null).show()
                    }.setNegativeButton("Close", null).show()
            }.show()
    }

    // ---- page 2: voice settings ------------------------------------------------------------------------------------

    private fun buildVoice(root: LinearLayout) {
        val p = SsiSettings.prefs(this)
        val s = SsiSettings.snapshot(this)
        fun put(key: String, v: Int) = p.edit().putInt(key, v).apply()
        fun put(key: String, v: Boolean) = p.edit().putBoolean(key, v).apply()

        val voiceLabel = ui.heading("Voice").also { root.addView(it) }
        voiceButton = Button(this).apply {
            id = View.generateViewId()
            text = "Voice: " + SsiEngine.voiceFor(this@SettingsActivity, s.voice).label
            setOnClickListener {
                val voices = SsiEngine.voices(this@SettingsActivity)     // the Accent SA, and what is imported now
                if (voices.isEmpty()) { toast("No voice can speak in this copy of the app."); return@setOnClickListener }
                val at = voices.indexOfFirst {
                    it.index == SsiEngine.voiceFor(this@SettingsActivity, SsiSettings.snapshot(this@SettingsActivity).voice).index
                }
                AlertDialog.Builder(this@SettingsActivity).setTitle("Voice")
                    .setSingleChoiceItems(voices.map { it.label }.toTypedArray(), at) { dialog, which ->
                        dialog.dismiss()
                        put(SsiSettings.VOICE, voices[which].index)
                        text = "Voice: " + voices[which].label
                    }.setNegativeButton("Cancel", null).show()
            }
        }
        voiceLabel.labelFor = voiceButton!!.id
        root.addView(voiceButton)
        ui.checkBox(root, "Use selected voice in all apps", s.overrideVoice) { put(SsiSettings.OVERRIDE_VOICE, it) }
        root.addView(ui.body("The Accents and the two Mockingboards are built in; the Braille Lite voices and the Speak-Out join them once " +
            "their firmware is imported on the Setup page. A screen reader asks this engine for a voice once and keeps it; with " +
            "this on, the voice chosen here is heard straight away. A request in Spanish gets the Spanish Braille " +
            "Lite either way, when it is here."))

        root.addView(ui.heading("Rate"))
        root.addView(ui.body("The NVDA add-ons' scale: 50 is the unit's factory rate. The rate an app asks for -- " +
            "the system's speech rate or a screen reader's own -- is applied on top."))
        ui.slider(root, "Speech rate", 100, s.rate, { if (it == 50) "50, the unit's factory rate" else "$it" }) {
            put(SsiSettings.RATE, it)
        }

        root.addView(ui.heading("Pitch"))
        root.addView(ui.body("50 is the unit's factory pitch; an app's pitch is applied on top, such as a screen " +
            "reader's higher pitch for capital letters."))
        ui.slider(root, "Pitch", 100, s.pitch, { if (it == 50) "50, the unit's factory pitch" else "$it" }) {
            put(SsiSettings.PITCH, it)
        }

        root.addView(ui.heading("Tone"))
        root.addView(ui.body("The Braille Lite's tone setting, 0 to 26. 7 is the factory tone. The Speak-Out has " +
            "its own, below; the Accents and the Mockingboards have none."))
        ui.slider(root, "Tone", 26, s.tone, { if (it == 7) "7, factory" else "$it" }) { put(SsiSettings.TONE, it) }

        root.addView(ui.heading("Volume"))
        root.addView(ui.body("The engine's own level, for every voice; Android's accessibility or media volume " +
                "still applies. 100 percent is the desktop voices' level; the default, 150, sits beside TalkBack's " +
                "own sounds. Above about 150 the loudest syllables can clip."))
        ui.slider(root, "Engine volume", SsiSettings.MAX_VOLUME, s.volume,
                { if (it == SsiSettings.DEFAULT_VOLUME) "$it percent, default" else "$it percent" }) {
            put(SsiSettings.VOLUME, it)
        }

        root.addView(ui.heading("The unit"))
        ui.checkBox(root, "Voice inflection", s.inflection) { put(SsiSettings.INFLECTION, it) }
        root.addView(ui.body("The unit's own intonation. Off, questions stay flat: the Braille Lite's status-menu " +
            "setting, and the Accent SA's monotone."))
        ui.checkBox(root, "Short pauses", s.shortPauses) { put(SsiSettings.SHORT_PAUSES, it) }
        root.addView(ui.body("Braille Lite only: sentences packed onto one line from the second on, as the NVDA " +
            "add-on's default."))
        ui.checkBox(root, "Read numbers as words", s.numbers) { put(SsiSettings.NUMBERS, it) }
        root.addView(ui.body("The Braille Lite and the Mockingboards, on by default, as the NVDA add-on's custom " +
            "number processing: 1,234,567 as one number, read in words, and the Spanish voice in Spain's way " +
            "(1.234.567, and 3,5 as tres coma cinco). Off, the unit's own firmware reads them (the Mockingboard's " +
            "digit by digit). The Accents keep their own add-on's number processing on."))
        ui.checkBox(root, "Run the unit ahead (experimental: with short pauses)", s.runAhead) {
            put(SsiSettings.RUN_AHEAD, it)
        }
        root.addView(ui.body("Braille Lite only, experimental and off by default, as in the NVDA add-on: the unit " +
            "runs ahead of its speech chip, so long text starts sooner. It needs short pauses on."))

        ui.choice(root, "Idle sound", listOf("Off", "Hiss", "Whine"), s.whine) { put(SsiSettings.WHINE, it) }
        root.addView(ui.body("Braille Lite only: the faint sound a real unit makes under its speech, hiss at even " +
            "volumes (the factory setting), whine at odd ones."))

        ui.choice(root, "Sample rate", listOf("11 kHz, like the unit's own speaker", "22 kHz (recommended)",
            "44 kHz"), SsiSettings.SAMPLE_RATES.indexOf(s.sampleRate)) { put(SsiSettings.SAMPLE_RATE, SsiSettings.SAMPLE_RATES[it]) }
        root.addView(ui.body("Inflection, the idle sound and the sample rate restart the unit on the next " +
            "utterance, which takes a moment."))

        root.addView(ui.heading("The Speak-Out"))
        ui.slider(root, "Speak-Out tone", 25, s.soTone, { tone ->
            "${'A' + tone}" + if (tone == SsiSettings.SO_DEFAULT_TONE) ", the box's own" else ""
        }) { put(SsiSettings.SO_TONE, it) }
        root.addView(ui.body("The Speak-Out's tones A to Z, as its NVDA add-on offers them; I is the box's own."))
        ui.checkBox(root, "Join phrases (fewer pauses between words)", s.soJoin) { put(SsiSettings.SO_JOIN, it) }
        ui.checkBox(root, "Shorten pauses between sentences", s.soShortPauses) { put(SsiSettings.SO_SHORT_PAUSES, it) }
        root.addView(ui.body("Speak-Out only, both on by default, as in its NVDA add-on."))
    }

    private companion object { const val REQUEST_FIRMWARE = 42 }
}
