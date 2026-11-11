// FirmwareImport's layout and wording on the JVM, with no phone and no firmware: the native side's judgement is
// played by a fake that knows a made-up image by the same signature (F3 C3 xx xx FF "COPYRIGHT") plus a letter for
// what it is, and a made-up Intel HEX text (":" records) as the Speak-Out's when it says GOOD, a damaged one when not,
// and made-up bytes starting "SWEETMICRO" and "SWEETEARLY" as the Mockingboards' files ("DOS33DISK" as their disks).
// The real judgement (bl_firmware.c, its list of releases; ssa_import.c, SPEAKOUT.HEX's sha256) has its own tests,
// test_import_native.py and test_android_native.py, on the real files.
//     gradlew testDebugUnitTest                              every case
//     gradlew testDebugUnitTest -Pssi263ImportBreak=1        control: the zip's top alone; the folder cases fail
//     gradlew testDebugUnitTest -Pssi263ImportBreak=unopened control: a zip in a zip in the source dropped in silence
//     gradlew testDebugUnitTest -Pssi263ImportBreak=state    control: a state not known as one; the state cases fail
//     gradlew testDebugUnitTest -Pssi263ImportBreak=speakout control: the Speak-Out's HEX dropped; its cases fail
//     gradlew testDebugUnitTest -Pssi263ImportBreak=mockingboard    the Mockingboard's file dropped; its cases fail
package com.ssi263speech.tts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

class FirmwareImportTest {
    @get:Rule val temp = TemporaryFolder()

    /** A made-up image: the signature, then E (English, the newest release), e (an older English release on the
     * list), S (Spanish), G (German), I (Italian), K (the Slovak Braille 'n Speak 2000), U (a Braille Lite 2000
     * release not on the list) or T (Type 'n Speak). */
    private fun image(kind: Char) = byteArrayOf(0xF3.toByte(), 0xC3.toByte(), 0x32, 0x03, 0xFF.toByte()) +
        "COPYRIGHT".toByteArray() + kind.code.toByte() + ByteArray(1000) { (it * 7).toByte() }

    /** A .BNS: a loader, then the image at 3000h. */
    private fun bns(kind: Char) = ByteArray(0x3000) { 0x18 } + image(kind)

    /** A made-up Intel HEX text: the Speak-Out's when it says GOOD, a damaged one when it says BAD. */
    private fun hex(good: Boolean) = (":020000020000FC\r\n:10000000" + (if (good) "GOOD" else "BAD0") +
        "000000000000000000000000000000\r\n:00000001FF\r\n").toByteArray()

    /** A made-up Mockingboard file: the native side knows the real one by its sha256. */
    private fun mockingboard() = MOCKINGBOARD_FILE.copyOf()

    /** A made-up disk image: the toolkit's when it says TOOLKIT (the fake then gives the file), another when not. */
    private fun disk(toolkit: Boolean) = "DOS33DISK".toByteArray() + (if (toolkit) "TOOLKIT" else "OTHER").toByteArray() +
        ByteArray(3000)

    /** A made-up Mockingboard disk 1: the fake gives the early file from it. */
    private fun disk1() = "DOS33DISK".toByteArray() + "DISK1".toByteArray() + ByteArray(3000)

    /** A made-up state: the size every state has, whatever is in it. */
    private fun state() = ByteArray(FirmwareImport.STATE_SIZE) { (it * 13).toByte() }

    private companion object {
        // two release voices, as the library's table gives them (the app reads them from SsiNative)
        const val HUNGARIAN = 10
        const val TURKISH = 11
        val RELEASES = listOf(
            FirmwareImport.Release(HUNGARIAN, "bl2_hu_1999_07", "BL2HUN.BNS", "Braille Lite 2000 (Hungarian)",
                                   "Braille Lite 2000 (magyar)", "hu"),
            FirmwareImport.Release(TURKISH, "bl2_tr_1998_12", "BL2TUR.BNS", "Braille Lite 2000 (Turkish)",
                                   "Braille Lite 2000 (Türkçe)", "tr"))
        val MOCKINGBOARD_FILE = "SWEETMICRO".toByteArray() + ByteArray(2000) { (it * 5).toByte() }
        val EARLY_FILE = "SWEETEARLY".toByteArray() + ByteArray(1900) { (it * 3).toByte() }
        const val EARLY_LABEL = "Mockingboard, early: Sweet Micro Systems' text-to-speech of Mockingboard disk 1"
        const val JAPANESE_ROW = 20         // release table rows no voice speaks (the fake's)
        const val BNS640_ROW = 30
    }

    private object Fake : FirmwareImport.Identify {
        val labels = listOf("English June 2003", "English September 2000", "Spanish September 2000",
                            "German September 2000", "Italian September 2000", "Slovak Braille 'n Speak 2000",
                            "Braille Lite Hungarian (BL2HUN.BNS)", "Braille Lite Turkish (BL2TUR.BNS)")

        override fun firmware(data: ByteArray, out: File): Pair<Int, String> {
            if (data.isNotEmpty() && data[0] == ':'.code.toByte()) {
                if (!String(data, Charsets.ISO_8859_1).contains("GOOD"))
                    return FirmwareImport.OTHER_HEX to "an Intel HEX file, but damaged: line 2's checksum or length is wrong"
                out.writeBytes(data)
                return FirmwareImport.SPEAKOUT to "GW Micro Speak-Out: SPEAKOUT.HEX"
            }
            if (String(data, 0, minOf(9, data.size), Charsets.ISO_8859_1) == "DOS33DISK") {
                val text = String(data, Charsets.ISO_8859_1)
                if (text.contains("DISK1")) {
                    out.writeBytes(EARLY_FILE)
                    return FirmwareImport.MOCKINGBOARD_EARLY to EARLY_LABEL
                }
                if (!text.contains("TOOLKIT"))
                    return FirmwareImport.OTHER_DISK to "a Mockingboard disk, but not the text-to-speech version 1.1 " +
                        "this voice runs"
                out.writeBytes(MOCKINGBOARD_FILE)
                return FirmwareImport.MOCKINGBOARD to "Mockingboard: Sweet Micro Systems' text-to-speech 1.1"
            }
            if (String(data, 0, minOf(10, data.size), Charsets.ISO_8859_1) == "SWEETEARLY") {
                out.writeBytes(data)
                return FirmwareImport.MOCKINGBOARD_EARLY to EARLY_LABEL
            }
            if (String(data, 0, minOf(10, data.size), Charsets.ISO_8859_1) == "SBLOAD.COM") {   // by sha256 there
                out.writeBytes(data)
                return FirmwareImport.SB_LOAD to "GW Micro Sounding Board 2.7: SBLOAD.COM"
            }
            if (String(data, 0, minOf(10, data.size), Charsets.ISO_8859_1) == "SBDRIVER.C") {
                out.writeBytes(data)
                return FirmwareImport.SB_COM to "GW Micro Sounding Board 2.7: SB.COM"
            }
            if (String(data, 0, minOf(10, data.size), Charsets.ISO_8859_1) == "SMARTALKEX") {   // by sha256 there
                out.writeBytes(data)
                return FirmwareImport.SMARTALK to "SmarTalk: Automated Functions' S_TALK.EXE 1.21"
            }
            if (String(data, 0, minOf(10, data.size), Charsets.ISO_8859_1) == "SWEETMICRO") {
                out.writeBytes(data)
                return FirmwareImport.MOCKINGBOARD to "Mockingboard: Sweet Micro Systems' text-to-speech 1.1"
            }
            val at = (0..data.size - 15).firstOrNull { i ->
                data[i] == 0xF3.toByte() && data[i + 1] == 0xC3.toByte() && data[i + 4] == 0xFF.toByte() &&
                    String(data, i + 5, 9, Charsets.ISO_8859_1) == "COPYRIGHT"
            } ?: return FirmwareImport.NONE to "no Braille Lite firmware in it"
            val (language, label) = when (data[at + 14].toInt().toChar()) {
                'E' -> FirmwareImport.ENGLISH to labels[0]
                'e' -> FirmwareImport.ENGLISH to labels[1]
                'S' -> FirmwareImport.SPANISH to labels[2]
                'G' -> FirmwareImport.GERMAN to labels[3]
                'I' -> FirmwareImport.ITALIAN to labels[4]
                'K' -> FirmwareImport.SLOVAK to labels[5]
                'H' -> HUNGARIAN to labels[6]
                'R' -> TURKISH to labels[7]
                'J' -> FirmwareImport.UNIT_RELEASE + JAPANESE_ROW to "Braille Lite Japanese (BL2JPN.BNS)"
                '6' -> FirmwareImport.UNIT_RELEASE + BNS640_ROW to "Braille 'n Speak 640 English June 2003"
                'U' -> return FirmwareImport.UNKNOWN to "not a release this app knows"
                else -> return FirmwareImport.REFUSED to "not a release this voice can run"
            }
            out.writeBytes(data.copyOfRange(at, data.size))
            return language to label
        }

        override fun known() = labels

        override fun unitRelease(row: Int) = when (row) {
            JAPANESE_ROW -> FirmwareImport.UnitRelease("bl2_ja_2000_01", "Braille Lite 2000 (Japanese)")
            BNS640_ROW -> FirmwareImport.UnitRelease("bns640_en_2003_06", "Braille 'n Speak 640 (English)")
            else -> null
        }
    }

    private fun zip(vararg entries: Pair<String, ByteArray>): ByteArray {
        val out = ByteArrayOutputStream()
        ZipOutputStream(out).use { z ->
            for ((name, bytes) in entries) { z.putNextEntry(ZipEntry(name)); z.write(bytes); z.closeEntry() }
        }
        return out.toByteArray()
    }

    private fun inspect(name: String, data: ByteArray): FirmwareImport.Plan {
        FirmwareImport.releases = RELEASES
        return FirmwareImport.inspect(name, data, temp.newFolder(), Fake)
    }

    private fun languages(plan: FirmwareImport.Plan) = plan.found.map { it.language }

    // ---- layouts ------------------------------------------------------------------------------------------------

    @Test fun zipWithTheFirmwareAtItsTop() {
        val plan = inspect("update.zip", zip("BL2ENG.BNS" to bns('E'), "HOWTOUPD.TXT" to "text".toByteArray()))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.ENGLISH), languages(plan))
        assertEquals("BL2ENG.BNS", plan.found[0].from)
        assertEquals("English June 2003", plan.found[0].label)
        assertTrue(plan.found[0].firmware.readBytes().contentEquals(image('E')))
    }

    @Test fun zipWithTheFirmwareOneFolderDown() {
        val plan = inspect("june2003.zip", zip("blt2000/BL2ENG.BNS" to bns('E'), "spanish/BL2SPA.BNS" to bns('S')))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.ENGLISH, FirmwareImport.SPANISH), languages(plan))
        assertEquals("spanish/BL2SPA.BNS", plan.found[1].from)
    }

    @Test fun theNvdaAddOnGivesItsFirmwareButNotItsStates() {
        val d = "synthDrivers/_ssi263_blazie/"
        val plan = inspect("blazie.nvda-addon", zip("manifest.ini" to "name = blazie".toByteArray(),
            "synthDrivers/blazie.py" to "#".toByteArray(), d + "BL2ENG.BNS" to bns('E'),
            d + "bl2_2003_warm.state" to state(), d + "BL2SPA.BNS" to bns('S'),
            d + "bl2spa_fresh.state" to state(), d + "bl.dll" to ByteArray(5000)))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.ENGLISH, FirmwareImport.SPANISH), languages(plan))
        assertTrue(plan.notes.toString(), plan.notes.single().startsWith(
            "${d}bl2_2003_warm.state, ${d}bl2spa_fresh.state are state files, not firmware, and are not used"))
    }

    @Test fun aZipWithNoFirmwareSaysSo() {
        val plan = inspect("photos.zip", zip("a.txt" to "hello".toByteArray(), "b/c.bin" to ByteArray(300)))
        assertTrue(plan.found.isEmpty())
        assertTrue(plan.refusal!!, plan.refusal!!.startsWith("This zip does not contain Braille Lite, Braille 'n " +
            "Speak, Speak-Out or Mockingboard firmware."))
    }

    @Test fun firmwareInAnyFolderIsFound() {
        // judged by its content, never by its folder (Tomi, 2026-10-18: anyone's own archive imports)
        val plan = inspect("deep.zip", zip("backup/2001/blt2000/disk1/BL2ENG.BNS" to bns('E')))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.ENGLISH), languages(plan))
        assertEquals("backup/2001/blt2000/disk1/BL2ENG.BNS", plan.found[0].from)
    }

    @Test fun namesDoNotMatterContentDoes() {
        val plan = inspect("x.zip", zip("BL2ENG.BNS" to "not firmware".toByteArray(), "renamed.bin" to bns('S')))
        assertEquals(listOf(FirmwareImport.SPANISH), languages(plan))
        assertEquals("renamed.bin", plan.found[0].from)
    }

    @Test fun anUpdateProgramWithTheImageInside() {
        val exe = "MZ".toByteArray() + ByteArray(7000) { 1 } + bns('E') + ByteArray(3000) { 2 }
        val plan = inspect("BLT2000.EXE", exe)
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.ENGLISH), languages(plan))
        assertEquals("BLT2000.EXE", plan.found[0].from)
    }

    @Test fun anUpdateProgramThatIsAZipBehindItsCode() {
        val exe = "MZ".toByteArray() + ByteArray(9000) { 1 } + zip("BL2ENG.BNS" to bns('E'), "SPELL.DIC" to ByteArray(99))
        val plan = inspect("blt2000.exe", exe)
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.ENGLISH), languages(plan))
    }

    @Test fun anUpdateProgramInsideAZip() {
        val exe = "MZ".toByteArray() + ByteArray(9000) { 1 } + zip("BL2ENG.BNS" to bns('E'))
        val plan = inspect("fs.zip", zip("FS june2003/blt2000.exe" to exe))
        assertEquals(listOf(FirmwareImport.ENGLISH), languages(plan))
        assertEquals("FS june2003/blt2000.exe, BL2ENG.BNS", plan.found[0].from)
    }

    @Test fun aSingleBnsFile() {
        val plan = inspect("BL2SPA.BNS", bns('S'))
        assertEquals(listOf(FirmwareImport.SPANISH), languages(plan))
        assertEquals("Spanish September 2000", plan.found[0].label)
    }

    @Test fun randomBytesAreNotFirmware() {
        val plan = inspect("noise.bin", ByteArray(300000) { (it * 31 + 7).toByte() })
        assertTrue(plan.found.isEmpty())
        assertTrue(plan.refusal!!.startsWith(FirmwareImport.NO_FIRMWARE_FILE))
    }

    // ---- only releases on the list (Tomi) -----------------------------------------------------------------------

    @Test fun anotherUnitsFirmwareIsRefused() {
        val plan = inspect("TNSENG-1.TNS", bns('T'))
        assertTrue(plan.found.isEmpty())
        assertTrue(plan.refusal!!.startsWith(FirmwareImport.NO_FIRMWARE_FILE))
        assertTrue(plan.refusal!!, plan.refusal!!.contains(
            "TNSENG-1.TNS is Blazie firmware, but not a Braille Lite 2000 release this voice can run."))
    }

    @Test fun anUnknownReleaseIsRefusedAndTheKnownOnesNamed() {
        val plan = inspect("BL2GER.BNS", bns('U'))
        assertTrue(plan.found.isEmpty())
        assertTrue(plan.refusal!!, plan.refusal!!.startsWith(
            "BL2GER.BNS is Braille Lite 2000 firmware, but not a release this app knows, so it cannot be imported"))
        assertTrue(plan.refusal!!, plan.refusal!!.contains(
            "The releases this app knows: English June 2003; English September 2000; Spanish September 2000; " +
            "German September 2000; Italian September 2000; Slovak Braille 'n Speak 2000; " +
            "Braille Lite Hungarian (BL2HUN.BNS); Braille Lite Turkish (BL2TUR.BNS)."))
    }

    @Test fun anUnknownReleaseInAZipIsRefused() {
        val plan = inspect("german.zip", zip("blt2000/BL2GER.BNS" to bns('U'), "LIESMICH.TXT" to ByteArray(10)))
        assertTrue(plan.found.isEmpty())
        assertTrue(plan.refusal!!, plan.refusal!!.startsWith("blt2000/BL2GER.BNS is Braille Lite 2000 firmware, " +
            "but not a release this app knows"))
    }

    @Test fun twoUnknownReleasesAreRefused() {
        val plan = inspect("others.zip", zip("a/BL2GER.BNS" to bns('U'), "b/BL2FRA.BNS" to bns('U')))
        assertTrue(plan.found.isEmpty())
        assertTrue(plan.refusal!!, plan.refusal!!.startsWith("a/BL2GER.BNS, b/BL2FRA.BNS are Braille Lite 2000 " +
            "firmware, but not a release this app knows"))
    }

    @Test fun aKnownReleaseIsTakenAndAnUnknownOneLeftOut() {
        val plan = inspect("mixed.zip", zip("de/BL2GER.BNS" to bns('U'), "en/BL2ENG.BNS" to bns('E')))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.ENGLISH), languages(plan))
        assertEquals("en/BL2ENG.BNS", plan.found[0].from)
        assertEquals("de/BL2GER.BNS is Braille Lite 2000 firmware of a release this app does not know; it is left " +
            "out.", plan.notes.single())
    }

    @Test fun twoEnglishReleasesGiveTheNewest() {
        val plan = inspect("both.zip", zip("once/BL2ENG.BNS" to bns('e'), "fs/BL2ENG.BNS" to bns('E')))
        assertEquals(listOf(FirmwareImport.ENGLISH), languages(plan))
        assertEquals("fs/BL2ENG.BNS", plan.found[0].from)
        assertTrue(plan.notes.single(), plan.notes.single().startsWith(
            "once/BL2ENG.BNS is another English release (English September 2000); only fs/BL2ENG.BNS"))
    }

    @Test fun theSameReleaseTwiceIsOne() {
        val exe = "MZ".toByteArray() + ByteArray(9000) { 1 } + zip("BL2ENG.BNS" to bns('E'))
        val plan = inspect("FS june2003.zip", zip("blt2000.exe" to exe, "blt2000/BL2ENG.BNS" to bns('E')))
        assertEquals(listOf(FirmwareImport.ENGLISH), languages(plan))
        assertTrue(plan.notes.toString(), plan.notes.isEmpty())
    }

    // ---- never a state (Tomi) -----------------------------------------------------------------------------------

    @Test fun aStateAloneIsRefusedInTomisWords() {
        val plan = inspect("bl2_2003_warm.state", state())
        assertTrue(plan.found.isEmpty())
        assertEquals(FirmwareImport.STATE_FILE, plan.refusal)
    }

    @Test fun aStateUnderAnotherNameIsStillAState() {
        val plan = inspect("backup.bin", state())
        assertEquals(FirmwareImport.STATE_FILE, plan.refusal)
    }

    @Test fun aZipHoldingOnlyAStateIsRefused() {
        val plan = inspect("state.zip", zip("bl2spa_fresh.state" to state()))
        assertTrue(plan.found.isEmpty())
        assertEquals("This zip holds a state file (bl2spa_fresh.state), not firmware. Please import only firmware " +
            "files, or zips containing them, with this tool.", plan.refusal)
    }

    @Test fun aZipWithFirmwareAndAStateTakesTheFirmwareOnly() {
        val plan = inspect("unit.zip", zip("BL2ENG.BNS" to bns('E'), "unit.dat" to state()))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.ENGLISH), languages(plan))
        assertEquals("unit.dat is a state file, not firmware, and is not used: this phone prepares the unit's " +
            "state itself from the firmware.", plan.notes.single())
        assertFalse(plan.found.any { it.firmware.length() == FirmwareImport.STATE_SIZE.toLong() })
    }

    // A state is larger once the unit's files reach past the RAM's first 256 KB or the flash's first 512 KB
    // (bl_board.h bl_save_state), and the emulator's ends with its clock's 64 bytes: each is still a state.  The
    // Spanish Braille Lite's own factory state is one of them (its setup fills all the RAM).

    private fun tail(magic: ByteArray) = magic + ByteArray(64 - magic.size) { 7 }
    private val clockTail get() = tail("BLCLOCK1".toByteArray(Charsets.US_ASCII))
    private val chipTail get() = tail("BLR6242".toByteArray(Charsets.US_ASCII) + 1)          // the 640's clock chip

    @Test fun everySizeAStateCanHaveIsAState() {
        assertEquals(setOf(786432, 1310720, 2359296, 2883584), FirmwareImport.STATE_SIZES)
        for (size in FirmwareImport.STATE_SIZES) {
            val s = ByteArray(size) { (it * 13).toByte() }
            assertEquals("$size bytes", FirmwareImport.STATE_FILE, inspect("backup.bin", s).refusal)
            assertEquals("$size bytes and the clock", FirmwareImport.STATE_FILE, inspect("backup.bin", s + clockTail).refusal)
            assertEquals("$size bytes and the 640's clock chip", FirmwareImport.STATE_FILE,
                         inspect("backup.bin", s + chipTail).refusal)
        }
    }

    @Test fun aLargerStateBesideFirmwareIsNotUsed() {
        val spanish = ByteArray(1310720) { (it * 13).toByte() }
        val plan = inspect("unit.zip", zip("BL2ENG.BNS" to bns('E'), "bl2spa_fresh.state" to spanish,
                                           "saved.dat" to spanish + clockTail))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.ENGLISH), languages(plan))
        assertEquals(1, plan.notes.size)
        assertTrue(plan.notes.single(), plan.notes.single().contains("bl2spa_fresh.state") &&
                   plan.notes.single().contains("saved.dat") && plan.notes.single().contains("state file"))
    }

    @Test fun aStatesSizeWithOtherBytesAfterItIsNotAState() {
        val s = ByteArray(FirmwareImport.STATE_SIZE) { (it * 13).toByte() }
        assertFalse(FirmwareImport.isState(s + ByteArray(64) { 7 }))        // 64 more bytes that are no clock's
        assertFalse(FirmwareImport.isState(s + ByteArray(1)))
        assertFalse(FirmwareImport.isState(ByteArray(262144 + 2097152 + 1)))
    }

    // ---- the Speak-Out: GW Micro's SPEAKOUT.HEX, by content (Tomi, 0.7.5) -----------------------------------------

    @Test fun theSpeakOutHexAlone() {
        val plan = inspect("SPEAKOUT.HEX", hex(true))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.SPEAKOUT), languages(plan))
        assertEquals("GW Micro Speak-Out: SPEAKOUT.HEX", plan.found[0].label)
        assertTrue(plan.found[0].firmware.readBytes().contentEquals(hex(true)))
    }

    @Test fun gwMicrosSpeakoutZip() {
        val plan = inspect("speakout.zip", zip("README.TXT" to "Speak-Out update".toByteArray(),
            "SPEAKOUT.HEX" to hex(true), "UPDATE.EXE" to "MZ".toByteArray() + ByteArray(500)))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.SPEAKOUT), languages(plan))
        assertEquals("SPEAKOUT.HEX", plan.found[0].from)
    }

    @Test fun theSpeakOutHexOneFolderDown() {
        val plan = inspect("speakout.zip", zip("speakout/SPEAKOUT.HEX" to hex(true)))
        assertEquals(listOf(FirmwareImport.SPEAKOUT), languages(plan))
        assertEquals("speakout/SPEAKOUT.HEX", plan.found[0].from)
    }

    @Test fun aRenamedHexIsStillTheSpeakOuts() {
        val plan = inspect("firmware.txt", hex(true))
        assertEquals(listOf(FirmwareImport.SPEAKOUT), languages(plan))
    }

    @Test fun bothUnitsInOneZip() {
        val plan = inspect("units.zip", zip("blt2000/BL2ENG.BNS" to bns('E'), "speakout/SPEAKOUT.HEX" to hex(true)))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.ENGLISH, FirmwareImport.SPEAKOUT), languages(plan))
    }

    @Test fun aDamagedHexIsRefusedAndSaysWhy() {
        val plan = inspect("SPEAKOUT.HEX", hex(false))
        assertTrue(plan.found.isEmpty())
        assertTrue(plan.refusal!!, plan.refusal!!.startsWith("SPEAKOUT.HEX is an Intel HEX file, but damaged: " +
            "line 2's checksum or length is wrong. " + FirmwareImport.ONLY_THE_HEX))
        assertTrue(plan.refusal!!, plan.refusal!!.endsWith(FirmwareImport.WHAT_TO_CHOOSE))
    }

    @Test fun aDamagedHexBesideFirmwareIsLeftOut() {
        val plan = inspect("mixed.zip", zip("BL2ENG.BNS" to bns('E'), "old/SPEAKOUT.HEX" to hex(false)))
        assertEquals(listOf(FirmwareImport.ENGLISH), languages(plan))
        assertEquals("old/SPEAKOUT.HEX is an Intel HEX file, but damaged: line 2's checksum or length is wrong; it is " +
            "left out.", plan.notes.single())
    }

    @Test fun aHexTwoFoldersDownIsFound() {
        val plan = inspect("deep.zip", zip("backup/speakout/SPEAKOUT.HEX" to hex(true)))
        assertEquals(listOf(FirmwareImport.SPEAKOUT), languages(plan))
    }

    @Test fun aStateBesideTheHexIsNotUsed() {
        val plan = inspect("unit.zip", zip("SPEAKOUT.HEX" to hex(true), "unit.dat" to state()))
        assertEquals(listOf(FirmwareImport.SPEAKOUT), languages(plan))
        assertTrue(plan.notes.single(), plan.notes.single().startsWith("unit.dat is a state file, not firmware"))
    }

    // ---- the Mockingboard: Sweet Micro Systems' file, by its sha256 (Tomi, 2026-10-09: imported as Blazie's is) ------

    @Test fun theMockingboardFileAlone() {
        val plan = inspect("mockingboard-tts-1.1.bin", mockingboard())
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.MOCKINGBOARD), languages(plan))
        assertEquals("Mockingboard: Sweet Micro Systems' text-to-speech 1.1", plan.found[0].label)
        assertTrue(plan.found[0].firmware.readBytes().contentEquals(mockingboard()))
    }

    @Test fun theMockingboardFileOneFolderDownBesideTheOthers() {
        val plan = inspect("units.zip", zip("blt2000/BL2ENG.BNS" to bns('E'), "speakout/SPEAKOUT.HEX" to hex(true),
            "mockingboard/mockingboard-tts-1.1.bin" to mockingboard()))
        assertEquals(listOf(FirmwareImport.ENGLISH, FirmwareImport.SPEAKOUT, FirmwareImport.MOCKINGBOARD),
            languages(plan))
        assertEquals("mockingboard/mockingboard-tts-1.1.bin", plan.found[2].from)
    }

    @Test fun theMockingboardFileTwoFoldersDownIsFound() {
        val plan = inspect("deep.zip", zip("backup/mockingboard/mockingboard-tts-1.1.bin" to mockingboard()))
        assertEquals(listOf(FirmwareImport.MOCKINGBOARD), languages(plan))
    }

    @Test fun theToolkitDiskGivesTheFile() {
        val plan = inspect("Mockingboard Developers toolkit.dsk", disk(true))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.MOCKINGBOARD), languages(plan))
        assertTrue(plan.found[0].firmware.readBytes().contentEquals(mockingboard()))
    }

    @Test fun theToolkitDiskInAZip() {
        val plan = inspect("disks.zip", zip("apple/toolkit.dsk" to disk(true), "README.TXT" to "disks".toByteArray()))
        assertEquals(listOf(FirmwareImport.MOCKINGBOARD), languages(plan))
        assertEquals("apple/toolkit.dsk", plan.found[0].from)
    }

    @Test fun anotherDiskIsRefusedAndSaysWhy() {
        val plan = inspect("mockingboard1.dsk", disk(false))
        assertTrue(plan.found.isEmpty())
        assertTrue(plan.refusal!!, plan.refusal!!.startsWith("mockingboard1.dsk is a Mockingboard disk, but not the " +
            "text-to-speech version 1.1 this voice runs."))
        assertTrue(plan.refusal!!, plan.refusal!!.endsWith(FirmwareImport.WHAT_TO_CHOOSE))
    }

    @Test fun anotherDiskBesideFirmwareIsLeftOut() {
        val plan = inspect("mixed.zip", zip("BL2ENG.BNS" to bns('E'), "mockingboard1.dsk" to disk(false)))
        assertEquals(listOf(FirmwareImport.ENGLISH), languages(plan))
        assertEquals("mockingboard1.dsk is a Mockingboard disk, but not the text-to-speech version 1.1 this voice " +
            "runs; it is left out.", plan.notes.single())
    }

    // ---- the early Mockingboard: disk 1's text-to-speech, its file or the disk (Tomi, 2026-10-10) ----------------------

    @Test fun theEarlyFileAlone() {
        val plan = inspect("mockingboard-tts-early.bin", EARLY_FILE)
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.MOCKINGBOARD_EARLY), languages(plan))
        assertEquals(EARLY_LABEL, plan.found[0].label)
    }

    @Test fun diskOneInAFolderOfAZipGivesTheEarlyFile() {
        val plan = inspect("apple.zip", zip("disks/mockingboard1.dsk" to disk1()))
        assertEquals(listOf(FirmwareImport.MOCKINGBOARD_EARLY), languages(plan))
        assertEquals("disks/mockingboard1.dsk", plan.found[0].from)
        assertTrue(plan.found[0].firmware.readBytes().contentEquals(EARLY_FILE))
    }

    @Test fun bothMockingboardsFromOneZip() {
        val plan = inspect("mockingboard.zip", zip("toolkit.dsk" to disk(true), "disk1/mockingboard1.dsk" to disk1()))
        assertEquals(listOf(FirmwareImport.MOCKINGBOARD, FirmwareImport.MOCKINGBOARD_EARLY), languages(plan))
        assertEquals(FirmwareImport.FILES[FirmwareImport.MOCKINGBOARD_EARLY], listOf("mockingboard-tts-early.bin"))
    }

    @Test fun theMockingboardHasNoStateToMake() {
        assertTrue(FirmwareImport.MOCKINGBOARD_EARLY in FirmwareImport.NO_STATE)
        assertEquals(FirmwareImport.BUILT_IN, setOf(FirmwareImport.MOCKINGBOARD, FirmwareImport.MOCKINGBOARD_EARLY,
                                                    FirmwareImport.SMARTALK))
        assertTrue(FirmwareImport.MOCKINGBOARD in FirmwareImport.NO_STATE)
        assertEquals(listOf("mockingboard-tts-1.1.bin"), FirmwareImport.FILES[FirmwareImport.MOCKINGBOARD])
        assertTrue(FirmwareImport.MOCKINGBOARD in FirmwareImport.IMPORTED)
    }

    // ---- 0.8's Blazie units: German, Italian (both Lite2000.BNS on their disks), the Slovak Braille 'n Speak --------

    @Test fun theGermanAndItalianDisksOneFolderDownBesideTheSlovakUnit() {
        val plan = inspect("blazie.zip", zip("german/Lite2000.BNS" to bns('G'), "italian/Lite2000.BNS" to bns('I'),
            "bns2000/BS2SLL.BNS" to bns('K')))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.GERMAN, FirmwareImport.ITALIAN, FirmwareImport.SLOVAK), languages(plan))
        assertEquals(listOf("german/Lite2000.BNS", "italian/Lite2000.BNS", "bns2000/BS2SLL.BNS"),
            plan.found.map { it.from })
        assertEquals("Slovak Braille 'n Speak 2000", plan.found[2].label)
    }

    @Test fun theUnifiedAddOnGivesEveryBlazieUnitButNoState() {
        val d = "synthDrivers/_ssi263_unified/_ssi263_blazie/"
        val plan = inspect("ssi263-speech-0.8.0.nvda-addon", zip("manifest.ini" to "name = ssi263".toByteArray(),
            "synthDrivers/_ssi263_unified/__init__.py" to "#".toByteArray(), d + "BL2ENG.BNS" to bns('E'),
            d + "bl2_2003_warm.state" to state(), d + "BL2SPA.BNS" to bns('S'), d + "Lite2000_de.BNS" to bns('G'),
            d + "Lite2000_it.BNS" to bns('I'), d + "BS2SLL.BNS" to bns('K'), d + "bs2sll_fresh.state" to state()))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.ENGLISH, FirmwareImport.SPANISH, FirmwareImport.GERMAN,
            FirmwareImport.ITALIAN, FirmwareImport.SLOVAK), languages(plan))
        assertTrue(plan.notes.toString(), plan.notes.single().startsWith(
            "${d}bl2_2003_warm.state, ${d}bs2sll_fresh.state are state files, not firmware, and are not used"))
    }

    @Test fun theNvdaAddOnsFlatNamesGiveEveryBlazieUnit() {
        val d = "synthDrivers/_ssi263_blazie/"
        val plan = inspect("blazie.nvda-addon", zip(d + "BL2ENG.BNS" to bns('E'), d + "BL2SPA.BNS" to bns('S'),
            d + "Lite2000_de.BNS" to bns('G'), d + "lite2000_de_fresh.state" to state(),
            d + "Lite2000_it.BNS" to bns('I'), d + "BS2SLL.BNS" to bns('K')))
        assertEquals(listOf(FirmwareImport.ENGLISH, FirmwareImport.SPANISH, FirmwareImport.GERMAN,
            FirmwareImport.ITALIAN, FirmwareImport.SLOVAK), languages(plan))
    }

    @Test fun eachBlazieUnitHasFilesOfItsOwnAndAState() {
        val names = FirmwareImport.FILES.values.flatten()
        assertEquals("every file name is one voice's", names.size, names.toSet().size)
        assertEquals(listOf("Lite2000_de.BNS", "lite2000_de_fresh.state"), FirmwareImport.FILES[FirmwareImport.GERMAN])
        assertEquals(listOf("Lite2000_it.BNS", "lite2000_it_fresh.state"), FirmwareImport.FILES[FirmwareImport.ITALIAN])
        assertEquals(listOf("BS2SLL.BNS", "bs2sll_fresh.state"), FirmwareImport.FILES[FirmwareImport.SLOVAK])
        for (v in FirmwareImport.BLAZIE) {
            assertTrue(v in FirmwareImport.IMPORTED)
            assertFalse(v in FirmwareImport.NO_STATE)
        }
        assertEquals(FirmwareImport.IMPORTED.toSet(), FirmwareImport.FILES.keys)
        assertEquals("German Braille Lite", FirmwareImport.unitName(FirmwareImport.GERMAN))
        assertEquals("Italian Braille Lite", FirmwareImport.unitName(FirmwareImport.ITALIAN))
        assertEquals("Slovak Braille 'n Speak 2000", FirmwareImport.unitName(FirmwareImport.SLOVAK))
        assertEquals("Speak-Out", FirmwareImport.unitName(FirmwareImport.SPEAKOUT))
    }

    // ---- the release voices (Hungarian, Turkish, ...: the library's release table) ---------------------------------

    @Test fun aLanguageUpdateIsImportedAsItsVoice() {
        val plan = inspect("BL2HUN.BNS", bns('H'))
        assertNull(plan.refusal)
        assertEquals(listOf(HUNGARIAN), languages(plan))
        assertEquals("Braille Lite Hungarian (BL2HUN.BNS)", plan.found[0].label)
    }

    @Test fun releaseVoicesComeAfterTheShippedOnesFromOneZip() {
        val plan = inspect("langs.zip", zip("BL2TUR.BNS" to bns('R'), "BL2ENG.BNS" to bns('E'),
                                            "BL2HUN.BNS" to bns('H')))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.ENGLISH, HUNGARIAN, TURKISH), languages(plan))
    }

    // ---- GW Micro's Sounding Board (0.8.5): two drivers, imported only together, the rest of sb.zip left out ----------

    private val sbLoad = "SBLOAD.COM".toByteArray() + ByteArray(3000) { (it * 7).toByte() }
    private val sbCom = "SBDRIVER.C".toByteArray() + ByteArray(2000) { (it * 11).toByte() }

    @Test fun sbZipBringsTheSoundingBoardAsOneVoiceOfTwoFiles() {
        // GW Micro's sb.zip as it came: the two drivers at its top beside the manual and its viewer
        val plan = inspect("sb.zip", zip("README" to "The Sounding Board".toByteArray(), "SBLOAD.COM" to sbLoad,
                                         "SECT1.TXT" to ByteArray(500), "SB.COM" to sbCom,
                                         "VIEW.EXE" to "MZ".toByteArray() + ByteArray(800)))
        assertNull(plan.refusal)
        val f = plan.found.single()
        assertEquals(FirmwareImport.SOUNDINGBOARD, f.language)
        assertEquals("SBLOAD.COM and SB.COM", f.from)
        assertEquals(sbLoad.toList(), f.firmware.readBytes().toList())
        assertEquals(sbCom.toList(), f.parts.single().readBytes().toList())
        assertEquals(listOf("SBLOAD.COM", "SB.COM"), FirmwareImport.files(f.language))
        assertTrue(plan.notes.isEmpty())
    }

    @Test fun oneSoundingBoardDriverAloneIsRefusedWithWhy() {
        val plan = inspect("SB.COM", sbCom)
        assertTrue(plan.found.isEmpty())
        assertTrue(plan.refusal!!, plan.refusal!!.contains("the Sounding Board's SB.COM, but its SBLOAD.COM is needed " +
                                                           "too (both are in GW Micro's sb.zip)"))
    }

    // ---- Automated Functions' SmarTalk: S_TALK.EXE, one file, from its disk or the NVDA add-on that carries it --------

    private val sTalk = "SMARTALKEX".toByteArray() + ByteArray(2500) { (it * 13).toByte() }

    @Test fun theSmarTalksExeIsItsVoice() {
        val plan = inspect("S_TALK.EXE", sTalk)
        assertNull(plan.refusal)
        val f = plan.found.single()
        assertEquals(FirmwareImport.SMARTALK, f.language)
        assertEquals(sTalk.toList(), f.firmware.readBytes().toList())
        assertEquals(listOf("S_TALK.EXE"), FirmwareImport.files(f.language))
        assertTrue(f.language in FirmwareImport.NO_STATE)
    }

    @Test fun theAddOnBringsTheSmarTalkItCarries() {
        // the add-on carries S_TALK.EXE beside the SmarTalk's driver (nvda/build_ssi263.py stage_smartalk), with its
        // notice: one import brings it with the Speak-Out
        val u = "synthDrivers/_ssi263_unified/"
        val plan = inspect("ssi263-speech.nvda-addon", zip("manifest.ini" to "name = ssi263".toByteArray(),
            u + "_ssi263_speakout/SPEAKOUT.HEX" to hex(true), u + "_ssi263_smartalk/S_TALK.EXE" to sTalk,
            u + "_ssi263_smartalk/licenses/SmarTalk-software-notice.txt" to "Copyright 1995".toByteArray()))
        assertNull(plan.refusal)
        assertEquals(setOf(FirmwareImport.SPEAKOUT, FirmwareImport.SMARTALK), languages(plan).toSet())
        assertEquals(sTalk.toList(), plan.found.single { it.language == FirmwareImport.SMARTALK }.firmware.readBytes()
            .toList())
    }

    // ---- the unit screen's releases (0.8.5: no voice speaks them -- the 640, Japanese -- kept under units/) ----------

    @Test fun aReleaseNoVoiceSpeaksIsKeptForTheUnitScreen() {
        val plan = inspect("BL2JPN.BNS", bns('J'))
        assertNull(plan.refusal)
        val f = plan.found.single()
        assertEquals(FirmwareImport.UNIT_RELEASE + JAPANESE_ROW, f.language)
        assertEquals("units/bl2_ja_2000_01.BNS", f.unit?.file)
        assertEquals("Braille Lite Japanese (BL2JPN.BNS)", f.label)
        assertTrue(f.firmware.isFile)
        assertNull(FirmwareImport.files(f.language))        // never a voice's files
    }

    @Test fun unitReleasesComeAfterTheVoicesFromOneZip() {
        val plan = inspect("units.zip", zip("BSPENG.BNS" to bns('6'), "BL2ENG.BNS" to bns('E'),
                                            "BL2JPN.BNS" to bns('J'), "again/BSPENG.BNS" to bns('6')))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.ENGLISH, FirmwareImport.UNIT_RELEASE + BNS640_ROW,
                            FirmwareImport.UNIT_RELEASE + JAPANESE_ROW), languages(plan))
        assertEquals("BSPENG.BNS", plan.found[1].from)                  // the same release twice is one
        assertEquals("units/bns640_en_2003_06.BNS", plan.found[1].unit?.file)
        assertNull(plan.found[0].unit)
    }

    @Test fun theUnifiedAddOnBringsEveryVoiceAtOnce() {
        // the 0.8 add-on: every engine's folder, the Blazie engine's language updates in bl2000/, and states that are
        // never taken (Tomi: someone who wants all the voices can have them in one import)
        val u = "synthDrivers/_ssi263_unified/"
        val d = u + "_ssi263_blazie/"
        val states = (1..20).map { d + "bl2000/state$it.state" to state() }.toTypedArray()
        val plan = inspect("ssi263-speech-0.8.0.nvda-addon", zip("manifest.ini" to "name = ssi263".toByteArray(),
            d + "BL2ENG.BNS" to bns('E'), d + "bl2_2003_warm.state" to state(), d + "Lite2000_de.BNS" to bns('G'),
            d + "bl2000/BL2HUN.BNS" to bns('H'), d + "bl2000/BL2TUR.BNS" to bns('R'),
            u + "_ssi263_speakout/SPEAKOUT.HEX" to hex(true),
            u + "_ssi263_mockingboard/mockingboard-tts-1.1.bin" to mockingboard(),
            u + "_ssi263_mockingboard/mockingboard-tts-early.bin" to EARLY_FILE,
            u + "_ssi263_accent/accent-sa/u2.BIN" to ByteArray(4000), *states))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.ENGLISH, FirmwareImport.GERMAN, FirmwareImport.SPEAKOUT,
            FirmwareImport.MOCKINGBOARD, FirmwareImport.MOCKINGBOARD_EARLY, HUNGARIAN, TURKISH), languages(plan))
        assertEquals("${d}bl2000/BL2HUN.BNS", plan.found.first { it.language == HUNGARIAN }.from)
        assertTrue(plan.notes.toString(), plan.notes.single().startsWith("21 state files ("))
        // the standalone Blazie add-on's bl2000/ too
        val b = "synthDrivers/_ssi263_blazie/"
        assertEquals(listOf(FirmwareImport.ENGLISH, HUNGARIAN), languages(inspect("blazie.nvda-addon",
            zip(b + "BL2ENG.BNS" to bns('E'), b + "bl2000/BL2HUN.BNS" to bns('H')))))
        // ... and in any other folder of it
        assertEquals(listOf(HUNGARIAN), languages(inspect("other.zip",
            zip("synthDrivers/_ssi263_blazie/other/BL2HUN.BNS" to bns('H')))))
    }

    @Test fun the085AddOnBringsTheSoundingBoardItCarries() {
        // 0.8.5's add-on carries GW Micro's two drivers beside SPEAKOUT.HEX (Tomi, 2026-10-18): one import, both voices
        val s = "synthDrivers/_ssi263_unified/_ssi263_speakout/"
        val plan = inspect("ssi263-speech-0.8.5.nvda-addon", zip("manifest.ini" to "name = ssi263".toByteArray(),
            s + "SPEAKOUT.HEX" to hex(true), s + "SBLOAD.COM" to sbLoad, s + "SB.COM" to sbCom))
        assertNull(plan.refusal)
        assertEquals(setOf(FirmwareImport.SPEAKOUT, FirmwareImport.SOUNDINGBOARD), languages(plan).toSet())
        val sb = plan.found.single { it.language == FirmwareImport.SOUNDINGBOARD }
        assertEquals(sbLoad.toList(), sb.firmware.readBytes().toList())
        assertEquals(sbCom.toList(), sb.parts.single().readBytes().toList())
    }

    @Test fun theEmulatorDownloadBringsEveryUnitItCarries() {
        // blazie-emu-0.8.0-windows.zip as it is: the program beside firmware/, each unit's files in it or below it
        // (Tomi: someone who imports the emulator's download gets its units, as from the add-on)
        FirmwareImport.releases = RELEASES
        val plan = inspect("blazie-emu-0.8.0-windows.zip", zip("blazie_emu.exe" to "MZ".toByteArray() + ByteArray(900),
            "firmware/BL2ENG.BNS" to bns('E'), "firmware/bl2_2003_warm.state" to state(),
            "firmware/spanish/BL2SPA.BNS" to bns('S'), "firmware/german/Lite2000.BNS" to bns('G'),
            "firmware/bl2000/BL2HUN.BNS" to bns('H'), "firmware/bns2000/BS2SLL.BNS" to bns('K'),
            "firmware/bns640/june2003/BSPENG.BNS" to bns('6')))
        assertNull(plan.refusal)
        assertEquals(setOf(FirmwareImport.ENGLISH, FirmwareImport.SPANISH, FirmwareImport.GERMAN, FirmwareImport.SLOVAK,
            HUNGARIAN, FirmwareImport.UNIT_RELEASE + BNS640_ROW), languages(plan).toSet())
        // the same folder one level down (a zip someone made of the download's folder)
        assertEquals(listOf(FirmwareImport.SPANISH), languages(inspect("emu.zip",
            zip("blazie-emu-0.8.0-windows/firmware/spanish/BL2SPA.BNS" to bns('S')))))
        // ... and deeper still
        assertEquals(listOf(FirmwareImport.SPANISH), languages(inspect("deep.zip",
            zip("firmware/a/b/c/d/BL2SPA.BNS" to bns('S')))))
    }

    @Test fun theFirmwareCollectionBringsEveryUnitAndKnowsItsArchive() {
        // blazie-firmware-collection-<date>.zip (Tomi: the big bundle imports, and the import knows its valid files):
        // the emulator's folder under another name, and system/ with every release of every unit -- the same release
        // twice is one, another release is said, a unit no app runs and a program are not firmware to take
        FirmwareImport.releases = RELEASES
        val plan = inspect("blazie-firmware-collection-2026-10-18.zip", zip("README.txt" to "text".toByteArray(),
            "emulator-firmware-folder/BL2ENG.BNS" to bns('E'), "emulator-firmware-folder/bl2_2003_warm.state" to state(),
            "emulator-firmware-folder/spanish/BL2SPA.BNS" to bns('S'),
            "emulator-firmware-folder/bl2000/BL2HUN.BNS" to bns('H'),
            "emulator-firmware-folder/bns640/june2003/BSPENG.BNS" to bns('6'),
            "system/BL2 Braille Lite 2000/BL2ENG_a55455eb.BNS" to bns('E'),
            "system/BL2 Braille Lite 2000/BL2ENG_a4d13165.BNS" to bns('e'),
            "system/BL4 Braille Lite 40/BL4ENG-1.BNS" to bns('T'),
            "system/BSP Braille n Speak 640/BSPENG_ff0738fe.BNS" to bns('6'),
            "programs/ADDCAL.BNS" to ByteArray(4000) { 0x18 }))
        assertNull(plan.refusal)
        assertEquals(setOf(FirmwareImport.ENGLISH, FirmwareImport.SPANISH, HUNGARIAN,
            FirmwareImport.UNIT_RELEASE + BNS640_ROW), languages(plan).toSet())
        assertEquals("each once", 4, plan.found.size)
        assertEquals("emulator-firmware-folder/BL2ENG.BNS", plan.found.first { it.language == FirmwareImport.ENGLISH }.from)
        assertTrue(plan.notes.toString(), plan.notes.any { it.startsWith("system/BL2 Braille Lite 2000/BL2ENG_a4d13165.BNS " +
            "is another English release") })
        assertTrue(plan.notes.toString(), plan.notes.none { "deeper in the zip" in it || "a55455eb" in it })
    }

    @Test fun anArchiveOfOnesOwnFoldersBringsEverySynth() {
        // Tomi, 2026-10-18: "top-level folders for each synth name: one gwmicro/sb.zip, gwmicro/speakout.hex and so
        // on" -- any folders, any names, zips in it opened; the add-on in it too, bigger than any firmware
        FirmwareImport.releases = RELEASES
        val noise = java.util.Random(263).let { r -> ByteArray(5 shl 20).also { r.nextBytes(it) } }
        val plan = inspect("my-synths.zip", zip(
            "synths/gwmicro/sb.zip" to zip("SBLOAD.COM" to sbLoad, "SB.COM" to sbCom, "README.DOC" to "x".toByteArray()),
            "synths/gwmicro/speakout.hex" to hex(true),
            "synths/blazie/braille lite 2000/english/BL2ENG.BNS" to bns('E'),
            "synths/sweet micro/disks/toolkit.dsk" to disk(true),
            "add-ons/ssi263-speech-0.8.5.nvda-addon" to zip(
                "synthDrivers/_ssi263_unified/_ssi263_blazie/bl2000/BL2HUN.BNS" to bns('H'), "noise.bin" to noise),
            "photos/cat.jpg" to noise.copyOf(6 shl 20)))
        assertNull(plan.refusal)
        assertEquals(setOf(FirmwareImport.ENGLISH, FirmwareImport.SPEAKOUT, FirmwareImport.MOCKINGBOARD,
            FirmwareImport.SOUNDINGBOARD, HUNGARIAN), languages(plan).toSet())
        assertEquals("synths/gwmicro/sb.zip, SBLOAD.COM and synths/gwmicro/sb.zip, SB.COM",
            plan.found.first { it.language == FirmwareImport.SOUNDINGBOARD }.from)
        assertEquals("add-ons/ssi263-speech-0.8.5.nvda-addon, synthDrivers/_ssi263_unified/_ssi263_blazie/bl2000/" +
            "BL2HUN.BNS", plan.found.first { it.language == HUNGARIAN }.from)
        assertTrue(plan.notes.toString(), plan.notes.isEmpty())
    }

    @Test fun aUnitImportedAloneIsSaidToBeNoVoice() {
        // Tomi, 2026-10-18: a Braille 'n Speak 640 alone is "not a voice", never "the voice speaks now"
        val alone = inspect("BSPENG.BNS", bns('6'))
        val said = FirmwareImport.doneMessage(alone.found)
        assertEquals("Imported Braille 'n Speak 640 English June 2003 for the Blazie Engineering unit screen. Note: " +
            "this one cannot be used as a voice; it runs on the unit screen.", said)
        // beside a voice: the voice speaks, the unit is said apart
        val both = FirmwareImport.doneMessage(inspect("both.zip", zip("BL2ENG.BNS" to bns('E'), "BSPENG.BNS" to bns('6'))).found)
        assertTrue(both, both.startsWith("Imported English June 2003. The voice speaks now. Also imported " +
            "Braille 'n Speak 640 English June 2003 for the Blazie Engineering unit screen. Note: this one cannot"))
        assertEquals("Imported English June 2003. The voice speaks now.",
            FirmwareImport.doneMessage(inspect("BL2ENG.BNS", bns('E')).found))
    }

    @Test fun aProgramThatOnlyLooksLikeAZipIsNotFirmware() {
        // the collection's programs/unzip.BNS (found on Tomi's phone, 2026-10-18): Blazie's unzip program carries a zip
        // signature with a name that is no text; opening it threw past the import, and the whole zip "could not be
        // read", its message the program's bytes
        val header = byteArrayOf(0x50, 0x4B, 3, 4, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            3, 0, 0, 0) + byteArrayOf(0xC3.toByte(), 0x28, 0xFF.toByte())
        val unzip = ByteArray(5000) { 0x18 } + header + ByteArray(200)
        val plan = inspect("collection.zip", zip("emulator-firmware-folder/BL2ENG.BNS" to bns('E'),
            "programs/unzip.BNS" to unzip))
        assertNull(plan.refusal)
        assertEquals(listOf(FirmwareImport.ENGLISH), languages(plan))
        // ... and alone: no firmware, said in words
        val alone = inspect("unzip.zip", zip("unzip.BNS" to unzip))
        assertTrue(alone.refusal!!, alone.refusal!!.startsWith(FirmwareImport.NO_FIRMWARE_ZIP))
    }

    @Test fun aZipInAZipInTheSourceIsSaid() {
        // the import opens one level of zips; a deeper one is named, never dropped in silence (its control: unopened)
        val plan = inspect("outer.zip", zip("BL2ENG.BNS" to bns('E'),
            "backup.zip" to zip("old/inner.zip" to zip("BL2SPA.BNS" to bns('S')))))
        assertEquals(listOf(FirmwareImport.ENGLISH), languages(plan))
        assertTrue(plan.notes.toString(), plan.notes.any { it.startsWith("backup.zip, old/inner.zip: a zip inside a zip") })
        // ... and alone, in the refusal
        val alone = inspect("outer.zip", zip("backup.zip" to zip("inner.zip" to zip("BL2SPA.BNS" to bns('S')))))
        assertTrue(alone.refusal!!, alone.refusal!!.contains("backup.zip, inner.zip: a zip inside a zip"))
    }

    @Test fun aReleaseVoiceHasItsFilesAndNames() {
        FirmwareImport.releases = RELEASES
        assertEquals(listOf("BL2HUN.BNS", "bl2_hu_1999_07.state"), FirmwareImport.files(HUNGARIAN))
        assertEquals("Braille Lite 2000 (Hungarian)", FirmwareImport.unitName(HUNGARIAN))
        assertEquals("Hungarian", FirmwareImport.languageName(HUNGARIAN))
        assertTrue(FirmwareImport.isBlazie(TURKISH))
        // the firmware list names the file to bring: the table's for a release voice, the known one for the others
        assertEquals("BL2HUN.BNS", FirmwareImport.bring(HUNGARIAN))
        for (v in FirmwareImport.IMPORTED) assertTrue("voice $v", FirmwareImport.bring(v) != null)
        assertFalse(TURKISH in FirmwareImport.NO_STATE)
        val shipped = FirmwareImport.FILES.values.flatten()
        assertTrue("a release voice's files are its own", RELEASES.flatMap { it.files }.none { it in shipped })
        // the BL2FRE release's name says which of the French releases it is; its language is French
        assertEquals("French", FirmwareImport.Release(20, "bl2_fr_fre_1999_07", "BL2FRE.BNS",
            "Braille Lite 2000 (French, BL2FRE)", "Braille Lite 2000 (français)", "fr").language)
        assertEquals("Arabic, UAE", FirmwareImport.Release(21, "bl2_ar_ae_1998_12", "BL2UAE.BNS",
            "Braille Lite 2000 (Arabic, UAE)", "Braille Lite 2000 (العربية، الإمارات)", "ar-AE").language)
    }

    @Test fun theWordsNameEveryUnit() {
        assertEquals("This file does not contain Braille Lite, Braille 'n Speak, Speak-Out or Mockingboard firmware.",
            FirmwareImport.NO_FIRMWARE_FILE)
        assertTrue(FirmwareImport.WHAT_TO_CHOOSE.contains("Lite2000.BNS (German, Italian)"))
        assertTrue(FirmwareImport.WHAT_TO_CHOOSE.contains("Slovak Braille 'n Speak 2000's: BS2SLL.BNS"))
        assertTrue(FirmwareImport.WHAT_TO_CHOOSE.contains("GW Micro's SPEAKOUT.HEX, or the speakout.zip holding it"))
        assertTrue(FirmwareImport.WHAT_TO_CHOOSE.contains("the Mockingboard's: mockingboard-tts-1.1.bin"))
        assertEquals("This is a state file, not firmware. Please import only firmware files, or zips containing " +
            "them, with this tool.", FirmwareImport.STATE_FILE)
    }
}
