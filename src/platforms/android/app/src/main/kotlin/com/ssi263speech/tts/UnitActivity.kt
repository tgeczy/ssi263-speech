// The Braille Lite unit screen: a Blazie unit running in real time on the firmware imported for the voices -- one app,
// no second firmware picker, as Apple's unit tab (src/platforms/apple/README.md).  Its units are the emulator's own
// catalog over the data folder (UnitNative.units: emc_list, present ones only); each unit's memory is the app's own
// (files/units), saved when it is switched off or the screen is left; the voices' states are only read.
//
// The keyboard field holds the display as Unicode braille (empty for a unit with none) and takes a hardware
// keyboard's keys as they go down and come up, by their places (scan codes): for the Braille Lite F D S J K L dots 1
// to 6, the space bar, ; the advance bar and A the back bar, a chord sent when its last key comes up; for the Type
// 'n Speak every key its own, Shift, Ctrl and Alt included.  Other keys (Tab, the Back key) are the screen's.  Whether
// TalkBack sends the field to a braille display, and how many keys a keyboard reports down at once, are for testers.
package com.ssi263speech.tts

import android.app.Activity
import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import java.util.Calendar
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.Process
import android.util.Log
import android.view.KeyEvent
import android.view.View
import android.view.WindowInsets
import android.widget.Button
import android.widget.LinearLayout
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.TextView
import java.io.File

class UnitActivity : Activity() {
    private lateinit var ui: SettingsWidgets
    private lateinit var status: TextView
    private lateinit var choice: RadioGroup
    private lateinit var power: Button
    private lateinit var oneHand: Button
    private lateinit var twoHand: Button
    private lateinit var field: TextView
    private var units: List<UnitNative.Unit> = emptyList()
    private var selected = 0
    private var quick = false

    @Volatile private var handle = 0L
    private var running: UnitNative.Unit? = null
    private var sound: Sound? = null
    private var starting = false
    private val main = Handler(Looper.getMainLooper())
    private val heldKeys = HashSet<Int>()          // the keys the unit took and has down (scan code, else key code)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        title = "Blazie Engineering unit"
        ui = SettingsWidgets(this)
        val prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        quick = prefs.getBoolean("quick", false)
        val page = ui.column()
        page.addView(ui.heading("Blazie Engineering unit"))
        status = ui.body("")
        page.addView(status)

        units = UnitNative.units(SsiData.dir(this).path)
        if (units.isEmpty()) {
            status.text = "No unit yet. Import a Braille Lite's or a Braille 'n Speak's firmware on the Setup tab; " +
                "every unit whose firmware the voices have can run here."
        } else {
            selected = units.indexOfFirst { it.key == prefs.getString("unit", null) }.coerceAtLeast(0)
            choice = ui.choice(page, "Unit", units.map { it.name }, selected) { selected = it; refresh() }
            power = Button(this).apply { setOnClickListener { if (handle != 0L) switchOff() else switchOn() } }
            page.addView(power)
            // one-handed mode, the braille keyboard's (its manual: dot 6 held as the unit is switched on, dot 3 to
            // leave it); the firmware keeps it in the unit's memory.  The Type 'n Speak has none.
            oneHand = Button(this).apply {
                text = "Switch on in one-handed mode (dot 6 held)"
                setOnClickListener { switchOn(UnitNative.ONE_HAND_ON) }
            }
            page.addView(oneHand)
            twoHand = Button(this).apply {
                text = "Switch on in two-handed mode (dot 3 held)"
                setOnClickListener { switchOn(UnitNative.ONE_HAND_OFF) }
            }
            page.addView(twoHand)
            ui.checkBox(page, "Quick key response", quick) { on ->
                quick = on
                prefs.edit().putBoolean("quick", on).apply()
                if (handle != 0L) UnitNative.nativeUnitQuick(handle, on)
            }
            page.addView(Button(this).apply {
                text = "Load programs and files into flash…"
                setOnClickListener { pickFiles() }
            })
            field = TextView(this).apply {
                isFocusable = true
                isFocusableInTouchMode = true
                textSize = 28f
                hint = "The unit's keyboard: F D S J K L and the space bar (the display's braille here)"
                visibility = View.GONE
                setOnFocusChangeListener { _, has -> if (!has) releaseKeys() }
            }
            page.addView(field)
            page.addView(ui.body("Keys: F D S J K L are dots 1 to 6, with the space bar; ; is the advance bar and " +
                "A the back bar. A Type 'n Speak takes every key of the keyboard. The Back key leaves this screen, " +
                "and the unit's memory is saved when it is switched off or the screen is left."))
            refresh()
            // Test hook: `am start -n com.ssi263speech.tts/.UnitActivity --es unit bl-en --ez autostart true` switches
            // that unit on without touching the screen (TalkBack takes a test's taps).  Never set in normal use.
            // Test hook: `--ez bgbreak true` (test_unit_background.py's control) lets a start that finishes unseen
            // switch the unit on, as before #52.
            bgBreak = intent?.getBooleanExtra("bgbreak", false) == true
            // Test hook: `--ei startdelay <ms>` holds the start that long before the unit's own, as a slow first
            // start does (test_unit_background.py: the screen left during it, every time).
            startDelay = intent?.getIntExtra("startdelay", 0) ?: 0
            if (intent?.getBooleanExtra("autostart", false) == true) {
                val want = units.indexOfFirst { it.key == intent.getStringExtra("unit") }
                if (want >= 0) { selected = want; choice.check(choice.getChildAt(want).id) }
                switchOn()
            }
        }
        val root = ScrollView(this).apply { addView(page) }
        root.setOnApplyWindowInsetsListener { view, insets ->
            val b = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
                insets.getInsets(WindowInsets.Type.systemBars()).let { intArrayOf(it.left, it.top, it.right, it.bottom) }
            else intArrayOf(insets.systemWindowInsetLeft, insets.systemWindowInsetTop, insets.systemWindowInsetRight,
                            insets.systemWindowInsetBottom)
            view.setPadding(b[0], b[1], b[2], b[3])
            insets
        }
        setContentView(root)
    }

    override fun onStart() {
        super.onStart()
        shown = true
    }

    override fun onStop() {
        super.onStop()
        shown = false
        if (handle != 0L) switchOff()               // never runs unseen; its memory saved
    }

    override fun onDestroy() {
        if (handle != 0L) switchOff()               // a screen the system destroys leaves no unit running unsaved
        super.onDestroy()
    }

    private fun refresh() {
        val on = running != null
        power.text = if (on) "Switch off" else if (starting) "Starting…" else "Switch on"
        power.isEnabled = !starting
        for (i in 0 until choice.childCount) choice.getChildAt(i).isEnabled = !on && !starting
        field.visibility = if (on) View.VISIBLE else View.GONE
        val hands = if (!on && !starting && !units[selected].typeNSpeak) View.VISIBLE else View.GONE
        oneHand.visibility = hands
        twoHand.visibility = hands
    }

    /** `hold`: keys held as it is switched on (one-handed mode's dot 6 or dot 3), 0 for none. */
    private fun switchOn(hold: Int = 0) {
        val unit = units[selected]
        getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString("unit", unit.key).apply()
        val savedDir = File(filesDir, "units").also { it.mkdirs() }
        val first = !File(savedDir, unit.saved).exists()
        starting = true
        status.text = "Starting the ${unit.name}…"
        refresh()
        val progress = object : Runnable {
            override fun run() {
                if (!starting) return
                val p = UnitNative.nativeUnitProgress()
                if (p > 0) status.text = "Setting up the ${unit.name} for its first start… ${(p * 100).toInt()}%"
                main.postDelayed(this, 500)
            }
        }
        main.postDelayed(progress, 500)
        Thread {
            if (startDelay > 0) Thread.sleep(startDelay.toLong())
            val h = UnitNative.nativeUnitStart(SsiData.dir(this).path, unit.key, savedDir.path, UnitNative.RATE, 3)
            val err = if (h == 0L) UnitNative.nativeUnitError() else ""
            main.post {
                starting = false
                // A start that finished while the screen was not shown -- Home or another app during it, which
                // onStop could not stop: nothing was on yet -- switched the unit on unseen, its sound and its
                // emulation running in the background (#52).  It is switched off at once instead, its memory saved.
                if (h != 0L && !shown && !isFinishing && !isDestroyed && !bgBreak) {
                    val saved = UnitNative.nativeUnitStop(h)
                    status.text = "The ${unit.name} was switched off: this screen was left while it started." +
                        (if (saved) " Its memory is saved." else "")
                    if (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0)
                        Log.d("SsiUnit", "not shown: ${unit.key} switched off as it came on, saved $saved")
                    refresh()
                    return@post
                }
                if (h == 0L || isFinishing || isDestroyed) {
                    if (h != 0L) UnitNative.nativeUnitStop(h)
                    status.text = "The ${unit.name} would not start: $err"
                    refresh()
                    return@post
                }
                UnitNative.nativeUnitQuick(h, quick)
                if (hold != 0 && !unit.typeNSpeak) UnitNative.nativeUnitHoldAtStart(h, hold)  // before its first render
                handle = h
                running = unit
                sound = Sound(h).also { it.start() }
                status.text = "The ${unit.name} is on."
                if (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0) Log.d("SsiUnit", "on ${unit.key}")
                refresh()
                showBraille()
                field.requestFocus()
                if (unit.typeNSpeak && first) {
                    AlertDialog.Builder(this).setTitle("A new Type 'n Speak").setMessage(
                        "Answer y to each setup question, or s for the Spanish unit: seven times in all. It sets up " +
                        "its file system, its flash and its folders; the flash takes about 45 seconds of clicks and " +
                        "the last clear about 35 seconds of silence. Wait for each question.")
                        .setPositiveButton("OK") { _, _ -> field.requestFocus() }.show()
                }
            }
        }.start()
    }

    private fun switchOff() {
        val h = handle
        if (h == 0L) return
        if (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0) Log.d("SsiUnit", "off ${running?.key}")
        val name = running?.name ?: "unit"
        if (UnitNative.nativeUnitFlashBusy(h) && !isFinishing && hasWindowFocus()) {
            status.text = "The $name is writing its flash; switch it off when it has finished."
            return
        }
        handle = 0L
        running = null
        heldKeys.clear()
        sound?.finish()
        sound = null
        val saved = UnitNative.nativeUnitStop(h)
        status.text = if (saved) "The $name is off; its memory is saved." else "The $name is off, but its memory " +
            "could not be saved."
        if (!isFinishing) refresh()
    }

    // ---- "Load programs and files into flash" (Apple's UnitFiles.swift, the Windows and GTK shells'): the unit
    // switched off and saved, the files chosen, added to its flash startup folder in one batch, all or nothing
    // (ssa_unit_add_files), names it already has replaced only when the person says so, the memory as it was kept as
    // <state>.before-import, the unit switched on again if it was on.

    private var reopen = false
    private var shown = false                       // between onStart and onStop
    private var bgBreak = false
    private var startDelay = 0

    private fun pickFiles() {
        val unit = units[selected]
        val state = File(File(filesDir, "units"), unit.saved)
        if (!state.exists()) {
            status.text = "Switch the ${unit.name} on once first: it has no memory of its own yet."
            return
        }
        reopen = handle != 0L
        if (handle != 0L) switchOff()
        if (handle != 0L) return                     // writing its flash: switchOff said so
        val pick = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
            .putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
        try {
            startActivityForResult(pick, PICK_FILES)
        } catch (e: ActivityNotFoundException) {
            status.text = "This phone has no file picker."
            afterLoad()
        }
    }

    @Deprecated("the platform's own picker result; this app has no AndroidX")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        if (requestCode != PICK_FILES) return super.onActivityResult(requestCode, resultCode, data)
        if (resultCode != RESULT_OK || data == null) {
            status.text = "Nothing loaded."
            afterLoad()
            return
        }
        val uris = data.clipData?.let { c -> (0 until c.itemCount).map { c.getItemAt(it).uri } }
            ?: listOfNotNull(data.data)
        val unit = units[selected]
        status.text = "Loading ${uris.size} file(s) into the ${unit.name}…"
        Thread {
            val files = try { uris.map { readPicked(it) } } catch (e: Exception) {
                main.post { status.text = "A file could not be read: ${e.message}"; afterLoad() }
                return@Thread
            }
            load(unit, files, false)
        }.start()
    }

    private class Picked(val name: String, val data: ByteArray, val time: Int, val date: Int)

    /** a picked document: its name, its bytes (2 MB at most: the unit's whole flash), its time as FAT keeps it */
    private fun readPicked(uri: Uri): Picked {
        var name = "file"
        var modified = System.currentTimeMillis()
        contentResolver.query(uri, null, null, null, null)?.use { c ->
            if (c.moveToFirst()) {
                c.getColumnIndex(OpenableColumns.DISPLAY_NAME).takeIf { it >= 0 }?.let { name = c.getString(it) ?: name }
                c.getColumnIndex(DocumentsContract.Document.COLUMN_LAST_MODIFIED).takeIf { it >= 0 }
                    ?.let { if (!c.isNull(it)) modified = c.getLong(it) }
            }
        }
        // read no further than the flash holds (UnitFiles, #50: a large file was read whole before the test)
        val bytes = contentResolver.openInputStream(uri)?.use { UnitFiles.read(it, name) } ?: ByteArray(0)
        val cal = Calendar.getInstance().apply { timeInMillis = modified }
        val time = (cal.get(Calendar.HOUR_OF_DAY) shl 11) or (cal.get(Calendar.MINUTE) shl 5) or (cal.get(Calendar.SECOND) / 2)
        val date = (maxOf(cal.get(Calendar.YEAR) - 1980, 0) shl 9) or ((cal.get(Calendar.MONTH) + 1) shl 5) or
            cal.get(Calendar.DAY_OF_MONTH)
        return Picked(name, bytes, time, date)
    }

    private fun load(unit: UnitNative.Unit, files: List<Picked>, replace: Boolean) {
        val state = File(File(filesDir, "units"), unit.saved).path
        val r = UnitNative.nativeUnitAddFiles(state, unit.codepage, files.map { it.name }.toTypedArray(),
            files.map { it.data }.toTypedArray(), files.map { it.time }.toIntArray(), files.map { it.date }.toIntArray(),
            replace).split('\t', limit = 3)
        main.post {
            when (r[0]) {
                "1" -> {
                    status.text = "Loaded into the ${unit.name}'s flash: ${r[1]} added, ${r[2]} replaced. Its memory " +
                        "before is kept as ${unit.saved}.before-import."
                    afterLoad()
                }
                "-1" -> AlertDialog.Builder(this).setTitle("Replace files?")
                    .setMessage("The ${unit.name} already has:\n${r.getOrElse(1) { "" }.trim()}\n\nReplace them?")
                    .setPositiveButton("Replace") { _, _ -> Thread { load(unit, files, true) }.start() }
                    .setNegativeButton("Don't replace") { _, _ -> status.text = "Nothing loaded."; afterLoad() }
                    .setOnCancelListener { status.text = "Nothing loaded."; afterLoad() }
                    .show()
                else -> {
                    status.text = "Nothing loaded: ${r.getOrElse(1) { "" }}"
                    afterLoad()
                }
            }
        }
    }

    /** the unit on again if it was on before the load */
    private fun afterLoad() {
        if (reopen && handle == 0L && !isFinishing) switchOn()
        reopen = false
    }

    private fun releaseKeys() {
        heldKeys.clear()
        if (handle != 0L) UnitNative.nativeUnitReleaseKeys(handle)
    }

    /** The display, read ten times a second while the unit runs. */
    private fun showBraille() {
        val h = handle
        if (h == 0L) return
        val cells = UnitNative.nativeUnitBraille(h)
        if (field.text.toString() != cells) field.text = cells
        main.postDelayed({ showBraille() }, 100)
    }

    override fun dispatchKeyEvent(e: KeyEvent): Boolean {
        val h = handle
        val down = e.action == KeyEvent.ACTION_DOWN
        if (h == 0L || !field.hasFocus() || e.keyCode == KeyEvent.KEYCODE_BACK ||
            !(down || e.action == KeyEvent.ACTION_UP)) return super.dispatchKeyEvent(e)
        val id = if (e.scanCode > 0) e.scanCode else -e.keyCode
        if (down && e.repeatCount > 0) return heldKeys.contains(id) || super.dispatchKeyEvent(e)
        if (!down && !heldKeys.contains(id)) return super.dispatchKeyEvent(e)
        val taken = UnitNative.nativeUnitKey(h, e.scanCode, e.getUnicodeChar(0), down)
        // a debug build's device test reads the keys here (adb logcat -s SsiUnit); a release build never logs typing
        if (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0)
            Log.d("SsiUnit", "key ${if (down) "down" else "up"} scan ${e.scanCode} code ${e.keyCode} taken $taken")
        if (!taken) return super.dispatchKeyEvent(e)
        if (down) heldKeys.add(id) else heldKeys.remove(id)
        return true
    }

    /** The unit's sound: 20 ms at a time into an AudioTrack, which paces it (beside TalkBack, never in its place). */
    private class Sound(private val h: Long) : Thread("unit-sound") {
        @Volatile private var go = true

        override fun run() {
            Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)
            val block = UnitNative.RATE / 50
            val min = AudioTrack.getMinBufferSize(UnitNative.RATE, AudioFormat.CHANNEL_OUT_MONO,
                                                  AudioFormat.ENCODING_PCM_16BIT)
            val track = AudioTrack.Builder()
                .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                                        .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                .setAudioFormat(AudioFormat.Builder().setSampleRate(UnitNative.RATE)
                                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                                    .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                .setBufferSizeInBytes(maxOf(min, block * 2 * 3))
                .setTransferMode(AudioTrack.MODE_STREAM).build()
            val pcm = ShortArray(block)
            track.play()
            while (go) {
                UnitNative.nativeUnitRender(h, pcm, block)
                track.write(pcm, 0, block)
            }
            track.stop()
            track.release()
        }

        fun finish() {
            go = false
            join(2000)
        }
    }

    companion object {
        private const val PREFS = "unit"
        private const val PICK_FILES = 41
    }
}
