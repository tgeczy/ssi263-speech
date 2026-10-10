// FirmwareImport's layout and wording on the Mac, with no device and no firmware: Android's FirmwareImportTest.kt in
// Swift, every case of it, plus the Accents' files and the zip forms the Apple side reads itself (ZipReader.swift:
// stored and deflated entries, sizes in a data descriptor).  The native side's judgement is played by a fake that
// knows a made-up image by the same signature (F3 C3 xx xx FF "COPYRIGHT") plus a letter for what it is, a made-up
// Intel HEX text as the Speak-Out's when it says GOOD, and made-up Aicom files by a first line ("AICOM u2" ...).  The
// real judgement has its own tests on the real files (test_apple_speech.py, test_android_native.py).
//
//     sh src/platforms/apple/test/run_swift_tests.sh                      every case
//     SSI263_IMPORT_BREAK=1|state|speakout|accent sh .../run_swift_tests.sh    the controls: each must FAIL

import Compression
import Foundation

// ---- a zip writer for the fixtures: stored or deflated, the sizes in the header or in a data descriptor ----------

func crc32(_ d: Data) -> UInt32 {
    var c: UInt32 = 0xFFFF_FFFF
    for b in d {
        c ^= UInt32(b)
        for _ in 0..<8 { c = (c >> 1) ^ (0xEDB8_8320 & (0 &- (c & 1))) }
    }
    return ~c
}

func deflate(_ d: Data) -> Data {
    let cap = d.count + 1024
    var out = Data(count: cap)
    let n = out.withUnsafeMutableBytes { (o: UnsafeMutableRawBufferPointer) -> Int in
        d.withUnsafeBytes { (i: UnsafeRawBufferPointer) -> Int in
            compression_encode_buffer(o.bindMemory(to: UInt8.self).baseAddress!, cap,
                                      i.bindMemory(to: UInt8.self).baseAddress!, d.count, nil, COMPRESSION_ZLIB)
        }
    }
    return out.prefix(n)
}

enum ZipStyle { case stored, deflated, descriptor }

func zip(_ entries: [(String, Data)], style: ZipStyle = .deflated) -> Data {
    var out = Data()
    func le16(_ v: Int) { out.append(UInt8(v & 0xFF)); out.append(UInt8(v >> 8 & 0xFF)) }
    func le32(_ v: Int) { le16(v & 0xFFFF); le16(v >> 16 & 0xFFFF) }
    var central = Data()
    var count = 0
    for (name, bytes) in entries {
        let offset = out.count
        let method = style == .stored ? 0 : 8
        let body = style == .stored ? bytes : deflate(bytes)
        let crc = Int(crc32(bytes))
        let flags = style == .descriptor ? 8 : 0
        let nameData = name.data(using: .utf8)!
        out.append(contentsOf: [0x50, 0x4B, 3, 4])
        le16(20); le16(flags); le16(method); le16(0); le16(0)
        le32(style == .descriptor ? 0 : crc)
        le32(style == .descriptor ? 0 : body.count)
        le32(style == .descriptor ? 0 : bytes.count)
        le16(nameData.count); le16(0)
        out.append(nameData)
        out.append(body)
        if style == .descriptor {
            out.append(contentsOf: [0x50, 0x4B, 7, 8]); le32(crc); le32(body.count); le32(bytes.count)
        }
        // the central directory entry (ZipReader never reads it, but a real zip has one)
        var c = Data([0x50, 0x4B, 1, 2])
        func c16(_ v: Int) { c.append(UInt8(v & 0xFF)); c.append(UInt8(v >> 8 & 0xFF)) }
        func c32(_ v: Int) { c16(v & 0xFFFF); c16(v >> 16 & 0xFFFF) }
        c16(20); c16(20); c16(flags); c16(method); c16(0); c16(0); c32(crc); c32(body.count); c32(bytes.count)
        c16(nameData.count); c16(0); c16(0); c16(0); c16(0); c32(0); c32(offset)
        c.append(nameData)
        central.append(c)
        count += 1
    }
    let cdAt = out.count
    out.append(central)
    out.append(contentsOf: [0x50, 0x4B, 5, 6]); le16(0); le16(0); le16(count); le16(count)
    le32(central.count); le32(cdAt); le16(0)
    return out
}

// ---- the fixtures --------------------------------------------------------------------------------------------------

func bytes(_ s: String) -> Data { s.data(using: .isoLatin1)! }
func filled(_ n: Int, _ f: (Int) -> UInt8) -> Data { Data((0..<n).map(f)) }

/// A made-up image: the signature, then E (English, the newest release), e (an older English release on the list),
/// S (Spanish), U (a Braille Lite 2000 release not on the list) or T (Type 'n Speak).
func image(_ kind: Character) -> Data {
    Data([0xF3, 0xC3, 0x32, 0x03, 0xFF]) + bytes("COPYRIGHT") + bytes(String(kind)) + filled(1000) { UInt8(($0 * 7) & 0xFF) }
}

/// A .BNS: a loader, then the image at 3000h.
func bns(_ kind: Character) -> Data { Data(repeating: 0x18, count: 0x3000) + image(kind) }

/// A made-up Intel HEX text: the Speak-Out's when it says GOOD, a damaged one when it says BAD.
func hex(_ good: Bool) -> Data {
    bytes(":020000020000FC\r\n:10000000" + (good ? "GOOD" : "BAD0") + "000000000000000000000000000000\r\n:00000001FF\r\n")
}

/// A made-up Aicom file: its first line says which.
func aicom(_ which: String) -> Data { bytes("AICOM \(which)\n") + filled(4000) { UInt8(($0 * 11) & 0xFF) } }

/// A made-up Mockingboard file: the native side knows the real one by its sha256.
let mockingboardFile = bytes("SWEETMICRO") + filled(2000) { UInt8(($0 * 5) & 0xFF) }

/// A made-up disk image: the toolkit's when it says TOOLKIT (the fake then gives the file), another when not.
func disk(_ toolkit: Bool) -> Data { bytes("DOS33DISK") + bytes(toolkit ? "TOOLKIT" : "OTHER") + Data(count: 3000) }

/// A made-up state: the size every state has, whatever is in it.
func state() -> Data { filled(FirmwareImport.stateSize) { UInt8(($0 * 13) & 0xFF) } }

struct Fake: FirmwareImport.Identify {
    static let labels = ["English June 2003", "English September 2000", "Spanish September 2000"]

    func firmware(_ data: Data, out: URL) -> (Int, String) {
        let text = String(data: data.prefix(16), encoding: .isoLatin1) ?? ""
        if text.hasPrefix("DOS33DISK") {
            if !(String(data: data, encoding: .isoLatin1) ?? "").contains("TOOLKIT") {
                return (FirmwareImport.otherDisk, "a Mockingboard disk, but not the text-to-speech version 1.1 this " +
                        "voice runs")
            }
            try? mockingboardFile.write(to: out)
            return (FirmwareImport.mockingboard, "Mockingboard: Sweet Micro Systems' text-to-speech 1.1")
        }
        if text.hasPrefix("SWEETMICRO") {
            try? data.write(to: out)
            return (FirmwareImport.mockingboard, "Mockingboard: Sweet Micro Systems' text-to-speech 1.1")
        }
        if text.hasPrefix("AICOM ") {
            let which = String(text.dropFirst(6).prefix(while: { $0 != "\n" }))
            let kinds = ["u2": FirmwareImport.accentU2, "u3": FirmwareImport.accentU3, "u4": FirmwareImport.accentU4,
                         "dvc": FirmwareImport.accentMini]
            guard let k = kinds[which] else { return (FirmwareImport.none, "no firmware") }
            try? data.write(to: out)
            return (k, "Aicom \(which)")
        }
        if data.first == UInt8(ascii: ":") {
            if !(String(data: data, encoding: .isoLatin1) ?? "").contains("GOOD") {
                return (FirmwareImport.otherHex, "an Intel HEX file, but damaged: line 2's checksum or length is wrong")
            }
            try? data.write(to: out)
            return (FirmwareImport.speakout, "GW Micro Speak-Out: SPEAKOUT.HEX")
        }
        let b = [UInt8](data), sig = [UInt8](bytes("COPYRIGHT"))
        var found: Int?
        if b.count >= 15 {
            for i in 0...(b.count - 15) where b[i] == 0xF3 && b[i + 1] == 0xC3 && b[i + 4] == 0xFF {
                if Array(b[(i + 5)..<(i + 14)]) == sig { found = i; break }
            }
        }
        guard let at = found else { return (FirmwareImport.none, "no Braille Lite firmware in it") }
        let kind: Int, label: String
        switch Character(UnicodeScalar(b[at + 14])) {
        case "E": (kind, label) = (FirmwareImport.english, Fake.labels[0])
        case "e": (kind, label) = (FirmwareImport.english, Fake.labels[1])
        case "S": (kind, label) = (FirmwareImport.spanish, Fake.labels[2])
        case "U": return (FirmwareImport.unknown, "not a release this app knows")
        default: return (FirmwareImport.refused, "not a release this voice can run")
        }
        try? Data(b[at...]).write(to: out)
        return (kind, label)
    }

    func known() -> [String] { Fake.labels }
}

// ---- the harness ---------------------------------------------------------------------------------------------------

var failures = 0
var current = ""
let tmpRoot = FileManager.default.temporaryDirectory.appendingPathComponent("ssi263-import-tests-\(getpid())")
var tmpCount = 0

func check(_ ok: Bool, _ what: @autoclosure () -> String, line: Int = #line) {
    if !ok {
        failures += 1
        print("FAIL  \(current) (line \(line)): \(what())")
    }
}

func eq<T: Equatable>(_ a: T, _ b: T, line: Int = #line) { check(a == b, "\(a) != \(b)", line: line) }

func inspect(_ name: String, _ data: Data) -> FirmwareImport.Plan {
    tmpCount += 1
    return FirmwareImport.inspect(name: name, data: data, staging: tmpRoot.appendingPathComponent("s\(tmpCount)"),
                                  id: Fake())
}

func kinds(_ p: FirmwareImport.Plan) -> [Int] { p.found.map { $0.kind } }

let E = FirmwareImport.english, S = FirmwareImport.spanish, SO = FirmwareImport.speakout

var tests: [(String, () -> Void)] = []
func test(_ name: String, _ body: @escaping () -> Void) { tests.append((name, body)) }

// ---- layouts -------------------------------------------------------------------------------------------------------

test("zipWithTheFirmwareAtItsTop") {
    let plan = inspect("update.zip", zip([("BL2ENG.BNS", bns("E")), ("HOWTOUPD.TXT", bytes("text"))]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [E])
    eq(plan.found.first?.from, "BL2ENG.BNS")
    eq(plan.found.first?.label, "English June 2003")
    eq(plan.found.first.flatMap { try? Data(contentsOf: $0.firmware) }, image("E"))
}

for style in [ZipStyle.stored, .deflated, .descriptor] {
    test("everyZipForm \(style)") {
        let plan = inspect("forms.zip", zip([("a.txt", bytes("hello")), ("blt2000/BL2ENG.BNS", bns("E")),
                                             ("spanish/BL2SPA.BNS", bns("S"))], style: style))
        eq(plan.refusal, nil)
        eq(kinds(plan), [E, S])
    }
}

test("zipWithTheFirmwareOneFolderDown") {
    let plan = inspect("june2003.zip", zip([("blt2000/BL2ENG.BNS", bns("E")), ("spanish/BL2SPA.BNS", bns("S"))]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [E, S])
    eq(plan.found.count > 1 ? plan.found[1].from : "", "spanish/BL2SPA.BNS")
}

test("theNvdaAddOnGivesItsFirmwareButNotItsStates") {
    let d = "synthDrivers/_ssi263_blazie/"
    let plan = inspect("blazie.nvda-addon", zip([("manifest.ini", bytes("name = blazie")),
        ("synthDrivers/blazie.py", bytes("#")), (d + "BL2ENG.BNS", bns("E")), (d + "bl2_2003_warm.state", state()),
        (d + "BL2SPA.BNS", bns("S")), (d + "bl2spa_fresh.state", state()), (d + "bl.dll", Data(count: 5000))]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [E, S])
    eq(plan.notes.count, 1)
    check(plan.notes.first?.hasPrefix("\(d)bl2_2003_warm.state, \(d)bl2spa_fresh.state are state files, not " +
        "firmware, and are not used") == true, "\(plan.notes)")
}

test("aZipWithNoFirmwareSaysSo") {
    let plan = inspect("photos.zip", zip([("a.txt", bytes("hello")), ("b/c.bin", Data(count: 300))]))
    eq(plan.found.count, 0)
    check(plan.refusal?.hasPrefix(FirmwareImport.noFirmwareZip) == true, plan.refusal ?? "nil")
}

test("firmwareTwoFoldersDownIsNotLookedForButNamed") {
    let plan = inspect("deep.zip", zip([("backup/blt2000/BL2ENG.BNS", bns("E"))]))
    eq(plan.found.count, 0)
    check(plan.refusal?.hasPrefix(FirmwareImport.noFirmwareZip) == true, plan.refusal ?? "nil")
    check(plan.refusal?.contains("backup/blt2000/BL2ENG.BNS is deeper than that") == true, plan.refusal ?? "nil")
}

test("namesDoNotMatterContentDoes") {
    let plan = inspect("x.zip", zip([("BL2ENG.BNS", bytes("not firmware")), ("renamed.bin", bns("S"))]))
    eq(kinds(plan), [S])
    eq(plan.found.first?.from, "renamed.bin")
}

test("anUpdateProgramWithTheImageInside") {
    let exe = bytes("MZ") + Data(repeating: 1, count: 7000) + bns("E") + Data(repeating: 2, count: 3000)
    let plan = inspect("BLT2000.EXE", exe)
    eq(plan.refusal, nil)
    eq(kinds(plan), [E])
    eq(plan.found.first?.from, "BLT2000.EXE")
}

test("anUpdateProgramThatIsAZipBehindItsCode") {
    let exe = bytes("MZ") + Data(repeating: 1, count: 9000) + zip([("BL2ENG.BNS", bns("E")), ("SPELL.DIC", Data(count: 99))])
    let plan = inspect("blt2000.exe", exe)
    eq(plan.refusal, nil)
    eq(kinds(plan), [E])
}

test("anUpdateProgramInsideAZip") {
    let exe = bytes("MZ") + Data(repeating: 1, count: 9000) + zip([("BL2ENG.BNS", bns("E"))])
    let plan = inspect("fs.zip", zip([("FS june2003/blt2000.exe", exe)]))
    eq(kinds(plan), [E])
    eq(plan.found.first?.from, "FS june2003/blt2000.exe, BL2ENG.BNS")
}

test("aSingleBnsFile") {
    let plan = inspect("BL2SPA.BNS", bns("S"))
    eq(kinds(plan), [S])
    eq(plan.found.first?.label, "Spanish September 2000")
}

test("randomBytesAreNotFirmware") {
    let plan = inspect("noise.bin", filled(300000) { UInt8(($0 * 31 + 7) & 0xFF) })
    eq(plan.found.count, 0)
    check(plan.refusal?.hasPrefix(FirmwareImport.noFirmwareFile) == true, plan.refusal ?? "nil")
}

test("aDamagedZipSaysSo") {
    var z = zip([("BL2ENG.BNS", bns("E"))])
    z = z.prefix(200)                      // cut inside the deflated data
    let plan = inspect("cut.zip", z)
    eq(plan.found.count, 0)
    check(plan.refusal?.hasPrefix("This zip is damaged") == true, plan.refusal ?? "nil")
}

// ---- only releases on the list (Tomi) ------------------------------------------------------------------------------

test("anotherUnitsFirmwareIsRefused") {
    let plan = inspect("TNSENG-1.TNS", bns("T"))
    eq(plan.found.count, 0)
    check(plan.refusal?.hasPrefix(FirmwareImport.noFirmwareFile) == true, plan.refusal ?? "nil")
    check(plan.refusal?.contains("TNSENG-1.TNS is Blazie firmware, but not a Braille Lite 2000 release this voice " +
        "can run.") == true, plan.refusal ?? "nil")
}

test("anUnknownReleaseIsRefusedAndTheKnownOnesNamed") {
    let plan = inspect("BL2GER.BNS", bns("U"))
    eq(plan.found.count, 0)
    check(plan.refusal?.hasPrefix("BL2GER.BNS is Braille Lite 2000 firmware, but not a release this app knows, so " +
        "it cannot be imported") == true, plan.refusal ?? "nil")
    check(plan.refusal?.contains("The releases this app knows: English June 2003; English September 2000; Spanish " +
        "September 2000.") == true, plan.refusal ?? "nil")
}

test("anUnknownReleaseInAZipIsRefused") {
    let plan = inspect("german.zip", zip([("blt2000/BL2GER.BNS", bns("U")), ("LIESMICH.TXT", Data(count: 10))]))
    eq(plan.found.count, 0)
    check(plan.refusal?.hasPrefix("blt2000/BL2GER.BNS is Braille Lite 2000 firmware, but not a release this app " +
        "knows") == true, plan.refusal ?? "nil")
}

test("twoUnknownReleasesAreRefused") {
    let plan = inspect("others.zip", zip([("a/BL2GER.BNS", bns("U")), ("b/BL2FRA.BNS", bns("U"))]))
    eq(plan.found.count, 0)
    check(plan.refusal?.hasPrefix("a/BL2GER.BNS, b/BL2FRA.BNS are Braille Lite 2000 firmware, but not a release " +
        "this app knows") == true, plan.refusal ?? "nil")
}

test("aKnownReleaseIsTakenAndAnUnknownOneLeftOut") {
    let plan = inspect("mixed.zip", zip([("de/BL2GER.BNS", bns("U")), ("en/BL2ENG.BNS", bns("E"))]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [E])
    eq(plan.found.first?.from, "en/BL2ENG.BNS")
    eq(plan.notes, ["de/BL2GER.BNS is Braille Lite 2000 firmware of a release this app does not know; it is left out."])
}

test("twoEnglishReleasesGiveTheNewest") {
    let plan = inspect("both.zip", zip([("once/BL2ENG.BNS", bns("e")), ("fs/BL2ENG.BNS", bns("E"))]))
    eq(kinds(plan), [E])
    eq(plan.found.first?.from, "fs/BL2ENG.BNS")
    eq(plan.notes.count, 1)
    check(plan.notes.first?.hasPrefix("once/BL2ENG.BNS is another English release (English September 2000); only " +
        "fs/BL2ENG.BNS") == true, "\(plan.notes)")
}

test("theSameReleaseTwiceIsOne") {
    let exe = bytes("MZ") + Data(repeating: 1, count: 9000) + zip([("BL2ENG.BNS", bns("E"))])
    let plan = inspect("FS june2003.zip", zip([("blt2000.exe", exe), ("blt2000/BL2ENG.BNS", bns("E"))]))
    eq(kinds(plan), [E])
    eq(plan.notes, [])
}

// ---- never a state (Tomi) ------------------------------------------------------------------------------------------

test("aStateAloneIsRefusedInTomisWords") {
    let plan = inspect("bl2_2003_warm.state", state())
    eq(plan.found.count, 0)
    eq(plan.refusal, FirmwareImport.stateFile)
}

test("aStateUnderAnotherNameIsStillAState") {
    eq(inspect("backup.bin", state()).refusal, FirmwareImport.stateFile)
}

test("aZipHoldingOnlyAStateIsRefused") {
    let plan = inspect("state.zip", zip([("bl2spa_fresh.state", state())]))
    eq(plan.found.count, 0)
    eq(plan.refusal, "This zip holds a state file (bl2spa_fresh.state), not firmware. Please import only firmware " +
        "files, or zips containing them, with this tool.")
}

test("aZipWithFirmwareAndAStateTakesTheFirmwareOnly") {
    let plan = inspect("unit.zip", zip([("BL2ENG.BNS", bns("E")), ("unit.dat", state())]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [E])
    eq(plan.notes, ["unit.dat is a state file, not firmware, and is not used: this device prepares the unit's state " +
        "itself from the firmware."])
}

// ---- the Speak-Out: GW Micro's SPEAKOUT.HEX, by content (Tomi, 0.7.5) ---------------------------------------------

test("theSpeakOutHexAlone") {
    let plan = inspect("SPEAKOUT.HEX", hex(true))
    eq(plan.refusal, nil)
    eq(kinds(plan), [SO])
    eq(plan.found.first?.label, "GW Micro Speak-Out: SPEAKOUT.HEX")
    eq(plan.found.first.flatMap { try? Data(contentsOf: $0.firmware) }, hex(true))
}

test("gwMicrosSpeakoutZip") {
    let plan = inspect("speakout.zip", zip([("README.TXT", bytes("Speak-Out update")), ("SPEAKOUT.HEX", hex(true)),
                                            ("UPDATE.EXE", bytes("MZ") + Data(count: 500))]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [SO])
    eq(plan.found.first?.from, "SPEAKOUT.HEX")
}

test("theSpeakOutHexOneFolderDown") {
    let plan = inspect("speakout.zip", zip([("speakout/SPEAKOUT.HEX", hex(true))]))
    eq(kinds(plan), [SO])
    eq(plan.found.first?.from, "speakout/SPEAKOUT.HEX")
}

test("aRenamedHexIsStillTheSpeakOuts") {
    eq(kinds(inspect("firmware.txt", hex(true))), [SO])
}

test("bothUnitsInOneZip") {
    let plan = inspect("units.zip", zip([("blt2000/BL2ENG.BNS", bns("E")), ("speakout/SPEAKOUT.HEX", hex(true))]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [E, SO])
}

test("aDamagedHexIsRefusedAndSaysWhy") {
    let plan = inspect("SPEAKOUT.HEX", hex(false))
    eq(plan.found.count, 0)
    check(plan.refusal?.hasPrefix("SPEAKOUT.HEX is an Intel HEX file, but damaged: line 2's checksum or length is " +
        "wrong. " + FirmwareImport.onlyTheHex) == true, plan.refusal ?? "nil")
    check(plan.refusal?.hasSuffix(FirmwareImport.whatToChoose) == true, plan.refusal ?? "nil")
}

test("aDamagedHexBesideFirmwareIsLeftOut") {
    let plan = inspect("mixed.zip", zip([("BL2ENG.BNS", bns("E")), ("old/SPEAKOUT.HEX", hex(false))]))
    eq(kinds(plan), [E])
    eq(plan.notes, ["old/SPEAKOUT.HEX is an Intel HEX file, but damaged: line 2's checksum or length is wrong; it is " +
        "left out."])
}

test("aHexTwoFoldersDownIsNamed") {
    let plan = inspect("deep.zip", zip([("backup/speakout/SPEAKOUT.HEX", hex(true))]))
    eq(plan.found.count, 0)
    check(plan.refusal?.contains("backup/speakout/SPEAKOUT.HEX is deeper than that") == true, plan.refusal ?? "nil")
}

test("aStateBesideTheHexIsNotUsed") {
    let plan = inspect("unit.zip", zip([("SPEAKOUT.HEX", hex(true)), ("unit.dat", state())]))
    eq(kinds(plan), [SO])
    eq(plan.notes.count, 1)
    check(plan.notes.first?.hasPrefix("unit.dat is a state file, not firmware") == true, "\(plan.notes)")
}

// ---- the Mockingboard: Sweet Micro Systems' file, by its sha256, or the toolkit disk holding it -------------------

let MB = FirmwareImport.mockingboard

test("theMockingboardFileAlone") {
    let plan = inspect("mockingboard-tts-1.1.bin", mockingboardFile)
    eq(plan.refusal, nil)
    eq(kinds(plan), [MB])
    eq(plan.found.first?.label, "Mockingboard: Sweet Micro Systems' text-to-speech 1.1")
    eq(plan.found.first.flatMap { try? Data(contentsOf: $0.firmware) }, mockingboardFile)
}

test("theMockingboardFileOneFolderDownBesideTheOthers") {
    let plan = inspect("units.zip", zip([("blt2000/BL2ENG.BNS", bns("E")), ("speakout/SPEAKOUT.HEX", hex(true)),
                                         ("mockingboard/mockingboard-tts-1.1.bin", mockingboardFile)]))
    eq(kinds(plan), [E, SO, MB])
    eq(plan.found.count > 2 ? plan.found[2].from : "", "mockingboard/mockingboard-tts-1.1.bin")
}

test("theMockingboardFileTwoFoldersDownIsNamed") {
    let plan = inspect("deep.zip", zip([("backup/mockingboard/mockingboard-tts-1.1.bin", mockingboardFile)]))
    eq(plan.found.count, 0)
    check(plan.refusal?.contains("backup/mockingboard/mockingboard-tts-1.1.bin is deeper than that") == true,
          plan.refusal ?? "nil")
}

test("theToolkitDiskGivesTheFile") {
    let plan = inspect("Mockingboard Developers toolkit.dsk", disk(true))
    eq(plan.refusal, nil)
    eq(kinds(plan), [MB])
    eq(plan.found.first.flatMap { try? Data(contentsOf: $0.firmware) }, mockingboardFile)
}

test("theToolkitDiskInAZip") {
    let plan = inspect("disks.zip", zip([("apple/toolkit.dsk", disk(true)), ("README.TXT", bytes("disks"))]))
    eq(kinds(plan), [MB])
    eq(plan.found.first?.from, "apple/toolkit.dsk")
}

test("aDiskTwoFoldersDownIsNamed") {
    let plan = inspect("deep.zip", zip([("backup/apple/toolkit.po", disk(true))]))
    eq(plan.found.count, 0)
    check(plan.refusal?.contains("backup/apple/toolkit.po is deeper than that") == true, plan.refusal ?? "nil")
}

test("anotherDiskIsRefusedAndSaysWhy") {
    let plan = inspect("mockingboard1.dsk", disk(false))
    eq(plan.found.count, 0)
    check(plan.refusal?.hasPrefix("mockingboard1.dsk is a Mockingboard disk, but not the text-to-speech version 1.1 " +
        "this voice runs.") == true, plan.refusal ?? "nil")
    check(plan.refusal?.hasSuffix(FirmwareImport.whatToChoose) == true, plan.refusal ?? "nil")
}

test("anotherDiskBesideFirmwareIsLeftOut") {
    let plan = inspect("mixed.zip", zip([("BL2ENG.BNS", bns("E")), ("mockingboard1.dsk", disk(false))]))
    eq(kinds(plan), [E])
    eq(plan.notes, ["mockingboard1.dsk is a Mockingboard disk, but not the text-to-speech version 1.1 this voice " +
        "runs; it is left out."])
}

test("theMockingboardHasNoStateToMake") {
    eq(FirmwareImport.stateFiles[MB], nil)
    eq(FirmwareImport.files[MB], "mockingboard-tts-1.1.bin")
    check(FirmwareImport.order.contains(MB), "the order has no Mockingboard")
}

// ---- the Accents: Aicom's files, which the Apple apps never carry ------------------------------------------------

let SA = [FirmwareImport.accentU2, FirmwareImport.accentU3, FirmwareImport.accentU4]

test("theAccentAddOnGivesItsRomsAndItsDriver") {
    let d = "synthDrivers/_ssi263_accent/"
    let plan = inspect("accent-ssi263.nvda-addon", zip([("manifest.ini", bytes("name = accent")),
        ("synthDrivers/accentmini.py", bytes("#")), (d + "AICOM.txt", bytes("notice")),
        (d + "accent-sa/u2.BIN", aicom("u2")), (d + "accent-sa/u3.BIN", aicom("u3")),
        (d + "accent-sa/u4.BIN", aicom("u4")), (d + "SPKEMS.DVC", aicom("dvc"))]))
    eq(plan.refusal, nil)
    eq(kinds(plan), SA + [FirmwareImport.accentMini])
    eq(plan.found.first?.from, d + "accent-sa/u2.BIN")
}

test("theSpeakOutAddOnGivesItsHex") {
    let plan = inspect("speakout-ssi263.nvda-addon", zip([("synthDrivers/speakout.py", bytes("#")),
        ("synthDrivers/_ssi263_speakout/SPEAKOUT.HEX", hex(true))]))
    eq(kinds(plan), [SO])
}

test("theRomsLooseAndRenamed") {
    let plan = inspect("roms.zip", zip([("a.bin", aicom("u4")), ("rom/b.bin", aicom("u2"))]))
    eq(kinds(plan), [FirmwareImport.accentU2, FirmwareImport.accentU4])
}

test("oneRomAlone") {
    let plan = inspect("u3.BIN", aicom("u3"))
    eq(plan.refusal, nil)
    eq(kinds(plan), [FirmwareImport.accentU3])
}

test("everyUnitInOneZip") {
    let plan = inspect("all.zip", zip([("BL2ENG.BNS", bns("E")), ("BL2SPA.BNS", bns("S")), ("SPEAKOUT.HEX", hex(true)),
        ("u2.BIN", aicom("u2")), ("u3.BIN", aicom("u3")), ("u4.BIN", aicom("u4")), ("SPKEMS.DVC", aicom("dvc"))]))
    eq(kinds(plan), [E, S, SO] + SA + [FirmwareImport.accentMini])
}

test("aRomTwoFoldersDownOutsideAnAddOnIsNamed") {
    let plan = inspect("deep.zip", zip([("backup/accent/u2.BIN", aicom("u2"))]))
    eq(plan.found.count, 0)
    check(plan.refusal?.contains("backup/accent/u2.BIN is deeper than that") == true, plan.refusal ?? "nil")
}

// ---- zips the real tools make: Finder's Compress (ditto), Info-ZIP's zip, Python's zipfile (NVDA's add-ons) ----

/// The fixtures written into a folder and zipped by `tool` there (OUT: the zip's path); nil when the tool did not
/// run.
func toolZip(_ tool: [String], _ files: [(String, Data)]) -> Data? {
    tmpCount += 1
    let dir = tmpRoot.appendingPathComponent("tool\(tmpCount)")
    let src = dir.appendingPathComponent("src")
    for (name, data) in files {
        let url = src.appendingPathComponent(name)
        try? FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try? data.write(to: url)
    }
    let out = dir.appendingPathComponent("out.zip")
    let p = Process()
    p.executableURL = URL(fileURLWithPath: tool[0])
    p.arguments = tool.dropFirst().map { $0 == "OUT" ? out.path : $0 }
    p.currentDirectoryURL = src
    p.standardOutput = FileHandle.nullDevice
    p.standardError = FileHandle.nullDevice
    guard (try? p.run()) != nil else { return nil }
    p.waitUntilExit()
    return p.terminationStatus == 0 ? try? Data(contentsOf: out) : nil
}

let unitFiles: [(String, Data)] = [("blt2000/BL2ENG.BNS", bns("E")), ("blt2000/README.TXT", bytes("x")),
                                   ("SPEAKOUT.HEX", hex(true)), ("u2.BIN", aicom("u2"))]
let pyZip = ["/usr/bin/python3", "-c", "import os, sys, zipfile; z = zipfile.ZipFile(sys.argv[1], 'w', " +
             "zipfile.ZIP_DEFLATED); [z.write(os.path.join(d, f)) for d, _, fs in os.walk('.') for f in fs]; z.close()", "OUT"]
for (label, tool) in [("ditto (Finder's Compress)", ["/usr/bin/ditto", "-c", "-k", "--sequesterRsrc", ".", "OUT"]),
                      ("Info-ZIP zip", ["/usr/bin/zip", "-r", "-q", "-X", "OUT", "."]),
                      ("Python zipfile", pyZip)] {
    test("aZipMadeBy \(label)") {
        guard let z = toolZip(tool, unitFiles) else { return check(false, "\(label) did not run") }
        let plan = inspect("made.zip", z)
        eq(plan.refusal, nil)
        eq(kinds(plan), [E, SO, FirmwareImport.accentU2])
    }
}

// ---- the words ------------------------------------------------------------------------------------------------------

test("theWordsNameEveryUnit") {
    eq(FirmwareImport.noFirmwareFile, "This file does not contain Braille Lite, Speak-Out, Mockingboard or Accent " +
        "firmware.")
    check(FirmwareImport.whatToChoose.contains("your own copy of the Mockingboard Developers Toolkit disk"), "")
    check(FirmwareImport.whatToChoose.contains("GW Micro's SPEAKOUT.HEX, or the speakout.zip holding it"), "")
    check(FirmwareImport.whatToChoose.contains("u2.BIN, u3.BIN and u4.BIN for the Accent SA and SPKEMS.DVC"), "")
    // a store app's words name no source: the package types, but never which package carries the firmware
    for w in ["NVDA add-on", "carries"] {
        check(!FirmwareImport.whatToChoose.contains(w), "whatToChoose says \"\(w)\"")
    }
    eq(FirmwareImport.stateFile, "This is a state file, not firmware. Please import only firmware files, or zips " +
        "containing them, with this tool.")
}

// ---- run -----------------------------------------------------------------------------------------------------------

for (name, body) in tests {
    current = name
    let before = failures
    body()
    if failures == before { print("ok    \(name)") }
}
try? FileManager.default.removeItem(at: tmpRoot)
print(failures == 0 ? "FirmwareImport: \(tests.count) of \(tests.count) cases" : "FAILED: \(failures)")
exit(failures == 0 ? 0 : 1)
