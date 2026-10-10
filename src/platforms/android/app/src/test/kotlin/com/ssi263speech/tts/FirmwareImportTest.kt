// FirmwareImport's layout and wording on the JVM, with no phone and no firmware: the native side's judgement is
// played by a fake that knows a made-up image by the same signature (F3 C3 xx xx FF "COPYRIGHT") plus a letter for
// what it is, and a made-up Intel HEX text (":" records) as the Speak-Out's when it says GOOD, a damaged one when not,
// and made-up bytes starting "SWEETMICRO" as the Mockingboard's file.
// The real judgement (bl_firmware.c, its list of releases; ssa_import.c, SPEAKOUT.HEX's sha256) has its own tests,
// test_import_native.py and test_android_native.py, on the real files.
//     gradlew testDebugUnitTest                              every case
//     gradlew testDebugUnitTest -Pssi263ImportBreak=1        control: the layout rules off; the layout cases fail
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
     * list), S (Spanish), U (a Braille Lite 2000 release not on the list) or T (Type 'n Speak). */
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

    /** A made-up state: the size every state has, whatever is in it. */
    private fun state() = ByteArray(FirmwareImport.STATE_SIZE) { (it * 13).toByte() }

    private companion object {
        val MOCKINGBOARD_FILE = "SWEETMICRO".toByteArray() + ByteArray(2000) { (it * 5).toByte() }
    }

    private object Fake : FirmwareImport.Identify {
        val labels = listOf("English June 2003", "English September 2000", "Spanish September 2000")

        override fun firmware(data: ByteArray, out: File): Pair<Int, String> {
            if (data.isNotEmpty() && data[0] == ':'.code.toByte()) {
                if (!String(data, Charsets.ISO_8859_1).contains("GOOD"))
                    return FirmwareImport.OTHER_HEX to "an Intel HEX file, but damaged: line 2's checksum or length is wrong"
                out.writeBytes(data)
                return FirmwareImport.SPEAKOUT to "GW Micro Speak-Out: SPEAKOUT.HEX"
            }
            if (String(data, 0, minOf(9, data.size), Charsets.ISO_8859_1) == "DOS33DISK") {
                if (!String(data, Charsets.ISO_8859_1).contains("TOOLKIT"))
                    return FirmwareImport.OTHER_DISK to "a Mockingboard disk, but not the text-to-speech version 1.1 " +
                        "this voice runs"
                out.writeBytes(MOCKINGBOARD_FILE)
                return FirmwareImport.MOCKINGBOARD to "Mockingboard: Sweet Micro Systems' text-to-speech 1.1"
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
                'U' -> return FirmwareImport.UNKNOWN to "not a release this app knows"
                else -> return FirmwareImport.REFUSED to "not a release this voice can run"
            }
            out.writeBytes(data.copyOfRange(at, data.size))
            return language to label
        }

        override fun known() = labels
    }

    private fun zip(vararg entries: Pair<String, ByteArray>): ByteArray {
        val out = ByteArrayOutputStream()
        ZipOutputStream(out).use { z ->
            for ((name, bytes) in entries) { z.putNextEntry(ZipEntry(name)); z.write(bytes); z.closeEntry() }
        }
        return out.toByteArray()
    }

    private fun inspect(name: String, data: ByteArray) =
        FirmwareImport.inspect(name, data, temp.newFolder(), Fake)

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
        assertTrue(plan.refusal!!, plan.refusal!!.startsWith("This zip does not contain Braille Lite, Speak-Out or " +
            "Mockingboard firmware."))
    }

    @Test fun firmwareTwoFoldersDownIsNotLookedForButNamed() {
        val plan = inspect("deep.zip", zip("backup/blt2000/BL2ENG.BNS" to bns('E')))
        assertTrue(plan.found.isEmpty())
        assertTrue(plan.refusal!!.startsWith(FirmwareImport.NO_FIRMWARE_ZIP))
        assertTrue(plan.refusal!!, plan.refusal!!.contains("backup/blt2000/BL2ENG.BNS is deeper than that"))
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
            "The releases this app knows: English June 2003; English September 2000; Spanish September 2000."))
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

    @Test fun aHexTwoFoldersDownIsNamed() {
        val plan = inspect("deep.zip", zip("backup/speakout/SPEAKOUT.HEX" to hex(true)))
        assertTrue(plan.found.isEmpty())
        assertTrue(plan.refusal!!, plan.refusal!!.contains("backup/speakout/SPEAKOUT.HEX is deeper than that"))
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

    @Test fun theMockingboardFileTwoFoldersDownIsNamed() {
        val plan = inspect("deep.zip", zip("backup/mockingboard/mockingboard-tts-1.1.bin" to mockingboard()))
        assertTrue(plan.found.isEmpty())
        assertTrue(plan.refusal!!, plan.refusal!!.contains("backup/mockingboard/mockingboard-tts-1.1.bin is deeper " +
            "than that"))
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

    @Test fun theMockingboardHasNoStateToMake() {
        assertTrue(FirmwareImport.MOCKINGBOARD in FirmwareImport.NO_STATE)
        assertEquals(listOf("mockingboard-tts-1.1.bin"), FirmwareImport.FILES[FirmwareImport.MOCKINGBOARD])
        assertTrue(FirmwareImport.MOCKINGBOARD in FirmwareImport.IMPORTED)
    }

    @Test fun theWordsNameEveryUnit() {
        assertEquals("This file does not contain Braille Lite, Speak-Out or Mockingboard firmware.",
            FirmwareImport.NO_FIRMWARE_FILE)
        assertTrue(FirmwareImport.WHAT_TO_CHOOSE.contains("GW Micro's SPEAKOUT.HEX, or the speakout.zip holding it"))
        assertTrue(FirmwareImport.WHAT_TO_CHOOSE.contains("the Mockingboard's: mockingboard-tts-1.1.bin"))
        assertEquals("This is a state file, not firmware. Please import only firmware files, or zips containing " +
            "them, with this tool.", FirmwareImport.STATE_FILE)
    }
}
