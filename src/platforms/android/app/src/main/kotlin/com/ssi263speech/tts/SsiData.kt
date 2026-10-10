// The units' files.  The Braille Lite's: the firmware each user imports (SsiImport) and the state made from it; the
// Speak-Out's: GW Micro's SPEAKOUT.HEX, and the Mockingboard's: Sweet Micro Systems' mockingboard-tts-1.1.bin,
// imported the same way.  In device-protected storage, where the native side
// opens them by path -- and where they can be read before the phone is first unlocked, so the voice works on the lock
// screen after a restart.  A release APK carries neither; a developer build made with SSI263_ANDROID_BUNDLE_FIRMWARE=1
// carries the Braille Lite's as assets, copied in once per installed version.  The Accents': Aicom's ROMs (and the
// Accent-mini's SPKEMS.DVC, in a build that has that voice), in the APK (assets/aicom), handed over in memory.
package com.ssi263speech.tts

import android.content.Context
import android.util.Log
import java.io.File

object SsiData {
    private const val ASSETS = "firmware"
    private const val STAMP = ".bundled"
    const val LABEL = ".label"              // beside each .BNS: what was imported, in words

    /** The files each imported voice needs, by SsiNative's voice index. */
    val FILES = FirmwareImport.FILES

    fun protectedContext(ctx: Context): Context =
        if (ctx.isDeviceProtectedStorage) ctx else ctx.createDeviceProtectedStorageContext()

    fun dir(ctx: Context): File = File(protectedContext(ctx).filesDir, "unit")

    /** Where an import is judged and made ready, beside the real folder; gone when the import is. */
    fun staging(ctx: Context): File = File(protectedContext(ctx).filesDir, "unit.importing")

    /** The voice can speak: the Accents' ROMs are in the APK; an imported voice's files are all here. */
    fun has(ctx: Context, voice: Int): Boolean {
        if (voice == SsiNative.ACCENT_SA) return accentRoms(ctx) != null
        if (voice == SsiNative.ACCENT_MINI) return accentMini(ctx) != null
        stageBundled(ctx)
        val files = FILES[voice] ?: return false
        return files.all { File(dir(ctx), it).isFile }
    }

    /** Firmware has been imported: a Braille Lite voice, the Speak-Out or the Mockingboard. */
    fun any(ctx: Context): Boolean = FirmwareImport.IMPORTED.any { has(ctx, it) }

    // ---- the Accent SA's ROMs: Aicom's, the one firmware the APK carries (firmware/AICOM.txt; Tomi, 2026-09-30) ----

    private const val AICOM = "aicom"
    private val AICOM_ROMS = listOf("u2.BIN" to 0x10000, "u3.BIN" to 0x8000, "u4.BIN" to 0x8000)
    @Volatile private var roms: List<ByteArray>? = null
    @Volatile private var romsChecked = false

    /** u2, u3 and u4 from the APK's assets, read once; null when one is missing or the wrong size (a broken build). */
    fun accentRoms(ctx: Context): List<ByteArray>? {
        if (romsChecked) return roms
        synchronized(this) {
            if (!romsChecked) {
                roms = try {
                    AICOM_ROMS.map { (name, size) ->
                        ctx.assets.open("$AICOM/$name").use { it.readBytes() }.also {
                            check(it.size == size) { "$name is ${it.size} bytes, not $size" }
                        }
                    }
                } catch (e: Exception) { Log.e("SsiData", "the Accent SA's ROMs are not in this APK", e); null }
                romsChecked = true
            }
            return roms
        }
    }

    // ---- the Accent-mini's driver: Aicom's, in a build that carries the voice (build_android.sh stages it then) ----

    private const val MINI = "SPKEMS.DVC"
    @Volatile private var mini: ByteArray? = null
    @Volatile private var miniChecked = false

    /** SPKEMS.DVC from the APK's assets, read once; null when this build has no Accent-mini (no asset, or a library
     * built without am_voice). */
    fun accentMini(ctx: Context): ByteArray? {
        if (miniChecked) return mini
        synchronized(this) {
            if (!miniChecked) {
                mini = try {
                    if (!SsiNative.nativeVoiceBuilt(SsiNative.ACCENT_MINI)) null
                    else ctx.assets.open("$AICOM/$MINI").use { it.readBytes() }.takeIf { it.isNotEmpty() }
                } catch (e: Exception) { null }       // not in this build: no voice, nothing wrong
                miniChecked = true
            }
            return mini
        }
    }

    /** What was imported for the voice, in words; null when nothing was. */
    fun label(ctx: Context, voice: Int): String? {
        if (!has(ctx, voice)) return null
        val files = FILES[voice] ?: return null
        return try { File(dir(ctx), files[0] + LABEL).readText() }
            catch (e: Exception) { "Braille Lite ${FirmwareImport.languageName(voice)} (built into this app)" }
    }

    /** Move a finished import's files (`ready`: .BNS, .state, .label) into place, each replacing its namesake --
     * holding the engine, so no utterance boots a unit from half of them. */
    fun install(ctx: Context, ready: File) = SsiEngine.withEngine {
        SsiEngine.reload()
        val dir = dir(ctx)
        dir.mkdirs()
        for (f in ready.listFiles()?.sortedBy { it.name } ?: emptyList()) {
            val live = File(dir, f.name)
            if (live.exists()) live.delete()
            if (!f.renameTo(live)) f.copyTo(live, overwrite = true)
        }
    }

    /** Remove every imported unit, the Braille Lite's, the Speak-Out's and the Mockingboard's (a developer build's bundled one stays away
     * until the app is reinstalled). */
    fun remove(ctx: Context) = SsiEngine.withEngine {
        SsiEngine.reload()
        dir(ctx).deleteRecursively()
    }

    private fun stampFile(ctx: Context): File = File(protectedContext(ctx).filesDir, STAMP)

    /** The files a developer build carries. */
    private fun bundled(ctx: Context): Set<String> =
        try { ctx.assets.list(ASSETS).orEmpty().toSet() } catch (e: Exception) { emptySet() }

    /** A developer build's firmware, copied in once per installed version (a reinstall copies it again).  A
     * release build has none, and this does nothing. */
    @Volatile private var bundleChecked = false
    private val bundleLock = Any()

    private fun stageBundled(ctx: Context): Unit = synchronized(bundleLock) {
        if (bundleChecked) return
        bundleChecked = true
        val names = bundled(ctx)
        if (names.isEmpty()) return
        val stamp = stampFile(ctx)
        val version = installedStamp(ctx)
        if (try { stamp.readText() == version } catch (e: Exception) { false }) return
        val dir = dir(ctx)
        dir.mkdirs()
        for (name in names) {
            val tmp = File(dir, "$name.tmp")
            ctx.assets.open("$ASSETS/$name").use { input -> tmp.outputStream().use { input.copyTo(it) } }
            if (!tmp.renameTo(File(dir, name))) {
                File(dir, name).delete()
                tmp.renameTo(File(dir, name))
            }
        }
        stamp.writeText(version)
        Log.i("SsiData", "bundled unit files staged in $dir: ${names.sorted()}")
    }

    private fun installedStamp(ctx: Context): String = try {
        val info = ctx.packageManager.getPackageInfo(ctx.packageName, 0)
        @Suppress("DEPRECATION")
        val code = if (android.os.Build.VERSION.SDK_INT >= 28) info.longVersionCode else info.versionCode.toLong()
        "$code/${info.lastUpdateTime}"
    } catch (e: Exception) { "unknown" }
}
